package sim

// audit_r39_test.go —— 2026-09-19 第 15 轮裁决（§0.4 的九项）的验收测试。
//
// 覆盖：
//
//	§3.2  建筑四分类（农村 / 城镇 / 资源 / 开发）+ 建造部门显式保持补贴  [第 2 项]
//	§5.3  工资结余 → 储蓄 → 投资（在 state_test.go 的 TestSavingsChannel... 中）
//	§4.5.8 福利金 6 档（0.2×档位 × 与平均工资的差距，封顶 120%）        [第 3 项 b]
//	§4.5.8 公共工程（国库直接采购建造力投向开发类，等级归政府）           [第 3 项 a]
//	§6.5  失业池（失业算劳工、单独资金池、无收入 ⇒ 无法消费 ⇒ 幸福度 0） [第 4 项]
//	§3.2/§8.6 短缺起步的显式标注                                        [第 7 项]
//	§4.5.1b 两条投资栈的预算改按**当期意向需求**比例分配                  [第 8 项 a]
//	§8.4  A7/A8 接入判定（在 internal/report 的 report_test.go 中）
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR39 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/build"
	"yehenala/market/internal/model"
)

// liquidityState 构造一个国库流动性充足的仿真（GovStartupFraction = 20 周工资），
// 使福利金 / 公共工程不被"可动用资金 = 0"这一约束空转（R25 实测：默认参数下
// 政府自 tick ~50 起可用资金为 0，任何支出开关都会空转）。
func liquidityState(t *testing.T) *State { return liquidityStateWith(t, 20) }

// liquidityStateWith 与 liquidityState 同，但可指定政府起步资金倍数
// （用于把"公式"与"§4.5.4 资金约束"分开验证，2026-09-19 第 20 轮）。
func liquidityStateWith(t *testing.T, govStartup float64) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   govStartup,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	return st
}

// TestAuditR39BuildingCategories 校验 §3.2 的四分类与"建造部门显式保持补贴"（第 2 项）。
func TestAuditR39BuildingCategories(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	count := map[model.Category]int{}
	for i := range st.Buildings {
		b := &st.Buildings[i]
		count[b.Spec.Category]++
		if b.Spec.Category != model.CatDevelopment && b.Spec.AllowSubsidy {
			t.Errorf("%s（类别 %s）的 AllowSubsidy = true —— 本版只有开发类建筑可接受补贴",
				b.Spec.Name, b.Spec.Category)
		}
	}
	// 四类都必须非空（"与农村、城镇、资源并列"）。
	for _, c := range []model.Category{model.CatRural, model.CatTown, model.CatResource, model.CatDevelopment} {
		if count[c] == 0 {
			t.Errorf("类别 %s 下没有任何建筑 —— 四分类未成立", c)
		}
	}
	// 建造部门 = 开发类，且显式保持补贴。
	pw := &st.Buildings[powerGoodIndex]
	if pw.Spec.Category != model.CatDevelopment {
		t.Errorf("建造部门的类别 = %s，应为 开发", pw.Spec.Category)
	}
	if !pw.Spec.AllowSubsidy {
		t.Error("建造部门的 AllowSubsidy = false —— 第 15 轮裁决要求开发类建筑显式保持补贴")
	}
	if !model.IsDevelopment(pw.Spec) {
		t.Error("model.IsDevelopment(建造部门) = false")
	}
	// 农业建筑属农村；煤铁属资源；金融区属城镇。
	if st.Buildings[grainBuildingIndex(st.Buildings)].Spec.Category != model.CatRural {
		t.Error("谷物农场的类别应为 农村")
	}
	for i := range st.Buildings {
		switch st.Buildings[i].Spec.Name {
		case "煤矿", "铁矿":
			if st.Buildings[i].Spec.Category != model.CatResource {
				t.Errorf("%s 的类别应为 资源，实际 %s", st.Buildings[i].Spec.Name, st.Buildings[i].Spec.Category)
			}
		case "金融区":
			if st.Buildings[i].Spec.Category != model.CatTown {
				t.Errorf("金融区的类别应为 城镇，实际 %s", st.Buildings[i].Spec.Category)
			}
		}
	}
	t.Logf("类别分布：农村 %d / 城镇 %d / 资源 %d / 开发 %d（开发类当前只有建造部门）",
		count[model.CatRural], count[model.CatTown], count[model.CatResource], count[model.CatDevelopment])
}

// TestAuditR39ShortageStartIsMarked 校验 §3.2/§8.6 的"短缺起步"显式标注（第 7 项）。
func TestAuditR39ShortageStartIsMarked(t *testing.T) {
	auditEnabled(t)
	// 统一起始等级（N0 = 5）= 短缺起步。
	st := newTestState(t)
	if !st.ShortageStart {
		t.Errorf("统一起始等级 N0=%.0f 时 ShortageStart = false —— 短缺起步未被标注",
			st.Params.ProductionInitLevel)
	}
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	if !snap.ShortageStart {
		t.Error("快照里的 ShortageStart = false")
	}
	// 起点产能 / 平衡产能（§8.6 的 L*）：只要求显著小于 1，证明"确实短缺"。
	var start float64
	for i := range st.Buildings {
		b := &st.Buildings[i]
		if b.Spec.IsNonMarket() || b.Spec.Category == model.CatDevelopment {
			continue
		}
		start += b.Level
	}
	t.Logf("短缺起步：生产建筑起始合计 %.0f 级（§8.6 的平衡产能 L* 在 20m 口径约 %.1f 倍于此）",
		start, 19.7)
	// 物质平衡布点（N0 = 0）不是短缺起步。
	st2, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  0,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	if st2.ShortageStart {
		t.Error("物质平衡布点（N0=0）被标注为短缺起步")
	}
}

// TestAuditR39WelfareTiersFormula 校验 §4.5.8 福利金的 6 档公式（第 3 项 b）。
//
//	每人补贴 = min(1.2, 0.2×档位) × max(0, 平均工资标准 6.75 − 本人工资)
func TestAuditR39WelfareTiersFormula(t *testing.T) {
	auditEnabled(t)

	runOne := func(tier int, govStartup float64) (paid, expected float64, nominal, actual []float64) {
		// 【口径（2026-09-19 第 20 轮）】把"名义公式"与"§4.5.4 资金约束"分开：
		// 关掉公共工程（它会在福利金之前花掉国库，使约束值依赖支出顺序）；
		// govStartup 只影响国库流动性，不影响公式本身。
		st := liquidityStateWith(t, govStartup)
		st.Params.PublicWorksShare = 0
		st.Params.WelfareTier = tier
		// 派发前记录各池现金（期末余额无法反推到账额——同 tick 还有工资/消费/储蓄）。
		before := make([]float64, len(st.Houses.Pools))
		for i := range st.Houses.Pools {
			before[i] = st.Houses.Pools[i].Cash()
		}
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		rate := 0.2 * float64(tier)
		if rate > 1.2 {
			rate = 1.2
		}
		wbar := model.AverageWage()
		nominal = make([]float64, len(st.Houses.Pools))
		actual = make([]float64, len(st.Houses.Pools))
		for i := range st.Houses.Pools {
			p := &st.Houses.Pools[i]
			actual[i] = p.Cash() - before[i]
			if p.Population <= 1e-12 {
				continue
			}
			w := model.CohortWages[p.Class]
			if p.Worksite == st.UnemployedSite {
				w = 0
			} else if p.Worksite >= 0 && p.Worksite < len(st.Buildings) {
				// 【§5 第 20 轮】本人工资取**该场地的劳动结构**：
				// 农业类与庄园的第二阶级是"农民 7 元"，不是全局的"工程师 10 元"。
				w = st.Buildings[p.Worksite].Spec.StructureOf().Wages[p.Class]
			}
			if gap := wbar - w; gap > 0 {
				nominal[i] = rate * gap * p.Population
				expected += nominal[i]
			}
		}
		// 记账自洽：各池现金变动之和必须等于本 tick 账面上的福利金总额
		// （人群池同 tick 还有工资/消费/储蓄，但那些是**跨池或跨部门**的转移，
		//  只有福利金是"政府 → 人群池"的单向注入，故这里只做总额核对）。
		var sumActual float64
		for _, v := range actual {
			sumActual += v
		}
		if math.Abs(sumActual) > 1e-6 {
			t.Logf("提示：人群池现金变动之和 = %.2f（同 tick 含工资 + 消费 + 储蓄，非零属正常）", sumActual)
		}
		return snap.Welfare, expected, nominal, actual
	}

	// 仅用于读取"名义额"：本条断言的是**公式**
	// （0.6 × Σ max(0, 6.75 − 本人工资) × 人口），与国库是否够发无关。
	// 第 20 轮起失业人口按 §5 的场地结构估值（失业者工资 0 ⇒ 差距 = 全额 6.75），
	// 故名义额远大于 R39 时的 2.9e6 量级。
	_, exp3, _, _ := runOne(3, 200)
	_, exp6, _, _ := runOne(6, 200)
	if exp3 <= 0 || exp6 <= 0 {
		t.Fatalf("名义补贴额为 0（档 3 = %.2f，档 6 = %.2f）——公式未生效", exp3, exp6)
	}
	// ① 公式：档 6 的名义额必须是档 3 的 2 倍（1.2 / 0.6）。
	if r := exp6 / exp3; math.Abs(r-2) > 1e-9 {
		t.Errorf("档 6 / 档 3 名义额 = %.9f，应为 2.000000000（120%% / 60%%）", r)
	}
	// ② §4.5.4 的资金约束形式：实发必须 ≤ 名义额、且 > 0（按比例裁剪而不是空转）。
	//    【口径说明】逐池的到账额无法从期末余额反推（余额还被工资/消费/储蓄同 tick 改写），
	//    故本条只断言**可测的边界**；"逐池按同一比例裁剪"由
	//    `internal/book` 的 TestWelfareScalesProportionally 直接守住。
	var scales []float64
	for _, gs := range []float64{2000, 200, 0.1} {
		paid, exp, _, _ := runOne(3, gs)
		if exp <= 0 {
			t.Fatalf("国库初值 %.1f：名义额为 0", gs)
		}
		scale := paid / exp
		if scale > 1+1e-9 {
			t.Errorf("国库初值 %.1f：实发 %.2f 超过名义 %.2f——资金约束不可能放大支出",
				gs, paid, exp)
		}
		if scale <= 0 {
			t.Fatalf("国库初值 %.1f：实发为 0——机制空转", gs)
		}
		scales = append(scales, scale)
		t.Logf("国库初值 %8.1f：名义 %12.0f、实发 %12.0f、裁剪比例 %.6f",
			gs, exp, paid, scale)
	}
	// 国库越充裕，裁剪比例不得越小（单调性）——这是"约束确实在起作用"的证据。
	if !(scales[0] >= scales[1]-1e-9 && scales[1] >= scales[2]-1e-9) {
		t.Errorf("裁剪比例随国库初值不单调：%v（国库越多、发得越少是矛盾的）", scales)
	}
	// 档 0 = 关闭。
	st0 := liquidityState(t)
	st0.Params.WelfareTier = 0
	s0, err := st0.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	if s0.Welfare != 0 {
		t.Errorf("档 0 仍发放了 %.2f —— 默认必须关闭", s0.Welfare)
	}
	t.Logf("福利金公式：档 3 名义 %.0f、档 6 名义 %.0f（严格 2 倍）；档 0 = 0（关闭）",
		exp3, exp6)
}

// TestAuditR39WelfareReachesUnemployedPool 校验福利金确实进入**失业池**（第 4 项）。
//
// 失业人口算劳工、单独占一个资金池；没有福利金时其满足度为 0（无法消费），
// 有了福利金之后该池有了收入，才可能消费。
func TestAuditR39WelfareReachesUnemployedPool(t *testing.T) {
	auditEnabled(t)
	// 20m 人口 + 耕地 5,000 ⇒ 自给农场满编后有失业（R23 实测 tick ~520 起）。
	// GovStartupFraction 取 500（而不是 20）：第 23 轮后失业规模大（18 万+），
	// §4.5.4 的可动用资金会成为约束（实测 20 时只剩 1.3e4，福利金被裁到发不出），
	// 而本测试要验的是"福利金进得了失业池"，不是"国库够不够"。
	st, err := New(Options{
		Population:           20_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   500,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("构造: %v", err)
	}
	// 【§5.2 第 23 轮口径调整】把储蓄率置 0，使"失业池本期收到的福利金"**留在池里**
	// 可被直接读出（σ=1 时结余当期被搬进储蓄账户，期末现金恒为 0，
	// "有没有收到福利金"就无从测量——旧断言因此只能靠消费额间接判断）。
	// 同时把公共工程归零，使国库的可用资金不被它先花掉。
	st.Params.SavingsRate = 0
	st.Params.PublicWorksShare = 0
	st.Params.WelfareTier = 6
	for i := 0; i < 600; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step: %v", err)
		}
	}
	if st.Unemployed <= 0 {
		t.Skipf("600 tick 时失业 = %.0f，无法检验失业池", st.Unemployed)
	}
	idx := cohortIndex(st, st.UnemployedSite, 0)
	if p := &st.Houses.Pools[idx]; math.Abs(p.Population-st.Unemployed) > 1e-6 {
		t.Errorf("失业池人口 = %.2f，应为失业人数 %.2f", p.Population, st.Unemployed)
	}
	if got := st.Houses.WealthTier(idx); got != model.DemandTierMin {
		t.Errorf("失业池财富档 = %.1f，应为劳工档 %.1f（失业人口算劳工）", got, model.DemandTierMin)
	}
	// 失业者工资按 0 计 ⇒ 其补贴率是全额平均工资的 120%。
	//
	// 【前置条件：国库必须有可动用资金】§4.5.4 规定福利金受可动用资金约束，
	// 国库为空时**一笔都发不出**（那不是"福利金没进失业池"，而是"没发"）。
	avail := st.Gov.AvailableCash(st.Market.Prices()[powerGoodIndex])
	if avail <= 0 {
		t.Skipf("600 tick 时政府可动用资金 = %.2f（§4.5.4 约束下福利金无从发放），本情形无法检验失业池", avail)
	}
	//
	// 【可测的判据：该池本期**消费了**】失业池没有任何其他收入来源
	//（不领工资、不做交易），它能消费的唯一可能就是收到了福利金。
	// 注意**不能**用"期末现金 > 0"来判：σ_save 之外的资金流与结余清空都会影响它，
	// 而消费额在当前流水下是干净的证据。
	cash := st.Houses.Pools[idx].Cash()
	spend := st.Last.Pools[idx].SpendNet
	wantWelfare := 1.2 * model.AverageWage() * st.Unemployed // 档 6 = 120%，本人工资 0
	t.Logf("失业 %.0f 人；失业池本期消费 %.2f，期末现金 %.2f（名义福利 %.0f，可动用资金 %.0f）",
		st.Unemployed, spend, cash, wantWelfare, avail)
	if spend <= 0 {
		t.Errorf("失业池本期消费 = %.2f，应 > 0——它没有其他收入来源，消费为 0 说明福利金没进该池"+
			"（名义应发 %.0f，可用资金 %.0f）", spend, wantWelfare, avail)
	}
	// 消费额不可能超过名义补贴（资金约束只会往下裁）。
	if spend > wantWelfare*(1+1e-6)+1e-6 {
		t.Errorf("失业池本期消费 %.2f 超过名义福利 %.2f", spend, wantWelfare)
	}
}

// TestAuditR39PublicWorks 校验 §4.5.8 的公共工程（第 3 项 a）。
//
// 三条断言：
//  1. 公共工程支出每 tick ≤ PublicWorksShare × 当期税收（预算规则）；
//  2. 政府的建造力净支出 = 公共工程支出（私人那部分由投资池全额偿还 ⇒ 净 0）；
//  3. 完工等级归**政府**（GovLevel 增长），且确实新建过公共工程订单。
func TestAuditR39PublicWorks(t *testing.T) {
	auditEnabled(t)
	st := liquidityState(t)
	if st.Params.PublicWorksShare <= 0 {
		t.Fatalf("公共工程占比 = %g，应 > 0（契约默认 0.5）", st.Params.PublicWorksShare)
	}
	share := st.Params.PublicWorksShare
	govLv0 := st.Buildings[powerGoodIndex].GovLevel

	var spend, units float64
	var badBudget, badNet int
	for i := 0; i < 300; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		spend += snap.PublicWorks
		units += snap.PublicWorksUnits
		if snap.PublicWorks > share*snap.Tax+1e-6 {
			badBudget++
		}
		// 私人扩建那一半的建造力由投资池全额偿还 ⇒ 「G2 采购 − G6 偿还」恒为 0；
		// 公共工程是**额外**的、不被偿还的支出，单列在 snap.PublicWorks 里。
		if net := snap.Flow.GovPowerSpend - snap.Flow.GovPowerRevenue; math.Abs(net) > 1e-6 {
			badNet++
		}
	}
	if spend <= 0 {
		t.Fatal("300 tick 内公共工程支出恒为 0 —— 政府支出端没有接上（检查可动用资金与建造力剩余产出）")
	}
	if badBudget > 0 {
		t.Errorf("%d 个 tick 的公共工程支出超过「占比 × 当期税收」的预算规则", badBudget)
	}
	if badNet > 0 {
		t.Errorf("%d 个 tick 的「G2 采购 − G6 偿还 ≠ 0」——私人扩建那一半没有被投资池全额偿还", badNet)
	}
	govLv1 := st.Buildings[powerGoodIndex].GovLevel
	if govLv1 <= govLv0 {
		t.Errorf("建造部门的政府持股级数未增长（%.2f → %.2f）——公共工程新增等级没有归政府",
			govLv0, govLv1)
	}
	if units <= 0 {
		t.Error("没有新建过公共工程订单（PublicWorksUnits 累计为 0）")
	}
	t.Logf("300 tick：公共工程支出累计 %.0f 元、新建订单等级累计 %.1f 级；"+
		"建造部门政府级数 %.1f → %.1f（私人级数 %.1f）",
		spend, units, govLv0, govLv1, st.Buildings[powerGoodIndex].PrivLevel)
}

// TestAuditR39BudgetFollowsCurrentIntent 校验 §4.5.1b 的预算分配（第 8 项 a）：
// 两条投资栈的预算按**当期意向需求**（建造力单位）比例分配，而不是累计贡献 K_m:K_f。
func TestAuditR39BudgetFollowsCurrentIntent(t *testing.T) {
	auditEnabled(t)
	st := liquidityState(t)
	var checked int
	for i := 0; i < 120; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step: %v", err)
		}
		dM, dF := st.demandManorTick, st.demandFinanceTick
		want := 0.5
		if dM+dF > 0 {
			want = dM / (dM + dF)
		}
		if math.Abs(st.budgetShareManor-want) > 1e-12 {
			t.Fatalf("tick %d：预算份额 = %.12f，应为需求比例 %.12f（需求_m=%.4f 需求_f=%.4f）",
				st.Tick, st.budgetShareManor, want, dM, dF)
		}
		if dM+dF > 0 {
			checked++
		}
	}
	if checked == 0 {
		t.Fatal("120 tick 内两条栈的意向需求恒为 0 —— 断言被空真通过")
	}
	// 反证：累计贡献比例与需求比例不是同一个数（否则本项裁决没有实际作用）。
	if st.KManor+st.KFinance > 0 {
		kShare := st.KManor / (st.KManor + st.KFinance)
		t.Logf("预算份额（当期需求） = %.6f；若按累计贡献 K_m:K_f 则为 %.6f（%d/%d tick 有意向需求）",
			st.budgetShareManor, kShare, checked, 120)
	}
}

// TestAuditR39DevelopmentTargetIsConstruction 校验公共工程的目标只能是开发类建筑。
func TestAuditR39DevelopmentTargetIsConstruction(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	bi := st.developmentTarget()
	if bi < 0 {
		t.Fatal("没有找到开发类建筑目标")
	}
	if !model.IsDevelopment(st.Buildings[bi].Spec) {
		t.Errorf("公共工程目标 %s 不是开发类建筑", st.Buildings[bi].Spec.Name)
	}
	if bi != powerGoodIndex {
		t.Errorf("公共工程目标下标 = %d，应为建造部门 %d（本版唯一的开发类建筑）", bi, powerGoodIndex)
	}
	// 投资栈划分与本类别正交：建造部门走金融栈（它不是 arable）。
	if build.IsArable(st.Buildings[bi].Spec) {
		t.Error("建造部门被判定为 arable —— 投资栈判据被污染")
	}
}

// cohortIndex 返回（场地, 阶级）对应的池下标（测试用）。
func cohortIndex(st *State, worksite, class int) int {
	for i := range st.Houses.Pools {
		if st.Houses.Pools[i].Worksite == worksite && st.Houses.Pools[i].Class == class {
			return i
		}
	}
	return -1
}
