package sim

import "testing"

// TestAuditWageBidScarcity 校验 1.2 M7 工资竞标在**真正稀缺**劳动市场上的抬价与回落路径。
//
// 【为什么要另立一局】TestAuditWageBid 用的默认参数劳动力**结构性过剩**
// （失业 ~18 万），M7.2 裁决①"只在缺员时抬价"使得溢价恒为 0 —— 抬价路径
// 从未被真正执行过。本局构造 labor demand > population 的对照场景，把
// M7.2 的第 2/3 步（抬价额 = cap × 纯利 ÷ 在岗人数）与裁决②（回落/衰减）
// 全部跑通。
//
// 【稀缺场景怎么来的】探针实测（scarce_probe_test.go）：
//
//	人口 50 万时，tick 140 起市场用工 > 人口，比值最高 1.19 ⇒ 出现缺员。
//	对照组：人口 100 万 → 比值 0.56、人口 500 万 → 比值 0.14，均无缺员。
//
// 【本局断言的四条】
//  1. **举价**：出现缺员后，缺员场地的溢价确实 > 0（抬价路径可达）；
//  2. **口径自洽**：实际人均工资 = 基准 + 溢价，且溢价非负；
//  3. **不发散（裁决②的本意：不许棘轮）**：4000 tick 内溢价**有界**，且
//     在稀缺缓解时**确实回落**（存在显著的下行段，而不是单调爬升）；
//  4. **货币守恒与借贷相等**在整局中不被破坏。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditWageBidScarcity -v
func TestAuditWageBidScarcity(t *testing.T) {
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

	const scarceTicks = 900
	firstShort, firstPremium := -1, -1
	var peakPremium, maxWage float64
	for i := 0; i < scarceTicks; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.InvariantErr != nil {
			t.Fatalf("不变量被破坏 @tick %d: %v", i, st.InvariantErr)
		}
		if v := len(st.Aud.Violations()); v != 0 {
			t.Fatalf("借贷不等 %d 笔 @tick %d", v, i)
		}
		short := false
		var tickMax float64
		for j := range st.Buildings {
			if j < len(st.wageShortTick) && st.wageShortTick[j] {
				short = true
			}
			p := st.WagePremium(j)
			if p < -1e-12 {
				t.Fatalf("溢价为负 %.6g（场地 %d @tick %d）——与 M7.2 第 3 步矛盾", p, j, i)
			}
			if w, base := st.EffectiveWage(j), st.BaseWage(j); w < base-1e-12 {
				t.Fatalf("实际工资 %.6f < 基准 %.6f（场地 %d @tick %d）", w, base, j, i)
			}
			if p > tickMax {
				tickMax = p
			}
			if w := st.EffectiveWage(j); w > maxWage {
				maxWage = w
			}
		}
		if short && firstShort < 0 {
			firstShort = i
		}
		if tickMax > 1e-9 && firstPremium < 0 {
			firstPremium = i
		}
		if tickMax > peakPremium {
			peakPremium = tickMax
		}
	}
	t.Logf("稀缺段 %d tick：首缺员 tick %d、首溢价 tick %d、峰值溢价 %.4f、最大人均工资 %.4f、人口 %.0f、配给比 %.4f",
		scarceTicks, firstShort, firstPremium, peakPremium, maxWage, st.Population, st.LaborMarketRatio)

	// ① 稀缺确实发生，且举价路径可达
	if firstShort < 0 {
		t.Fatalf("人口 50 万未出现任何缺员 —— 对照场景失效，本局无法验证抬价路径")
	}
	if firstPremium < 0 || peakPremium <= 1e-9 {
		t.Fatalf("缺员（首 tick %d）但溢价始终为 0 —— M7.2 第 2 步抬价未执行", firstShort)
	}
	if peakPremium <= 2*6.75 {
		t.Errorf("峰值溢价 %.4f 未达基准工资(6.75)的两倍 —— 抬价幅度不足", peakPremium)
	}

	// ② 稀缺缓解 ⇒ 必须回落（裁决②的反棘轮面）。
	//
	// 【为什么用注入人口 + 撤除耕地约束】人口 50 万时配给比长期 < 1（实测
	// 0.48–0.91），缺员**永不消除** ⇒ 回落路径在自然局内不可达。这里把
	// 人口放大到 2000 万（远超全部场地申报用工）并撤除耕地上限，使
	// `wageShortTick` 全为 false，从而真正检验"招满即回落 + 缓慢衰减"。
	st.Population = 20_000_000
	st.Params.ArableCap = 1e9
	peakBefore := peakPremium
	var afterRelief float64
	const reliefTicks = 400
	for i := 0; i < reliefTicks; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("缓解段 Step %d: %v", i, err)
		}
		if st.InvariantErr != nil {
			t.Fatalf("缓解段不变量被破坏 @tick %d: %v", i, st.InvariantErr)
		}
		var tickMax float64
		for j := range st.Buildings {
			if p := st.WagePremium(j); p > tickMax {
				tickMax = p
			}
		}
		afterRelief = tickMax
	}
	t.Logf("缓解段 %d tick（人口 2000 万）：峰值 %.4f → 末溢价 %.4f（降到 %.3f%%）",
		reliefTicks, peakBefore, afterRelief, 100*afterRelief/peakBefore)

	if afterRelief >= 0.4*peakBefore {
		t.Errorf("稀缺缓解后溢价仍为峰值的 %.1f%%（%.4f → %.4f）—— "+
			"M7.2 裁决②（招满即回落 + 缓慢衰减）反棘轮失效",
			100*afterRelief/peakBefore, peakBefore, afterRelief)
	}

	// ④ 货币守恒
	delta := st.TotalMoney() - money0
	want := st.TickNewCapitalTotal() + st.InfusionTotal()
	if diff := delta - want; diff > 1e-3 || diff < -1e-3 {
		t.Errorf("货币守恒：Δ = %.4f，应为 %.4f（差 %.6f）", delta, want, diff)
	}
}
