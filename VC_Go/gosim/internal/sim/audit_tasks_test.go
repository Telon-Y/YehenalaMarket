package sim

// audit_tasks_test.go —— 三项任务的验收测试
//
//	任务1  私有化机制（总开关 + 逐建筑开关）
//	任务2  §5 亏损解雇与 §4.4 缩编是否真的在工作
//	任务3  每种生产建筑起始 10 级
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditTask -v

import (
	"fmt"
	"testing"
)

// TestAuditTask3InitialLevels 验收任务3：每种生产建筑统一取契约默认起始等级
// （契约修订后为 Params.ProductionInitLevel = 5，不再是 10）。
func TestAuditTask3InitialLevels(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	fmt.Printf("\n%-12s %10s %10s %10s\n", "建筑", "起始等级", "契约目标", "可私有化")
	want := st.Params.ProductionInitLevel
	for i := range st.Buildings {
		b := &st.Buildings[i]
		target := want
		if !b.Spec.Produces() {
			// 金融区/宅邸庄园按推导布点；**仓库**由 InitialWarehouseLevel 给定；
			// **消费代理**是虚构记账主体，恒为 0 级（§4.5.6）。
			target = -1
		}
		if b.Spec.Recipe.Output == powerGoodIndex {
			target = st.Params.InitialPowerLevel // 建造部门由吞吐量决定
		}
		if b.Spec.IsWarehouse {
			target = st.Params.InitialWarehouseLevel
		}
		fmt.Printf("%-12s %10.2f %10.2f %10v\n",
			b.Spec.Name, b.Level, target, b.Spec.AllowPrivatize)

		if !b.Spec.Produces() {
			// 【§4.5.6】非商品生产者不适用"统一起始等级"：
			//	金融区/庄园 —— 级数由掌控比推导；
			//	仓库       —— InitialWarehouseLevel（建模值）；
			//	消费代理   —— 恒为 0 级。
			switch {
			case b.Spec.IsWarehouse:
				if b.Level != st.Params.InitialWarehouseLevel {
					t.Errorf("%s 起始等级 %.2f，应为 InitialWarehouseLevel=%.2f",
						b.Spec.Name, b.Level, st.Params.InitialWarehouseLevel)
				}
			case b.Spec.IsAgent:
				if b.Level != 0 {
					t.Errorf("%s（消费代理）起始等级 %.2f，应恒为 0", b.Spec.Name, b.Level)
				}
			}
			continue
		}
		if b.Spec.Recipe.Output == powerGoodIndex {
			if b.Level != st.Params.InitialPowerLevel {
				t.Errorf("%s 起始等级 %.2f，应为 InitialPowerLevel=%.2f",
					b.Spec.Name, b.Level, st.Params.InitialPowerLevel)
			}
			continue
		}
		if b.Level != want {
			t.Errorf("%s 起始等级 %.2f，应为 Params.ProductionInitLevel=%.2f"+
				"（契约修订：每种生产建筑统一起始等级）",
				b.Spec.Name, b.Level, want)
		}
	}
}

// TestAuditTask1PrivatizeSwitch 验收任务1：私有化的总开关与逐建筑开关。
//
// 【前提说明】默认经济里私有部门整体亏损（资本池每 tick 净减数十万），
// 故"盈利建筑"极少，私有化在默认参数下几乎不发生。本测试用【受控场景】
// 验证开关的语义——全部注入通过 Scenario 显式声明并打印，
// 输出里能看到"哪些数据被改过、从多少改成多少、为什么"。
func TestAuditTask1PrivatizeSwitch(t *testing.T) {
	auditEnabled(t)

	// ---- 开关关闭：所有权完全不变 ----
	off := newTestState(t)
	scOff := newScenario(t, "总开关关闭，验证所有权不变")
	scOff.Param("PrivatizeEnabled",
		func(st *State) float64 { return b2f(st.Params.PrivatizeEnabled) },
		func(st *State, v float64) { st.Params.PrivatizeEnabled = v != 0 },
		0, "对照组的自变量：确认关闭时机制完全不运行")
	scOff.CapitalAdd(1e9, "排除'资本不足'这一干扰项，单独检验开关语义")
	scOff.Apply(off)

	govBefore := govShareWeighted(off)
	// 【前值 → 后值（2026-09-19 第 15 轮）】旧断言用"政府持股比例不变"来证明
	// "关闭时机制不运行"。§5.3 储蓄渠道落地后，投资池首次有了稳定资金来源，
	// 私人扩建真实发生；而 §4.5.1b 定案"谁出资、谁拥有"⇒ 新增等级全归资本建筑、
	// 政府持股**被稀释**（实测 0.300 → 0.256）。故正确的判据是
	// "没有任何**股权转让**"：逐建筑核对 GovLevel 不减少。
	govLvBefore := make([]float64, len(off.Buildings))
	for i := range off.Buildings {
		govLvBefore[i] = off.Buildings[i].GovLevel
	}
	if _, err := off.Run(50); err != nil {
		t.Fatalf("Run: %v", err)
	}
	govAfterOff := govShareWeighted(off)
	fmt.Printf("\n[总开关关闭] 政府持股 %.9f → %.9f（差 %.3e；差额只允许来自扩建稀释）\n",
		govBefore, govAfterOff, govAfterOff-govBefore)
	for i := range off.Buildings {
		if off.Buildings[i].GovLevel < govLvBefore[i]-1e-9 {
			t.Errorf("%s 的政府持股级数在总开关关闭时减少（%.4f → %.4f）——发生了私有化转让",
				off.Buildings[i].Spec.Name, govLvBefore[i], off.Buildings[i].GovLevel)
		}
	}
	if off.privatizeUnits != 0 {
		t.Errorf("总开关关闭时本 tick 仍转让了 %.4f 级股权", off.privatizeUnits)
	}

	// ---- 开关开启：盈利且允许私有化的建筑被转让 ----
	on := newTestState(t)
	scOn := newScenario(t, "总开关开启，验证机制真的会转让股权")
	scOn.Param("PrivatizeEnabled",
		func(st *State) float64 { return b2f(st.Params.PrivatizeEnabled) },
		func(st *State, v float64) { st.Params.PrivatizeEnabled = v != 0 },
		1, "实验组的自变量")
	scOn.Param("PrivatizeMargin",
		func(st *State) float64 { return st.Params.PrivatizeMargin },
		func(st *State, v float64) { st.Params.PrivatizeMargin = v },
		-1, "受控：默认经济几乎全部亏损，若不放松阈值则机制无从触发，"+
			"本测试要验的是开关语义而不是盈利判据")
	scOn.CapitalAdd(1e9, "排除'资本不足'这一干扰项")
	scOn.Apply(on)

	govBeforeOn := govShareWeighted(on)
	govLvBeforeOn := make([]float64, len(on.Buildings))
	for i := range on.Buildings {
		govLvBeforeOn[i] = on.Buildings[i].GovLevel
	}
	if _, err := on.Run(50); err != nil {
		t.Fatalf("Run: %v", err)
	}
	govAfterOn := govShareWeighted(on)
	fmt.Printf("[总开关开启] 政府持股 %.6f → %.6f\n", govBeforeOn, govAfterOn)
	if govAfterOn >= govBeforeOn {
		t.Errorf("总开关开启后政府持股未下降（%.8f → %.8f）——私有化未发生",
			govBeforeOn, govAfterOn)
	}

	// ---- 逐建筑开关：AllowPrivatize=false 的必须保持原持股 ----
	fmt.Printf("\n%-12s %10s %10s %8s\n", "建筑", "末政府级", "持股比例", "可私有")
	for i := range on.Buildings {
		b := &on.Buildings[i]
		share := 0.0
		if b.Level > 1e-9 {
			share = b.GovLevel / b.Level
		}
		fmt.Printf("%-12s %10.2f %10.4f %8v\n",
			b.Spec.Name, b.GovLevel, share, b.Spec.AllowPrivatize)
		// 金融区天然 100% 私有（初始 share = 0），不参与判定
		//
		// 【前值 → 后值 + 理由】旧断言把"初始政府持股"硬编码为 0.70；
		// §4.5.1（2026-09-19 第 11 轮）已把 $s_{gov}$ 下调到 0.30，
		// 故改读 Params.GovInitialShare——否则该断言会在参数变更后误报。
		//
		// 【前值 → 后值（2026-09-19 第 15 轮）】判据由"持股比例 ≥ 初始值"
		// 改为"**级数**不减少"：§4.5.1b 的"谁出资、谁拥有"使扩建稀释政府比例
		// （谷物农场 0.30 → 0.0670、棉花 0.2082 实测），但那是稀释、不是转让。
		if !b.Spec.AllowPrivatize && !b.Spec.IsNonMarket() && b.Level > 1e-9 {
			if b.GovLevel < govLvBeforeOn[i]-1e-9 {
				t.Errorf("%s 标注为不可私有化，但政府持股级数减少（%.4f → %.4f）——发生了转让",
					b.Spec.Name, govLvBeforeOn[i], b.GovLevel)
			}
		}
	}

	// ---- 货币守恒：私有化只是所有权转移，不得改变总量 ----
	// 注入过资本，故守恒基线取注入后的总量。
	if v := on.Aud.Violations(); len(v) > 0 {
		t.Errorf("私有化存在借贷不相等记录: %v", v)
	}
}

// b2f 把 bool 转成 float64，供 Scenario.Param 统一读写用。
func b2f(b bool) float64 {
	if b {
		return 1
	}
	return 0
}

// TestAuditTask2HireAndDecay 验收任务2：§5 亏损解雇与 §4.4 缩编。
//
// 【2026-09-19 裁决后的判定口径】§4.4 已**冻结**，故本测试判四件事：
//  1. 亏损建筑的雇佣率逐 tick 下降（解雇仍在动）
//  2. 雇佣率能跌破 75%（缩编门槛照常统计）
//  3. 默认参数下**等级不下降**（冻结生效，等级只增不减）
//  4. 显式注入 DecayFrozen = false 后，缩编重新生效（代码路径未被删除）
func TestAuditTask2HireAndDecay(t *testing.T) {
	auditEnabled(t)

	report := func(tag string, st *State, initHire, initLevel []float64) (fired, below75, shrunk, grew int) {
		fmt.Printf("\n[%s] %-12s %8s %8s %8s %8s %10s\n",
			tag, "建筑", "初始等级", "末等级", "初始雇佣", "末雇佣", "IdleTicks")
		for i := range st.Buildings {
			b := &st.Buildings[i]
			fmt.Printf("%-12s %8.2f %8.2f %8.4f %8.4f %10d\n",
				b.Spec.Name, initLevel[i], b.Level, initHire[i], b.HireRate, b.IdleTicks)
			if b.HireRate < initHire[i]-1e-9 {
				fired++
			}
			if b.HireRate < 0.75 {
				below75++
			}
			// 金融区与宅邸庄园的级数由推导得出（每 tick 重算），不属于"缩编"。
			if b.Spec.IsNonMarket() {
				continue
			}
			if initLevel[i] > 1e-9 && b.Level < initLevel[i]-1e-9 {
				shrunk++
			}
			if b.Level > initLevel[i]+1e-9 {
				grew++
			}
		}
		return
	}

	// ---- 默认参数：缩编冻结 ----
	st := newTestState(t)
	if !st.Params.DecayFrozen {
		t.Fatal("默认参数应为 DecayFrozen = true（§4.4 冻结）")
	}
	initHire := make([]float64, len(st.Buildings))
	initLevel := make([]float64, len(st.Buildings))
	for i := range st.Buildings {
		initHire[i] = st.Buildings[i].HireRate
		initLevel[i] = st.Buildings[i].Level
	}
	if _, err := st.Run(1200); err != nil {
		t.Fatalf("Run: %v", err)
	}
	fired, below75, shrunk, grew := report("冻结（默认）", st, initHire, initLevel)
	fmt.Printf("\n解雇（雇佣率下降）= %d/%d   跌破75%% = %d/%d   缩编（等级下降）= %d/%d\n",
		fired, len(st.Buildings), below75, len(st.Buildings), shrunk, len(st.Buildings))

	if fired == 0 {
		t.Error("§5 的亏损解雇机制没有生效：没有任何建筑的雇佣率下降")
	}
	if below75 == 0 {
		t.Error("没有任何建筑跌破 75%，缩编门槛统计失效")
	}
	if shrunk != 0 {
		t.Errorf("§4.4 已冻结，但仍有 %d 个建筑等级下降", shrunk)
	}

	// ---- 显式解冻：缩编必须重新生效（证明代码路径仍在）----
	st2 := newTestState(t)
	sc := newScenario(t, "解冻 §4.4，验证缩编代码路径仍可用")
	sc.Param("DecayFrozen",
		func(s *State) float64 { return b2f(s.Params.DecayFrozen) },
		func(s *State, v float64) { s.Params.DecayFrozen = v != 0 },
		0, "对照组：恢复 §4.4 的原始行为")
	sc.Apply(st2)
	defer sc.Restore(st2)

	initHire2 := make([]float64, len(st2.Buildings))
	initLevel2 := make([]float64, len(st2.Buildings))
	for i := range st2.Buildings {
		initHire2[i] = st2.Buildings[i].HireRate
		initLevel2[i] = st2.Buildings[i].Level
	}
	if _, err := st2.Run(1200); err != nil {
		t.Fatalf("Run(解冻): %v", err)
	}
	_, _, shrunk2, _ := report("解冻（对照）", st2, initHire2, initLevel2)
	fmt.Printf("\n[解冻对照] 缩编（等级下降）= %d/%d\n", shrunk2, len(st2.Buildings))
	if shrunk2 == 0 {
		t.Error("解冻后缩编仍未生效：§4.4 的代码路径已损坏")
	}
	_ = grew
}

// TestAuditTask1PriceIsBuildCost 校验私有化对价公式：
//
//	每级对价 = 建造成本（建造力）× 建造力当期价格 × 估值倍数
//
// 即契约定案的"私有化成本就是建造成本"。
//
// 【前提】默认经济里资本池为负（私有部门整体亏损），私有化根本不会发生，
// 故本测试注入充足资本，单独检验对价公式。
func TestAuditTask1PriceIsBuildCost(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	sc := newScenario(t, "验证私有化对价 = 建造成本 × 建造力价格")
	sc.Param("PrivatizeEnabled",
		func(s *State) float64 { return b2f(s.Params.PrivatizeEnabled) },
		func(s *State, v float64) { s.Params.PrivatizeEnabled = v != 0 },
		1, "开启机制才能观察对价")
	sc.Param("PrivatizeMargin",
		func(s *State) float64 { return s.Params.PrivatizeMargin },
		func(s *State, v float64) { s.Params.PrivatizeMargin = v },
		-1, "受控：默认经济几乎全亏，不放松阈值则无从触发")
	sc.Param("PrivatizeStep",
		func(s *State) float64 { return s.Params.PrivatizeStep },
		func(s *State, v float64) { s.Params.PrivatizeStep = v },
		1.0, "受控：一次转让全部政府持股，便于用单笔金额反解每级对价")

	// 选一个可私有化、且政府持股最多的建筑作为唯一观察对象
	target := -1
	var maxGov float64
	for i := range st.Buildings {
		b := &st.Buildings[i]
		if b.Spec.AllowPrivatize && !b.Spec.IsFinance && b.GovLevel > maxGov {
			target, maxGov = i, b.GovLevel
		}
	}
	if target < 0 {
		t.Fatal("找不到可私有化的建筑")
	}
	sc.OnlyPrivatizable(target,
		"把观察面收窄到单一建筑：privatizePaid 是全建筑合计，"+
			"只有单一建筑时才能用 实付/级数 反解出每级对价")
	sc.CapitalAdd(1e15, "排除'付不起'这一干扰项，单独检验对价公式")
	// 【2026-09-19 第 28 轮追加】关闭"收购用投资池余额出资"，使本 tick 的成交
	// **只由资本池出款**——否则投资池（本轮实测 3.0e9）会与资本池同时出钱，
	// `privatizePaid / privatizeUnits` 变成两个付款方的混合，对价公式无法反解。
	sc.Param("Params.AcquireFromInvestment",
		func(s *State) float64 {
			if s.Params.AcquireFromInvestment {
				return 1
			}
			return 0
		},
		func(s *State, v float64) { s.Params.AcquireFromInvestment = v != 0 },
		0, "受控：本断言要反解'每级对价 = 建造成本 × 建造力价'，必须让成交只由资本池出款")
	sc.Apply(st)
	defer sc.Restore(st)

	b := &st.Buildings[target]
	powerPrice := st.Market.Prices()[powerGoodIndex]
	wantUnit := b.Spec.BuildCost * powerPrice * st.Params.PrivatizePriceMult
	govBefore := b.GovLevel
	// 拟转让量 = 政府持股 × 步长（与 step.go 的口径一致）。
	// 实际转让量可能略小（受资本池约束），故断言用【实付反解】而不是这个值。
	wantUnits := govBefore * st.Params.PrivatizeStep

	fmt.Printf("\n观察对象 = %s（建造成本 %.0f 建造力/级）\n建造力价格 = %.2f\n每级对价 = %.2f\n拟转让 %.4f 级\n",
		b.Spec.Name, b.Spec.BuildCost, powerPrice, wantUnit, wantUnits)

	// 【口径】对价用的是【本 tick 结算后】的建造力价格（step 的 ⑨ 步才设），
	// 故不能拿 Step 之前读到的价格去断言。这里直接读实现记录的
	// privatizePriceTick（本 tick 私有化所用的建造力价格），再核对
	// 实付 = 级数 × 建造成本 × 该价格 × (1+t)。
	if _, err := st.Step(); err != nil {
		t.Fatalf("Step: %v", err)
	}
	settlePrice := st.privatizePriceTick
	if settlePrice <= 0 {
		t.Fatalf("实现未记录本 tick 的建造力价格（privatizePriceTick = 0）")
	}
	wantUnit = b.Spec.BuildCost * settlePrice * st.Params.PrivatizePriceMult
	fmt.Printf("本 tick 私有化所用的建造力价格 = %.4f\n每级对价 = %.4f\n",
		settlePrice, wantUnit)

	fmt.Printf("实际转让 %.4f 级，实付 %.2f\n", st.privatizeUnits, st.privatizePaid)
	if st.privatizePaid <= 0 || st.privatizeUnits <= 0 {
		t.Fatalf("未发生私有化（转让 %.4f 级，实付 %.2f）", st.privatizeUnits, st.privatizePaid)
	}
	// 对价必须恰好等于 转让级数 × 每级对价 × (1+t)。
	// 用实付与实物级数【双向核对】，任一侧偏差都暴露实现错误。
	wantPaid := wantUnits * wantUnit * (1 + st.Params.TaxRate)
	if diff := st.privatizePaid - wantPaid; diff > 1e-6 || diff < -1e-6 {
		t.Errorf("实付 %.4f，应等于 %.4f × %.4f × (1+t) = %.4f",
			st.privatizePaid, wantUnits, wantUnit, wantPaid)
	}
	if diff := st.privatizeUnits - wantUnits; diff > 1e-6 || diff < -1e-6 {
		t.Errorf("转让 %.4f 级，应等于 政府持股 %.4f × 步长 %g = %.4f",
			st.privatizeUnits, govBefore, st.Params.PrivatizeStep, wantUnits)
	}
	// 股权腿与现金腿必须成对：政府级的减少量 = 转让级数
	if got := govBefore - b.GovLevel; got < st.privatizeUnits-1e-6 {
		t.Errorf("股权腿 %.4f 与现金腿 %.4f 不匹配", got, st.privatizeUnits)
	}
	if v := st.Aud.Violations(); len(v) > 0 {
		t.Errorf("私有化存在借贷不相等记录: %v", v)
	}
}
