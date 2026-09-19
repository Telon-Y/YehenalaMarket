package sim

// audit_survival_test.go —— 生存性诊断：起点布点与税率对"经济能否自持"的影响
//
// 目的：把"经济为何在 500~1000 周期内归零"拆成可对照的两轴：
//
//	轴一：起始布点（统一 5 级 vs 物质平衡布点）
//	轴二：税率（0.10 vs 0）
//
// 判据（可量化的"存活"）：末 tick 总级数 > 0 且人口年化 ≥ 0。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditSurvival -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/model"
	"yehenala/market/internal/produce"
)

// survivalRun 跑 ticks 个周期，返回末态总级数、末态人口、以及若干抽样点的
// 政府现金池 / 工资 / 成交额。
type survivalPoint struct {
	Tick       int64
	Levels     float64
	Pop        float64
	GovCash    float64
	GovDebt    float64
	Wage       float64
	SpendNet   float64
	SatMin     float64
	AllocMin   float64
	AllocMinGd string
}

func survivalRun(t *testing.T, st *State, ticks int, sampleEvery int) []survivalPoint {
	t.Helper()
	var pts []survivalPoint
	for i := 0; i < ticks; i++ {
		sn, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", i+1, err)
		}
		if (i+1)%sampleEvery != 0 && i != 0 {
			continue
		}
		var lv float64
		for _, l := range sn.Levels {
			lv += l
		}
		// 配给比最小值：指出当期最紧的投入品
		plan := produce.Settle(st.buildingSpecs(), st.levels(), st.hireRates(), st.subsistence())
		allocMin, allocGood := 1.0, "—"
		for g := 0; g < model.Goods; g++ {
			if plan.ProposedInputs[g] <= 1e-9 {
				continue
			}
			if plan.AllocRatio[g] < allocMin {
				allocMin, allocGood = plan.AllocRatio[g], st.Goods[g].Name
			}
		}
		satMin := 1.0
		for _, s := range sn.Sat {
			if s < satMin {
				satMin = s
			}
		}
		pts = append(pts, survivalPoint{
			Tick: sn.Tick, Levels: lv, Pop: sn.Population,
			GovCash: sn.GovCash, GovDebt: sn.GovDebt,
			Wage: sn.WageBill, SpendNet: sn.SpendNet,
			SatMin: satMin, AllocMin: allocMin, AllocMinGd: allocGood,
		})
	}
	return pts
}

func TestAuditSurvival(t *testing.T) {
	auditEnabled(t)

	type cfg struct {
		name      string
		initLevel float64
		tax       float64
	}
	cases := []cfg{
		{"统一5级  t=0.10", 5, 0.10},
		{"统一5级  t=0.00", 5, 0.00},
		{"物质平衡 t=0.10", 0, 0.10},
		{"物质平衡 t=0.00", 0, 0.00},
	}
	const ticks, sample = 600, 100

	fmt.Printf("\n人口 %.0f，起始等级 %s，共跑 %d 周期\n",
		1e7, "见下", ticks)
	for _, c := range cases {
		st, err := New(Options{
			Population:           10_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  c.initLevel,
		})
		if err != nil {
			t.Fatalf("%s: %v", c.name, err)
		}
		st.Params.TaxRate = c.tax
		var lv0 float64
		for _, b := range st.Buildings {
			lv0 += b.Level
		}
		pts := survivalRun(t, st, ticks, sample)

		fmt.Printf("\n──────────────── %s ────────────────\n", c.name)
		fmt.Printf("起始总级数 = %.1f   起始人口 = %.0f\n", lv0, st.Population)
		fmt.Printf("%6s %10s %12s %14s %14s %13s %13s %8s %16s\n",
			"tick", "总级数", "人口", "政府现金池", "政府债务", "工资总额", "税前成交", "最低满足", "最紧投入(配给比)")
		for _, p := range pts {
			fmt.Printf("%6d %10.1f %12.0f %14.0f %14.0f %13.0f %13.0f %8.3f %14s(%.3f)\n",
				p.Tick, p.Levels, p.Pop, p.GovCash, p.GovDebt,
				p.Wage, p.SpendNet, p.SatMin, p.AllocMinGd, p.AllocMin)
		}
		last := pts[len(pts)-1]
		alive := last.Levels > 0
		fmt.Printf("  ⇒ 末态总级数 %.1f（起始 %.1f，留存 %.1f%%）  存活=%v\n",
			last.Levels, lv0, 100*last.Levels/lv0, alive)
	}
}
