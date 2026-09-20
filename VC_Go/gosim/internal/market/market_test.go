package market

import (
	"math"
	"testing"

	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/model"
)

// TestConvergesToAnalyticEquilibrium 校验 §2 的数值积分与 §2.4 的解析平衡态一致。
//
// 这是价格侧最核心的守门测试：把产能固定，从 P_init 出发，
// RK4 必须收敛到 P* = P₀·(a/S)^(1/ε)。
//
// 已由 tools/price_dynamics_probe.js 独立验证：11 种商品最大相对误差 7.2e-13
// （R14 改 m ≡ S 后复测为 4.05e-13%，同量级）。
// 若本测试失败，说明 §2.3 的 K/m/ρ 标定、RK4 实现或钳制逻辑出了问题。
//
// 循环次数说明（2026-09-19）：§2.3 改后 m = 当期流通量，T = 2π√(m/K) 是内生的，
// 本测试里建造力（ε = 0.2、P_cost = 7250）的 T 约 1340 tick，按 ζ = 0.7 收敛到
// 1e-6 需约 3.1·T ≈ 4200 tick，故取 20000 tick 留足余量。
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

	for tick := 0; tick < 20000; tick++ {
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
// §2.3 改后 m = 当期流通量 S、ρ = 2ζ√(m·K)，故 ω = √(K/m) 不再是常数：
//
//	ω = √( (ε/P_cost) · (a/S) · (P/P_cost)^(−ε−1) )
//
// 本测试在契约允许的运行包络内逐商品核算最坏情形：需求/供给比 a/S ≤ 100、
// 价格贴下限 0.2·P_cost。**同时核算两套锚定口径**——契约默认的方案 A 取 ε≡1，
// 历史口径取该商品的 ε；取两者的较大者，使测试在两种口径下都成立。
func TestStabilityCriterion(t *testing.T) {
	p := model.DefaultParams()
	h := p.DT / float64(p.Substeps)
	const (
		demandSupplyMax = 100.0 // a/S 的上限（远超实测范围）
		ratioMin        = 0.2   // 价格下限 P_floor/P_cost（§2.4）
	)
	worst, worstName, worstEps := 0.0, "", 0.0
	for _, g := range model.GoodSpecs() {
		for _, eps := range []float64{1, g.Eps} { // 1 = 方案 A（默认）；g.Eps = 历史口径
			omega := math.Sqrt(eps / g.Pcost * demandSupplyMax * math.Pow(ratioMin, -eps-1))
			if omega*h > worst {
				worst, worstName, worstEps = omega*h, g.Name, eps
			}
		}
	}
	if worst > 2.8 {
		t.Fatalf("%s（ε=%.1f）: ω·h = %.4f 超过 RK4 的稳定边界 2.8", worstName, worstEps, worst)
	}
	t.Logf("包络内最坏 ω·h = %.4f（%s，ε=%.1f；边界 2.8，余量 %.0f 倍）",
		worst, worstName, worstEps, 2.8/worst)
}

// TestUnitElasticAnchorPricePath 校验 §3.4 候选方案 A（支出份额锚）的价格路径。
//
// 方案 A 取 ε ≡ 1 且 a = S₀·P_init/P_cost，于是解析均衡为 P*(λ) = P_init/λ：
//   · 产能翻倍 ⇒ 价格减半（与 ε 无关，故必需品不再被 −1/ε 放大）；
//   · 与现行锚（P* = P_init·λ^(−1/ε)）在 λ>1 时显著不同。
//
// 本测试同时钉住"开关只改需求弹性"这一点：关掉开关时收敛到现行解析式。
func TestUnitElasticAnchorPricePath(t *testing.T) {
	goods := model.GoodSpecs()
	p := model.DefaultParams()
	const idx = 0 // 谷物，ε = 0.3（现行锚下最容易被压制的一种）

	run := func(unitElastic bool, lambda float64) float64 {
		st := NewState(goods, p)
		st.UnitElastic = unitElastic
		g := goods[idx]
		const s0 = 250.0
		s := s0 * lambda
		eps := g.Eps
		if unitElastic {
			eps = 1
		}
		st.Markets[idx].A = calibrate.DemandConstantAt(g, s0, eps) // 锚定在 S₀
		for tick := 0; tick < 40000; tick++ {
			st.SettleAll(uniformSupply(s, len(goods)), nil)
		}
		return st.Markets[idx].Price
	}

	const lambda = 2.0
	gotA := run(true, lambda)
	wantA := goods[idx].Pinit / lambda
	if rel := math.Abs(gotA-wantA) / wantA; rel > 1e-6 {
		t.Errorf("方案 A：λ=%.1f 时价格 %.4f，解析 P_init/λ = %.4f（相对误差 %.2e）",
			lambda, gotA, wantA, rel)
	}

	// 现行锚：解析均衡 P* = P_init·λ^(−1/ε)。谷物 ε=0.3、λ=2 时 P* ≈ 80.4，
	// **低于地板 0.2·P_cost = 135**，故价格贴底锁死——这正是 §3.4 病态 P2
	// （价格与短缺脱节）的最小可复现例：产能翻倍后谷物价格不再由供需决定。
	gotOld := run(false, lambda)
	wantOld := goods[idx].Pinit * math.Pow(lambda, -1/goods[idx].Eps)
	floor := goods[idx].PriceFloorRatio * goods[idx].Pcost
	if wantOld >= floor {
		t.Fatalf("该商品的解析 P* = %.2f 应低于地板 %.2f 才能演示贴底病态", wantOld, floor)
	}
	if math.Abs(gotOld-floor) > 1e-9 {
		t.Errorf("现行锚：λ=%.1f 时价格 %.4f，应恰好贴在地板 %.4f（解析 P* = %.4f 在其下方）",
			lambda, gotOld, floor, wantOld)
	}
	t.Logf("λ=%.1f：现行锚贴地板 %.1f 元（解析 P* = %.1f，被钳制吞掉）vs 方案 A %.1f 元 —— 差 %.1f 倍",
		lambda, gotOld, wantOld, gotA, gotA/gotOld)
}

// uniformSupply 构造一份"每种商品同样多"的固定供给切片（仅供单商品测试使用）。
func uniformSupply(s float64, n int) []float64 {
	out := make([]float64, n)
	for i := range out {
		out[i] = s
	}
	return out
}

// TestInertiaEqualsCirculatingGoods 校验 §2.3 的新口径：
//
//	m ≡ 当期市场内流通商品量（= 当期供给 S）
//	ρ = 2ζ·√(m·K)，T = 2π·√(m/K)
//
// 推论：同一价格、同一需求标定 a 下 K 不变，故流通量翻倍 ⇒ 周期 ×√2。
func TestInertiaEqualsCirculatingGoods(t *testing.T) {
	goods := model.GoodSpecs()
	p := model.DefaultParams()
	const idx = 0

	probe := func(supply float64) (mass, period float64) {
		st := NewState(goods, p)
		st.Markets[idx].A = 250 // 固定 a，使两次调用的 K(P_init) 相同
		st.Integrate(idx, supply, 0)
		return st.Markets[idx].Mass, st.Markets[idx].Period
	}

	m1, t1 := probe(250)
	if math.Abs(m1-250) > 1e-12 {
		t.Errorf("惯性 m = %.9f，应等于当期流通量 250", m1)
	}
	m2, t2 := probe(500)
	if math.Abs(m2-500) > 1e-12 {
		t.Errorf("惯性 m = %.9f，应等于当期流通量 500", m2)
	}
	if rel := math.Abs(t2/t1-math.Sqrt2) / math.Sqrt2; rel > 1e-9 {
		t.Errorf("流通量翻倍后周期比 = %.12f，应为 √2 = %.12f", t2/t1, math.Sqrt2)
	}

	// 契约起点的闭式解：由 a = S·r^ε（§3.4 步骤 3）与 K = (εa/P₀)r^(−ε−1) 得
	//   T = 2π·√(P₀·r/ε) = 2π·√(P_init/ε)
	// 即 T 与流通量 S 无关、只由该商品的 P_init 与 ε 决定。
	g := goods[idx]
	st := NewState(goods, p)
	const s0 = 250.0
	st.Markets[idx].A = s0 * math.Pow(g.Pinit/g.Pcost, g.Eps)
	st.Integrate(idx, s0, 0)
	wantT := 2 * math.Pi * math.Sqrt(g.Pinit/g.Eps)
	if rel := math.Abs(st.Markets[idx].Period-wantT) / wantT; rel > 1e-12 {
		t.Errorf("%s: 起点周期 %.6f，闭式解 2π√(P_init/ε) = %.6f", g.Name, st.Markets[idx].Period, wantT)
	}
	for _, gg := range goods {
		t.Logf("%-8s T = %7.1f tick（P_init = %6.0f，ε = %.1f）", gg.Name, 2*math.Pi*math.Sqrt(gg.Pinit/gg.Eps), gg.Pinit, gg.Eps)
	}
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
