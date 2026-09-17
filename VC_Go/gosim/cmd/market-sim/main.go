// Command market-sim 运行 1.0 生产与市场模拟（含政府 / 资本层）。
//
// 用法：
//
//	market-sim -ticks 10000 -pop 10000000 -tax 0.10
//	market-sim -ticks 10000 -calibrate-only
//
// 输出：§3.4 标定结果、运行期时序摘要、§8.4 验收判据 A1–A6 的判定。
package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	"yehenala/market/internal/model"
	"yehenala/market/internal/report"
	"yehenala/market/internal/sim"
)

func main() {
	ticks := flag.Int("ticks", 10000, "运行的周期数（1 tick = 1 周）")
	pop := flag.Float64("pop", 10_000_000, "开局人口")
	tier := flag.Float64("tier", 10, "§6.3 需求量表所取的财富档")
	tax := flag.Float64("tax", 0.10, "政府税率（G1，对全部交易额征收）")
	ctrl := flag.Float64("ctrl", 5, "每级金融区掌控的其余建筑级数（G5）")
	financeLabor := flag.Float64("finance-labor", 1000, "金融区每级雇佣人数（G5）")
	financeCost := flag.Float64("finance-cost", 400, "金融区建造成本（建造力）")
	demandScale := flag.Float64("k", 0, "§6.3 需求缩放系数；0 表示用标定解")
	govMarkup := flag.Float64("gov-markup", 0, "政府出售建造力的加价率（G2/G6）")
	govStartup := flag.Float64("gov-startup", 0.5, "政府现金池初值占债务上限的比例（G7）")
	subsist := flag.Float64("subsist", 0, "诊断性覆盖自给农场规模；0 表示用契约默认值 0.05")
	powerInit := flag.Float64("power-init", 0, "诊断性覆盖建造部门起步等级；0 表示用建模默认值 20")
	calibrateOnly := flag.Bool("calibrate-only", false, "只做标定并打印结果")
	privatize := flag.Bool("privatize", false, "开启私有化机制（§4.5.1 修订；默认关闭）")
	initLevel := flag.Float64("init-level", -1, "每种生产建筑的起始等级：-1 = 契约默认 5；0 = 物质平衡布点；>0 = 统一该等级")
	flag.Parse()

	out := &strings.Builder{}
	defer func() { fmt.Print(out.String()) }()

	// 标定（§3.4 + 三表联合标定）
	specs := model.BuildingSpecs(*financeLabor, *financeCost)
	cal, err := sim.CalibrateOnly(specs, *demandScale)
	if err != nil {
		fmt.Fprintf(os.Stderr, "标定失败: %v\n", err)
		os.Exit(1)
	}
	goods := model.GoodSpecs()
	report.PrintCalibration(out, goods, cal.Margins, cal.SpectralRadius, cal.DemandScale)

	if *calibrateOnly {
		return
	}

	// 构造仿真
	st, err := sim.New(sim.Options{
		Population:           *pop,
		WealthTier:           *tier,
		DemandScale:          cal.DemandScale,
		FinanceLaborPerLevel: *financeLabor,
		FinanceBuildCost:     *financeCost,
		GovStartupFraction:   *govStartup,
		SubsistenceScale:     optionalFloat(*subsist),
		InitialPowerLevel:    *powerInit,
		ProductionInitLevel:  *initLevel,
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "初始化失败: %v\n", err)
		os.Exit(1)
	}
	st.Params.TaxRate = *tax
	st.Params.ControlPerFinance = *ctrl
	st.Params.GovPowerMarkup = *govMarkup
	// §4.5.1 修订：私有化机制（默认关闭，可用 -privatize 开启）
	st.Params.PrivatizeEnabled = *privatize

	fmt.Fprintf(out, "\n--- 开局状态 ---\n")
	printLayout(out, st)

	// 运行
	snaps, err := st.Run(*ticks)
	if err != nil {
		fmt.Fprintf(os.Stderr, "运行失败: %v\n", err)
		fmt.Fprint(os.Stderr, out.String())
		os.Exit(1)
	}

	// 时序摘要（抽样若干 tick）
	fmt.Fprintf(out, "\n--- 时序摘要 ---\n")
	fmt.Fprintf(out, "%8s %12s %8s %7s %16s %13s %12s %13s %13s\n",
		"tick", "人口", "总级数", "建造力", "政府现金池", "税收", "经营净额", "政府债务", "债务上限")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		var lv, pw float64
		for j, l := range sn.Levels {
			lv += l
			if j == 10 {
				pw = l
			}
		}
		fmt.Fprintf(out, "%8d %12.0f %8.0f %7.1f %16.0f %13.0f %12.0f %13.0f %13.0f\n",
			sn.Tick, sn.Population, lv, pw, sn.GovCash, sn.Tax, sn.Flow.GovOperating,
			sn.GovDebt, sn.GovDebtCap)
	}

	// 资金流分解：诊断"政府现金池为何被砸穿"
	fmt.Fprintf(out, "\n--- 资金流分解（抽样 tick）---\n")
	fmt.Fprintf(out, "%8s %13s %13s %13s %13s %13s %13s %10s\n",
		"tick", "工资总额", "消费支出", "私有纯利", "政府经营", "税收", "购力支出", "售力收入")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		f := sn.Flow
		fmt.Fprintf(out, "%8d %13.0f %13.0f %13.0f %13.0f %13.0f %13.0f %10.0f\n",
			sn.Tick, f.WageTotal, f.SpendNet, f.CapitalProfit, f.GovOperating,
			f.GovTax, f.GovPowerSpend, f.GovPowerRevenue)
	}

	// 部门利润率分布：定位亏损来源
	fmt.Fprintf(out, "\n--- 部门利润率（抽样 tick，单位 %%）---\n")
	fmt.Fprintf(out, "%8s", "tick")
	for _, g := range goods {
		fmt.Fprintf(out, "%9s", g.Name)
	}
	fmt.Fprintf(out, "\n")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		fmt.Fprintf(out, "%8d", sn.Tick)
		for j := range goods {
			m := 0.0
			if j < len(sn.Margins) {
				m = sn.Margins[j] * 100
			}
			fmt.Fprintf(out, "%9.0f", m)
		}
		fmt.Fprintf(out, "\n")
	}

	// 终态
	fmt.Fprintf(out, "\n--- 终态 ---\n")
	printTerminal(out, snaps[len(snaps)-1], goods)

	// §8.4 判定
	sum := report.Assess(snaps, goods, st.Params)
	sum.Print(out)
}

func optionalFloat(v float64) *float64 {
	if v == 0 {
		return nil
	}
	return &v
}

func sampleTick(i, total int) bool {
	marks := map[int]bool{0: true, 51: true, 259: true, 519: true, 999: true, 2999: true, 4999: true, 9999: true}
	if marks[i] {
		return true
	}
	return i == total-1
}

func printLayout(out *strings.Builder, st *sim.State) {
	fmt.Fprintf(out, "%-12s %10s %10s %10s\n", "建筑", "等级", "政府级", "私有级")
	var total float64
	for i := range st.Buildings {
		b := &st.Buildings[i]
		total += b.Level
		fmt.Fprintf(out, "%-12s %10.1f %10.1f %10.1f\n", b.Spec.Name, b.Level, b.GovLevel, b.PrivLevel)
	}
	fmt.Fprintf(out, "%-12s %10.1f\n", "合计", total)
	fmt.Fprintf(out, "政府现金池 %.0f   资本现金池 %.0f   人群现金池 %.0f   货币总量 %.0f\n",
		st.Gov.Cash.Balance(), st.Cap.Cash.Balance(),
		st.Houses.TotalCash(), st.TotalMoney())
}

func printTerminal(out *strings.Builder, sn *sim.Snapshot, goods []model.Good) {
	fmt.Fprintf(out, "%-10s %10s %10s %10s\n", "商品", "价格", "P/Pcost", "利润率")
	for i, g := range goods {
		m := 0.0
		if i < len(sn.Margins) {
			m = sn.Margins[i]
		}
		fmt.Fprintf(out, "%-10s %10.1f %10.3f %9.2f%%\n", g.Name, sn.Prices[i], sn.PriceRatio[i], m*100)
	}
	fmt.Fprintf(out, "人口 %.0f  政府现金池 %.0f  资本现金池 %.0f  税收 %.0f\n",
		sn.Population, sn.GovCash, sn.CapitalCash, sn.Tax)
}
