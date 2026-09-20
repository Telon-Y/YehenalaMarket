package sim

// 【1.2 M7 验证辅助探针②】稀缺场景下的**长期**行为：溢价是否发散。
//
// 背景：探针①在 pop500k（市场用工/人口 = 1.19）发现 600 tick 内最大溢价 ≈ 97.8 元，
// 而基准工资只有 6.75 元。若溢价无限发散，M7 就重现了裁决②要避免的**棘轮**；
// 若它被"利润率 → 抬价额度 A_i = cap×纯利"隐式封顶，则应在某水平饱和。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestScarceLongProbe -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

func TestScarceLongProbe(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           500_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		WageBidEnabled:       true,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	money0 := st.TotalMoney()
	report := func(i int) {
		var maxP, maxW, sumP float64
		hot := -1
		for j := range st.Buildings {
			p := st.WagePremium(j)
			sumP += p
			if p > maxP {
				maxP, hot = p, j
			}
			if w := st.EffectiveWage(j); w > maxW {
				maxW = w
			}
		}
		fmt.Printf("tick %5d 人口 %9.0f 失业 %8.0f 配给比 %.4f 最大溢价 %10.3f 最大人均工资 %10.3f 溢价和 %10.2f 热点 %d\n",
			i, st.Population, st.Unemployed, st.LaborMarketRatio, maxP, maxW, sumP, hot)
	}
	for i := 0; i < 4000; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.InvariantErr != nil {
			t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
		}
		if v := len(st.Aud.Violations()); v != 0 {
			t.Fatalf("借贷不等 %d 笔 @%d", v, i)
		}
		switch i {
		case 99, 199, 399, 799, 1199, 1999, 2999, 3999:
			report(i + 1)
		}
	}
	delta := st.TotalMoney() - money0
	want := st.TickNewCapitalTotal() + st.InfusionTotal()
	fmt.Printf("货币守恒：Δ=%.4f 应=%.4f 差=%.6f\n", delta, want, delta-want)
	// 货币总量是否被工资溢价推成负数（各现金池）
	for _, kv := range []struct {
		name string
		bal  float64
	}{
		{"政府", st.Aud.Balance(ledger.Gov())},
		{"资本", st.Aud.Balance(ledger.Capital())},
		{"投资池", st.Aud.Balance(ledger.Investment())},
		{"储蓄池", st.Aud.Balance(ledger.Savings())},
	} {
		fmt.Printf("  %-8s 余额 %22.2f\n", kv.name, kv.bal)
	}
}
