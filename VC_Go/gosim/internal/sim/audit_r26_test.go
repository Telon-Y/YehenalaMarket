package sim

// audit_r26_test.go —— 第 11 轮（R26）新语义的专项审计。
//
// 覆盖契约 §4.5.1 / §4.5.1b / §4.5.5 / §4.5.7 / §8-9 / §8-13 的五条新规则：
//
//	① 两条投资栈的**建筑范围**：庄园栈只含农业建筑，金融栈不含农业建筑
//	② 庄园级数 = (自给农场 + 农业建筑) ÷ 掌控比（§4.5.5 改写）
//	③ 完工归属：新增等级**全部**计入私人持股（谁出资谁拥有），政府持股被稀释
//	④ 补贴只抬升雇佣口径（MarginEMA），**不得触发扩建**（§4.5.7 校验条款）
//	⑤ 投资池**不得透支**（预算是硬约束，§4.5.1b）
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR26 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/build"
	"yehenala/market/internal/model"
)

// TestAuditR26ManorStackIsArableOnly 校验两条投资栈的建筑范围（§4.5.1b）。
func TestAuditR26ManorStackIsArableOnly(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	// 构造"所有建筑都远超扩建阈值"的利润数组：本测试只检验**范围划分**，
	// 不涉及资金，故不需要任何场景注入。
	ema := make([]float64, len(st.Buildings))
	for i := range ema {
		ema[i] = 1.0
	}
	specs := st.buildingSpecs()
	levels := st.levels()
	manor, finance := build.Plan(specs, levels, ema, st.Params)

	if len(manor) == 0 || len(finance) == 0 {
		t.Fatalf("两条栈必须都非空才能检验划分：manor=%d finance=%d", len(manor), len(finance))
	}
	for _, it := range manor {
		if !build.IsArable(specs[it.BuildingIndex]) {
			t.Errorf("庄园栈出现非农业建筑 %s —— 违反 §4.5.5「庄园只能扩建农业建筑」",
				specs[it.BuildingIndex].Name)
		}
		if it.Stack != build.StackManor {
			t.Errorf("庄园栈意向的 Stack=%q，应为 %q", it.Stack, build.StackManor)
		}
	}
	for _, it := range finance {
		b := specs[it.BuildingIndex]
		if build.IsArable(b) {
			t.Errorf("金融栈出现农业建筑 %s —— 它应归庄园栈", b.Name)
		}
		if b.IsNonMarket() {
			t.Errorf("非市场建筑 %s 不应产生扩建意向（§4.5.2 / §4.5.5）", b.Name)
		}
	}
	t.Logf("庄园栈 %d 条（全部为农业建筑）、金融栈 %d 条", len(manor), len(finance))
}

// TestAuditR26ManorLevelIncludesArable 校验 §4.5.5 的新推导式：
//
//	N_manor = max(1, (N_subsistence + Σ_{农业建筑} N_i) / c_ctrl)
//
// 【为什么能反证旧口径】只要农业建筑等级 > 0，新口径的结果就与
// "只取自给农场"的旧口径不同——测试据此断言两者确实不同，
// 防止公式被改回去而测试仍然通过（空真）。
func TestAuditR26ManorLevelIncludesArable(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	if _, err := st.Step(); err != nil {
		t.Fatalf("Step 失败: %v", err)
	}

	var arable float64
	for i := range st.Buildings {
		if build.IsArable(st.Buildings[i].Spec) {
			arable += st.Buildings[i].Level
		}
	}
	subs := st.subsistenceFarmLevels()
	want := (subs + arable) / st.Params.ControlPerFinance
	if want < 1 {
		want = 1
	}
	got := st.Buildings[model.ManorIndex].Level
	if math.Abs(got-want) > 1e-9 {
		t.Errorf("庄园级数 = %.6f，按 (自给 %.2f + 农业 %.2f)/%.0f 应为 %.6f",
			got, subs, arable, st.Params.ControlPerFinance, want)
	}

	old := subs / st.Params.ControlPerFinance
	if old < 1 {
		old = 1
	}
	if arable > 1e-9 && math.Abs(old-want) < 1e-9 {
		t.Errorf("新口径 (%.6f) 与旧口径 (%.6f) 无法区分——本断言对回归无效", want, old)
	}
	t.Logf("自给农场 %.2f + 农业建筑 %.2f ⇒ 庄园 %.4f 级（旧口径 %.4f）", subs, arable, got, old)
}

// TestAuditR26NewLevelsBelongToCapital 校验 §4.5.1b / §8-13 的归属：
// 新增等级**全部**计入私人持股，政府持股既不增加、也不参与分新增等级，
// 因此完工后政府持股比例被**稀释**。
func TestAuditR26NewLevelsBelongToCapital(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	sc := newScenario(t, "给投资池注资，隔离'资本建筑净额为负 ⇒ 投资池恒为 0 ⇒ 队列恒空'这一资金约束")
	sc.InvestmentAdd(5e9, "本测试要验的是**完工归属**，若没有这笔显式隔离资金，"+
		"队列恒空、完工永不发生，断言会被空真通过")
	sc.Apply(st)
	defer sc.Restore(st)

	n := len(st.Buildings)
	govBefore := make([]float64, n)
	privBefore := make([]float64, n)
	lvlBefore := make([]float64, n)
	for i := range st.Buildings {
		govBefore[i] = st.Buildings[i].GovLevel
		privBefore[i] = st.Buildings[i].PrivLevel
		lvlBefore[i] = st.Buildings[i].Level
	}
	if _, err := st.Run(600); err != nil {
		t.Fatalf("Run 失败: %v", err)
	}

	grew := 0
	for i := range st.Buildings {
		b := &st.Buildings[i]
		// 【前值 → 后值（2026-09-19 第 15 轮）】旧断言是"政府持股只减不增"。
		// §4.5.8 的公共工程由国库出资、等级归政府，故开发类建筑（建造部门）的
		// 政府级数**会**增加（实测 6 → 17）。判据细化为：
		//  ① 只有开发类建筑的政府级数可以增加；
		//  ② 新增等级必须被完整归属：ΔLevel = ΔPriv + ΔGov。
		if b.GovLevel > govBefore[i]+1e-6 && !model.IsDevelopment(b.Spec) {
			t.Errorf("%s 的政府持股从 %.6f 增到 %.6f —— 私人扩建的新增等级不得归政府（谁出资谁拥有）",
				b.Spec.Name, govBefore[i], b.GovLevel)
		}
		d := b.Level - lvlBefore[i]
		if d <= 1e-6 {
			continue
		}
		dp := b.PrivLevel - privBefore[i]
		dg := b.GovLevel - govBefore[i]
		if math.Abs(dp+dg-d) > 1e-6 {
			t.Errorf("%s 新增 %.6f 级，但 私人 %.6f + 政府 %.6f = %.6f —— 归属不完整",
				b.Spec.Name, d, dp, dg, dp+dg)
		}
		if govBefore[i] > 1e-9 && lvlBefore[i] > 1e-9 {
			shareBefore := govBefore[i] / lvlBefore[i]
			shareAfter := b.GovLevel / b.Level
			if shareAfter >= shareBefore-1e-12 && !model.IsDevelopment(b.Spec) {
				t.Errorf("%s 政府持股比例未被稀释：%.6f → %.6f", b.Spec.Name, shareBefore, shareAfter)
			}
		}
		grew++
	}
	if grew == 0 {
		t.Fatal("600 tick 内没有任何建筑完工 —— 归属断言被空真通过（检查投资池注入是否生效）")
	}
	t.Logf("600 tick 内共 %d 类建筑完工；私人扩建全部归资本建筑，开发类建筑的公共工程部分归政府", grew)
}

// TestAuditR26SubsidyLiftsHireNotExpansion 校验 §4.5.7 的校验条款：
// 补贴只抬升**雇佣口径**（MarginEMA），扩建判定必须用**不含补贴**的 ProfitEMA。
//
// 【前值 → 后值（2026-09-19 第 15 轮）】旧实现以**建造部门**为目标，理由是
// "它在本经济中恒为 −100% 亏损，是天然的受补贴对象"。第 15 轮把利润率口径
// 订正为 §3.1 的**名义每级口径**（价格-成本）后，该部门不再恒亏（t=0 时全体
// 部门恰为 +20%），旧的"恒亏目标"消失。现改为**动态选择**：跑一段基线，
// 取窗口内出现过负 ProfitEMA 的部门作为目标；若一个都没有，则跳过并报明原因。
func TestAuditR26SubsidyLiftsHireNotExpansion(t *testing.T) {
	auditEnabled(t)

	// ① 先用基线（补贴关闭）找出一个会转亏的部门。
	probe := newTestState(t)
	neg := -1
	for i := 0; i < 300 && neg < 0; i++ {
		if _, err := probe.Step(); err != nil {
			t.Fatalf("基线 Step 失败: %v", err)
		}
		for j := range probe.Buildings {
			if probe.Buildings[j].Spec.IsNonMarket() {
				continue
			}
			if probe.Buildings[j].ProfitEMA < 0 {
				neg = j
				break
			}
		}
	}
	if neg < 0 {
		t.Skip("300 tick 基线内没有任何部门出现负 ProfitEMA —— 补贴的触发条件无从检验")
	}
	target := neg

	st := newTestState(t)
	sc := newScenario(t, "开启补贴总开关，并只对目标部门开放补贴")
	sc.Param("SubsidyEnabled",
		func(s *State) float64 { return b2f(s.Params.SubsidyEnabled) },
		func(s *State, v float64) { s.Params.SubsidyEnabled = v > 0 },
		1, "补贴总开关默认关闭；本测试要验证它开启后的行为")
	restore := sc.SubsidyOnly(target,
		"把补贴观察面收窄到单一部门：这样'补贴入账'与'是否扩建'能一一对应，"+
			"否则其他建筑的扩建会污染观察")
	sc.Apply(st)
	defer func() { restore(); sc.Restore(st) }()
	t.Logf("受观察部门 = %s（基线 300 tick 内出现过负 ProfitEMA）", st.Buildings[target].Spec.Name)

	var paid float64
	// 【前值 → 后值（2026-09-19 第 15 轮）】旧实现用 `before := len(st.Orders)` 后
	// 取 `st.Orders[before:]` 判定"本 tick 是否新立项"。当订单在同期**完工出队**
	// （completeOrders 会从队列里删除已完成的订单）时，该切片会越界 panic
	// （实测 `slice bounds out of range [5:3]`）；公共工程（§4.5.8）也会往队列里
	// 追加政府订单，进一步使"按下标切分"失去意义。
	// 现改为**计数**口径：只统计指向 target 的**非公共工程**订单条数是否增加。
	// 【2026-09-19 第 27 轮追加】还必须排除**收购订单**（`o.Acquire`）：
	// 第 27 轮把"私有化"改成建造列表里的一类订单（用户裁决"将收购概念同样加入建造列表"），
	// 它与扩建订单共用 `s.Orders`。而本测试观察的是「补贴**不得触发扩建**」
	// ——收购不是扩建（它买的是存量股权、不消耗建造力），故必须排除，
	// 否则会把"补贴把 marginEMA 抬过私有化阈值 ⇒ 出现收购单"误判为"补贴触发了扩建"。
	// 实测该误判在 tick 44 发生（棉花种植园：ProfitEMA=0.0836 ≤ 0.10，但订单数 0 → 1）。
	countTarget := func() int {
		n := 0
		for _, o := range st.Orders {
			if o.BuildingIndex == target && !o.PublicWorks && !o.Acquire {
				n++
			}
		}
		return n
	}
	// 600 tick：负 margin 在默认布点下约 tick 260 才出现，窗口必须覆盖它。
	for i := 0; i < 600; i++ {
		before := countTarget()
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step 失败: %v", err)
		}
		paid += st.subsidyPaidTick
		b := &st.Buildings[target]
		if b.ProfitEMA > st.Params.ExpandThreshold {
			continue // 该建筑此刻本就该扩建，不在本条款的观察范围内
		}
		if after := countTarget(); after > before {
			t.Fatalf("tick %d：ProfitEMA=%.4f ≤ 阈值 %.2f（补贴并未使它盈利），"+
				"但该建筑仍被立项扩建（订单数 %d → %d）—— 违反 §4.5.7「补贴不得触发扩建」",
				st.Tick, b.ProfitEMA, st.Params.ExpandThreshold, before, after)
		}
	}
	if paid <= 0 {
		t.Fatal("600 tick 内没有发生任何补贴 —— 本条断言被空真通过")
	}
	t.Logf("600 tick 补贴合计 %.0f 元；受补贴建筑 MarginEMA=%.4f / ProfitEMA=%.4f（补贴只抬前者）",
		paid, st.Buildings[target].MarginEMA, st.Buildings[target].ProfitEMA)
}

// TestAuditR26InvestmentPoolNeverOverdrafts 校验 §4.5.1b：
// 投资池的预算是硬约束——两条栈合计支出不得超过可动用额，余额不得为负。
func TestAuditR26InvestmentPoolNeverOverdrafts(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	sc := newScenario(t, "给投资池注资：让两条栈真正花钱，才能检验'不透支'")
	sc.InvestmentAdd(5e9, "默认参数下投资池恒为 0 ⇒ 支出恒为 0 ⇒ 透支约束被空真通过")
	sc.Apply(st)
	defer sc.Restore(st)

	var spent float64
	snaps, err := st.Run(300)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	for _, s := range snaps {
		if s.InvestmentPool < -1e-6 {
			t.Fatalf("tick %d：投资池余额 %.6f 为负 —— 预算未被当作硬约束（§4.5.1b）",
				s.Tick, s.InvestmentPool)
		}
		spent += s.InvestmentPaid
	}
	if spent <= 0 {
		t.Fatal("300 tick 内投资池未支付任何建造力货款 —— 透支约束被空真通过")
	}
	t.Logf("300 tick 投资池累计付给政府 %.0f 元，期末余额 %.2f（从未为负）", spent, st.InvestmentPool())
}
