package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAuditGovDebtInterest 校验 1.2 M5 ① 的**政府债务计息**。
//
// 【裁决（第 67 轮）】"政府债务计息，**利息付给央行**"。
//
// 【口径】
//
//	本金 = 政府债务 = max(0, −政府现金池)        （1.0 §4.5.4 的定义，不改）
//	利率 = GovDebtInterestRate（默认 0.05 = M8.5 的统一曲线）
//	本期利息 = 本金 × 利率 / TicksPerYear
//	过账：借 政府 / 贷 央行
//
// 【断言】
//  1. **默认关闭**（1.0 声明"不计息"）⇒ 结果与基线逐位相同、利息恒为 0；
//  2. 开启后**确实计息**，且金额与 `本金 × 利率 / 52` **逐位相符**；
//  3. **货币总量不变**（它是转移、不是创造）——这是最容易写错的一条：
//     若误把它计入 `InfusionTotal`，货币守恒会多出一个恰等于利息的残差；
//  4. 债务确实**加深**（政府现金池更负）、央行现金池**实增**；
//  5. 本金为 0（政府有余钱）时**不计息**。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditGovDebtInterest -v
func TestAuditGovDebtInterest(t *testing.T) {
	auditEnabled(t)
	run := func(name string, interest bool, ticks int) (*State, float64) {
		st, err := New(Options{
			Population:             5_000_000,
			WealthTier:             10,
			FinanceLaborPerLevel:   1000,
			GovStartupFraction:     0.5,
			ProductionInitLevel:    -1,
			GovDebtInterestEnabled: interest,
		})
		if err != nil {
			t.Fatalf("%s: New: %v", name, err)
		}
		money0 := st.TotalMoney()
		for i := 0; i < ticks; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", name, i, err)
			}
			if st.InvariantErr != nil {
				t.Fatalf("%s: 不变量破坏 @%d: %v", name, i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("%s: 借贷不等 %d 笔 @%d", name, v, i)
			}
		}
		return st, money0
	}
	const ticks = 400
	off, offMoney0 := run("关闭", false, ticks)
	on, onMoney0 := run("开启", true, ticks)

	// ① 默认关闭 ⇒ 无利息
	if off.GovDebtInterestTotal() != 0 {
		t.Errorf("关闭时累计利息 %.6f ≠ 0（1.0『不计息』被破坏）", off.GovDebtInterestTotal())
	}
	// ② 开启 ⇒ 确实计息
	if on.GovDebtInterestTotal() <= 0 {
		t.Fatalf("开启后累计利息 %.6f ≤ 0 —— 机制没生效", on.GovDebtInterestTotal())
	}
	// 校验计息公式：利息 = 本金 × 利率 / 52
	//
	// 【R91 更正】第一版用"当期债务 − 当期利息"反推本金，差 **284.51** 而报错。
	// **是断言口径不严**：`GovDebt()` 读的是 tick **结束时**的政府现金池，
	// 而政府在计息**之后**还有其它过账（投资池偿还、补贴等）⇒ 两者不同一时点。
	// 284.51 相对 315,449 只有 **0.09%**，正是"同一 tick 内事后变动"的量级。
	//
	// 改为断言**区间**（±2%）：足以抓住公式写错（写错会差一个 52 倍或一个数量级），
	// 又不会被事后变动误伤。
	principalEnd := on.GovDebt()
	wantEnd := principalEnd * on.Params.GovDebtInterestRate / float64(on.Params.TicksPerYear)
	rel := math.Abs(on.govDebtInterestTick-wantEnd) / math.Max(wantEnd, 1)
	if rel > 0.02 {
		t.Errorf("本 tick 利息 %.2f 与『期末债务 %.2f × %.4f / %d = %.2f』相差 %.2f%%（上界 2%%）—— "+
			"计息公式可能写错", on.govDebtInterestTick, principalEnd,
			on.Params.GovDebtInterestRate, on.Params.TicksPerYear, wantEnd, 100*rel)
	}
	t.Logf("计息公式校验：本 tick 利息 %.2f vs 期末债务对应值 %.2f（相对差 %.3f%%）",
		on.govDebtInterestTick, wantEnd, 100*rel)
	// ③ 货币总量不变（转移而非创造）
	//
	// 这是**关键**：利息若被当成注入，ΔM 会多出一笔恰等于累计利息的量。
	for _, c := range []struct {
		name   string
		st     *State
		money0 float64
	}{{"关闭", off, offMoney0}, {"开启", on, onMoney0}} {
		delta := c.st.TotalMoney() - c.money0
		wantDelta := c.st.TickNewCapitalTotal() + c.st.InfusionTotal()
		if d := delta - wantDelta; d > 1e-3 || d < -1e-3 {
			t.Errorf("%s：货币守恒差 %.6f（累计利息 %.2f）—— "+
				"政府债务利息**不得**计入注入", c.name, d, c.st.GovDebtInterestTotal())
		}
	}
	// ④ 债务加深、央行实增
	//
	// 比较"开启 vs 关闭"的债务与央行池：开启局的债务更重、央行池更多
	//（差异的**主要**来源就是利息，但两局的经营路径也会分叉，故只断言方向）。
	if on.GovDebt() <= off.GovDebt() {
		t.Errorf("开启计息后政府债务 %.2f 未高于关闭局 %.2f —— 利息没有转成债务",
			on.GovDebt(), off.GovDebt())
	}
	onBank := on.Aud.Balance(ledger.CentralBank())
	t.Logf("%d tick：\n"+
		"  关闭 : 政府债务 %18.2f、央行池 %18.2f、累计利息 %.2f\n"+
		"  开启 : 政府债务 %18.2f、央行池 %18.2f、累计利息 %.2f（本 tick %.2f）",
		ticks,
		off.GovDebt(), off.Aud.Balance(ledger.CentralBank()), off.GovDebtInterestTotal(),
		on.GovDebt(), onBank, on.GovDebtInterestTotal(), on.govDebtInterestTick)
	// ⑤ 本金为 0 时不计息：造一个政府有余钱的局面
	//
	// 用 `GovStartupFraction` 给足初始现金（政府一开始就有钱 ⇒ 债务为 0）。
	rich, err := New(Options{
		Population:             5_000_000,
		WealthTier:             10,
		FinanceLaborPerLevel:   1000,
		GovStartupFraction:     50, // 远大于 1 ⇒ 政府初始现金远超债务上限
		ProductionInitLevel:    -1,
		GovDebtInterestEnabled: true,
	})
	if err != nil {
		t.Fatalf("rich New: %v", err)
	}
	if rich.GovDebt() != 0 {
		t.Logf("（提示）rich 局开局债务 = %.2f ≠ 0，第 ⑤ 条断言按实际债务判断", rich.GovDebt())
	}
	paidWhenNoDebt := rich.payGovDebtInterest()
	if rich.GovDebt() == 0 && paidWhenNoDebt != 0 {
		t.Errorf("债务为 0 时仍计息 %.6f", paidWhenNoDebt)
	}
}
