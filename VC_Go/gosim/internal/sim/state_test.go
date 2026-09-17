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
// 否则测的是另一套经济。故统一到 10_000_000（= 契约修订后的默认人口）。
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
		FinanceBuildCost:     400,
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
func TestInitialCalibrationClosesMarket(t *testing.T) {
	st := newTestState(t)
	net := st.netSupply(st.calibration)
	for i := range st.Goods {
		g := st.Goods[i]
		d := st.Market.Markets[i].A * math.Pow(g.Pinit/g.Pcost, -g.Eps)
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
// 推进若干 tick 后，政府必须确实采购过建造力。
//
// 这是"建造力财政黑洞"是否被真正补上的直接检验。若本测试失败，
// 说明政府现金池在开局就被耗空（V2/V3 版本的崩解模式），
// 应检查现金池初值是否与一个周期的税收规模同阶。
func TestGovernmentCanPurchasePower(t *testing.T) {
	st := newTestState(t)
	snaps, err := st.Run(120)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	var bought float64
	for _, s := range snaps {
		bought += s.PowerPurchased
	}
	if bought <= 0 {
		t.Fatal("120 个 tick 内政府采购建造力始终为 0——货币闭环未接上")
	}
	t.Logf("120 tick 累计采购建造力 = %.2f 单位", bought)
}

// TestSoldPowerNeverExceedsPurchased 校验建造力不会被凭空出售。
func TestSoldPowerNeverExceedsPurchased(t *testing.T) {
	st := newTestState(t)
	snaps, err := st.Run(200)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	for _, s := range snaps {
		if s.PowerSold > s.PowerPurchased+1e-6 {
			t.Fatalf("tick %d: 售出 %.6f 超过采购 %.6f", s.Tick, s.PowerSold, s.PowerPurchased)
		}
	}
}

// TestSitePowerLimitInPipeline 校验 §4.2 的每工地 30 建造力上限在管线里生效。
func TestSitePowerLimitInPipeline(t *testing.T) {
	st := newTestState(t)
	for i := 0; i < 200; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		for _, o := range st.Orders {
			if o.Progress > st.Params.SitePowerLimit+1e-6 {
				t.Fatalf("订单投入 %.6f 超过每工地上限 %.1f", o.Progress, st.Params.SitePowerLimit)
			}
		}
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
//	Δ现金池 = 税收 + 经营净额 − 采购支出 + 售力收入 − 政府自有项目付款
//
// 残差恒为 0 才算通过。这条测试的价值来自历史：曾出现"政府经营净额被记两次"
// 的 bug（DistributeProfit 内部已入账，调用方又累加并再次入账），
// 表现为残差恰等于 −经营净额；不变量测试能立刻抓到，而单看余额曲线看不出来。
//
// §4.5.3 修订后本测试新增了 GovBuildoutPaid 一项：全过程交易税让政府
// 自有项目的付款也真实减少现金池，若分解式漏掉它，残差会恰等于该笔付款。
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
		// "政府收自己的税"要扣除：它计入 GovTax，但不是政府池的净流入。
		explained := (f.GovTax - f.GovSelfTax) + f.GovOperating - f.GovPowerSpend +
			f.GovPowerRevenue - f.GovBuildoutPaid
		if resid := delta - explained; math.Abs(resid) > tol {
			var building, house float64
			for i := range st.Buildings {
				building += st.bal(i)
			}
			house = st.Houses.TotalCash()
			t.Fatalf("tick %d 资金流未守恒：Δ现金=%.2f 已解释=%.2f 残差=%.2f\n"+
				"（税收 %.2f / 经营 %.2f / 采购 %.2f / 售力 %.2f / 政府项目付款 %.2f）\n"+
				"四池：建筑%.0f 政府%.0f 资本%.0f 人群%.0f",
				snap.Tick, delta, explained, resid,
				f.GovTax, f.GovOperating, f.GovPowerSpend,
				f.GovPowerRevenue, f.GovBuildoutPaid,
				building, st.balGov(), st.balCap(), house)
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
// （docs/AUDIT-1.0.md §3）。本测试就是防止它复发。
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
//	Δ人群现金池 = +工资总额 − 本 tick 消费支出 − 消费交易税
//
// 这是"居民拥有资产负债表"的最低可验证含义。
func TestWagesAreActuallyPaid(t *testing.T) {
	st := newTestState(t)
	prevHouse := st.Houses.TotalCash()
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	wage := snap.Flow.WageTotal
	if wage <= 0 {
		t.Fatalf("工资总额 = %.2f，应为正", wage)
	}
	// 居民在本 tick 内的收支：先收工资，再付出消费（含税）。
	// 故期末余额 = 期初 + 工资 − 消费净额 − 消费税。
	wantLeft := prevHouse + wage - snap.SpendNet - snap.Flow.ConsumerTax
	if got := st.Houses.TotalCash(); math.Abs(got-wantLeft) > 1e-3 {
		t.Errorf("人群池期末现金 = %.4f，应为 %.4f\n"+
			"（期初 %.2f + 工资 %.2f − 消费净 %.2f − 消费税 %.2f）",
			got, wantLeft, prevHouse, wage, snap.SpendNet, snap.Flow.ConsumerTax)
	}
	if st.Houses.TotalCash() <= prevHouse {
		t.Errorf("人群池现金未增加（%.2f → %.2f），工资没有真正付出",
			prevHouse, st.Houses.TotalCash())
	}
}

// TestDebtNeverExceedsCapInPipeline 校验债务上限在真实管线中是硬约束：
// 政府债务不得超过 §4.5.4 的上限。
//
// 允许超限的唯一途径是被动的经营亏损累积（政府无法阻止自己持有亏损部门），
// 但由本 tick 的主动支出造成的超限必须被阻止。
func TestDebtNeverExceedsCapInPipeline(t *testing.T) {
	st := newTestState(t)
	for i := 0; i < 120; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		// 若本 tick 有主动支出（采购或政府项目付款），则期末债务必须仍在上限内
		spent := snap.Flow.GovPowerSpend > 0
		if spent && snap.GovDebt > snap.GovDebtCap+tolSlack {
			t.Fatalf("tick %d：有主动支出（采购 %.2f）但债务 %.2f 超过上限 %.2f",
				snap.Tick, snap.Flow.GovPowerSpend, snap.GovDebt, snap.GovDebtCap)
		}
	}
}

// tolSlack 是债务上限比较的容差（浮点与分步结算的余量）。
const tolSlack = 1.0
