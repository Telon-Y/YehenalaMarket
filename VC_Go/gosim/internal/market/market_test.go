package market

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// TestConvergesToAnalyticEquilibrium 校验 §2 的数值积分与 §2.4 的解析平衡态一致。
//
// 这是价格侧最核心的守门测试：把产能固定，从 P_init 出发，
// RK4 必须收敛到 P* = P₀·(a/S)^(1/ε)。
//
// 已由 tools/price_dynamics_probe.js 独立验证：11 种商品最大相对误差 7.2e-13。
// 若本测试失败，说明 T/ζ 标定、RK4 实现或钳制逻辑出了问题。
func TestConvergesToAnalyticEquilibrium(t *testing.T) {
	goods := model.GoodSpecs()
	p := model.DefaultParams()
	st := NewState(goods, p)

	// 构造一组固定供给与标定常数：令 a = S 的某个倍数，
	// 使 P* 落在钳制区间内，便于逐项比对。
	supply := []float64{1000, 200, 500, 300, 40, 300, 300, 200, 150, 100, 20}
	want := make([]float64, len(goods))
	for i, g := range goods {
		// 取 a 使 P* = Pcost · 1.25（落在一半处，远离钳制边界）
		st.Markets[i].A = supply[i] * math.Pow(1.25, g.Eps)
		want[i] = g.Pcost * 1.25
	}

	for tick := 0; tick < 2000; tick++ {
		st.SettleAll(supply, nil)
	}

	for i, g := range goods {
		got := st.Markets[i].Price
		rel := math.Abs(got-want[i]) / want[i]
		if rel > 1e-6 {
			t.Errorf("%s: 收敛价 %.6f，解析平衡态 %.6f，相对误差 %.3e",
				g.Name, got, want[i], rel)
		}
		// 同时确认没有触发钳制（否则解析解不适用）
		if st.Markets[i].ClampTicks != 0 {
			t.Errorf("%s: 触发了 %d 次价格钳制，该情形不应触边", g.Name, st.Markets[i].ClampTicks)
		}
	}
}

// TestStabilityCriterion 校验 §2.4 的 RK4 稳定性要求 ω·h ≤ 2.8。
//
// ω = sqrt(K/m)，由 m = K·T²/(4π²) 得 ω = 2π/T，故 ω·h = 2π·h/T。
// 这给出一个与商品、价格都无关的判据：h ≤ 2.8·T/(2π)。
// T = 26、n_sub = 10 时 h = 0.05，ω·h = 0.0121，远低于 2.8 的边界。
func TestStabilityCriterion(t *testing.T) {
	p := model.DefaultParams()
	h := p.DT / float64(p.Substeps)
	omega := 2 * math.Pi / p.PricePeriod
	omegaH := omega * h
	if omegaH > 2.8 {
		t.Fatalf("ω·h = %.4f 超过 RK4 的稳定边界 2.8", omegaH)
	}
	t.Logf("ω·h = %.6f（边界 2.8，余量 %.0f 倍）", omegaH, 2.8/omegaH)
}

// TestPriceFloorPreventsNegativeDemand 校验 §2.4 的价格钳制确实阻止了 P→0 发散。
//
// 契约 §2.1 明确指出常弹性需求的代价是 P→0 时 D→∞，故必须设价格下限。
// 本测试给一个极端供给冲击，确认价格停在下限且需求有限。
func TestPriceFloorPreventsNegativeDemand(t *testing.T) {
	goods := model.GoodSpecs()
	p := model.DefaultParams()
	st := NewState(goods, p)

	const idx = 0 // 谷物，ε = 0.3（弹性最小，最容易发散）
	supply := make([]float64, len(goods))
	for i := range supply {
		supply[i] = 1e6
	}
	supply[idx] = 1e-6 // 极端短缺
	st.Markets[idx].A = 1000

	for tick := 0; tick < 500; tick++ {
		st.SettleAll(supply, nil)
	}
	floor := goods[idx].PriceFloorRatio * goods[idx].Pcost
	if st.Markets[idx].Price < floor*0.999 {
		t.Errorf("价格 %.4f 跌破下限 %.4f", st.Markets[idx].Price, floor)
	}
	if math.IsInf(st.Markets[idx].Demand, 0) || math.IsNaN(st.Markets[idx].Demand) {
		t.Error("需求发散为 Inf/NaN")
	}
}

// TestEquilibriumIdentityIsEpsilonIndependent 校验 §2.4 的标定恒等式：
// a = S ⇒ P* = P₀ = Pcost，且与 ε 无关。这是契约 §3.1 价格表的验收条件。
func TestEquilibriumIdentityIsEpsilonIndependent(t *testing.T) {
	goods := model.GoodSpecs()
	for i, g := range goods {
		const s = 123.456
		got, capped := Equilibrium(g, s, s, 0)
		if capped {
			t.Errorf("%s: a = S 时不应进入产能不足分支", g.Name)
		}
		if math.Abs(got-g.Pcost) > 1e-9 {
			t.Errorf("%s (ε=%.1f): P* = %.6f，应为 Pcost = %.6f", g.Name, g.Eps, got, g.Pcost)
		}
		_ = i
	}
}

// TestCapacityShortageBranch 校验 §2.4 的正解条件 F_ext < S：
// 若外生冲击吞掉全部供给，价格应顶到上限并报告产能不足分支。
func TestCapacityShortageBranch(t *testing.T) {
	g := model.GoodSpecs()[0]
	got, capped := Equilibrium(g, 100, 50, 60) // F_ext = 60 > S = 50
	if !capped {
		t.Error("F_ext > S 时应进入产能不足分支")
	}
	ceil := g.PriceCeilRatio * g.Pcost
	if math.Abs(got-ceil) > 1e-9 {
		t.Errorf("产能不足时价格应为上限 %.1f，实测 %.1f", ceil, got)
	}
}
