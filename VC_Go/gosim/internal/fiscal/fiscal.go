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
// ============================ 解法（G1–G6） ============================
//
// G1 政府抽取全部交易额的税率 t 作为税收。
// G2 建造力必须在市场内购买，最终购买方是政府；政府现金池支付 P_power。
// G3 建筑分所有权：政府 / 私有。
// G4 私有建筑的运营纯利划归金融区（资本收入）。
// G5 金融区是建筑：每级雇 1,000 人，每级掌控 5 级其余建筑。
// G6 私有扩建的资金从建筑现金池划拨给政府现金池（政府代建）。
//
// 为什么这样能闭合：建造力是【唯一没有最终消费者】的商品，其唯一买家是投资需求。
// 政府用税收采购建造力并把它卖给要扩建的企业：
//
//	税收 → 政府 → 建造部门（工资 + 投入）→ 回到居民/企业流通
//	企业现金池 → 政府（购买建造力）
//
// 两条流方向相反、金额相等，因此【总流通货币不变】，同时扩建获得了真实资金约束。
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
// docs/CHANGES-1.0.md）。收敛到 ledger 之后，守恒成为构造性事实。
package fiscal

import (
	"errors"
	"fmt"

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

	// PowerInventory 是政府已采购但尚未售出的建造力（公共储备）。
	PowerInventory float64

	// PowerOutput 是本 tick 建造部门的产出，用于计算债务上限的资产基数（G7）。
	PowerOutput float64

	// DebtCapBoundTicks 记录债务触及上限的累计 tick 数，用于诊断。
	DebtCapBoundTicks int64

	// TaxRate 是交易税率（§4.5.3 G1）。冗余保存一份，使 SellPower 能在
	// 政府自建项目的付款里算出含税总额，而不必让调用方重复传参。
	TaxRate float64

	// 本 tick 的资金流明细（报告用）。
	TaxCollected    float64 // 税收总额（消费端 + 中间环节 + 建造力交易）
	PowerPurchased  float64 // 向建造部门采购的建造力
	PowerSold       float64 // 售给扩建方的建造力
	PowerRevenue    float64 // 售出建造力的货款（净额）
	OperatingIncome float64 // 政府建筑的经营净额
	BuildoutPaid    float64 // 政府为自己项目支付的建造力货款（含税总额）
	// OwnBuildoutPaid 是通过 SellPower 真实走完双边记账的那部分。
	OwnBuildoutPaid float64
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

// CanAfford 判断支出 amount 后是否仍在债务上限内。
func (g *Government) CanAfford(amount, powerPrice float64) bool {
	if amount <= 0 {
		return true
	}
	// 支出后余额不得低于「−债务上限」
	return g.Cash.Balance()-amount >= -g.DebtCap(powerPrice)
}

// Capital 是资本方，载体是金融区建筑（G4/G5）。
type Capital struct {
	// Cash 是金融区现金池（资本收入池）。
	Cash Ledger

	// ControlledLevels 是当前掌控的其余建筑级数。
	ControlledLevels float64
	// ControlCapacity 是掌控上限 = 金融区级数 × ControlPerFinance。
	ControlCapacity float64

	// 本 tick 的资金流明细。
	OperatingProfit float64 // 从私有建筑取得的运营纯利
	WageBill        float64 // 金融区自身工资支出
	BuildoutPaid    float64 // 金融区为其项目支付的建造力货款
}

// ErrControlLimit 表示扩建请求超过金融区的掌控上限（G5）。
var ErrControlLimit = errors.New("fiscal: 超出金融区掌控上限")

// CollectTax 按 G1 计算交易税（审计用独立算式）。
//
// 修订说明：真正的入账由 ledger 包的交易构造函数完成
// （买方扣含税额、卖方收净额、政府收税额，三者之和为零）。
// 保留本函数是为了给"应当收多少税"提供一个可核对的独立算式。
func CollectTax(taxRate float64, consumerSpend, intermediateSpend, powerSpend float64) float64 {
	if taxRate <= 0 {
		return 0
	}
	return (consumerSpend + intermediateSpend + powerSpend) * taxRate
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

// ShareProfit 按【划分】语义拆分一笔运营纯利。
//
// 返回值三者之和恒等于入参 profit —— 这是消除重复记账的充要条件。
//
//	gov     = profit × govShare                    → 政府现金池（G3）
//	retain  = profit × (1−govShare) × retainRatio  → 建筑自身现金池（用于扩建，§4.3）
//	capital = profit × (1−govShare) × (1−retainRatio) → 资本（金融区）现金池（G4）
//
// 修订说明（docs/AUDIT-1.0.md §4）：原 DistributeProfit 让建筑池拿全额利润、
// 政府/资本池再各拿一份，实测 Δ货币/Σ利润 = 2.00（重复入账）。
func ShareProfit(profit, govShare, retainRatio float64) (gov, capital, retain float64) {
	if govShare < 0 {
		govShare = 0
	}
	if govShare > 1 {
		govShare = 1
	}
	if retainRatio < 0 {
		retainRatio = 0
	}
	if retainRatio > 1 {
		retainRatio = 1
	}
	gov = profit * govShare
	priv := profit * (1 - govShare)
	retain = priv * retainRatio
	capital = priv - retain
	return
}

// UpdateControl 刷新金融区的掌控上限（G5）。
//
// 契约补全：金融区每级雇 1,000 人（阶层比例 75/20/5 不变，均薪 6.75），
// 每级掌控 ControlPerFinance 级其余建筑。这是资本扩张的硬约束——
// 资本部门自身也要占用建造力与劳动力，构成扩张的自我限制。
func (c *Capital) UpdateControl(financeLevels, otherLevels, perFinance float64) {
	c.ControlCapacity = financeLevels * perFinance
	c.ControlledLevels = otherLevels
}

// CanControl 判断是否还能再掌控 add 级（G5）。
func (c *Capital) CanControl(add float64) bool {
	return c.ControlledLevels+add <= c.ControlCapacity
}

// PurchasePower 执行 G2：政府向建造部门采购建造力。
//
// 记账口径（G7 + §4.5.3）：
//
//	政府池   −Gross
//	政府税收 +Tax（自己收自己，仍在池内，故净值恰为 −Net）
//
// 本函数只做政府侧的扣款与计税。给建造力卖方入账 Net 是调用方
// （sim.step）的职责——那里持有建筑账户标识，而 fiscal 包不应依赖 sim。
// 二者合起来才构成借贷相等的完整交易。
//
// PowerPurchased 是【本 tick 的累计流量】而非单次值，故此处累加。
// 若写成赋值，同一 tick 内的第二次采购会覆盖第一次的记录，
// 使"售出 ≤ 采购"的不变量被误判为违反（历史 bug）。
func (g *Government) PurchasePower(powerOutput, powerPrice float64) float64 {
	if powerOutput <= 0 || powerPrice <= 0 {
		return 0
	}
	avail := g.AvailableCash(powerPrice)
	if Trace != nil {
		Trace(fmt.Sprintf("[PurchasePower] 传入产出=%.4f 传入价=%.4f 现金=%.0f 债务上限=%.0f 可用=%.0f → 最多 %.4f 单位",
			powerOutput, powerPrice, g.Cash.Balance(), g.DebtCap(powerPrice), avail, avail/powerPrice))
	}
	if avail <= 0 {
		if g.Debt() >= g.DebtCap(powerPrice) {
			g.DebtCapBoundTicks++
		}
		return 0
	}
	qty := powerOutput
	if afford := avail / powerPrice; qty > afford {
		qty = afford
	}
	if qty <= 0 {
		return 0
	}
	gross := qty * powerPrice
	tax := gross * g.TaxRate / (1 + g.TaxRate)
	// 借 政府 Gross；贷 政府 Tax（自己收自己，仍留在池内）
	// ⇒ 政府池净减少恰为 Net = Gross − Tax，与建造部门收到的 Net 对等。
	// 若漏掉后面那笔 Tax 回冲，政府池会净减 Gross，与卖方不对等，
	// 留下 Net·t 量级的差额（实测 15071.84 = 150718.44×10%）。
	g.Cash.Add(-gross)
	g.Cash.Add(tax)
	g.TaxCollected += tax
	g.PowerPurchased += qty
	g.PowerInventory += qty
	return qty
}

// ResetFlow 清零本 tick 的流量计数器。
//
// 政府与资本的流量字段（税收、经营、采购、售出、转移）都是"每 tick 流量"，
// 必须在 tick 开始时重置，否则会跨 tick 累加成存量，使报告与不变量检查失真。
func (g *Government) ResetFlow() {
	g.TaxCollected = 0
	g.PowerPurchased = 0
	g.PowerSold = 0
	g.PowerRevenue = 0
	g.OperatingIncome = 0
	g.BuildoutPaid = 0
	g.OwnBuildoutPaid = 0
}

// ResetFlow 清零资本方本 tick 的流量计数器。
func (c *Capital) ResetFlow() {
	c.OperatingProfit = 0
	c.WageBill = 0
	c.BuildoutPaid = 0
}

// SaleRequest 是一次建造力购买请求（由扩建意向产生）。
type SaleRequest struct {
	// BuildingIndex 是申请扩建的建筑类别下标。
	BuildingIndex int
	// Units 是申请扩建的等级数。
	Units float64
	// PowerPerLevel 是该建筑的建造成本（建造力/级，§3.2）。
	PowerPerLevel float64
	// Payer 说明谁出钱：'gov' 走政府现金池，'capital' 走金融区现金池，'firm' 走建筑现金池。
	Payer string
}

// SaleResult 是一次成交的结果。
type SaleResult struct {
	BuildingIndex int
	Granted       float64 // 实际成交的建造力
	Paid          float64 // 实付货款（净额）
	Payer         string
}

// SellPower 执行 G2/G6：把政府持有的建造力卖给扩建方。
//
// 这是闭环的另一半：企业为获得建造力付款给政府，货币回到政府，
// 而在此之前政府已经用这笔钱支付了建造部门的工资与投入，货币已经进入流通。
// 净效果 = 货币总量不变 + 建造力从建造部门转移到投资方。
//
// 记账口径（§4.5.3）：
//
//	外部付款方（firm / capital）：
//	    借 付款方  Gross = Paid·(1+t)   （由调用方的 debit 回调完成）
//	    贷 政府    Net + Tax            （两者都进政府池，合并为一笔）
//
//	政府自有项目（payer == "gov"）：
//	    借 政府    Gross
//	    贷 建造力卖方  Net              （由 credit 回调入账）
//	    贷 政府    Tax                  （自己收自己，仍留在池内）
//	  净值：政府池 −Net，卖方 +Net，税收 +Tax
//
// 【为什么政府自建也必须双边记账】旧实现把它当"内部转账"，只累加
// BuildoutPaid 而【不动任何账户余额】——工程是白得的，货币审计会看到
// 一笔无人认领的差额。实测残差恰等于该笔付款。
//
// wallets 提供三类付款方的可用余额读取器，避免本包依赖 sim 包。
// credit 是给建造力卖方的入账回调（政府自建项目的收款方）。
func (g *Government) SellPower(
	requests []SaleRequest,
	salePrice, sitePowerLimit float64,
	balanceOf func(payer string, buildingIndex int) float64,
	credit func(net float64),
) []SaleResult {
	taxRate := g.TaxRate
	if taxRate < 0 {
		taxRate = 0
	}
	if credit == nil {
		credit = func(float64) {}
	}
	limit := g.PowerInventory
	results := make([]SaleResult, 0, len(requests))
	for _, req := range requests {
		if limit <= 1e-9 {
			break
		}
		need := req.PowerPerLevel * req.Units
		want := need
		if want > sitePowerLimit {
			want = sitePowerLimit // §4.2：每工地每 tick 最多投入 30 建造力
		}
		if want > limit {
			want = limit
		}
		avail := balanceOf(req.Payer, req.BuildingIndex)
		if avail < 0 {
			avail = 0
		}
		grant := want
		if afford := avail / salePrice; grant > afford {
			grant = afford
		}
		if grant <= 1e-9 {
			continue
		}
		paid := grant * salePrice

		if req.Payer == "gov" {
			gross := paid * (1 + taxRate)
			if !g.CanAfford(gross, salePrice) {
				g.DebtCapBoundTicks++
				continue
			}
			// 借 政府 Gross；贷 卖方 Net；贷 政府 Tax
			g.Cash.Add(-gross)
			credit(paid)
			g.Cash.Add(paid * taxRate)
			g.TaxCollected += paid * taxRate
			g.BuildoutPaid += gross
			g.OwnBuildoutPaid += gross
		} else {
			// 借 付款方 Gross（由调用方的 debit 回调完成）
			// 贷 政府 Net + Tax（两者都进政府池）
			g.Cash.Add(paid * (1 + taxRate))
			g.TaxCollected += paid * taxRate
			g.PowerRevenue += paid
		}
		limit -= grant
		g.PowerSold += grant
		results = append(results, SaleResult{
			BuildingIndex: req.BuildingIndex,
			Granted:       grant,
			Paid:          paid,
			Payer:         req.Payer,
		})
	}
	g.PowerInventory = limit
	return results
}

// Validate 检查政府与资本账目的基本不变量。
func (g *Government) Validate(powerPrice float64) error {
	if g.PowerInventory < -1e-6 {
		return fmt.Errorf("fiscal: 政府建造力库存为负 (%.6f)", g.PowerInventory)
	}
	if g.PowerSold > g.PowerPurchased+1e-6 {
		return fmt.Errorf("fiscal: 售出建造力 (%.3f) 超过采购量 (%.3f)", g.PowerSold, g.PowerPurchased)
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
