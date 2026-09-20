package sim

// 【R79 诊断④】复现 R78 守卫里 WageBidEnabled 那一条的**确切**比较，
// 定位它与 `TestM7LazyReconcile` 的矛盾（守卫说"几乎相同"，探针说 600 tick 差 9.37e7）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestM7GuardReconcile -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

func TestM7GuardReconcile(t *testing.T) {
	auditEnabled(t)
	runGuard := func(name string, mut func(o *Options)) (pop, invest, capital, gov, tax, div, debt float64) {
		o := Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
		}
		if mut != nil {
			mut(&o)
		}
		st, err := New(o)
		if err != nil {
			t.Fatalf("%s: New: %v", name, err)
		}
		for i := 0; i < 600; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", name, i, err)
			}
		}
		return st.Population, st.InvestmentPool(), st.balCap(), st.balGov(),
			st.Gov.TaxCollected, st.Aud.Balance(ledger.LaborDividend()),
			st.Cap.DebtOutstandingTotal()
	}
	bPop, bInv, bCap, bGov, bTax, bDiv, bDebt := runGuard("base", func(o *Options) { o.Population = 500_000 })
	oPop, oInv, oCap, oGov, oTax, oDiv, oDebt := runGuard("on", func(o *Options) {
		o.Population = 500_000
		o.WageBidEnabled = true
	})
	fmt.Printf("%-14s %20s %20s\n", "量", "base(稀缺)", "on(+M7)")
	for _, r := range []struct {
		n    string
		a, b float64
	}{
		{"人口", bPop, oPop},
		{"投资池", bInv, oInv},
		{"资本池", bCap, oCap},
		{"政府池", bGov, oGov},
		{"税收", bTax, oTax},
		{"分红池", bDiv, oDiv},
		{"未偿负债", bDebt, oDebt},
	} {
		rel := 0.0
		if r.a != 0 {
			rel = (r.b - r.a) / r.a
		}
		fmt.Printf("%-14s %20.10g %20.10g  逐位相同=%v 相对差=%+.3e\n",
			r.n, r.a, r.b, r.a == r.b, rel)
	}
}
