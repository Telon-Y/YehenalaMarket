package sim

// 【R73 诊断】金融区为什么**永远还不了款**？
//
// R70/R71 实测：无论放贷口径如何，`DebtPaidTotal()` 恒为 **0**。
// 原因是 `serviceDebt` 的"可用额 = 金融区本期净额"，而该净额
// `financeNet = capitalIncome − wages[金融区]` 长期为负。
//
// 本探针把 `Cap.OperatingProfit`（= capitalIncome）与 `Cap.WageBill`
// （= 金融区工资）逐 tick 打印，定位"是收入太少，还是工资太高"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestFinanceRepayProbe -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/model"
)

func TestFinanceRepayProbe(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		BankEnabled:          true,
		LoanFromBalance:      true,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	fmt.Printf("%6s %8s %10s %14s %14s %16s %14s %12s\n",
		"tick", "金融区级", "金融区人", "资本收入", "金融区工资", "净额(=可还额)", "应付", "实付")
	var totalPaid float64
	for i := 0; i < 800; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		totalPaid += st.Cap.DebtPaid
		if st.Tick%100 != 0 && st.Tick != 1 {
			continue
		}
		fb := &st.Buildings[model.FinanceIndex]
		workers := fb.Level * fb.HireRate * fb.Spec.LaborPerLevel
		fmt.Printf("%6d %8.2f %10.1f %14.2f %14.2f %16.2f %14.2f %12.2f\n",
			st.Tick, fb.Level, workers,
			st.Cap.OperatingProfit, st.Cap.WageBill,
			st.Cap.OperatingProfit-st.Cap.WageBill,
			st.Cap.DebtServiceDue, st.Cap.DebtPaid)
	}
	fmt.Printf("\n800 tick 累计实付 = %.2f；未偿 = %.2f（贷款 %d 笔）\n",
		totalPaid, st.Cap.DebtOutstandingTotal(), len(st.Cap.Debt))
	fmt.Printf("金融区级数 = %.4f、工资/级 = %.2f、资本收入/级 = %.2f\n",
		st.Buildings[model.FinanceIndex].Level,
		st.Cap.WageBill/maxf(st.Buildings[model.FinanceIndex].Level, 1e-9),
		st.Cap.OperatingProfit/maxf(st.Buildings[model.FinanceIndex].Level, 1e-9))
}
