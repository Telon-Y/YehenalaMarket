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
	"math"
	"sort"

	"yehenala/market/internal/book"
	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/cohort"
	"yehenala/market/internal/consume"
	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/ledger"
	"yehenala/market/internal/market"
	"yehenala/market/internal/model"
	"yehenala/market/internal/product"
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
	//
	// 【§4.5.7 校验条款】它是**含补贴**的口径：补贴计入本期收入后 EMA 上升，
	// 于是 §5.2 的增雇分支生效（受补贴建筑主动追求满员）。
	MarginEMA float64
	// ProfitEMA 是**不含补贴**的真实利润率 EMA（§4.5.7 的校验条款）。
	//
	// §4.1 的扩建判定必须用它，而不是 MarginEMA：
	// 否则"亏损建筑靠补贴把 margin 抬到 0 以上"会被扩建逻辑误读为盈利，
	// 用真金白银继续扩产（既是重复补贴，也违背"救急不救扩"的设计意图）。
	ProfitEMA float64
	// LastMargin 是上一 tick 的即时利润率（含补贴），用于报告。
	LastMargin float64
	// LastProfitMargin 是上一 tick 的即时利润率（不含补贴），用于报告。
	LastProfitMargin float64
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
// 替换进度见 docs/ACTIVE.md §七（R1–R37 逐轮记录）。
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
//
// 【队列语义（2026-09-19 修订）】s.Orders 是**持久队列**：订单跨 tick 存活，
// 每 tick 按 FIFO 推进 min(剩余需求, SitePowerLimit) 的建造力量，直到 Progress
// 达到 BuildCost × Units 才完工。
//
// 【顺带修正的缺陷】旧实现在创建订单时把当次投入一次性写进 Progress，之后
// 永远不再推进，于是**昂贵订单永远不完工**（剩余需求始终 > 0，而 Progress 不再增长）。
// 现在每 tick 真实累加 Progress，并在 Advance 里记录本 tick 的投入量。
type Order struct {
	// BuildingIndex 是目标建筑类别。
	BuildingIndex int
	// Units 是本次扩建的等级数。
	Units float64
	// Progress 是**累计**已投入的建造力。
	Progress float64
	// Advance 是本 tick 实际投入的建造力量（诊断与 §4.2 上限校验用）。
	Advance float64
	// Stack 是出资/立项的投资栈："manor"（庄园栈）或 "finance"（金融栈）。
	//
	// 【§4.5.1b】谁出资、谁拥有：庄园栈建成的等级归宅邸庄园，金融栈归金融区。
	// 政府不参与新增等级，故订单不再有 Payer。
	Stack string
	// PublicWorks 标记这是**政府公共工程**订单（§4.5.8，2026-09-19 第 15 轮裁决）。
	//
	// 公共工程与私人扩建的三点区别：
	//  1. 出资方是**国库**（直接采购建造力，不走投资池，也没有 G6 偿还）；
	//  2. 完工等级归**政府**（GovLevel += added），不归资本建筑；
	//  3. 投向只能是**开发类建筑**（§3.2 裁决）。
	// 它是政府的**真实支出**（不是转移支付），也是"建造部门产能自锁"
	// （R31/R34）的结构性出口——需求端由国库托底。
	PublicWorks bool

	// Acquire 标记这是**收购订单**（§4.5.1a，2026-09-19 第 27 轮裁决）。
	//
	// 【为什么要把它放进建造列表】用户裁决："将收购概念同样加入建造列表"。
	// 在此之前私有化是一笔**瞬时**交易（`book.Privatize` 在 ⑦ 一次性成交），
	// 于是它既不受队列吞吐约束、又与"扩建"争抢同一笔资本现金，实测结果是
	// **一笔都成交不了**（资本池 −2.77e9，见 docs/ACTIVE.md §七 R51）。
	//
	// 放进队列后：
	//   - 它是一张**持久订单**，与扩建订单**同列**（同一 `s.Orders`，同一 FIFO）；
	//   - `Units` 是**待收购的级数**，`Progress` 是**累计已付对价**（不是建造力）；
	//   - `Remaining` 相应改为返回**剩余对价**（= Units×单价 − Progress）；
	//   - 完工时一次性完成股权转让 **政府级 → 私有级**（与扩建订单"完工才加级"同构）；
	//   - **不消耗建造力**：它买的是**存量股权**，不是新建产能，故不进入 §4.2 的
	//     每工地 30 建造力上限、也不占用建造力产出；
	//   - 资金来源是**资本池**（`book.Privatize` 的借方），并由"资本留存"机制
	//     保证它有钱（见 sim 的资本留存：扩张出资与收购出资分开列账）。
	Acquire bool
	// UnitPrice 是收购订单的**每级对价**（元/级）。仅 Acquire 为 true 时有意义。
	//
	// 口径 = 该建筑建造成本 × 建造力当期价 × `PrivatizePriceMult`，
	// 在**下单时冻结**（订单存续期内不随建造力价格波动）——与扩建订单按完工时的
	// 建造成本记账不同，因为收购的对价是双方在下单时约定的。
	UnitPrice float64
}

// serviceDebt 推进 1.2 的借贷台账（M8.4 / M8.4.1）：**入池前先扣本期还款额**，不足则延期。
//
// 调用点：`step` 的 ⑧（资本入池）里、`InvestmentInflow` **之前**。
// 本方法会**就地修改** financeNet（扣掉实付的还本息）。
//
// 【口径（严格按裁决）】
//
//	应付 due = Σ 各笔贷款的等额本息（M8.2）
//	实付 paid = min(due, max(0, financeNet))
//	延期 deferred = due − paid   → 滚入 Loan.Outstanding 并按 5%/年继续计息（M8.4.1）
//
// 【记账】只有**实付额**过账（借 金融区 / 贷 储蓄银行）；延期部分**不走账**
// （它不是资金流动，只是负债余额增加）——这与 M8.7 的裁决一致：
// 负债是**字段**，不进 `Auditor.balances`、不参与借贷相等。
//
// 【违约规则的边界（M8.4.1）】不核销、不加速、不没收；`Loan.Deferrals` 记次数供诊断。
//
// 【默认不生效】仅当 `Params.BankEnabled = true` 时由 step 调用。
func (s *State) serviceDebt(financeNet *float64) {
	s.Cap.DebtServiceDue, s.Cap.DebtPaid, s.Cap.DebtDeferred = 0, 0, 0
	if len(s.Cap.Debt) == 0 {
		return
	}
	// ① 先汇总本 tick 应付额（各笔的等额本息）
	var due float64
	for i := range s.Cap.Debt {
		due += s.Cap.Debt[i].PeriodicPayment()
	}
	s.Cap.DebtServiceDue = due
	if due <= 0 {
		return
	}
	// ② 可用额 = 金融区**本期净额**（可为负 ⇒ 实付 0）。这就是"入池前先扣"的含义。
	avail := *financeNet
	if avail < 0 {
		avail = 0
	}
	paid := avail
	if paid > due {
		paid = due
	}
	// ③ 记账（只过实付额）
	if paid > 0 {
		s.Cap.DebtPaid = s.Bk().DebtService(paid)
		*financeNet -= s.Cap.DebtPaid
	}
	// ④ 台账推进：按各笔的占比分摊实付，差额滚入未偿余额（M8.4.1 违约则延期）
	var remaining = s.Cap.DebtPaid
	for i := range s.Cap.Debt {
		l := &s.Cap.Debt[i]
		share := l.PeriodicPayment()
		if due > 0 {
			share = paid * (l.PeriodicPayment() / due)
		}
		if share > remaining {
			share = remaining
		}
		remaining -= share
		l.Service(share)
	}
	s.Cap.DebtDeferred = due - s.Cap.DebtPaid
}

// issueLoan 发放一笔 1.2 的贷款（M8.3 的 ①）：**借 储蓄银行 / 贷 投资池**，
// 并把负债登记到金融区头上（**非现金科目**，`fiscal.Capital.Debt`）。
//
// 【为什么资金直连投资池】裁决原文："将贷款**记在金融区头上**，然后**投入投资池**"。
// 1.0 实测金融区营运净额长期为负；若钱先进它的现金池，会被先拿去补亏损，
// 到不了投资池。故**资金腿**与**负债腿**分开（M8.3）。
//
// 【放贷方 = 储蓄银行】它的钱来自"劳动力多余的资金"（M4.3 / step 的 ④c）。
// 若储蓄银行余额不足，本函数不发放（不透支）。
//
// 返回实际发放的本金（0 表示未发放）。
func (s *State) issueLoan() float64 {
	if !s.Params.BankEnabled {
		return 0
	}
	p := s.Params.LoanPrincipal
	if p <= 0 {
		return 0
	}
	// 【R71 收口(a)：放贷额度锚到银行余额】
	//
	// R70 实测的致命失衡：`LoanPrincipal` 每 52 tick 固定放 50 万，
	// 而储蓄银行**每 tick** 存入约 180 万 ⇒ 存入速率是放出速率的 **190 倍**
	// ⇒ 800 tick 后银行握着 **18.75 亿**却只贷出 **750 万**（0.4%），
	// 而同一笔钱在 1.0 里本会进投资池去付建造力 ⇒ **投资池被抽干 99.6%**。
	//
	// 收口口径：**每期可贷额 = 银行余额 × LoanBalanceFraction**。
	// 于是"贷出能力"随存款增长，190 倍的失衡被结构性消除，
	// 而 R61 的"每笔 50 万"降格为**下限保护**——不推翻任何已裁决的原则。
	//
	// 【为什么是**替换**而不是 min()：R71 第一次实测的教训】
	// 先写成 `min(余额 × f, LoanPrincipal)` —— 结果与 R65 **逐位相同**：
	// 因为 50 万这个**绝对**上限永远小于"余额 × f"（余额很快就到亿级），
	// `min` 恒取 50 万，收口完全没生效。
	// ⇒ 必须让余额比例**取代**绝对上限，否则收口是空操作。
	//
	// 【默认不生效】`LoanFromBalance=false` 时 `p` 保持原值 ⇒ 逐位复现 R65 行为。
	if s.Params.LoanFromBalance {
		if f := s.Params.LoanBalanceFraction; f > 0 {
			pool := s.SavingsBankBalance()
			if pool <= 0 {
				return 0
			}
			p = pool * f
		}
	}
	got := s.Bk().SavingsBankLend(p)
	if got <= 0 {
		return 0
	}
	// 负债腿：登记在金融区头上（非现金科目，不进账本）
	s.Cap.Debt = append(s.Cap.Debt, fiscal.Loan{
		Principal:   got,
		AnnualRate:  s.Params.LoanAnnualRate,
		TermYears:   s.Params.LoanTermYears,
		Outstanding: got,
	})
	return got
}

// SavingsStockBalance 返回 1.2 M1 第 3 条的**储蓄余额（已积累但尚未分配）**
// = `savingsStock − savingsAlloc`。
//
// 【量纲】元。它是**累计流量之差**，不是账本余额（不进 `Auditor`，
// 因为那些钱已在投资池/储蓄银行里、已被计入货币总量）。
// `Params.SavingsStockTrack = false`（默认）时恒为 0。
func (s *State) SavingsStockBalance() float64 { return s.savingsStock - s.savingsAlloc }

// SavingsStockTotal 返回累计**已积累**的储蓄结余（§5.3 第一步的累计）。
func (s *State) SavingsStockTotal() float64 { return s.savingsStock }

// SavingsAllocatedTotal 返回累计**已分配为投资**的储蓄（§5.3 第二步的累计）。
func (s *State) SavingsAllocatedTotal() float64 { return s.savingsAlloc }

// GoldProducedTotal 返回累计产出的黄金（盎司）——1.2 §1.2-5。
//
// 【它为什么是一个累计量而不是"库存"】金矿每 tick 产出黄金，中央银行**同期收购买币**
// （见 `mintGold`），故黄金不做库存：产出即被央行买走。累计量用于诊断与审计。
// `Params.CentralBankEnabled = false`（默认）时恒为 0。
func (s *State) GoldProducedTotal() float64 { return s.goldProduced }

// MintedTotal 返回中央银行累计创造的货币量（元）——1.2 §1.2-5。
//
// 【它与货币守恒的关系】造币是**货币注入**，走 `PostInjection` 并计入 `InfusionTotal`。
// 若漏计，A8 的恒等式 `ΔM == NewCapital + 注入` 会报一个恰等于造币额的假残差。
func (s *State) MintedTotal() float64 { return s.mintedTotal }

// priceIndex 返回**消费品价格的加权通胀比例**（1.0 = 基期价格水平）。
//
//	通胀比例 = Σ_g w_g·P_g(当期) / Σ_g w_g·P_g(基期)
//
// 权重 `w_g` 用 §6.1 四个消费组的**使用价值**（`ConsumeGroupSpecs` 的 `Uses`），
// 即"居民实际在买的东西"——建造力等非消费品不参与（它们不进消费篮子）。
//
// 【为什么按消费权重而不是等权/产出权重】"通胀"要回答的是
// "**居民买同一篮子东西要花多少钱**"，故权重必须与消费篮子一致。
//
// 【基期价格】`GoodSpecs()` 的 `Pcost`（零利润价）——它是契约里**标定过的**
// 一组固定价格，且 1.0 的价格体系本就围绕它波动。用当期价 ÷ Pcost 得到
// "相对基期的倍数"。
//
// 【默认不生效】`InflationDeflation = false` 时本函数不被调用。
func (s *State) priceIndex() float64 {
	prices := s.Market.Prices()
	groups := model.ConsumeGroupSpecs()
	var num, den float64
	for _, g := range groups {
		for gi, w := range g.Uses {
			if w <= 0 || gi < 0 || gi >= len(prices) || gi >= len(s.Goods) {
				continue
			}
			base := s.Goods[gi].Pcost
			if base <= 0 {
				continue
			}
			num += w * prices[gi]
			den += w * base
		}
	}
	if den <= 0 {
		return 1
	}
	idx := num / den
	if idx <= 0 || math.IsNaN(idx) || math.IsInf(idx, 0) {
		return 1
	}
	return idx
}

// deflatedBudgets 返回**平减后**的各人群池消费预算（1.2 工资平减）。
//
// 【口径（第 72 轮裁决）】"将工资除以通胀比例后**套入需求表**，
// 再用**原始工资**以**原始价格**购买消费品。"
//
// 实现：`预算 = 人群池现金 ÷ 通胀比例`。于是"这笔钱能买多少消费品"
// 按**基期价格**计量 ⇒ 通胀不再通过预算反馈成需求。
//
// 【它只影响"买多少"，不影响"记多少"】记账仍用名义金额 ⇒
// 货币守恒、借贷相等、利润归属**全部不变**（已用审计断言守住）。
func (s *State) deflatedBudgets(taxRate float64) []float64 {
	out := s.Houses.Budgets(taxRate)
	if !s.Params.InflationDeflation {
		return out
	}
	idx := s.priceIndex()
	if idx <= 0 {
		return out
	}
	for i := range out {
		out[i] /= idx
	}
	return out
}

// hasOrderFor 报告**是否已有在途订单**指向该建筑（R96 的去重判据）。
//
// 【为什么需要它】`build.Plan` 每个 tick 都会为"仍然盈利"的建筑重新给出意向，
// 而原实现**无条件**把意向追加成新订单 ⇒ 同一建筑会累积多条并行订单
// （实测末期高档服装厂有 **4 条**：90.2% / 65.7% / 35.8% / 12.0%）。
// 后果是建造力预算被摊薄、谁都不完工，且 `stackDemand` 把重复订单都计成需求
// ⇒ 需求被系统性高估。
//
// 注意它查的是**全部**订单（含 `Acquire` 收购单），因为收购单同样占用该建筑的
// 建造队列语义。"是否已在途"与"进度多少"无关——进度由 `completeOrders` 判定。
func (s *State) hasOrderFor(bi int) bool {
	for i := range s.Orders {
		if s.Orders[i].BuildingIndex == bi {
			return true
		}
	}
	return false
}

// goldMineIndex 返回**第一个金矿**的场地下标；没有金矿时返回 -1。
//
// 【为什么只用第一个金矿作收款方】§1.2-5 的购金付款是"给金矿"，
// 没有规定多家金矿时如何分配。本实现取**第一个**金矿（specs 里只有一个金矿类别，
// 故它同时代表该类别的全部等级）——这是一处**登记在案的简化**。
func (s *State) goldMineIndex() int {
	for i := range s.Buildings {
		if s.Buildings[i].Spec.ProducesGold {
			return i
		}
	}
	return -1
}

// centralBankIndex 返回中央银行的场地下标；未启用时返回 -1。
//
// 【为什么要查而不是写常量】央行只有开启 `CentralBankEnabled` 时才存在于 specs 里，
// 且将来若调整追加顺序，写死的下标会静默指向别的建筑。查一次并缓存即可。
func (s *State) centralBankIndex() int {
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsCentralBank {
			return i
		}
	}
	return -1
}

// goldPerTick 返回本 tick 金矿的黄金产出（盎司）。
//
//	产出 = Σ_{金矿} (等级 × 雇佣率) × 每级产出量
//
// 与 §3.3 对普通商品的 `ActualOutput` 同口径（都是 `等级 × 雇佣率 × 配方量`），
// 区别只是它**不进商品市场**（裁决：黄金外生价格、不进 A 矩阵）。
func (s *State) goldPerTick() float64 {
	var out float64
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if !b.Spec.ProducesGold || b.Spec.Recipe.Qty <= 0 {
			continue
		}
		out += b.Level * b.HireRate * b.Spec.Recipe.Qty
	}
	return out
}

// mintGold 结算本 tick 的**购金造币**（1.2 §1.2-5）。
//
// 原文："单位中央银行消耗 20 黄金，生产 400,000 货币进入现金池。
// 其中一半用于支付 10,000 原每单位黄金给金矿。剩余部分进入中央银行现金池。"
//
// 口径（本实现）：
//
//	可造币批数 n = floor(金矿本期产出 / GoldPerMint)      （未满一批的黄金留待下期）
//	造币额 M     = n × MoneyPerMint                        ← **货币注入**
//	付给金矿 P   = n × GoldPerMint × GoldPrice             ← 借 央行 / 贷 金矿
//	央行留存     = M − P
//
// 【两条腿的记账】
//   - **造币**是唯一的货币创造 ⇒ 走 `PostInjection`（贷 央行账户），计入 `InfusionTotal`；
//   - **购金付款**是普通转移 ⇒ 走 `Post`（借 央行 / 贷 金矿）。
//
// 【为什么"未满一批的黄金"不累加库存】金矿产出是**连续**的（每 tick 都有），
// 而造币是**离散**的（每 20 单位一批）。若把不足一批的黄金丢弃，会低估造币；
// 若累积库存，则要再引入一个存量账。本实现取**累计小数批**的做法：
// `goldCarry` 记住"已产出但尚未造币"的黄金，跨 tick 累加 ⇒ 长期造币量精确、
// 且不丢弃产出。这是**登记在案的实现口径**（原文没有说不足一批时怎么办）。
func (s *State) mintGold() (produced, minted, paidToMine float64) {
	if !s.Params.CentralBankEnabled {
		return 0, 0, 0
	}
	produced = s.goldPerTick()
	s.goldProduced += produced
	s.goldCarry += produced

	perMint := s.Params.GoldPerMint
	if perMint <= 0 {
		return produced, 0, 0
	}
	batches := math.Floor(s.goldCarry / perMint)
	if batches <= 0 {
		return produced, 0, 0
	}
	// 消耗掉已造币的那部分黄金（余数留下期）
	s.goldCarry -= batches * perMint

	// 【R86：造币额的两种口径】
	//
	//	MintWageFraction > 0 ⇒ M = 批数 × f × **当期工资总额**   （推荐：锚到经济体量）
	//	否则                ⇒ M = 批数 × MoneyPerMint           （§1.2-5 草案固定值）
	//
	// 工资锚的理由见 `Params.MintWageFraction` 的注释：固定值会每 tick 印出
	// 货币存量的 58%。用**当期工资总额**作锚，是因为 1.0 的初始货币本身就是
	// 按"一周工资流量"标定的（§4.3 修订）——同一条纪律。
	var perBatch float64
	if f := s.Params.MintWageFraction; f > 0 {
		perBatch = f * s.wageBillTick
	} else {
		perBatch = s.Params.MoneyPerMint
	}
	if perBatch <= 0 {
		return produced, 0, 0
	}
	minted = batches * perBatch
	// 【R87：按 §1.2-5 的**二分之一**分成，而不是"黄金价 × 单位数"】
	//
	// 原文："生产 400,000 货币进入现金池。**其中一半**用于支付 10,000 原每单位
	// 黄金给金矿。**剩余部分**进入中央银行现金池。"
	//
	// 即：M 中 **二分之一**归金矿、**二分之一**留央行。
	// 【为什么不用"黄金 × GoldPrice"】那样在 `GoldPrice = 10000` 时会付掉
	// `20 × 10000 = 200,000`，恰好也是 400,000 的一半——**两个口径在
	// §1.2-5 的原始数值下恰好等价**。但有工资锚之后 `M` 变小，两者就**不再等价**：
	// 实测 `M ≈ 141,600` 而"黄金 × GoldPrice"仍是 200,000 ⇒ 被 `min` 封顶到 M
	// ⇒ **央行一分不留**（实测央行池恒为 0，与原文"剩余部分进央行池"矛盾）。
	//
	// 故改为**按 M 的二分之一**分成：无论 M 因何种口径变化，
	// "一半给金矿、一半留央行"这条**结构**始终成立。
	// 此时 `GoldPrice` 退为**诊断/校验用的名义价**（`20 × GoldPrice` 应等于
	// 固定口径下的一半；`TestCentralBankSpecs` 会断言这一点）。
	paidToMine = minted / 2
	s.mintedTotal += minted
	// 用于造币的黄金单位数（诊断量）：批数 × 每批黄金数
	s.goldMintedUnits += batches * perMint

	// ① 造币：唯一的货币创造腿（注入）
	s.Bk().MintMoney(minted)
	// 【R87】造币必须计入 `infusionTotal`，否则 A8 的
	// `ΔM == NewCapital + InfusionTotal` 会报一个恰等于造币额的残差。
	s.infusionTotal += minted
	// ② 购金付款：央行 → 金矿（普通转移）
	if paidToMine > 0 {
		if mineIdx := s.goldMineIndex(); mineIdx >= 0 {
			s.Bk().PayForGold(mineIdx, paidToMine)
		}
	}
	s.mintTick = minted
	s.goldPaidTick = paidToMine
	return produced, minted, paidToMine
}

// expandCentralBank 实现 §1.2-5 的第三项："**当黄金有剩余时，中央银行自动扩建**"。
//
// 【怎样表达"黄金有剩余"】本实现里黄金产出即被造币消耗，没有库存，
// 故"剩余"必须通过**产能**表达：
//
//	央行吞吐量 = 级数 × GoldPerBankLevel          （每 tick 能吃进多少黄金）
//	本期产出   = 金矿产出
//	产出 > 吞吐量 ⇒ **黄金有剩余**（央行吃不完）⇒ 扩建
//
// 【目标级数】把本期产出全部吃下所需的级数 = ceil(产出 / GoldPerBankLevel)。
// 【为什么不用建造力】§1.2-5 把央行的扩建写成**自动**（"当黄金有剩余时…自动扩建"），
// 与仓库的"额度驱动自动扩建"同类（§4.5.6）——但仓库走公共工程的**建造订单**，
// 而央行是货币机构、没有建造成本（`BuildCost = 0`）。
// 故这里**直接改级数**，不产生建造订单、不消耗建造力。
//
// 【默认不生效】`CentralBankEnabled = false` 或 `GoldPerBankLevel <= 0` 时直接返回。
//
// 返回本 tick 新增的级数。
func (s *State) expandCentralBank() float64 {
	ci := s.centralBankIndex()
	if ci < 0 || s.Params.GoldPerBankLevel <= 0 {
		return 0
	}
	produced := s.goldPerTick()
	if produced <= 0 {
		return 0
	}
	b := &s.Buildings[ci]
	// 目标级数 = 把本期产出全部吃下所需
	want := math.Ceil(produced / s.Params.GoldPerBankLevel)
	if want <= b.Level {
		return 0
	}
	added := want - b.Level
	b.Level = want
	// 央行是国有的（与仓库同口径：它的钱是造出来的，不属于私人）
	b.GovLevel = want
	b.PrivLevel = 0
	s.centralBankExpandedTick = added
	return added
}

// goldProducedTotal 见 `GoldProducedTotal`。

// payGovDebtInterest 结算本 tick 的**政府债务利息**（1.2 M5 ①）。
//
// 【裁决（第 67 轮）】"政府债务计息，**利息付给央行**"。
//
// 【口径】
//
//	本金 = 政府债务 = max(0, −政府现金池)          （1.0 §4.5.4 的定义，不改）
//	利率 = Params.GovDebtInterestRate（默认 5%，= M8.5 的统一曲线）
//	本期利息 = 本金 × 利率 / TicksPerYear
//
// 过账：`借 政府 / 贷 央行` ⇒ **政府的现金池更负**（即债务增加）、**央行的现金池实增**。
// 货币总量**不变**（它不是创造，只是转移）——这一点很重要，故它**不**进 `InfusionTotal`。
//
// 【为什么"以举债付息"是自洽的】1.0 的债务不是一笔独立台账，而是"现金池为负"的
// **等价说法**。既然没有独立的还本付息现金流，利息就只能表现为"债务增加"
// （借记政府）。这与 M8.4.1"违约则延期、滚入未偿余额"是同一性质的处理。
//
// 【默认不生效】`GovDebtInterestEnabled = false` 时直接返回 0（1.0 逐位不变）。
func (s *State) payGovDebtInterest() float64 {
	if !s.Params.GovDebtInterestEnabled {
		return 0
	}
	principal := s.GovDebt()
	if principal <= 0 {
		return 0
	}
	rate := s.Params.GovDebtInterestRate
	if rate <= 0 {
		return 0
	}
	tpy := float64(s.Params.TicksPerYear)
	if tpy <= 0 {
		tpy = 52
	}
	interest := principal * rate / tpy
	if interest <= 0 {
		return 0
	}
	paid := s.Bk().PayGovDebtInterest(interest)
	s.govDebtInterestTick = paid
	s.govDebtInterestTotal += paid
	return paid
}

// GovDebt 返回政府债务 = max(0, −政府现金池)（1.0 §4.5.4 的定义）。
func (s *State) GovDebt() float64 {
	b := s.balGov()
	if b < 0 {
		return -b
	}
	return 0
}

// GovDebtInterestTotal 返回累计支付的政府债务利息（1.2 M5 ①，诊断用）。
func (s *State) GovDebtInterestTotal() float64 { return s.govDebtInterestTotal }

// laborPrivateShare 返回**私人份额中归劳动力**的比例（1.2 M4.2 所有权重构）。
//
//	未开重构                    ⇒ **0**（利润归属走单腿 ⇒ 1.0 逐位不变）
//	开启（默认资本份额 0.30）    ⇒ **0.70**
//
// 【口径来源】R57 第 3 条："资本直接承接政府 30%、劳动力直接承接资本 70%"，
// 两条划转各自一跳：① 政府 → 资本 30%；② 资本 → 劳动力 70%。
// 于是私人份额由"资本 0.70"变为"资本 0.30 + 劳动力 0.70"。
func (s *State) laborPrivateShare() float64 {
	if !s.Params.OwnershipRestructure {
		return 0
	}
	f := 1 - s.Params.OwnershipCapitalShare
	if f < 0 {
		return 0
	}
	if f > 1 {
		return 1
	}
	return f
}

// laborLegs 按**人头等分**构造"统一入口 → 人群池"的派发腿（1.2 M4.2 / M8.6）。
//
// 【受益人集合（2026-09-20 第 51 轮裁决）】"**统一：在职人口，失业者不参与**"。
// 即排除 `s.UnemployedSite` 那个虚拟场地；其余有 `Population > 0` 的池按人头平分。
//
// 【为什么两条腿共用本函数】M4.2 的分红派发与 M8.6 ③ 的利息当期分配是**同一机制**：
//
//	统一入口（劳动力分红池 / 储蓄银行）→ 按人头分摊 → §5.1 的人群池
//
// 故两者都走这里，规则只有一处、不会漂移。
//
// 【为什么按人头而不是按阶级】M4.2 裁决："劳动力**不分阶级**，视作**一同管理**"，
// 且第 3 次裁决澄清"按人头平分**就是**按场地人数比例平分——两者**等价**"。
// 故不需要为分红改造 §5.1 的池结构，只需一个统一入口 + 按人口加权。
//
// `amount` 是**可分配总额**；各腿金额之和恒等于返回值（按比例裁剪时同除一个 scale）。
func (s *State) laborLegs(amount float64) []book.WelfareLeg {
	if amount <= 0 || s.Houses == nil {
		return nil
	}
	type want struct {
		idx int
		pop float64
	}
	var wants []want
	var totalPop float64
	for i := range s.Houses.Pools {
		p := &s.Houses.Pools[i]
		if p.Population <= 1e-12 {
			continue
		}
		// 裁决：失业者不参与
		if p.Worksite == s.UnemployedSite {
			continue
		}
		wants = append(wants, want{idx: i, pop: p.Population})
		totalPop += p.Population
	}
	if totalPop <= 0 {
		return nil
	}
	legs := make([]book.WelfareLeg, 0, len(wants))
	for _, w := range wants {
		amt := amount * (w.pop / totalPop)
		if amt <= 1e-9 {
			continue
		}
		legs = append(legs, book.WelfareLeg{Pool: s.Houses.Account(w.idx), Amount: amt})
	}
	return legs
}

// distributeLaborDividend 把**劳动力分红池**的余额派发给在职人口（1.2 M4.2）。
//
//	借 劳动力分红池   Σ金额
//	贷 人群池[p]       各池按人头分摊
//
// 【为什么派发"该账户的余额"而不是"本 tick 新增"】该账户是**净额累计器**
// （亏损期 `Owner < 0` ⇒ 分红为负 ⇒ 被借记），若只派发本 tick 净额，
// 历史累积会永远留在池里（R75 实测 800 tick 沉淀 30.13 亿）。
// 派发**余额**则每 tick 归零，符合 M4.2"持有者获每周期对应分红"的口径。
//
// 【默认不生效】`OwnershipRestructure=false` 时分红池恒为 0 ⇒ 本函数直接返回。
func (s *State) distributeLaborDividend() float64 {
	acc := ledger.LaborDividend()
	bal := s.Aud.Balance(acc)
	if bal <= 0 {
		return 0
	}
	legs := s.laborLegs(bal)
	if len(legs) == 0 {
		return 0
	}
	return s.Bk().PayLaborDividend(legs)
}

// cappedMargin 返回参与抬价的**利润率因子**，按 `Params.WageBidMarginCap` 封顶。
//
// 【R92 的口径】实测（R68/R69）稀缺局里参与抬价的场地利润率高达 **20.17**
// （正常开局是 0.20）⇒ "按比例拿利润加价"必然给出相对工资 15~37 倍的加价。
//
//	封顶后 m' = min(m, cap)      （cap ≤ 0 表示不封顶，逐位复现裁决原文）
//
// 【为什么用它而不是"封顶溢价本身"】封顶溢价 = 给结果设一个"工资倍数上限"，
// 那正是 M7.2 裁决⑤(c) **明确回避**过的做法。封顶**输入**（利润率）不动结果的上界，
// 只把"什么样的利润率才配参与抬价"这件事说清楚——**对准根因**。
func (s *State) cappedMargin(margin float64) float64 {
	if margin <= 0 {
		return 0
	}
	if cap := s.Params.WageBidMarginCap; cap > 0 && margin > cap {
		return cap
	}
	return margin
}

// effectiveWagePerLevel 返回第 i 个场地的**每级工资成本**（含竞标溢价）。
//
//	= (baseWage_i + p_i) × LaborPerLevel
//
// `WageBidEnabled=false` 时它**恒等于** `Spec.WagePerLevel()`——
// 这是"1.0 逐位不变"的关键：当期零利润价 P⁰(t) 与标定用的口径因此完全一致。
func (s *State) effectiveWagePerLevel(i int) float64 {
	if i < 0 || i >= len(s.Buildings) {
		return 0
	}
	b := &s.Buildings[i]
	w := b.Spec.WagePerLevel()
	if p := s.WagePremium(i); p > 0 {
		w += p * b.Spec.LaborPerLevel
	}
	return w
}

// allocateByWageOrder 实现 1.2 M7.2 第 4 步：**按实际人均工资降序配给劳动力**。
//
// 返回**全局配给系数** k（= 实际到岗总量 / 申报总量），供调用方按 1.0 第 23 轮的
// 同一条口径缩放工资与产出——注意这里是**诊断量与兜底**，真正的配给已按工资排序完成。
//
// 【与 §5.1 人群池的关系】本函数只决定"每个场地拿到多少**总**劳动力"，
// 场地内的**阶级构成**仍按 §5 的劳动结构比例（75/20/5）分摊——
// 这与 M7.2 第 5 步"溢价按比例放大到各阶级、保持工资结构"一致。
//
// 【为什么"排序即配置"】裁决④定"无迁徙成本"：模型里工人没有"地点"状态，
// 只有"场地 × 阶级"的池，故同一 tick 内按工资降序分配完毕，不需要迁移率。
//
// 【默认不生效】仅当 `Params.WageBidEnabled` 时由 `allocateSubsistenceLabor` 调用。
func (s *State) allocateByWageOrder(levels, hire []float64) float64 {
	n := len(s.Buildings)
	if len(s.wageShortTick) != n {
		s.wageShortTick = make([]bool, n)
	}
	type slot struct {
		idx  int
		wage float64
	}
	order := make([]slot, 0, n)
	var demanded float64
	for i := 0; i < n && i < len(levels) && i < len(hire); i++ {
		sp := s.Buildings[i].Spec
		if sp.LaborPerLevel <= 0 {
			continue
		}
		demand := levels[i] * hire[i] * sp.LaborPerLevel
		if demand <= 0 {
			continue
		}
		demanded += demand
		order = append(order, slot{idx: i, wage: s.EffectiveWage(i)})
	}
	// 降序：工资高者先拿人；同工资按场地下标稳定排序（保证确定性）
	sort.SliceStable(order, func(a, b int) bool { return order[a].wage > order[b].wage })
	remaining := s.Population
	var allocated float64
	for _, o := range order {
		sp := s.Buildings[o.idx].Spec
		demand := levels[o.idx] * hire[o.idx] * sp.LaborPerLevel
		give := demand
		if give > remaining {
			give = remaining
		}
		if give < 0 {
			give = 0
		}
		remaining -= give
		allocated += give
		// 缺员标记：申报量未被满足（供 ⑭ 的溢价更新用，M7.2 裁决①）
		s.wageShortTick[o.idx] = give < demand-1e-9
	}
	if demanded <= 0 {
		return 1
	}
	k := allocated / demanded
	if k > 1 {
		k = 1
	}
	return k
}

// SavingsBank 返回储蓄银行现金池余额（1.2 M4.3 的诊断口径）。
func (s *State) SavingsBankBalance() float64 {
	return s.Aud.Balance(ledger.SavingsBank())
}

// ===== 1.2 M7 工资竞标 =====

// BaseWage 返回第 i 个场地的**基准人均工资**（§5 的劳动结构，不含竞标溢价）。
func (s *State) BaseWage(i int) float64 {
	if i < 0 || i >= len(s.Buildings) {
		return 0
	}
	sp := s.Buildings[i].Spec
	if sp.LaborPerLevel <= 0 {
		return 0
	}
	return sp.WagePerLevel() / sp.LaborPerLevel
}

// EffectiveWage 返回第 i 个场地的**实际人均工资** = 基准 + 竞标溢价（1.2 M7.2 第 1 步）。
//
// `WageBidEnabled=false` 或 `wagePremium` 为空时退化为基准工资。
func (s *State) EffectiveWage(i int) float64 {
	w := s.BaseWage(i)
	if i >= 0 && i < len(s.wagePremium) {
		w += s.wagePremium[i]
	}
	return w
}

// WagePremium 返回第 i 个场地的当期竞标溢价（诊断）。
func (s *State) WagePremium(i int) float64 {
	if i < 0 || i >= len(s.wagePremium) {
		return 0
	}
	return s.wagePremium[i]
}

// updateWageBids 按 1.2 M7.2 的第 2–3 步更新各场地的竞标溢价。
//
// 规则（严格按裁决）：
//
//	① 只在**本期缺员**时抬价（裁决①(a)）；
//	② 抬价额 = wageBidCap × max(0, 本期纯利) / 在岗人数（第 2 步的 A_i ÷ 人数）；
//	③ **招满即回落**（裁决②(b)）——本期招满且未缺员 ⇒ 按衰减率下调；
//	④ **缓慢衰减**（裁决②(c)）——任何情形下都先按年化速率向 0 衰减；
//	⑤ 不设显式上限（裁决⑤(c)）：由 §4.1 的 10% 阈值隐式约束。
//
// 【为什么每 tick 只抬一次】避免"抬价 → 成本 → 利润率 → 抬价"在同一 tick 内递归。
//
// 【默认不生效】`WageBidEnabled=false` 时本函数不被调用，`wagePremium` 保持全 0。
func (s *State) updateWageBids() {
	n := len(s.Buildings)
	if len(s.wagePremium) != n {
		s.wagePremium = make([]float64, n)
	}
	if len(s.wageShortTick) != n {
		s.wageShortTick = make([]bool, n)
	}
	decayPerTick := s.Params.WageBidDecay / float64(s.Params.TicksPerYear)
	if decayPerTick < 0 {
		decayPerTick = 0
	}
	if decayPerTick > 1 {
		decayPerTick = 1
	}
	for i := range s.Buildings {
		b := &s.Buildings[i]
		p := s.wagePremium[i]
		// ④ 先衰减（对所有场地生效：只有上抬没有下探会重现棘轮）
		p *= (1 - decayPerTick)
		// ③ 招满即回落：本期没缺员 ⇒ 额外下调一次（比"缓慢衰减"更明确）
		if !s.wageShortTick[i] {
			p *= (1 - decayPerTick)
		}
		// ①② 缺员则抬价（额度受利润约束）
		if s.wageShortTick[i] && b.HireRate > 1e-9 {
			// 【R68 核查：分母口径**不改**】
			//
			// R67 遗留问题里我提出过"缺员时在岗人数被压缩 ⇒ 分母变小 ⇒ 人均溢价被放大"，
			// 并据此把分母从 `Level × HireRate × LaborPerLevel` 改成了 `Level × LaborPerLevel`。
			// **实测反驳了这个诊断**：改动前后峰值溢价一位不变（97.8249 → 97.8249）。
			//
			// 原因（`premium_anatomy_test.go` 逐 tick 拆解）：`HireRate` 由 `build.AdjustHire`
			// 产生且被 `math.Min(1, …)` 封顶，它是**雇佣率（申报口径）**，不是"到岗比例"；
			// 真正的配给结果记在 `s.LaborMarketRatio`，而 `payWages` 的 `eff = levels×hire`
			// 支付的正是**申报量**。所以 `Level × HireRate × LaborPerLevel` **本来就是**申报满编，
			// `Level × LaborPerLevel` 反而在 `HireRate < 1` 时**多算**了分母。
			// 故回滚为原口径——它忠实于裁决措辞"在岗人数"，且是三者中唯一正确的。
			workers := b.Level * b.HireRate * b.Spec.LaborPerLevel
			if workers > 1e-9 {
				// 第 2 步：A_i = wageBidCap × 基数（R68 新增 `WageBidBase` 对照口径）
				//
				// 基数按 `Params.WageBidBase` 分派：
				//   ""/"profit" ⇒ **纯利**（裁决原文；默认，逐位复现）
				//   "wagebill"  ⇒ **当期工资总额 × max(0, 利润率)**（R68 建议的收口口径）
				var A float64
				switch s.Params.WageBidBase {
				case "wagebill":
					// 当期工资总额 = 在岗人数 × 人均基准工资；利润率取本期实际值。
					// 注意用 `BaseWage` 而非 `EffectiveWage`：以**工资**为基数时若再用
					// 含溢价的工资，会形成"溢价 → 更大基数 → 更大溢价"的自反馈。
					//
					// 【R92】利润率因子按 `WageBidMarginCap` 封顶（0 = 不封顶）。
					if margin := s.cappedMargin(b.LastMargin); margin > 0 {
						A = s.Params.WageBidCap * (workers * s.BaseWage(i)) * margin
					}
				default:
					profit := b.LastProfit
					if profit > 0 {
						A = s.Params.WageBidCap * profit
						// 【R92】利润率封顶：把"参与抬价的利润率"压到上限。
						//
						// `b.LastProfit` 已经含了当期利润率（纯利 = 收入 − 成本 − 工资），
						// 故不能"再乘一个 cap"——那会重复作用。正确做法是按**比例**缩放：
						//
						//	A' = A × min(1, cap / 实际利润率)
						//
						// 于是"利润率 20.17、cap 0.2"时 A' = A × 0.0099 ⇒ 加价被压回
						// "正常利润率"应有的水平。cap ≤ 0 时比例恒为 1 ⇒ 逐位复现原口径。
						if cap := s.Params.WageBidMarginCap; cap > 0 {
							if m := b.LastMargin; m > cap {
								A *= cap / m
							}
						}
					}
				}
				p += A / workers
			}
		}
		if p < 0 {
			p = 0
		}
		s.wagePremium[i] = p
	}
}

// loanIssuedTick 是本 tick 实际发放的贷款本金（1.2 M8，诊断用）。
func (s *State) LoanIssuedTick() float64 { return s.loanIssuedTick }

func (s *State) hasAcquireOrder(bi int) bool {
	for i := range s.Orders {
		o := &s.Orders[i]
		if o.Acquire && o.BuildingIndex == bi && o.Remaining(s.Buildings) > 1e-9 {
			return true
		}
	}
	return false
}

// acquireNeed 返回本 tick 全部收购订单**还差的对价总额**（元）。
//
// 它被用作"资本留存"的依据：⑨b 把资本净额转入投资池之前先扣掉这一笔，
// 于是**收购的单子有钱、扩建的单子用剩下的钱**——这是"收购进建造列表"
// 能真正成交的关键（否则资本的钱全被 §4.5.1b 抽去付建造力，收购永远排不上）。
func (s *State) acquireNeed() float64 {
	var need float64
	for i := range s.Orders {
		o := &s.Orders[i]
		if !o.Acquire {
			continue
		}
		if r := o.Remaining(s.Buildings); r > 0 {
			need += r
		}
	}
	return need
}

// processAcquisitions 推进全部**收购订单**（§4.5.1a 第 27/28 轮）。
//
// 口径与扩建订单的三点区别：
//   - 付的是**对价**而不是建造力；
//   - **不占用建造力产出**、不受 §4.2 每工地 30 上限约束（买的是存量股权）；
//   - 完工时才一次性转让 `Units` 级（政府级 → 私有级），与扩建"完工才加级"同构。
//
// 每 tick 的推进额受两条约束：① 付款方的可用余额；② 每 tick 至多完成
// `PrivatizeStep` 比例的级数（年化速率，与下单量同一旋钮），避免一 tick 买下整个部门。
//
// 【出资方（第 28 轮裁决："允许收购用投资池余额出资"）】按下述顺序取：
//
//	① **投资池**（`PrivatizeFromInvestment`）：它是"可动用于投资的资金"，
//	   本意就是投资——买存量股权也是投资。实测默认参数下这里有 3.0e9 闲置。
//	② 资本池（`Privatize`）：投资池也付不动时，退回资本自己的现金。
//
// 【为什么不是"资本池优先"】默认参数下资本池长期为负（−3.38e9），
// 若先试它就等于永远不动；而投资池的钱本来就是居民储蓄、等着被投出去。
//
// 返回本 tick 实际成交的级数与付出对价。
func (s *State) processAcquisitions() (units, paid float64) {
	if !s.Params.PrivatizeEnabled {
		return 0, 0
	}
	step := s.Params.PrivatizeStep
	if step <= 0 {
		step = 0.05
	}
	for i := range s.Orders {
		o := &s.Orders[i]
		if !o.Acquire {
			continue
		}
		remaining := o.Remaining(s.Buildings)
		if remaining <= 1e-9 {
			continue
		}
		b := &s.Buildings[o.BuildingIndex]
		if b.GovLevel <= 1e-9 {
			continue // 政府已无持股（可能被别的单子买走了）
		}
		// ① 本 tick 对价上限：拟转让级数 ≤ 政府持股 × 步长。
		wantUnits := b.GovLevel * step
		// ② 受收购单自身的剩余量约束。
		if byOrder := remaining / o.UnitPrice; wantUnits > byOrder {
			wantUnits = byOrder
		}
		if wantUnits <= 1e-9 {
			continue
		}
		// ③ 出资方：默认"投资池优先，其次资本池"（第 28 轮用户裁决）。
		//
		// 顺序的原因：默认参数下资本池长期为负（−3.38e9），先试它就等于永远不动；
		// 而投资池的钱本来就是居民储蓄、等着被投出去。
		//
		// `Params.AcquireFromInvestment = false` 时只用资本池（第 27 轮口径）——
		// 供"在受控条件下反解对价公式"的审计断言使用（见该字段的说明）。
		var res book.PrivatizeResult
		if s.Params.AcquireFromInvestment {
			res = s.Bk().PrivatizeFromInvestment(wantUnits, o.UnitPrice, s.Params.TaxRate)
		}
		if res.Units <= 1e-9 {
			res = s.Bk().Privatize(wantUnits, o.UnitPrice, s.Params.TaxRate)
		}
		if res.Units <= 1e-9 {
			continue // 两处都没钱：整张单子本 tick 停摆（下一 tick 再试）
		}
		o.Progress += res.Paid
		moved := res.Units
		if moved > b.GovLevel {
			moved = b.GovLevel
		}
		b.GovLevel -= moved
		b.PrivLevel += moved
		units += moved
		paid += res.Paid
	}
	// 完工的收购单移出队列（剩余 ≤ 0），与扩建单的清理同一处口径。
	kept := s.Orders[:0]
	for i := range s.Orders {
		o := s.Orders[i]
		if o.Acquire && o.Remaining(s.Buildings) <= 1e-9 {
			continue
		}
		kept = append(kept, o)
	}
	s.Orders = kept
	return units, paid
}

// Remaining 返回订单尚未投入的需求。
//
// 【两种口径（2026-09-19 第 27 轮）】
//   - 扩建订单：**建造力**剩余需求 = 建造成本 × 级数 − 已投入建造力；
//   - 收购订单（`Acquire`）：**对价**剩余额 = 级数 × 每级对价 − 已付对价。
//
// 两者共用同一张队列表，故本函数必须按类型分派——否则收购订单会被
// 当成"要花建造力"，把建造力产出错误地算进它的剩余里。
func (o Order) Remaining(buildings []BuildingState) float64 {
	if o.BuildingIndex < 0 || o.BuildingIndex >= len(buildings) {
		return 0
	}
	if o.Acquire {
		return o.Units*o.UnitPrice - o.Progress
	}
	return buildings[o.BuildingIndex].Spec.BuildCost*o.Units - o.Progress
}

// State 是仿真的全部可变状态。
type State struct {
	Goods     []model.Good
	Buildings []BuildingState
	Market    *market.State

	// UnemployedSite 是本次运行里"**失业**"这一虚拟劳动场地的下标（§5.1 / §6.5）。
	//
	// 【为什么是运行时字段而不是常量】1.2 的央行/金矿会向 `BuildingSpecs`
	// **追加建筑** ⇒ `len(specs)` 变化 ⇒ 失业场地的下标随之变化。
	// 常量 `model.UnemployedSite` 只对应"关闭央行"这一种规格；若在开启央行时仍用它，
	// 失业人口会被记到某个真实建筑的池里。
	//
	// 关闭央行（默认）时它等于 `model.UnemployedSite`，故 1.0 逐位不变。
	UnemployedSite int

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
	// 导致货币每 tick 净损 15%~34%（见 docs/ACTIVE.md §6.5）。
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

	// ===== §4.2 修订 / §4.5.5：自给农场与宅邸庄园 =====

	// SubsistenceLevels 是当期自给农场级数（= 未使用耕地 × SubsistenceScale）。
	SubsistenceLevels float64
	// SubsistenceHireRate 是自给农场的雇佣率（备用劳动力池填充率 ∈[0,1]）。
	//
	// 就业顺序：一般生产建筑（含金融区与宅邸庄园）→ 自给农场 → 失业。
	SubsistenceHireRate float64
	// LaborMarketRatio 是本 tick 的**劳动力配给系数** ∈ (0,1]（§5.2 第 23 轮人口约束）。
	//
	// acquireCommitted 是**本 tick 为收购订单留存的资本额**（§4.5.1a 第 27 轮）。
	// ⑨b 把资本净额转入投资池之前先扣掉它，使"收购有钱、扩建用剩下的钱"。
	acquireCommitted float64
	// loanIssuedTick 是本 tick 实际发放的贷款本金（1.2 M8，诊断用）。每 tick 开头清零。
	loanIssuedTick float64
	// laborDividendTick 是本 tick 贷记进 `ledger.LaborDividend()` 的**劳动力分红**
	//（1.2 M4.2 所有权重构，诊断用）。每 tick 开头清零。
	//
	// 【它是什么】所有权重构后私人份额纯利中归劳动力的那一腿（默认 70%）。
	// 未开重构时恒为 0。按 M4.2 裁决"劳动力不分阶级、按人头平分"，
	// 故它贷记到**一个统一入口**，而不是分散进 §5.1 的 39 个人群池。
	laborDividendTick float64
	// laborDividendPaidTick 是本 tick 从分红池**实发到人群池**的金额
	//（1.2 M4.2 派发腿，诊断用）。每 tick 开头清零。
	laborDividendPaidTick float64
	// privateShareTick 是本 tick 归属给"私人"的全部份额 Σ res.Owner
	//（1.2 M4.2 口径验证用）。每 tick 开头清零。
	//
	// 它与 `laborDividendTick` 的关系**逐位成立**：
	//
	//	laborDividendTick == privateShareTick × laborPrivateShare()
	//
	// 故用这两个量就能验证"资本 30% / 劳动力 70%"，且不受后续资金回流影响
	//（用期末池余额反算会有 2.75e7 的二阶偏差，见 R76）。
	privateShareTick float64

	// ===== 1.2 M7 工资竞标（默认关闭）=====
	//
	// wagePremium[i] 是第 i 个场地当期的**竞标溢价** p_i ≥ 0（元/人/周期），
	// 与 §5 的基准工资相加得到**实际人均工资**：
	//
	//	实际人均工资_i = baseWage_i + wagePremium[i]
	//	baseWage_i     = Spec.WagePerLevel() / Spec.LaborPerLevel
	//
	// 它**只**在本 tick 的以下三处生效（都与 `WageBidEnabled` 绑定）：
	//   ① 工资支付（`payWages`）的金额放大；
	//   ② 当期零利润价 P⁰(t) 的单位劳动成本（`refreshPzero`）；
	//   ③ 配置顺序（`allocateSubsistenceLabor` 按实际工资降序）。
	// `WageBidEnabled=false` 时本切片恒为 0 ⇒ 三处都退化为 1.0 口径。
	wagePremium []float64
	// wageShortTick[i] 标记第 i 个场地**本期是否缺员**（申报量未被满足），
	// 供 ⑭ 的溢价更新使用（1.2 M7.2 裁决①：只在缺员时抬价）。
	wageShortTick []bool
	// WagePaid 是本 tick 因竞标溢价而多付的工资总额（诊断）。
	wagePremiumPaidTick float64 //
	// 当"各场地申报雇佣人口之和 > 总人口"时，本值为 人口 / 申报量，所有场地的
	// **有效雇佣率**按它等比缩放（见 allocateSubsistenceLabor）。= 1 表示未触限。
	// 零值（新建 State）由 step 开头的 allocateSubsistenceLabor 立即改写。
	// 它只影响本 tick 的实际用工/工资/产出，**不写回** `Building.HireRate`——
	// 后者仍由 §5.2 的利润率信号驱动。
	LaborMarketRatio float64
	// SubsidyPaidTick 是本 tick 支付的政府补贴总额（§4.5.7）。
	subsidyPaidTick float64

	// ===== 2026-09-19 第 15 轮裁决新增的三条资金流（每 tick 流量）=====

	// savingTick 是本 tick 居民**结余全额进入储蓄固定账户**的金额（§5.3 第一步）。
	savingTick float64
	// savingInvestTick 是本 tick 从储蓄账户**实际转入投资池**的金额（§5.3 第二步，
	// = σ_save × savingTick）。σ_save = 1 时两者相等、储蓄账户期末归零。
	savingInvestTick float64
	// ===== 1.2 M1 第 3 条：**存量**口径（储蓄余额 vs 已分配投资）=====
	//
	// 【为什么需要它】1.0 的 §5.3 只有两个**流量**口径：`savingTick`（本期结余）
	// 与 `savingInvestTick`（本期转入）。而 M1 第 3 条要求把
	// "**储蓄账户余额**"与"**已分配投资**"拆成两个口径——即需要**存量**视图。
	//
	// 【机械储蓄账户不能当存量用】`ledger.Savings()`（储蓄固定账户）的余额
	// **按设计每期归零**（σ_save = 1 时第二步全额转出，R20 的口径）。
	// 故它**不表达**"居民累计攒了多少"。
	//
	// 【做法：两个诊断账（不进 ledger，不影响任何过账）】
	//   savingsStock   —— 居民累计**攒下**的结余（§5.3 第一步的累计）
	//   savingsAlloc   —— 其中累计**已分配为投资**的部分（§5.3 第二步的累计）
	// 二者之差即"**已积累但尚未分配**"的储蓄余额。
	//
	// 【为什么不做成 ledger 账户】它会被算进 `Auditor.Total()`（= 货币总量），
	// 而这些钱**已经**在投资池/储蓄银行里、已被计入 ⇒ 会造成**重复计量**。
	// 与 `fiscal.Capital.Debt`、`BuildingState.GovLevel` 同族：**都是字段，不是钱**。
	//
	// 【默认不生效】`Params.SavingsStockTrack = false` 时两者恒为 0
	// ⇒ 不写入任何量 ⇒ 1.0 逐位不变。
	savingsStock float64
	savingsAlloc float64

	// ===== 1.2 §1.2-5 央行/金矿的诊断量（默认全为 0）=====
	//
	// goldProduced 是累计产出的黄金（盎司）；goldCarry 是"已产出但尚未满一批"的黄金，
	// 跨 tick 累加（见 `mintGold` 的口径说明）。
	goldProduced float64
	goldCarry    float64
	// goldMintedUnits 是累计**用于造币**的黄金单位数（= 批数 × GoldPerMint）。
	goldMintedUnits float64
	// mintedTotal 是累计创造的货币量（元）——货币注入，计入 InfusionTotal。
	mintedTotal float64
	// mintTick / goldPaidTick 是本 tick 的造币额与购金付款额（每 tick 开头清零）。
	mintTick     float64
	goldPaidTick float64
	// wageBillTick 是本 tick 的**工资总额**（满编口径的实付工资，见 step 的 ③）。
	// 它是 R86 的**造币锚**：`MintWageFraction × wageBillTick` = 每批造币额。
	wageBillTick float64
	// goldPaidPrev 是**上一 tick** 央行实付给金矿的购金款。
	//
	// 【为什么需要单独存】`goldPaidTick` 每 tick 开头被清零（它是"本 tick"的诊断量），
	// 而金矿的**收入信号**必须读**上一期已结算**的量（§8：用上一期结算量做决策信号）。
	// 若直接读 `goldPaidTick`，拿到的恒为 0 ⇒ 金矿 `LastRevenue = 0`
	// ⇒ 明明收着钱却显示零收入（R86 实测到该现象）。
	goldPaidPrev float64
	// centralBankExpandedTick 是本 tick 由"黄金有剩余"自动新增的央行级数（§1.2-5）。
	centralBankExpandedTick float64

	// ===== 1.2 M5 ① 政府债务利息（默认关闭）=====	//
	// govDebtInterestTick 是本 tick 支付的利息（每 tick 开头清零）；
	// govDebtInterestTotal 是累计值（诊断）。
	govDebtInterestTick  float64
	govDebtInterestTotal float64

	// goldMineSlotTick 是**诊断量**（R94）：金矿本 tick 在建造力分配里拿到的额度。
	//
	// 【为什么需要它】R93 发现"金矿盈利但永不扩建、且 0 个订单"，
	// 并已排除意图/利润信号/上限/预算/`PowerNeed` 五项。
	// 剩下最可能的一环是**建造力分配**：`slots` 按顺序消耗 `remaining`，
	// 用尽即 `break` ⇒ 排在后面的场地（金矿下标 15，最靠后）可能**永远拿不到额度**
	// ——即使它有预算、有意向。本字段把这个"拿到的额度"暴露出来，
	// 使"是分配问题"还是"其它问题"能被直接读出，而不必再靠推断。
	goldMineSlotTick float64
	// goldMineInSlotsTick 记录**金矿本 tick 的意向是否进入了 slots 列表**（诊断，R94）。
	//
	// 它把"金矿拿不到建造力"这件事分成两种完全不同的原因：
	//
	//	false ⇒ 意向**根本没进 lists*（被 `capByBudget` 或更早的过滤挡掉）
	//	true  ⇒ 意向进了列表，但**排在后面、额度被前面的槽位吃光**（分配顺序问题）
	goldMineInSlotsTick bool
	// welfarePaidTick 是本 tick 政府发放的福利金总额（§4.5.8）。
	welfarePaidTick float64
	// publicWorksSpendTick 是本 tick 公共工程采购建造力的国库支出（§4.5.8）。
	publicWorksSpendTick float64
	// publicWorksUnitsTick 是本 tick 公共工程新建的订单等级数（诊断）。
	publicWorksUnitsTick float64
	// publicWorksSoldTick 是本 tick 公共工程实际投入队列的建造力量（诊断；
	// 下一 tick 会在 ⓪ 被抓取到 powerPublicWorksPrev，用于建造部门的收入确认）。
	publicWorksSoldTick float64
	// budgetShareManor 是本 tick 投资池预算中**庄园栈的份额**（§4.5.1b 修订：
	// 由累计贡献 K_m:K_f 改为**当期意向需求**比例）。
	budgetShareManor float64
	// demandManorTick / demandFinanceTick 是本 tick 两条栈的**当期意向需求**
	// （建造力单位，已按每工地上限裁剪）——预算分配比例就是二者的比值（§4.5.1b）。
	// 保留为显式字段，供审计直接核对"预算比例 = 需求比例"，不必从日志反解。
	demandManorTick   float64
	demandFinanceTick float64
	// ShortageStart 标记本次开局是否采用"短缺起步"布点（§3.2/§8.6 第 15 轮裁决）。
	// 口径：起点产能远小于平衡产能（统一起始等级 N0 > 0 即视为短缺起步；
	// 物质平衡布点 N0 = 0 则不是）。它被显式标注在报告与快照里，
	// 使"从短缺起步"成为**声明的设计选择**，而不是看起来像标定误差。
	ShortageStart bool

	// ===== §4.5.6 仓库与消费代理（2026-09-19 第 16 轮落地）=====

	// tradeVolumeTick 是本 tick 通过仓库的**贸易量**（单位数，单向过手量）。
	//
	// 口径：入库量 = 出库量（同一 tick 内完成），故按单向计，不重复计双向。
	// 它只统计**经仓库**的商品（建造力走 §4.5.3 的政府直接采购，不经仓库）。
	tradeVolumeTick float64
	// warehouseQuotaTick 是本 tick 的贸易额度 = 仓库级数 × 每级额度。
	warehouseQuotaTick float64
	// warehouseGoodsInTick 是仓库本 tick **收到**的出库货款（含加价、不含消费税）。
	warehouseGoodsInTick float64
	// warehouseGoodsOutTick 是仓库本 tick **付出**的入库货款（含增值税）。
	warehouseGoodsOutTick float64
	// warehouseVATTick / warehouseConsumeTaxTick 是本 tick 两段税的税额（诊断）。
	warehouseVATTick        float64
	warehouseConsumeTaxTick float64
	// warehouseExpandUnitsTick 是本 tick 因额度不足而下达的仓库扩建订单等级数。
	warehouseExpandUnitsTick float64
	// warehouseExpandSpendTick 是本 tick 为仓库扩建而采购的建造力金额（国库支出）。
	warehouseExpandSpendTick float64
	// warehouseProfitTick 是仓库本 tick 的纯利（全归政府；亏损为负）。
	warehouseProfitTick float64
	// warehouseWageTick 是仓库本 tick 的工资支出（⑥ 计算纯利时用的那一份）。
	//
	// 【为什么必须单独存】仓库的等级可能在**同一 tick 的后半段**（⑫ 完工）增加，
	// 故 tick 末读到的 Level 与 ⑥ 计算纯利时用的 Level 可能差一级；
	// 审计要用"记账当时的工资额"才能逐位核对纯利。
	warehouseWageTick float64
	// lastManorDeposit 是宅邸庄园本 tick 从入库中分得的净额（§4.5.5 的收入）。
	//
	// 仓库落地后庄园的收入不再是"消费者货款里的自给份额"，而是**入库款里的自给份额**
	// （中间投入那一路也一并计入——这正是 R21 记录缺口的关闭处）。
	lastManorDeposit float64

	// ===== §7.2 实际工农生产总值（2026-09-19 第 18 轮）=====

	// productTick 是本 tick 的**实际工农生产总值**核算结果（按固定 P_ref 计价）。
	//
	// 它与 §7 的名义 GDP 并列：名义口径随价格水平漂移，本口径只依赖实物量，
	// 故用于"去除货币的干扰"地判断经济是否真的在增长（见 internal/product 包注释）。
	productTick product.Result
	// productBaseAdded 是**首个 tick** 的工农增加值，用作指数的基期。
	//
	// 取首 tick 而不是"开局理论值"：开局尚未生产，理论值为 0，无法作分母。
	productBaseAdded float64
	// Unemployed 是既有劳动力中未被任何人雇佣的人数（诊断用）。
	Unemployed float64
	// 旧口径的"消费者货款全部记给专业生产者"的残差修正见 consumerSellerShares。

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
	privatizePaid      float64

	// ===== §4.5.1b 投资池与两条投资栈 =====

	// KManor / KFinance 是两条栈的**累计贡献**（各自历次入池额之和）。
	//
	// 投资池的可动用额按 K_m : K_f 分配给两条栈（§4.5.1b）：
	// 庄园栈 = P·K_m/(K_m+K_f)，金融栈 = P − 庄园栈；K_m+K_f ≤ 0 时各 0.5。
	KManor   float64
	KFinance float64

	// tickInflowManor / tickInflowFinance 是本 tick 两条栈的入池额（流量）。
	tickInflowManor   float64
	tickInflowFinance float64
	// tickInvestmentPaid 是本 tick 投资池付给政府的建造力货款（G6）。
	tickInvestmentPaid float64
	// powerPaidManor / powerPaidFinance 是本 tick 各栈支付的建造力货款（诊断）。
	powerPaidManor   float64
	powerPaidFinance float64

	// powerNeedTick 是本 tick 队列**实际需要的建造力量**（已按每工地上限与
	// 各栈预算裁剪，但**未**按产出/可动用资金裁剪）——G2 的采购量是它的进一步裁剪。
	powerNeedTick float64
	// powerAvailTick 是采购时刻政府可动用的资金（§4.5.4），供审计复算采购量。
	powerAvailTick float64

	// ===== §七 R28：投资池与国库恒 ∞ 的诊断开关 =====

	// unlimitedFunds 见 Options.UnlimitedFunds。开启时投资池每 tick 补到哨兵水位、
	// 且 G2 的「可动用资金」裁剪被解除（采购只受队列需要量与当期产出限制）。
	unlimitedFunds bool
	// dynamicPcost 见 Options.DynamicPcost（§七 R32 的当期零利润价口径）。
	dynamicPcost bool
	// infusionTick / infusionTotal 是诊断注入的流量与累计（元）。
	//
	// 它们与 §4.3 的营运本金分开计量：开启 UnlimitedFunds 时，
	// A8 的货币守恒恒等式读作 `ΔM == NewCapital + 诊断注入`；
	// 开关关闭时两值恒为 0，原恒等式逐位不变。
	infusionTick  float64
	infusionTotal float64
	// powerBoughtPrev 是【上一 tick】的建造力采购量（在 ⓪ 清零流量前抓取）。
	//
	// 用途：建造部门的收入确认。采购发生在 ⑩（利润归属 ⑥ 之后），
	// 故本期 ⑥ 只能用上一期的采购量作为它的销量（详见 step ⑥ 的说明）。
	powerBoughtPrev float64

	// powerPublicWorksPrev 是【上一 tick】公共工程（§4.5.8）采购的建造力量。
	//
	// 与 powerBoughtPrev 同理：公共工程的采购发生在 ⑩，而建造部门的收入确认在 ⑥，
	// 故本期 ⑥ 只能看到上一期的量。二者相加构成建造部门上期的全部销量。
	powerPublicWorksPrev float64

	// 政府池资金流分解的三路【账户实际变动】（每 tick 开头清零）。
	//
	// 全部用"钱真的动了多少"计量，不用公式推算——这样
	// "政府池 Δ = 各路之和"按构造成立，留下的残差一定是真正的未知资金流。
	flowGovOperatingDelta float64
	// flowGovPurchaseDelta 是政府采购建造力【实际流出政府池】的金额（G2）。
	flowGovPurchaseDelta float64
	// flowGovInvestDelta 是投资池偿还政府的金额（G6，实际入账）。
	flowGovInvestDelta float64

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

// infuse 给账户注入**诊断资金**（仅 Options.UnlimitedFunds 的对照实验使用，§七 R28）。
//
// 它与 §4.3 的营运本金走同一机制（PostInjection：允许借贷不等的单边分录），
// 但累计到 infusionTotal 而不是 book 的营运本金计数器，理由是让货币守恒的
// 恒等式在开启该开关时仍然可写、可核：
//
//	关闭开关：ΔM == NewCapital
//	开启开关：ΔM == NewCapital + 诊断注入
//
// 这样"无限资金"是一个**被显式计量**的外部假设，而不是一笔来路不明的钱。
func (s *State) infuse(acc ledger.Account, amount float64) {
	if amount <= 0 || s.Aud == nil {
		return
	}
	t := &ledger.Txn{Name: "诊断注入（无限资金实验）"}
	t.Credit(acc, amount)
	s.PostInjection(t)
	s.infusionTick += amount
	s.infusionTotal += amount
}

// InfusionTotal 返回累计的**货币注入**额（= 诊断注入 + 中央银行的造币）。
//
// 【R87 更正：它必须包含造币】此前它只统计 `UnlimitedFunds` 诊断注入，
// 而 A8 的货币守恒恒等式是
//
//	ΔM == NewCapital + InfusionTotal
//
// 造币是**第二处**货币创造（1.2 §1.2-5），若不进这个口径，
// 守恒审计会报一个**恰等于造币额**的残差。
// 实测（R87 的 `TestAuditCentralBankMinting`）：货币守恒差 **84,981,088.07**，
// 与累计造币额**逐位相同** —— 正是这里漏计的。
func (s *State) InfusionTotal() float64 { return s.infusionTotal }

// refreshPzero 重算全部商品的**当期零利润价**（§七 R32）。
//
//		P⁰_j(t) = Σ_i A[i][j]·P_i(t) + l_j
//
//	  - A[i][j] = 商品 i 每单位商品 j 的投入（calibrate.InputMatrix，**含自投入**，
//	    例如煤矿烧煤——故必须用**当期**价格估值，不能解固定点）；
//	  - l_j = 单位劳动成本 = b.WagePerLevel() / 单级产出（满编口径，§3.4）；
//	    其中 WagePerLevel() 随建筑的劳动结构而不同（§5 第 20 轮：农业 28,250、
//	    城镇 33,750）。
//	  - P_i(t) = **上一 tick 结算后的当期价格**（本 tick 的价格方程尚未跑）。
//
// 这就是"让人均满编的该生产单位恰好 0 利润"的售价。它与静态 P_cost 的差别在于：
// 静态解用"全部投入品也处于各自零利润价"这一长期条件（Leontief 固定点），
// 当期解只用**今天的实际投入价格**。当上游价格偏离长期值（例如被钳制带截住）时，
// 两者会分叉——这正是 §3.4 记录的"地板脱节（P2）"的机制。
func (s *State) refreshPzero() {
	if s.calibration == nil {
		return
	}
	a := s.calibration.A
	prices := s.Market.Prices()
	for j, b := range s.Buildings {
		if j >= model.Goods || !b.Spec.Produces() || b.Spec.Recipe.Qty <= 0 {
			continue
		}
		var pz float64
		for i := 0; i < model.Goods && i < len(a); i++ {
			if j < len(a[i]) && i < len(prices) {
				pz += a[i][j] * prices[i]
			}
		}
		// 【§4.5.6】投入按**买家的实际成本**计：仓库加价与消费税都落在买家身上，
		// 故零利润价方程是 P⁰ = w·Σ A·P + l（w = 买家加载系数）。
		pz = s.Params.BuyerWedge()*pz + s.effectiveWagePerLevel(j)/b.Spec.Recipe.Qty
		s.Market.SetPzero(j, pz)
	}
}

// UnlimitedFundsDiagnostic 报告"无限资金"诊断开关是否开启（§七 R28）。
func (s *State) UnlimitedFundsDiagnostic() bool { return s.unlimitedFunds }

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

// balCap 返回资本现金池余额（= 金融区的营运现金池，§4.5.1b）。
func (s *State) balCap() float64 { return s.Aud.Balance(ledger.Capital()) }

// balInvest 返回投资池余额（§4.5.1b）。
func (s *State) balInvest() float64 { return s.Aud.Balance(ledger.Investment()) }

// balSavings 返回居民储蓄固定账户余额（§5.3 第 20 轮）。
func (s *State) balSavings() float64 { return s.Aud.Balance(ledger.Savings()) }

// balHouse 返回某人群池余额。
func (s *State) balHouse(i int) float64 {
	return s.Aud.Balance(s.Houses.Account(i))
}

// balBuildingTotal 返回全部建筑现金池之和。
func (s *State) balBuildingTotal() float64 { return s.Aud.TotalOf(ledger.KindBuilding) }

// TickNewCapitalTotal 返回累计注入的营运本金（§4.3，唯一合法的货币注入）。
func (s *State) TickNewCapitalTotal() float64 { return s.totalNewCapital }

// InvestmentPool 返回投资池余额（§4.5.1b）。
func (s *State) InvestmentPool() float64 { return s.balInvest() }

// KManorTotal / KFinanceTotal 返回两条投资栈的累计贡献（§4.5.1b）。
func (s *State) KManorTotal() float64   { return s.KManor }
func (s *State) KFinanceTotal() float64 { return s.KFinance }

// TotalLevels 返回全部建筑的等级之和。
func (s *State) TotalLevels() float64 {
	var t float64
	for i := range s.Buildings {
		t += s.Buildings[i].Level
	}
	return t
}

// TickRecon 记录一个 tick 内五个货币持有池的实际 Δ 与应有 Δ。
//
// 用途：定位货币守恒缺口【落在哪一个池】。
// 五者残差之和必须等于总货币的实际 Δ；残差非零的池即为漏点所在。
type TickRecon struct {
	GovDelta, CapDelta, HouseDelta, BuildDelta, InvestDelta float64
	BuildExpected                                           float64
}

// TotalDelta 返回五池实际 Δ 之和（即总货币的真实变化）。
func (t *TickRecon) TotalDelta() float64 {
	return t.GovDelta + t.CapDelta + t.HouseDelta + t.BuildDelta + t.InvestDelta
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
	// ===== §4.5.6 仓库路径（2026-09-19 第 16 轮）=====
	//
	// ConsumerRevenue 现在是"仓库从消费代理收到的 base"（消费者实付的税前部分），
	// IntermediateIn 是"仓库从生产买家收到的 base"，二者都是仓库的**流入**；
	// 仓库的流出是 DepositGross，生产者的流入是 DepositNet。
	//
	//	DepositNet   —— 入库净额（生产者实收合计，正向）
	//	DepositGross —— 入库含税总额（仓库实付合计，负向）
	//	ConsumerTaxWh / InputTaxWh —— 两段税的税额（诊断，已含在 Gov.TaxCollected）
	DepositNet    float64
	DepositGross  float64
	ConsumerTaxWh float64
	InputTaxWh    float64
	// SubsidyPaid 是政府补贴入账合计（§4.5.7；建筑池的流入，聚合口径）。
	//
	// 【为什么必须进汇总式】补贴是"借政府 / 贷建筑"的转移支付，建筑池确有流入；
	// 漏记它会让聚合对账残差恰等于当期补贴额。
	SubsidyPaid float64
	// InvestmentOut 是**建筑池**流向投资池的金额（只有宅邸庄园有这一腿）。
	//
	// 金融区的入池款从 ledger.Capital() 转出，不经过任何建筑池，
	// 故不计入本项（它体现在 TickRecon.CapDelta 上）。
	InvestmentOut float64
	// ProfitIn 是本 tick 利润归属中**作为所属资本建筑**收到的私人份额合计
	// （§4.5.1；只有宅邸庄园这一路进建筑池，见 BuildingRecon.ProfitIn）。
	ProfitIn float64
	// ManorInflow / FinanceInflow 是本 tick 两条栈的入池额（管理口径报表字段）。
	ManorInflow   float64
	FinanceInflow float64

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
	// Retained 是利润归属给该建筑的**补足额** R_i（正值 = 贷方份额）。
	//
	// 【注意它不是建筑池的净腿】book.ProfitAllocate 的记账是
	// 「借 建筑[i] 纯利、贷 建筑[i] 补足额」，故建筑池的净腿恰为
	// `补足 − 纯利 = −(政府份额 + 所有者份额)`，由 ProfitLeg 单独记录。
	Retained float64
	// ProfitLeg 是利润归属对【该建筑现金池】的净影响（= 补足额 − 纯利）。
	//
	// 之所以必须单列：Retained 只是三份额之一，而现金池实际发生的是
	// 借全额、贷补足两腿相抵后的净额。把 Retained 当净额会让逐建筑对账
	// 在利润为负时出现 2×|利润| 量级的假残差（实测 213 万）。
	ProfitLeg float64
	// Wage 是工资支出（负向）。
	//
	// 【金融区除外】金融区的工资从 ledger.Capital() 支付（§4.5.1b 的场地账户
	// 口径），不动任何建筑池，故不计入本项，也不计入 Recon.WagePaid。
	Wage float64
	// InputOut 是中间投入付款含税（负向）。
	InputOut float64
	// InputIn 是作为卖方收到的中间投入货款（正向）。
	InputIn float64
	// ConsumerIn 是作为卖方收到的消费者货款（正向）。
	ConsumerIn float64
	// ===== §4.5.6 仓库路径（2026-09-19 第 16 轮）=====
	//
	// 仓库落地后，生产者的销售收入**不再**直接来自买家（ConsumerIn / InputIn 因此
	// 在生产建筑上恒为 0），而是来自仓库的**入库款**；买家也不再付给生产者，
	// 而是付给仓库（出库款）。三者是三条独立的腿，必须分别登记：
	//
	//	DepositIn    —— 生产建筑从入库中分得的净额（正向）
	//	WarehouseIn  —— 仓库从出库中收到的 base（正向，只有仓库有）
	//	WarehouseOut —— 仓库付出的入库款（含增值税，负向，只有仓库有）
	DepositIn    float64
	WarehouseIn  float64
	WarehouseOut float64
	// PowerNet 是建造力买卖的净额（正向为收入）。
	PowerNet float64
	// NewCapital 是新完工建筑的营运本金注入。
	NewCapital float64
	// Subsidy 是政府补贴入账（§4.5.7，正向 = 贷方）。
	//
	// 补贴是转移支付：它使建筑现金池增加，必须进入逐建筑对账的应得 Δ，
	// 否则逐建筑残差会恰好多出一笔补贴额（与当年漏记消费税款同类）。
	Subsidy float64
	// InvestmentOut 是流向投资池的入池款（§4.5.1b，负向；只有宅邸庄园有）。
	InvestmentOut float64
	// ProfitIn 是该建筑**作为所属资本建筑**收到的私人份额纯利（§4.5.1，正向）。
	//
	// 【只有宅邸庄园有这一腿】农业建筑的私人份额纯利由 ProfitAllocate 贷记
	// ledger.Building(ManorIndex)；金融区的同类收入直接进 ledger.Capital()，
	// 不经过任何建筑池，故不计入本项（它体现在 TickRecon.CapDelta 上）。
	// 漏记它的后果与漏记消费税款同类：逐建筑残差恰好等于该笔利润（实测 761,834）。
	ProfitIn float64
}

// Expected 返回该建筑按明细算出的应有 Δ。
//
// 【口径】逐条列出该建筑现金池本 tick 实际发生的资金腿：
//
//   - 消费收入、中间投入收入、**入库净额**、**出库收款**、建造力净额、
//     新资本（唯一注入）、利润归属净腿、补贴、作为所属资本建筑收到的私人份额纯利
//     − 工资、中间投入付款（含税）、**入库付款（含税）**、入池款（§4.5.1b）
//
// 其中利润归属净腿是 ProfitLeg（= 补足额 − 纯利），**不是** Retained——
// 记账用的是"借全额纯利、贷补足额"两条腿，见 BuildingRecon.ProfitLeg 的说明。
//
// 【§4.5.6 仓库路径】生产建筑的 ConsumerIn / InputIn 恒为 0（销售收入改走 DepositIn），
// 仓库则有 WarehouseIn（出库收款）与 WarehouseOut（入库付款）。
func (b BuildingRecon) Expected() float64 {
	return b.ConsumerIn + b.InputIn + b.DepositIn + b.WarehouseIn + b.PowerNet +
		b.NewCapital + b.ProfitLeg + b.Subsidy + b.ProfitIn -
		b.Wage - b.InputOut - b.WarehouseOut - b.InvestmentOut
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
//   - 消费收入(仓库从消费代理收到) + 中间投入收入(仓库从生产买家收到)
//   - 入库净额(生产者收到) + 建造力收入(净) + 新资本 + 利润划分净腿
//     − 工资 − 中间投入付款(含税) − 入库付款(含税) − 建造力支出(净)
//
// 【§4.5.6】仓库路径把"销售"拆成两条腿：买家付给仓库（IntermediateIn / ConsumerRevenue）、
// 仓库付给生产者（DepositNet / DepositGross）。两条腿都发生在**建筑池内部**，
// 故汇总式必须同时计入，否则残差恰等于"仓库付出 − 仓库收到"。
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
	return r.ConsumerRevenue + r.IntermediateIn + r.DepositNet + r.PowerRevenue +
		r.ProfitLegTotal + r.NewCapital + r.ProfitIn + r.SubsidyPaid -
		r.WagePaid - r.IntermediateOut - r.DepositGross - r.PowerSpend - r.InvestmentOut
}

// FlowDiag 记录一个 tick 内的资金流分解。
type FlowDiag struct {
	// GovOperating 是政府建筑的经营净额（可为负）。
	GovOperating float64
	// GovTax 是税收。
	GovTax float64
	// GovPowerSpend 是政府采购建造力的支出（G2，按需采购，不计税）。
	GovPowerSpend float64
	// GovPowerRevenue 是投资池偿还的建造力货款（G6，全部进政府池）。
	//
	// 【恒等式（供审计断言）】§4.5.3 改写后，建造力交易不计税、也不存在
	// "政府自己收自己"的自反税腿；2026-09-19 第 15 轮裁决又给政府补上了
	// 支出端（福利金 + 公共工程），故政府池的分解式现为：
	//
	//	Δ政府 = GovTax + GovOperating + GovPowerRevenue
	//	        − GovPowerSpend − GovSubsidy − GovWelfare − GovPublicWorks − GovWarehouseExpand
	//	        + PrivatizePaid
	//
	// 前值（本轮之前）的分解式含 GovSelfTax 与 GovBuildoutPaid 两项，
	// 二者对应"整批采购 + 转售 + 政府自建"的旧口径；该口径已删除。
	GovPowerRevenue float64
	// CapitalProfit 是归金融区的私人份额纯利合计（非农业建筑，可为负）。
	CapitalProfit float64
	// ManorProfit 是归宅邸庄园的私人份额纯利 + 自给产出货款（农业建筑，可为负）。
	ManorProfit float64
	// WageTotal 是全社会工资。
	WageTotal float64
	// SpendNet 是消费者税前支出。
	SpendNet float64
	// PowerOut 是建造部门当期产出。
	PowerOut float64
	// PowerBought 是政府采购量，PowerSold 是即买即用投入队列的量。
	PowerBought, PowerSold float64
	// PowerNeed 是本 tick 队列实际需要的建造力量（G2 的采购量是它被
	// 产出与可动用资金裁剪后的结果）。
	PowerNeed float64
	// PowerAvail 是采购时刻政府可动用的资金（§4.5.4）。
	PowerAvail float64
	// WorstSector / WorstMargin 是本期利润率最低的部门。
	WorstSector int
	WorstMargin float64
	// BestSector / BestMargin 是本期利润率最高的部门。
	BestSector int
	BestMargin float64

	// ConsumerTax 是消费环节收取的交易税（已含在 GovTax 内，单列供核对）。
	ConsumerTax float64
	// InputNet / InputTax 是中间投入环节的货款净额与税额。
	InputNet, InputTax float64
	// GovSubsidy 是本 tick 支付的政府补贴（§4.5.7，政府池的【净流出】，正数表示流出）。
	GovSubsidy float64
	// ===== §4.5.6 仓库路径（2026-09-19 第 16 轮）=====
	//
	// 贸易量、额度、仓库收支与两段税，全部取【记账事实】而不是公式推算。
	TradeVolume          float64 // 本 tick 过库贸易量（单位数，单向）
	TradeQuota           float64 // 贸易额度 = 仓库级数 × 每级额度
	WarehouseIn          float64 // 仓库出库收款（含加价、不含消费税）
	WarehouseOut         float64 // 仓库入库付款（含增值税）
	VATCollected         float64 // 增值税 ν（入库环节，已含在 GovTax 内，单列供核对）
	ConsumeTaxCollected  float64 // 消费税 τ（出库环节，已含在 GovTax 内，单列供核对）
	WarehouseCash        float64 // 仓库期末现金池
	AgentCash            float64 // 消费代理期末余额（恒为 0）
	WarehouseExpandSpend float64 // 本 tick 因额度不足而下达的仓库扩建采购额
	WarehouseExpandUnits float64 // 本 tick 新建的仓库扩建订单等级数
	// GovWarehouseExpand 是仓库自动扩建的国库支出（§4.5.6）。
	//
	// 【为什么单列】它与公共工程同性质（政府订单、不被投资池偿还），
	// 但由**额度规则**触发而不是公共工程预算。政府现金流分解式必须计入它，
	// 否则残差恰等于当期仓库扩建支出（实测 tick 1 残差 −242,205.03）。
	GovWarehouseExpand float64
	// GovWelfare 是本 tick 支付的福利金（§4.5.8，政府池净流出，正数表示流出）。
	GovWelfare float64
	// GovPublicWorks 是本 tick 公共工程采购建造力的支出（§4.5.8，政府池净流出；
	// 与 G2 不同，它**没有**投资池偿还，故是政府的真实支出）。
	GovPublicWorks float64
	// HouseSaving 是本 tick 居民工资结余**全额**进入储蓄固定账户的金额（§5.3 第一步，
	// 人群池净流出）。
	HouseSaving float64
	// SavingsToInvest 是本 tick 从储蓄固定账户转入投资池的金额（§5.3 第二步
	// = σ_save × HouseSaving；σ_save = 1 时两者相等）。
	SavingsToInvest float64
	// Happiness 是按人口加权的幸福度（§6.5：四组满足度均值）。
	Happiness float64
	// HappinessByClass 是各阶级的幸福度（失业者算劳工）。
	HappinessByClass [3]float64
	// NoIncomePools 是"有人口但无收入、无法消费"的池数（§6.5 诊断）。
	NoIncomePools int
	// HouseCash 是期末人群现金池总额（居民的货币存量）。
	HouseCash float64
	// Sat 是按人口加权的四组满足度（§6.5 人口增长的输入）。
	Sat [4]float64

	// ===== §4.5.1b 投资池诊断字段 =====

	// InvestmentInflowManor / InvestmentInflowFinance 是本 tick 两条栈的入池额。
	InvestmentInflowManor   float64
	InvestmentInflowFinance float64
	// InvestmentPaid 是本 tick 投资池付给政府的建造力货款（G6）。
	InvestmentPaid float64
	// InvestmentPool 是期末投资池余额。
	InvestmentPool float64
	// KManor / KFinance 是两条栈的累计贡献（含本期）。
	KManor, KFinance float64
	// PowerPaidManor / PowerPaidFinance 是本 tick 各栈支付的建造力货款。
	PowerPaidManor, PowerPaidFinance float64

	// ===== §4.5.1 修订：私有化诊断字段 =====

	// PrivatizeEnabled 是本次运行的私有化总开关状态。
	PrivatizeEnabled bool
	// PrivatizeUnits 是本 tick 转让的等级数（股权腿）。
	PrivatizeUnits float64
	// PrivatizePaid 是本 tick 支付的对价（现金腿，含税）。
	PrivatizePaid float64
	// GovShareAfter 是期末政府持股比例（按级数加权，诊断用）。
	//
	// 它的下降速度直接反映私有化强度与扩建稀释；配合逐建筑的 AllowPrivatize，
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

// refPrices 返回**固定的参考价向量** P_ref（§3.4 的零利润价解）。
//
// 【为什么用它给实际产出计价】P_ref 在开局解一次后**不再随价格变动**，
// 故"实物量 × P_ref"是一个与货币量、价格水平、税率无关的**实际**口径。
// 标定缺失时回退到契约表格的 GoodSpecs（同样固定）。
func (s *State) refPrices() []float64 {
	if s.calibration != nil && len(s.calibration.Pcost) >= model.Goods {
		return s.calibration.Pcost
	}
	out := make([]float64, model.Goods)
	for i, g := range s.Goods {
		if i < model.Goods {
			out[i] = g.Pcost
		}
	}
	return out
}

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
	// AnchorExpenditureShare 覆盖 §3.4 的方案 A 开关（nil = 用契约默认：开启）。
	//
	// 契约已裁决采用 A + C′，故默认开启；显式传 false 可回退到历史常弹性锚（对照实验用）。
	AnchorExpenditureShare *bool
	// AnchorDerivedDemand 覆盖 §3.4 的方案 C′ 开关（nil = 用契约默认：开启）。
	AnchorDerivedDemand *bool
	// InitPriceMult 是**诊断开关**（§七 R31 的对照实验，不是契约参数）：
	// 把**开局价 P_init 整体放大**该倍数（0 或 1 = 契约默认）。
	//
	// §3.4 的开局价由加成价方程解出（`openerMarkup = 1/6` ⇒ P_init ≈ 1.2·P_cost），
	// 本开关作用在**解出来的 P_init 上**，并同时生效于：
	//
	//	① 市场初价（market.State 的 Price）；
	//	② 需求锚 a = S₀·(P_init/P_cost)^ε（§3.4 步骤 3）——故长期均衡价同倍放大；
	//	③ 报告里的开局利润率（P_init 下的 margin）。
	//
	// 它**不动** P_cost（零利润价由工资与配方反推，是价格体系的地板基准），
	// 故倍数 = 3 意味着"开局加成从 1.2× 抬到 3.6×"；开局利润率不是统一的 200%，
	// 而是 **38.9%~260%**——因为成本里的**工资项不随价格缩放**，
	// 劳动密集部门（谷物/织物）抬得最多，原料密集部门（钢）抬得最少。
	InitPriceMult float64

	// StaticPcost 是**对照开关**（契约 §2.4 / §3.4 的"零利润价两个角色"）。
	//
	// 默认 false = **现行 1.0 口径**：价格钳制带与 A1/A3 判据的参考价用
	// **当期零利润价**（"让该生产单位在当期投入价格与满编工资下恰好 0 利润的售价"）
	//
	//	P⁰_j(t) = Σ_i A[i][j]·P_i(t) + l_j
	//
	// 置 true 可回退到 2026-09-19 之前的旧口径（钳制带与判据都用固定 P_cost），
	// 仅用于对照实验。
	//
	// 【两个角色，只有 B 可以动态】需求归一化的参考价 $P_{ref}$ 必须**固定**：
	// 把它也换成当期成本会形成"价格 → 成本 → 需求 → 价格"的自指正反馈
	// （实测税收 ×5、价格水平自我抬升，见 docs/ACTIVE.md §七 R32）。
	StaticPcost bool
	// FinanceLaborPerLevel 是金融区每级雇佣人数（G5，默认 1000）。
	//
	// 【2026-09-19 裁决】金融区不建造，故**没有**建造成本参数
	// （原 FinanceBuildCost 已删除）。
	FinanceLaborPerLevel float64
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

	// UnlimitedFunds 是**诊断开关**（§七 R28 的对照实验，不是契约参数）：	// 把"投资池"与"国库"都当作恒 ∞，使建造链只受【实物产能】限制。
	//
	// 开启后的行为：
	//   - 每 tick 把投资池补到哨兵水位（1e12），故两条投资栈的预算永不成为约束；
	//   - 解除 G2 的「可动用资金」裁剪，故国库（含债务上限）不再约束采购量，
	//     采购只由**队列需要量**与**建造力当期产出**决定。
	//
	// 【记账纪律】注入额单独计量（State.InfusionTotal），不走 §4.3 的营运本金计数器；
	// 开启时 A8 的恒等式读作 `ΔM == NewCapital + 诊断注入`，关闭时与原来完全一致。
	UnlimitedFunds bool

	// ===== §5.2 增雇口径（2026-09-19 第 22 轮）=====

	// NoExpandDilution 关闭"扩招压价折现"，使 §5.2 的增雇判定退化为
	// **第 20 轮旧口径**（只看当期利润率 EMA > 0）。它是审计用的历史对照臂。
	NoExpandDilution bool
	// ExpectedMarginFloor 是增雇判定的下限（默认 0 = 期望扩招后利润率严格为正）。
	ExpectedMarginFloor float64
	// ExpandPlanHorizon 是增雇的前瞻步数（默认 1；见 model.Params 的字段说明）。
	ExpandPlanHorizon int
	// BasketScale 是 §6.3 需求篮子的整体缩放系数（默认 1.0；见 model.Params 字段说明）。
	BasketScale float64
	// ===== 1.2 借贷台账（M8）：默认全关 =====
	//
	// BankEnabled / Loan* 对应 model.Params 的同名字段（见那里的说明）。
	// 默认值为零值/显式默认，**保证 1.0 基线逐位不变**。
	BankEnabled         bool
	LoanPrincipal       float64
	LoanAnnualRate      float64
	LoanTermYears       float64
	LoanIssueInterval   int
	LoanFromBalance     bool
	LoanBalanceFraction float64
	// ===== 1.2 M4.2 所有权重构（默认关闭）=====
	OwnershipRestructure  bool
	OwnershipCapitalShare float64
	// ===== 1.2 配置档（R80）=====
	//
	// Profile12 一次性打开全部 1.2 行为（四个开关），使"1.2 的完整配置"
	// 有一个**单一入口**。默认 false ⇒ 与 1.0 基线逐位相同。
	Profile12 bool
	// 四个**显式覆盖**指针：非 nil 时以指针为准，优先级高于 Profile12。
	//
	// 用 `*bool` 而不是 `bool`：Go 的零值无法区分"没设置"与"显式设为 false"，
	// 而"1.2 但关掉某个开关"这个对照实验正需要后者。
	// 与既有的 `AnchorExpenditureShare *bool` 同法。
	BankEnabledOverride     *bool
	OwnershipOverride       *bool
	WageBidOverride         *bool
	LoanFromBalanceOverride *bool
	// SavingsStockTrack 打开 1.2 M1 第 3 条的储蓄**存量**口径（默认 false）。
	SavingsStockTrack bool
	// ===== 1.2 §1.2-5 央行/金矿（默认关闭）=====
	CentralBankEnabled       bool
	GoldMineLaborPerLevel    float64
	CentralBankLaborPerLevel float64
	GoldPrice                float64
	GoldPerMint              float64
	MoneyPerMint             float64
	MintWageFraction         float64
	// ===== 1.2 M3/M6 玩家投资接口（默认关闭）=====
	//
	// 【为什么用 `InvestAIOff` 而不是 `InvestAIEnabled bool`】Go 的零值 `false`
	// 会让"没设置"与"显式关闭 AI"无法区分，而后者正是本接口的目的
	// （M6："当玩家控制时**自动关闭**该 AI"）。用一个**反向命名**的布尔量，
	// 零值即 = "AI 仍开启" = 1.0 基线口径。
	InvestAIOff bool
	// InvestManorShare 是玩家指定的庄园栈预算占比 ∈ [0,1]；**nil = 未设定**，交回 AI。
	//
	// 【为什么必须用 `*float64`（R89 实测的坑）】我第一版用 `float64` 并约定
	// "−1 = 未设定"。但**调用方不写这个字段时，Go 的零值是 0**，而 0 是一个
	// **有效的玩家选择**（全给金融）⇒ `opt.InvestManorShare >= 0` 恒为真
	// ⇒ 每一局都被当成"玩家选了全给金融"，把 AI 口径**静默覆盖**掉。
	// 实测：`New(Options{InvestAIOff: true})` 得到 `InvestManorShare = 0`，
	// 而 `DefaultParams()` 是 −1 ⇒ 连"只关 AI、不给方向"都变成了显式方向。
	//
	// 指针能区分"未设置"（nil）与"显式 0"，与 `AnchorExpenditureShare *bool` 同法。
	InvestManorShare *float64
	// ===== 1.2 M5 ① 政府债务计息（默认关闭）=====
	GovDebtInterestEnabled bool
	GovDebtInterestRate    float64
	// InflationDeflation 打开 1.2 的**工资平减**（默认 false ⇒ 1.0 逐位不变）。
	InflationDeflation bool
	// ===== 1.2 M7 工资竞标（默认关闭）=====
	WageBidEnabled bool
	WageBidCap     float64
	WageBidDecay   float64
	WageBidBase    string
	// WageBidMarginCap 参与抬价的利润率因子上限（默认 0 = 不封顶）。见 `Params.WageBidMarginCap`。
	WageBidMarginCap float64
	// CapStartupFraction 覆盖资本（金融区）现金池的初始规模（单位：周工资倍数）。
	//
	// 【为什么需要它】§4.5.1a 的私有化可行性直接由**资本池规模**决定
	// （对价 = 建造成本 × 建造力价；资本池没钱就一笔都成交不了）。
	// 默认值由包内常量 `startupCapFraction = 0.25` 给出；> 0 时本字段覆盖它，
	// 用于把"资本池不足"这一闸门单独隔离出来做对照实验（第 26 轮新增）。
	CapStartupFraction float64
	// PowerByQueue / PowerAnchorByQueue 是 §4.1/§3.4 第 23 轮的建造部门队列口径开关。
	//
	// 【零值语义】两者在 `DefaultParams` 里都是 **true**，故本字段用"指针 = 显式覆盖"：
	// nil 表示沿用默认（true），&false 才是"关掉它做对照"。
	PowerByQueue       *bool
	PowerAnchorByQueue *bool

	// ===== §4.5.6 税制与仓库加价（2026-09-19 第 17 轮：让税制可替换）=====
	//
	// 【为什么必须有这三个入口】现行两段税（增值税 ν + 消费税 τ）与 5% 加价是
	// **抽象替身**，后续会改成具体税种。它们只在三处进入模型：
	//
	//	① 买家的实际付款（出库与入库交易的记账腿）；
	//	② §3.4 的标定（价格方程 p = w·Aᵀp + l、加成价、§3.1 的价格两列）；
	//	③ 决策/判据用的利润率口径（`fullCapacityCosts`）。
	//
	// 后两者共用 **买家加载系数** w = (1+加价)(1+消费税)，所以只要能在**标定之前**
	// 改掉这三个参数，换税种就退化为"改一个 w + 改交易腿"。若只在 New 之后改
	// Params（旧做法），标定与运行时口径就会分叉（价格表按旧 w 解、交易按新 w 记）。
	//
	// 三个字段用**指针**：nil = 用契约默认（ν=τ=2.5%、加价 5%）；显式 0 表示免税/不加价。
	VATRate         *float64
	ConsumeTaxRate  *float64
	WarehouseMarkup *float64
}

// New 构造一个完成标定与开局布点的仿真状态。
func New(opt Options) (*State, error) {
	goods := model.GoodSpecs()
	if err := model.ValidateGoods(goods); err != nil {
		return nil, err
	}
	p := model.DefaultParams()
	// 【§4.5.6 / 第 17 轮】税制与加价必须在**标定之前**生效：
	// 它们进入 §3.4 的买家加载系数 w = (1+加价)(1+消费税)，而 w 决定价格表与利润率。
	if opt.VATRate != nil {
		p.VATRate = *opt.VATRate
	}
	if opt.ConsumeTaxRate != nil {
		p.ConsumeTaxRate = *opt.ConsumeTaxRate
	}
	if opt.WarehouseMarkup != nil {
		p.WarehouseMarkup = *opt.WarehouseMarkup
	}
	if opt.SubsistenceScale != nil {
		p.SubsistenceScale = *opt.SubsistenceScale
	}
	if opt.InitialPowerLevel > 0 {
		p.InitialPowerLevel = opt.InitialPowerLevel
	}
	if opt.ProductionInitLevel >= 0 {
		p.ProductionInitLevel = opt.ProductionInitLevel
	}
	// §5.2 第 22 轮：增雇口径（默认沿用 Params 的第 22 轮值）
	if opt.NoExpandDilution {
		p.NoExpandDilution = true
	}
	if opt.ExpectedMarginFloor != 0 {
		p.ExpectedMarginFloor = opt.ExpectedMarginFloor
	}
	if opt.ExpandPlanHorizon > 1 {
		p.ExpandPlanHorizon = opt.ExpandPlanHorizon
	}
	if opt.PowerByQueue != nil {
		p.PowerByQueue = *opt.PowerByQueue
	}
	if opt.PowerAnchorByQueue != nil {
		p.PowerAnchorByQueue = *opt.PowerAnchorByQueue
	}
	if opt.BasketScale > 0 {
		p.BasketScale = opt.BasketScale
	}
	// 1.2 借贷台账（M8）：总开关与条款覆盖（默认全关 ⇒ 1.0 逐位不变）
	if opt.BankEnabled {
		p.BankEnabled = true
	}
	if opt.LoanPrincipal > 0 {
		p.LoanPrincipal = opt.LoanPrincipal
	}
	if opt.LoanAnnualRate > 0 {
		p.LoanAnnualRate = opt.LoanAnnualRate
	}
	if opt.LoanTermYears > 0 {
		p.LoanTermYears = opt.LoanTermYears
	}
	if opt.LoanIssueInterval > 0 {
		p.LoanIssueInterval = opt.LoanIssueInterval
	}
	// R71 收口(a)：放贷额度锚到银行余额。默认 false ⇒ 逐位复现 R65 行为。
	if opt.LoanFromBalance {
		p.LoanFromBalance = true
	}
	if opt.LoanBalanceFraction > 0 {
		p.LoanBalanceFraction = opt.LoanBalanceFraction
	}
	// 1.2 M4.2 所有权重构。默认 false ⇒ 逐位复现 1.0。
	if opt.OwnershipRestructure {
		p.OwnershipRestructure = true
	}
	if opt.OwnershipCapitalShare > 0 {
		p.OwnershipCapitalShare = opt.OwnershipCapitalShare
	}
	// 1.2 M1 第 3 条：储蓄存量口径（默认关闭 ⇒ 1.0 逐位不变）
	if opt.SavingsStockTrack {
		p.SavingsStockTrack = true
	}
	// 1.2 §1.2-5 央行/金矿（默认关闭 ⇒ 1.0 逐位不变）
	if opt.CentralBankEnabled {
		p.CentralBankEnabled = true
	}
	if opt.GoldMineLaborPerLevel > 0 {
		p.GoldMineLaborPerLevel = opt.GoldMineLaborPerLevel
	}
	if opt.GoldPrice > 0 {
		p.GoldPrice = opt.GoldPrice
	}
	if opt.MoneyPerMint > 0 {
		p.MoneyPerMint = opt.MoneyPerMint
	}
	if opt.GoldPerMint > 0 {
		p.GoldPerMint = opt.GoldPerMint
	}
	if opt.MintWageFraction > 0 {
		p.MintWageFraction = opt.MintWageFraction
	}
	// 1.2 M3/M6 玩家投资接口（默认 AI 开启、占比未设定 ⇒ 逐位复现 1.0）
	if opt.InvestAIOff {
		p.InvestAIEnabled = false
	}
	if opt.InvestManorShare != nil {
		p.InvestManorShare = *opt.InvestManorShare
	}
	// 1.2 M5 ① 政府债务计息（默认关闭 ⇒ 1.0 逐位不变）
	if opt.GovDebtInterestEnabled {
		p.GovDebtInterestEnabled = true
	}
	if opt.GovDebtInterestRate > 0 {
		p.GovDebtInterestRate = opt.GovDebtInterestRate
	}
	// 1.2 工资平减（默认关闭 ⇒ 1.0 逐位不变）
	if opt.InflationDeflation {
		p.InflationDeflation = true
	}
	// 1.2 M7 工资竞标开关（**必须**在下面的 Profile12 / 覆盖块之前）
	//
	// 【R80 为什么强调顺序】`Profile12` 与 `*Override` 的语义是
	// "显式 opt > profile > 默认"。若把本行留在它们**之后**，
	// `WageBidEnabled=true` 会把 `WageBidOverride=&false` 覆盖回 true，
	// 于是"1.2 但关掉竞标"这个对照**做不到**。实测已确认该顺序问题存在。
	if opt.WageBidEnabled {
		p.WageBidEnabled = true
	}
	// 【1.2 配置档：一次性打开全部 1.2 行为】
	//
	// 【为什么需要它】到本轮为止 1.2 有四个独立开关，默认**全关**（以守住 1.0 基线）。
	// 但那意味着**"1.2 的完整配置"没有任何单一入口**——目标"完成 1.2 的实现"
	// 无法用一条命令实例化，只能靠人工记住四个开关。
	// `Profile12` 就是那个入口：它把"1.2 的全部行为"作为一个**可复现的配置**给出。
	//
	// 【优先级：显式 opt > Profile12 > 默认】
	// 单个开关的"显式覆盖"用 `*bool` 指针表达（与 `AnchorExpenditureShare` 等既有字段同法）：
	//   - `Profile12=true`、四个指针都 nil ⇒ **四个全开**（1.2 完整配置）
	//   - 某个指针非 nil ⇒ **该开关以指针为准**（可做单因子对照，例如"1.2 但关掉竞标"）
	//   - `Profile12=false`（默认）⇒ 与现状**逐位相同**
	//
	// 【注意】`Profile12` 不改任何**默认值**，故 1.0 基线仍然逐位不变。
	if opt.Profile12 {
		p.BankEnabled = true
		p.OwnershipRestructure = true
		p.WageBidEnabled = true
		p.LoanFromBalance = true
		// 【R90 补全】配置档必须覆盖**全部**已实现的 1.2 特性，否则它名不副实：
		// 一个"1.2 档"却要人再手工加 `-central-bank`，那它就不是单一入口。
		p.CentralBankEnabled = true
		p.SavingsStockTrack = true
	}
	// 显式覆盖放在 profile 之后 ⇒ 它们**优先**
	if opt.BankEnabledOverride != nil {
		p.BankEnabled = *opt.BankEnabledOverride
	}
	if opt.OwnershipOverride != nil {
		p.OwnershipRestructure = *opt.OwnershipOverride
	}
	if opt.WageBidOverride != nil {
		p.WageBidEnabled = *opt.WageBidOverride
	}
	if opt.LoanFromBalanceOverride != nil {
		p.LoanFromBalance = *opt.LoanFromBalanceOverride
	}
	// ── 以下是**非开关**的 1.2 参数（比例、利率条款、竞标参数）──
	// 它们不覆盖上面那四个布尔开关，故放在覆盖块之后是安全的。
	if opt.WageBidCap > 0 {
		p.WageBidCap = opt.WageBidCap
	}
	if opt.WageBidDecay > 0 {
		p.WageBidDecay = opt.WageBidDecay
	}
	// R68：M7.2 第 2 步的计费基数。默认空串 ⇒ `updateWageBids` 走 `default` 分支
	// = 纯利口径 ⇒ 逐位复现既有行为。
	if opt.WageBidBase != "" {
		p.WageBidBase = opt.WageBidBase
	}
	// R92：参与抬价的利润率封顶（默认 0 = 不封顶 ⇒ 逐位复现裁决原文）
	if opt.WageBidMarginCap > 0 {
		p.WageBidMarginCap = opt.WageBidMarginCap
	}
	// §3.4 已裁决采用方案 A + C′（契约默认开启）；显式传值可回退到历史口径做对照。
	if opt.AnchorExpenditureShare != nil {
		p.AnchorExpenditureShare = *opt.AnchorExpenditureShare
	}
	if opt.AnchorDerivedDemand != nil {
		p.AnchorDerivedDemand = *opt.AnchorDerivedDemand
	}
	// 【1.2 §1.2-5：央行/金矿】开启时改用**追加两类建筑**的规格表。
	//
	// 这是"1.0 基线逐位不变"的**唯一分支点**：关闭（默认）时走原来的
	// `model.BuildingSpecs`，15 项规格逐位不变 ⇒ `UnemployedSite` 仍为 15。
	// 开启时 17 项 ⇒ `st.UnemployedSite = len(specs) = 17`（见下方 Houses 构造）。
	specs := model.BuildingSpecs(opt.FinanceLaborPerLevel)
	if p.CentralBankEnabled {
		specs = model.BuildingSpecsWithCentralBank(
			opt.FinanceLaborPerLevel,
			p.GoldMineLaborPerLevel,
			p.CentralBankLaborPerLevel,
		)
	}

	cal, err := calibrate.Run(specs, 1.0/6.0, p.BuyerWedge())
	if err != nil {
		return nil, fmt.Errorf("标定失败: %w", err)
	}
	if opt.DemandScale > 0 {
		cal.DemandScale = opt.DemandScale
	}

	// §七 R31 的诊断开关：把开局价整体放大 InitPriceMult 倍（0/1 = 契约默认）。
	// 口径见 Options.InitPriceMult 与 scaleOpeningPrices 的注释。
	priceMult := opt.InitPriceMult
	if priceMult <= 0 {
		priceMult = 1.0
	}
	scaleOpeningPrices(goods, cal, specs, priceMult)

	st := &State{
		Goods:       goods,
		Params:      p,
		Population:  opt.Population,
		WealthTier:  opt.WealthTier,
		Market:      market.NewState(goods, p),
		demandScale: cal.DemandScale,
		calibration: cal,

		// §七 R28 的诊断开关：投资池与国库恒 ∞ 的对照实验。
		unlimitedFunds: opt.UnlimitedFunds,
		// §七 R32 / 契约 §2.4：钳制带与判据的参考价 = **当期零利润价**（默认口径）。
		// StaticPcost = true 时回退到旧的固定 P_cost 口径（对照实验）。
		dynamicPcost: !opt.StaticPcost,

		// §3.2 / §8.6（2026-09-19 第 15 轮裁决）：**显式标注短缺起步**。
		// 统一起始等级（N0 > 0）意味着起点产能远小于平衡产能（20m 口径约 1/19.7），
		// 经济**有意**从短缺状态开始——这是设计选择（让扩建、投资、财政等功能
		// 在开局就被激活），不是标定误差。取物质平衡布点（N0 = 0）时标注为 false。
		ShortageStart: p.ProductionInitLevel > 0,
	}
	st.Buildings = make([]BuildingState, len(specs))
	for i, sp := range specs {
		// 两个利润率 EMA 的初值必须相同（0.2 = §3.4 标定给出的开局 20% 利润率）：
		// 它们衡量的是同一个量（满编口径即时利润率），只是一个含补贴、一个不含。
		// 【前值 → 后值】ProfitEMA 是本次新增字段；若保留零值，扩建判定会被
		// 人为推迟约一个 EMA 窗口（12 期）才越过 10% 阈值——那不是契约语义。
		st.Buildings[i] = BuildingState{Spec: sp, HireRate: 1.0, MarginEMA: 0.2, ProfitEMA: 0.2}
	}

	// 开局布点：由 §6.3 的最终需求经 Leontief 完全需求反推每种建筑的级数。
	if err := st.initialLayout(cal); err != nil {
		return nil, err
	}
	// 开局劳动力分配（§4.2 修订）：必须先算出自给农场雇佣率，否则 subsistence()
	// 在 t=0 会返回 0（未分配劳动力 ⇒ 雇佣率 0），使净供给与需求标定都少了自给产出。
	st.syncManorLevel()
	{
		lv := st.levels()
		hr := st.hireRates()
		st.allocateSubsistenceLabor(lv, hr)
	}

	// 标定需求常数 a = S₀·(Pinit/Pcost)^ε（§3.4 步骤 3）。
	// 注意 S₀ 必须是【净供给】Y − A·Y，而不是总产出——否则会把中间投入重复算作可售量。
	//
	// 方案 A（支出份额锚，实验开关）取 ε ≡ 1，故 a = S₀·P_init/P_cost，
	// 于是 P*(λ) = P_init/λ。
	net := st.netSupply(cal)
	st.Market.UnitElastic = p.AnchorExpenditureShare
	for i := range st.Goods {
		mk := &st.Market.Markets[i]
		eps := goods[i].Eps
		if st.Market.UnitElastic {
			eps = 1
		}
		mk.A = calibrate.DemandConstantAt(goods[i], net[i], eps)
	}

	// 【唯一记账账本】（§4.5.3 修订）
	//
	// 所有主体的余额都存放在这里；Gov / Cap / Houses / Buildings 只持有账户标识。
	// 必须在任何注资之前建立，否则各池读不到余额。
	st.Aud = ledger.NewAuditor()
	// 统一记账簿与审计账本共用同一实例——这是"只有一份余额"的落地处。
	st.bk = &book.Book{
		Aud:          st.Aud,
		Buildings:    len(specs),
		Classes:      cohort.ClassCount,
		PowerIdx:     powerGoodIndex,
		FinanceIdx:   model.FinanceIndex,
		WarehouseIdx: model.WarehouseIndex,
		AgentIdx:     model.AgentIndex,
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
	//
	// 【2026-09-19 第 15 轮裁决】多开一个**虚拟劳动场地**：失业（§6.5）。
	// 失业人口算劳工（阶级 0），其资金池与就业者严格分开——它只能收到福利金
	// （§4.5.8），没有工资收入；福利金关闭时预算为 0 ⇒ 无法消费 ⇒ 幸福度 0。
	// 故 worksites = len(specs) + 1，最后一个场地下标 = 失业场地。
	//
	// 【2026-09-20 第 58 轮：失业场地改为**运行时**确定】
	// 1.2 的央行/金矿会**追加建筑**（`BuildingSpecs` 末尾），使 `len(specs)` 变大。
	// 若失业场地下标仍是编译期常量 `s.UnemployedSite`（= 15），
	// 开启央行时它就会指向某个真实建筑 ⇒ **失业人口被记到别人的池里**。
	//
	// 故新增 `State.UnemployedSite`（运行时 = `len(specs)`）；常量
	// `model.UnemployedSite` 保留为**关闭央行时的取值**与既有测试的参照。
	//
	// 【实测依据】本约束不是推测：把两个哑建筑追加到 specs 末尾
	// （`BuildingTypes`/`UnemployedSite` 各 +2）后跑同一局，
	// 人口由 **5,688,326 变为 6,381,408** —— 证明**任何**场地数变化都会改变基线。
	// 故必须让下标随 specs 走，而不是让 specs 去迁就常量。
	st.UnemployedSite = len(specs)
	st.Houses = cohort.NewLedger(st.Aud, len(specs)+1)

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
	// 政府现金池规模：契约默认 0.50 × 周工资；Options.GovStartupFraction > 0 时覆盖它
	// （2026-09-19 修复：此前该选项被忽略，`-gov-startup` 是个空旗标）。
	govFraction := startupGovFraction
	if opt.GovStartupFraction > 0 {
		govFraction = opt.GovStartupFraction
	}
	st.Gov.Cash.SetInitial(govFraction * wageBill)
	// 资本现金池规模：契约默认 0.25 × 周工资；Options.CapStartupFraction > 0 时覆盖它
	// （第 26 轮新增，用于隔离"资本池不足 ⇒ 私有化零成交"这一闸门）。
	capFraction := startupCapFraction
	if opt.CapStartupFraction > 0 {
		capFraction = opt.CapStartupFraction
	}
	st.Cap.Cash.SetInitial(capFraction * wageBill)
	// 建筑现金池按【基数 + 按级数】分摊。
	//
	// 不能只按级数分摊：建造部门默认只有 20 级，而它要垫付钢/铁/工具的
	// 中间投入（每级 25×P钢 + 25×P铁 + 20×P工具 ≈ 8.8 万），
	// 按级数分到的钱不足以做第一笔采购，会立刻透支并让整条扩建链断掉。
	const firmBasePerType = 0.05 // 每类建筑的基数，单位为"周工资"
	// 宅邸庄园（§4.5.5）单独给一份起步流动性：它不参与"按级数分摊"，
	// 否则 ~2,000 级庄园会吃掉几乎全部建筑现金池（实测占比 96%），
	// 使真正的生产建筑开局就无钱采购。总额仍为契约 §4.3 的 1.75 × 周工资，
	// 只是在"建筑"与"庄园"两行之间重新分配（0.75 / 0.25）。
	firmStock := (startupFirmFraction - startupManorFraction) * wageBill
	base := firmBasePerType * wageBill
	perLevelPool := firmStock - base*float64(len(st.Buildings))
	if perLevelPool < 0 {
		perLevelPool = 0
	}
	totalLevels := maxLevels(st.Buildings)
	for i := range st.Buildings {
		if st.Buildings[i].Spec.IsManor {
			st.Aud.SetBalance(ledger.Building(i), startupManorFraction*wageBill)
			continue
		}
		// 消费代理是**零余额**的透传主体（§4.5.6）：不给它任何起步现金，
		// 否则"代理余额恒为 0"这条审计断言在开局就不成立。
		if st.Buildings[i].Spec.IsAgent {
			continue
		}
		share := base
		if totalLevels > 0 {
			share += perLevelPool * st.Buildings[i].Level / totalLevels
		}
		// 【§4.5.1b 场地账户口径】金融区的营运现金池就是 ledger.Capital()，
		// 故它那一份起步流动性直接加进资本池，不进 Building[FinanceIndex]
		// （后者本版不再承载任何资金流）。
		if i == model.FinanceIndex {
			st.Aud.SetBalance(ledger.Capital(), st.Aud.Balance(ledger.Capital())+share)
			continue
		}
		st.Aud.SetBalance(ledger.Building(i), share)
	}
	// 投资池开局为 0：它的资金只能来自资本建筑的当期净额（§4.5.1b），
	// 给它初始现金等于凭空注入一笔没有来源的货币，违反 §4.5.3 的货币守恒。
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
	// startupManorFraction 是宅邸庄园现金池的初始规模（§4.5.5）。
	//
	// 从"建筑"那一份里划出 0.25：契约 §4.3 的初始货币总量仍是 1.75 × 周工资，
	// 只是分配表多了一行（政府 0.50 / 资本 0.25 / 建筑 0.75 / 庄园 0.25 / 人群 0）。
	startupManorFraction = 0.25
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
		total += b.Level * b.Spec.WagePerLevel()
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
//	政府（按当期持股承担亏损份额，§4.5.1）现金池被砸穿 → 建造力采购归零 →
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
		// 【§4.5.6】仓库与消费代理**没有配方**（Recipe.Qty = 0）：若把它们当作
		// 生产者，下面的 need/Qty 会除以 0。故这里的判据是 Produces() 而不是 IsNonMarket()。
		if !s.Buildings[i].Spec.Produces() {
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
			if !s.Buildings[j].Spec.Produces() || levels[j] <= 0 {
				continue
			}
			for good, qty := range s.Buildings[j].Spec.Recipe.Inputs {
				used[good] += qty * levels[j]
			}
		}

		maxChange := 0.0
		for i := range s.Buildings {
			b := &s.Buildings[i]
			if !b.Spec.Produces() || i == powerIdx {
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
				if !s.Buildings[j].Spec.Produces() {
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
		if !s.Buildings[i].Spec.Produces() {
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
			// 【1.2 §1.2-5】金矿**也要**布点：它与消费代理不同，是"有产出、要利润"
			// 的生产建筑（只是产出不进商品市场）。若不给它起始等级，它会永远停在 0
			// ⇒ 既不产金也不造币、也不会有扩建意向（实测：600 tick 全程 0 级）。
			//
			// 央行**不布点**：它的级数按 §1.2-5 由"黄金有剩余"驱动自动扩建，
			// 不由建造力产生（与金融区/庄园同理）。
			//
			// 建造力（`i == powerGoodIndex`）独立由 `InitialPowerLevel` 决定。
			if b.Spec.IsCentralBank || i == powerGoodIndex {
				continue
			}
			// 【必须带上 `CentralBankEnabled` 守卫】金矿只在开启央行时才存在于 specs 里，
			// 所以下面的 `b.Spec.ProducesGold` 分支在关闭时**不可达**；
			// 但仍显式写出开关条件，使"关闭 ⇒ 不布点"成为**结构性**保证，
			// 而不是依赖"那个规格根本不存在"这一间接事实。
			//
			// 【实测教训】第一次改这里时把条件写成"生产 OR 产金"而没检查开关，
			// 基线人口立刻由 5,688,326 变成 5,626,879 —— 因为「未开央行」时
			// 该分支本不该有任何建筑命中，而我让它命中了。
			if !b.Spec.Produces() && !(b.Spec.ProducesGold && s.Params.CentralBankEnabled) {
				continue
			}
			lv := s.Params.ProductionInitLevel
			if b.Spec.Cap > 0 && lv > b.Spec.Cap {
				lv = b.Spec.Cap
			}
			b.Level = lv
		}
	}

	// ── 第 4 步：金融区按掌控比推导（G5）──────────────────────
	// 【2026-09-19 裁决】金融区不建造，级数是所有权的显式表达。
	s.syncFinanceLevel()
	// 宅邸庄园同样按推导布点（§4.5.5）：级数 = 自给农场级数 ÷ 掌控比。
	s.syncManorLevel()

	// ── 第 4b 步：仓库的起步等级（§4.5.6）───────────────────
	//
	// 仓库是**可建造**的国有贸易枢纽，但其起步规模不由需求反推（它不生产商品），
	// 而由 Params.InitialWarehouseLevel 给定（建模值，与 InitialPowerLevel 同性质）。
	// 消费代理恒为 0 级（它是虚构的记账主体）。
	if w := &s.Buildings[model.WarehouseIndex]; w.Level <= 0 {
		w.Level = s.Params.InitialWarehouseLevel
	}
	s.Buildings[model.AgentIndex].Level = 0

	// ── 第 5 步：所有权拆分（G3）────────────────────────────────
	//
	// 【2026-09-19 裁决（§4.5.1）】初始政府持股 s_gov 由 0.70 下调到 0.30
	// （Params.GovInitialShare）。金融区与宅邸庄园全归私有（不持政府股份），
	// 它们是"资本所有权 / 农业资本"的显式表达。
	// 【§4.5.6】仓库**归国有**（s_gov = 1）：它是国有的贸易枢纽，利润全部进政府池。
	for i := range s.Buildings {
		share := s.Params.GovInitialShare
		switch {
		case s.Buildings[i].Spec.IsWarehouse:
			share = 1
		case !s.Buildings[i].Spec.Produces():
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
		if bs[i].Spec.Produces() && bs[i].Spec.Recipe.Output == good {
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

// syncFinanceLevel 按 §3.2/§4.5.2 的裁决重算金融区级数。
//
// 【2026-09-19 裁决】金融区是"所有权的显式表达"：不建造、不进入建造队列、
// 不消耗建造力、不参与缩编，其级数恒由掌控比反推：
//
//	N_finance = max(1, Σ_{非农业} 生产建筑 Level / c_ctrl)
//
// 求和取**非农业**生产建筑等级：农业建筑归宅邸庄园掌控（§4.5.5），
// 且不含金融区自身（否则自我指涉）。
//
// 由此掌控上限 = N_finance × c_ctrl ≡ Σ其余等级，恒不小于实际持有量，
// 故 G5 的掌控上限在"推导口径"下**不再是约束**——这是"无需建造"的直接后果，
// 已在契约 §4.5.2 与 docs/ACTIVE.md 中明示。
//
// 所有权拆分同步维护：金融区天然 100% 私有（GovLevel = 0）。
func (s *State) syncFinanceLevel() {
	var other float64
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if !b.Spec.Produces() || b.Spec.LandKind == "arable" {
			continue
		}
		other += b.Level
	}
	lv := other / s.Params.ControlPerFinance
	if lv < 1 {
		lv = 1
	}
	fin := &s.Buildings[model.FinanceIndex]
	fin.Level = lv
	fin.GovLevel = 0
	fin.PrivLevel = lv
}

// syncManorLevel 按 §4.5.5 重算宅邸庄园级数。
//
// 【农业版金融区】宅邸庄园是自给农场与农业建筑的所有权载体：不建造、
// 不消耗建造力、不进入建造队列、不参与缩编，级数由它掌控的**全部农业等级**推导：
//
//	N_manor = max(1, (N_subsistence + Σ_{农业建筑} N_i) / c_ctrl)
//
// 【2026-09-19 改写】求和里**含自给农场级数**（前值只取农业建筑）：
// 自给农场从此不只是"分母上的一项"，而与庄园自有农业建筑一起决定庄园规模。
//
// 它每级雇 1,000 人（劳工 75% / 教士 20% / 贵族 5%，工资 5/10/20 ⇒ 6,750 元/级），
// 拿货币工资并在市场消费；收入是自给农场产出的全部销售收入 + 农业建筑的私人份额纯利。
func (s *State) syncManorLevel() {
	subs := s.subsistenceFarmLevels()
	var arable float64
	for i := range s.Buildings {
		if s.Buildings[i].Spec.LandKind == "arable" {
			arable += s.Buildings[i].Level
		}
	}
	lv := (subs + arable) / s.Params.ControlPerFinance
	if lv < 1 {
		lv = 1
	}
	m := &s.Buildings[model.ManorIndex]
	m.Level = lv
	m.GovLevel = 0
	m.PrivLevel = lv
	s.SubsistenceLevels = subs
}

// subsistenceFarmLevels 返回当期自给农场级数（未使用耕地 × SubsistenceScale）。
func (s *State) subsistenceFarmLevels() float64 {
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
	return idle * s.Params.SubsistenceScale
}

// allocateSubsistenceLabor 实现 §4.2 修订的就业顺序：
//
//	① 一般生产建筑（含金融区、宅邸庄园）按其雇佣率占用劳动力；
//	② 余量（备用劳动力池）配置给自给农场，每级需要 5,000 自给农；
//	③ 仍有余量即为失业。
//
// 自给农场的产出按此雇佣率缩放（见 subsistence）；自给农不领货币工资、
// 不进市场购买——他们的消费已经在 §3.3 的配方里约去。
func (s *State) allocateSubsistenceLabor(levels, hire []float64) {
	var market float64
	for i, b := range s.buildingSpecs() {
		if i >= len(levels) || i >= len(hire) {
			break
		}
		market += levels[i] * hire[i] * b.LaborPerLevel
	}
	// 【§5.2 第 23 轮：人口约束（兜底）】各场地雇佣人口之和**不得**超过总人口。
	//
	// 【为什么必须有这条】§5.2 的增雇只由利润率驱动，没有任何全局劳动力上限。
	// 实测（R45/R46）：tick 1,500 市场用工 519 万 > 总人口 229 万，其中建造部门一家
	// 430 万 ⇒ 备用劳动力恒为 0 ⇒ 自给农场雇佣率归零 ⇒ 断粮 ⇒ 人口 −8.49%/年 到 0。
	//
	// 【口径：按比例配给，不改雇佣率状态】超出时把**本 tick 的有效雇佣率**整体乘一个
	// 系数 k = 人口 / 市场需求，于是"谁也不会被单独惩罚"，且 `HireRate` 的演化仍由
	// §5.2 的利润率信号驱动（k 只是本 tick 的配给闸门）。被裁掉的那部分人口登记为失业。
	//
	// 返回 k（≤ 1）。调用方必须把它一致地用于工资、产出与快照——
	// 否则"付了 5,000 人的工资、只报了 2,000 人就业"会让货币闭环与人群池对不上。
	ratio := 1.0
	if market > s.Population && market > 1e-9 {
		ratio = s.Population / market
		market = s.Population
	}
	// 【1.2 M7.2 第 4 步：按工资降序配给】**只在 wageBidEnabled 时生效**——
	// 关闭时本块整体跳过，`ratio` 仍是 1.0 第 23 轮的**全局等比**口径（逐位不变）。
	//
	// 规则（裁决）：各场地按**实际人均工资** w_i = baseWage_i + p_i **降序**排序，
	// 依次满足其申报需求，直到人口耗尽；**未获配的申报量按实际到岗/申报比例下调**。
	// 这与"全局等比"的区别是：**出价高者优先拿满**，出价低者被挤掉（而不是所有人同比例缩水）。
	if s.Params.WageBidEnabled {
		ratio = s.allocateByWageOrder(levels, hire)
	}
	reserve := s.Population - market
	if reserve < 0 {
		reserve = 0
	}
	capacity := s.SubsistenceLevels * s.Params.SubsistenceLaborPerLevel
	if capacity <= 0 {
		s.SubsistenceHireRate = 0
		s.Unemployed = reserve
		s.LaborMarketRatio = ratio
		return
	}
	hr := reserve / capacity
	if hr > 1 {
		hr = 1
	}
	if hr < 0 {
		hr = 0
	}
	s.SubsistenceHireRate = hr
	s.Unemployed = reserve - hr*capacity
	s.LaborMarketRatio = ratio
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
		if !b.Spec.Produces() {
			continue
		}
		out[b.Spec.Recipe.Output] += b.Level * b.Spec.Recipe.Qty * b.HireRate
	}
	for i, v := range s.subsistence() {
		out[i] += v
	}
	return out
}

// subsistence 返回自给农场的产出（§3.3/§4.2 修订）。
//
// 【口径】① 级数 = 未使用耕地 × SubsistenceScale（1:1）；
//
//	② 产出按【自给农场雇佣率】缩放——自给农场是备用劳动力池，
//	   每级需要 5,000 自给农，劳动力不足则同比例减产；
//	③ 表中的 谷物2/织物1/服装0.5 是**已约去自给农自身消费的净产出**，
//	   故自给农不领货币工资、不进市场购买。
func (s *State) subsistence() map[int]float64 {
	hr := s.SubsistenceHireRate
	if hr < 0 {
		hr = 0
	}
	if hr > 1 {
		hr = 1
	}
	return model.SubsistenceOutput(s.subsistenceFarmLevels() * hr)
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
	Tick   int64
	Prices []float64
	// PriceRatio = P / P⁰，其中 P⁰ 是**当期零利润价**（§七 R32：
	// 动态模式为每 tick 重算值，静态模式为契约的 P_cost）。
	PriceRatio []float64
	// Pzero 是当期的零利润价切片（§七 R32），供 A1/A3 判据与报告使用。
	Pzero          []float64
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
	// + 金融区现金池 + 人群现金池（§5.1）+ **投资池**（§4.5.1b）。
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
	// ManorCash 是宅邸庄园现金池余额（§4.5.5）。
	ManorCash float64
	// SubsistenceHire 是自给农场雇佣率；Unemployed 是失业人数（§4.2 修订）。
	SubsistenceHire float64
	Unemployed      float64
	// PrivatizeUnits / PrivatizePaid / GovShareAfter 是私有化诊断（§4.5.1 修订）。
	PrivatizeUnits float64
	PrivatizePaid  float64
	GovShareAfter  float64
	// Flow 是本期的资金流分解，用于诊断政府现金池的变化来源。
	Flow FlowDiag

	// ===== §4.5.1b 投资池快照 =====

	// InvestmentPool 是期末投资池余额。
	InvestmentPool float64
	// KManor / KFinance 是两条投资栈的累计贡献（含本期）。
	KManor, KFinance float64
	// InvestmentInflowManor / InvestmentInflowFinance 是本 tick 两条栈的入池额。
	InvestmentInflowManor   float64
	InvestmentInflowFinance float64
	// InvestmentPaid 是本 tick 投资池付给政府的建造力货款（G6）。
	InvestmentPaid float64
	// PowerNeed 是本 tick 队列实际需要的建造力量（G2 的采购量由它裁剪而来）。
	PowerNeed float64
	// PowerInventory 是政府公共储备，**恒为 0**（§4.5.3 G2 即买即用）。
	PowerInventory float64
	// ProfitEMAs 是逐建筑【不含补贴】的利润率 EMA（§4.5.7）。
	//
	// 它与 Margins（含补贴的即时利润率）并列输出：扩建判定用本字段，
	// 雇佣调整用 MarginEMA。
	ProfitEMAs []float64
	// ProfitMargins 是逐建筑【不含补贴】的即时利润率。
	ProfitMargins []float64
	// MarginsEMA 是逐建筑**含补贴**的利润率 EMA（§5.2 的雇佣调整输入）。
	MarginsEMA []float64
	// ManorConsumerIn 是宅邸庄园本 tick 从消费者货款中分得的自给产出收入（§4.5.5）。
	ManorConsumerIn float64

	// ===== §4.5.6 仓库与消费代理（2026-09-19 第 16 轮）=====

	// TradeVolume 是本 tick 过库的贸易量（单位数，单向过手）。
	TradeVolume float64
	// TradeQuota 是本 tick 的贸易额度 = 仓库级数 × WarehouseQuotaPerLevel。
	TradeQuota float64
	// WarehouseCash / AgentCash 是两个贸易节点的期末现金池（代理恒为 0）。
	WarehouseCash float64
	AgentCash     float64
	// WarehouseLevel 是仓库当期级数。
	WarehouseLevel float64
	// WarehouseIn / WarehouseOut 是仓库本 tick 的出库收款与入库付款（含税）。
	WarehouseIn  float64
	WarehouseOut float64
	// WarehouseProfit 是仓库本 tick 的纯利（加价 − 增值税 − 自身工资；全归政府）。
	WarehouseProfit float64
	// WarehouseWage 是仓库本 tick 的工资支出（⑥ 计算纯利时用的那一份；
	// 与 tick 末 Level 派生的工资可能差一级——完工发生在纯利计算之后）。
	WarehouseWage float64
	// VAT / ConsumeTax 是本 tick 两段税的税额（已含在 Tax 内，单列供核对）。
	VAT        float64
	ConsumeTax float64
	// WarehouseExpandSpend / WarehouseExpandUnits 是本 tick 的仓库自动扩建支出与新建订单等级数。
	WarehouseExpandSpend float64
	WarehouseExpandUnits float64

	// ===== §7.2 实际工农生产总值（2026-09-19 第 18 轮）=====

	// GrossAgri / GrossIndustry 是实际总产值（实物产出 × 固定 P_ref）。
	GrossAgri     float64
	GrossIndustry float64
	// GrossProduct 是工农总产值（= 农业 + 工业，含中间投入，会重复计算）。
	GrossProduct float64
	// ProductInput 是中间投入价值（实物取用量 × 固定 P_ref）。
	ProductInput float64
	// ProductAdded 是**工农增加值**（= 总产值 − 中间投入），即"实际口径的 GDP"。
	ProductAdded float64
	// ProductAddedAgri / ProductAddedIndustry 是两部门的增加值。
	ProductAddedAgri     float64
	ProductAddedIndustry float64
	// ProductPerCapita 是人均实际增加值（ProductAdded / 人口）。
	ProductPerCapita float64
	// ProductIndex 是实际增加值相对**首个 tick** 的指数（基期 = 1）。
	ProductIndex float64
	// ProductBaseAdded 是首 tick 的实际工农增加值（指数基期值，供报告标注）。
	ProductBaseAdded float64
	// ProductSubsistence 是自给农场那一部分实际产出（已含在农业里，单列供分离观察）。
	ProductSubsistence float64

	// ===== 2026-09-19 第 15 轮裁决新增的快照字段 =====

	// Saving 是本 tick 居民工资结余**全额**进入储蓄固定账户的金额（§5.3 第一步）。
	Saving float64
	// SavingInvest 是本 tick 从储蓄账户转入投资池的金额（§5.3 第二步
	// = σ_save × Saving）。σ_save = 1 时两者相等，储蓄账户期末归零。
	SavingInvest float64
	// SavingsAccount 是期末**居民储蓄固定账户**余额（§5.3）。
	// 默认 σ_save = 1 时恒为 0；σ < 1 时它是"已储蓄未投资"的挂账。
	SavingsAccount float64
	// AcquirePaid 是本 tick **投资池出资收购政府股权**的支出（§4.5.1a 第 28 轮）。
	//
	// 它是投资池的一条**新增流出腿**（借 投资池、贷 政府，`ledger.InvestmentBuyEquity`）：
	// 投资池的余额恒等式因此是
	//
	//	Δ投资池 = 储蓄转入 + 资本建筑入池 − 付政府(建造力) − **本项**
	//
	// 把它单列（而不是并入"付政府"）是为了让审计能分开核对两条完全不同的语义：
	// 前者是"偿还建造力货款"，本项是"买存量股权"。
	AcquirePaid float64
	// Welfare 是本 tick 政府发放的福利金总额（§4.5.8）。
	Welfare float64
	// PublicWorks 是本 tick 公共工程采购建造力的国库支出（§4.5.8）。
	PublicWorks float64
	// PublicWorksUnits 是本 tick 公共工程新建订单的等级数（§4.5.8）。
	PublicWorksUnits float64
	// NoIncomePools 是"有人口但无收入 ⇒ 无法消费"的池数（§6.5 诊断）。
	NoIncomePools int
	// Happiness 是按人口加权的幸福度 = 四组满足度均值（§6.5）。
	Happiness float64
	// HappinessByClass 是各阶级的幸福度（失业者算劳工，§6.5）。
	HappinessByClass [3]float64
	// BudgetShareManor 是本 tick 投资池预算中庄园栈的份额（§4.5.1b 修订后
	// 由**当期意向需求**比例决定，不再是累计贡献比例）。
	BudgetShareManor float64
	// ShortageStart 标记本次运行是"短缺起步"布点（§3.2/§8.6 第 15 轮裁决）。
	ShortageStart bool
	// Infusion 是累计的诊断注入额（§七 R28 的 UnlimitedFunds；正常为 0）。
	//
	// §8.4 的 A8（货币守恒）判定需要它：恒等式读作
	// `ΔM == NewCapital 累计 + Infusion 累计`。
	Infusion float64
}
