// Package book 是 1.0 契约的【统一记账簿】。
//
// ============================ 为什么要单独一个文件 ============================
//
// 修订前，"钱从哪个池扣、记到哪个池"散落在 sim / fiscal / consume / cohort
// 四个包的十几个 Cash.Add 调用点上，各写各的。后果是货币守恒只能靠事后审计
// 发现，而审计每修一处、残差就跑到另一处——实测连续暴露四个不同层级的缺陷：
//
//	① 工资只作为成本扣减，钱从未真正付出（居民没有账户）
//	② 利润被建筑现金池与政府池各记一次（Δ货币/Σ利润 = 2.00）
//	③ 对中间投入计征的税，政府收了但没有任何账户被扣
//	④ 政府自建项目付款只累加计数、不动任何账户余额（工程是白得的）
//
// 本文件的做法是：把所有离散的记账【收进一个文件】。
// 每一类资金流动在这里只有一个方法，方法名即交易名，借贷两侧写在同一处，
// 由 ledger.Auditor.Post 校验 Σ借 == Σ贷。
//
// 业务代码（sim.step）只调用本文件的方法，不得自行读写任何余额。
// 这样"漏记一半"在结构上就不可能发生：没有"另一半"可以漏。
//
// ============================ 与 ledger 包的分工 ============================
//
//	ledger  提供机制：账户、分录、Txn、校验、总账
//	book    提供语义：工资 / 消费 / 中间投入 / 按需购力 / 投资池入池与偿还 /
//	                  利润归属 / 补贴 / 私有化 / 资本注入
//
// book 是唯一被允许调用 Aud.Post 的地方（除开局注资外）。
package book

import (
	"fmt"

	"yehenala/market/internal/ledger"
)

// Book 是统一记账簿。它持有账本与全部账户标识，业务代码只与它交互。
type Book struct {
	// Aud 是底层审计账本（余额唯一存放处）。
	Aud *ledger.Auditor
	// Buildings 是建筑类别数。
	Buildings int
	// Classes 是阶级数（§5：3）。
	Classes int
	// PowerIdx 是"建造力"在建筑类别中的下标（§3.1 最后一项）。
	PowerIdx int
	// FinanceIdx 是金融区下标。
	FinanceIdx int
	// WarehouseIdx / AgentIdx 是 §4.5.6 的仓库与消费代理下标。
	WarehouseIdx int
	AgentIdx     int

	// injected 累计注入的营运本金（唯一合法的货币创造）。
	injected float64
}

// New 建立记账簿。
func New(worksites, classes, powerIdx, financeIdx int) *Book {
	return &Book{
		Aud:        ledger.NewAuditor(),
		Buildings:  worksites,
		Classes:    classes,
		PowerIdx:   powerIdx,
		FinanceIdx: financeIdx,
	}
}

// ===== 账户构造 =====

func (b *Book) Gov() ledger.Account        { return ledger.Gov() }
func (b *Book) Capital() ledger.Account    { return ledger.Capital() }
func (b *Book) Investment() ledger.Account { return ledger.Investment() }
func (b *Book) Savings() ledger.Account    { return ledger.Savings() }
func (b *Book) Bld(i int) ledger.Account {
	return ledger.Building(i)
}

// House 返回（场地, 阶级）对应的人群账户。
func (b *Book) House(site, class int) ledger.Account {
	return ledger.Household(site, class, b.Classes)
}

// ===== 余额读取 =====

func (b *Book) Bal(a ledger.Account) float64 { return b.Aud.Balance(a) }

// BalGov / BalCapital / BalInvestment / BalBld / BalHouse 是常用的便捷读取。
func (b *Book) BalGov() float64        { return b.Aud.Balance(ledger.Gov()) }
func (b *Book) BalCapital() float64    { return b.Aud.Balance(ledger.Capital()) }
func (b *Book) BalInvestment() float64 { return b.Aud.Balance(ledger.Investment()) }
func (b *Book) BalSavings() float64    { return b.Aud.Balance(ledger.Savings()) }
func (b *Book) BalBld(i int) float64 {
	return b.Aud.Balance(ledger.Building(i))
}
func (b *Book) BalHouse(site, class int) float64 {
	return b.Aud.Balance(b.House(site, class))
}

// Total 返回全社会货币存量。
func (b *Book) Total() float64 { return b.Aud.Total() }

// TotalBuildings 返回全部建筑现金池之和。
func (b *Book) TotalBuildings() float64 { return b.Aud.TotalOf(ledger.KindBuilding) }

// TotalHouseholds 返回全部人群现金池之和。
func (b *Book) TotalHouseholds() float64 { return b.Aud.TotalOf(ledger.KindHousehold) }

// TotalInvestment 返回投资池余额（§4.5.1b）。
func (b *Book) TotalInvestment() float64 { return b.Aud.TotalOf(ledger.KindInvestment) }

// TotalSavings 返回**居民储蓄固定账户**余额（§5.3，2026-09-19 第 20 轮）。
// 默认 σ_save = 1 时它每 tick 归零（结余当期全额转入投资池）。
func (b *Book) TotalSavings() float64 { return b.Aud.TotalOf(ledger.KindSavings) }

// ===== 开局注资（唯一不走交易构造的资金注入）=====

// Endow 给某账户注资，用于 New 的开局布点。
func (b *Book) Endow(a ledger.Account, v float64) { b.Aud.SetBalance(a, v) }

// ===== 校验与诊断 =====

// Post 过账一笔交易；借贷不等时返回错误。
func (b *Book) Post(t *ledger.Txn) error { return b.Aud.Post(t) }

// Violations 返回全部借贷不相等记录（应为空）。
func (b *Book) Violations() []string { return b.Aud.Violations() }

// TotalInjected 返回累计的货币注入额（§4.3 营运本金）。
func (b *Book) TotalInjected() float64 { return b.injected }

// ===== ⑥ 利润归属（§4.5.1，2026-09-19 新口径）=====

// ProfitResult 是一次利润归属的三条腿。
type ProfitResult struct {
	// Retain 是补足建筑自身现金池的留池额 R_i（恒 ≥ 0）。
	Retain float64
	// Gov 是政府份额（盈利时 ≥ 0；亏损时 < 0 = 政府按持股承担亏损）。
	Gov float64
	// Owner 是所属资本建筑的私人份额（盈利时 ≥ 0；亏损时 < 0）。
	Owner float64
	// Labor 是私人份额中**归劳动力**的那一腿（1.2 M4.2；未开重构时恒为 0）。
	//
	// 口径：`Labor = Owner × laborShare`，而资本实得 `Owner − Labor`
	// ⇒ 两腿之和逐位等于 `Owner`（见 `ProfitAllocateSplit`）。
	Labor float64
	// Profit 是参与划分的纯利 π_i 本身（留池 + 政府 + 所有者 ≡ π_i）。
	Profit float64
}

// ProfitAllocate 按 §4.5.1 把一笔运营纯利【归属】给三条腿。
//
//	借 建筑[i]        纯利
//	贷 建筑[i]        补足自身现金池 R_i
//	贷 政府           政府份额
//	贷 所属资本建筑     私人份额
//
// 【删除留存比例 r_ret】补足额不再是"私人份额的一个比例"，而是
// R_i = min(max(π_i, 0), max(0, C*_i − B_i))：把自身现金池补到营运资金目标
// C*_i（= 一个周期的满编营运成本）为止。补足之后的余额 π^net_i 才按**当期**
// 持股比例 s_gov 支付：政府份额 = π^net_i·s_gov，私人份额 = π^net_i·(1−s_gov)。
//
// 【亏损（π_i < 0）】不补池、不分配：政府池 −|π_i|·s_gov、所属资本建筑池
// −|π_i|·(1−s_gov)，同额贷记建筑现金池（回补本期营运支出），三条腿之和恒为 0。
//
// 【借贷相等的充要条件】三条腿之和恒等于纯利——本方法自行按
// "先补池、再按持股"的顺序算出三者，调用方只提供 C*_i 与当期持股，
// 因此"忘了把某一份额算进去"在结构上不可能发生。
//
// 参数：
//
//	profit  是本期运营纯利 π_i（可为负）
//	govShare 是**当期**政府持股比例 s_gov（调用方按 GovLevel/Level 给出）
//	cstar   是营运资金目标 C*_i（一个周期的满编营运成本）
//	balance 是建筑当前现金池余额 B_i
//
// owner 是所属资本建筑的账户（农业建筑 → 宅邸庄园；其余 → 金融区）。
//
// 【1.2 M4.2 所有权重构】`laborShare` 是**私人份额中归劳动力**的比例（默认 0）：
//
//	= 0     ⇒ 走原单腿路径（`ledger.ProfitAllocate`），1.0 逐位不变
//	= 0.70  ⇒ 走两腿路径（`ledger.ProfitAllocateSplit`），资本留 30%、劳动力得 70%
//
// 它由调用方按 `Params.OwnershipRestructure` / `OwnershipCapitalShare` 算好传入，
// **本函数不做策略判断**——book 层只负责"按给定比例把腿拆对"。
func (b *Book) ProfitAllocate(
	i int, profit, govShare, cstar, balance float64,
	owner ledger.Account, laborShare float64,
) ProfitResult {
	if govShare < 0 {
		govShare = 0
	}
	if govShare > 1 {
		govShare = 1
	}
	if laborShare < 0 {
		laborShare = 0
	}
	if laborShare > 1 {
		laborShare = 1
	}
	res := ProfitResult{Profit: profit}
	if profit >= 0 {
		room := cstar - balance
		if room < 0 {
			room = 0
		}
		res.Retain = profit
		if res.Retain > room {
			res.Retain = room
		}
		net := profit - res.Retain
		res.Gov = net * govShare
		// 【残差归所有者】私人份额用减法而不是再乘一次 (1−govShare)，
		// 保证 留池 + 政府 + 所有者 逐位等于纯利（浮点乘法不满足结合律）。
		res.Owner = net - res.Gov
	} else {
		res.Gov = profit * govShare
		res.Owner = profit - res.Gov
	}
	// 【同样的纪律用在资本/劳动力拆分上】只算 laborAmt = Owner × laborShare，
	// 资本那腿取 `Owner − laborAmt`（由 `ProfitAllocateSplit` 内部做减法）
	// ⇒ 两腿之和**逐位**等于 Owner，不引入浮点乘法误差。
	laborAmt := res.Owner * laborShare
	res.Labor = laborAmt
	b.mustPost(ledger.ProfitAllocateSplit(
		i, res.Retain, res.Gov, owner, res.Owner, laborAmt))
	return res
}

// ===== ⑧ 新建营运本金（唯一的货币注入）=====

// NewCapital 注入新建建筑的营运本金（§4.3）。
//
// 这是系统里【唯一】允许的货币创造，故走 PostInjection 而不是 Post，
// 并累计到 injected 供守恒审计核对。
func (b *Book) NewCapital(i int, amount float64) {
	if amount <= 0 {
		return
	}
	t := &ledger.Txn{Name: "新建营运本金（货币注入）"}
	t.Credit(ledger.Building(i), amount)
	b.Aud.PostInjection(t)
	b.injected += amount
}

// MintMoney 是**中央银行的造币腿**（1.2 §1.2-5）。
//
//	贷 中央银行现金池   amount          （没有借方 —— 这是**货币创造**）
//
// 【为什么走 PostInjection】1.2 的央行按裁决"购买金矿生产货币"——
// 这是**货币创造**，与 §4.3 的"新建营运本金"同族，是本系统允许的**第二处**注入。
// 必须走 `PostInjection` 并累计到 `injected`；否则 A8 的恒等式
//
//	ΔM == NewCapital + InfusionTotal
//
// 会报一个恰等于造币额的**假残差**。
func (b *Book) MintMoney(amount float64) {
	if amount <= 0 {
		return
	}
	t := &ledger.Txn{Name: "中央银行造币（货币注入）"}
	t.Credit(ledger.CentralBank(), amount)
	b.Aud.PostInjection(t)
	b.injected += amount
}

// PayForGold 是**央行购金付款**（1.2 §1.2-5）。
//
//	借 中央银行现金池   amount
//	贷 金矿营运现金池   amount
//
// 它是普通转移（不是创造），故走 `mustPost`。
// `mineIdx` 由调用方查得——**不写死下标**，因为金矿的位置取决于追加顺序。
func (b *Book) PayForGold(mineIdx int, amount float64) {
	if amount <= 0 || mineIdx < 0 {
		return
	}
	t := &ledger.Txn{Name: "央行购金付款"}
	t.Debit(ledger.CentralBank(), amount)
	t.Credit(ledger.Building(mineIdx), amount)
	b.mustPost(t)
}

// PayGovDebtInterest 过账"政府债务利息 → 中央银行"（1.2 M5 ①）。
//
// 与 `MintMoney` 相反：它**不创造货币**（贷方是央行、借方是政府），
// 只是把政府的债务成本转成央行的现金 ⇒ 走 `mustPost`（借贷相等）。
func (b *Book) PayGovDebtInterest(amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.GovDebtInterest(amount))
	return amount
}

// mustPost 过账；借贷不等时 panic。
//
// 本文件内构造的交易都已按定义保证相等，若仍不等说明是代码 bug，
// 必须立刻暴露而不是留下一个要追的残差。
func (b *Book) mustPost(t *ledger.Txn) {
	if err := b.Aud.Post(t); err != nil {
		panic(fmt.Sprintf("book: %v", err))
	}
}
