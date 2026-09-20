package sim

// 【R71 诊断】默认口径两次运行是否真的不一致？
//
// `TestAuditLoanFromBalance` 的第①条断言（默认口径两次运行逐位一致）失败。
// 但 Go 的 map 迭代顺序随机化是**唯一**可能的非确定源，而 `Cap.Debt` 是切片。
// 本探针把两次运行的关键量逐一打印，定位差异是"真·非确定"还是"我的断言写错了"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestDeterminismProbe -v

import (
	"fmt"
	"math"
	"testing"
)

func TestDeterminismProbe(t *testing.T) {
	auditEnabled(t)
	mk := func() *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			BankEnabled:          true,
		})
		if err != nil {
			t.Fatalf("New: %v", err)
		}
		for i := 0; i < 800; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
		}
		return st
	}
	a, b := mk(), mk()
	rows := []struct {
		name string
		av   float64
		bv   float64
	}{
		{"投资池", a.InvestmentPool(), b.InvestmentPool()},
		{"未偿负债", a.Cap.DebtOutstandingTotal(), b.Cap.DebtOutstandingTotal()},
		{"银行余额", a.SavingsBankBalance(), b.SavingsBankBalance()},
		{"人口", a.Population, b.Population},
		{"货币总量", a.TotalMoney(), b.TotalMoney()},
		{"累计实付", a.Cap.DebtPaidTotal(), b.Cap.DebtPaidTotal()},
		{"贷款笔数", float64(len(a.Cap.Debt)), float64(len(b.Cap.Debt))},
	}
	anyDiff := false
	for _, r := range rows {
		eq := r.av == r.bv
		nan := math.IsNaN(r.av) || math.IsNaN(r.bv)
		if !eq {
			anyDiff = true
		}
		fmt.Printf("%-10s A=%22.9f  B=%22.9f  差=%+.3e 相等=%v NaN=%v\n",
			r.name, r.av, r.bv, r.av-r.bv, eq, nan)
	}
	fmt.Printf("存在差异 = %v\n", anyDiff)
	// 逐笔贷款对比
	n := len(a.Cap.Debt)
	if len(b.Cap.Debt) < n {
		n = len(b.Cap.Debt)
	}
	for i := 0; i < n; i++ {
		la, lb := a.Cap.Debt[i], b.Cap.Debt[i]
		if la.Outstanding != lb.Outstanding || la.PaidPrincipal != lb.PaidPrincipal ||
			la.PaidInterest != lb.PaidInterest || la.Deferrals != lb.Deferrals {
			fmt.Printf("  贷款 %d 不同：A(out=%.6f pp=%.6f pi=%.6f def=%d) B(out=%.6f pp=%.6f pi=%.6f def=%d)\n",
				i, la.Outstanding, la.PaidPrincipal, la.PaidInterest, la.Deferrals,
				lb.Outstanding, lb.PaidPrincipal, lb.PaidInterest, lb.Deferrals)
		}
	}
}
