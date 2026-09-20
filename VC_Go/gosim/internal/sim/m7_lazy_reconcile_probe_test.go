package sim

// 【R79 诊断③】M7 在"稀缺 + 仅 M7"下到底惰性不惰性？
//
// 两个实验给出**矛盾**的结果：
//   - R78 的守卫（`TestAudit12FlagsAreOptIn`，600 tick）：稀缺 + WageBidEnabled
//     与稀缺基线"几乎相同"（只报 3 项且量级很小）；
//   - R79 的探针（`TestM7ActivationProbe`，1200 tick）：同样配置投资池
//     2.01789e9 → 2.21315e9（**差 +1.95e8**）。
//
// 两者不该矛盾。本探针用**同一局**逐 tick 对比，定位差异到底是
// (a) 我读错了量，(b) 两处 Options 实际不同，还是 (c) 时长导致的（600 vs 1200 tick）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestM7LazyReconcile -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

func TestM7LazyReconcile(t *testing.T) {
	auditEnabled(t)
	mk := func(bid bool) *State {
		st, err := New(Options{
			Population:           500_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			WageBidEnabled:       bid,
		})
		if err != nil {
			t.Fatalf("New(bid=%v): %v", bid, err)
		}
		return st
	}
	off, on := mk(false), mk(true)
	fmt.Printf("%6s %18s %18s %14s %10s %10s %14s\n",
		"tick", "投资池(关)", "投资池(开)", "差", "峰值溢价", "缺员", "政府池(关/开)")
	for i := 0; i < 1200; i++ {
		if _, err := off.Step(); err != nil {
			t.Fatalf("off Step %d: %v", i, err)
		}
		if _, err := on.Step(); err != nil {
			t.Fatalf("on Step %d: %v", i, err)
		}
		if st := on.Tick; st != 300 && st != 600 && st != 900 && st != 1200 {
			continue
		}
		var maxPrem float64
		for j := range on.Buildings {
			if p := on.WagePremium(j); p > maxPrem {
				maxPrem = p
			}
		}
		nShort := 0
		for _, b := range on.wageShortTick {
			if b {
				nShort++
			}
		}
		fmt.Printf("%6d %18.6g %18.6g %14.3g %10.4f %10d  %.4g/%.4g\n",
			on.Tick, off.InvestmentPool(), on.InvestmentPool(),
			on.InvestmentPool()-off.InvestmentPool(), maxPrem, nShort,
			off.Aud.Balance(ledger.Gov()), on.Aud.Balance(ledger.Gov()))
	}
}
