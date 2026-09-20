package sim

// 【R88 诊断】金矿与中央银行：产出、造币、以及"黄金有剩余 ⇒ 央行自动扩建"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestGoldMineProbe -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

func TestGoldMineProbe(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		CentralBankEnabled:   true,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	gi, ci := st.goldMineIndex(), st.centralBankIndex()
	fmt.Printf("金矿下标 %d、央行下标 %d、失业场地 %d（场地总数 %d）\n",
		gi, ci, st.UnemployedSite, len(st.Buildings))
	fmt.Printf("%6s %8s %8s %10s %12s %14s %16s %14s\n",
		"tick", "金矿级", "央行级", "本tick产金", "累计产金", "累计造币", "央行池", "金矿池")
	for i := 0; i < 600; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.Tick%100 != 0 {
			continue
		}
		fmt.Printf("%6d %8.3f %8.3f %10.3f %12.1f %14.1f %16.1f %14.1f\n",
			st.Tick, st.Buildings[gi].Level, st.Buildings[ci].Level,
			st.goldPerTick(), st.GoldProducedTotal(), st.MintedTotal(),
			st.Aud.Balance(ledger.CentralBank()), st.bal(gi))
	}
	fmt.Printf("\n吞吐量 = 央行级 %.3f × 每级 %g = %.2f；本期产出 %.3f\n",
		st.Buildings[ci].Level, st.Params.GoldPerBankLevel,
		st.Buildings[ci].Level*st.Params.GoldPerBankLevel, st.goldPerTick())
	fmt.Printf("末期：金矿利润率EMA %.6f、收入 %.2f、纯利 %.2f\n",
		st.Buildings[gi].MarginEMA, st.Buildings[gi].LastRevenue, st.Buildings[gi].LastProfit)
}
