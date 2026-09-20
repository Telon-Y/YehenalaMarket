package model

import "testing"

// TestGoodIndicesMatchSpecs 断言包级商品下标常量与 `GoodSpecs()` 的**顺序一致**。
//
// 【为什么必须有它】R84 把商品下标提为包级常量（`GoodCoal` / `GoodTools` 等），
// 供 `BuildingSpecsWithCentralBank` 在**函数外**构造金矿配方使用。
// 但这样一来，"配方里的煤是不是 5"就不再是编译器保证的事——
// 若有人调整 `GoodSpecs()` 里 `names` 的顺序而漏改常量，
// **金矿会默默消耗错误的商品**（不 panic、不报错，只是经济含义变了）。
//
// 本测试逐个把常量与 `GoodSpecs()` 的实际名字对齐，把那个静默风险钉死。
func TestGoodIndicesMatchSpecs(t *testing.T) {
	specs := GoodSpecs()
	if len(specs) != Goods {
		t.Fatalf("len(GoodSpecs())=%d，应等于 Goods=%d", len(specs), Goods)
	}
	want := []struct {
		idx  int
		name string
		got  string
	}{
		{GoodGrain, "谷物", specs[GoodGrain].Name},
		{GoodFood, "加工食品", specs[GoodFood].Name},
		{GoodFabric, "织物", specs[GoodFabric].Name},
		{GoodClothes, "服装", specs[GoodClothes].Name},
		{GoodLuxury, "高档服装", specs[GoodLuxury].Name},
		{GoodCoal, "煤", specs[GoodCoal].Name},
		{GoodIron, "铁", specs[GoodIron].Name},
		{GoodSteel, "钢", specs[GoodSteel].Name},
		{GoodTools, "工具", specs[GoodTools].Name},
		{GoodHousing, "住房", specs[GoodHousing].Name},
		{GoodPower, "建造力", specs[GoodPower].Name},
	}
	for _, w := range want {
		if w.name != w.got {
			t.Errorf("下标 %d 的常量名字是 %q，但 GoodSpecs() 实际是 %q —— "+
				"商品表顺序变了而常量没跟着改", w.idx, w.name, w.got)
		}
	}
	// 黄金**不在**商品表里（裁决：外生价格、不进 A 矩阵），且它的下标 == Goods。
	if GoldGood != Goods {
		t.Errorf("GoldGood = %d，应等于 Goods = %d（黄金不占 A 矩阵维数）", GoldGood, Goods)
	}
	// 逐个下标互不相同（防重复定义）
	seen := map[int]string{}
	for _, w := range want {
		if prev, ok := seen[w.idx]; ok {
			t.Errorf("下标 %d 被 %q 与 %q 同时占用", w.idx, prev, w.name)
		}
		seen[w.idx] = w.name
	}
}

// TestCentralBankSpecs 校验 1.2 的**金矿与中央银行**两条建筑规格
// 与 §1.2-5 的原文数值一致，且它们的追加**不改变** 1.0 的规格表。
func TestCentralBankSpecs(t *testing.T) {
	base := BuildingSpecs(1000)
	withBank := BuildingSpecsWithCentralBank(1000, 5000, 0)

	// ① 追加而不是改写：前 len(base) 项必须逐位相同
	if len(withBank) != len(base)+2 {
		t.Fatalf("追加后 %d 项，应为 %d（= %d + 2）", len(withBank), len(base)+2, len(base))
	}
	for i := range base {
		if base[i].Name != withBank[i].Name {
			t.Errorf("下标 %d 被改写：基线 %q → 追加后 %q", i, base[i].Name, withBank[i].Name)
		}
	}
	// ② 金矿（§1.2-5："劳动力 5k、每级消耗 15 煤炭 + 15 工具、产 25 黄金、最多 50 级"）
	gm := withBank[len(base)]
	if gm.Name != "金矿" {
		t.Fatalf("追加项 0 是 %q，应为金矿", gm.Name)
	}
	if !gm.ProducesGold {
		t.Errorf("金矿未标记 ProducesGold")
	}
	if gm.Produces() {
		t.Errorf("金矿的 Produces() 为 true —— 黄金会越出 11 维价格/配给数组")
	}
	if gm.LaborPerLevel != 5000 {
		t.Errorf("金矿每级劳动力 = %g，§1.2-5 规定 5k", gm.LaborPerLevel)
	}
	if gm.Cap != GoldMineCap || GoldMineCap != 50 {
		t.Errorf("金矿上限 = %g / GoldMineCap = %d，§1.2-5 规定 50", gm.Cap, GoldMineCap)
	}
	if gm.Recipe.Output != GoldGood {
		t.Errorf("金矿产出下标 = %d，应为 GoldGood = %d", gm.Recipe.Output, GoldGood)
	}
	if gm.Recipe.Qty != 25 {
		t.Errorf("金矿产出量 = %g，§1.2-5 规定 25 黄金", gm.Recipe.Qty)
	}
	for _, in := range []struct {
		idx  int
		name string
	}{{GoodCoal, "煤"}, {GoodTools, "工具"}} {
		if got := gm.Recipe.Inputs[in.idx]; got != 15 {
			t.Errorf("金矿每级消耗%s = %g，§1.2-5 规定 15", in.name, got)
		}
	}
	if len(gm.Recipe.Inputs) != 2 {
		t.Errorf("金矿投入项数 = %d，应恰为 2（煤、工具）", len(gm.Recipe.Inputs))
	}
	// ③ 中央银行
	cb := withBank[len(base)+1]
	if cb.Name != "中央银行" {
		t.Fatalf("追加项 1 是 %q，应为中央银行", cb.Name)
	}
	if !cb.IsCentralBank {
		t.Errorf("央行未标记 IsCentralBank")
	}
	if cb.Produces() {
		t.Errorf("央行的 Produces() 为 true —— 它没有商品配方")
	}
	if cb.BuildCost != 0 {
		t.Errorf("央行建造成本 = %g，应 0（级数由「黄金有剩余」驱动，不由建造力产生）", cb.BuildCost)
	}
	if cb.LaborPerLevel != 0 {
		t.Errorf("央行每级劳动力 = %g，默认应为 0（登记在案的简化）", cb.LaborPerLevel)
	}
	// ④ 参数默认值对上 §1.2-5
	p := DefaultParams()
	if p.GoldPrice != 10_000 {
		t.Errorf("GoldPrice = %g，§1.2-5 规定 10,000", p.GoldPrice)
	}
	if p.GoldPerMint != 20 {
		t.Errorf("GoldPerMint = %g，§1.2-5 规定 20", p.GoldPerMint)
	}
	if p.MoneyPerMint != 400_000 {
		t.Errorf("MoneyPerMint = %g，§1.2-5 规定 400,000", p.MoneyPerMint)
	}
	// ⑤ 开关默认关闭（1.0 基线保护）
	if p.CentralBankEnabled {
		t.Errorf("CentralBankEnabled 默认为 true —— 会改变 len(specs) 从而改变基线")
	}
}
