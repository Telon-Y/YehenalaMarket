// Command market-sim 运行 1.0 生产与市场模拟（含政府 / 资本层）。
//
// 用法：
//
//	market-sim -ticks 10000 -pop 10000000 -tax 0.05
//	market-sim -ticks 10000 -calibrate-only
//	market-sim -ticks 1000 -export-dir out\webdata      # 导出逐 tick 快照(JSONL)供 1.1 界面使用
//	market-sim -ticks 1000 -export-dir out\webdata -serve :8787   # 顺带起只读服务
//
// 输出：§3.4 标定结果、运行期时序摘要、§8.4 验收判据 A1–A9 的判定。
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"math"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"yehenala/market/internal/ledger"
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
	// ===== 1.2 借贷台账（M8）：默认全关，保证 1.0 基线逐位不变 =====
	bankEnabled := flag.Bool("bank", false,
		"1.2 M8 借贷台账总开关（默认 false）：开启后劳动力结余转入**储蓄银行**、储蓄银行放贷给金融区（借 储蓄银行/贷 投资池）、每 tick 在入池前先扣还本息、不足则延期")
	loanPrincipal := flag.Float64("loan-principal", 500_000, "1.2 M8.1 单笔贷款本金（元，默认 500k）")
	loanRate := flag.Float64("loan-rate", 0.05, "1.2 M8.1/M5 贷款**年化**利率（默认 5%，无息例外已取消）")
	loanTerm := flag.Float64("loan-term", 5, "1.2 M8.1 贷款期限（年，默认 5）")
	loanInterval := flag.Int("loan-interval", 52, "1.2 M8 放贷节奏：每多少 tick 发放一笔新贷款（默认 52 = 每年一笔）")
	loanFromBalance := flag.Bool("loan-from-balance", false,
		"1.2 R71 收口(a)：把每期可贷额锚到**储蓄银行余额**（min(余额×比例, 单笔上限)），消除 R70 的 190 倍存贷失衡（默认关闭）")
	loanBalanceFraction := flag.Float64("loan-balance-fraction", 0.5,
		"1.2 R71：每期可贷额 ≤ 银行余额 × 本值（默认 0.5，仅在 -loan-from-balance 时生效）")
	// ===== 1.2 M4.2 所有权重构：默认关闭，保证 1.0 基线逐位不变 =====
	ownership := flag.Bool("ownership-restructure", false,
		"1.2 M4.2 所有权重构（默认 false）：私人份额纯利按 资本/劳动力 拆分——政府→资本 30%、资本→劳动力 70%")
	ownershipCapShare := flag.Float64("ownership-capital-share", 0.30,
		"1.2 M4.2 重构后资本在私人份额中的占比（默认 0.30 ⇒ 劳动力 0.70）")
	// ===== 1.2 M7 工资竞标：默认关闭，保证 1.0 基线逐位不变 =====
	wageBid := flag.Bool("wage-bid", false,
		"1.2 M7 工资竞标总开关（默认 false）：开启后各场地按**实际人均工资（基准+溢价）降序**配给劳动力，溢价由缺员与利润驱动、按年化速率衰减")
	wageBidCap := flag.Float64("wage-bid-cap", 0.5, "1.2 M7.2 第 2 步：可竞标资金占剩余利润的比例上限（默认 0.5 = 最高 50%）")
	wageBidDecay := flag.Float64("wage-bid-decay", 0.05, "1.2 M7.2 裁决②：溢价的**年化**衰减率（默认 5%/年）")
	wageBidBase := flag.String("wage-bid-base", "profit",
		"1.2 M7.2 第 2 步 A_i 的计费基数：profit（默认，裁决原文=纯利的 cap 倍）｜wagebill（R68/R92 对照口径=工资总额×利润率）")
	wageBidMarginCap := flag.Float64("wage-bid-margin-cap", 0,
		"R92 对照口径：参与抬价的**利润率因子上限**（0=不封顶）。注意 R92 已实测它对峰值溢价**无效**（峰值场地利润率仅 0.0238），保留作已证伪方案的证据")
	// ===== 2026-09-19 第 24 轮：§6.3 需求篮子的整体缩放 =====
	basketScale := flag.Float64("basket-scale", 1.0,
		"§6.3 需求篮子整体系数（默认 1.0 = 契约原表）。调大 ⇒ 每档目标消费量按比例提高、篮子价值向该档工资靠拢，用于检验\"篮子太小导致消费/工资只有 0.2\"这一假设；不改动商品门类")
	// ===== 1.2 配置档（R80）：一次打开全部 1.2 行为 =====
	profile12 := flag.Bool("profile12", false,
		"1.2 配置档：一次性打开全部 1.2 行为（= -bank -loan-from-balance -ownership-restructure -wage-bid）。显式给出的单个开关**优先于**本档。注意：Go 的 flag 不允许点名里带「.」，故名为 profile12 而非 profile-1.2")
	savingsStock := flag.Bool("savings-stock", false,
		"1.2 M1 第 3 条：打开储蓄**存量**口径（区分「储蓄账户余额」与「已分配投资」）。默认关闭 ⇒ 1.0 逐位不变")
	centralBank := flag.Bool("central-bank", false,
		"1.2 §1.2-5：开启**中央银行 + 金矿**（追加两类建筑）。默认关闭 ⇒ 1.0 逐位不变（场地数会由 15 变 17）")
	goldPrice := flag.Float64("gold-price", 10_000, "1.2 §1.2-5：黄金的**外生价格**（元/单位，默认 10,000）")
	moneyPerMint := flag.Float64("money-per-mint", 400_000, "1.2 §1.2-5：每次造币创造的货币量（默认 400,000）")
	goldPerMint := flag.Float64("gold-per-mint", 20, "1.2 §1.2-5：每多少单位黄金触发一次造币（默认 20）")
	goldMineLabor := flag.Float64("gold-mine-labor", 5000, "1.2 §1.2-5：金矿每级雇佣人数（默认 5000）")
	// ===== 1.2 M3/M6 玩家投资接口（默认关闭 = AI 托管）=====
	investAIOff := flag.Bool("invest-ai-off", false,
		"1.2 M3/M6：**关闭政府投资 AI**（玩家接管投资方向）。需配合 -invest-manor-share 指定方向；默认不关 = 1.0 的自动口径")
	investManorShare := flag.Float64("invest-manor-share", -1,
		"1.2 M3/M6：玩家指定的**庄园栈预算占比** ∈ [0,1]；-1（默认）= 未设定、交回 AI")
	// ===== 1.1 界面用：导出逐 tick 快照（只读，不影响任何数值路径）=====
	exportDir := flag.String("export-dir", "",
		"1.1：把**逐 tick 快照**导出到该目录（snapshots.jsonl + meta.json），供图形界面回放；空 = 不导出")
	exportEvery := flag.Int("export-every", 0,
		"1.1：每 N 个 tick 导出一行（0 = 自动：≤2000 tick 全导，否则约为 2000 行）")
	serveAddr := flag.String("serve", "",
		"1.1：把这些数据与 web/ 一起用**只读 HTTP** 提供（如 :8787）；空 = 不启服务")
	serveOnly := flag.Bool("serve-only", false,
		"1.1：跳过仿真与标定，直接把已存在的 -export-dir 数据用 -serve 提供（用于启动脚本的秒开）")
	// ===== 1.2 M5 ① 政府债务计息（默认关闭）=====
	govDebtInterest := flag.Bool("gov-debt-interest", false,
		"1.2 M5 ①：给**政府债务**计息（利息付给**中央银行**，第 67 轮裁决）。默认关闭 ⇒ 1.0 的『不计息』")
	govDebtRate := flag.Float64("gov-debt-rate", 0.05,
		"1.2 M5 ①：政府债务的**年化**利率（默认 0.05 = M8.5 的统一 5%）")
	inflationDeflation := flag.Bool("inflation-deflation", false,
		"1.2 工资平减（第 72 轮裁决）：把消费预算除以**通胀比例**，使通胀不通过预算反馈成需求。默认关闭 ⇒ 1.0 逐位不变")
	flag.Parse()

	// 【1.1 启动脚本用】-serve-only：数据已经导出好了，跳过标定与仿真，直接把
	// 现成的 JSONL 用只读 HTTP 提供出去。这样启动是**秒级**的（用户不必等仿真）。
	if *serveOnly {
		if *serveAddr == "" || *exportDir == "" {
			fmt.Fprintln(os.Stderr, "-serve-only 需要同时给出 -serve 与 -export-dir")
			os.Exit(1)
		}
		if _, err := os.Stat(filepath.Join(*exportDir, "snapshots.jsonl")); err != nil {
			fmt.Fprintf(os.Stderr, "-serve-only 找不到 %s（请先不加 -serve-only 跑一次以导出）\n",
				filepath.Join(*exportDir, "snapshots.jsonl"))
			os.Exit(1)
		}
		if err := serveData(*serveAddr, *exportDir); err != nil {
			fmt.Fprintf(os.Stderr, "服务失败: %v\n", err)
			os.Exit(1)
		}
		return
	}

	// 【R80】区分"显式设置"与"默认值"：只有 flag.Visit 会报告**命令行上真正出现**的开关。
	//
	// 为什么必须这么做：`Profile12` 要打开四个开关，但用户可能想"1.2 但关掉竞标"
	// 这类单因子对照。若无法区分，`-wage-bid=false` 与"没写"就完全一样，
	// 于是 profile 会把它覆盖回 true ⇒ 对照做不到。
	//
	// 做法：把**命令行上出现过的**那四个开关收进 `*bool` 覆盖指针。
	// 未出现的保持 nil ⇒ 交给 profile 决定。
	explicit := map[string]bool{}
	flag.Visit(func(f *flag.Flag) { explicit[f.Name] = true })
	overrideIfSet := func(name string, v *bool) *bool {
		if explicit[name] {
			return v
		}
		return nil
	}
	optBankOverride := overrideIfSet("bank", bankEnabled)
	optOwnershipOverride := overrideIfSet("ownership-restructure", ownership)
	optWageBidOverride := overrideIfSet("wage-bid", wageBid)
	optLoanOverride := overrideIfSet("loan-from-balance", loanFromBalance)
	// 【R89】玩家投资方向：只有命令行上**真正出现** `-invest-manor-share` 时才传指针，
	// 否则传 nil = "未设定、交回 AI"。这与上面四个开关用 `flag.Visit` 的理由相同：
	// `-invest-manor-share 0`（全给金融）与"没写"是**两个不同的意思**，
	// 而 flag 的默认值无法区分它们。
	var optInvestShare *float64
	if explicit["invest-manor-share"] {
		optInvestShare = investManorShare
	}

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
		NoExpandDilution:        *noExpandDilution,
		ExpectedMarginFloor:     *expandFloor,
		ExpandPlanHorizon:       *expandHorizon,
		PowerByQueue:            powerByQueue,
		PowerAnchorByQueue:      powerAnchorByQueue,
		BankEnabled:             *bankEnabled,
		LoanPrincipal:           *loanPrincipal,
		LoanAnnualRate:          *loanRate,
		LoanTermYears:           *loanTerm,
		LoanIssueInterval:       *loanInterval,
		LoanFromBalance:         *loanFromBalance,
		LoanBalanceFraction:     *loanBalanceFraction,
		OwnershipRestructure:    *ownership,
		OwnershipCapitalShare:   *ownershipCapShare,
		Profile12:               *profile12,
		SavingsStockTrack:       *savingsStock,
		CentralBankEnabled:      *centralBank,
		GoldPrice:               *goldPrice,
		MoneyPerMint:            *moneyPerMint,
		GoldPerMint:             *goldPerMint,
		GoldMineLaborPerLevel:   *goldMineLabor,
		InvestAIOff:             *investAIOff,
		InvestManorShare:        optInvestShare,
		GovDebtInterestEnabled:  *govDebtInterest,
		GovDebtInterestRate:     *govDebtRate,
		InflationDeflation:      *inflationDeflation,
		BankEnabledOverride:     optBankOverride,
		OwnershipOverride:       optOwnershipOverride,
		WageBidOverride:         optWageBidOverride,
		LoanFromBalanceOverride: optLoanOverride,
		WageBidEnabled:          *wageBid,
		WageBidCap:              *wageBidCap,
		WageBidDecay:            *wageBidDecay,
		WageBidBase:             *wageBidBase,
		WageBidMarginCap:        *wageBidMarginCap,
		BasketScale:             *basketScale,
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
	st.Params.SavingsRate = *saveRate         // §5.3 工资结余 → 储蓄 → 投资
	st.Params.WelfareTier = *welfare          // §4.5.8 福利金档位 0–6
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

	// ===== 1.1 界面数据：导出逐 tick 快照（只读；不改动任何数值路径）=====
	if *exportDir != "" {
		n, err := exportSnapshots(*exportDir, snaps, goods, st, sum, *exportEvery)
		if err != nil {
			fmt.Fprintf(os.Stderr, "导出失败: %v\n", err)
			os.Exit(1)
		}
		fmt.Fprintf(out, "\n--- 1.1 界面数据已导出 ---\n")
		fmt.Fprintf(out, "  目录 = %s（snapshots.jsonl %d 行 + meta.json）\n", *exportDir, n)
		fmt.Fprintf(out, "  用法：market-sim -ticks N -export-dir <目录> -serve :8787 后用浏览器打开\n")
	}

	if *serveAddr != "" {
		if *exportDir == "" {
			fmt.Fprintln(os.Stderr, "-serve 需要同时给出 -export-dir")
			os.Exit(1)
		}
		// 先把已生成的报告输出，再起只读服务
		fmt.Print(out.String())
		out.Reset()
		if err := serveData(*serveAddr, *exportDir); err != nil {
			fmt.Fprintf(os.Stderr, "服务失败: %v\n", err)
			os.Exit(1)
		}
	}
}

// ---------------------------------------------------------------------------
// 1.1 界面数据导出
//
// 【纪律】以下逻辑全部**只读**：只读取运行结束后的快照与参数，不写回任何引擎字段。
// 故它不改变 1.0 的任何数值路径（契约 §六-1 的回归锚点仍然成立）。
// ---------------------------------------------------------------------------

// widget 是导出行里除逐商品/逐建筑切片之外的全部标量。
type widget struct {
	Tick             int64      `json:"tick"`
	Population       float64    `json:"population"`
	Unemployed       float64    `json:"unemployed"`
	Happiness        float64    `json:"happiness"`
	HappinessByClass [3]float64 `json:"happinessByClass"`
	NoIncomePools    int        `json:"noIncomePools"`

	GovCash     float64 `json:"govCash"`
	GovDebt     float64 `json:"govDebt"`
	GovDebtCap  float64 `json:"govDebtCap"`
	CapitalCash float64 `json:"capitalCash"`
	HouseCash   float64 `json:"houseCash"`
	TotalMoney  float64 `json:"totalMoney"`
	CashTotal   float64 `json:"cashTotal"`
	NewCapital  float64 `json:"newCapital"`
	Infusion    float64 `json:"infusion"`

	InvestmentPool float64 `json:"investmentPool"`
	KManor         float64 `json:"kManor"`
	KFinance       float64 `json:"kFinance"`

	Tax        float64 `json:"tax"`
	VAT        float64 `json:"vat"`
	ConsumeTax float64 `json:"consumeTax"`
	WageBill   float64 `json:"wageBill"`
	SpendNet   float64 `json:"spendNet"`
	Saving     float64 `json:"saving"`
	SavingInv  float64 `json:"savingInvest"`
	SavingsAcc float64 `json:"savingsAccount"`
	Welfare    float64 `json:"welfare"`
	PublicWks  float64 `json:"publicWorks"`
	AcquirePay float64 `json:"acquirePaid"`

	SubsistenceHire float64 `json:"subsistenceHire"`
	WarehouseLevel  float64 `json:"warehouseLevel"`
	TradeVolume     float64 `json:"tradeVolume"`

	GrossProduct      float64 `json:"grossProduct"`
	ProductInput      float64 `json:"productInput"`
	ProductAdded      float64 `json:"productAdded"`
	ProductAddedAgri  float64 `json:"productAddedAgri"`
	ProductAddedInd   float64 `json:"productAddedIndustry"`
	ProductPerCapita  float64 `json:"productPerCapita"`
	ProductIndex      float64 `json:"productIndex"`
	ProductSubsist    float64 `json:"productSubsistence"`

	// 逐商品（下标与 meta.goods 对齐）
	Price  []float64 `json:"price"`
	Pzero  []float64 `json:"pzero"`
	Ratio  []float64 `json:"ratio"` // P / P⁰（当期零利润价）
	Supply []float64 `json:"supply"`
	Demand []float64 `json:"demand"`
	Margin []float64 `json:"margin"`
	// 逐建筑（下标与 meta.buildings 对齐）；hire/cash 仅末行有值，中间行为 null
	Level []float64  `json:"level"`
	Hire  []*float64 `json:"hire"`
	Cash  []*float64 `json:"cash"`
}

// meta 是界面需要的静态信息（只导一次）。
type meta struct {
	Goods         []string           `json:"goods"`
	GoodPinit     []float64          `json:"goodPinit"`
	GoodPcost     []float64          `json:"goodPcost"`
	Buildings     []string           `json:"buildings"`
	BuildExplicit []bool             `json:"buildExplicit"` // 可拆：Produces && !IsNonMarket && !IsAgent
	LaborPerLevel []float64          `json:"laborPerLevel"` // 每级雇佣人数
	LaborStructures []laborStructRow `json:"laborStructures"` // 逐建筑的阶级结构（§5 第 20 轮）
	Params        map[string]float64 `json:"params"`
	Assess        []assessRow        `json:"assess"`
	Ticks         int                `json:"ticks"`
	Every         int                `json:"every"`
	Note          string             `json:"note"`
}

// laborStructRow 是某个建筑的阶级结构。**界面必须用它算阶级占比**，
// 不得写死 75/20/5 —— 农业与庄园的"工程师"档其实是农民（7 元），
// 而各建筑的占比也可能随版本变化（§5 第 20 轮把农业单列）。
type laborStructRow struct {
	Name   string     `json:"name"`
	Shares [3]float64 `json:"shares"`
	Wages  [3]float64 `json:"wages"`
}

type assessRow struct {
	ID     string `json:"id"`
	Text   string `json:"text"`
	Pass   bool   `json:"pass"`
	Detail string `json:"detail"`
	Note   string `json:"note"`
}

func exportSnapshots(dir string, snaps []*sim.Snapshot, goods []model.Good, st *sim.State,
	sum *report.Summary, every int) (int, error) {

	if err := os.MkdirAll(dir, 0o755); err != nil {
		return 0, err
	}
	// 自动采样：把行数控制在 ~2000，避免 10,000 tick 导出几十 MB
	if every <= 0 {
		every = 1
		if len(snaps) > 2000 {
			every = (len(snaps) + 1999) / 2000
		}
	}

	f, err := os.Create(filepath.Join(dir, "snapshots.jsonl"))
	if err != nil {
		return 0, err
	}
	defer f.Close()
	enc := json.NewEncoder(f)

	n := 0
	nB := len(st.Buildings)
	for i, sn := range snaps {
		isLast := i == len(snaps)-1
		// 首行与末行必导（末态是判据窗口的结论所在）
		if i%every != 0 && !isLast {
			continue
		}
		w := widget{
			Tick: sn.Tick, Population: sn.Population, Unemployed: sn.Unemployed,
			Happiness: sn.Happiness, HappinessByClass: sn.HappinessByClass,
			NoIncomePools: sn.NoIncomePools,
			GovCash:       sn.GovCash, GovDebt: sn.GovDebt, GovDebtCap: sn.GovDebtCap,
			CapitalCash: sn.CapitalCash, HouseCash: sn.HouseCash,
			TotalMoney: sn.TotalMoney, CashTotal: sn.CashTotal,
			NewCapital: sn.NewCapital, Infusion: sn.Infusion,
			InvestmentPool: sn.InvestmentPool, KManor: sn.KManor, KFinance: sn.KFinance,
			Tax: sn.Tax, VAT: sn.VAT, ConsumeTax: sn.ConsumeTax,
			WageBill: sn.WageBill, SpendNet: sn.SpendNet,
			Saving: sn.Saving, SavingInv: sn.SavingInvest, SavingsAcc: sn.SavingsAccount,
			Welfare: sn.Welfare, PublicWks: sn.PublicWorks, AcquirePay: sn.AcquirePaid,
			SubsistenceHire: sn.SubsistenceHire,
			WarehouseLevel:  sn.WarehouseLevel, TradeVolume: sn.TradeVolume,
			GrossProduct: sn.GrossProduct, ProductInput: sn.ProductInput,
			ProductAdded: sn.ProductAdded, ProductAddedAgri: sn.ProductAddedAgri,
			ProductAddedInd: sn.ProductAddedIndustry, ProductPerCapita: sn.ProductPerCapita,
			ProductIndex: sn.ProductIndex, ProductSubsist: sn.ProductSubsistence,
			Price: sn.Prices, Pzero: sn.Pzero, Ratio: sn.PriceRatio,
			Supply: sn.Supply, Demand: sn.Demand, Margin: sn.Margins,
		}
		// 逐建筑：等级来自快照（每行都有）；雇佣率与现金池只有 State 有，
		// 而 State 是**末态** ⇒ 只有最后一行能填真值，中间行给 null（前端显示"—"）。
		// 用 []*float64 是为了让 null 合法：encoding/json 不能序列化 NaN。
		w.Level = make([]float64, nB)
		for j := 0; j < nB; j++ {
			if j < len(sn.Levels) {
				w.Level[j] = sn.Levels[j]
			}
		}
		if isLast {
			w.Hire = make([]*float64, nB)
			w.Cash = make([]*float64, nB)
			for j := 0; j < nB; j++ {
				hr := st.Buildings[j].HireRate
				cs := st.Aud.Balance(ledger.Building(j))
				w.Hire[j] = &hr
				w.Cash[j] = &cs
			}
		}
		if err := enc.Encode(&w); err != nil {
			return n, err
		}
		n++
	}

	// meta
	names := make([]string, 0, len(goods))
	pinit := make([]float64, 0, len(goods))
	pcost := make([]float64, 0, len(goods))
	for _, g := range goods {
		names = append(names, g.Name)
		pinit = append(pinit, g.Pinit)
		pcost = append(pcost, g.Pcost)
	}
	bnames := make([]string, 0, nB)
	bexp := make([]bool, 0, nB)
	laborPer := make([]float64, 0, nB)
	laborStr := make([]laborStructRow, 0, nB)
	for i := 0; i < nB; i++ {
		sp := st.Buildings[i].Spec
		bnames = append(bnames, sp.Name)
		// 可拆 = 显式建筑（产出商品、不是金融区/宅邸庄园、不是消费代理）
		bexp = append(bexp, sp.Produces() && !sp.IsNonMarket() && !sp.IsAgent)
		laborPer = append(laborPer, sp.LaborPerLevel)
		s := sp.StructureOf()
		laborStr = append(laborStr, laborStructRow{Name: s.Name, Shares: s.Shares, Wages: s.Wages})
	}
	rows := make([]assessRow, 0, len(sum.Verdicts))
	for _, v := range sum.Verdicts {
		rows = append(rows, assessRow{v.ID, v.Text, v.Pass, v.Detail, v.Note})
	}
	m := meta{
		Goods: names, GoodPinit: pinit, GoodPcost: pcost,
		Buildings: bnames, BuildExplicit: bexp,
		LaborPerLevel: laborPer, LaborStructures: laborStr,
		Params: map[string]float64{
			"taxRate": st.Params.TaxRate, "vat": st.Params.VATRate,
			"consumeTax": st.Params.ConsumeTaxRate, "warehouseMarkup": st.Params.WarehouseMarkup,
			"saveRate": st.Params.SavingsRate, "welfareTier": float64(st.Params.WelfareTier),
			"publicWorks": st.Params.PublicWorksShare, "arableCap": st.Params.ArableCap,
			"initLevel": st.Params.ProductionInitLevel, "powerInit": st.Params.InitialPowerLevel,
			"controlPerFinance": st.Params.ControlPerFinance, "govInitialShare": st.Params.GovInitialShare,
			"sitePowerLimit": st.Params.SitePowerLimit, "demandScale": st.DemandScale(),
		},
		Assess: rows, Ticks: len(snaps), Every: every,
		Note: "由 market-sim -export-dir 生成；逐 tick 快照，只读导出，不影响引擎数值路径。" +
			"中间 tick 的逐建筑 hire/cash 为 null（快照未含该向量）。",
	}
	mb, err := json.MarshalIndent(&m, "", "  ")
	if err != nil {
		return n, err
	}
	if err := os.WriteFile(filepath.Join(dir, "meta.json"), mb, 0o644); err != nil {
		return n, err
	}
	return n, nil
}

// serveData 起一个**只读**HTTP 服务：/data/*（导出的 JSON）+ /（web/ 前端）。
// 有意不提供任何写接口——前端只能读（与契约 §0.2-13"国家层不写账"同一纪律）。
func serveData(addr, dataDir string) error {
	webDir, err := findWebDir()
	if err != nil {
		return err
	}
	mux := http.NewServeMux()
	mux.Handle("/data/", http.StripPrefix("/data/", http.FileServer(http.Dir(dataDir))))
	mux.Handle("/", http.FileServer(http.Dir(webDir)))
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	fmt.Printf("\n--- 1.1 只读服务已启动 ---\n  浏览器打开  http://localhost%s/   （Ctrl+C 结束）\n", addr)
	fmt.Printf("  数据目录 = %s\n  前端目录 = %s\n", dataDir, webDir)
	return http.Serve(ln, mux)
}

// findWebDir 从当前目录向上找 web/（源码树与发布包两种布局都能命中）。
func findWebDir() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for i := 0; i < 6; i++ {
		p := filepath.Join(dir, "web")
		if st, err := os.Stat(p); err == nil && st.IsDir() {
			return p, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return "", fmt.Errorf("找不到 web/ 目录（请在仓库根或 gosim/ 下运行）")
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
