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
//	book    提供语义：工资 / 消费 / 中间投入 / 购力 / 售力 / 利润划分 / 资本注入
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

func (b *Book) Gov() ledger.Account     { return ledger.Gov() }
func (b *Book) Capital() ledger.Account { return ledger.Capital() }
func (b *Book) Bld(i int) ledger.Account {
	return ledger.Building(i)
}

// House 返回（场地, 阶级）对应的人群账户。
func (b *Book) House(site, class int) ledger.Account {
	return ledger.Household(site, class, b.Classes)
}

// ===== 余额读取 =====

func (b *Book) Bal(a ledger.Account) float64 { return b.Aud.Balance(a) }

// BalGov / BalCapital / BalBld / BalHouse 是常用的便捷读取。
func (b *Book) BalGov() float64     { return b.Aud.Balance(ledger.Gov()) }
func (b *Book) BalCapital() float64 { return b.Aud.Balance(ledger.Capital()) }
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

// ===== ⑦ 利润划分 =====

// ProfitSplit 按 §4.5.1 把一笔运营纯利【划分】给三个归属。
//
//	借 建筑[i]      利润总额
//	贷 政府         政府份额
//	贷 资本         资本份额
//	贷 建筑[i]      留存份额
//
// 【借贷相等的充要条件】三份额之和恒等于利润。本方法自行按
// govShare / retainRatio 计算三份额，调用方只提供这两个比例，
// 因此"忘了把某一份额算进去"在结构上不可能发生。
//
// 返回实际入账的三份额，供调用方登记诊断字段。
func (b *Book) ProfitSplit(i int, profit, govShare, retainRatio float64) (gov, capital, retain float64) {
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

	t := &ledger.Txn{Name: "利润划分"}
	t.Debit(ledger.Building(i), gov+capital+retain)
	t.Credit(ledger.Gov(), gov)
	t.Credit(ledger.Capital(), capital)
	t.Credit(ledger.Building(i), retain)
	b.mustPost(t)
	return
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

// mustPost 过账；借贷不等时 panic。
//
// 本文件内构造的交易都已按定义保证相等，若仍不等说明是代码 bug，
// 必须立刻暴露而不是留下一个要追的残差。
func (b *Book) mustPost(t *ledger.Txn) {
	if err := b.Aud.Post(t); err != nil {
		panic(fmt.Sprintf("book: %v", err))
	}
}
