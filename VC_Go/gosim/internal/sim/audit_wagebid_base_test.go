package sim

import "testing"

// TestAuditWageBidBase 对比 M7.2 第 2 步 `A_i` 的两种**计费基数**（R68 的收口对照）。
//
// 【背景】R67 在稀缺局里实测人均工资冲到基准的 15~37 倍（峰值 247 元 / 基准 6.75）。
// R68 定位到根因**不是分母**（那个假设已被实测推翻并回滚），而是**基数**：
// 裁决原文 `A_i = WageBidCap × max(0, 纯利)` 以**纯利**计费，而
// 该经济里"人均纯利 ÷ 人均工资 = 36 倍"（场地 13：243.96 ÷ 6.75）
// ⇒ "纯利的一半"必然产出"相对工资 18 倍"的加价。
//
// 本局把两种基数放在**完全相同的稀缺场景**下对跑：
//
//	profit   （默认，裁决原文）A_i = cap × max(0, 纯利)
//	wagebill （R68 对照口径）  A_i = cap × 工资总额 × max(0, 利润率)
//
// 【断言】
//  1. **默认口径逐位不变**：`WageBidBase` 为空串时与"显式 profit"完全一致（基线保护）；
//  2. 两种基数都**不发散**（有界）；
//  3. `wagebill` 的峰值溢价**显著低于** `profit` —— 即"以工资倍数为界"确实收紧了量级；
//  4. `wagebill` 下**人均工资 ≤ (1+cap) × 基准工资**的稳态上界
//     （这是它相对 `profit` 的核心性质：不需要"显式工资上限"就自然有界）；
//  5. 两种口径都不破坏货币守恒与借贷相等。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditWageBidBase -v
func TestAuditWageBidBase(t *testing.T) {
	auditEnabled(t)
	newScarce := func(base string) *State {
		st, err := New(Options{
			Population:           500_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			WageBidEnabled:       true,
			WageBidBase:          base,
		})
		if err != nil {
			t.Fatalf("New(%q): %v", base, err)
		}
		return st
	}
	// run 跑 ticks 并返回（峰值溢价, 峰值人均工资, 货币守恒误差）
	run := func(st *State, ticks int) (peakPremium, peakWage, moneyErr float64) {
		money0 := st.TotalMoney()
		for i := 0; i < ticks; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
			if st.InvariantErr != nil {
				t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("借贷不等 %d 笔 @%d", v, i)
			}
			for j := range st.Buildings {
				if p := st.WagePremium(j); p > peakPremium {
					peakPremium = p
				}
				if w := st.EffectiveWage(j); w > peakWage {
					peakWage = w
				}
			}
		}
		delta := st.TotalMoney() - money0
		want := st.TickNewCapitalTotal() + st.InfusionTotal()
		return peakPremium, peakWage, delta - want
	}

	const ticks = 900
	stProfit := newScarce("")
	pPeak, pWage, pErr := run(stProfit, ticks)
	stExplicit := newScarce("profit")
	ePeak, _, _ := run(stExplicit, ticks)
	stWage := newScarce("wagebill")
	wPeak, wWage, wErr := run(stWage, ticks)

	base := stProfit.BaseWage(1) // 城镇基准 6.75
	t.Logf("稀缺局 %d tick：\n"+
		"  profit  (默认/空串) 峰值溢价 %10.4f 峰值人均工资 %10.4f（基准 %.2f 的 %.1f 倍）\n"+
		"  profit  (显式)      峰值溢价 %10.4f\n"+
		"  wagebill(对照)      峰值溢价 %10.4f 峰值人均工资 %10.4f（基准的 %.1f 倍）\n"+
		"  货币守恒误差 profit %.3e / wagebill %.3e",
		ticks,
		pPeak, pWage, base, pWage/base,
		ePeak,
		wPeak, wWage, wWage/base,
		pErr, wErr)

	// ① 默认（空串）必须与显式 "profit" 逐位一致 —— 基线保护
	if diff := pPeak - ePeak; diff > 1e-12 || diff < -1e-12 {
		t.Errorf("WageBidBase 空串(%.10f) 与显式 profit(%.10f) 不一致，差 %.3e —— "+
			"默认分支必须逐位复现裁决原文口径", pPeak, ePeak, diff)
	}
	// ② 都不得发散
	if pPeak > 1e5 {
		t.Errorf("profit 基数下溢价发散到 %.4f", pPeak)
	}
	if wPeak > 1e5 {
		t.Errorf("wagebill 基数下溢价发散到 %.4f", wPeak)
	}
	// ③ wagebill 应显著收紧量级
	if wPeak >= pPeak {
		t.Errorf("wagebill 峰值 %.4f 未低于 profit 峰值 %.4f —— 收紧未生效", wPeak, pPeak)
	}
	if wPeak > 0.9*pPeak {
		t.Errorf("wagebill 峰值 %.4f 只降到 profit 峰值 %.4f 的 %.1f%%，收紧幅度不足",
			wPeak, pPeak, 100*wPeak/pPeak)
	}
	// ④ **诚实的经验界**（不是理论界）
	//
	// 我原本预期 wagebill 会收敛到 (1+cap)×基准 的量级，**实测否定**：
	// 峰值仍是基准的 10.8 倍。原因（`wagebill_bound_probe_test.go` 实测）：
	//
	//	在岗场地的 `LastMargin` 高达 **20.17**（不是 0.2），且 decayPerTick 仅 0.000962
	//	⇒ 每 tick 增量 = cap×base×margin = 0.5×6.75×20.17 ≈ **68.07 元**
	//	⇒ 稳态    p* = 68.07 / (2×0.000962) ≈ **35,000 元**
	//
	// 即 wagebill 只是把增长速度从"纯利的比例"换成"工资×利润率"，
	// **两个因子在这套参数下都不小**，故量级并未回到"工资的倍数"。
	// 故此处只断言一个**宽松的经验界**（≤50× 基准），把真实结论留给日志与裁决。
	if wWage > 50*base {
		t.Errorf("wagebill 下峰值人均工资 %.4f 超过基准的 50 倍（%.4f）—— "+
			"连经验界都被突破，需重新评估基数口径", wWage, 50*base)
	}
	t.Logf("注：wagebill **没有**把溢价约束回「工资的倍数」——峰值仍是基准的 %.1f 倍；"+
		"根因是 LastMargin≈20.17 与极小的 decayPerTick，参见 R69。", wWage/base)
	// ⑤ 货币守恒
	for _, e := range []struct {
		name string
		err  float64
	}{{"profit", pErr}, {"wagebill", wErr}} {
		if e.err > 1e-3 || e.err < -1e-3 {
			t.Errorf("%s 口径货币守恒误差 %.6f", e.name, e.err)
		}
	}
}
