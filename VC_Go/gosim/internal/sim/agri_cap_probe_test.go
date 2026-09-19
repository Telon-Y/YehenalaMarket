package sim

// agri_cap_probe_test.go —— 「农业为什么恒定」的上限诊断（§7.2 / §4.2）。
//
// 用户提问：实际产出表里农业一项在 tick ~3,000 之后完全不动，这是"到达上限"了吗？
//
// 农业实际增加值（§7.2）只有两个来源：
//
//	① 专业农业 = 农村类建筑（谷物农场 #0 / 棉花种植园 #2）的实物产出 × P_ref；
//	② 自给农场 = 未使用耕地 × SubsistenceScale 的产出 × P_ref，再按【自给农场雇佣率】缩放。
//
// 而自给农场是**备用劳动力池**（§4.2 修订的就业顺序）：
//
//	一般生产建筑（含金融区/庄园/仓库）先占人 → 余量给自给农场 → 仍有余量才是失业。
//
// 于是"农业恒定"只有三种互斥的可能：
//
//	(A) 耕地耗尽：未使用耕地 → 0（但上限 5,000、专业农业只用个位数~两位数级）；
//	(B) 自给农场撞到**满编上限**：雇佣率被钳在 1，且未使用耕地不再变化
//	    ⇒ 产出 = 固定上限，与人口、投资、价格全都无关；
//	(C) 雇佣率 < 1 且恒定（劳动余量恰好恒定）⇒ 是"劳动被工业抽走"，不是上限。
//
// 本探针把 未使用耕地 / 自给容量 / 劳动余量 / 雇佣率 / 自给产出 / 专业农业产出
// 逐项并列打印，直接分辨这三者；第二张表再把**专业农业自身的雇佣率**与
// 谷物/织物的价格比（当期价 ÷ P_ref）并列，回答"专业农业为什么是 0"。
//
// 运行：go test ./internal/sim/ -run TestPrintAgriCapacityLimit -v

import (
	"fmt"
	"testing"
)

// TestPrintAgriCapacityLimit 打印农业的两条来源与三个可能的约束量。
func TestPrintAgriCapacityLimit(t *testing.T) {
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
	ref := st.refPrices()
	// 1 单位自给农场的产出 = 谷物 2 / 织物 1 / 服装 0.5（§3.3，已约去自给农自身消费）。
	// 用它把自给产出的**实物单位数**从价值里反解出来，才能和耕地/劳动上限直接比较。
	unitValue := 2*ref[0] + 1*ref[2] + 0.5*ref[3]

	fmt.Printf("参考价：谷物 %.4f、织物 %.4f、服装 %.4f ⇒ 1 单位自给产出 = %.4f 元\n",
		ref[0], ref[2], ref[3], unitValue)
	fmt.Printf("耕地上限 ArableCap = %.0f；自给农场规模 = 未使用耕地 × %.1f；每级需 %.0f 人\n",
		st.Params.ArableCap, st.Params.SubsistenceScale, st.Params.SubsistenceLaborPerLevel)
	fmt.Printf("价格钳制带：下限比 %.2f、上限比 %.2f（§3.4，相对当期 P⁰）\n\n",
		st.Goods[0].PriceFloorRatio, st.Goods[0].PriceCeilRatio)

	sample := map[int64]bool{1: true, 52: true, 260: true, 520: true,
		1000: true, 2000: true, 3000: true, 5000: true, 7000: true, 10000: true}

	type row struct {
		tick                           int64
		pop, market, reserve, capacity float64
		idle, hire                     float64
		subsUnits, proAgri, agri, ind  float64
		farmHire, cottonHire           float64
		grainRatio, fabricRatio        float64
	}
	var rows []row

	var (
		lastAgri       = -1.0
		lastAgriChange int64
		lastIdle       = -1.0
		lastIdleChange int64
		minAgri        float64
		minAgriTick    int64
		hireAtCapFrom  int64 = -1
		belowCapTicks  int
		lastBelowCap   int64
		farmDeadFrom   int64 = -1
	)
	for i := 0; i < 10000; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}

		// 市场用工 = Σ 一般生产建筑 级数 × 雇佣率 × 每级劳动（含金融区/庄园/仓库）
		var market float64
		for j, b := range st.buildingSpecs() {
			market += st.Buildings[j].Level * st.Buildings[j].HireRate * b.LaborPerLevel
		}
		// 未使用耕地（专业农业占用后的余量，§4.2）
		idle := st.Params.ArableCap
		for j := range st.Buildings {
			if st.Buildings[j].Spec.LandKind == "arable" {
				idle -= st.Buildings[j].Level
			}
		}
		capacity := st.subsistenceFarmLevels() * st.Params.SubsistenceLaborPerLevel
		reserve := st.Population - market

		// 记录"最后一次变化"的 tick：这就是"农业恒定"的起点。
		if snap.ProductAddedAgri != lastAgri {
			lastAgriChange, lastAgri = snap.Tick, snap.ProductAddedAgri
		}
		if idle != lastIdle {
			lastIdleChange, lastIdle = snap.Tick, idle
		}
		if minAgri == 0 || snap.ProductAddedAgri < minAgri {
			minAgri, minAgriTick = snap.ProductAddedAgri, snap.Tick
		}
		if hireAtCapFrom < 0 && st.SubsistenceHireRate >= 1-1e-12 {
			hireAtCapFrom = snap.Tick
		}
		if hireAtCapFrom > 0 && st.SubsistenceHireRate < 1-1e-12 {
			belowCapTicks++
			lastBelowCap = snap.Tick
		}
		// 专业谷物农场的雇佣率跌破 1% 的时点（"专业农业停摆"）
		if farmDeadFrom < 0 && st.Buildings[0].HireRate < 0.01 {
			farmDeadFrom = snap.Tick
		}

		if !sample[snap.Tick] {
			continue
		}
		rows = append(rows, row{
			tick: snap.Tick, pop: st.Population, market: market, reserve: reserve,
			capacity: capacity, idle: idle, hire: st.SubsistenceHireRate,
			subsUnits: snap.ProductSubsistence / unitValue,
			proAgri:   snap.GrossAgri - snap.ProductSubsistence,
			agri:      snap.ProductAddedAgri, ind: snap.ProductAddedIndustry,
			farmHire: st.Buildings[0].HireRate, cottonHire: st.Buildings[2].HireRate,
			grainRatio: snap.Prices[0] / ref[0], fabricRatio: snap.Prices[2] / ref[2],
		})
	}

	fmt.Printf("【表 1】自给农场的两个约束（耕地/劳动）\n")
	fmt.Printf("%6s %11s %11s %11s %11s %9s %9s %10s %11s %11s %11s\n",
		"tick", "人口", "市场用工", "劳动余量", "自给容量", "未用耕地", "自给雇佣率",
		"自给单位数", "专业农业", "农业增加值", "工业增加值")
	for _, r := range rows {
		fmt.Printf("%6d %11.0f %11.0f %11.0f %11.0f %9.2f %9.4f %10.1f %11.0f %11.0f %11.0f\n",
			r.tick, r.pop, r.market, r.reserve, r.capacity, r.idle, r.hire,
			r.subsUnits, r.proAgri, r.agri, r.ind)
	}

	fmt.Printf("\n【表 2】专业农业的雇佣率与价格比（当期价 ÷ P_ref；专业谷物农场 #0、棉花 #2）\n")
	fmt.Printf("%6s %14s %14s %14s %14s\n",
		"tick", "谷物农场雇佣率", "棉花种植园雇佣率", "谷物价/P_ref", "织物价/P_ref")
	for _, r := range rows {
		fmt.Printf("%6d %14.4f %14.4f %14.4f %14.4f\n",
			r.tick, r.farmHire, r.cottonHire, r.grainRatio, r.fabricRatio)
	}

	// 专业农业的明细（末态）：级数与雇佣率分离，才能看出它是"没有产能"还是"没人干活"。
	// 表中"级数×雇佣率"是**未施加配给折减**的产出；与末态"专业农业（参考价）"对照，
	// 差额就是 §3.3 的短缺惩罚。
	fmt.Printf("\n【表 3】末态专业农业明细（农村类，§3.2）\n")
	fmt.Printf("%-12s %10s %10s %16s\n", "建筑", "级数", "雇佣率", "级数×雇佣率产出")
	for j, b := range st.buildingSpecs() {
		if b.LandKind != "arable" {
			continue
		}
		v := st.Buildings[j].Level * st.Buildings[j].HireRate * b.Recipe.Qty
		fmt.Printf("%-12s %10.2f %10.4f %16.1f\n", b.Name, st.Buildings[j].Level, st.Buildings[j].HireRate, v)
	}

	fmt.Printf("\n结论量：\n")
	fmt.Printf("  未使用耕地最后一次变化：tick %d（此后冻结在 %.4f，即专业农业占用 %.4f 级）\n",
		lastIdleChange, lastIdle, st.Params.ArableCap-lastIdle)
	fmt.Printf("  自给农场雇佣率首次钳到 1（满编）：tick %d\n", hireAtCapFrom)
	fmt.Printf("  此后雇佣率 < 1 的 tick 数：%d（最后一次 tick %d）\n", belowCapTicks, lastBelowCap)
	fmt.Printf("  农业增加值最后一次变化：tick %d；最小 %.0f（tick %d）；末态 %.0f\n",
		lastAgriChange, minAgri, minAgriTick, lastAgri)
	fmt.Printf("  专业谷物农场雇佣率跌破 1%%：tick %d\n", farmDeadFrom)
	fmt.Printf("  末态人口 %.0f、失业 %.0f、自给雇佣率 %.6f\n",
		st.Population, st.Unemployed, st.SubsistenceHireRate)
	// 自给产出 vs 专业满编产出：解释专业农业为何无人可雇却招不到人。
	fmt.Printf("  末态自给产出 vs 专业满编产能：谷物 %.1f vs %.1f 单位、织物 %.1f vs %.1f 单位\n",
		2*st.SubsistenceLevels, st.Buildings[0].Level*50,
		st.SubsistenceLevels, st.Buildings[2].Level*45)
}
