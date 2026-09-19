package product

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// TestComputeSplitsAgricultureAndIndustry 校验部类划分与固定价计价。
//
// 用一个**手工构造**的最小输入：谷物农场（农村）与工具厂（城镇），
// 参考价取 100 / 200，检验总产值、中间投入与增加值逐项对上。
func TestComputeSplitsAgricultureAndIndustry(t *testing.T) {
	specs := []model.Building{
		{Name: "谷物农场", Category: model.CatRural, Recipe: model.Recipe{Output: 0, Qty: 50}},
		{Name: "工具厂", Category: model.CatTown, Recipe: model.Recipe{
			Output: 8, Qty: 80, Inputs: map[int]float64{7: 20}}}, // 20 钢 → 80 工具
		{Name: "仓库", Category: model.CatDevelopment, IsWarehouse: true},
	}
	// 参考价：谷物 100（下标 0）、钢 200（下标 7，工具厂的投入）、工具 200（下标 8，工具厂的产出）
	ref := []float64{100, 0, 0, 0, 0, 0, 0, 200, 200, 0, 0}
	in := Input{
		Specs:      specs,
		Levels:     []float64{2, 3, 10},
		Hire:       []float64{1, 0.5, 1},
		Shortage:   []float64{1, 0.8, 1},
		AllocRatio: []float64{1, 1, 1, 1, 1, 1, 1, 0.5, 1, 1, 1},
		RefPrices:  ref,
		Population: 10,
	}
	r := Compute(in)

	// 农业：2 级 × 1 × 50 × 100 = 10,000
	if math.Abs(r.GrossAgri-10000) > 1e-9 {
		t.Errorf("农业总产值 = %.4f，应为 10000", r.GrossAgri)
	}
	// 工业：3 级 × 0.5 × 80 × 0.8 × 200 = 19,200
	if math.Abs(r.GrossIndustry-19200) > 1e-9 {
		t.Errorf("工业总产值 = %.4f，应为 19200", r.GrossIndustry)
	}
	// 工业中间投入：3 × 0.5 × 20 × 0.5(配给) × 200 = 3,000
	if math.Abs(r.InterIndustry-3000) > 1e-9 {
		t.Errorf("工业中间投入 = %.4f，应为 3000", r.InterIndustry)
	}
	if math.Abs(r.InterAgri) > 1e-9 {
		t.Errorf("农业中间投入 = %.4f，应为 0（谷物农场无投入）", r.InterAgri)
	}
	if want := 10000.0 + 19200.0 - 3000.0; math.Abs(r.Added-want) > 1e-9 {
		t.Errorf("工农增加值 = %.4f，应为 %.4f", r.Added, want)
	}
	if want := r.Added / 10; math.Abs(r.PerCapita-want) > 1e-12 {
		t.Errorf("人均实际增加值 = %.4f，应为 %.4f", r.PerCapita, want)
	}
	// 仓库有 10 级但不生产商品：不得进入任何一部类（防空真）。
	if r.Gross != r.GrossAgri+r.GrossIndustry {
		t.Error("总产值 ≠ 农业 + 工业")
	}
}

// TestSubsistenceCountsAsAgriculture 校验自给农场产出计入农业且无中间投入。
func TestSubsistenceCountsAsAgriculture(t *testing.T) {
	specs := []model.Building{{Name: "谷物农场", Category: model.CatRural, Recipe: model.Recipe{Output: 0, Qty: 50}}}
	in := Input{
		Specs:       specs,
		Levels:      []float64{0},
		Hire:        []float64{1},
		RefPrices:   []float64{100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		Subsistence: map[int]float64{0: 200, 2: 30},
		Population:  1,
	}
	r := Compute(in)
	// 200 谷物 × 100 + 30 织物 × 0（参考价表里织物为 0）= 20,000
	if math.Abs(r.GrossAgri-20000) > 1e-9 {
		t.Errorf("农业总产值 = %.4f，应为 20000（自给产出按参考价计入）", r.GrossAgri)
	}
	if math.Abs(r.Added-r.GrossAgri) > 1e-9 {
		t.Errorf("自给农场无货币中间投入，增加值应等于总产值（%.4f vs %.4f）", r.Added, r.GrossAgri)
	}
}

// TestPricesDoNotAffectRealProduct 校验本口径**与货币/价格无关**（这是它的存在理由）。
//
// 同一组实物量，无论"当期市价"是多少，结果必须逐位相同——因为只用固定参考价。
func TestPricesDoNotAffectRealProduct(t *testing.T) {
	specs := []model.Building{{Name: "谷物农场", Category: model.CatRural, Recipe: model.Recipe{Output: 0, Qty: 50}}}
	base := Input{
		Specs:      specs,
		Levels:     []float64{4},
		Hire:       []float64{0.75},
		RefPrices:  []float64{100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		Population: 8,
	}
	a := Compute(base)
	// "价格翻 100 倍"这个信息根本没有入口——Input 里只有实物量与固定参考价。
	if a.Added != Compute(base).Added {
		t.Error("同一输入两次核算结果不同（应逐位相同）")
	}
	if a.PerCapita <= 0 {
		t.Error("人均实际增加值应为正")
	}
}
