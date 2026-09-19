package sim

// nominal_probe_test.go —— 「名义发散」与「实际增长」的分离诊断。
//
// 背景（用户提问）：10,000 tick 长跑出现价格全撞钳制带、政府债务 1e13 量级的**名义发散**。
// 这是否说明"经济在正常运行且健康"？要回答它必须把**名义量**与**实际量**分开看：
//
//	实际产出指数  = Σ_g Q_g(t)·P_g(0) / Σ_g Q_g(0)·P_g(0)   （拉氏，固定基期价格）
//	价格指数      = Σ_g q_g(0)·P_g(t) / Σ_g q_g(0)·P_g(0)   （拉氏，固定基期篮子）
//	实际货币存量  = 名义货币 / 价格指数
//
// 若实际产出停滞而只有价格与货币在涨，那就是纯粹的通货膨胀（价格机制失效），
// 不能读作"健康"；若实际产出也在增长，则名义发散是"实际增长 + 名义失锚"的叠加。
//
// 运行：go test ./internal/sim/ -run TestPrintNominalVsReal -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/model"
)

// TestPrintNominalVsReal 打印名义与实际两条轨迹（默认参数、**20m 人口**，与 CLI 默认一致）。
func TestPrintNominalVsReal(t *testing.T) {
	st, err := New(Options{
		Population:           20_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("构造: %v", err)
	}
	// 基期（t=0）的价格与过库量：用开局价与初始净供给作为拉氏权重。
	baseP := make([]float64, model.Goods)
	for i := range baseP {
		baseP[i] = st.Goods[i].Pinit
	}
	baseQ := st.netSupply(st.calibration)
	sample := map[int64]bool{1: true, 52: true, 260: true, 520: true, 1000: true, 3000: true, 5000: true, 10000: true}

	fmt.Printf("%6s %12s %12s %12s %12s %12s %12s\n",
		"tick", "实际产出指数", "价格指数", "名义GDP", "实际GDP", "名义货币", "实际货币")
	for i := 0; i < 10000; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		if !sample[snap.Tick] {
			continue
		}
		// 实际产出：Σ 产出量 × 基期价
		var real, base float64
		for g := 0; g < model.Goods; g++ {
			real += snap.Supply[g] * baseP[g]
			base += baseQ[g] * baseP[g]
		}
		realIdx := real / base
		// 价格指数：Σ 基期过库量 × 当期价
		var num, den float64
		for g := 0; g < model.Goods; g++ {
			num += baseQ[g] * snap.Prices[g]
			den += baseQ[g] * baseP[g]
		}
		pIdx := num / den
		// 名义 GDP 用 §7 的口径（消费者支出 + 各池期末余额）
		nomGDP := snap.SpendNet + snap.CashTotal
		money := snap.TotalMoney
		fmt.Printf("%6d %12.3f %12.3f %12.3e %12.3e %12.3e %12.3e\n",
			snap.Tick, realIdx, pIdx, nomGDP, nomGDP/pIdx, money, money/pIdx)
	}
	// 结论量：末态的实际资本存量与名义货币
	fmt.Printf("\n末态：总级数 %.0f（开局 %.0f）、人口 %.0f、失业 %.0f、幸福度 %.4f\n",
		st.TotalLevels(), 323.0, st.Population, st.Unemployed, st.Last.Happiness())
	fmt.Printf("政府债务 %.3e（债务上限 %.3e，比值 %.3f）\n",
		st.Gov.Debt(), st.Gov.DebtCap(st.powerPriceNow), st.Gov.Debt()/st.Gov.DebtCap(st.powerPriceNow))
}
