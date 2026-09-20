package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAuditDebtLedgerIsInert 证明 R70/R71 里"未偿负债滚到 33 亿"在本参数下
// **对经济没有任何影响** —— 债务台账是纯记账，不是真实约束。
//
// 【为什么这是个必须钉住的结论】
// R70/R71 观察到 `DebtPaidTotal() == 0`、未偿从 750 万滚到 33 亿，看起来像灾难。
// R73 的探针定位到根因：`serviceDebt` 的可用额是
// `financeNet = capitalIncome − 金融区工资`，而 **资本收入恒为负**
// （资本按 §4.5.1 承担生产建筑亏损的 70% 份额，实测 −1.36e6 ~ −3.28e6/tick），
// 而金融区工资只有 1.6e4~2.8e4/tick ⇒ 净额恒为负 ⇒ **可还额恒为 0**。
//
// 【本测试如何证明"惰性"】把年化利率从 5% 降到 1e-9（≈0）：
// 若负债服务真的在经济中起作用，两者必然分叉；若结果**逐位相同**，
// 则说明"负债服务"这条路径在当前参数下从未真正执行过。
//
// 【结论的含义】
//   - 起作用的是**放贷通道**（储蓄银行 → 投资池），它把投资池从 541 万抬到 18.5 亿；
//   - 不起作用的是**债务台账**（利息/还本/违约延期）——它只是"记着"；
//   - ⇒ M8 的利息分配（`SavingsBankToLabor`）因此**永不触发**，
//     劳动力拿不到任何利息收入（1.2 M8.6 ③ 在当前参数下是死代码）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditDebtLedgerIsInert -v
func TestAuditDebtLedgerIsInert(t *testing.T) {
	auditEnabled(t)
	run := func(rate float64, interval int, ticks int) *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			BankEnabled:          true,
			LoanFromBalance:      true,
			LoanAnnualRate:       rate,
			LoanIssueInterval:    interval,
		})
		if err != nil {
			t.Fatalf("New(rate=%g): %v", rate, err)
		}
		for i := 0; i < ticks; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
			if st.InvariantErr != nil {
				t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
			}
		}
		return st
	}
	const ticks = 800
	hi := run(0.05, 52, ticks)       // 裁决的 5% 年化
	lo := run(1e-9, 52, ticks)       // ≈0%（负债服务惰性）
	none := run(0.05, 100000, ticks) // 完全不放贷

	t.Logf("%d tick：\n"+
		"  5%%  年化 : 投资池 %16.2f 人口 %.0f 未偿 %16.2f 累计实付 %.2f\n"+
		"  ≈0%% 年化 : 投资池 %16.2f 人口 %.0f 未偿 %16.2f 累计实付 %.2f\n"+
		"  不放贷   : 投资池 %16.2f 人口 %.0f 未偿 %16.2f 累计实付 %.2f",
		ticks,
		hi.InvestmentPool(), hi.Population, hi.Cap.DebtOutstandingTotal(), hi.Cap.DebtPaidTotal(),
		lo.InvestmentPool(), lo.Population, lo.Cap.DebtOutstandingTotal(), lo.Cap.DebtPaidTotal(),
		none.InvestmentPool(), none.Population, none.Cap.DebtOutstandingTotal(), none.Cap.DebtPaidTotal())

	// ① 实付恒为 0 —— 这正是"可还额恒为 0"的直接后果
	if hi.Cap.DebtPaidTotal() > 1e-6 {
		t.Errorf("5%% 年化下累计实付 %.6f ≠ 0 —— R73 的\"资本收入恒为负\"结论被推翻，"+
			"本测试的前提不再成立", hi.Cap.DebtPaidTotal())
	}
	// ② 负债确实在滚（台账非空，说明它"记着"）
	if hi.Cap.DebtOutstandingTotal() <= 0 {
		t.Errorf("未偿负债 %.2f ≤ 0 —— 贷款台账未生效", hi.Cap.DebtOutstandingTotal())
	}
	// ③ 核心断言：改利率不改变任何经济量 ⇒ 债务台账是惰性的
	//
	// 用 1e-4 相对容差而非逐位比较：R72 登记的记账层有 ~1e-7 的运行间噪声，
	// 但本断言针对的是"利率 5% 与 0% 的差别"，量级远大于该噪声。
	for _, c := range []struct {
		name string
		a, b float64
	}{
		{"投资池", hi.InvestmentPool(), lo.InvestmentPool()},
		{"人口", hi.Population, lo.Population},
		{"政府现金池", hi.Aud.Balance(ledger.Gov()), lo.Aud.Balance(ledger.Gov())},
		{"资本现金池", hi.Aud.Balance(ledger.Capital()), lo.Aud.Balance(ledger.Capital())},
	} {
		tol := 1e-4 * math.Max(1, math.Abs(c.a))
		if d := math.Abs(c.a - c.b); d > tol {
			t.Errorf("改利率（5%% → ≈0%%）后 %s 变了：%.6f → %.6f（差 %.6g）—— "+
				"说明债务服务确实影响了经济，本测试的\"惰性\"结论不成立",
				c.name, c.a, c.b, d)
		}
	}
	// ④ 对照：**放贷通道**是有效的（不放贷时投资池塌掉）
	if none.InvestmentPool() >= 0.5*hi.InvestmentPool() {
		t.Errorf("不放贷时投资池 %.2f 仍达放贷局的 %.1f%% —— "+
			"放贷通道的有效性无法确立", none.InvestmentPool(),
			100*none.InvestmentPool()/math.Max(hi.InvestmentPool(), 1))
	}
}
