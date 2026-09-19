package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// newTestState 构造一个与 cmd/market-sim 默认参数一致的仿真（人口 10m）。
//
// 【人口口径】本函数原先用 200_000，那只是"让测试跑得快"的旧默认值。
// 人口会线性改变需求与工资，"满足度/成交额"一类断言必须与实际运行口径一致，
// 否则测的是另一套经济。故统一到 10_000_000（契约 §8.6 的参考人口；
// 命令行默认开局人口已于 2026-09-19 上调到 20m，测试仍固定用 10m 以保持证据可比）。
//
// GovStartupFraction 必须显式给出：债务机制（G7）下政府现金池初值取债务上限的
// 一定比例。若留 0，政府起步没有任何可动用资金，G2 采购在第一个 tick 就会被
// 现金约束卡死，届时"采购是否发生"测的是初值而非货币闭环本身。
func newTestState(t *testing.T) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		DemandScale:          0, // 用标定解
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		// -1 = 保持 Params 的契约默认起始等级（5）。
		// 本字段零值 0 的含义是"物质平衡布点"，不能靠留空来表达默认。
		ProductionInitLevel: -1,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	return st
}

// TestInitialCalibrationClosesMarket 校验 §3.4 步骤 3 的标定目标：
// 开局（t=0，未推进 tick）时每种商品的过剩需求 E ≈ 0，价格方程处于静止点。
//
// 这是"后续价格变化完全由产能扩张与人口增长驱动"这一设计意图的前提。
//
// 注意弹性口径随 §3.4 的裁决变化：方案 A（支出份额锚）取 ε ≡ 1，
// 故必须用 st.Market.EpsOf(g) 而不是 g.Eps。
func TestInitialCalibrationClosesMarket(t *testing.T) {
	st := newTestState(t)
	net := st.netSupply(st.calibration)
	for i := range st.Goods {
		g := st.Goods[i]
		d := st.Market.Markets[i].A * math.Pow(g.Pinit/g.Pcost, -st.Market.EpsOf(g))
		e := d - net[i]
		scale := math.Max(1, math.Abs(net[i]))
		if math.Abs(e)/scale > 1e-6 {
			t.Errorf("%s: 开局过剩需求 E = %.6e（净供给 %.6e，需求 %.6e），标定未闭合",
				g.Name, e, net[i], d)
		}
	}
}

// TestSingleTickKeepsFiniteValues 校验最基础的数值卫生：
// 一个 tick 之后所有价格、利润率、等级、人口都是有限值且非负。
func TestSingleTickKeepsFiniteValues(t *testing.T) {
	st := newTestState(t)
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step 失败: %v", err)
	}
	for i, p := range snap.Prices {
		if math.IsNaN(p) || math.IsInf(p, 0) {
			t.Errorf("%s: 价格为 %v", st.Goods[i].Name, p)
		}
		if p <= 0 {
			t.Errorf("%s: 价格为非正 %v", st.Goods[i].Name, p)
		}
	}
	for i, m := range snap.Margins {
		if math.IsNaN(m) || math.IsInf(m, 0) {
			t.Errorf("建筑 %d: 利润率为 %v", i, m)
		}
	}
	for i, l := range snap.Levels {
		if l < 0 || math.IsNaN(l) {
			t.Errorf("建筑 %d: 等级为 %v", i, l)
		}
	}
	if snap.Population <= 0 || math.IsNaN(snap.Population) {
		t.Errorf("人口为 %v", snap.Population)
	}
}

// TestGovernmentCanPurchasePower 校验 G2 在真实管线里生效：
// 政府必须确实按需采购过建造力，而不是把整个建造部门饿死。
//
// 【前值 → 后值（2026-09-19，第 11 轮）】旧断言是"直接跑 120 tick 就必须有采购"——
// 那对应"政府整批买下建造部门产出"的旧口径。§4.5.3 改为**无队列不采购**之后，
// 采购量由队列需要量决定，而队列需要量又由投资池预算决定（§4.5.1b）；
// 默认参数下两个资本建筑扣除自身工资后的净额均为负 ⇒ 投资池恒为 0 ⇒ 队列恒空 ⇒
// 采购恒为 0。**这是新资金口径的直接后果（已在报告中列为需要裁决的量级问题），
// 不是记账闭环的缺陷**。因此本测试改为在【受控场景】下隔离资金约束，
// 单独检验 G2/G6 的记账闭环本身。
func TestGovernmentCanPurchasePower(t *testing.T) {
	st := newTestState(t)
	sc := newScenario(t, "给投资池注资，隔离'资本建筑净额为负'这一资金约束")
	sc.InvestmentAdd(5e9, "默认参数下投资池恒为 0（庄园与金融区净额均为负），"+
		"本测试要验的是 G2 采购与 G6 偿还的记账闭环，故显式隔离资金约束")
	sc.Apply(st)
	defer sc.Restore(st)

	snaps, err := st.Run(120)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	var bought, paid, inflowed float64
	for _, s := range snaps {
		bought += s.PowerPurchased
		paid += s.InvestmentPaid
		inflowed += s.InvestmentInflowManor + s.InvestmentInflowFinance
		// G6 逐 tick 对账：投资池偿还 = 政府采购货款（净支出为 0）。
		want := s.PowerPurchased * s.Prices[powerGoodIndex]
		if math.Abs(s.InvestmentPaid-want) > 1e-6 {
			t.Fatalf("tick %d：投资池偿还 %.4f ≠ 采购货款 %.4f（政府净支出应为 0）",
				s.Tick, s.InvestmentPaid, want)
		}
	}
	if bought <= 0 {
		t.Fatal("120 个 tick 内政府采购建造力始终为 0——货币闭环未接上")
	}
	t.Logf("120 tick 累计采购建造力 = %.2f 单位；投资池累计入池 %.0f、付给政府 %.0f",
		bought, inflowed, paid)
}

// TestSoldPowerNeverExceedsPurchased 校验建造力不会被凭空出售。
//
// 【前值 → 后值（第 11 轮）】"售出"旧指政府把储备转售给扩建方；G6 改写后
// PowerSold 的含义是"本 tick 即买即用、实际投入队列的建造力量"。不变量
// 仍然成立，且在本版口径下更严格：无队列时两者同时为 0。
func TestSoldPowerNeverExceedsPurchased(t *testing.T) {
	st := newTestState(t)
	sc := newScenario(t, "给投资池注资：让队列非空，从而真正检验'投入 ≤ 采购'")
	sc.InvestmentAdd(5e9, "默认参数下投资池为 0 ⇒ 队列恒空 ⇒ 本不变量会被空真通过")
	sc.Apply(st)
	defer sc.Restore(st)

	snaps, err := st.Run(200)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	var anySold bool
	for _, s := range snaps {
		if s.PowerSold > s.PowerPurchased+1e-6 {
			t.Fatalf("tick %d: 投入队列 %.6f 超过采购 %.6f", s.Tick, s.PowerSold, s.PowerPurchased)
		}
		if s.PowerInventory != 0 {
			t.Fatalf("tick %d: 公共储备 %.6f 应恒为 0（§4.5.3 G2 即买即用）", s.Tick, s.PowerInventory)
		}
		if s.PowerSold > 0 {
			anySold = true
		}
	}
	if !anySold {
		t.Error("200 tick 内没有任何建造力被投入队列——本不变量的断言被空真通过")
	}
}

// TestSitePowerLimitInPipeline 校验 §4.2 的每工地 30 建造力上限在管线里生效。
//
// 【前值 → 后值（第 11 轮）】旧实现只在创建订单时写一次 Progress、之后不再推进；
// 现在 Progress 是**累计**投入量，故上限约束的是"每 tick 的投入量"（Order.Advance），
// 而不是累计值。断言随之改为检查 Advance。
func TestSitePowerLimitInPipeline(t *testing.T) {
	st := newTestState(t)
	sc := newScenario(t, "给投资池注资：让队列非空，从而真正检验每工地上限")
	sc.InvestmentAdd(5e9, "默认参数下投资池为 0 ⇒ 队列恒空 ⇒ 上限约束无从检验")
	sc.Apply(st)
	defer sc.Restore(st)

	var sawAdvance bool
	for i := 0; i < 200; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		for _, o := range st.Orders {
			if o.Advance > st.Params.SitePowerLimit+1e-6 {
				t.Fatalf("订单本 tick 投入 %.6f 超过每工地上限 %.1f",
					o.Advance, st.Params.SitePowerLimit)
			}
			if o.Progress < 0 {
				t.Fatalf("订单累计进度为负：%.6f", o.Progress)
			}
			if o.Advance > 0 {
				sawAdvance = true
			}
		}
	}
	if !sawAdvance {
		t.Error("200 tick 内没有任何订单被推进——上限约束被空真通过")
	}
}

// TestArableCapIsEnforced 校验 §4.2 的耕地约束：
// 谷物农场 + 棉花种植园的等级之和永不超过 10,000。
func TestArableCapIsEnforced(t *testing.T) {
	st := newTestState(t)
	for i := 0; i < 100; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		var used float64
		for j := range st.Buildings {
			if st.Buildings[j].Spec.LandKind == "arable" {
				used += st.Buildings[j].Level
			}
		}
		if used > st.Params.ArableCap+1e-6 {
			t.Fatalf("tick %d: 耕地占用 %.4f 超过上限 %.0f", st.Tick, used, st.Params.ArableCap)
		}
	}
}

// TestControlCapacityFollowsFinanceLevels 校验 G5 的掌控上限随金融区等级变化。
func TestControlCapacityFollowsFinanceLevels(t *testing.T) {
	st := newTestState(t)
	if _, err := st.Step(); err != nil {
		t.Fatalf("Step 失败: %v", err)
	}
	want := st.Buildings[model.FinanceIndex].Level * st.Params.ControlPerFinance
	if math.Abs(st.Cap.ControlCapacity-want) > 1e-6 {
		t.Errorf("掌控上限 = %.4f，应为 金融区等级 %.4f × %.1f = %.4f",
			st.Cap.ControlCapacity, st.Buildings[model.FinanceIndex].Level,
			st.Params.ControlPerFinance, want)
	}
}

// TestNoNaNOverMediumRun 是"无隐患停机"的守门测试（ARCHITECTURE §1.2）：
// 任何 tick 都不产生 NaN / Inf / 负价格 / 负等级。
func TestNoNaNOverMediumRun(t *testing.T) {
	st := newTestState(t)
	snaps, err := st.Run(500)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	for _, s := range snaps {
		for i, p := range s.Prices {
			if math.IsNaN(p) || math.IsInf(p, 0) || p <= 0 {
				t.Fatalf("tick %d 商品 %d: 价格 %v", s.Tick, i, p)
			}
		}
		if math.IsNaN(s.Population) || math.IsInf(s.Population, 0) {
			t.Fatalf("tick %d: 人口 %v", s.Tick, s.Population)
		}
		if math.IsNaN(s.GovCash) || math.IsInf(s.GovCash, 0) {
			t.Fatalf("tick %d: 政府现金池 %v", s.Tick, s.GovCash)
		}
		if math.IsNaN(s.CapitalCash) || math.IsInf(s.CapitalCash, 0) {
			t.Fatalf("tick %d: 资本现金池 %v", s.Tick, s.CapitalCash)
		}
		for i, l := range s.Levels {
			if l < 0 || math.IsNaN(l) {
				t.Fatalf("tick %d 建筑 %d: 等级 %v", s.Tick, i, l)
			}
		}
	}
}

// TestAcceptanceCriteriaAreComputable 校验 §8.4 判据在真实运行结果上可计算
// （不要求通过——本工程尚未声称 A1–A6 可达）。
func TestAcceptanceCriteriaAreComputable(t *testing.T) {
	st := newTestState(t)
	snaps, err := st.Run(300)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if len(snaps) < 200 {
		t.Skip("周期不足，跳过判据计算")
	}
	// 此处只校验快照字段完整，判据本身由 report 包负责。
	last := snaps[len(snaps)-1]
	if len(last.PriceRatio) != model.Goods {
		t.Errorf("PriceRatio 长度 = %d，应为 %d", len(last.PriceRatio), model.Goods)
	}
	if len(last.Margins) != model.BuildingTypes {
		t.Errorf("Margins 长度 = %d，应为 %d", len(last.Margins), model.BuildingTypes)
	}
	if len(last.Levels) != model.BuildingTypes {
		t.Errorf("Levels 长度 = %d，应为 %d", len(last.Levels), model.BuildingTypes)
	}
}

// TestGovCashFlowIsFullyExplained 是政府现金池的资金流守恒守门测试。
//
// 断言：每个 tick 的现金池变化必须能被已知资金流完全解释：
//
//	Δ现金池 = 税收 + 经营净额 − 按需采购支出 + 投资池偿还 − 补贴
//	          − 福利金 − 公共工程 − 仓库扩建 (+ 私有化对价)
//
// 残差恒为 0 才算通过。这条测试的价值来自历史：曾出现"政府经营净额被记两次"
// 的 bug（DistributeProfit 内部已入账，调用方又累加并再次入账），
// 表现为残差恰等于 −经营净额；不变量测试能立刻抓到，而单看余额曲线看不出来。
//
// 【前值 → 后值（2026-09-19，第 11 轮）】分解式原有 GovSelfTax（政府自己收自己的税）
// 与 GovBuildoutPaid（政府自建付款）两项；建造力交易定案**不计税**、G6 改为
// "投资池全额偿还"、本版无政府自有项目，故这两项一并删除，
// 并把投资池偿还（GovPowerRevenue）计入。
//
// 【前值 → 后值（2026-09-19，第 15 轮）】政府新增**支出端**（§4.5.8）：
// 福利金（GovWelfare，转移支付）与公共工程（GovPublicWorks，真实支出）两条腿
// 必须计入分解式。公共工程默认开启（占税收 50%），故不修正本式会稳定出现残差
// ——实测 tick 7 残差 −41,183.35 恰等于当期公共工程支出。
//
// 注：从第二个 tick 起校验。首个 tick 的采购发生在价格更新之前，
// 若用 tick 末价格折算会产生固定的口径差，与本测试要抓的记账错误无关。
func TestGovCashFlowIsFullyExplained(t *testing.T) {
	st := newTestState(t)
	const tol = 1e-6
	prev := st.balGov()
	for i := 1; i <= 60; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		f := snap.Flow
		delta := st.balGov() - prev
		explained := f.GovTax + f.GovOperating - f.GovPowerSpend +
			f.GovPowerRevenue - f.GovSubsidy - f.GovWelfare - f.GovPublicWorks -
			f.GovWarehouseExpand + f.PrivatizePaid
		if resid := delta - explained; math.Abs(resid) > tol {
			var building, house float64
			for i := range st.Buildings {
				building += st.bal(i)
			}
			house = st.Houses.TotalCash()
			t.Fatalf("tick %d 资金流未守恒：Δ现金=%.2f 已解释=%.2f 残差=%.2f\n"+
				"（税收 %.2f / 经营 %.2f / 采购 %.2f / 投资池偿还 %.2f / 补贴 %.2f / 福利金 %.2f / 公共工程 %.2f / 仓库扩建 %.2f）\n"+
				"五池：建筑%.0f 政府%.0f 资本%.0f 人群%.0f 投资池%.0f",
				snap.Tick, delta, explained, resid,
				f.GovTax, f.GovOperating, f.GovPowerSpend,
				f.GovPowerRevenue, f.GovSubsidy, f.GovWelfare, f.GovPublicWorks, f.GovWarehouseExpand,
				building, st.balGov(), st.balCap(), house, st.balInvest())
		}
		prev = st.balGov()
	}
}

// TestMoneyIsConserved 是 §4.5.3「总流通货币不变」的守门测试。
//
// 这是修订后 1.0 最核心的不变量：系统没有造币/销毁机制，
// 因此"建筑 + 政府 + 资本 + 人群"四类账户之和必须逐 tick 恒定。
//
// 唯一允许的 Δ 来源是 §4.3 的"新建筑营运本金"（每级 5000 元），
// 它只在完工 tick 发生；故本测试把该部分单独扣除后再比较。
//
// 修订前实测每 tick 净损 15%~34%，5 个 tick 后总货币为负
// （docs/ACTIVE.md §6.5）。本测试就是防止它复发。
func TestMoneyIsConserved(t *testing.T) {
	st := newTestState(t)
	m0 := st.TotalMoney()
	if m0 <= 0 {
		t.Fatalf("初始货币存量 = %.2f，必须为正（§4.3 修订：按周工资流量标定）", m0)
	}
	prev := m0
	for i := 1; i <= 200; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", i, err)
		}
		// 新完工建筑的营运本金是契约允许的货币注入，单独计量后从 Δ 中扣除。
		var injected float64
		if snap.Flow.PowerBought >= 0 { // 占位，实际注入量由 NewCapital 记录
			injected = snap.NewCapital
		}
		delta := st.TotalMoney() - prev - injected
		if math.Abs(delta) > 1e-3 {
			t.Fatalf("tick %d 货币守恒被破坏：Δ=%.6f（注入本金 %.2f，扣除后 %.6f）\n存量：建筑%.0f 政府%.0f 资本%.0f 人群%.0f",
				snap.Tick, st.TotalMoney()-prev, injected, delta,
				buildingCashOf(st), st.balGov(), st.balCap(), st.Houses.TotalCash())
		}
		if st.InvariantErr != nil {
			t.Fatalf("tick %d 记账簿不变量被破坏：%v", snap.Tick, st.InvariantErr)
		}
		prev = st.TotalMoney()
	}
}

// buildingCashOf 返回建筑现金池总额（诊断用）。
func buildingCashOf(s *State) float64 {
	var m float64
	for i := range s.Buildings {
		m += s.bal(i)
	}
	return m
}

// TestWagesAreActuallyPaid 校验 §5.1 修订的核心：
// 工资必须从建筑现金池【真实划转】到人群现金池，而不是只作为成本被扣减。
//
//	Δ建筑现金池 ⊇ −工资总额
//	Δ人群现金池 = +工资总额 + 福利金 − 本 tick 消费支出 − 消费交易税 − 居民储蓄
//
// 这是"居民拥有资产负债表"的最低可验证含义。
//
// 【前值 → 后值（2026-09-19，第 15 轮）】§5.3 新增"工资结余 → 储蓄 → 投资"渠道，
// 且默认储蓄率 σ_save = 1.0 ⇒ 每个 tick 末人群池被清空（结余全部转入投资池），
// 因此原先"期末余额 > 期初余额"的断言不再成立（实测 0.00 → 0.00）。
// 本测试因此**显式注入 σ_save = 0**（场景注入，见日志），专门校验工资的实际划转；
// 储蓄渠道本身由 TestSavingsChannelMovesWageSurplusToInvestment 单独校验。
//
// 【前值 → 后值（第 20 轮）】储蓄改成**两步**：第一步把结余**全额**搬进储蓄固定账户，
// 第二步才按 σ 从该账户转投资。σ_save = 0 时渠道整体关闭（两步都不发生），
// 结余留在人群池——"工资到账"的断言因此仍然成立，且额外断言储蓄账户为 0。
func TestWagesAreActuallyPaid(t *testing.T) {
	st := newTestState(t)
	sc := newScenario(t, "隔离储蓄渠道（σ_save = 0），单独校验工资是否真实付出")
	sc.Param("SavingsRate",
		func(s *State) float64 { return s.Params.SavingsRate },
		func(s *State, v float64) { s.Params.SavingsRate = v },
		0,
		"§5.3 的储蓄渠道会在 tick 末把人群池结余转入储蓄账户（σ=0 时停在第一步）；要检验'工资到账'，必须先把该渠道关闭")
	sc.Apply(st)

	prevHouse := st.Houses.TotalCash()
	prevSavings := st.balSavings()
	prevNet := prevHouse + prevSavings
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	wage := snap.Flow.WageTotal
	if wage <= 0 {
		t.Fatalf("工资总额 = %.2f，应为正", wage)
	}
	// 居民在本 tick 内的收支：先收工资（与福利金），再付出消费（含税）与储蓄。
	// 故期末净头寸 = 期初 + 工资 + 福利金 − 消费净额 − 消费税 − σ×储蓄。
	// （σ=0 时第二步不发生，储蓄账户留存全额结余。）
	wantLeft := prevNet + wage + snap.Welfare - snap.SpendNet - snap.Flow.ConsumerTax - snap.SavingInvest
	gotNet := st.Houses.TotalCash() + st.balSavings()
	if math.Abs(gotNet-wantLeft) > 1e-3 {
		t.Errorf("居民净头寸（人群池 + 储蓄账户）期末 = %.4f，应为 %.4f\n"+
			"（期初 %.2f + 工资 %.2f + 福利金 %.2f − 消费净 %.2f − 消费税 %.2f − 转投资 %.2f）",
			gotNet, wantLeft, prevNet, wage, snap.Welfare, snap.SpendNet, snap.Flow.ConsumerTax, snap.SavingInvest)
	}
	if gotNet <= prevNet {
		t.Errorf("居民净头寸未增加（%.2f → %.2f），工资没有真正付出", prevNet, gotNet)
	}
	// σ = 0 ⇒ 储蓄渠道整体关闭（两步都不发生），结余留在人群池；
	// 这条断言正是"隔离"二字的落地处。
	if snap.SavingInvest != 0 {
		t.Errorf("σ_save = 0 时转入投资池 = %.4f，应为 0", snap.SavingInvest)
	}
	if snap.Saving != 0 {
		t.Errorf("σ_save = 0 时进入储蓄账户 = %.4f，应为 0（渠道整体关闭）", snap.Saving)
	}
	if st.balSavings() != 0 {
		t.Errorf("σ_save = 0 时储蓄固定账户 = %.4f，应为 0", st.balSavings())
	}
}

// TestSavingsChannelMovesWageSurplusToInvestment 校验 §5.3 的储蓄渠道
// （2026-09-19 第 15 轮裁决："显式定义「工资结余 → 储蓄 → 投资」渠道"）。
//
// 断言（默认 σ_save = 1.0）：
//  1. 本 tick 的储蓄额恰等于"期初 + 工资 + 福利金 − 消费净 − 消费税"（结余全储蓄）；
//  2. 期末人群池余额 ≈ 0（结余被清空，不再沉积）；
//  3. 投资池确实收到这笔钱（它同时也是资本净额入池的接收方，故只断言增量 ≥ 储蓄额
//     —— 投资池还可能在同期收到资本建筑入池）。
//
// 【它修的是什么】R29/R30 实测：数量篮子只值该档工资的 21.5%~28.8%，
// 结余此前没有出口，沉积在人群池（tick 1000 达 2.58e9），
// 在 R26 的资金口径下表现为"投资池恒为 0、经济零建造"。
func TestSavingsChannelMovesWageSurplusToInvestment(t *testing.T) {
	st := newTestState(t)
	if st.Params.SavingsRate <= 0 {
		t.Fatalf("契约默认储蓄率 = %g，应 > 0（§5.3 默认 1.0）", st.Params.SavingsRate)
	}
	prevHouse := st.Houses.TotalCash()
	prevInvest := st.balInvest()
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	want := prevHouse + snap.Flow.WageTotal + snap.Welfare - snap.SpendNet - snap.Flow.ConsumerTax
	if math.Abs(snap.Saving-want) > 1e-3 {
		t.Errorf("本 tick 储蓄 = %.4f，应为结余 %.4f（期初 %.2f + 工资 %.2f + 福利金 %.2f − 消费净 %.2f − 消费税 %.2f）",
			snap.Saving, want, prevHouse, snap.Flow.WageTotal, snap.Welfare, snap.SpendNet, snap.Flow.ConsumerTax)
	}
	if snap.Saving <= 0 {
		t.Fatalf("储蓄额 = %.4f，应 > 0（工资结余必须存在——R30：篮子只值工资的 21.5%%~28.8%%）", snap.Saving)
	}
	if got := st.Houses.TotalCash(); math.Abs(got) > 1e-6 {
		t.Errorf("σ_save = 1 时期末人群池余额 = %.6f，应为 0（结余全部储蓄）", got)
	}
	// 【§5.3 第 20 轮】储蓄是两步：结余全额进储蓄固定账户，再按 σ 转投资。
	// σ = 1 ⇒ 第二步把该账户**全部**转走，故期末储蓄账户必须归零。
	if got := st.balSavings(); math.Abs(got) > 1e-6 {
		t.Errorf("σ_save = 1 时期末储蓄固定账户 = %.6f，应为 0（每周期全部转移入投资池）", got)
	}
	if math.Abs(snap.SavingInvest-snap.Saving) > 1e-3 {
		t.Errorf("σ_save = 1 时转入投资 = %.4f，应等于本期结余 %.4f", snap.SavingInvest, snap.Saving)
	}
	// 投资池是**流量池**：本 tick 既有流入（居民储蓄 + 两个资本建筑的净额入池），
	// 也有流出（G6 向政府偿还建造力货款、以及**第 28 轮的收购对价**），
	// 故不能用"增量 ≥ 储蓄"断言，必须用完整的流量恒等式：
	//
	//	Δ = 转入投资 + 入池_m + 入池_f − 付政府 − 收购对价
	wantInvest := snap.SavingInvest + snap.InvestmentInflowManor + snap.InvestmentInflowFinance -
		snap.InvestmentPaid - snap.AcquirePaid
	if delta := st.balInvest() - prevInvest; math.Abs(delta-wantInvest) > 1e-3 {
		t.Errorf("投资池 Δ = %.2f，应为 %.2f（转投资 %.2f + 入池_m %.2f + 入池_f %.2f − 付政府 %.2f − 收购 %.2f）",
			delta, wantInvest, snap.SavingInvest, snap.InvestmentInflowManor, snap.InvestmentInflowFinance,
			snap.InvestmentPaid, snap.AcquirePaid)
	}
}

// TestUnemployedPoolCannotConsumeWithoutIncome 校验 §6.5（2026-09-19 第 15 轮裁决）：
// 失业人口算劳工、单独占用一个资金池，**没有收入就无法消费**，
// 因而其满足度与幸福度均为 0，并通过人口增长条件拉低人口增速。
//
// 【它修的是什么】修正前"有人口但预算 ≤ 0"的池被记为满足度 = 1（空真通过），
// 于是失业者被当作"过得很好"，人口增长条件（§6.5）完全不生效——
// R15/R34 实测人口复利到 1.17e11 / 2.34e11 而 A5 仍判"通过"。
//
// 【布点】用 **20m 人口 + 耕地 5,000**：只有在自给农场满编之后才会出现失业
// （R23 实测 tick ~520 起）。10m 布点下 60 tick 内没有失业，断言会被空真通过。
func TestUnemployedPoolCannotConsumeWithoutIncome(t *testing.T) {
	st, err := New(Options{
		Population:           20_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	// 福利金关闭（默认 0）：失业池没有任何收入。
	st.Params.WelfareTier = 0
	// 【为什么用"搜索首个失业 tick"而不是固定 600 tick】失业出现与否取决于
	// 人口（自给农场容量 24.95M）与市场岗位的差额，而人口增长又受人满足度反馈
	// （满足度低 ⇒ 人口回落 ⇒ 失业再次归零）。实测失业在 tick ~500 与 ~1100
	// 两次出现，600 tick 恰好落在两次之间的空窗。故这里搜索到出现为止。
	found := false
	for i := 0; i < 1200; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step: %v", err)
		}
		if st.Unemployed > 1 {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("1200 tick 内从未出现失业——失业池的断言被空真通过")
	}
	idx := cohortIndex(st, model.UnemployedSite, 0)
	if idx < 0 {
		t.Fatal("找不到失业资金池")
	}
	// 失业池无收入 ⇒ 满足度 0（不是 1）。
	if s := st.Last.Pools[idx].Sat[model.GroupBasicFood]; s > 1e-12 {
		t.Errorf("失业池的基础食物满足度 = %.6f，应为 0（无收入 ⇒ 无法消费）", s)
	}
	if st.Last.NoIncome == 0 {
		t.Errorf("存在失业 %.0f 人，但 NoIncome 池数 = 0 —— 失业池没有被识别为「无收入」", st.Unemployed)
	}
	// 幸福度总体必须低于"全体就业"的 1.0。
	if h := st.Last.Happiness(); h >= 1.0 {
		t.Errorf("存在 %.0f 失业人口时总体幸福度 = %.4f，不应为 1.0", st.Unemployed, h)
	}
	t.Logf("失业 %.0f 人；NoIncome 池数 = %d；幸福度 %.4f（劳工 %.4f）",
		st.Unemployed, st.Last.NoIncome, st.Last.Happiness(), st.Last.HappinessByClass(0))
}

// TestDebtNeverExceedsCapInPipeline 校验债务上限在真实管线中是硬约束：
// 政府债务不得超过 §4.5.4 的上限。
//
// 允许超限的唯一途径是被动的经营亏损累积（政府无法阻止自己持有亏损部门），
// 但由本 tick 的主动支出造成的超限必须被阻止。
//
// 【前值 → 后值（2026-09-19 第 15 轮）】判据的"主动支出"口径改为**净支出**：
// §4.5.1b 的 G2 采购由投资池在**同一 tick 内全额偿还**（净 0），它不会抬高期末债务；
// 真正可能推高债务的是不被偿还的开支（§4.5.8 的公共工程、福利金、§4.5.7 的补贴）。
// 旧口径把"采购"当作主动支出，于是在"国库已触顶、靠同一 tick 的偿还完成采购"时
// 误报（实测 tick 82：采购 167.7 万、债务超上限 1030 元 = 0.03%，而该笔采购的净腿为 0）。
func TestDebtNeverExceedsCapInPipeline(t *testing.T) {
	st := newTestState(t)
	for i := 0; i < 120; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		// 净支出 = 采购 − 投资池偿还 + 公共工程 + 福利金 + 补贴
		// （采购的偿还腿在同一 tick 内完成，故净额为 0）。
		netSpend := snap.Flow.GovPowerSpend - snap.Flow.GovPowerRevenue +
			snap.PublicWorks + snap.Welfare + snap.Flow.GovSubsidy
		if netSpend > 1 && snap.GovDebt > snap.GovDebtCap+tolSlack {
			t.Fatalf("tick %d：有净支出（%.2f：采购 %.2f − 偿还 %.2f + 公共工程 %.2f + 福利 %.2f + 补贴 %.2f）"+
				"但债务 %.2f 超过上限 %.2f",
				snap.Tick, netSpend, snap.Flow.GovPowerSpend, snap.Flow.GovPowerRevenue,
				snap.PublicWorks, snap.Welfare, snap.Flow.GovSubsidy,
				snap.GovDebt, snap.GovDebtCap)
		}
	}
}

// tolSlack 是债务上限比较的容差（浮点与分步结算的余量）。
const tolSlack = 1.0
