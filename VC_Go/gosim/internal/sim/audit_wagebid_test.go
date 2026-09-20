package sim

import (
	"math"
	"testing"
)

// TestAuditWageBid 校验 1.2 M7 工资竞标（默认关闭）的六条不变量。
//
// 覆盖：
//  1. **关闭时必须与 1.0 逐位一致**——`wagePremium` 恒为 0、实际工资 = 基准工资；
//  2. 开启后若**无稀缺**（有失业），按 M7.2 裁决①"只在缺员时抬价" ⇒ 溢价仍为 0；
//  3. 开启后机制可运行（不 panic、不破坏不变量与货币守恒）；
//  4. 溢价**非负**（M7.2 第 3 步）；
//  5. 实际人均工资 = 基准 + 溢价（M7.2 第 1/3 步的口径）；
//  6. 各场地的**基准工资**与 1.0 的劳动结构一致（城镇 6.75 / 农业 5.65）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditWageBid -v
func TestAuditWageBid(t *testing.T) {
	auditEnabled(t)
	newOpts := func(bid bool) Options {
		return Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			WageBidEnabled:       bid,
		}
	}

	// ① 关闭时：溢价恒 0、实际工资 = 基准
	off, err := New(newOpts(false))
	if err != nil {
		t.Fatalf("构造（关闭）: %v", err)
	}
	for i := 0; i < 200; i++ {
		if _, err := off.Step(); err != nil {
			t.Fatalf("Step: %v", err)
		}
	}
	for j := range off.Buildings {
		if p := off.WagePremium(j); p != 0 {
			t.Fatalf("WageBidEnabled=false 时场地 %d 溢价 = %g，应为 0", j, p)
		}
		if w, base := off.EffectiveWage(j), off.BaseWage(j); math.Abs(w-base) > 1e-12 {
			t.Fatalf("关闭时实际工资 %.6f ≠ 基准 %.6f（场地 %d）", w, base, j)
		}
	}
	// ⑥ 基准工资与 §5 的劳动结构一致（农业 5.65、城镇 6.75）
	if w := off.BaseWage(0); math.Abs(w-5.65) > 1e-9 {
		t.Errorf("谷物农场基准工资 = %.4f，应为 5.65（§5 农业结构）", w)
	}
	if w := off.BaseWage(1); math.Abs(w-6.75) > 1e-9 {
		t.Errorf("加工食品厂基准工资 = %.4f，应为 6.75（§5 城镇结构）", w)
	}

	// ③④⑤ 开启时：机制可运行、溢价非负、口径自洽
	on, err := New(newOpts(true))
	if err != nil {
		t.Fatalf("构造（开启）: %v", err)
	}
	moneyBefore := on.TotalMoney()
	for i := 0; i < 200; i++ {
		if _, err := on.Step(); err != nil {
			t.Fatalf("Step: %v", err)
		}
		if on.InvariantErr != nil {
			t.Fatalf("不变量被破坏：%v", on.InvariantErr)
		}
		if v := len(on.Aud.Violations()); v != 0 {
			t.Fatalf("借贷不等违规 %d 笔", v)
		}
		for j := range on.Buildings {
			if p := on.WagePremium(j); p < 0 {
				t.Fatalf("溢价为负 %.6g（场地 %d）——与 M7.2 第 3 步矛盾", p, j)
			}
			if w, base := on.EffectiveWage(j), on.BaseWage(j); w < base-1e-12 {
				t.Fatalf("实际工资 %.6f < 基准 %.6f（场地 %d）", w, base, j)
			}
		}
	}
	delta := on.TotalMoney() - moneyBefore
	want := on.TickNewCapitalTotal() + on.InfusionTotal()
	if diff := delta - want; diff > 1e-3 || diff < -1e-3 {
		t.Errorf("货币守恒：Δ = %.4f，应为 %.4f（差 %.4f）", delta, want, diff)
	}

	// ② 无稀缺 ⇒ 无溢价（裁决①的字面后果）
	var maxP float64
	var nShort int
	for j := range on.Buildings {
		if p := on.WagePremium(j); p > maxP {
			maxP = p
		}
		if j < len(on.wageShortTick) && on.wageShortTick[j] {
			nShort++
		}
	}
	t.Logf("开启 200 tick 后：最大溢价 %.6f、缺员场地 %d、失业 %.0f、自给率 %.4f",
		maxP, nShort, on.Unemployed, on.SubsistenceHireRate)
	if on.Unemployed > 0 && nShort == 0 && maxP > 0 {
		t.Errorf("有失业（%.0f）但仍有缺员溢价 %.6f——与 M7.2 裁决①（只在缺员时抬价）矛盾",
			on.Unemployed, maxP)
	}
	if maxP == 0 {
		t.Logf("提示：本局**无劳动力稀缺**（失业 %.0f），故竞标按设计不产生溢价 —— "+
			"这是裁决①的正确后果，不是实现缺陷；稀缺场景的溢价抬升路径需另行构造对照", on.Unemployed)
	}
}
