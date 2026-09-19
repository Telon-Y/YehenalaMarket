// Command market-sim 运行 1.0 生产与市场模拟（含政府 / 资本层）。
//
// 用法：
//
//	market-sim -ticks 10000 -pop 10000000 -tax 0.05
//	market-sim -ticks 10000 -calibrate-only
//
// 输出：§3.4 标定结果、运行期时序摘要、§8.4 验收判据 A1–A6 的判定。
package main

import (
	"flag"
	"fmt"
	"math"
	"os"
	"strings"

	"yehenala/market/internal/model"
	"yehenala/market/internal/report"
	"yehenala/market/internal/sim"
)

func main() {
	ticks := flag.Int("ticks", 10000, "运行的周期数（1 tick = 1 周）")
	pop := flag.Float64("pop", 5_000_000, "开局人口（2026-09-19 第 21 轮裁决：土地等级 1,000 + 人口 5m，由 10m 下调）")
	tier := flag.Float64("tier", 10, "§6.3 需求量表所取的财富档")
	tax := flag.Float64("tax", 0.05, "政府税率（G1）；§4.5.6 的两段税 ν=τ=2.5%（综合 ≈4.94%）尚未实现，本版以单一税率 5% 作替身")
	ctrl := flag.Float64("ctrl", 20, "掌控比 c_ctrl：每级金融区/宅邸庄园掌控的其余建筑级数（G5/§4.5.5）")
	financeLabor := flag.Float64("finance-labor", 1000, "金融区每级雇佣人数（G5）")
	demandScale := flag.Float64("k", 0, "§6.3 需求缩放系数；0 表示用标定解")
	govStartup := flag.Float64("gov-startup", 0.5, "政府现金池初值占债务上限的比例（G7）")
	subsist := flag.Float64("subsist", 0, "诊断性覆盖自给农场规模；0 表示用契约默认值 1.0（1 级未使用土地 → 1 级自给农场）")
	powerInit := flag.Float64("power-init", 0, "诊断性覆盖建造部门起步等级；0 表示用建模默认值 20")
	calibrateOnly := flag.Bool("calibrate-only", false, "只做标定并打印结果")
	privatize := flag.Bool("privatize", false, "开启私有化机制（§4.5.1 修订；默认关闭）")
	initLevel := flag.Float64("init-level", -1, "每种生产建筑的起始等级：-1 = 契约默认 5；0 = 物质平衡布点；>0 = 统一该等级")
	// §3.4 已裁决采用的锚定方案（A + C′）。默认开启；用 =false 可回退历史口径做对照实验。
	anchorExp := flag.Bool("anchor-expenditure", true, "§3.4 方案 A：支出份额锚（ε≡1，P*∝1/S）；=false 回退历史口径")
	anchorDer := flag.Bool("anchor-derived", true, "§3.4 方案 C′：中间品/建造力按派生需求每 tick 重锚；=false 回退历史口径")
	subsidy := flag.Bool("subsidy", false, "§4.5.7 政府补贴总开关：对亏损且未满员的 AllowSubsidy 建筑拨款，使其追求满员")
	subsidyCap := flag.Float64("subsidy-cap", 0, "每周期单个建筑类别的补贴上限（元）；0 = 不限，仅受债务上限约束")
	unlimitedFunds := flag.Bool("unlimited-funds", false,
		"【诊断·对照实验】把投资池与国库都当作恒 ∞：投资池每 tick 补到哨兵水位、G2 解除可动用资金裁剪，使建造链只受实物产能限制（注入额单独计量，见 -h 与 docs/ACTIVE.md 的 R28）")
	priceMult := flag.Float64("price-mult", 1.0,
		"【诊断·对照实验】把开局价 P_init 整体放大该倍数（1 = 契约默认 1.2×P_cost；3 = 3.6×P_cost，开局利润率 20% → 200%）。不动 P_cost，见 docs/ACTIVE.md 的 R31")
	staticPcost := flag.Bool("static-pcost", false,
		"【对照开关】钳制带与 A1/A3 判据改用**固定** P_cost（2026-09-19 旧口径）；默认 false = 用**当期零利润价** P⁰(t)=ΣA·P(t)+l（契约 §2.4 / §3.4），见 docs/ACTIVE.md 的 R32/R33")
	// ===== 2026-09-19 第 15 轮裁决的三个新政策旋钮 =====
	saveRate := flag.Float64("save-rate", 1.0,
		"§5.3 储蓄率 σ_save ∈ [0,1]：每个 tick 消费结算后，各人群池的工资结余按该比例转入总投资池（1.0 = 结余全部储蓄，契约默认）")
	welfare := flag.Int("welfare", 0,
		"§4.5.8 福利金档位 0–6：每人补贴 = min(1.2, 0.2×档位) × max(0, 平均工资标准 6.75 − 本人工资)；0 = 关闭（契约默认）")
	publicWorks := flag.Float64("public-works", 0.5,
		"§4.5.8 公共工程支出占当期税收的比例 ∈ [0,1]（受债务上限约束）：国库直接采购建造力投入**开发类建筑**（建造部门），新增等级归政府")
	// ===== 2026-09-19 第 17 轮：税制与加价（抽象替身，可在标定前替换）=====
	vat := flag.Float64("vat", 0.025,
		"§4.5.6 增值税 ν（生产 → 仓库）：仓库按 生产者售价×(1+ν) 付款，ν 部分上缴政府")
	consumeTax := flag.Float64("consume-tax", 0.025,
		"§4.5.6 消费税 τ（仓库 → 买家）：买家按 仓库卖出价×(1+τ) 付款，τ 部分上缴政府")
	warehouseMarkup := flag.Float64("warehouse-markup", 0.05,
		"§4.5.6 仓库加价率：卖出价 = 买入价×(1+加价)。**三者共同构成买家加载系数 w**，且进入 §3.4 标定")
	// ===== 2026-09-19 第 22 轮：§5.2 增雇口径（期望扩招后利润率）=====
	noExpandDilution := flag.Bool("no-expand-dilution", false,
		"【对照开关】关闭 §5.2 的\"扩招压价折现\"，使增雇判定退化为**第 20 轮旧口径**（只看当期利润率 EMA > 0）。默认 false = 第 22 轮新口径（期望扩招后利润率 > 0）")
	expandFloor := flag.Float64("expand-margin-floor", 0,
		"§5.2 增雇判定的下限：只有\"期望扩招后利润率\" > 本值才增雇（默认 0 = 严格为正）")
	expandHorizon := flag.Int("expand-horizon", 52,
		"§5.2 增雇的前瞻步数：52 = 按一年后的雇佣率前瞻（契约默认，\"扩招\"的年度语义）；1 = 只折现本 tick 的一步（对照臂）")
	// ===== 2026-09-19 第 23 轮：建造部门的队列口径 =====
	powerByQueue := flag.Bool("power-by-queue", true,
		"§4.1 第 23 轮：建造部门豁免\"利润率 > 10% 就扩建\"，产能改由\"队列深度 ÷ 目标周期\"驱动）。=false 回退旧口径（对照臂）")
	powerAnchorByQueue := flag.Bool("power-anchor-queue", true,
		"§3.4 第 23 轮：建造力需求锚 = 在手订单 ÷ 队列目标周期（而不是订单绝对量）。=false 回退旧锚（对照臂）")
	// ===== 2026-09-19 第 24 轮：§6.3 需求篮子的整体缩放 =====
	basketScale := flag.Float64("basket-scale", 1.0,
		"§6.3 需求篮子整体系数（默认 1.0 = 契约原表）。调大 ⇒ 每档目标消费量按比例提高、篮子价值向该档工资靠拢，用于检验\"篮子太小导致消费/工资只有 0.2\"这一假设；不改动商品门类")
	flag.Parse()

	out := &strings.Builder{}
	defer func() { fmt.Print(out.String()) }()

	// 标定（§3.4 + 三表联合标定）。-price-mult 会同步作用于报告的 P_init 与利润率，
	// 使"打印的标定"与"实际运行的局"逐位一致（§七 R31）。
	specs := model.BuildingSpecs(*financeLabor)
	// 【第 17 轮】买家加载系数 w 必须与运行时一致，故先用 CLI 的税/加价算出 w 再标定。
	wedge := (1 + *warehouseMarkup) * (1 + *consumeTax)
	cal, err := sim.CalibrateOnlyScaledWedge(specs, *demandScale, *priceMult, wedge)
	if err != nil {
		fmt.Fprintf(os.Stderr, "标定失败: %v\n", err)
		os.Exit(1)
	}
	goods := model.GoodSpecs()
	// 报告里的"开局价"必须取**标定解**（可能已被 -price-mult 缩放），而不是
	// model.GoodSpecs 里那份契约表格常量——否则会出现"打印的是 1.2×P_cost、
	// 跑的是 3.6×P_cost"这类口径分叉（§七 R31 实测踩到过）。
	// 【2026-09-19 第 16 轮】参考价 P_ref 同理取标定解：仓库落地后 §3.4 的方程
	// 含买家加载系数 w，表里的取整值会有 ≤0.1% 偏差，报告与运行必须同源。
	for i := range goods {
		if i < len(cal.Pinit) {
			goods[i].Pinit = cal.Pinit[i]
		}
		if i < len(cal.Pcost) {
			goods[i].Pcost = cal.Pcost[i]
		}
	}
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
		GovStartupFraction:   *govStartup,
		SubsistenceScale:     optionalFloat(*subsist),
		InitialPowerLevel:    *powerInit,
		ProductionInitLevel:  *initLevel,
		// §3.4 锚定方案（A + C′，契约默认开启）
		AnchorExpenditureShare: anchorExp,
		AnchorDerivedDemand:    anchorDer,
		// §七 R28 的诊断开关：投资池与国库恒 ∞ 的对照实验
		UnlimitedFunds: *unlimitedFunds,
		// §七 R31 的诊断开关：开局价整体放大
		InitPriceMult: *priceMult,
		// §七 R32 的对照开关：默认用当期（动态）零利润价作钳制带参考价
		StaticPcost: *staticPcost,
		// §4.5.6 / 第 17 轮：税制与仓库加价（在标定之前生效，故与价格表同源）
		VATRate:         vat,
		ConsumeTaxRate:  consumeTax,
		WarehouseMarkup: warehouseMarkup,
		// §5.2 第 22 轮：增雇改按"期望扩招后利润率"
		NoExpandDilution:    *noExpandDilution,
		ExpectedMarginFloor: *expandFloor,
		ExpandPlanHorizon:   *expandHorizon,
		PowerByQueue:        powerByQueue,
		PowerAnchorByQueue:  powerAnchorByQueue,
		BasketScale:         *basketScale,
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "初始化失败: %v\n", err)
		os.Exit(1)
	}
	st.Params.TaxRate = *tax
	st.Params.ControlPerFinance = *ctrl
	// §4.5.7 补贴开关（默认关闭；打开后对亏损且未满员的 AllowSubsidy 建筑拨款）
	st.Params.SubsidyEnabled = *subsidy
	st.Params.SubsidyCap = *subsidyCap
	// §4.5.1 修订：私有化机制（默认关闭，可用 -privatize 开启）
	st.Params.PrivatizeEnabled = *privatize
	// ===== 2026-09-19 第 15 轮裁决的三个政策旋钮 =====
	st.Params.SavingsRate = *saveRate       // §5.3 工资结余 → 储蓄 → 投资
	st.Params.WelfareTier = *welfare        // §4.5.8 福利金档位 0–6
	st.Params.PublicWorksShare = *publicWorks // §4.5.8 公共工程支出占税收比例

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
	fmt.Fprintf(out, "%8s %12s %8s %7s %16s %13s %12s %13s %13s %13s %13s %10s %12s %10s\n",
		"tick", "人口", "总级数", "建造力", "政府现金池", "税收", "经营净额", "政府债务", "债务上限",
		"庄园现金池", "投资池", "自给农场雇佣率", "失业", "幸福度")
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
		fmt.Fprintf(out, "%8d %12.0f %8.0f %7.1f %16.0f %13.0f %12.0f %13.0f %13.0f %13.0f %13.0f %10.3f %12.0f %10.4f\n",
			sn.Tick, sn.Population, lv, pw, sn.GovCash, sn.Tax, sn.Flow.GovOperating,
			sn.GovDebt, sn.GovDebtCap, sn.ManorCash, sn.InvestmentPool,
			sn.SubsistenceHire, sn.Unemployed, sn.Happiness)
	}

	// 资金流分解：诊断"政府现金池为何被砸穿"
	fmt.Fprintf(out, "\n--- 资金流分解（抽样 tick）---\n")
	fmt.Fprintf(out, "%8s %13s %13s %13s %13s %13s %13s %13s %10s %13s %12s %12s\n",
		"tick", "工资总额", "消费支出", "金融区纯利", "庄园纯利", "政府经营", "税收", "按需采购", "投资池偿还",
		"居民储蓄", "福利金", "公共工程")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		f := sn.Flow
		fmt.Fprintf(out, "%8d %13.0f %13.0f %13.0f %13.0f %13.0f %13.0f %13.0f %13.0f %13.0f %12.0f %12.0f\n",
			sn.Tick, f.WageTotal, f.SpendNet, f.CapitalProfit, f.ManorProfit, f.GovOperating,
			f.GovTax, f.GovPowerSpend, f.GovPowerRevenue, f.HouseSaving, f.GovWelfare, f.GovPublicWorks)
	}

	// 投资池与两条投资栈（§4.5.1b）：入池额、累计贡献、按贡献分配的预算。
	fmt.Fprintf(out, "\n--- 投资池与两条投资栈（抽样 tick）---\n")
	fmt.Fprintf(out, "%8s %14s %14s %14s %14s %14s %14s %14s\n",
		"tick", "投资池", "K_m(庄园)", "K_f(金融区)", "入池_m", "入池_f", "付政府", "政府净支出")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		f := sn.Flow
		// 政府的建造力净支出 = 采购支出 − 投资池偿还（本版应恒为 0）。
		fmt.Fprintf(out, "%8d %14.0f %14.0f %14.0f %14.0f %14.0f %14.0f %14.2f\n",
			sn.Tick, sn.InvestmentPool, sn.KManor, sn.KFinance,
			sn.InvestmentInflowManor, sn.InvestmentInflowFinance, sn.InvestmentPaid,
			f.GovPowerSpend-f.GovPowerRevenue)
	}

	// 实际工农生产总值（§7.2）：用**固定的 P_ref** 给实物量计价，与货币量/价格水平无关，
	// 故可直接用来判断"经济到底有没有真的增长"（把名义通胀剔除掉）。
	fmt.Fprintf(out, "\n--- 实际工农生产总值（§7.2，固定参考价 P_ref 计价；与货币无关）---\n")
	fmt.Fprintf(out, "%8s %14s %14s %14s %14s %14s %14s %14s %12s\n",
		"tick", "农业总产值", "其中自给农场", "工业总产值", "工农总产值", "中间投入", "工农增加值", "人均增加值", "指数(基期=1)")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		fmt.Fprintf(out, "%8d %14.0f %14.0f %14.0f %14.0f %14.0f %14.0f %14.4f %12.3f\n",
			sn.Tick, sn.GrossAgri, sn.ProductSubsistence, sn.GrossIndustry, sn.GrossProduct,
			sn.ProductInput, sn.ProductAdded, sn.ProductPerCapita, sn.ProductIndex)
	}

	// 仓库与消费代理（§4.5.6）：贸易量 / 额度 / 两段税 / 仓库纯利 / 代理余额
	fmt.Fprintf(out, "\n--- 仓库与消费代理（§4.5.6，抽样 tick）---\n")
	fmt.Fprintf(out, "%8s %14s %14s %10s %14s %14s %12s %12s %14s %12s\n",
		"tick", "贸易量(单位)", "额度(单位)", "仓库级数", "入库付款(含税)", "出库收款(净)",
		"增值税ν", "消费税τ", "仓库纯利", "代理余额")
	for i, sn := range snaps {
		if !sampleTick(i, len(snaps)) {
			continue
		}
		fmt.Fprintf(out, "%8d %14.1f %14.1f %10.1f %14.0f %14.0f %12.0f %12.0f %14.0f %12.4f\n",
			sn.Tick, sn.TradeVolume, sn.TradeQuota, sn.WarehouseLevel,
			sn.WarehouseOut, sn.WarehouseIn, sn.VAT, sn.ConsumeTax, sn.WarehouseProfit, sn.AgentCash)
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

	// §七 R28 的诊断开关：把"无限资金"这一外部假设显式打印出来，
	// 否则读者会把注入造出来的建造量误读成模型自持的能力。
	if st.UnlimitedFundsDiagnostic() {
		fmt.Fprintf(out, "\n--- ⚠ 诊断模式：投资池与国库恒 ∞（-unlimited-funds）---\n")
		fmt.Fprintf(out, "  诊断注入累计 = %.0f 元（投资池哨兵水位 1e12，每 tick 补足）\n", st.InfusionTotal())
		fmt.Fprintf(out, "  货币守恒读作：ΔM == NewCapital(%.0f) + 诊断注入(%.0f)\n",
			st.TickNewCapitalTotal(), st.InfusionTotal())
		fmt.Fprintf(out, "  **本模式的建造量与级数不能作为经济自持能力的证据**——它只用来隔离"+
			"收支不匹配，检验其余机制是否正常。\n")
	}
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
	fmt.Fprintf(out, "政府现金池 %.0f   资本现金池 %.0f   投资池 %.0f   人群现金池 %.0f   货币总量 %.0f\n",
		st.Gov.Cash.Balance(), st.Cap.Cash.Balance(), st.InvestmentPool(),
		st.Houses.TotalCash(), st.TotalMoney())
	fmt.Fprintf(out, "初始政府持股 s_gov = %.2f（§4.5.1：由 0.70 下调）   投资栈累计贡献 K_m=%.0f K_f=%.0f\n",
		st.Params.GovInitialShare, st.KManorTotal(), st.KFinanceTotal())
	// 2026-09-19 第 15 轮裁决：把三个新政策旋钮与**短缺起步**标注在开局状态里。
	fmt.Fprintf(out, "§5.3 储蓄率 σ_save = %.2f   §4.5.8 福利金档位 = %d/6   公共工程占税收 = %.2f\n",
		st.Params.SavingsRate, st.Params.WelfareTier, st.Params.PublicWorksShare)
	if st.ShortageStart {
		fmt.Fprintf(out, "开局布点 = **短缺起步**（§3.2 第 15 轮裁决）：统一起始等级 N0=%.0f，"+
			"起点产能远小于平衡产能（20m 口径约 1/19.7）——这是**声明的设计选择**，"+
			"使扩建/投资/财政等功能在开局即被激活，不是标定误差。\n", st.Params.ProductionInitLevel)
	} else {
		fmt.Fprintf(out, "开局布点 = 物质平衡布点（N0=0，非短缺起步）。\n")
	}
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
	fmt.Fprintf(out, "人口 %.0f  政府现金池 %.0f  资本现金池 %.0f  投资池 %.0f  税收 %.0f\n",
		sn.Population, sn.GovCash, sn.CapitalCash, sn.InvestmentPool, sn.Tax)
	// 2026-09-19 第 15 轮裁决：末态的幸福度 / 失业 / 收入端与支出端。
	fmt.Fprintf(out, "幸福度 %.4f（劳工 %.4f / 工程师 %.4f / 资本家 %.4f）  失业 %.0f  无收入池 %d\n",
		sn.Happiness, sn.HappinessByClass[0], sn.HappinessByClass[1], sn.HappinessByClass[2],
		sn.Unemployed, sn.NoIncomePools)
	fmt.Fprintf(out, "居民储蓄 %.0f  福利金 %.0f  公共工程 %.0f（+%.1f 级）  庄园栈预算份额 %.3f\n",
		sn.Saving, sn.Welfare, sn.PublicWorks, sn.PublicWorksUnits, sn.BudgetShareManor)
	// §4.5.6 仓库与消费代理
	fmt.Fprintf(out, "仓库：%.1f 级、贸易量 %.1f / 额度 %.1f 单位、现金 %.0f、纯利 %.0f（加价 5%% −增值税 −工资，全归政府）\n",
		sn.WarehouseLevel, sn.TradeVolume, sn.TradeQuota, sn.WarehouseCash, sn.WarehouseProfit)
	// §7.2 实际工农生产总值（固定 P_ref）：与名义量并列，用来看"真实增长"
	fmt.Fprintf(out, "实际工农生产总值（固定 P_ref）：增加值 %.0f（农业 %.0f、其中自给农场 %.0f / 工业 %.0f）、总产值 %.0f、中间投入 %.0f、人均 %.4f、指数 %.3f（基期 %.0f）\n",
		sn.ProductAdded, sn.ProductAddedAgri, sn.ProductSubsistence, sn.ProductAddedIndustry, sn.GrossProduct,
		sn.ProductInput, sn.ProductPerCapita, sn.ProductIndex, sn.ProductBaseAdded)
	fmt.Fprintf(out, "两段税：增值税 ν = %.0f 元 + 消费税 τ = %.0f 元（合计 %.0f 元，占税收 %.1f%%）；"+
		"消费代理余额 = %.4f（应恒为 0）\n",
		sn.VAT, sn.ConsumeTax, sn.VAT+sn.ConsumeTax, 100*(sn.VAT+sn.ConsumeTax)/math.Max(1, sn.Tax), sn.AgentCash)
}
