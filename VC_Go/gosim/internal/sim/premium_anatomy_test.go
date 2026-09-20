package sim

// 【1.2 M7 验证辅助探针③】拆解溢价 97.8 到底从哪来。
//
// R68 我假设"分母用了**在岗人数**，缺员时被压缩 ⇒ 人均溢价被放大"。
// 实测反驳：把分母改成**申报满编**后，峰值溢价一位不变（97.8249 → 97.8249）。
// 说明该放大机制不存在。本探针逐 tick 打印热点场地的分项：
//
//	Level / HireRate / 申报用工(=满编) / 实到岗 / 本期纯利 / A_i / 溢价增量
//
// 以确定溢价的真实来源。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestPremiumAnatomy -v

import (
	"fmt"
	"testing"
)

func TestPremiumAnatomy(t *testing.T) {
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
	fmt.Printf("%5s %5s %10s %8s %10s %10s %14s %12s %10s %10s\n",
		"tick", "场地", "Level", "HireRate", "申报用工", "实到岗", "纯利", "A_i", "溢价", "人均工资")
	for i := 0; i < 700; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.Tick < 240 || st.Tick > 640 || st.Tick%50 != 0 {
			continue
		}
		for j := range st.Buildings {
			if st.WagePremium(j) <= 0 {
				continue
			}
			b := &st.Buildings[j]
			claimed := b.Level * b.HireRate * b.Spec.LaborPerLevel
			// 实到岗：按全局配给比折算（与 allocateSubsistenceLabor 同口径的近似）
			arrived := claimed * st.LaborMarketRatio
			A := 0.0
			if b.LastProfit > 0 {
				A = st.Params.WageBidCap * b.LastProfit
			}
			fmt.Printf("%5d %5d %10.2f %8.4f %10.1f %10.1f %14.2f %12.2f %10.3f %10.4f\n",
				st.Tick, j, b.Level, b.HireRate, claimed, arrived, b.LastProfit, A,
				st.WagePremium(j), st.EffectiveWage(j))
		}
	}
}
