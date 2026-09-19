package sim

// audit_r32_test.go —— §七 R32：**当期（动态）零利润价**的口径诊断。
//
// ============================ 背景 ============================
//
// 用户判断："价格本身应该是一个动态的东西，零利润价却是固定的。要么放弃零利润价，
// 要么把零利润价计算改成动态的，且概念改成：**让生产单位 0 利润的价格**。"
//
// 实现把 P_cost 的**两个角色**拆开（这是本实验最关键的发现）：
//
//	A. 需求归一化 D = a(P/P_cost)^(−ε) 的参考价 —— **必须固定**。
//	   a 是在 t=0 用 P_init/P_cost 定标的；若把 P_cost 换成与 P 正相关的
//	   "当期成本"，D 会随 P 上升而上升 ⇒ **价格→成本→需求→价格的自指正反馈**
//	   （实测：价格水平自我抬升、税收被放大 5 倍、政府沉淀翻倍）。
//
//	B. 价格钳制带 [0.2,5]×P⁰ 与 A1/A3 判据的参考价 —— **应当是动态的**：
//	   P⁰_j(t) = Σ_i A[i][j]·P_i(t) + l_j（"让生产单位 0 利润的售价"）。
//	   成本变了，地板/天花板要跟着动，否则就是 §3.4 记录的"地板脱节"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR32 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

func newDynPcostState(t *testing.T, dynamic bool, priceMult float64) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		// 契约现行口径 = 动态（默认）；dynamic=false 走 -static-pcost 的对照臂。
		StaticPcost:   !dynamic,
		InitPriceMult: priceMult,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	return st
}

// TestAuditR32DynamicPcostFormula 校验 P⁰ 的算式与"两个角色分开"。
func TestAuditR32DynamicPcostFormula(t *testing.T) {
	auditEnabled(t)
	static := newDynPcostState(t, false, 1)
	dyn := newDynPcostState(t, true, 1)
	// P⁰ 是在 tick 内【价格结算之前】用当 tick 的期初价格算出来的，
	// 故复算必须取 Step 之前的那个价格切片（否则差了本 tick 的价格变动）。
	pricesBefore := dyn.Market.Prices()
	if _, err := static.Step(); err != nil {
		t.Fatalf("static: %v", err)
	}
	if _, err := dyn.Step(); err != nil {
		t.Fatalf("dyn: %v", err)
	}

	cal := dyn.calibration
	var checked int
	for j, b := range dyn.Buildings {
		if j >= model.Goods || b.Spec.IsNonMarket() || b.Spec.Recipe.Qty <= 0 {
			continue
		}
		// ① 动态模式：P⁰_j = w·Σ_i A[i][j]·P_i + l_j（用期初价格复算，逐位一致）。
		//
		// 【前值 → 后值（2026-09-19 第 16 轮）】仓库落地前 w = 1，即 P⁰ = ΣA·P + l；
		// 落地后买家加载 w = (1+加价)(1+消费税) = 1.07625 进入投入项
		// —— 因为投入是按**买家的实际成本**计价的（§3.4 / §4.5.6）。
		w := dyn.Params.BuyerWedge()
		var want float64
		for i := 0; i < model.Goods; i++ {
			want += cal.A[i][j] * pricesBefore[i]
		}
		want = w*want + b.Spec.WagePerLevel()/b.Spec.Recipe.Qty
		if got := dyn.Market.Markets[j].Pzero; math.Abs(got-want) > 1e-9*math.Max(1, want) {
			t.Errorf("%s：P⁰ = %.6f，按 w·Σ A[i][j]·P_i + l_j 应为 %.6f（w=%.5f）", b.Spec.Name, got, want, w)
		}
		// ② 静态模式：P⁰ 恒等于契约的 P_cost（逐位一致 ⇒ 关闭开关不影响既有证据）。
		if got := static.Market.Markets[j].Pzero; math.Abs(got-static.Goods[j].Pcost) > 1e-9 {
			t.Errorf("%s：静态模式下 P⁰ = %.4f，应 ≡ P_cost = %.4f", b.Spec.Name, got, static.Goods[j].Pcost)
		}
		checked++
	}
	if checked < 10 {
		t.Fatalf("只检查了 %d 个部门——断言被空真通过", checked)
	}
	// ③ 需求归一化的参考价（Good.Pcost）**不被**动态开关改动：它必须保持固定。
	for i := range dyn.Goods {
		if math.Abs(dyn.Goods[i].Pcost-static.Goods[i].Pcost) > 1e-12 {
			t.Errorf("%s：需求归一化的参考价被动态开关改动（%.4f vs %.4f）——这正是要避免的自指反馈",
				dyn.Goods[i].Name, dyn.Goods[i].Pcost, static.Goods[i].Pcost)
		}
		if math.Abs(dyn.Market.Markets[i].A-static.Market.Markets[i].A) > 1e-9 {
			t.Errorf("%s：需求锚 a 被动态开关改动（%.4f vs %.4f）",
				dyn.Goods[i].Name, dyn.Market.Markets[i].A, static.Market.Markets[i].A)
		}
	}
	t.Logf("已核对 %d 个部门：动态 P⁰ = w·ΣA·P + l 成立（w=%.5f）；需求参考价与需求锚均未被改动",
		checked, dyn.Params.BuyerWedge())
}

// TestAuditR32BandFollowsCost 校验"钳制带跟着成本走"——这正是动态化的用处。
//
// 在 3× 开局价下（P_init = 3.6×P_cost），静态带的上限 5×P_cost 会很快把价格截住
// （R31 实测：铁/钢/工具/建造力全部顶在 5×P_cost）；动态带的上限是 5×P⁰(t)，
// 而 P⁰(t) ≈ 3.6×P_cost ⇒ 上限随之抬高，不再误截。
func TestAuditR32BandFollowsCost(t *testing.T) {
	auditEnabled(t)
	static := newDynPcostState(t, false, 3)
	dyn := newDynPcostState(t, true, 3)
	if _, err := static.Step(); err != nil {
		t.Fatalf("static: %v", err)
	}
	if _, err := dyn.Step(); err != nil {
		t.Fatalf("dyn: %v", err)
	}

	iron := buildingByOutput(static.Buildings, 6) // 铁 = 商品 6
	if iron < 0 {
		t.Fatal("找不到铁矿建筑")
	}
	staticCeil := static.Goods[6].PriceCeilRatio * static.Goods[6].Pcost
	dynCeil := dyn.Goods[6].PriceCeilRatio * dyn.Market.Markets[6].Pzero
	if dynCeil <= staticCeil*2 {
		t.Errorf("3× 下动态上限 %.0f 未显著高于静态上限 %.0f —— 带没有跟着成本走",
			dynCeil, staticCeil)
	}
	t.Logf("铁：静态带上限 = 5×P_cost = %.0f；动态带上限 = 5×P⁰(t) = %.0f（P⁰ = %.0f）",
		staticCeil, dynCeil, dyn.Market.Markets[6].Pzero)

	// 跑一段，确认动态带下铁的价格**能**超过静态上限（不再被误截）。
	for i := 0; i < 400; i++ {
		if _, err := static.Step(); err != nil {
			t.Fatalf("%v", err)
		}
		if _, err := dyn.Step(); err != nil {
			t.Fatalf("%v", err)
		}
	}
	pStatic := static.Market.Markets[6].Price
	pDyn := dyn.Market.Markets[6].Price
	t.Logf("400 tick 后铁价：静态带 %.0f（上限 %.0f）／动态带 %.0f", pStatic, staticCeil, pDyn)
	if pDyn <= pStatic {
		t.Errorf("动态带下铁价 %.0f 未高于静态带 %.0f —— 动态化没有解除误截", pDyn, pStatic)
	}
	// 两条都不得出现非有限值。
	for _, st := range []*State{static, dyn} {
		for i := range st.Market.Markets {
			if math.IsNaN(st.Market.Markets[i].Price) || math.IsInf(st.Market.Markets[i].Price, 0) {
				t.Fatalf("%s 价格非有限值", st.Goods[i].Name)
			}
		}
	}
}
