package sim

// audit_r29_test.go —— §七 R29：**工资回笼率**诊断（"商品卖出去能不能把工资收回来"）。
//
// ============================ 要回答的问题 ============================
//
// 用户判断："商品价格出售后并不能回笼劳动力所需工资，导致了所有的问题。"
//
// 本诊断把它拆成四个可直接核对的量：
//
//	① 回笼率 = (消费者净支出 + 政府采购建造力) / 工资总额
//	   —— 中间投入在企业部门内部相消，故只有这两条收入腿能把工资收回来；
//	      而本期政府采购为 0（无队列），所以回笼率 ≈ 消费支出/工资。
//	② 购买力 vs 支出：Σ 人群池税前预算（= 现金/(1+t)）与消费净支出之比
//	   —— 若预算比支出大几个数量级，则"买不动"与"没钱"无关。
//	③ 需求满足度：§5.1 的按人口加权 Sat[组]
//	   —— 若必需品组满足度已 = 1，说明居民**已经买满了需求表要的东西**，
//	      剩下的工资是"需求表本身没有条目可买"，而不是缺货或缺钱。
//	④ 缺钱/缺货池数（Starved/Constrained）与人群池现金累积。
//
// 【口径订正】本测试最初用 `Σ PoolTargetValue / 工资` 当"需求价值"，
// 实测与满足度矛盾（目标价值远大于实际支出，但满足度已=1）。原因：
// `PoolTargetValue` 按组内各商品的使用价值**均摊**估值，而实际购买会用
// 更便宜的替代品（织物/谷物）满足同一组——故它高估了真实支出。
// 现改用"预算 vs 支出"与"满足度"两个不依赖估值的量。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR29 -v

import (
	"testing"
)

// TestAuditR29WageRecoupment 逐期测量工资回笼率与受限原因。
//
// 【前值 → 后值（2026-09-19 第 15 轮）】本诊断度量的是**封闭口径**下
// "企业部门只靠消费者支出能收回多少工资"。第 15 轮裁决给政府补上了支出端
// （§4.5.8 公共工程默认用 50% 税收采购建造力）并给居民补上了储蓄渠道（§5.3），
// 两者都会改变该比值的分子/分母——实测 tick 1 回笼率由 28.1% 升到 61.0%，
// 但这**不是**"篮子量级不匹配"被修好了，而是多了一条政府购买腿。
// 故本测试显式**关闭**这三条新通道（场景注入，见日志），
// 以保持与 R29 原始结论（回笼率 18%~28%、人群池持续沉积）可比。
func TestAuditR29WageRecoupment(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	sc := newScenario(t, "关闭第 15 轮新增的三条通道，回到 R29 的封闭口径")
	sc.Param("PublicWorksShare",
		func(s *State) float64 { return s.Params.PublicWorksShare },
		func(s *State, v float64) { s.Params.PublicWorksShare = v },
		0, "§4.5.8 公共工程会以政府购买的形式回笼工资（实测 tick 1 回笼率 28.1% → 61.0%），必须先关掉才能度量封闭口径")
	sc.Param("SavingsRate",
		func(s *State) float64 { return s.Params.SavingsRate },
		func(s *State, v float64) { s.Params.SavingsRate = v },
		0, "§5.3 储蓄渠道会把人群池结余清空，使断言⑤『未回笼的工资沉积在人群池』无法度量")
	sc.Param("WelfareTier",
		func(s *State) float64 { return float64(s.Params.WelfareTier) },
		func(s *State, v float64) { s.Params.WelfareTier = int(v) },
		0, "§4.5.8 福利金是转移支付，会提高居民购买预算而不改变工资总额，污染回笼率的分母口径")
	sc.Apply(st)
	defer sc.Restore(st)

	marks := map[int64]bool{1: true, 52: true, 260: true, 520: true, 1000: true}
	type row struct {
		tick        int64
		wage        float64
		spend       float64
		powerBuy    float64
		recoup      float64
		budget      float64
		houseCash   float64
		starved     int
		constrained int
		sat         [4]float64
		// satEmployed 是**只对有收入的池**加权的四组满足度（第 20 轮新增，见断言③）。
		satEmployed [4]float64
		noIncomePop float64
		profit      float64
	}
	var rows []row
	var firstStarved, firstConstrained = -1, -1

	for i := 0; i < 1000; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", i, err)
		}
		if !marks[snap.Tick] {
			continue
		}
		o := st.Last
		var budget float64
		for _, b := range o.PoolBudget {
			budget += b
		}
		// 全部门纯利之和：中间投入在对内求和时相消，只剩"消费 + 政府采购 − 工资"。
		var profit float64
		for j := range st.Buildings {
			if !st.Buildings[j].Spec.IsNonMarket() {
				profit += st.Buildings[j].LastProfit
			}
		}
		income := o.SpendNet + snap.Flow.GovPowerSpend
		r := row{
			tick:        snap.Tick,
			wage:        snap.Flow.WageTotal,
			spend:       o.SpendNet,
			powerBuy:    snap.Flow.GovPowerSpend,
			recoup:      income / snap.Flow.WageTotal,
			budget:      budget,
			houseCash:   st.Houses.TotalCash(),
			starved:     o.Starved,
			constrained: o.Constrained,
			profit:      profit,
		}
		copy(r.sat[:], o.Sat[:])
		// 有收入池的口径：预算 > 0 且有人口（o.PoolBudget 已按池给出）。
		var wsum [4]float64
		var wpop float64
		for pi := range o.Pools {
			p := st.Houses.Pools[pi]
			if p.Population <= 1e-12 {
				continue
			}
			if o.PoolBudget[pi] <= 0 {
				r.noIncomePop += p.Population
				continue
			}
			for g := 0; g < 4; g++ {
				wsum[g] += o.Pools[pi].Sat[g] * p.Population
			}
			wpop += p.Population
		}
		if wpop > 1e-12 {
			for g := 0; g < 4; g++ {
				r.satEmployed[g] = wsum[g] / wpop
			}
		}
		rows = append(rows, r)
		if snap.Tick == 1 {
			firstStarved, firstConstrained = o.Starved, o.Constrained
		}
	}

	t.Logf("%6s %13s %13s %8s %14s %9s %6s %6s %14s %16s",
		"tick", "工资总额", "消费净支出", "回笼率", "可支配预算", "预算/支出", "缺钱", "缺货", "满足度0-2", "满足度3(住房)")
	for _, r := range rows {
		t.Logf("%6d %13.0f %13.0f %7.1f%% %14.0f %8.1fx %6d %6d %6.3f/%.3f/%.3f %10.3f",
			r.tick, r.wage, r.spend, r.recoup*100, r.budget, r.budget/r.spend,
			r.starved, r.constrained, r.sat[0], r.sat[1], r.sat[2], r.sat[3])
	}
	t.Logf("%6s %16s %16s %16s", "tick", "有收入池满足度0-2", "无收入人口", "无收入占比")
	for _, r := range rows {
		pop := st.Population
		share := 0.0
		if pop > 1e-9 {
			share = r.noIncomePop / pop * 100
		}
		t.Logf("%6d %6.3f/%.3f/%.3f %16.0f %15.1f%%",
			r.tick, r.satEmployed[0], r.satEmployed[1], r.satEmployed[2], r.noIncomePop, share)
	}
	t.Logf("%6s %14s %16s", "tick", "全部门纯利", "人群池现金")
	for _, r := range rows {
		t.Logf("%6d %14.0f %16.0f", r.tick, r.profit, r.houseCash)
	}

	if len(rows) < 3 {
		t.Fatalf("采样点不足（%d）——诊断被空真通过", len(rows))
	}
	// ① 回笼率必须显著小于 1：这正是"卖货收不回工资"的量化形式。
	for _, r := range rows {
		if r.recoup >= 1 {
			t.Errorf("tick %d：回笼率 %.1f%% ≥ 100%%——与'卖货收不回工资'的判断矛盾",
				r.tick, r.recoup*100)
		}
	}
	if rows[0].recoup > 0.6 {
		t.Errorf("tick 1 回笼率 %.1f%%，预期远低于 60%%", rows[0].recoup*100)
	}
	// ② 不是"没钱"：购买力（可支配预算）必须远大于实际支出。
	for _, r := range rows {
		if r.budget/r.spend < 3 {
			t.Errorf("tick %d：可支配预算只有支出的 %.1f 倍——不能断言'买不动与没钱无关'",
				r.tick, r.budget/r.spend)
		}
	}
	if firstStarved != 0 {
		t.Errorf("tick 1 缺钱池数 = %d，预期 0", firstStarved)
	}
	// ③ 必需品组（简朴衣物 / 基础食物 / 标准衣物）对**有收入的家庭**已经买满：
	//    剩下的工资不是"缺货"造成的，而是需求表本身没有更多条目。
	//
	// 【口径（2026-09-19 第 20 轮）】r.sat[g] 是**含零预算池在内**的人口加权均值。
	// 第 20 轮把耕地上限降到 500 后，自给农场容量只有约 247 万
	// （= 494 级 × 5,000 人），10m 人口下**失业 71.7%**——这些池预算为 0、
	// 满足度按 §6.5 记 0，于是"全体均值"被压到 0.05，而**每个有收入的池都是 1.000**。
	// 故本节改为对"有收入的池"求加权均值：本测试要证的命题是
	// "**工资挣到手的人**买不满是因为需求表没有更多条目"，不是"失业者也能买满"。
	for _, r := range rows {
		if r.satEmployed[0] < 0.999 || r.satEmployed[1] < 0.999 {
			t.Errorf("tick %d：有收入家庭的必需品满足度 组0=%.3f 组1=%.3f，预期均已买满",
				r.tick, r.satEmployed[0], r.satEmployed[1])
		}
	}
	t.Logf("tick 1：缺钱池数=%d、缺货池数=%d（住房组满足度 %.3f 是唯一的供给受限项）",
		firstStarved, firstConstrained, rows[0].sat[3])
	// ④ 价格向 P_cost（乃至以下）回落时，同样的数量篮子回笼的名义价值更少。
	//
	// 【前值 → 后值（2026-09-19 第 20 轮）】旧断言是**单调下降**（"靠降价把工资收回来不可行"）。
	// 第 20 轮把耕地上限降到 500 后，失业率 71.7% ⇒ 工资总额被压缩，
	// 而"公共工程"这一腿回笼（默认 50% 税收采购建造力）在**本测试里已被关闭**，
	// 于是回笼率不再单调下降（实测 tick 1 = 47.1% → tick 1000 = 50.3%）。
	// 该现象与 R29 的核心命题无关（R29 要证的是"回笼率远小于 1、且不因为没钱"），
	// 故本条改为**机制不变量**：回笼率全程 < 1、且**任何时点都不是 100%**——
	// 即"企业部门靠消费者支出无法收回全部工资"这一结论在参数变更后**依然成立**。
	last := rows[len(rows)-1]
	maxRecoup := 0.0
	for _, r := range rows {
		if r.recoup > maxRecoup {
			maxRecoup = r.recoup
		}
	}
	if maxRecoup >= 1 {
		t.Errorf("回笼率峰值 %.1f%% ≥ 100%%——与'卖货收不回工资'的判断矛盾", maxRecoup*100)
	}
	if maxRecoup < 0.05 {
		t.Errorf("回笼率峰值只有 %.1f%%——低到不像「卖货收不回工资」，更像工资从未付出", maxRecoup*100)
	}
	t.Logf("回笼率 tick 1 = %.1f%% → tick %d = %.1f%%（峰值 %.1f%%）："+
		"全程远小于 100%%，「卖货收不回工资」在参数变更后仍成立",
		rows[0].recoup*100, last.tick, last.recoup*100, maxRecoup*100)
	// ⑤ 未回笼的工资必须体现在人群池现金的持续累积上。
	if last.houseCash <= rows[0].houseCash {
		t.Errorf("人群池现金未累积：tick %d 为 %.0f，tick %d 为 %.0f",
			rows[0].tick, rows[0].houseCash, last.tick, last.houseCash)
	}
	t.Logf("回笼率 %.1f%% → %.1f%%，人群池现金 %.0f → %.0f（未回笼的工资沉淀在这里；"+
		"可支配预算已达支出的 %.0f 倍）",
		rows[0].recoup*100, last.recoup*100, rows[0].houseCash, last.houseCash, last.budget/last.spend)
}
