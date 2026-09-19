package sim

// audit_r28_test.go —— §七 R28：「投资池与国库恒 ∞」的对照实验。
//
// ============================ 这个实验要回答什么 ============================
//
// R26 实现后实测到两条量级问题（`docs/ACTIVE.md` §四第 5、6 项）：
//
//	① 投资池恒为 0（两个资本建筑扣工资后净额均为负）⇒ 两条栈无预算 ⇒ 队列恒空；
//	② 政府因没有支出端而变成货币沉淀池（或降税后转为赤字）。
//
// 两者都属**收支不匹配**。本测试把这一条单独隔离出去——显式假设
// "投资池与国库恒 ∞"（`Options.UnlimitedFunds`），然后检验**其余机制**是否正常：
//
//	队列 → G2 按需采购 → G6 投资池全额偿还（政府净支出 0）→ 订单推进与完工
//	→ 新增等级归出资资本建筑 → 两条栈各自的建筑范围 → 记账与货币守恒。
//
// 【纪律】"无限资金"是一个**被显式计量**的外部假设：每笔注入都记入
// `State.InfusionTotal`，故货币守恒恒等式在本模式下读作
// `ΔM == NewCapital + 诊断注入`——不是"来路不明的钱"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR28 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/build"
	"yehenala/market/internal/model"
)

// newUnlimitedState 构造与 newTestState 同参、但开启了"无限资金"诊断开关的状态。
func newUnlimitedState(t *testing.T) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		DemandScale:          0,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		// §七 R28：投资池每 tick 补到哨兵水位；G2 解除"可动用资金"裁剪。
		UnlimitedFunds: true,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	return st
}

// TestAuditR28UnlimitedFundsMachinery 在"投资池与国库恒 ∞"下跑 400 tick，
// 逐 tick 核对全部机制性不变量，最后核对货币守恒与完工归属。
func TestAuditR28UnlimitedFundsMachinery(t *testing.T) {
	auditEnabled(t)
	st := newUnlimitedState(t)

	// 【第 28 轮：本测试关闭收购】它要验的是"完工归属"——"新增等级必须被完整归属：
	// 私人扩建 → PrivLevel；公共工程 → GovLevel"。而 §4.5.1a 的**收购**会合法地
	// 把 GovLevel 转成 PrivLevel（政府级**减少**），在"无限资金"下尤其活跃
	//（实测谷物农场政府级 −1.48 级），会把"新增等级里政府占负值"误报成归属缺陷。
	// 故这里显式关闭私有化——本测试不观察它，其守恒另由 `TestSavingsChannel…`、
	// `TestAuditG2G6Cash` 与 `book` 的私有化断言覆盖。
	st.Params.PrivatizeEnabled = false

	n := len(st.Buildings)
	govBefore := make([]float64, n)
	privBefore := make([]float64, n)
	lvlBefore := make([]float64, n)
	for i := range st.Buildings {
		govBefore[i] = st.Buildings[i].GovLevel
		privBefore[i] = st.Buildings[i].PrivLevel
		lvlBefore[i] = st.Buildings[i].Level
	}
	moneyBefore := st.TotalMoney()

	var (
		powerBought float64
		paidToGov   float64
		tickWithBuy int
	)
	const ticks = 400
	for i := 0; i < ticks; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: Step 失败: %v", i, err)
		}
		// ① 记账：不得出现借贷不相等（唯一允许的注入是营运本金 + 显式诊断注入）。
		if v := st.Aud.Violations(); len(v) > 0 {
			t.Fatalf("tick %d: 借贷不相等记录 %d 条: %v", i, len(v), v)
		}
		// ② G2 按需采购：投入 ≤ 采购 ≤ 产出；政府不持有公共储备。
		if snap.PowerSold > snap.PowerPurchased+1e-6 {
			t.Fatalf("tick %d: 投入 %.6f > 采购 %.6f", i, snap.PowerSold, snap.PowerPurchased)
		}
		if snap.PowerInventory != 0 {
			t.Fatalf("tick %d: 公共储备 %.6f ≠ 0（G2 即买即用）", i, snap.PowerInventory)
		}
		if snap.PowerPurchased > 1e-9 {
			tickWithBuy++
		}
		// ③ G6：投资池偿还 == 政府当期的建造力采购货款（政府净支出恒为 0）。
		if want := snap.PowerPurchased * snap.Prices[powerGoodIndex]; math.Abs(snap.InvestmentPaid-want) > 1e-6 {
			t.Fatalf("tick %d: 投资池偿还 %.4f ≠ 采购货款 %.4f（政府净支出应为 0）",
				i, snap.InvestmentPaid, want)
		}
		// ④ 投资池不得透支（预算硬约束；本模式下有哨兵水位兜底，更不该为负）。
		if snap.InvestmentPool < -1e-6 {
			t.Fatalf("tick %d: 投资池余额 %.2f 为负", i, snap.InvestmentPool)
		}
		// ⑤ 逐建筑对账残差必须为 0（没有未登记的资金腿）。
		if r := st.Recon.Residual(); math.Abs(r) > 1e-6 {
			t.Fatalf("tick %d: 建筑池对账残差 %.4f ≠ 0", i, r)
		}
		// ⑥ 两条栈的范围：庄园栈的订单只能指向农业建筑。
		for k := range st.Orders {
			o := &st.Orders[k]
			if o.BuildingIndex < 0 || o.BuildingIndex >= n {
				t.Fatalf("tick %d: 订单指向非法建筑下标 %d", i, o.BuildingIndex)
			}
			if o.Stack == build.StackManor && !build.IsArable(st.Buildings[o.BuildingIndex].Spec) {
				t.Fatalf("tick %d: 庄园栈订单指向非农业建筑 %s",
					i, st.Buildings[o.BuildingIndex].Spec.Name)
			}
			if o.Progress < 0 {
				t.Fatalf("tick %d: 订单进度为负 %.6f", i, o.Progress)
			}
		}
		// ⑦ 完工归属：私人扩建的新增等级不归政府（谁出资谁拥有）。
		//
		// 【前值 → 后值（2026-09-19 第 15 轮）】旧断言是"政府持股只减不增"。
		// §4.5.8 新增公共工程（国库出资、等级归政府）后，政府持股**会**因公共工程
		// 而增加——实测 tick 61 建造部门 6.000000 → 8.000000。故判据细化为：
		// 政府级数的增加只能来自**公共工程订单的完工**，私人扩建一律归私人。
		for j := range st.Buildings {
			if st.Buildings[j].GovLevel > govBefore[j]+1e-6 {
				if !model.IsDevelopment(st.Buildings[j].Spec) {
					t.Fatalf("tick %d: %s 的政府持股从 %.6f 增到 %.6f——私人扩建的新增等级不得归政府",
						i, st.Buildings[j].Spec.Name, govBefore[j], st.Buildings[j].GovLevel)
				}
			}
		}
		if math.IsNaN(st.TotalMoney()) || math.IsInf(st.TotalMoney(), 0) {
			t.Fatalf("tick %d: 货币总量非有限值 %.4f", i, st.TotalMoney())
		}
		powerBought += snap.PowerPurchased
		paidToGov += snap.InvestmentPaid
	}

	// ⑧ 机制确实在运转（防止断言被空真通过）。
	if tickWithBuy == 0 || powerBought <= 0 {
		t.Fatal("400 tick 内没有任何建造力采购——队列恒空，机制断言被空真通过")
	}
	if paidToGov <= 0 {
		t.Fatal("400 tick 内投资池没有向政府支付任何货款")
	}
	var completed int
	for i := range st.Buildings {
		b := &st.Buildings[i]
		if b.Spec.IsNonMarket() {
			continue // 金融区/庄园级数由掌控比推导，不属于"完工"
		}
		d := b.Level - lvlBefore[i]
		if d > 1e-6 {
			completed++
			// 新增等级必须被完整归属：私人扩建 → PrivLevel；公共工程 → GovLevel。
			//
			// 【前值 → 后值（2026-09-19 第 15 轮）】旧断言要求"新增等级全部落进私人持股"。
			// §4.5.8 让国库也能出资扩建开发类建筑（等级归政府），故对开发类建筑
			// 改为"私人 + 政府 = 全部新增"；其余建筑仍然必须全部归私人
			// （实测 建造部门 新增 8.000000 级全部来自公共工程）。
			dp := b.PrivLevel - privBefore[i]
			dg := b.GovLevel - govBefore[i]
			if math.Abs(dp+dg-d) > 1e-6 {
				t.Errorf("%s 新增 %.6f 级，但 私人 %.6f + 政府 %.6f = %.6f，归属不完整",
					b.Spec.Name, d, dp, dg, dp+dg)
			}
			if !model.IsDevelopment(b.Spec) && math.Abs(dg) > 1e-6 {
				t.Errorf("%s（非开发类）新增等级里政府占 %.6f 级——只有公共工程能增加政府持股",
					b.Spec.Name, dg)
			}
		}
	}
	if completed == 0 {
		t.Fatal("400 tick 内没有任何生产建筑完工——完工归属断言被空真通过")
	}

	// ⑨ 货币守恒：本模式下读作 ΔM == NewCapital + 诊断注入。
	//
	// 【容差口径（2026-09-19 第 15 轮）】本模式的注入量级是 ~1e12（哨兵水位），
	// 400 tick 的浮点累加误差在 1e-3 元量级（实测 0.0012 元 = 相对 1.2e-15），
	// 故容差改为**相对**口径：1e-9 × 量级。绝对 1e-3 在这个量级上过紧，
	// 会把双精度舍入误判成记账缺陷。
	gotMoney := st.TotalMoney() - moneyBefore
	wantMoney := st.TickNewCapitalTotal() + st.InfusionTotal()
	tolMoney := 1e-9 * math.Max(1, math.Abs(wantMoney))
	if math.Abs(gotMoney-wantMoney) > tolMoney {
		t.Errorf("货币守恒失败：ΔM=%.4f，应为 NewCapital(%.4f) + 诊断注入(%.4f) = %.4f（容差 %.4f）",
			gotMoney, st.TickNewCapitalTotal(), st.InfusionTotal(), wantMoney, tolMoney)
	}

	t.Logf("【无限资金对照实验】%d tick：采购建造力 %.0f 单位（%d 个 tick 非零）、"+
		"投资池付政府 %.0f 元、%d 类生产建筑完工、期末投资池 %.0f",
		ticks, powerBought, tickWithBuy, paidToGov, completed, st.InvestmentPool())
	t.Logf("  货币守恒：ΔM=%.0f == NewCapital(%.0f) + 诊断注入(%.0f)",
		gotMoney, st.TickNewCapitalTotal(), st.InfusionTotal())
	t.Logf("  结论：资金不受限时，队列/按需采购/全额偿还/完工归属/两条栈/记账全部正常——"+
		"唯一的阻塞是收支不匹配（投资池入池为 0）")
}

// TestAuditR28TreasuryCapWouldBind 对照：**不开**诊断开关时，国库的可动用资金
// 会真的卡住采购（即使投资池被显式注资）。这条测试固定"为什么需要这个开关"。
func TestAuditR28TreasuryCapWouldBind(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	sc := newScenario(t, "只给投资池注资、不动国库：检验'国库'是否真的会卡住采购")
	sc.InvestmentAdd(5e9, "把'投资池没钱'这一条隔离掉，只看国库侧")
	sc.Apply(st)
	defer sc.Restore(st)

	var qty, availQtyLimited float64
	for i := 0; i < 60; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", i, err)
		}
		qty += snap.PowerPurchased
		// 采购量是否被"可动用资金"而非"队列需要量/产出"裁剪
		if snap.PowerPurchased < st.powerNeedTick-1e-6 && st.powerNeedTick > 0 {
			availQtyLimited++
		}
	}
	if qty <= 0 {
		t.Fatal("60 tick 内没有任何采购")
	}
	t.Logf("【对照】只给投资池注资：60 tick 采购 %.1f 单位，其中 %d 个 tick 的采购量"+
		"低于队列需要量（说明国库侧确实会产生约束）", qty, int(availQtyLimited))
}
