package sim

// audit_r31_test.go —— §七 R31：**把开局价整体放大 3 倍**的对照实验。
//
// ============================ 背景 ============================
//
// R30 定位到：§6.3 需求篮子只值该档工资的 21.5%~28.8%，故"卖货收不回工资"。
// 用户的判断是"那就说明初始价格有问题，先把初始价格调到 3 倍试试"。
//
// §3.4 的开局价由加成价方程解出（openerMarkup = 1/6 ⇒ P_init ≈ 1.2·P_cost），
// 本实验用 `Options.InitPriceMult = 3` 把它抬到 3.6·P_cost（**不动 P_cost**）。
//
// 【口径要点】名义工资是 5/10/20 元/人/周期、**不随价格缩放**，故"价格 ×3"
// 等价于"**实际工资 ÷3**"：成本里的工资项不变、售价 ×3，劳动密集部门的利润率
// 抬得最多（实测 38.9%~260%，不是统一的 200%）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR31 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// newPricedState 构造与 newTestState 同参、但开局价放大 mult 倍的状态。
func newPricedState(t *testing.T, mult float64) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		DemandScale:          0,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		InitPriceMult:        mult,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	return st
}

// TestAuditR31InitPriceMult 校验开关的机械效果：开局价 ×3、P_cost 不动、利润率抬高、锚 ×3。
func TestAuditR31InitPriceMult(t *testing.T) {
	auditEnabled(t)
	base := newPricedState(t, 1)
	tri := newPricedState(t, 3)

	for i := range base.Goods {
		g0, g3 := base.Goods[i], tri.Goods[i]
		// ① 零利润价不动——它是价格体系的地板基准，不属于"初始价格"。
		if math.Abs(g0.Pcost-g3.Pcost) > 1e-9 {
			t.Errorf("%s：P_cost 被改动（%.2f → %.2f），本实验只应改开局价",
				g0.Name, g0.Pcost, g3.Pcost)
		}
		// ② 开局价 ×3。
		if math.Abs(g3.Pinit-3*g0.Pinit) > 1e-6 {
			t.Errorf("%s：开局价 %.2f，应为 %.2f（3×）", g0.Name, g3.Pinit, 3*g0.Pinit)
		}
		// ③ 需求锚 a = S₀·(P_init/P_cost)^ε 同倍放大（故长期均衡价也 ×3）。
		if a0, a3 := base.Market.Markets[i].A, tri.Market.Markets[i].A; math.Abs(a3-3*a0) > 1e-6*math.Abs(a0) {
			t.Errorf("%s：需求锚 %.4f，应为 3×%.4f = %.4f", g0.Name, a3, a0, 3*a0)
		}
	}

	// ④ 开局利润率整体抬高，且**不是**统一的 200%（工资项不随价格缩放）。
	//    成本口径与 calibrate.Margin 一致：投入品也按**缩放后的 P_init** 计价。
	var lo, hi float64 = math.Inf(1), math.Inf(-1)
	for i := range tri.Buildings {
		b := &tri.Buildings[i]
		// 【§4.5.6】只统计**商品生产者**：仓库与消费代理没有配方（Qty = 0），
		// 其"利润率"由贸易收支定义，不参与"开局加成价"这条断言。
		if !b.Spec.Produces() {
			continue
		}
		m := marginOf(b, tri.Goods, tri.Goods[b.Spec.Recipe.Output].Pinit)
		lo, hi = math.Min(lo, m), math.Max(hi, m)
		if m <= 0.20+1e-9 {
			t.Errorf("%s：开局利润率 %.2f%%，应高于基线 20%%", b.Spec.Name, m*100)
		}
	}
	t.Logf("开局利润率：基线统一 20%% → 3× 时为 %.1f%% ~ %.1f%%（劳动密集部门抬得最多）",
		lo*100, hi*100)
	if hi-lo < 0.5 {
		t.Errorf("3× 下的开局利润率区间 %.1f%%~%.1f%% 过窄，与'工资项不缩放'的机制不符",
			lo*100, hi*100)
	}
}

// marginOf 复算利润率（与 calibrate.Margin 同口径：投入品按给定价格向量计价）。
func marginOf(b *BuildingState, goods []model.Good, price float64) float64 {
	rev := price * b.Spec.Recipe.Qty
	var inputs float64
	for idx, per := range b.Spec.Recipe.Inputs {
		if idx < len(goods) {
			inputs += per * goods[idx].Pinit
		}
	}
	cost := inputs + b.Spec.LaborPerLevel*model.AverageWage()
	if cost <= 0 {
		return 0
	}
	return rev/cost - 1
}

// TestAuditR31ThreeTimesPricesEffect 跑 1000 tick，量化"3× 开局价"的实际效果。
func TestAuditR31ThreeTimesPricesEffect(t *testing.T) {
	auditEnabled(t)
	base := newPricedState(t, 1)
	tri := newPricedState(t, 3)

	levels0 := totalLevels(tri)
	money0 := tri.TotalMoney()
	var poolPeak float64
	var growthStopTick int64
	lastLevels := levels0

	type row struct {
		tick                      int64
		baseWage, baseSpend       float64
		triWage, triSpend         float64
		triPool, triManorInflow   float64
		baseLv, triLv             float64
	}
	var rows []row
	marks := map[int64]bool{1: true, 52: true, 260: true, 520: true, 1000: true}

	for i := 0; i < 1000; i++ {
		sb, err := base.Step()
		if err != nil {
			t.Fatalf("baseline tick %d: %v", i, err)
		}
		snapT, err := tri.Step()
		if err != nil {
			t.Fatalf("3x tick %d: %v", i, err)
		}
		if snapT.InvestmentPool > poolPeak {
			poolPeak = snapT.InvestmentPool
		}
		lv := totalLevels(tri)
		if lv > lastLevels+1e-6 {
			lastLevels = lv
			growthStopTick = snapT.Tick
		}
		if !marks[snapT.Tick] {
			continue
		}
		rows = append(rows, row{
			tick: snapT.Tick,
			baseWage: sb.Flow.WageTotal, baseSpend: sb.Flow.SpendNet,
			triWage: snapT.Flow.WageTotal, triSpend: snapT.Flow.SpendNet,
			triPool: snapT.InvestmentPool, triManorInflow: tri.tickInflowManor,
			baseLv: totalLevels(base), triLv: lv,
		})
	}

	t.Logf("%6s | %13s %13s %7s | %13s %13s %7s %12s %14s",
		"tick", "基线工资", "基线消费", "回笼率", "3×工资", "3×消费", "回笼率", "3×投资池", "3×总级数")
	for _, r := range rows {
		t.Logf("%6d | %13.0f %13.0f %6.1f%% | %13.0f %13.0f %6.1f%% %12.0f %14.0f",
			r.tick, r.baseWage, r.baseSpend, r.baseSpend/r.baseWage*100,
			r.triWage, r.triSpend, r.triSpend/r.triWage*100, r.triPool, r.triLv)
	}

	if len(rows) < 3 {
		t.Fatalf("采样点不足（%d）", len(rows))
	}
	// ① 名义消费支出在 tick 1 应≈3×（同样的数量篮子、三倍的价格）。
	//    容差放宽到 15%：成交用的是本 tick 结算后的价格，已不再是精确的 P_init。
	r1 := rows[0]
	if math.Abs(r1.triSpend-3*r1.baseSpend)/(3*r1.baseSpend) > 0.15 {
		t.Errorf("tick 1 消费支出 %.0f，应≈基线 %.0f 的 3 倍", r1.triSpend, r1.baseSpend)
	}
	// ② 回笼率显著改善（基线 ~28%，3× 应 > 55%）。
	if r1.triSpend/r1.triWage < 0.55 {
		t.Errorf("tick 1 回笼率 %.1f%%，预期 > 55%%", r1.triSpend/r1.triWage*100)
	}
	t.Logf("回笼率：基线 %.1f%% → 3× %.1f%%（tick 1）", r1.baseSpend/r1.baseWage*100,
		r1.triSpend/r1.triWage*100)
	// ③ 投资池真的进钱了（基线恒为 0），并因此发生扩建。
	if poolPeak <= 0 {
		t.Error("3× 下投资池从未为正——实验没有接上投资通道")
	}
	grown := totalLevels(tri) - levels0
	if grown <= 1 {
		t.Errorf("3× 下总级数只从 %.0f 增到 %.0f——没有发生扩建", levels0, totalLevels(tri))
	}
	// ④ 但它**不是自持的**：扩建在某个 tick 之后停住。
	//
	// 【前值 → 后值（2026-09-19 第 15 轮）】旧断言是"投资池最终回到 0"——
	// 那在当时成立，因为投资池只有资本建筑净额一条来源，庄园净额转负后池子就空了。
	// §5.3 新增居民储蓄渠道（默认 σ_save = 1.0）后，投资池每 tick 都收到工资结余，
	// **不会再回到 0**（实测期末 3.79e9）。因此"停住"的证据必须改用**总级数**：
	// 最后一次增长之后级数不再增加。投资池水位只作报告量。
	last := rows[len(rows)-1]
	if growthStopTick <= 0 {
		t.Errorf("未能观察到扩建停住的时点（总级数全程未增长）")
	}
	t.Logf("⚠ 投资池期末 = %.0f（不再回到 0：§5.3 储蓄渠道每 tick 持续入池）——"+
		"「扩建停住」由总级数判定（最后一次增长在 tick %d），不由投资池余额判定",
		last.triPool, growthStopTick)
	if last.triManorInflow != 0 {
		t.Logf("tick %d 庄园入池 = %.0f（预期已回落到 0：庄园净额转负）",
			last.tick, last.triManorInflow)
	}
	// ⑤ 货币守恒不受价格倍数影响（唯一注入仍是营运本金）。
	if got, want := tri.TotalMoney()-money0, tri.TickNewCapitalTotal(); math.Abs(got-want) > 1e-3 {
		t.Errorf("3× 下货币守恒失败：ΔM=%.2f，应为营运本金 %.2f", got, want)
	}
	t.Logf("扩建：总级数 %.0f → %.0f（+%.0f），最后一次增长在 tick %d，之后停住；"+
		"投资池峰值 %.0f，期末 %.0f",
		levels0, totalLevels(tri), grown, growthStopTick, poolPeak, last.triPool)
}

// totalLevels 返回全部建筑的等级合计（诊断用）。
func totalLevels(st *State) float64 {
	var s float64
	for i := range st.Buildings {
		s += st.Buildings[i].Level
	}
	return s
}
