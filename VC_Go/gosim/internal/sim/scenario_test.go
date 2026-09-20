package sim

// scenario_test.go —— 显式的测试场景注入
//
// ============================ 纪律 ============================
//
// 【严禁不明示修改数据】。
//
// 测试里为了隔离待验证的机制，常常需要构造受控场景（给资本池注资、
// 调整触发阈值、放宽步长等）。这类改动【必须显式声明】，否则：
//
//   - 读代码的人无法区分"模型本来的行为"与"测试注入的结果"；
//   - 一个由注入造成的通过，会被误读成机制的通过；
//   - 注入的数值散落在各处 SetBalance / Params.X = ...，无人能汇总核对。
//
// 本文件提供 Scenario：每笔注入都必须带【字段 + 理由】，由 Apply 统一施加，
// 并在施加前/后各读一次真实值，把"旧值 → 新值"逐条打印到测试日志。
// 因此带注入的测试，输出里必然出现 "[场景注入]" 清单；
// 读输出的人不可能看不到"哪些数据被改过、从多少改成多少、为什么"。
//
// 在测试里直接写 SetBalance / Params.X = ... 在本文件的纪律下是禁止的。

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

// Injection 是一笔显式的数据注入。
type Injection struct {
	// Field 是被修改的字段路径。
	Field string
	// Before 是施加前的实际值（运行时读取，不是手写）。
	Before string
	// After 是施加后的实际值（运行时读取）。
	After string
	// Reason 是为什么必须注入。
	Reason string
}

// Scenario 收集一次测试的全部注入，并负责施加与打印。
type Scenario struct {
	t       *testing.T
	Title   string
	entries []scenarioEntry
	// restoreSwitches 由 OnlyPrivatizable 记录，供 Restore 还原。
	restoreSwitches []bool
	// restoreSubsidy 由 SubsidyOnly 记录，供 Restore 还原（§4.5.7）。
	restoreSubsidy []bool
}

type scenarioEntry struct {
	field  string
	reason string
	// snapshot 在施加【之前】执行，返回可读的旧值。
	snapshot func(*State) string
	// apply 执行注入。
	apply func(*State)
}

// newScenario 开启一个场景。
func newScenario(t *testing.T, title string) *Scenario {
	t.Helper()
	return &Scenario{t: t, Title: title}
}

// Param 注入一个 Params 字段。
//
// get/set 必须指向同一字段，这样 Before/After 都是运行时真实读取的值。
func (s *Scenario) Param(
	field string,
	get func(*State) float64,
	set func(*State, float64),
	to float64,
	reason string,
) *Scenario {
	s.entries = append(s.entries, scenarioEntry{
		field:    "Params." + field,
		reason:   reason,
		snapshot: func(st *State) string { return fmt.Sprintf("%g", get(st)) },
		apply: func(st *State) {
			set(st, to)
		},
	})
	return s
}

// CapitalTo 把资本池余额设为绝对值。
func (s *Scenario) CapitalTo(to float64, reason string) *Scenario {
	return s.capital(fmt.Sprintf("%g", to), func(st *State) {
		st.Aud.SetBalance(ledger.Capital(), to)
	}, reason)
}

// CapitalAdd 给资本池追加资金。
func (s *Scenario) CapitalAdd(delta float64, reason string) *Scenario {
	return s.capital("现有余额 + "+fmt.Sprintf("%g", delta), func(st *State) {
		st.Aud.SetBalance(ledger.Capital(), st.balCap()+delta)
	}, reason)
}

func (s *Scenario) capital(to string, apply func(*State), reason string) *Scenario {
	s.entries = append(s.entries, scenarioEntry{
		field:    "资本池余额（ledger.Capital()）",
		reason:   reason + "（目标：" + to + "）",
		snapshot: func(st *State) string { return fmt.Sprintf("%g", st.balCap()) },
		apply:    apply,
	})
	return s
}

// InvestmentAdd 给**投资池**追加资金（§4.5.1b）。
//
// 用途：默认参数下两个资本建筑（宅邸庄园 / 金融区）扣除自身工资后的净额均为负，
// 故投资池恒为 0、两条投资栈永远无预算、建造队列恒空（这是 R26 新资金口径的
// 直接后果，不是实现缺陷）。要单独检验"队列 / 采购 / 完工归属 / 两条栈"这些
// 机制本身，必须先隔离资金约束——本注入就是那笔被显式声明的隔离资金。
//
// 【纪律】与 CapitalAdd 同理：每笔注入都带字段与理由，并由 Apply 打印"旧值 → 新值"。
func (s *Scenario) InvestmentAdd(delta float64, reason string) *Scenario {
	s.entries = append(s.entries, scenarioEntry{
		field:  "投资池余额（ledger.Investment()）",
		reason: reason + "（目标：现有余额 + " + fmt.Sprintf("%g", delta) + "）",
		snapshot: func(st *State) string {
			return fmt.Sprintf("%g", st.balInvest())
		},
		apply: func(st *State) {
			st.Aud.SetBalance(ledger.Investment(), st.balInvest()+delta)
		},
	})
	return s
}

// OnlyPrivatizable 把【除 target 之外】的所有建筑的 AllowPrivatize 设为 false。
//
// 用途：把机制的观察面收窄到单一建筑，从而能用单笔金额反解对价公式。
// 这是一笔真实的数据注入，故同样登记、打印。
//
// 返回恢复函数；测试应当 defer 调用它，把开关还原——
// 避免注入泄漏到同一 State 的后续断言（这本身也是"不明示修改"的一种）。
func (s *Scenario) OnlyPrivatizable(target int, reason string) func() {
	var saved []bool
	s.entries = append(s.entries, scenarioEntry{
		field:  fmt.Sprintf("全部建筑的 Spec.AllowPrivatize（仅保留 [%d]）", target),
		reason: reason,
		snapshot: func(st *State) string {
			var n int
			for i := range st.Buildings {
				if st.Buildings[i].Spec.AllowPrivatize {
					n++
				}
			}
			return fmt.Sprintf("开启数 = %d", n)
		},
		apply: func(st *State) {
			saved = make([]bool, len(st.Buildings))
			for i := range st.Buildings {
				saved[i] = st.Buildings[i].Spec.AllowPrivatize
				st.Buildings[i].Spec.AllowPrivatize = (i == target)
			}
		},
	})
	return func() {
		if saved == nil {
			return
		}
		// 恢复需要一个 State；由调用方通过 Restore 传入。
		s.restoreSwitches = saved
	}
}

// Restore 还原 OnlyPrivatizable / SubsidyOnly 记录的原开关值。
func (s *Scenario) Restore(st *State) {
	if s.restoreSwitches == nil && s.restoreSubsidy == nil {
		return
	}
	for i := range st.Buildings {
		if i < len(s.restoreSwitches) {
			st.Buildings[i].Spec.AllowPrivatize = s.restoreSwitches[i]
		}
		if i < len(s.restoreSubsidy) {
			st.Buildings[i].Spec.AllowSubsidy = s.restoreSubsidy[i]
		}
	}
	s.t.Logf("[场景注入] 已还原 %d 类建筑的 AllowPrivatize / %d 类建筑的 AllowSubsidy 原值",
		len(s.restoreSwitches), len(s.restoreSubsidy))
	s.restoreSwitches = nil
	s.restoreSubsidy = nil
}

// SubsidyOnly 把【除 target 之外】的所有建筑的 AllowSubsidy 设为 false（§4.5.7）。
//
// 用途与 OnlyPrivatizable 同构：把补贴的观察面收窄到单一建筑，
// 使"补贴入账"与"该建筑是否被扩建"能够一一对应。
func (s *Scenario) SubsidyOnly(target int, reason string) func() {
	var saved []bool
	s.entries = append(s.entries, scenarioEntry{
		field:  fmt.Sprintf("全部建筑的 Spec.AllowSubsidy（仅保留 [%d]）", target),
		reason: reason,
		snapshot: func(st *State) string {
			var n int
			for i := range st.Buildings {
				if st.Buildings[i].Spec.AllowSubsidy {
					n++
				}
			}
			return fmt.Sprintf("开启数 = %d", n)
		},
		apply: func(st *State) {
			saved = make([]bool, len(st.Buildings))
			for i := range st.Buildings {
				saved[i] = st.Buildings[i].Spec.AllowSubsidy
				st.Buildings[i].Spec.AllowSubsidy = (i == target)
			}
		},
	})
	return func() {
		if saved != nil {
			s.restoreSubsidy = saved
		}
	}
}

// Apply 施加全部注入，并把"字段：旧值 → 新值（理由）"逐条打印。
func (s *Scenario) Apply(st *State) {
	s.t.Helper()
	logged := make([]Injection, 0, len(s.entries))
	for _, e := range s.entries {
		before := e.snapshot(st)
		e.apply(st)
		after := e.snapshot(st)
		logged = append(logged, Injection{
			Field:  e.field,
			Before: before,
			After:  after,
			Reason: e.reason,
		})
	}
	s.t.Logf("[场景注入] %s —— 共 %d 笔显式注入：", s.Title, len(logged))
	for i, inj := range logged {
		s.t.Logf("  %d) %s: %s → %s   【理由】%s",
			i+1, inj.Field, inj.Before, inj.After, inj.Reason)
	}
	// 注入后立刻核对货币守恒：注入本身会改变总量，故这里只记录，
	// 不做断言——断言留给测试正文，避免把"注入造成的 Δ"误判为缺陷。
	s.t.Logf("  （注入后货币总量 = %.0f；注入会改变该值，守恒断言须扣除注入额）",
		st.TotalMoney())
}
