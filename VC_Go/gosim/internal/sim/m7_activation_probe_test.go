package sim

// 【R79 诊断②】M7 在稀缺局里**何时**才起作用？
//
// 实测（`TestAll12FlagsProbe`）：
//   稀缺 + M4.2+银行+锚余额      投资池 5,002,825,509.81
//   稀缺 **全开**（+M7）          投资池 5,183,761,744.60   ← M7 改变了 +3.6%
//
// 但 R78 的守卫在"稀缺 + 仅 M7"下报"几乎惰性"（只改 3 项，量级很小）。
// 本探针把稀缺局里的**开关组合**逐级加上，找出 M7 从"惰性"变"起作用"的**拐点**，
// 以确定它依赖的是哪一个前置条件。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestM7ActivationProbe -v

import (
	"fmt"
	"testing"
)

func TestM7ActivationProbe(t *testing.T) {
	auditEnabled(t)
	type row struct {
		name            string
		bank, loan, own bool
	}
	rows := []row{
		{"稀缺 无其他开关", false, false, false},
		{"稀缺 +银行", true, false, false},
		{"稀缺 +银行+锚余额", true, true, false},
		{"稀缺 +重构", false, false, true},
		{"稀缺 +重构+银行+锚余额", true, true, true},
	}
	run := func(r row, bid bool) (pop, invest, maxPrem float64, short int) {
		st, err := New(Options{
			Population:           500_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			BankEnabled:          r.bank,
			LoanFromBalance:      r.loan,
			OwnershipRestructure: r.own,
			WageBidEnabled:       bid,
		})
		if err != nil {
			t.Fatalf("%s: New: %v", r.name, err)
		}
		var nShort int
		for i := 0; i < 1200; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", r.name, i, err)
			}
			for j := range st.Buildings {
				if p := st.WagePremium(j); p > maxPrem {
					maxPrem = p
				}
			}
			for j := range st.wageShortTick {
				if st.wageShortTick[j] {
					nShort++
					break
				}
			}
		}
		return st.Population, st.InvestmentPool(), maxPrem, nShort
	}
	fmt.Printf("%-26s %14s %20s %12s %8s\n", "配置", "投资池(M7关)", "投资池(M7开)", "峰值溢价", "缺员tick")
	for _, r := range rows {
		_, iOff, _, sOff := run(r, false)
		_, iOn, prem, sOn := run(r, true)
		mark := ""
		if iOn != iOff {
			mark = fmt.Sprintf("  ← 差 %+.3e", iOn-iOff)
		}
		fmt.Printf("%-26s %14.6g %20.6g %12.4f %8d%s\n", r.name, iOff, iOn, prem, sOn, mark)
		_ = sOff
	}
}
