// Package sim 是 L3 用例层：持有全部状态、按契约 §8 的管线推进 tick、
// 做不变量断言并产出快照。
//
// 契约 §8 的主循环（12 步）与架构 M1–M3 修正的对应关系：
//
//	§8-1  利润率与开工率        → sim.step 的 settle 段（用上一 tick 的 EMA 做决策）
//	§8-2  总产出/投入/劳动成本   → produce 包（含短缺惩罚与自给农场）
//	§8-3  利润入现金池          → settle（按 G3/G4 分账给政府/资本）
//	§8-4  消费者购买            → consume 包（工资是唯一资金来源）
//	§8-5  建造力分配与进度       → build 包（G2 政府采购、G6 企业购买）
//	§8-6  完工                  → build 包
//	§8-7  低雇佣率缩编          → build 包
//	§8-8  E = D − S             → market 包
//	§8-9  RK4 价格积分           → market 包
//	§8-10 AI 扩建订单           → ai 包（按 margin EMA 与 §4.1 规则）
//	§8-11 人口更新              → demography
//	§8-12 触发式重算 P_cost      → calibrate（工资/配方变动超阈值时）
//
// M1 修正：价格结算（§8-8/9）提前到消费之前，使消费者用本 tick 价格购买。
// M2 修正：显式引入工资池，工资在同一 tick 全部支出（§5「全部工资用于消费」）。
// M3 修正：雇佣/扩建决策用 n−1 的 margin EMA，消除内生性。
package sim

import (
	"fmt"

	"yehenala/market/internal/book"
	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/cohort"
	"yehenala/market/internal/consume"
	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/ledger"
	"yehenala/market/internal/market"
	"yehenala/market/internal/model"
)

// BuildingState 是一类建筑的动态状态。
type BuildingState struct {
	// Spec 是静态定义。
	Spec model.Building
	// Acc 是该类建筑现金池在共享审计账本中的账户（§4.5.3 修订）。
	//
	// 余额不再由本结构自行持有——只有一份，读自 State.Aud。
	Acc ledger.Account
	// Level 是等级（唯一的规模状态，§3.1 领域模型）。
	Level float64
	// GovLevel / PrivLevel 是按所有权拆分的等级（G3）。
	GovLevel, PrivLevel float64
	// HireRate 是实际雇佣 / 满编，∈[0,1]（§5）。
	HireRate float64
	// IdleTicks 是雇佣率低于 75% 的连续 tick 数（§4.4）。
	IdleTicks int
	// MarginEMA 是利润率的指数移动平均（架构 D13，抑制抖动）。
	MarginEMA float64
	// LastMargin 是上一 tick 的即时利润率，用于报告。
	LastMargin float64
	// LastProfit 是上一 tick 的利润额。
	LastProfit float64
	// LastRevenue 是上一 tick 的销售收入（已扣除交易税）。
	LastRevenue float64
}

// CashOf 返回该类建筑现金池的余额（读自共享审计账本）。
func (b *BuildingState) CashOf(a *ledger.Auditor) float64 { return a.Balance(b.Acc) }

// micro 实现旧接口（Cash.Balance 字段与 Cash.Add 方法），
// 但底层读写都落在共享审计账本上。
//
// 【为什么保留这层兼容层】
// step.go 里有数十处 Cash.Balance / Cash.Add 调用点。逐个改写成 ledger 交易
// 是大改动、且容易在改写过程中再次引入记账错误。本层让：
//
//	读 → 一律读审计账本（余额只有一份，不可能两处不一致）
//	写 → 一律走 ledger.Txn 过账（保留可审计轨迹）
//
// 但【写】仍然是单边分录，不保证借贷相等。真正的收敛路径是逐步把每个
// 调用点替换为 ledger 包里对应的交易构造函数（Wage / ConsumerPurchase /
// Intermediate / PowerPurchase / ...），那些函数保证借贷相等。
// 替换进度见 docs/CHANGES-1.0.md。
type micro struct {
	aud *ledger.Auditor
	acc ledger.Account
}

// Balance 返回余额。
func (m micro) Balance() float64 { return m.aud.Balance(m.acc) }

// Add 过账一笔单边分录。
func (m micro) Add(delta float64) {
	if delta == 0 {
		return
	}
	t := &ledger.Txn{Name: "单边分录（待收敛为 ledger 交易）"}
	if delta > 0 {
		t.Credit(m.acc, delta)
	} else {
		t.Debit(m.acc, -delta)
	}
	m.aud.PostInjection(t)
}

// SetInitial 设定开局余额。
func (m micro) SetInitial(v float64) { m.aud.SetBalance(m.acc, v) }

// cashAcc 是账户视图的聚合，供 State 以 s.GovCash() 这类形式取用。
type cashAcc struct{}

// BuildingCash 返回建筑 i 的现金视图。
func (s *State) BuildingCash(i int) micro {
	return micro{aud: s.Aud, acc: ledger.Building(i)}
}

// GovCash 返回政府现金视图。
func (s *State) GovCash() micro { return micro{aud: s.Aud, acc: ledger.Gov()} }

// CapCash 返回资本现金视图。
func (s *State) CapCash() micro { return micro{aud: s.Aud, acc: ledger.Capital()} }

// HouseCash 返回人群池 i 的现金视图。
func (s *State) HouseCash(i int) micro {
	return micro{aud: s.Aud, acc: s.Houses.Account(i)}
}

// Order 是一个施工订单（§4.2）。
type Order struct {
	// BuildingIndex 是目标建筑类别。
	BuildingIndex int
	// Units 是本次扩建的等级数。
	Units float64
	// Progress 是已投入的建造力。
	Progress float64
	// Payer 说明出资方：'gov' / 'capital' / 'firm'。
	Payer string
}

// State 是仿真的全部可变状态。
type State struct {
	Goods     []model.Good
	Buildings []BuildingState
	Market    *market.State

	// Aud 是【唯一记账账本】（§4.5.3 修订）。
	//
	// 政府、资本、建筑、人群四类主体的全部余额都存放在这里，各结构只持有
	// 账户标识。任何资金流动都必须以一笔借贷相等的交易过账，
	// 因此"货币守恒"是构造性事实，不需要靠事后审计去追残差。
	//
	// 唯一例外是新建建筑的营运本金（§4.3），它走 PostInjection，
	// 是本系统允许的唯一货币注入。
	Aud *ledger.Auditor

	Gov fiscal.Government
	Cap fiscal.Capital

	// Houses 是人群现金池账本（§5.1 修订）。
	//
	//	每个劳动场地（每类建筑）× 每个阶级 = 一个独立现金池（3 × 12 = 36 个）。
	//
	// 工资从 BuildingState.Cash 实际划转到这些池，消费从这些池实际扣款。
	// 修订前居民没有任何账户，工资只作为成本从利润里扣减而从未付出，
	// 导致货币每 tick 净损 15%~34%（见 docs/AUDIT-1.0.md §3）。
	Houses *cohort.Ledger

	Params model.Params

	// Population 是总人口（§6.5）。
	Population float64
	// WealthTier 是当前财富档（由平均工资插值，§6.3）。
	WealthTier float64

	Orders []Order

	// Tick 是当前周期数。
	Tick int64

	// 诊断累计量
	BlockedBuilds int64
	ClampEvents   int64

	// Last 是上一 tick 的消费结算结果，供报告使用。
	Last consume.PooledOutcome

	// demandScale 是三表联合标定系数（§6.3 需求量表的整体缩放）。
	// 它由 calibrate 解出：工资与需求都随人口线性缩放，故必须缩放需求侧
	// 才能让"居民税后工资 = 最终需求价值"成立。详见 calibrate.jointDemandScale。
	demandScale float64

	// calibration 保存标定结果，供报告输出与触发式重算 P_cost 使用。
	calibration *calibrate.Result

	// Flow 累计本 tick 的资金流，供诊断"政府现金池为何被砸穿"。
	//
	// 只看政府现金池余额无法区分亏损来自运营、采购还是建设支出，
	// 因此把每一笔流向拆开记录，这是定位建造力财政黑洞是否真被补上的唯一可靠手段。
	Flow FlowDiag

	// powerPriceNow 是本 tick 的建造力价格，供资金流诊断使用。
	powerPriceNow float64

	// powerSpendActual 是政府采购建造力【实际流出政府现金池】的金额。
	//
	// 必须用账户前后差计量，不能写成 PowerPurchased×price：
	// 后者在"政府资金不足、只买下部分产出"时会与实际扣款不符
	// （实测残差恰为未买下部分的税额），因为 PurchasePower 内部按
	// AvailableCash 裁剪了采购量，而裁剪发生在价格口径转换之后。
	powerSpendActual float64

	// privatizeUnits / privatizePaid 是本 tick 私有化的股权腿与现金腿
	// （§4.5.1 修订）。每 tick 开头清零；Snapshot 从这里读取。
	privatizeUnits float64
	// privatizePriceSeen / privatizePriceTick 记录本 tick 最后一笔私有化的
	// 每级对价与所用建造力价格，供测试对账（§4.5.1 修订）。
	privatizePriceSeen float64
	privatizePriceTick float64
	privatizePaid  float64

	// 政府池资金流分解的三路【账户实际变动】（每 tick 开头清零）。
	//
	// 全部用"钱真的动了多少"计量，不用公式推算——这样
	// "政府池 Δ = 各路之和"按构造成立，留下的残差一定是真正的未知资金流。
	flowGovOperatingDelta float64
	flowGovBuildoutDelta  float64
	// flowGovSaleDelta 是售力给外部付款方的含税入账（实际）。
	flowGovSaleDelta float64
	// flowGovPurchaseSelfTax 是政府采购建造力里"政府收自己的税"。
	flowGovPurchaseSelfTax float64
	// flowGovBuildoutSelfTax 是政府自建付款里"政府收自己的税"。
	//
	// 它计入 GovTax，但不是政府池的净流入（买方与卖方都在政府账内），
	// 故在政府池的资金流分解式中必须单独扣除，否则同一笔钱算两遍。
	flowGovBuildoutSelfTax float64

	// tickNewCapital 累计本 tick 因新建建筑完工而注入的营运本金（§4.3）。
	// 每 tick 开头清零；它是货币守恒审计中唯一允许的 Δ 来源。
	tickNewCapital float64
	// totalNewCapital 是从开局起累计注入的营运本金（诊断用）。
	totalNewCapital float64

	// bk 是统一记账簿（包装同一个审计账本）。
	bk *book.Book

	// reconBefore 是本 tick 开头各建筑现金池余额的快照（诊断用）。
	reconBefore []float64

	// TickRecon 是本 tick 四个货币持有池的 Δ 快照（诊断用）。
	TickRecon *TickRecon

	// InvariantErr 是本 tick 违反应收不变量时的错误（nil 表示全部通过）。
	InvariantErr error

	// Recon 是本 tick 的建筑现金池对账明细（诊断用）。
	//
	// 建筑池的 Δ 必须能被下列各项完全解释，残差恒为 0：
	//
	//	Δ建筑 = 消费者收入 + 中间投入收入 + 建造力收入 + 经营留存
	//	      − 工资 − 中间投入付款(含税) − 建造力支出 − 金融区工资
	//
	// 残差非零即说明有一笔 Cash.Add 没有对手方（造币/销毁）。
	Recon Recon
}

// Post 以一笔借贷相等的交易过账。
//
// 借贷不等会返回错误并记入 Violations——记错账必须立刻暴露，
// 而不是留下一个需要事后审计去追的残差。
func (s *State) Post(t *ledger.Txn) {
	if s.Aud == nil {
		return
	}
	before := s.Aud.Total()
	if err := s.Aud.Post(t); err != nil {
		s.InvariantErr = err
	}
	// 【守门】任何借贷相等的交易都不得改变货币总量。
	// 这条断言把"守恒"从"事后审计"变成"过账时即刻失败"，
	// 一旦有交易漏记借方或贷方，出错位置就是这里。
	if after := s.Aud.Total(); after != before {
		s.InvariantErr = fmt.Errorf("记账簿：交易「%s」改变了货币总量 %.6f → %.6f（Δ=%.6f）",
			t.Name, before, after, after-before)
	}
}

// PostInjection 过账一笔货币注入（唯一允许借贷不等的入口，§4.3 营运本金）。
func (s *State) PostInjection(t *ledger.Txn) {
	if s.Aud == nil {
		return
	}
	s.Aud.PostInjection(t)
}

// Bk 是统一记账簿（book.Book），包住同一个审计账本。
//
// 所有资金流动都应通过它：book 里每一类流动只有一个方法、借贷两侧写在同一处，
// 因此"漏记一半"在结构上不可能发生。
//
// 【接入状态】sim 已在 New 里建立 bk 并共用同一审计账本；
// step.go 中已完成工资与消费两路向 book 的迁移，其余成对单边分录待收敛。
func (s *State) Bk() *book.Book { return s.bk }

// ===== 余额读取的便捷访问器 =====
//
// 全部读自同一个审计账本，故不存在"两处余额不一致"的可能。

// bal 返回某类建筑现金池的余额。
func (s *State) bal(i int) float64 { return s.Aud.Balance(ledger.Building(i)) }

// balGov 返回政府现金池余额。
func (s *State) balGov() float64 { return s.Aud.Balance(ledger.Gov()) }

// balCap 返回资本现金池余额。
func (s *State) balCap() float64 { return s.Aud.Balance(ledger.Capital()) }

// balHouse 返回某人群池余额。
func (s *State) balHouse(i int) float64 {
	return s.Aud.Balance(s.Houses.Account(i))
}

// balBuildingTotal 返回全部建筑现金池之和。
func (s *State) balBuildingTotal() float64 { return s.Aud.TotalOf(ledger.KindBuilding) }

// TickNewCapitalTotal 返回累计注入的营运本金（§4.3，唯一合法的货币注入）。
func (s *State) TickNewCapitalTotal() float64 { return s.totalNewCapital }

// TotalLevels 返回全部建筑的等级之和。
func (s *State) TotalLevels() float64 {
	var t float64
	for i := range s.Buildings {
		t += s.Buildings[i].Level
	}
	return t
}

// TickRecon 记录一个 tick 内四个货币持有池的实际 Δ 与应有 Δ。
//
// 用途：定位货币守恒缺口【落在哪一个池】。
// 四者残差之和必须等于总货币的实际 Δ；残差非零的池即为漏点所在。
type TickRecon struct {
	GovDelta, CapDelta, HouseDelta, BuildDelta float64
	BuildExpected                              float64
}

// TotalDelta 返回四池实际 Δ 之和（即总货币的真实变化）。
func (t *TickRecon) TotalDelta() float64 {
	return t.GovDelta + t.CapDelta + t.HouseDelta + t.BuildDelta
}

// Recon 是建筑现金池的对账明细。
type Recon struct {
	ConsumerRevenue float64
	IntermediateIn  float64
	PowerRevenue    float64
	RetainedProfit  float64
	FinanceProfit   float64
	WagePaid        float64
	IntermediateOut float64
	PowerSpend      float64
	NewCapital      float64

	// ProfitLegTotal 是利润划分对【全部建筑现金池】的净影响合计
	// （= Σ(留存份额 − 全额利润)，含金融区那一腿）。
	//
	// 它与 RetainedProfit 的区别：RetainedProfit 记的是"划分给建筑的留存份额"
	// （管理口径），而现金池实际发生的是"借全额、贷留存"两腿相抵后的净额
	// （记账口径）。对账必须用后者，否则利润为负时会差 2×|利润| 量级。
	ProfitLegTotal float64

	// actual 是本 tick 建筑池的实际 Δ，由 step 在 tick 末填入。
	actual float64

	// 逐建筑明细（诊断用）：每个建筑各自的 Δ 与已解释项。
	//
	// 建筑池的对账容易在"某类建筑少记一笔"时整体差额被其他建筑掩盖，
	// 故必须能逐项下钻。ByBuilding[i] 的 Residual 非零即指出精确的漏点。
	ByBuilding []BuildingRecon
}

// BuildingRecon 是单个建筑的对账明细。
type BuildingRecon struct {
	// Actual 是该建筑现金池的实际 Δ。
	Actual float64
	// Retained 是利润划分给该建筑的留存份额（正值 = 贷方份额）。
	//
	// 【注意它不是建筑池的净腿】book.ProfitSplit 的记账是
	// 「借 建筑[i] 全额利润、贷 建筑[i] 留存份额」，故建筑池的净腿恰为
	// `留存 − 利润 = −(政府份额 + 资本份额)`，由 ProfitLeg 单独记录。
	// 用 Retained 直接当净腿是错的（利润为负时符号会反）。
	Retained float64
	// ProfitLeg 是利润划分对【该建筑现金池】的净影响（= 留存 − 利润）。
	//
	// 之所以必须单列：Retained 只是三份额之一，而现金池实际发生的是
	// 借全额、贷留存两腿相抵后的净额。把 Retained 当净额会让逐建筑对账
	// 在利润为负时出现 2×|利润| 量级的假残差（实测 213 万）。
	ProfitLeg float64
	// Wage 是工资支出（负向）。
	Wage float64
	// InputOut 是中间投入付款含税（负向）。
	InputOut float64
	// InputIn 是作为卖方收到的中间投入货款（正向）。
	InputIn float64
	// ConsumerIn 是作为卖方收到的消费者货款（正向）。
	ConsumerIn float64
	// PowerNet 是建造力买卖的净额（正向为收入）。
	PowerNet float64
	// NewCapital 是新完工建筑的营运本金注入。
	NewCapital float64
}

// Expected 返回该建筑按明细算出的应有 Δ。
//
// 【口径】逐条列出该建筑现金池本 tick 实际发生的资金腿：
//
//	+ 消费收入、中间投入收入、建造力净额、新资本（唯一注入）、利润划分净腿
//	− 工资、中间投入付款（含税）
//
// 其中利润划分净腿是 ProfitLeg（= 留存 − 利润），**不是** Retained——
// 记账用的是"借全额利润、贷留存份额"两条腿，见 BuildingRecon.ProfitLeg 的说明。
func (b BuildingRecon) Expected() float64 {
	return b.ConsumerIn + b.InputIn + b.PowerNet + b.NewCapital + b.ProfitLeg -
		b.Wage - b.InputOut
}

// Residual 返回实际 Δ 与应有 Δ 的残差。
func (b BuildingRecon) Residual() float64 { return b.Actual - b.Expected() }

// ActualDelta 返回本 tick 建筑池的实际 Δ。
func (r Recon) ActualDelta() float64 { return r.actual }

// Residual 返回实际 Δ 与应有 Δ 的残差。恒为 0 才说明没有未记账的资金流。
func (r Recon) Residual() float64 { return r.actual - r.ExpectedDelta() }

// ExpectedDelta 返回按对账明细计算出的建筑池应有 Δ。
//
// 【口径】汇总层面同样逐条列出实际资金腿：
//
//	+ 消费收入 + 中间投入收入 + 建造力收入(净) + 新资本 + 利润划分净腿
//	− 工资 − 中间投入付款(含税) − 建造力支出(净)
//
// 【单一真相来源】这里的每一项都必须由【同一个循环】写入，不得另起口径。
// 历史上这里踩过两次坑：
//
//  1. 汇总的 RetainedProfit 只累加了 11 类非金融建筑（金融区在循环里被 continue
//     跳过），却又把 FinanceProfit 当独立项加了一次 —— 两处口径不一致，
//     残差恰等于金融区净额。
//  2. 把 RetainedProfit 直接当"现金池净腿"用 —— 但记账是"借全额利润、贷留存份额"，
//     净腿是 留存 − 利润。利润为负时符号相反，残差达 2×|利润| 量级（实测 213 万），
//     而全局的货币守恒与借贷相等审计却全部通过。
//
// 因此汇总使用 ProfitLegTotal（记账口径），RetainedProfit 只作管理口径的报表字段。
func (r Recon) ExpectedDelta() float64 {
	return r.ConsumerRevenue + r.IntermediateIn + r.PowerRevenue +
		r.ProfitLegTotal + r.NewCapital -
		r.WagePaid - r.IntermediateOut - r.PowerSpend
}

// FlowDiag 记录一个 tick 内的资金流分解。
type FlowDiag struct {
	// GovOperating 是政府建筑的经营净额（可为负）。
	GovOperating float64
	// GovTax 是税收。
	GovTax float64
	// GovPowerSpend 是政府采购建造力的支出。
	GovPowerSpend float64
	// GovPowerRevenue 是政府售出建造力的【含税】入账总额（净额 + 税额）。
	//
	// 用含税口径的原因：税额由步骤 ⑨ 从付款方另行扣除后同时计入政府池，
	// 故资金流分解式必须与账户实际变动一致，否则会留下净额·t 的残差。
	GovPowerRevenue float64
	// GovSelfTax 是"政府收自己的税"（政府采购与自建付款中的税额）。
	//
	// 它计入 GovTax，却【不是政府池的净流入】（买方与卖方都在政府账内），
	// 故政府池的资金流分解式必须写成
	//
	//	Δ政府 = (GovTax − GovSelfTax) + GovOperating + GovPowerRevenue
	//	        − GovPowerSpend − GovBuildoutPaid
	//
	// 少了这个扣除，同一笔税会被算两遍，留下 Net·t 量级的假残差。
	GovSelfTax float64
	// CapitalProfit 是私有建筑的运营纯利合计（可为负）。
	CapitalProfit float64
	// WageTotal 是全社会工资。
	WageTotal float64
	// SpendNet 是消费者税前支出。
	SpendNet float64
	// PowerOut 是建造部门当期产出。
	PowerOut float64
	// PowerBought 是政府采购量，PowerSold 是售出量。
	PowerBought, PowerSold float64
	// WorstSector / WorstMargin 是本期利润率最低的部门。
	WorstSector int
	WorstMargin float64
	// BestSector / BestMargin 是本期利润率最高的部门。
	BestSector int
	BestMargin float64

	// ===== §4.5.3 修订新增：全过程交易税与人群池的诊断字段 =====
	//
	// 这些字段的作用是让政府现金池的变化能被【完整的】已知资金流解释。
	// 修订前的分解漏掉了"非政府付款方购买建造力的税"与"政府自有项目付款"，
	// 使残差不为零——残差非零即意味着存在未记账的货币创造/销毁。
	//
	// 恒等式（供 TestGovCashFlowIsFullyExplained 断言）：
	//
	//	ΔGovCash = GovTax + GovOperating + GovPowerRevenue − GovPowerSpend
	//	         − GovBuildoutPaid

	// ConsumerTax 是消费环节收取的交易税（已含在 GovTax 内，单列供核对）。
	ConsumerTax float64
	// InputNet / InputTax 是中间投入环节的货款净额与税额。
	InputNet, InputTax float64
	// PowerPurchaseTax 是政府采购建造力时收取的交易税（资金不离开政府，
	// 故不进入 ΔGovCash，但属于税收，必须计入 GovTax 以保持税基口径完整）。
	PowerPurchaseTax float64
	// PowerSaleTax 是向扩建方售出建造力时收取的交易税（资金留在政府）。
	PowerSaleTax float64
	// GovBuildoutPaid 是政府为【自有项目】支付的建造力货款。
	// 这是内部转账，但确实减少政府现金池，必须出现在分解式里。
	GovBuildoutPaid float64
	// HouseCash 是期末人群现金池总额（居民的货币存量）。
	HouseCash float64
	// Sat 是按人口加权的四组满足度（§6.5 人口增长的输入）。
	Sat [4]float64

	// ===== §4.5.1 修订：私有化诊断字段 =====

	// PrivatizeEnabled 是本次运行的私有化总开关状态。
	PrivatizeEnabled bool
	// PrivatizeUnits 是本 tick 转让的等级数（股权腿）。
	PrivatizeUnits float64
	// PrivatizePaid 是本 tick 支付的对价（现金腿，含税）。
	PrivatizePaid float64
	// GovShareAfter 是期末政府持股比例（按级数加权，诊断用）。
	//
	// 它的下降速度直接反映私有化强度；配合逐建筑的 AllowPrivatize，
	// 可以观察"哪些部门被市场接手"。
	GovShareAfter float64
}

// TotalMoney 返回全社会货币存量（建筑 + 政府 + 资本 + 人群四类账户之和）。
//
// §4.5.3 要求的硬不变量是：任何 tick 之后本值必须保持不变（新建筑营运本金除外）。
// 这是判断"是否存在未记账的货币创造/销毁"的唯一直接手段。
func (s *State) TotalMoney() float64 {
	if s.Aud == nil {
		return 0
	}
	return s.Aud.Total()
}

// powerGoodIndex 是"建造力"的商品下标，与 fiscal.PowerGoodIndex 一致。
// 在此重复声明是为了让 sim 包在布点逻辑里不依赖 fiscal 包的语义常量。
const powerGoodIndex = 10

// Calibration 返回本次运行的标定结果（只读用途）。
func (s *State) Calibration() *calibrate.Result { return s.calibration }

// DemandScale 返回三表联合标定系数。
func (s *State) DemandScale() float64 { return s.demandScale }

// Options 是构造仿真的输入。
type Options struct {
	// Population 是开局人口。注意：它不是自由参数——工资与需求同比缩放，
	// 故人口只决定经济体的绝对规模，不改变守恒比例（见 calibrate.jointDemandScale）。
	Population float64
	// WealthTier 是 §6.3 需求量表所取的财富档。
	WealthTier float64
	// DemandScale 是三表联合标定系数；0 表示采用 calibrate 的解。
	DemandScale float64
	// FinanceLaborPerLevel 是金融区每级雇佣人数（G5，默认 1000）。
	FinanceLaborPerLevel float64
	// FinanceBuildCost 是金融区建造成本（建造力）。
	FinanceBuildCost float64
	// GovStartupFraction 是政府现金池初值占债务上限的比例（G7）。
	//
	// 债务机制下不能用"若干个周期的税收"来定初始货币——那会超出债务上限，
	// 使上限形同虚设（见 New 中的说明）。默认 0.5，即政府起步时用掉一半举债空间。
	GovStartupFraction float64
	// SubsistenceScale 覆盖默认的自给农场规模；nil 表示用 model.DefaultParams 的值
	// （契约/架构写定的 0.05）。
	// 之所以用指针，是为了区分"未设置"与"显式设为 0"。
	SubsistenceScale *float64
	// InitialPowerLevel 是建造部门的起步等级。
	//
	// 【这是本工程的建模选择，不是契约参数】：契约只规定"建造力每级产出 15、
	// 建造成本 100"，没有规定开局建筑数量。由于 1 级建造部门仅产 15 建造力/tick，
	// 而 1 级普通建筑平均要 600 建造力，起步过小会让任何扩建都被回本周期卡住。
	// 默认 20 级（= 300 建造力/tick），可用 -power-init 覆盖为契约的最小值 1。
	InitialPowerLevel float64

	// ProductionInitLevel 覆盖每种生产建筑的起始等级（契约修订：默认 5）。
	//
	// 它必须在【布点阶段】生效，故走 Options 而不是运行期改 Params——
	// 局点已定后再改参数是不会回算开局等级的。
	//
	// 【零值语义】本字段用"负数表示不改动默认"作哨兵（与 SubsistenceScale 用
	// 指针同理，这里用一个不可能取到的负值即可）：
	//
	//	< 0（如 -1，命令行默认）  保持 Params 的契约默认值 5
	//	= 0                      物质平衡布点（按中间消耗 + 最终需求解算）
	//	> 0                      每种生产建筑统一取该等级
	//
	// 之所以不能沿用"0 表示不覆盖"：那样命令行就【永远无法】选中物质平衡布点，
	// 而它正是"统一等级布点偏离多少"这一对照实验的另一臂。
	ProductionInitLevel float64
}

// New 构造一个完成标定与开局布点的仿真状态。
func New(opt Options) (*State, error) {
	goods := model.GoodSpecs()
	if err := model.ValidateGoods(goods); err != nil {
		return nil, err
	}
	p := model.DefaultParams()
	if opt.SubsistenceScale != nil {
		p.SubsistenceScale = *opt.SubsistenceScale
	}
	if opt.InitialPowerLevel > 0 {
		p.InitialPowerLevel = opt.InitialPowerLevel
	}
	if opt.ProductionInitLevel >= 0 {
		p.ProductionInitLevel = opt.ProductionInitLevel
	}
	specs := model.BuildingSpecs(opt.FinanceLaborPerLevel, opt.FinanceBuildCost)

	cal, err := calibrate.Run(specs, 1.0/6.0)
	if err != nil {
		return nil, fmt.Errorf("标定失败: %w", err)
	}
	if opt.DemandScale > 0 {
		cal.DemandScale = opt.DemandScale
	}

	st := &State{
		Goods:       goods,
		Params:      p,
		Population:  opt.Population,
		WealthTier:  opt.WealthTier,
		Market:      market.NewState(goods, p),
		demandScale: cal.DemandScale,
		calibration: cal,
	}
	st.Buildings = make([]BuildingState, len(specs))
	for i, sp := range specs {
		st.Buildings[i] = BuildingState{Spec: sp, HireRate: 1.0, MarginEMA: 0.2}
	}

	// 开局布点：由 §6.3 的最终需求经 Leontief 完全需求反推每种建筑的级数。
	if err := st.initialLayout(cal); err != nil {
		return nil, err
	}

	// 标定需求常数 a = S₀·(Pinit/Pcost)^ε（§3.4 步骤 3）。
	// 注意 S₀ 必须是【净供给】Y − A·Y，而不是总产出——否则会把中间投入重复算作可售量。
	net := st.netSupply(cal)
	for i := range st.Goods {
		mk := &st.Market.Markets[i]
		mk.A = calibrate.DemandConstant(goods[i], net[i])
	}

	// 【唯一记账账本】（§4.5.3 修订）
	//
	// 所有主体的余额都存放在这里；Gov / Cap / Houses / Buildings 只持有账户标识。
	// 必须在任何注资之前建立，否则各池读不到余额。
	st.Aud = ledger.NewAuditor()
	// 统一记账簿与审计账本共用同一实例——这是"只有一份余额"的落地处。
	st.bk = &book.Book{
		Aud:        st.Aud,
		Buildings:  len(specs),
		Classes:    cohort.ClassCount,
		PowerIdx:   powerGoodIndex,
		FinanceIdx: model.FinanceIndex,
	}
	st.Gov = fiscal.Government{Cash: fiscal.NewLedger(st.Aud, ledger.Gov())}
	st.Cap = fiscal.Capital{Cash: fiscal.NewLedger(st.Aud, ledger.Capital())}
	for i := range st.Buildings {
		st.Buildings[i].Acc = ledger.Building(i)
	}

	// 债务上限的资产基数依赖"建造力产出"，必须在开局就填好。
	//
	// 【这是一个真实 bug 的修复】旧实现只在 step 的采购步骤里设置
	// s.Gov.PowerOutput，而 New 返回后到第一个 tick 之间它为 0，
	// 于是 DebtCap = 0、AvailableCash = 0，政府的任何主动支出都被拒绝
	// （实测"债务触限 tick 数 = 59/60"）。开局状态必须自洽。
	//
	// 注意必须放在 st.Gov 被赋值【之后】——否则会被新构造的零值覆盖。
	st.Gov.PowerOutput = st.Buildings[powerGoodIndex].Level * specs[powerGoodIndex].Recipe.Qty

	// 人群现金池账本（§5.1 修订）。初始现金为 0——居民的第一次收入来自第一期工资；
	// 给初始现金等于凭空注入一笔没有来源的货币，违反 §4.5.3 的货币守恒。
	st.Houses = cohort.NewLedger(st.Aud, len(specs))

	// 初始货币存量：必须由【流量】导出，而不是取任意数值。
	//
	// 修订前初始货币总量 = 5,075,000，而一个 tick 的工资总额 = 24,444,555
	// （实测，见 out/sim_v1.log）—— 货币存量只有一周工资的 1/25。
	// 在这样的存量下，"工资实际支付"根本不可能发生，货币每 tick 被摧毁 15%~34%。
	// 这正是旧实现把工资做成"只扣成本、不付出"的隐性原因。
	//
	// 修订后按【一周工资总额】给存量，并分配到三个持有主体：
	//
	//	政府现金池  = 0.50 × 周工资   （G7 的举债空间之外的起步流动性，§4.5.4）
	//	资本现金池  = 0.25 × 周工资   （G5 金融区的营运资金）
	//	建筑现金池  = 1.00 × 周工资   （按级数均分，§4.3）
	//
	// 合计 = 1.75 × 周工资，即全社会的货币存量约为 1.75 周的工资流量。
	// 这个量级是"工资能真实付出、且货币周转速度合理"的最低要求。
	wageBill := st.wageBillNow()
	st.Gov.Cash.SetInitial(startupGovFraction * wageBill)
	st.Cap.Cash.SetInitial(startupCapFraction * wageBill)
	// 建筑现金池按【基数 + 按级数】分摊。
	//
	// 不能只按级数分摊：建造部门默认只有 20 级，而它要垫付钢/铁/工具的
	// 中间投入（每级 25×P钢 + 25×P铁 + 20×P工具 ≈ 8.8 万），
	// 按级数分到的钱不足以做第一笔采购，会立刻透支并让整条扩建链断掉。
	const firmBasePerType = 0.05 // 每类建筑的基数，单位为"周工资"
	firmStock := startupFirmFraction * wageBill
	base := firmBasePerType * wageBill
	perLevelPool := firmStock - base*float64(len(st.Buildings))
	if perLevelPool < 0 {
		perLevelPool = 0
	}
	totalLevels := maxLevels(st.Buildings)
	for i := range st.Buildings {
		share := base
		if totalLevels > 0 {
			share += perLevelPool * st.Buildings[i].Level / totalLevels
		}
		st.Aud.SetBalance(ledger.Building(i), share)
	}
	return st, nil
}

// 初始货币存量的分配比例（相对"一周工资总额"）。
//
// 这三个常数是【建模选择，不是契约参数】：契约只规定 §4.3 的"每级建筑初始
// 现金池 5,000 元"，但那个数值与 §5 的工资表不在同一量纲上（周工资 ≈ 24.4e6，
// 而 5,000 元/级 × 836 级 = 4.18e6）。修订后的契约 §4.3 改为按流量标定，
// 这三个比例是它的实现默认值，可调。
const (
	startupGovFraction = 0.50
	// startupCapFraction 是资本（金融区）现金池的初始规模，单位为"周工资"。
	startupCapFraction = 0.25
	// startupFirmFraction 是建筑现金池总规模。
	startupFirmFraction = 1.00
)

// maxLevels 返回建筑总级数（用于把建筑现金池按规模分摊）。
func maxLevels(bs []BuildingState) float64 {
	var s float64
	for i := range bs {
		s += bs[i].Level
	}
	if s <= 1e-9 {
		return 1
	}
	return s
}

// wageBillNow 返回当前布点下【一个 tick 的工资总额】（满编口径）。
//
// 这是初始货币存量的基准流量。用满编而非实际雇佣率，是为了让存量在
// 开局就足以覆盖一次完整发薪；雇佣率低于满编时只会更宽松。
func (s *State) wageBillNow() float64 {
	var total float64
	for i := range s.Buildings {
		b := &s.Buildings[i]
		total += b.Level * b.Spec.LaborPerLevel * model.AverageWage()
	}
	if total <= 0 {
		// 兜底：布点为空时至少给一个正存量，避免零货币的死局。
		total = 1e6
	}
	return total
}

// initialLayout 按【标定后的需求曲线】布点，而不是按完全需求。
//
// 为什么必须这样（这是本工程最容易搞错、且会导致整局崩解的一步）：
//
//	§3.4 步骤 3 把需求曲线的归一化常数锚在"开局净供给"上：a = S₀·(Pinit/Pcost)^ε。
//	于是 t=0 时 D(Pinit) = S₀ 恒成立——价格方程处于静止点。
//	但 P* = P₀·(a/S)^(1/ε) 表明：只要实际产出 S 偏离 S₀，价格就会移动。
//
//	若按"完全需求 (I−A)⁻¹f"布点，得到的产能与 S₀ 无关，二者一般不等。
//	后果是开局即有大量部门产能过剩 → 价格崩向地板 → 这些部门长期亏损 →
//	政府（持有 70% 的亏损部门）现金池被砸穿 → 建造力采购归零 →
//	建造部门失去唯一买家 → 全经济崩解。
//
//	正确做法：让每种商品的产能恰好等于其需求曲线的要求值 D_i(Pinit) = a_i。
//	由于 a_i 就是由 S₀ 标定的，这等价于令开局产出与售价刚好出清。
//
// 实现上取"人均净供给"，再乘以人口得总量。
func (s *State) initialLayout(cal *calibrate.Result) error {
	// ── 第 1 步：最终需求（消费者侧）────────────────────────────
	//
	// fPer 是每 1 人每 tick 的最终需求（§6.2/§6.3），乘人口得全社会最终需求量。
	// 注意这是【净】需求：它不含产业链自身的中间消耗。
	fPer := calibrate.PerCapitaFinalDemand(s.WealthTier, cal.DemandScale)
	finalDemand := make([]float64, model.Goods)
	for i := range finalDemand {
		finalDemand[i] = fPer[i] * s.Population
	}

	// ── 第 2 步：求解建筑级数 ──────────────────────────────────
	//
	// 【为什么不能用 Leontief 完全需求反推】
	//
	// 旧实现取 Y = (I−A)⁻¹·f 作为总产出目标，再除以单级产出得级数。它给出的
	// 产业链级数是个位数（煤 0.98、铁 1.06、钢 0.74），而实际申报的中间投入
	// 是产出的 8~20 倍，于是开局就处于极端短缺：所有下游建筑的 shortageFactor
	// 被压到 0.12~0.17，净供给塌到需求的一小部分，经济从第 0 tick 起就不可能出清。
	//
	// 根因：Leontief 完全需求矩阵是按【价格方程】的单位需求系数构造的，
	// 而它把投入系数按"每单位产出"归一（A[i][j] = 投入量/产出量）。这对价格是对的，
	// 但用它反推【级数】时，各种商品的"单级产出量"差异极大（谷物 50、建造力 15），
	// 归一化后的系数会严重低估高投入比部门的实际消耗：
	// 建造部门每级要 25 钢 + 25 铁 + 20 工具，而单级只产 15 建造力——
	// 20 级建造部门就要吞掉 500 单位铁，远超按需求反推出的 63 单位。
	//
	// 正确的求解方式是【直接按级数做物质平衡】：
	//
	//	每种商品的级数 L_i 必须满足：q_i·L_i = 最终需求_i + Σ_j c_ij·L_j
	//
	// 其中 c_ij 是建筑 i 每级对商品 j 的消耗量（§3.3 配方直接给出，
	// 不经过任何归一化）。这是一个线性不动点问题，用迭代求解即可。
	//
	// 建造力是投资品而非中间投入（§3.3 配方里没有任何建筑消耗它），
	// 故它的级数由 Options.InitialPowerLevel 给定；反过来它对钢/铁/工具的
	// 消耗必须完整计入——旧实现恰恰漏掉了这一项最大的中间需求。

	levels := make([]float64, len(s.Buildings))
	output := make([]int, len(s.Buildings))
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsFinance {
			output[i] = -1
			continue
		}
		output[i] = s.Buildings[i].Spec.Recipe.Output
	}
	// 建造部门先按起步规模占位，使它的投入需求进入迭代。
	powerIdx := powerGoodIndex
	levels[powerIdx] = s.Params.InitialPowerLevel
	if levels[powerIdx] < 1 {
		levels[powerIdx] = 1
	}

	for pass := 0; pass < 200; pass++ {
		// 各商品被中间投入消耗的总量
		used := make([]float64, model.Goods)
		for j := range s.Buildings {
			if s.Buildings[j].Spec.IsFinance || levels[j] <= 0 {
				continue
			}
			for good, qty := range s.Buildings[j].Spec.Recipe.Inputs {
				used[good] += qty * levels[j]
			}
		}

		maxChange := 0.0
		for i := range s.Buildings {
			b := &s.Buildings[i]
			if b.Spec.IsFinance || i == powerIdx {
				continue
			}
			need := finalDemand[output[i]] + used[output[i]]
			want := need / b.Spec.Recipe.Qty
			if b.Spec.Cap > 0 && want > b.Spec.Cap {
				want = b.Spec.Cap
			}
			if want < 0.5 {
				want = 0.5
			}
			if d := absf(want - levels[i]); d > maxChange {
				maxChange = d
			}
			levels[i] = want
		}
		if maxChange < 1e-6 {
			break
		}
	}

	// ── 第 3 步：自给农场挤占专业耕地的处理 ──────────────────────
	//
	// §4.2 规定未利用耕地自动生成自给农场。自给产出先抵扣谷物的最终需求，
	// 再决定专业谷物农场与棉花种植园的级数；两者合计受耕地上限约束。
	//
	// 迭代一次即可：自给产出只依赖耕地占用，而耕地占用只依赖这两个级数。
	for pass := 0; pass < 50; pass++ {
		sub := s.subsistenceAtLevels(levels)
		grainIdx := output[grainBuildingIndex(s.Buildings)]
		changed := false
		for _, i := range []int{grainBuildingIndex(s.Buildings), cottonBuildingIndex(s.Buildings)} {
			if i < 0 {
				continue
			}
			b := &s.Buildings[i]
			g := b.Spec.Recipe.Output
			need := finalDemand[g]
			// 谷物的自给产出抵扣需求；织物不受自给影响（自给也产织物，一并抵扣）
			need -= sub[g]
			if need < 0 {
				need = 0
			}
			// 中间消耗
			for j := range s.Buildings {
				if s.Buildings[j].Spec.IsFinance {
					continue
				}
				if q, ok := s.Buildings[j].Spec.Recipe.Inputs[g]; ok {
					need += q * levels[j]
				}
			}
			want := need / b.Spec.Recipe.Qty
			if want < 0.5 {
				want = 0.5
			}
			if d := absf(want - levels[i]); d > 1e-9 {
				changed = true
				levels[i] = want
			}
		}
		_ = grainIdx
		if !changed {
			break
		}
	}

	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsFinance {
			continue
		}
		s.Buildings[i].Level = levels[i]
	}
	s.applyArableCap()

	// ── 第 3b 步：统一的起始等级（契约修订：每种生产建筑 10 级）────────
	//
	// 大于 0 时覆盖上面需求驱动解出的差异极大的级数
	// （棉花 0.94 级 vs 铁矿 16.5 级），使开局的资本有机构成可控。
	// 建造部门独立由 InitialPowerLevel 决定，因为它不是按需求建厂，
	// 而是按"扩建吞吐量"建厂。
	if s.Params.ProductionInitLevel > 0 {
		for i := range s.Buildings {
			b := &s.Buildings[i]
			if b.Spec.IsFinance || i == powerGoodIndex {
				continue
			}
			lv := s.Params.ProductionInitLevel
			if b.Spec.Cap > 0 && lv > b.Spec.Cap {
				lv = b.Spec.Cap
			}
			b.Level = lv
		}
	}

	// ── 第 4 步：金融区按掌控比布点（G5）──────────────────────
	var other float64
	for i := range s.Buildings {
		if !s.Buildings[i].Spec.IsFinance {
			other += s.Buildings[i].Level
		}
	}
	fin := &s.Buildings[model.FinanceIndex]
	fin.Level = other / s.Params.ControlPerFinance
	if fin.Level < 1 {
		fin.Level = 1
	}

	// ── 第 5 步：所有权拆分（G3）：默认政府占 70%，金融区全归私有 ──
	for i := range s.Buildings {
		share := 0.70
		if s.Buildings[i].Spec.IsFinance {
			share = 0
		}
		s.Buildings[i].GovLevel = s.Buildings[i].Level * share
		s.Buildings[i].PrivLevel = s.Buildings[i].Level - s.Buildings[i].GovLevel
	}
	return nil
}

// grainBuildingIndex / cottonBuildingIndex 返回谷物与棉花建筑的类别下标。
//
// 之所以要查而不是写死：谷物是 0 号商品、棉花（织物）是 2 号，
// 但建筑类别的排列依赖 GoodSpecs 的顺序，写死下标会在商品表变动时静默出错。
func grainBuildingIndex(bs []BuildingState) int { return buildingByOutput(bs, 0) }

func cottonBuildingIndex(bs []BuildingState) int { return buildingByOutput(bs, 2) }

func buildingByOutput(bs []BuildingState, good int) int {
	for i := range bs {
		if !bs[i].Spec.IsFinance && bs[i].Spec.Recipe.Output == good {
			return i
		}
	}
	return -1
}

func absf(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}

// subsistenceAtLevels 计算给定等级下的自给农场产出（不修改状态）。
func (s *State) subsistenceAtLevels(levels []float64) map[int]float64 {
	var used float64
	for i := range s.Buildings {
		if s.Buildings[i].Spec.LandKind == "arable" {
			used += levels[i]
		}
	}
	idle := s.Params.ArableCap - used
	if idle < 0 {
		idle = 0
	}
	return model.SubsistenceOutput(idle * s.Params.SubsistenceScale)
}

func (s *State) applyArableCap() {
	var used float64
	type arableRef struct{ idx int }
	var refs []arableRef
	for i := range s.Buildings {
		if s.Buildings[i].Spec.LandKind == "arable" {
			used += s.Buildings[i].Level
			refs = append(refs, arableRef{i})
		}
	}
	if used <= s.Params.ArableCap || used == 0 {
		return
	}
	k := s.Params.ArableCap / used
	for _, r := range refs {
		s.Buildings[r.idx].Level *= k
	}
}

// output 返回每种商品的总产出（含自给农场），不施加配给。
func (s *State) output() []float64 {
	out := make([]float64, model.Goods)
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if b.Spec.IsFinance {
			continue
		}
		out[b.Spec.Recipe.Output] += b.Level * b.Spec.Recipe.Qty * b.HireRate
	}
	for i, v := range s.subsistence() {
		out[i] += v
	}
	return out
}

// subsistence 返回自给农场的产出（§3.3/§4.2：未利用耕地自动生成，不耗劳动力、不发工资）。
func (s *State) subsistence() map[int]float64 {
	var used float64
	for i := range s.Buildings {
		if s.Buildings[i].Spec.LandKind == "arable" {
			used += s.Buildings[i].Level
		}
	}
	idle := s.Params.ArableCap - used
	if idle < 0 {
		idle = 0
	}
	return model.SubsistenceOutput(idle * s.Params.SubsistenceScale)
}

// netSupply 计算净供给 Y − A·Y（可售量），用于需求标定。
func (s *State) netSupply(cal *calibrate.Result) []float64 {
	y := s.output()
	net := make([]float64, model.Goods)
	for i := 0; i < model.Goods; i++ {
		var interm float64
		for j := 0; j < model.Goods; j++ {
			interm += cal.A[i][j] * y[j]
		}
		net[i] = y[i] - interm
		if net[i] < 1e-9 {
			net[i] = 1e-9
		}
	}
	return net
}

// Snapshot 是一次 tick 结束后的诊断快照（报告与 §8.4 判据的输入）。
type Snapshot struct {
	Tick           int64
	Prices         []float64
	PriceRatio     []float64
	Supply         []float64
	Demand         []float64
	Margins        []float64
	Levels         []float64
	GovCash        float64
	CapitalCash    float64
	HouseCash      float64
	Tax            float64
	PowerPurchased float64
	PowerSold      float64
	Population     float64
	Sat            [4]float64
	SpendNet       float64
	WageBill       float64
	OverdraftTick  int64
	// CashTotal 是全部现金池期末总额（§7 GDP 的存量口径）。
	//
	// 口径（契约 §7 修订后）：建筑现金池 + 政府现金池（取 max(0,·)）
	// + 金融区现金池 + 人群现金池（§5.1 修订新增）。
	CashTotal float64
	// TotalMoney 是全社会货币存量（政府现金池【按实际值】计入，可为负）。
	//
	// 与 CashTotal 的区别：CashTotal 是 GDP 口径（政府债务不计入），
	// TotalMoney 是货币守恒审计口径（债务是真实的负余额，必须计入）。
	// §4.5.3 要求 TotalMoney 逐 tick 恒定（仅 NewCapital 为合法注入）。
	TotalMoney float64
	// NewCapital 是本 tick 因新建建筑完工而注入的营运本金（§4.3）。
	//
	// 这是系统里【唯一】允许的货币创造，故货币守恒审计必须把它单独计量。
	NewCapital float64
	// GovDebt 是政府债务（= max(0, −政府现金池余额)）。
	GovDebt float64
	// GovDebtCap 是当期债务上限。
	GovDebtCap float64
	// GovPowerOutput 是建造部门当期产出，债务上限的资产基数来源。
	GovPowerOutput float64
	// PrivatizeUnits / PrivatizePaid / GovShareAfter 是私有化诊断（§4.5.1 修订）。
	PrivatizeUnits float64
	PrivatizePaid  float64
	GovShareAfter  float64
	// Flow 是本期的资金流分解，用于诊断政府现金池的变化来源。
	Flow FlowDiag
}
