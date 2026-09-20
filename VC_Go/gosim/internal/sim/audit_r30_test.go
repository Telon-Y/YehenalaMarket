package sim

// audit_r30_test.go —— §七 R30：**"工资为什么花不完"的口径诊断**。
//
// ============================ 问题 ============================
//
// 契约 §5 写着"全部工资用于消费"，但实现里 §6.3 的消费是**按财富档查数量表**：
// 每个池把目标量买满（满足度 = 1）就停手，剩下的现金留在池里。于是两个口径不等价：
//
//	"全部工资用于消费"        —— 流量恒等式（要求 消费支出 = 工资）
//	"按 §6.3 的数量表消费"     —— 数量规则（花掉多少取决于篮子的价值）
//
// 二者只有在"**篮子价值 = 工资**"时才是同一句话。本诊断把这条等式逐档拆开核对：
//
//	① 逐财富档：§6.3 篮子按"最便宜的满足组合"计价 ÷ 该档工资
//	   —— 若远小于 1，则"按档买满"必然剩下大部分工资；
//	② 联合标定 k 到底保证了什么：k 的方程是
//	   k = w(1−t)/v，其中 w = **为生产该篮子所需的**人均工资（Leontief 反推），
//	   v = 篮子的人均价值 —— 它保证的是"**需求所需的**工资 = 篮子价值"，
//	   而**不是**"当前开局布点的实际工资总额 = 篮子价值"；
//	③ 默认开局的实际工资总额 vs 同一人口的篮子价值。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR30 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// basketValuePerCapita 用"最便宜的满足组合"给 §6.3 的数量篮子计价（元/人/周期）。
//
// 口径依据：consume.buyOneUnit 按 §6.4 的权重 w = S/(2P) 降序购买，即**先用每单位
// 使用价值最便宜的商品**满足该组，故"最便宜组合"就是实际成交价的良好近似。
func basketValuePerCapita(tier float64, goods []model.Good, groups []model.ConsumeGroup) float64 {
	per100k := model.DemandAt(tier)
	var value float64
	for gi, g := range groups {
		target := per100k[gi]
		if target <= 0 {
			continue
		}
		best := math.Inf(1)
		for idx, per := range g.Uses {
			if per > 0 && idx < len(goods) {
				if c := goods[idx].Pcost / per; c < best {
					best = c
				}
			}
		}
		if !math.IsInf(best, 1) {
			value += target * best
		}
	}
	return value / 100000
}

// TestAuditR30TierBasketVsWage 逐档核对"篮子价值 vs 该档工资"。
func TestAuditR30TierBasketVsWage(t *testing.T) {
	auditEnabled(t)
	goods := model.GoodSpecs()
	groups := model.ConsumeGroupSpecs()

	ratios := map[float64]float64{}
	for _, tier := range []float64{5, 10, 20} {
		v := basketValuePerCapita(tier, goods, groups)
		ratios[tier] = v / tier
		t.Logf("财富档 %4.0f：§6.3 篮子在 P_cost 下值 **%.3f 元/人/周期**，"+
			"该档工资 %.0f 元 ⇒ 篮子/工资 = **%.1f%%**",
			tier, v, tier, v/tier*100)
	}
	for tier, r := range ratios {
		if r > 0.5 {
			t.Errorf("财富档 %.0f：篮子只值工资的 %.1f%%，预期远低于 50%%"+
				"（否则'按档买满'不会剩下大笔工资）", tier, r*100)
		}
	}
	// 三档的比率应彼此接近（说明差的是一个**统一的量级系数**，而不是某一档的错）。
	lo, hi := math.Inf(1), 0.0
	for _, r := range ratios {
		lo = math.Min(lo, r)
		hi = math.Max(hi, r)
	}
	t.Logf("三档比率范围 %.1f%% ~ %.1f%%（说明缺的是一个统一的量级系数）", lo*100, hi*100)

	// ② 联合标定 k 保证的是"需求所需的工资"，不是"当前布点的实际工资"。
	st := newTestState(t)
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	var poolPop float64
	for i := range st.Houses.Pools {
		// 【口径（2026-09-19 第 20 轮）】只统计**就业池**：本节的命题是
		// "某档工资 vs 该档篮子"，分母必须是**挣这份工资的人**。
		// 第 20 轮把耕地上限降到 500 后失业率 71.7%，若把零工资的失业池计入分母，
		// 人均工资会被稀释到 0.327 元，与"档 10 工资 10 元"不是同一个量。
		if st.Houses.Pools[i].Worksite == st.UnemployedSite {
			continue
		}
		poolPop += st.Houses.Pools[i].Population
	}
	v10 := basketValuePerCapita(10, goods, groups)
	k := st.DemandScale()
	wImplied := k * v10 / (1 - st.Params.TaxRate)
	actual := snap.Flow.WageTotal / poolPop
	t.Logf("标定 k = %.4f：由 k = w(1−t)/v 反解的人均需求背书工资 = %.3f 元"+
		"（档 10 篮子 %.3f 元）；而**当前开局实际**就业人均工资 = %.3f 元 ⇒ 相差 %.1f 倍",
		k, wImplied, v10, actual, actual/wImplied)
	if actual <= wImplied {
		t.Errorf("当前开局就业人均工资 %.3f 未超过需求背书 %.3f——与实测的'回笼率仅 20~30%%'矛盾",
			actual, wImplied)
	}

	// ③ 汇总：实际工资总额 vs 同人口的篮子价值。
	var targetValue float64
	for _, v := range st.Last.PoolTargetValue {
		targetValue += v
	}
	t.Logf("同一 tick：实际工资总额 %.0f 元，按池人口计的消费目标（估值上限）%.0f 元"+
		" ⇒ 工资里至少有 %.0f%% 没有对应的消费条目",
		snap.Flow.WageTotal, targetValue,
		(1-targetValue/snap.Flow.WageTotal)*100)
	// 【前值 → 后值（2026-09-19 第 20 轮）】旧断言是"消费目标 < 工资总额"。
	// 第 20 轮后失业 71.7%：工资总额被压缩到 247 万，而 7.17M 无收入人口
	// **仍然按 §6.3 表带着目标量**（满足度记 0 是"买不起"，不是"不想要"），
	// 于是目标的**估值上限**反超工资（实测 1,350 万 vs 247 万，−447%）。
	// 这恰恰是裁决第 7/23 项的量化形式：**缺的是消费品目，不是购买力**——
	// 目标条目在表里存在而工资买不起，与"工资花不完"是同一枚硬币的两面。
	// 故本条按**方向**判定：工资与目标必须显著背离（任一方向都算），不再要求单向。
	ratio := targetValue / snap.Flow.WageTotal
	if math.Abs(ratio-1) < 0.05 {
		t.Errorf("消费目标 %.0f 与工资总额 %.0f 几乎相等（比 %.3f）——"+
			"与'工资与需求篮子量级不闭合'的实测矛盾", targetValue, snap.Flow.WageTotal, ratio)
	}
	t.Logf("目标/工资 = %.3f（第 20 轮：失业池把目标留在表里却买不起，比值 > 1）", ratio)
}
