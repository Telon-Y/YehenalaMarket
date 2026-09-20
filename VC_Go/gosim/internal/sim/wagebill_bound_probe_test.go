package sim

// 【R69 验证辅助探针】wagebill 基数为何没有收敛到 (1+cap)×基准 的理论界？
//
// 理论推导（E1）：若每 tick 都缺员、利润率 m 稳定，则
//
//	Δp = cap × m × base     （因为 A_i/workers = cap × 工资总额 × m / workers
//	                          = cap × base × m）
//	稳态    p* = cap × m × base / (2 × decayPerTick)
//
// cap=0.5、base=6.75、m=0.2、decayPerTick=0.05/52 ⇒ p* ≈ 17.6 ⇒ 人均工资 ≈ 24。
// 实测峰值 66.08 ⇒ **推导与实现不符**。本探针逐 tick 打印缺员标记、利润率、
// 溢价增量，以定位差异来源。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestWageBillBoundProbe -v

import (
	"fmt"
	"testing"
)

func TestWageBillBoundProbe(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           500_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		WageBidEnabled:       true,
		WageBidBase:          "wagebill",
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	decay := st.Params.WageBidDecay / float64(st.Params.TicksPerYear)
	cap := st.Params.WageBidCap
	fmt.Printf("cap=%.3f decayPerTick=%.6f base(城镇)=%.4f\n", cap, decay, st.BaseWage(1))
	fmt.Printf("%5s %5s %6s %10s %10s %12s %12s %12s\n",
		"tick", "场地", "缺员", "利润率", "在岗人数", "工资总额", "理论Δp", "实际溢价")
	var nShort int
	for i := 0; i < 900; i++ {
		// 记录 step 前的状态，便于分离"本 tick 抬了多少"
		premBefore := make([]float64, len(st.Buildings))
		for j := range st.Buildings {
			premBefore[j] = st.WagePremium(j)
		}
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.Tick < 300 || st.Tick%60 != 0 {
			continue
		}
		for j := range st.Buildings {
			p := st.WagePremium(j)
			if p <= 0 {
				continue
			}
			b := &st.Buildings[j]
			workers := b.Level * b.HireRate * b.Spec.LaborPerLevel
			wageBill := workers * st.BaseWage(j)
			margin := b.LastMargin
			theo := 0.0
			if margin > 0 {
				theo = cap * wageBill * margin / workers
			}
			short := false
			if j < len(st.wageShortTick) {
				short = st.wageShortTick[j]
			}
			if short {
				nShort++
			}
			fmt.Printf("%5d %5d %6v %10.4f %10.1f %12.2f %12.6f %12.4f\n",
				st.Tick, j, short, margin, workers, wageBill, theo, p)
		}
	}
	fmt.Printf("（采样窗口中缺员次数 %d）\n", nShort)
}
