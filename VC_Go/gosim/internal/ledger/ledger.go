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
// 账户共 16 个（§4.5.1b 修订：新增与政府/资本/建筑/人群并列的**投资池**）：
//
//	政府现金池      Gov
//	资本现金池      Capital            （金融区持有，§4.5.2）
//	投资池          Investment         （投资资金，§4.5.1b）
//	建筑现金池 ×12   Building[i]        （i = 0..12，含金融区与宅邸庄园）
//	人群现金池 ×N    Household[场地][阶级]
//
// 交易类型与借贷方向（借 = 余额减少，贷 = 余额增加）：
//
//	① 工资         借 建筑[i]（金融区取 Capital）  贷 人群[i][各阶级]
//	② 消费         借 人群[池]                   贷 建筑[产出该商品的卖方 / 宅邸庄园]
//	                                             贷 政府（税额）
//	③ 中间投入     借 建筑[买方]                 贷 建筑[卖方]
//	                                             贷 政府（税额）
//	④ 政府采购建造力 借 政府                      贷 建筑[建造力]
//	                                             ※按需即买即用、不计税（§4.5.3 G2）
//	⑤ 投资池偿还     借 投资池                    贷 政府
//	                                             ※G6：政府净支出为 0，无自反税
//	⑥ 利润归属       借 建筑[i]                  贷 建筑[i]补足 / 政府 / 所属资本建筑
//	                                             三者之和恒等于纯利（§4.5.1，无留存比例）
//	⑦ 资本建筑入池   借 资本建筑账户               贷 投资池
//	                                             ※庄园用 Building[Manor]，金融区用 Capital
//	⑧ 新建营运本金   借 （无）                    贷 建筑[i]
//	                                             ※唯一合法的货币注入
//
// §4.5.3 修订后**不存在"政府自己收自己"的自反税腿**：建造力交易不计税
// （G2 采购与 G6 偿还都按净额成交），故政府买建造力、再收投资池的偿还，
// 净支出恒为 0。历史上省掉自反税腿造成的 Net·t 残差因此从结构上消失。
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

// Epsilon 是借贷相等的**绝对**容差下限。
//
// 取 1e-6 元：远小于任何有经济意义的金额（最小货币单位是"元"），
// 又足以吸收小额交易的浮点累加误差。超差即报错，不做静默修补。
//
// 【2026-09-19 第 15 轮补：量级相关的相对容差】经济一旦真实扩张，单笔中间投入
// 会到 1e9 元量级，此时双精度累加误差本身就可达 1e-6 元（相对 1e-15），
// 被绝对容差判为"借贷不相等"并 panic（实测借 3816197311.827538 −
// 贷 3816197311.827536 = 1e-6）。故实际容差取
// `max(Epsilon, 1e-12 × 金额量级)`：在 1e9 量级上等于 1e-3 元，
// 仍远小于任何真实记账差错（真实差错是"漏一整条腿"，量级为元~万元）。
const Epsilon = 1e-6

// RelativeEpsilon 是相对容差（1e-12 = 双精度有效位的两个数量级余量）。
const RelativeEpsilon = 1e-12

// toleranceFor 返回给定金额量级下的借贷相等容差。
func toleranceFor(debits, credits float64) float64 {
	scale := math.Max(math.Abs(debits), math.Abs(credits))
	tol := RelativeEpsilon * scale
	if tol < Epsilon {
		tol = Epsilon
	}
	return tol
}

// AccountKind 是账户类别。
type AccountKind int

const (
	// KindGovernment 是政府现金池。
	KindGovernment AccountKind = iota
	// KindCapital 是资本（金融区）现金池。
	//
	// 【§4.5.1b 口径】它同时是金融区这个"资本建筑"的营运现金池：
	// 金融区的工资从这里支付，其私人份额纯利也记入这里（§4.5.2）。
	KindCapital
	// KindInvestment 是投资池（§4.5.1b）。
	//
	// 它是与政府/资本/建筑/人群并列的第 5 类账户：两个资本建筑（宅邸庄园、
	// 金融区）扣除自身工资后的纯利全额入池，再按累计贡献 K_m : K_f 分配给
	// 两条投资栈（庄园栈 / 金融栈），由投资池向政府偿还建造力货款（§4.5.3 G6）。
	KindInvestment
	// KindBuilding 是建筑现金池，Index 为建筑类别下标。
	//
	// 宅邸庄园（§4.5.5）也用它：庄园的销售收入、私人份额纯利与工资走同一个池，
	// 即 ledger.Building(model.ManorIndex)——不另开账户类别。
	KindBuilding
	// KindHousehold 是人群现金池，Index 由 HouseholdIndex 编码。
	KindHousehold
	// KindSavings 是**居民储蓄固定账户**（§5.3，2026-09-19 第 20 轮）。
	//
	// 【为什么单独开一个账户，而不是直接留在人群池里】
	// 第 20 轮裁决原文：「工资结余当前计入某固定账户，随后每周期全部转移入投资池，
	// 维护货币循环，但记账」。
	//
	// 于是结余**不再沉积在人群池**，而是每 tick 走两步两笔借贷相等的交易：
	//
	//	① 借 人群池[p]  结余     贷 储蓄账户  结余     （结余离开居民钱包）
	//	② 借 储蓄账户   σ·结余   贷 投资池    σ·结余   （按储蓄率转入投资，σ=1 全额）
	//
	// 账户本身**不留钱**（σ=1 时每 tick 归零），它的作用是让"结余 → 投资"
	// 这笔搬运在账本上有一个**具名的过手方**：任何一期都能读出"本期结余多少、
	// 其中多少转成了投资、多少仍挂在储蓄账户"，而不再需要从人群池余额反推。
	//
	// 排序：它放在 KindHousehold **之后**，以保证 Household/Investment 等既有
	// 账户的常量值不变（审计证据与快照的可比性）。
	KindSavings
)

// Account 定位一个账户。
type Account struct {
	Kind  AccountKind
	Index int
}

// 构造账户的便捷函数。
func Gov() Account           { return Account{Kind: KindGovernment} }
func Capital() Account       { return Account{Kind: KindCapital} }
func Investment() Account    { return Account{Kind: KindInvestment} }
func Savings() Account       { return Account{Kind: KindSavings} }
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
	case KindInvestment:
		return "投资池"
	case KindBuilding:
		return fmt.Sprintf("建筑[%d]", a.Index)
	case KindHousehold:
		return fmt.Sprintf("人群[%d]", a.Index)
	case KindSavings:
		return "居民储蓄"
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

// Balanced 返回借贷是否相等（按金额量级取相对容差）。
func (t *Txn) Balanced() bool {
	return math.Abs(t.SumDebits()-t.SumCredits()) <= toleranceFor(t.SumDebits(), t.SumCredits())
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
// 余额本身按账户存放；五类"池"（政府 / 资本 / 投资池 / 建筑 / 人群）只是同一张
// 余额表上的不同账户，因此"五池之和 = 货币总量"是表的定义，不需要额外断言。
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
	tol := toleranceFor(t.SumDebits(), t.SumCredits())
	if d := math.Abs(t.Net()); d > tol {
		msg := fmt.Sprintf("%s: 借贷不相等，借 %.6f − 贷 %.6f = %.6f（容差 %.1e）",
			t.Name, t.SumDebits(), t.SumCredits(), t.Net(), tol)
		a.violations = append(a.violations, msg)
		return fmt.Errorf("ledger: %s", msg)
	}
	// 【把容差内的浮点残差吸收掉（2026-09-19 第 16 轮）】
	//
	// 大额交易（1e9~1e12 元）的借贷两侧由不同的浮点累加路径得到，差值可达
	// ~1e-12 相对量级。若原样过账，这笔极小残差会**逐 tick 累积**：
	// 实测 10,000 tick 的 §8.4 A8 逐 tick 残差最大 3.05e-02 元。
	// 故在容差内把残差并入最大的一笔贷方分录，使每笔交易的 Σ借 == Σ贷 **精确成立**。
	// 真实差错（漏一整条腿，量级为元~万元）远大于容差，仍会被上面的检查抓住。
	if net := t.Net(); net != 0 {
		if len(t.Credits) > 0 {
			k, mx := 0, 0.0
			for i, e := range t.Credits {
				if v := math.Abs(e.Amount); v > mx {
					k, mx = i, v
				}
			}
			t.Credits[k].Amount += net
		} else if len(t.Debits) > 0 {
			k, mx := 0, 0.0
			for i, e := range t.Debits {
				if v := math.Abs(e.Amount); v > mx {
					k, mx = i, v
				}
			}
			t.Debits[k].Amount -= net
		}
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
