package sim

// price_anchor_test.go —— §3.4 候选方案 C′（派生需求锚）的行为测试。
//
// 背景：现行锚把中间品需求钉在【开局净供给】上，产能偏离开局后需求被低估 λ 倍
// （见 docs/ACTIVE.md §6.4）。方案 C′ 每 tick 按派生需求重锚。
// 本文件验证两件事：
//  1. 开关关闭时锚**不随等级变化**（与契约现行条文一致）；
//  2. 开关打开时锚 = (Σ_j 等级_j·q_gj)·r^ε，即随下游产能等比增长。
//
// 注意开关默认关闭：它只是裁决前的对照实验入口，不是契约条文。

import (
	"math"
	"testing"

	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/model"
)

func anchorTestState(t *testing.T, derived bool) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		AnchorDerivedDemand:  &derived,
	})
	if err != nil {
		t.Fatalf("New(derived=%v): %v", derived, err)
	}
	return st
}

// derivedDemandOf 按契约 §3.3 的配方计算某商品的派生需求 Σ_j 等级_j·q_gj。
func derivedDemandOf(st *State, good int) float64 {
	var sum float64
	for j, b := range st.buildingSpecs() {
		if b.IsNonMarket() {
			continue
		}
		if q, ok := b.Recipe.Inputs[good]; ok {
			sum += st.Buildings[j].Level * q
		}
	}
	return sum
}

// TestDefaultAnchorSchemeIsAdopted 守门：§3.4 的裁决（2026-09-19）把方案 A + C′ 定为契约默认。
//
// 若有人把默认改回历史口径，本测试立刻失败——那属于契约修订，必须走裁决与记录（ACTIVE.md §七）。
func TestDefaultAnchorSchemeIsAdopted(t *testing.T) {
	p := model.DefaultParams()
	if !p.AnchorExpenditureShare {
		t.Error("§3.4 裁决：方案 A（支出份额锚）应为默认开启")
	}
	if !p.AnchorDerivedDemand {
		t.Error("§3.4 裁决：方案 C′（派生需求锚）应为默认开启")
	}

	// 默认构造的状态必须把 A 接到市场需求弹性上（ε ≡ 1）
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	if !st.Market.UnitElastic {
		t.Error("默认状态下 market.UnitElastic 应为 true（方案 A）")
	}
	for i, g := range st.Goods {
		if got := st.Market.EpsOf(g); got != 1 {
			t.Fatalf("%s: 默认口径下需求弹性 = %.2f，应为 1（方案 A）", g.Name, got)
		}
		_ = i
	}

	// C′ 生效的可观察后果：跑一个 tick 后，中间品的锚随下游产能重锚（≠ 开局标定锚）
	coal := buildingByOutput(st.Buildings, 5)
	before := st.Market.Anchor(coal)
	if _, err := st.Step(); err != nil {
		t.Fatalf("Step: %v", err)
	}
	if after := st.Market.Anchor(coal); math.Abs(after-before) < 1e-9 {
		t.Errorf("默认应启用 C′：跑一个 tick 后煤的锚应随派生需求重锚，实测 %.6f → %.6f", before, after)
	}
}

func TestDerivedAnchorFollowsDownstreamCapacity(t *testing.T) {
	const coalGood = 5   // 煤：被 煤(15)/铁(15)/钢(30) 每级消耗
	const steelGood = 7  // 钢（下游放大器）

	// ── 关：锚不随等级变化 ──
	off := anchorTestState(t, false)
	coalIdx := buildingByOutput(off.Buildings, coalGood)
	steelIdx := buildingByOutput(off.Buildings, steelGood)
	if coalIdx < 0 || steelIdx < 0 {
		t.Fatal("找不到煤或钢的建筑下标")
	}
	before := off.Market.Anchor(coalIdx)
	off.Buildings[steelIdx].Level *= 2
	if _, err := off.Step(); err != nil {
		t.Fatalf("Step: %v", err)
	}
	if got := off.Market.Anchor(coalIdx); math.Abs(got-before) > 1e-9 {
		t.Errorf("开关关闭时锚发生了变化：%.6f → %.6f（应恒为开局标定值）", before, got)
	}

	// ── 开：锚 = 当期派生需求 · r^ε ──
	on := anchorTestState(t, true)
	coalIdx = buildingByOutput(on.Buildings, coalGood)
	steelIdx = buildingByOutput(on.Buildings, steelGood)
	baseDerived := derivedDemandOf(on, coalGood)

	on.Buildings[steelIdx].Level *= 2
	if _, err := on.Step(); err != nil {
		t.Fatalf("Step: %v", err)
	}
	wantDerived := derivedDemandOf(on, coalGood)
	if math.Abs(wantDerived-1.5*baseDerived) > 1e-9 {
		t.Fatalf("测试前提失效：钢等级翻倍后派生需求应 ×1.5，实测 %.3f → %.3f", baseDerived, wantDerived)
	}
	want := calibrate.DemandConstantAt(on.Goods[coalGood], wantDerived, on.Market.EpsOf(on.Goods[coalGood]))
	if got := on.Market.Anchor(coalIdx); math.Abs(got-want) > 1e-9 {
		t.Errorf("C′ 锚 = %.6f，应为 派生需求 %.3f · r^ε = %.6f（ε = %.1f）",
			got, wantDerived, want, on.Market.EpsOf(on.Goods[coalGood]))
	}
	t.Logf("煤的派生需求：%.0f → %.0f（钢等级 ×2）⇒ 锚按同比例重锚（%.3f → %.3f）",
		baseDerived, wantDerived, calibrate.DemandConstantAt(on.Goods[coalGood], baseDerived, on.Market.EpsOf(on.Goods[coalGood])), want)

	// 最终品不受 C′ 影响（住房有家庭最终需求，锚应保持标定值）
	housingIdx := buildingByOutput(on.Buildings, 9)
	housingWant := calibrate.DemandConstantAt(on.Goods[9], housingAnchorNet(on), on.Market.EpsOf(on.Goods[9]))
	if got := on.Market.Anchor(housingIdx); math.Abs(got-housingWant) > 1e-6 {
		t.Errorf("最终品（住房）的锚被 C′ 改动了：%.6f，应为标定值 %.6f", got, housingWant)
	}
}

// housingAnchorNet 取住房的净供给标定值（与 New 里的口径一致），用于比对最终品锚。
func housingAnchorNet(st *State) float64 {
	net := st.netSupply(st.calibration)
	return net[9]
}
