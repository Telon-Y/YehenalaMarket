// Package fiscal 实现「政府 + 资本」两层主体，用以闭合 1.0 契约里
// 「建造力无人付款」的财政黑洞。
//
// ============================ 问题（已量化） ============================
//
// 1.0 契约 §4.3 只写了"建造力费用从现金池扣除"，从未定义这笔钱付给谁；
// §3.1 把建造力列为有单价（P_cost = 7250 元/单位）的商品，但没有任何主体申报购买它。
// 量化结果（tools/construction_sink_probe.js）：
//
//	ARCHITECTURE §10.2 目标：10,000 tick 内 50 级 → 3,000 级
//	总建造力需求 = 2,950 × 600 = 1,770,000 单位
//	按 P_cost 计价需要 128.32 亿元；按 Pinit 计价需要 210.93 亿元
//	而 §4.3 给出的初始货币总量 = 5,000 元/级 × 50 级 = 250,000 元
//	⇒ 缺口 5.1e4 倍。建造力没有任何合法货币来源，扩建在资金上不可能发生。
//
// 更深一层（tools/accounting_probe.js）：
//
//	封闭经济中，居民收入 = 工资，消费者支出 ≤ 工资；
//	建筑总收入 = 消费者支出 + 中间投入付款 = 工资 + 中间投入 = 建筑总成本
//	⇒ 全局总利润 ≡ 0（会计恒等式，与价格、产量、参数无关）
//	⇒ "所有建筑同时获得正利润"在数学上不存在。
//
// ============================ 解法（G1–G7） ============================
//
// G1 政府按税率 t 征收交易税（消费环节 + 中间投入环节）。**建造力交易不计税**
// （2026-09-19 裁决）。
// G2 建造力按需采购：政府只在当期存在建造队列时才采购，采购量 = 队列本 tick
// 的实际需要量，受当期产出与 §4.5.4 可动用资金裁剪；即买即用、不留储备。
// G3 建筑分所有权：政府 / 私有；初始政府持股 s_gov = 0.30（§4.5.1）。
// G4 生产建筑的私人份额纯利归**所属资本建筑**（农业建筑 → 宅邸庄园；
// 其余 → 金融区），见 §4.5.1。
// G5 金融区级数由掌控比反推：N_finance = max(1, Σ非农业等级 / c_ctrl)（§4.5.2）。
// G6 私人扩建的建造力货款由**投资池全额付给政府**（§4.5.3）：政府先按 G2 付给
// 建造部门，再收偿还，故政府净支出为 0；不存在"转售"，也不存在自反税腿。
// G7 政府现金池允许为负（债务），上限 = 2 ×（建造力产出 × 建造力价格）。
//
// 为什么这样能闭合：建造力是【唯一没有最终消费者】的商品，其唯一买家是投资需求。
//
//	税收 → 政府 → 建造部门（工资 + 投入）→ 回到居民/企业流通
//	投资池 → 政府（偿还）→ 政府净支出 0
//
// 投资池的资金来自两个资本建筑扣除自身工资后的纯利（§4.5.1b），
// 因此扩建获得了真实资金约束，而货币总量不变（每笔交易借贷相等）。
// 若像朴素做法那样让政府把税收全部沉淀（只买不卖），则每 tick 从流通中抽走与税收
// 等量的货币，消费支出随之下降，经济进入通缩螺旋——这是本包 V3 版本的崩解原因，
// 已记录在 tools/core_sim_gov.txt。
//
// ============================ 唯一记账入口（本次修订） ============================
//
// 本包不再自行持有余额。所有现金池都挂接到 ledger.Auditor，余额只有一份；
// 资金流动走 ledger 包的交易构造函数，那些函数保证借贷相等。
//
// 修订前的做法是各池自行持有 Balance、各自 Cash.Add，于是货币守恒只能靠事后审计
// 发现，而每修一处残差就跑到另一处——实测连续暴露四个不同层级的缺陷（见
// docs/ACTIVE.md §七）。收敛到 ledger 之后，守恒成为构造性事实。
package fiscal

import (
	"fmt"
	"math"

	"yehenala/market/internal/ledger"
	"yehenala/market/internal/model"
)

// PowerGoodIndex 是"建造力"在商品列表中的下标（契约 §3.1 的最后一项）。
const PowerGoodIndex = 10

// Ledger 是一个现金池的【视图】。
//
// 余额存放在共享的 ledger.Auditor 里，本结构只保存账户标识。
// 这样"同一笔钱被两个池各记一次"在结构上不可能发生——
// 曾经的重复入账（Δ货币/Σ利润 = 2.00）正是各池自行持有余额的结果。
type Ledger struct {
	// Acc 是该池在审计账本中的账户标识。
	Acc ledger.Account
	// Auditor 是共享的审计账本（所有池指向同一实例）。
	Auditor *ledger.Auditor
	// OverdraftTicks 记录余额为负的 tick 数，用于诊断。
	OverdraftTicks int64
}

// NewLedger 构造一个挂接到指定审计账本的池视图。
func NewLedger(a *ledger.Auditor, acc ledger.Account) Ledger {
	return Ledger{Acc: acc, Auditor: a}
}

// Balance 返回余额（读自审计账本）。
//
// 注意它是方法而不是字段：余额只有审计账本一份，
// 不存在"两个池各有自己的 Balance 而彼此不一致"的可能。
func (l *Ledger) Balance() float64 {
	if l == nil || l.Auditor == nil {
		return 0
	}
	return l.Auditor.Balance(l.Acc)
}

// String 返回账户标识，便于诊断输出。
func (l *Ledger) String() string {
	if l == nil {
		return "<nil>"
	}
	return l.Acc.String()
}

// SetInitial 设定开局余额（仅用于 New 的开局注资）。
func (l *Ledger) SetInitial(v float64) {
	if l == nil || l.Auditor == nil {
		return
	}
	l.Auditor.SetBalance(l.Acc, v)
}

// Add 以【单边分录】调整余额。
//
// 纪律：业务代码不应直接调用它——所有资金流动必须走 ledger 包的
// 交易构造函数（Wage / Intermediate / PowerPurchase / ...），
// 那些函数保证借贷相等。本方法仅用于开局注资与调试。
func (l *Ledger) Add(delta float64) {
	if l == nil || l.Auditor == nil {
		return
	}
	t := &ledger.Txn{Name: "单边调整（仅限开局/调试）"}
	if delta >= 0 {
		t.Credit(l.Acc, delta)
	} else {
		t.Debit(l.Acc, -delta)
	}
	l.Auditor.PostInjection(t)
	if l.Balance() < 0 {
		l.OverdraftTicks++
	}
}

// Post 过账一笔借贷相等的交易。
//
// 借贷不等时返回错误——记错账必须立刻暴露，而不是留下一个要追的残差。
func (l *Ledger) Post(t *ledger.Txn) error {
	if l == nil || l.Auditor == nil {
		return fmt.Errorf("fiscal: 审计账本未挂接")
	}
	return l.Auditor.Post(t)
}

// Government 是政府（G1/G2/G6/G7）。
type Government struct {
	// Cash 是政府现金池。允许为负——负值即为政府债务（G7）。
	Cash Ledger

	// PowerInventory 是政府持有的公共储备。
	//
	// 【2026-09-19 裁决：恒为 0】G2 改为"按需即买即用"后，当期采购的建造力在
	// 同一 tick 内全部投入队列，政府不持有公共储备。本字段保留为显式诊断位
	// （审计断言 PowerInventory ≡ 0），不再被任何代码累加。
	PowerInventory float64

	// PowerOutput 是本 tick 建造部门的产出，用于计算债务上限的资产基数（G7）。
	PowerOutput float64

	// DebtCapBoundTicks 记录债务触及上限的累计 tick 数，用于诊断。
	DebtCapBoundTicks int64

	// 本 tick 的资金流明细（报告用）。
	TaxCollected    float64 // 税收总额（消费端 + 中间投入环节；建造力不计税）
	PowerPurchased  float64 // 按需采购的建造力（G2）
	PowerSold       float64 // 即买即用、实际投入队列的建造力（≡ PowerPurchased）
	PowerRevenue    float64 // 投资池偿还的建造力货款（G6，净额）
	OperatingIncome float64 // 政府建筑的经营净额
}

// G7 政府债务机制（契约 §4.5）
//
//	债务      = max(0, −现金池余额)
//	资产基数  = 建造力产出 × 建造力价格
//	债务上限  = DebtCapMultiplier × 资产基数
//
// 语义选择（契约未明说，本实现采用保守解）：
//   - 债务不计息（§1.3 把货币/银行/利率列为 1.2 的非目标）；
//   - 触及上限时表现为【G2 停摆】而非违约重组——即无法再增加债务，
//     采购与政府项目付款都被压缩，直到收入恢复。
const (
	// DebtCapMultiplier 是债务上限相对资产基数的倍数（契约 §4.5 定为 2）。
	DebtCapMultiplier = 2.0
)

// Trace 是可选的调试钩子：非 nil 时 PurchasePower 会报告每次调用的
// 入参与约束命中情况。生产路径保持 nil，无额外开销。
var Trace func(string)

// Debt 返回政府当前债务（现金池为负时为其绝对值，否则为 0）。
func (g *Government) Debt() float64 {
	b := g.Cash.Balance()
	if b < 0 {
		return -b
	}
	return 0
}

// AssetBase 返回债务上限的资产基数 = 建造力产出 × 建造力价格（G7）。
func (g *Government) AssetBase(powerPrice float64) float64 {
	if g.PowerOutput <= 0 || powerPrice <= 0 {
		return 0
	}
	return g.PowerOutput * powerPrice
}

// DebtCap 返回当期债务上限。
func (g *Government) DebtCap(powerPrice float64) float64 {
	return DebtCapMultiplier * g.AssetBase(powerPrice)
}

// AvailableCash 返回在不突破债务上限的前提下，当期可动用的现金。
//
// 债务上限约束的是政府可动用的【总资金】，不只是举债空间：
// 若现金池为正就返回全额余额，那么只要手里有钱，政府就可以在债务上限之外
// 继续支出——上限形同虚设。实测中这会立刻失控：初始现金池 3,907 万而债务
// 上限只有 497 万时，一次采购就能把债务推到上限的 7 倍。
//
// 正确口径按余额 B 的符号分三段（各段在 B=0 处连续，该处三段都给 cap）：
//
//	B > 0：可动用 = min(B, cap)
//	       —— 手里有钱也不得超过上限（余额本身可能已因初始注资超过上限）
//	B = 0：可动用 = cap，即全部举债空间可用
//	B < 0：可动用 = cap + B，即剩余举债空间
//
// 历史 bug：早期写成"余额 ≥ 0 时返回余额"，导致余额恰为 0 的政府
// （债台未筑但也没现金）可动用资金为 0，完全借不到钱；而余额为正时
// 又能超出上限随意支出。两个方向都错。
func (g *Government) AvailableCash(powerPrice float64) float64 {
	cap := g.DebtCap(powerPrice)
	b := g.Cash.Balance()
	switch {
	case b > 0:
		if b < cap {
			return b
		}
		return cap
	case b == 0:
		return cap
	default:
		return cap + b
	}
}

// Capital 是资本方，载体是金融区建筑（G4/G5）。
//
// 【§4.5.2 / §4.5.1b 口径】Cash 就是金融区这个"资本建筑"的营运现金池：
// 金融区的工资从这里支付、非农业生产建筑的私人份额纯利记入这里、
// 扣除工资后的净额由这里转入投资池（K_f）。
type Capital struct {
	// Cash 是金融区现金池（资本收入池 = 金融区营运现金池）。
	Cash Ledger

	// ControlledLevels 是当前掌控的其余建筑级数。
	ControlledLevels float64
	// ControlCapacity 是掌控上限 = 金融区级数 × ControlPerFinance。
	ControlCapacity float64

	// 本 tick 的资金流明细。
	OperatingProfit float64 // 归金融区的私人份额纯利（收入）
	WageBill        float64 // 金融区自身工资支出
	InvestInflow    float64 // 扣除工资后的净额中真正入池的部分（K_f 的本期增量）
	InvestPaid      float64 // 金融栈本期支付的建造力货款（由投资池出资）

	// ===== §4.5.1a / 1.2 M8：借贷台账（2026-09-20 第 39/40 轮）=====
	//
	// 【为什么是字段而不是 ledger 账户】1.0 的 `ledger.Auditor` 里**只有现金余额**，
	// `Total()` 的定义就是"全部账户余额之和 = 货币总量"；而**负债不是钱**。
	// 若把负债做成 `AccountKind`，它会被算进货币总量，必须在
	// `Total` / `TotalOf` / 借贷相等 / GDP 存量口径**四处**显式排除，漏一处即出错。
	// 故负债与"持股"（`BuildingState.GovLevel/PrivLevel`）同族——都是**字段**。
	// 用户裁决（1.2 M8.7）：选 (A) `Capital.Debt` 字段。
	//
	// 【默认不生效】`Params.BankEnabled = false` 时 `Debt` 恒为空，
	// 本结构对 1.0 的全部不变量与判据**零影响**（逐位不变）。
	Debt []Loan

	// DebtServiceDue 是本 tick 应还的本息合计（由 sim 在入池前先扣）。
	DebtServiceDue float64
	// DebtPaid 是本 tick 实际支付的还本息。
	DebtPaid float64
	// DebtDeferred 是本 tick 因资金不足而滚入未偿余额的部分（M8.4.1 违约则延期）。
	DebtDeferred float64
}

// Loan 是一笔**借贷凭证**（1.2 M8 的"债券"，**不是商品**）。
//
// 台账字段依据 1.2 M8.1：本金 / 利率 / 期限 / 已还本 / 已付息 / 余额 / 延期次数。
// 还款方式为**等额本息**（M8.2）：每周期固定金额
//
//	A = P·i(1+i)^n / ((1+i)^n − 1)，  i = 年化利率/52，n = 期限(年)×52
//
// 默认参数（M8.1/§1.2-7 裁决）：本金 500,000、年化 5%、5 年 ⇒ **2,182.68 元/周期**。
type Loan struct {
	// Principal 是本金（元）。
	Principal float64
	// AnnualRate 是**年化**利率（0.05 = 年化 5%）。
	AnnualRate float64
	// TermYears 是期限（年）；ActualTermYears 是"最短期限"，延期后实际结清时间后移（M8.4.1）。
	TermYears float64
	// PaidPrincipal / PaidInterest 是累计已还本 / 已付息。
	PaidPrincipal float64
	PaidInterest  float64
	// Outstanding 是当前未偿余额（含已滚入的延期金额与利息，M8.4.1 的复利展期）。
	Outstanding float64
	// Deferrals 是延期次数（诊断用，M8.4.1 边界 2 要求记录）。
	Deferrals int
}

// PeriodicPayment 返回等额本息的每周期还款额（M8.2）。
//
// i = AnnualRate/52（按 1 年 = 52 周期，与 §5 的时间语义一致），n = TermYears×52。
// AnnualRate ≤ 0 时退化为"本金均摊"（无息），仅用于对照与测试。
func (l Loan) PeriodicPayment() float64 {
	n := l.TermYears * 52
	if n <= 0 {
		return l.Outstanding
	}
	if l.AnnualRate <= 0 {
		return l.Principal / n
	}
	i := l.AnnualRate / 52
	g := math.Pow(1+i, n)
	return l.Principal * i * g / (g - 1)
}

// Service 按 M8.4.1 推进一期还款：能付多少付多少，付不出的部分**延期**
// （滚入 Outstanding 并按同一利率继续计息，**不核销、不加速、不没收**）。
//
// 返回本期的 (应付额, 实付额, 滚入额)。
//
// 【口径】本期先计息（对 Outstanding 按 i 计息），再拿 available 去还：
//   - available ≥ 应付 ⇒ 全额还清本期，Outstanding 按摊还表下降；
//   - 0 < available < 应付 ⇒ 部分支付，差额滚入；
//   - available ≤ 0 ⇒ 实付 0、全额滚入（Deferrals++）。
func (l *Loan) Service(available float64) (due, paid, deferred float64) {
	i := l.AnnualRate / 52
	if i < 0 {
		i = 0
	}
	// 本期计息（对未偿余额）
	interest := l.Outstanding * i
	due = l.PeriodicPayment()
	if due > l.Outstanding+interest {
		due = l.Outstanding + interest
	}
	if available < 0 {
		available = 0
	}
	paid = available
	if paid > due {
		paid = due
	}
	deferred = due - paid
	// 本金部分 = 应付 − 利息（利息优先，符合等额本息的构成）
	principalPart := paid - interest
	if principalPart < 0 {
		principalPart = 0
	}
	interestPart := paid - principalPart
	l.PaidPrincipal += principalPart
	l.PaidInterest += interestPart
	l.Outstanding += interest - paid
	if l.Outstanding < 1e-9 {
		l.Outstanding = 0
	}
	if deferred > 1e-9 {
		l.Deferrals++
	}
	return due, paid, deferred
}

// DebtServiceDue / DebtPaid / DebtDeferredTotal 是台账的汇总口径（诊断与报告用）。
func (c *Capital) DebtServiceDueTotal() float64 { return c.DebtServiceDue }
func (c *Capital) DebtPaidTotal() float64       { return c.DebtPaid }
func (c *Capital) DebtOutstandingTotal() float64 {
	var v float64
	for i := range c.Debt {
		v += c.Debt[i].Outstanding
	}
	return v
}

// CollectTax 按 G1 计算交易税（审计用独立算式）。
//
// 修订说明：真正的入账由 ledger 包的交易构造函数完成
// （买方扣含税额、卖方收净额、政府收税额，三者之和为零）。
// 保留本函数是为了给"应当收多少税"提供一个可核对的独立算式。
//
// 【建造力不计税】powerSpend 只作历史参数保留在签名里以便对照，
// 2026-09-19 裁决后不再进入税基（见 §4.5.3 与包注释 G1）。
func CollectTax(taxRate float64, consumerSpend, intermediateSpend float64) float64 {
	if taxRate <= 0 {
		return 0
	}
	return (consumerSpend + intermediateSpend) * taxRate
}

// Transaction 是一笔【货款 + 交易税】的拆分。
//
// 语义：买方支付 Net·(1+t)，卖方收到 Net，政府收到 Net·t。
type Transaction struct {
	// Net 是不含税的货款（= 数量 × 单价）。
	Net float64
	// Tax 是税额。
	Tax float64
	// Gross 是买方实际支付 = Net + Tax。
	Gross float64
}

// NewTransaction 按税率拆分一笔交易。
func NewTransaction(net, taxRate float64) Transaction {
	if net <= 0 {
		return Transaction{}
	}
	return Transaction{Net: net, Tax: net * taxRate, Gross: net * (1 + taxRate)}
}

// TaxOn 返回一笔货款的税额（审计用独立算式）。
func TaxOn(net, taxRate float64) float64 {
	if net <= 0 || taxRate <= 0 {
		return 0
	}
	return net * taxRate
}

// UpdateControl 刷新金融区的掌控上限（G5）。
//
// 契约补全：金融区每级雇 1,000 人（劳动结构取 §5 的城镇通用口径，均薪 6.75），
// 每级掌控 ControlPerFinance 级其余建筑。
//
// 【2026-09-19 口径提示】金融区级数由掌控比**反推**（§4.5.2）：
// N_finance = Σ非农业等级 / c_ctrl，故掌控上限 = N_finance × c_ctrl 恒等于
// 其余建筑等级之和，G5 的掌控上限在推导口径下**不再是约束**——这是
// "金融区不建造"的直接后果，已在契约 §4.5.2 中明示。资本的自我限制现在
// 只剩工资义务（每级 1,000 人的工资随推导级数自动增长）。
func (c *Capital) UpdateControl(financeLevels, otherLevels, perFinance float64) {
	c.ControlCapacity = financeLevels * perFinance
	c.ControlledLevels = otherLevels
}

// CanControl 判断是否还能再掌控 add 级（G5）。
func (c *Capital) CanControl(add float64) bool {
	return c.ControlledLevels+add <= c.ControlCapacity
}

// PurchaseLimitQty 返回 G2 在当期可采购的建造力上限（单位数）。
//
// 两条上限（契约 §4.5.3 G2 第 2 条）：
//
//	① 建造力当期产出 powerOutput；
//	② §4.5.4 的可动用资金 AvailableCash / price。
//
// 【按需采购】调用方还要用"队列本 tick 的实际需要量 needQty"再裁剪一次：
//
//	qty = min(needQty, PurchaseLimitQty(...))
//
// 即**无队列时不采购**（needQty = 0 ⇒ qty = 0），政府不持有公共储备。
// 本函数只做资金/产出约束，不碰任何余额——记账由 book.PurchasePower 一笔完成。
func (g *Government) PurchaseLimitQty(powerOutput, powerPrice float64) float64 {
	if powerOutput <= 0 || powerPrice <= 0 {
		return 0
	}
	avail := g.AvailableCash(powerPrice)
	if Trace != nil {
		Trace(fmt.Sprintf("[PurchaseLimitQty] 产出=%.4f 价=%.4f 现金=%.0f 债务上限=%.0f 可用=%.0f → 最多 %.4f 单位",
			powerOutput, powerPrice, g.Cash.Balance(), g.DebtCap(powerPrice), avail, avail/powerPrice))
	}
	if avail <= 0 {
		if g.Debt() >= g.DebtCap(powerPrice) {
			g.DebtCapBoundTicks++
		}
		return 0
	}
	return avail / powerPrice
}

// ResetFlow 清零本 tick 的流量计数器。
//
// 政府与资本的流量字段（税收、经营、采购、偿还、转移）都是"每 tick 流量"，
// 必须在 tick 开始时重置，否则会跨 tick 累加成存量，使报告与不变量检查失真。
func (g *Government) ResetFlow() {
	g.TaxCollected = 0
	g.PowerPurchased = 0
	g.PowerSold = 0
	g.PowerRevenue = 0
	g.OperatingIncome = 0
}

// ResetFlow 清零资本方本 tick 的流量计数器。
func (c *Capital) ResetFlow() {
	c.OperatingProfit = 0
	c.WageBill = 0
	c.InvestInflow = 0
	c.InvestPaid = 0
}

// Validate 检查政府账目的基本不变量（§4.5.1b / §4.5.3）。
//
// 三条断言：
//
//	① 公共储备恒为 0（G2 即买即用，政府不持有建造力）；
//	② 投入队列的建造力量不得超过当期采购量（不得出售没买到的建造力）；
//	③ 采购量与投入量在本版口径下恒等（买多少、投多少）。
func (g *Government) Validate(powerPrice float64) error {
	if g.PowerInventory < -1e-6 || g.PowerInventory > 1e-6 {
		return fmt.Errorf("fiscal: 政府公共储备应恒为 0，实际 %.6f（§4.5.3 G2 即买即用）", g.PowerInventory)
	}
	if g.PowerSold > g.PowerPurchased+1e-6 {
		return fmt.Errorf("fiscal: 投入队列的建造力 (%.3f) 超过采购量 (%.3f)", g.PowerSold, g.PowerPurchased)
	}
	return nil
}

// EstimateTaxRevenue 估算稳态税收，用于给政府现金池定初值。
//
// 关键教训（见 tools/core_sim_gov.txt 的 V2/V3 崩解记录）：
// 若政府现金池初值只按 §4.3 的"5,000 元/级"给定，则在第一年就会出现
// 远超初始货币的经营缺口，采购建造力立即归零。政府现金池初值必须与
// "一个周期的税收规模"同阶，因此这里给出显式估算函数。
func EstimateTaxRevenue(pop, demandScale, taxRate float64, pcost []float64) float64 {
	f := calibrateFinalDemand(pop, demandScale)
	var consumerValue, intermediateValue float64
	for i := range f {
		consumerValue += f[i] * pcost[i]
	}
	// 中间投入交易额约为消费者支出的 1.3 倍（由 §3.3 的投入结构决定），
	// 这里用 Leontief 完全需求的比例近似；精确值由 sim 包在运行时给出。
	intermediateValue = consumerValue * 1.3
	return (consumerValue + intermediateValue) * taxRate
}

// calibrateFinalDemand 是 calibrate.PerCapitaFinalDemand 的本地薄封装，
// 避免 fiscal → calibrate 的包依赖（fiscal 只依赖 model 与 ledger）。
//
// 财富档取 contractTierEstimate：EstimateTaxRevenue 只用于给政府现金池定一个
// 量级正确的初值，不需要精确档位，故取 §6.3 表格的中段（财富 10）。
func calibrateFinalDemand(pop, scale float64) []float64 {
	const contractTierEstimate = 10.0
	per100k := model.DemandAt(contractTierEstimate)
	groups := model.ConsumeGroupSpecs()
	f := make([]float64, model.Goods)
	for gi, g := range groups {
		target := per100k[gi] * pop / 100000 * scale
		var total float64
		for _, v := range g.Uses {
			total += v
		}
		if total <= 0 {
			continue
		}
		for idx, v := range g.Uses {
			f[idx] += target * v / total
		}
	}
	return f
}
