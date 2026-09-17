package sim

// audit_equilibrium_test.go —— 手动推演平衡价格与平衡等级
//
// ============================ 定义（契约 §2.4 / §3.4） ============================
//
// 本文件里的"平衡"指**零利润平衡**：
//
//	① 平衡价格  P* = P_cost —— 利润率为零的价格（§3.4 零利润价方程的解）
//	② 平衡等级  L*          —— 使 P* 恰好落在零利润价、且供给等于需求的那个产能水平
//
// 推导：
//
//	价格方程在均衡处静止（dP/dt = d²P/dt² = 0）⇒ E = D − S = 0 ⇒ D = S
//	需求函数 D = a (P*/P_cost)^{−ε}，在 P* = P_cost 处 D = a
//	故均衡条件化为       S* = a
//
//	利润率归零 ⇔ 收入 = 成本，而成本含【中间投入】，中间投入本身随产能变化，
//	故这是一个联立方程：
//
//	    L*_i = (最终需求_i + Σ_j c_ij · L*_j) / q_i
//
//	其中 c_ij 是建筑 j 每级对商品 i 的消耗量（§3.3 配方直接给出），
//	q_i 是建筑 i 的单级产出。
//
// 这与开局布点用的是同一个物质平衡方程，区别只在需求取哪个价格点：
//
//	统一起始等级 N_0 = 5   —— 契约指定的固定起点（本次修订）
//	平衡等级 L*            —— 使 P* = P_cost 的产能水平（本文件求解）
//
// 注意 L* 对应"利润率恰好为零"的假想位置。契约 §3.4 明确说明实际停建点
// **严格早于** L*（因为 10% 阈值），所以 L* 是产能的上界，
// 实际稳态落在 [N_0, L*) 之间。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditEquilibrium -v

import (
	"fmt"
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// finalDemandAtCostPrice 返回 P = P_cost 处每种商品的最终需求总量。
//
// 口径与 §5.1 / §6.3 / consume.PurchaseByPools 一致：
// 逐阶级查离散需求表，按该阶级人口缩放，再按使用价值权重分摊到具体商品。
func finalDemandAtCostPrice(st *State, pop float64) []float64 {
	out := make([]float64, model.Goods)
	groups := model.ConsumeGroupSpecs()
	for c := range model.CohortShares {
		d := model.DemandAt(model.CohortWages[c])
		sub := pop * model.CohortShares[c]
		for gi := range groups {
			var totalUse float64
			for _, v := range groups[gi].Uses {
				totalUse += v
			}
			if totalUse <= 0 {
				continue
			}
			target := d[gi] * sub / 100000 * st.demandScale
			for idx, v := range groups[gi].Uses {
				out[idx] += target * v / totalUse
			}
		}
	}
	return out
}

// equilibriumLevels 求每种商品的零利润平衡等级 L*（不动点迭代）。
//
//	L_i ← (最终需求_i + Σ_j c_ij·L_j) / q_i
//
// 建造力是投资品、金融区按掌控比定价，二者都不由消费需求决定，故排除在迭代之外。
func equilibriumLevels(st *State, pop float64) (levels, finalDemand []float64, iters int) {
	n := len(st.Buildings)
	finalDemand = finalDemandAtCostPrice(st, pop)
	levels = make([]float64, n)

	isProducer := func(i int) bool {
		return !st.Buildings[i].Spec.IsFinance &&
			st.Buildings[i].Spec.Recipe.Output != powerGoodIndex
	}
	// 初值：只按最终需求布点（不含中间投入），随后迭代补上链条消耗
	for i := range st.Buildings {
		if !isProducer(i) {
			continue
		}
		if q := st.Buildings[i].Spec.Recipe.Qty; q > 0 {
			levels[i] = finalDemand[st.Buildings[i].Spec.Recipe.Output] / q
		}
	}
	const maxIter, tol = 4000, 1e-11
	for it := 0; it < maxIter; it++ {
		used := make([]float64, model.Goods)
		for j := range st.Buildings {
			if !isProducer(j) {
				continue
			}
			for good, qty := range st.Buildings[j].Spec.Recipe.Inputs {
				used[good] += qty * levels[j]
			}
		}
		maxChange := 0.0
		for i := range st.Buildings {
			if !isProducer(i) {
				continue
			}
			need := finalDemand[st.Buildings[i].Spec.Recipe.Output] + used[st.Buildings[i].Spec.Recipe.Output]
			want := need / st.Buildings[i].Spec.Recipe.Qty
			if d := math.Abs(want - levels[i]); d > maxChange {
				maxChange = d
			}
			levels[i] = want
		}
		iters = it + 1
		if maxChange < tol {
			break
		}
	}
	for i := range st.Buildings {
		if !isProducer(i) {
			levels[i] = st.Buildings[i].Level
		}
	}
	return levels, finalDemand, iters
}

// budgetFeasibility 校验"平衡态的需求居民买不买得起"。
//
// 平衡态若要真正成立，需要价格方程的名义需求 D 与实际可成交的**有效需求**相等。
// 名义需求由 §6.3 表给出，有效需求受居民现金约束——两者的比就是本函数算的比值。
//
// 三表联合标定的口径（calibrate.jointDemandScale 的注释明确写了
// 「这里不含税率（税率由调用方在运行时决定），故基准情形按 t = 0 标定」）：
//
//	k = 每级工资 / 每级最终需求价值(P_cost)
//
// 于是"工资收入 = 最终需求价值"只在不收税时成立。收全过程 10% 交易税后，
// 居民可支配额只剩 1/(1+t) 的水平，而 §6.3 的需求表并未随税率缩小。
func budgetFeasibility(st *State, totalLevels, demandValue float64) (wage, avail, ratio, ratioNet float64) {
	wage = totalLevels * 5000 * model.AverageWage()
	t := st.Params.TaxRate
	// 居民支付的是含税总额，故能买到的税前商品价值 = 现金 / (1+t)。
	avail = wage / (1 + t)
	ratio = wage / demandValue     // 不收税时的覆盖率
	ratioNet = avail / demandValue // 实收 10% 税后的覆盖率
	return
}

// TestAuditEquilibrium 手动推演平衡价格与平衡等级，并核对
// 用户要求：「初始等级 < 平衡等级」。
func TestAuditEquilibrium(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	const pop = 1e7

	levels, finalDemand, iters := equilibriumLevels(st, pop)

	fmt.Printf("\n人口 %.0f   需求缩放 k = %.4f   不动点迭代 %d 次收敛\n",
		pop, st.demandScale, iters)
	fmt.Printf("平衡价格 P* = P_cost（§3.4 零利润价方程 p = (I−Aᵀ)⁻¹·l 的解）\n\n")
	fmt.Printf("%-12s %10s %12s %10s %12s %14s %8s\n",
		"建筑", "起始等级", "平衡等级L*", "单级产出", "P_cost", "最终需求", "判定")

	prodTotal, prodOK, allOK := 0, 0, true
	var totalLevels float64
	for i := range st.Buildings {
		b := &st.Buildings[i]
		if b.Spec.IsFinance || b.Spec.Recipe.Output == powerGoodIndex {
			continue
		}
		verdict := "OK"
		prodTotal++
		if levels[i] <= b.Level {
			verdict = "★失败"
			allOK = false
		} else {
			prodOK++
		}
		totalLevels += levels[i]
		fmt.Printf("%-12s %10.2f %12.2f %10.0f %12.2f %14.1f %8s\n",
			b.Spec.Name, b.Level, levels[i], b.Spec.Recipe.Qty,
			st.Goods[b.Spec.Recipe.Output].Pcost, finalDemand[b.Spec.Recipe.Output], verdict)
	}

	// 建造力与金融区不由消费需求决定，单独报告
	pw := &st.Buildings[powerGoodIndex]
	fmt.Printf("%-12s %10.2f %12s %10.0f %12.2f %14s %8s\n",
		pw.Spec.Name, pw.Level, "（投资品）", pw.Spec.Recipe.Qty,
		st.Goods[powerGoodIndex].Pcost, "—", "—")
	fin := &st.Buildings[model.FinanceIndex]
	fmt.Printf("%-12s %10.2f %12s %10.0f %12s %14s %8s\n",
		fin.Spec.Name, fin.Level, "（掌控比）", fin.Spec.LaborPerLevel, "—", "—", "—")

	fmt.Printf("\n生产建筑共 %d 种，起始等级严格小于 L* 的有 %d 种\n", prodTotal, prodOK)

	// ---------- 平衡态的可支付性：名义需求 vs 有效需求 ----------
	var demandValue float64
	for g := range finalDemand {
		demandValue += finalDemand[g] * st.Goods[g].Pcost
	}
	wage, avail, ratio, ratioNet := budgetFeasibility(st, totalLevels, demandValue)
	fmt.Printf("\n【平衡态可支付性】\n")
	fmt.Printf("  平衡等级合计        L*      = %12.2f 级\n", totalLevels)
	fmt.Printf("  工资总额（L*×5000×6.75）    = %12.2f 元\n", wage)
	fmt.Printf("  最终需求价值（P_cost 计价） = %12.2f 元\n", demandValue)
	fmt.Printf("  不收税覆盖率 工资/需求      = %12.6f\n", ratio)
	fmt.Printf("  10%% 税后可购价值 工资/1.1   = %12.2f 元\n", avail)
	fmt.Printf("  10%% 税后覆盖率              = %12.6f\n", ratioNet)
	if ratioNet < 1 {
		fmt.Printf("  ⇒ 结构性缺口：%.4f%%，居民在手现金买不下 §6.3 表列的名义需求\n",
			(1-ratioNet)*100)
		fmt.Printf("     （联合标定按 t=0 定 k，见 calibrate.jointDemandScale 注释）\n")
	}

	if !allOK {
		t.Errorf("存在起始等级 ≥ 平衡等级的建筑：起始产能已达或超过零利润产能，"+
			"这些部门在 t=0 的利润率必然 ≤ 0、不会扩建")
	}
}

// TestAuditLevelCompositionGap 判定"工资 / 最终需求价值"这个比值到底由什么决定。
//
// 契约 §1 与 §8.5 把这个缺口记为"工资是最终消费品价值的 3.15 倍"，
// 并断言它无法通过调整需求缩放系数 k 解决（需求放大 k 倍 → 布点级数也放大 →
// 工资也放大 k 倍 → 比值不变）。
//
// 那个"对 k 不变"的论证是对的，但由此只能推出"改 k 无效"，
// **不能推出"缺口在 §6.3 需求表的量级"**。本测试把真实成因钉死：
//
//	工资总额     = 33750 × Σ L_i                       （每级工资 5000×6.75）
//	最终需求价值 = k × Σ P_cost_i × f_i
//	而 Σ L*_i    = Σ (最终需求_i + 中间投入_i)/q_i = k × Σ [(I−A)⁻¹f]_i/q_i
//
// 故  工资 / 最终需求价值 = Σ L_i / Σ L*_i
//
// k 在分子分母各出现一次、恰好抵消（这与契约的判断一致），
// 于是该比值**只由「实际布点 L」与「需求匹配布点 L*」的构成比决定**，
// 与 k 无关、与 §6.3 需求表的量级无关。在需求匹配布点上它恒等于 1。
//
// 实测：统一 5 级布点 50.00 级 / 需求匹配 688.61 级 = 0.0726 ⇒ 其倒数为 13.77。
// 契约记载的 3.15 则是【工资 ÷ 已实现的消费支出】——两者不是同一个量：
// 后者还额外受"买得起多少"与"买得到多少"两道约束。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditLevelCompositionGap -v
func TestAuditLevelCompositionGap(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	const pop = 1e7

	// 需求匹配布点（平衡布点）
	eq, finalDemand, _ := equilibriumLevels(st, pop)

	// 实际布点（契约默认：每种生产建筑统一 5 级）
	var actualLevels, eqLevels float64
	for i := range st.Buildings {
		if st.Buildings[i].Spec.IsFinance || st.Buildings[i].Spec.Recipe.Output == powerGoodIndex {
			continue
		}
		actualLevels += st.Buildings[i].Level
		eqLevels += eq[i]
	}
	var demandValue float64
	for g := range finalDemand {
		demandValue += finalDemand[g] * st.Goods[g].Pcost
	}

	wage := func(lv float64) float64 { return lv * 5000 * model.AverageWage() }

	fmt.Printf("\n【比值分解】人口 %.0f   k = %.4f\n", pop, st.demandScale)
	fmt.Printf("%-26s %14s %14s %14s\n", "", "生产级数", "工资总额", "工资/需求价值")
	fmt.Printf("%-26s %14.2f %14.0f %14.6f\n", "实际布点（统一 5 级）",
		actualLevels, wage(actualLevels), wage(actualLevels)/demandValue)
	fmt.Printf("%-26s %14.2f %14.0f %14.6f\n", "需求匹配布点 L*",
		eqLevels, wage(eqLevels), wage(eqLevels)/demandValue)
	fmt.Printf("\n最终需求价值（P_cost 计价）= %.2f，k = %.4f\n", demandValue, st.demandScale)

	// 需求匹配布点上的比值必须为 1（这是 Leontief 恒等式，不是拟合结果）
	if got := wage(eqLevels) / demandValue; math.Abs(got-1) > 1e-3 {
		t.Errorf("需求匹配布点上 工资/最终需求价值 = %.6f，应恒等于 1"+
			"（Leontief 恒等式：33750·ΣL* ≡ Σ P_cost·最终需求）", got)
	}

	// 实际布点的比值必须恰等于"级数构成比"——这就是缺口的全部来源。
	// 容差 1e-4：两者只在浮点意义上"恒等"，残差 6e-6 来自不动点迭代
	// （tol=1e-11）在 688 级量级上的累积误差，不是第二个缺口来源。
	ratioOfLevels := actualLevels / eqLevels
	if got := wage(actualLevels) / demandValue; math.Abs(got-ratioOfLevels) > 1e-4 {
		t.Errorf("实际布点比值 %.6f 与级数构成比 %.6f 不符：缺口还有别的来源",
			got, ratioOfLevels)
	}
	fmt.Printf("\n⇒ 工资/需求价值 = 实际布点级数 / 需求匹配布点级数 = %.2f / %.2f = %.4f\n",
		actualLevels, eqLevels, ratioOfLevels)
	fmt.Printf("   故该比值是【起点产能构成】的函数：k 在分子分母各出现一次、恰好抵消，\n")
	fmt.Printf("   改需求缩放系数 k 无法改变它（契约 §1 的判断正确），\n")
	fmt.Printf("   但缺口的位置是【起点布点】，不是【§6.3 需求表的量级】。\n")
}
