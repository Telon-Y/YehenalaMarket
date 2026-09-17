// Package ledger 实现 1.0 契约的【唯一记账入口】。
//
// ============================ 为什么需要它 ============================
//
// 修订前，"钱从哪个池扣、记到哪个池"散落在 sim / fiscal / consume / cohort
// 四个包的十几个 Cash.Add 调用点上，各写各的。后果是货币守恒只能靠【事后审计】
// 发现，而审计每修一处、残差就跑到另一处——实测连续暴露了四个不同层级的缺陷：
//
//	① 工资只作为成本扣减，钱从未真正付出（居民没有账户）
//	② 利润被建筑现金池与政府池各记一次（Δ货币/Σ利润 = 2.00）
//	③ 对中间投入计征的税，政府收了但没有任何账户被扣
//	④ 政府自建项目付款只累加计数、不动任何账户余额（工程是白得的）
//
// 本包把记账收敛为一条规则：**每一笔交易都必须借贷相等**。
// Post 校验 Σ借 == Σ贷（逐位相等），不相等直接报错。
// 于是"货币守恒"不再是需要追查的目标，而是【构造性事实】：
// 任何一笔交易的净效应都为零，系统唯一的货币注入只有 NewCapital。
//
// ============================ 资金流量表 ============================
//
// 账户共 15 个：
//
//	政府现金池      Gov
//	资本现金池      Capital            （金融区持有）
//	建筑现金池 ×12   Building[i]        （i = 0..11，含金融区自身）
//	人群现金池 ×N    Household[场地][阶级]
//
// 交易类型与借贷方向（借 = 余额减少，贷 = 余额增加）：
//
//	① 工资         借 建筑[i]                    贷 人群[i][各阶级]
//	② 消费         借 人群[池]                   贷 建筑[产出该商品的卖方]
//	                                             贷 政府（税额）
//	③ 中间投入     借 建筑[买方]                 贷 建筑[卖方]
//	                                             贷 政府（税额）
//	④ 政府采购建造力 借 政府                      贷 建筑[建造力]
//	                                             贷 政府（税额）※自己收自己
//	⑤ 售力给扩建方   借 建筑[付款方]或资本        贷 政府（净额）
//	                                             贷 政府（税额）※同上
//	⑥ 政府自建付款   借 政府                      贷 建筑[建造力]
//	                                             贷 政府（税额）※同上
//	⑦ 利润划分       借 建筑[i]                  贷 政府 / 资本 / 建筑[i]留存
//	                                             三者之和恒等于利润
//	⑧ 新建营运本金   借 （无）                    贷 建筑[i]
//	                                             ※唯一合法的货币注入
//
// ④⑤⑥ 中的税额是"政府自己收自己"：资金不离开政府池，故对政府池的净影响
// 只等于净额。把它们显式写出（而不是省掉）是为了让每笔交易都满足借贷相等，
// 从而不必在别处补记任何补偿项——这正是此前反复出错的地方。
//
// ============================ 与 Cash.Add 的关系 ============================
//
// 所有账户余额的变动【必须】经过 Post。直接调用 Cash.Add 会绕过校验，
// 是本包明令禁止的；sim 包中残留的 Cash.Add 仅允许出现在 New 的初始化里。
package ledger

import (
	"fmt"
	"math"
)

// Epsilon 是借贷相等的容差。
//
// 取 1e-6 元：远小于任何有经济意义的金额（最小货币单位是"元"），
// 又足以吸收浮点累加误差。超差即报错，不做静默修补。
const Epsilon = 1e-6

// AccountKind 是账户类别。
type AccountKind int

const (
	// KindGovernment 是政府现金池。
	KindGovernment AccountKind = iota
	// KindCapital 是资本（金融区）现金池。
	KindCapital
	// KindBuilding 是建筑现金池，Index 为建筑类别下标。
	KindBuilding
	// KindHousehold 是人群现金池，Index 由 HouseholdIndex 编码。
	KindHousehold
)

// Account 定位一个账户。
type Account struct {
	Kind  AccountKind
	Index int
}

// 构造账户的便捷函数。
func Gov() Account           { return Account{Kind: KindGovernment} }
func Capital() Account       { return Account{Kind: KindCapital} }
func Building(i int) Account { return Account{Kind: KindBuilding, Index: i} }
func Household(worksite, class int, classes int) Account {
	return Account{Kind: KindHousehold, Index: worksite*classes + class}
}

// String 返回可读的账户名，用于错误信息与审计输出。
func (a Account) String() string {
	switch a.Kind {
	case KindGovernment:
		return "政府"
	case KindCapital:
		return "资本"
	case KindBuilding:
		return fmt.Sprintf("建筑[%d]", a.Index)
	case KindHousehold:
		return fmt.Sprintf("人群[%d]", a.Index)
	}
	return "未知"
}

// Entry 是一条分录。
type Entry struct {
	Account Account
	Amount  float64 // 正数表示该方向的金额
}

// Txn 是一笔交易的完整分录：借方与贷方。
//
// 借贷之和必须相等——这是本包唯一的不变量，也是货币守恒的充要条件。
type Txn struct {
	// Name 是交易类型名，用于审计与错误定位。
	Name string
	// Debits 是借方分录（余额减少）。
	Debits []Entry
	// Credits 是贷方分录（余额增加）。
	Credits []Entry
}

// Debit 追加一条借方分录并返回自身，便于链式构造。
func (t *Txn) Debit(a Account, amount float64) *Txn {
	if amount != 0 {
		t.Debits = append(t.Debits, Entry{Account: a, Amount: amount})
	}
	return t
}

// Credit 追加一条贷方分录并返回自身，便于链式构造。
func (t *Txn) Credit(a Account, amount float64) *Txn {
	if amount != 0 {
		t.Credits = append(t.Credits, Entry{Account: a, Amount: amount})
	}
	return t
}

// SumDebits / SumCredits 返回两侧合计。
func (t *Txn) SumDebits() float64  { return sum(t.Debits) }
func (t *Txn) SumCredits() float64 { return sum(t.Credits) }

// Balanced 返回借贷是否相等。
func (t *Txn) Balanced() bool {
	return math.Abs(t.SumDebits()-t.SumCredits()) <= Epsilon
}

// Net 返回交易的净额（借 − 贷）。恒等于 0 即货币守恒。
func (t *Txn) Net() float64 { return t.SumDebits() - t.SumCredits() }

func sum(es []Entry) float64 {
	var s float64
	for _, e := range es {
		s += e.Amount
	}
	return s
}

// Auditor 是记账账本。它持有全部余额，并强制每笔交易借贷相等。
//
// 余额本身按账户存放；四个"池"只是同一张余额表上的不同账户，
// 因此"四池之和 = 货币总量"是表的定义，不需要额外断言。
type Auditor struct {
	// balances 是账户余额表。用 map 是因为账户数量随建筑类别与阶级数变化，
	// 且 map 的零值即是"余额 0"，无需显式初始化。
	balances map[Account]float64

	// posted 是本 tick 已过账的交易数（诊断用）。
	posted int
	// violations 记录借贷不相等的交易（应为 0；非 0 即实现有 bug）。
	violations []string
	// unexplained 记录绕过 Post 直接改动的金额累计（应为 0）。
	unexplained float64
}

// NewAuditor 建立空账本（所有账户余额为 0）。
func NewAuditor() *Auditor {
	return &Auditor{balances: make(map[Account]float64)}
}

// Balance 返回账户余额。
func (a *Auditor) Balance(acc Account) float64 { return a.balances[acc] }

// SetBalance 设定账户初始余额。
//
// 【仅用于 New 的开局注资】。运行期一律走 Post。
// 之所以允许它存在：初始货币存量是模型的外生给定值（§4.3 修订：
// 按一周工资流量标定），不是任何交易的结果。
func (a *Auditor) SetBalance(acc Account, v float64) {
	a.balances[acc] = v
}

// Post 过账一笔交易，强制借贷相等。
//
// 返回 error 而不是静默修正：记错账必须立刻暴露，而不是留下一个
// 需要靠事后审计去追的残差。
func (a *Auditor) Post(t *Txn) error {
	if d := math.Abs(t.Net()); d > Epsilon {
		msg := fmt.Sprintf("%s: 借贷不相等，借 %.6f − 贷 %.6f = %.6f（容差 %.1e）",
			t.Name, t.SumDebits(), t.SumCredits(), t.Net(), Epsilon)
		a.violations = append(a.violations, msg)
		return fmt.Errorf("ledger: %s", msg)
	}
	for _, e := range t.Debits {
		a.balances[e.Account] -= e.Amount
	}
	for _, e := range t.Credits {
		a.balances[e.Account] += e.Amount
	}
	a.posted++
	return nil
}

// MustPost 与 Post 相同，但借贷不等时 panic。
//
// 用于"按构造必定相等"的场景（例如 Gross = Net + Tax 的拆分），
// 使错误在开发期立刻暴露而不是被吞掉。
func (a *Auditor) MustPost(t *Txn) {
	if err := a.Post(t); err != nil {
		panic(err)
	}
}

// Posted 返回已过账的交易数（诊断用）。
func (a *Auditor) Posted() int { return a.posted }

// Violations 返回全部借贷不相等记录（应为空）。
func (a *Auditor) Violations() []string { return a.violations }

// ResetTick 在每个 tick 开头清零交易计数（余额是存量，不清）。
func (a *Auditor) ResetTick() { a.posted = 0 }

// Total 返回全部账户余额之和，即全社会货币存量。
//
// 由于每笔交易净效应为零，Total 只有在 NewCapital（新建营运本金）
// 发生时才改变。这就是 §4.5.3「总流通货币不变」的直接含义。
func (a *Auditor) Total() float64 {
	var s float64
	for _, v := range a.balances {
		s += v
	}
	return s
}

// TotalOf 返回某一类账户的余额合计。
func (a *Auditor) TotalOf(kind AccountKind) float64 {
	var s float64
	for acc, v := range a.balances {
		if acc.Kind == kind {
			s += v
		}
	}
	return s
}

// Snapshot 返回余额表的副本（诊断用）。
func (a *Auditor) Snapshot() map[Account]float64 {
	out := make(map[Account]float64, len(a.balances))
	for k, v := range a.balances {
		out[k] = v
	}
	return out
}
