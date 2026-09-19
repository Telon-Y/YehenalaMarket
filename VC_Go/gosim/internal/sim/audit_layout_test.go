package sim

// audit_layout_test.go —— 开局布点与消费结算的审计（§3.3 / §4.2 / §5.1 / §6.3）
//
// 本文件记录三项诊断，它们各自对应一处此前的结构性缺陷：
//
//	TestAuditLayout           开局布点是否让产业链自身匹配（配给比应全部为 1）
//	TestAuditConsumption      消费是否受限于"缺货"还是"缺钱"
//	TestAuditScaleGap         工资与最终需求价值的量级缺口（三表联合标定的输入）
//
// 默认跳过。运行：
//
//	$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAudit -v

import (
	"math"
	"fmt"
	"testing"

	"yehenala/market/internal/model"
	"yehenala/market/internal/produce"
)

// TestAuditLayout 检查开局布点的产业链匹配度。
//
// 【历史缺陷】旧布点算法用 Leontief 完全需求反推级数，漏掉产业链自身的
// 中间消耗需求（尤其建造部门的钢/铁/工具），使煤/铁/钢/工具的级数被算成个位数，
// 配给比只有 0.12~0.17，经济从第 0 tick 起就不可能出清。
//
// 【期望】全部商品的配给比 = 1.0000。
func TestAuditLayout(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	plan := produce.Settle(st.buildingSpecs(), st.levels(), st.hireRates(), st.subsistence())

	fmt.Printf("\n%-12s %10s %12s %12s %10s %12s\n",
		"建筑", "级数", "潜在产出", "实际产出", "配给比", "短缺惩罚")
	for i := range st.Buildings {
		b := &st.Buildings[i]
		if b.Spec.IsFinance {
			continue
		}
		o := b.Spec.Recipe.Output
		fmt.Printf("%-12s %10.2f %12.1f %12.1f %10.4f %12.4f\n",
			b.Spec.Name, b.Level, plan.GrossOutput[o], plan.ActualOutput[o],
			plan.AllocRatio[o], plan.ShortageFactor[i])
	}
	fmt.Printf("\n%-12s %12s %12s %12s %12s\n", "商品", "申报投入", "实际产出", "配给比", "净供给")
	for g := range st.Goods {
		fmt.Printf("%-12s %12.1f %12.1f %12.4f %12.1f\n",
			st.Goods[g].Name, plan.ProposedInputs[g], plan.ActualOutput[g],
			plan.AllocRatio[g], plan.NetSupply[g])
	}

	// 【契约变更后的口径】初始等级统一为 Params.ProductionInitLevel（契约修订后 = 5）。
	//
	// 旧的布点算法按"中间消耗 + 最终需求"的物质平衡解算，配给比恒为 1.0000；
	// 统一等级后，产业链各部分不再按需求比例匹配，配给比必然偏离 1。
	// 这【不是缺陷】而是契约选择：统一的起始等级让"后续扩建把结构推向均衡"
	// 成为可观察过程，代价是开局存在结构性短缺或过剩。
	//
	// 因此本测试不再断言配给比 = 1，改为【测量并报告】偏离幅度。
	var worst float64
	var worstGood string
	for g := range st.Goods {
		dev := math.Abs(plan.AllocRatio[g] - 1)
		if dev > worst {
			worst, worstGood = dev, st.Goods[g].Name
		}
	}
	fmt.Printf("\n[布点测量] 统一 %g 级下的配给比偏离：最大 %.4f（%s）\n",
		st.Params.ProductionInitLevel, worst, worstGood)
	if worst > 0.5 {
		t.Logf("提示：%s 的配给比偏离 %.4f，开局存在明显结构性短缺/过剩；"+
			"若需要匹配的布点，用命令行 -init-level 0（= Params.ProductionInitLevel 0）回退到物质平衡解算",
			worstGood, worst)
	}
}

// TestAuditConsumptionShortfall 诊断消费受限的原因。
//
// 判定规则：
//
//	PoolSpend ≈ PoolTargetValue < PoolBudget     ⇒ 供给不足（缺货）
//	PoolSpend ≈ PoolBudget     < PoolTargetValue ⇒ 预算不足（缺钱）
//
// 【重要】预算必须在 Step【之后】取——池里的现金是本 tick 工资划入的，
// Step 之前还是 0（居民没有初始货币，见 §4.5.3 的货币守恒要求）。
func TestAuditConsumptionShortfall(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	budgets := st.Houses.Budgets(st.Params.TaxRate)
	o := st.Last

	fmt.Printf("\n工资总额 = %.0f   消费税前支出 = %.0f   支出/工资 = %.1f%%\n",
		snap.Flow.WageTotal, o.SpendNet, o.SpendNet/snap.Flow.WageTotal*100)
	fmt.Printf("三表联合标定系数 k = %.4f\n", st.demandScale)
	fmt.Printf("各消费组目标量: ")
	for g := 0; g < 4; g++ {
		fmt.Printf("组%d=%.1f ", g, o.Target[g])
	}
	fmt.Printf("\n按人口加权的满足度: ")
	for g := 0; g < 4; g++ {
		fmt.Printf("组%d=%.3f ", g, o.Sat[g])
	}
	fmt.Printf("\n缺货池数=%d 缺钱池数=%d\n\n", o.Constrained, o.Starved)

	fmt.Printf("%-4s %-8s %10s %14s %14s %12s %6s\n",
		"池", "财富档", "人口", "池现金", "预算(税前)", "实际支出", "受限")
	for i := range st.Houses.Pools {
		p := &st.Houses.Pools[i]
		if p.Population <= 1e-9 {
			continue
		}
		reason := "—"
		if o.PoolSpend[i] < o.PoolTargetValue[i]*0.999 {
			if o.PoolSpend[i] >= o.PoolBudget[i]*0.999 {
				reason = "缺钱"
			} else {
				reason = "缺货"
			}
		}
		fmt.Printf("%-4d %-8.1f %10.0f %14.0f %14.0f %12.0f %6s\n",
			i, st.Houses.WealthTier(i), p.Population,
			p.Cash(), budgets[i], o.PoolSpend[i], reason)
	}

	// 三个人群等级必须各落在不同财富档——这是 §5.1 的口径。
	seen := map[float64]bool{}
	for i := range st.Houses.Pools {
		if st.Houses.Pools[i].Population > 1e-9 {
			seen[st.Houses.WealthTier(i)] = true
		}
	}
	if len(seen) < 3 {
		t.Errorf("人群池只落在 %d 个财富档（%v），应为 3 个（劳工 5 / 工程师 10 / 资本家 20）",
			len(seen), seen)
	}

	// 工资必须真的划入人群池：池现金总额 = 工资 + 福利金 − 本 tick 消费支出 − 消费税 − 储蓄。
	//
	// 【前值 → 后值（2026-09-19 第 15 轮）】§5.3 的储蓄渠道把消费结余转入投资池
	// （默认全部转入），故"池现金 = 工资 − 消费"不再成立；同时 §4.5.8 的福利金
	// 是居民的净收入。恒等式现为四项。
	wantLeft := snap.Flow.WageTotal + snap.Welfare - o.SpendNet - snap.Flow.ConsumerTax - snap.Saving
	if got := st.Houses.TotalCash(); abs64(got-wantLeft) > 1e-3 {
		t.Errorf("人群池现金 %.2f，应为 工资%.2f + 福利金%.2f − 消费净%.2f − 消费税%.2f − 储蓄%.2f = %.2f",
			got, snap.Flow.WageTotal, snap.Welfare, o.SpendNet, snap.Flow.ConsumerTax, snap.Saving, wantLeft)
	}
}

// TestAuditScaleGap 量化"工资 ≫ 最终需求价值"的量级缺口。
//
// 这个比值就是三表联合标定系数 k 应当取的量级：工资必须等于需求价值。
// 若它显著偏离 1，说明【布点规模】与【需求量表】不在同一量级——
// 那正是"消费支出只有工资的一部分、居民被迫储蓄"的根因。
func TestAuditScaleGap(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	wage := snap.Flow.WageTotal
	spend := st.Last.SpendNet
	kShould := 0.0
	if spend > 0 {
		kShould = wage / spend
	}
	fmt.Printf("\n工资总额 = %.0f   消费支出(税前) = %.0f\n", wage, spend)
	fmt.Printf("支出/工资 = %.4f   当前 k = %.4f   应为 k = %.4f\n",
		spend/wage, st.demandScale, kShould)

	// 逐商品核对供需
	fmt.Printf("\n%-12s %12s %12s %10s\n", "商品", "净供给", "购买量", "购买/供给")
	for g := range st.Goods {
		sup := snap.Supply[g]
		if sup < 1e-9 {
			continue
		}
		fmt.Printf("%-12s %12.1f %12.1f %9.1f%%\n",
			st.Goods[g].Name, sup, st.Last.Bought[g], st.Last.Bought[g]/sup*100)
	}

	// 记录量级缺口，供契约修订参考。这不是失败断言，是量级证据。
	if kShould > 1.2 || (kShould > 0 && kShould < 0.8) {
		t.Logf("量级缺口：k 应为 %.4f 而当前为 %.4f（相差 %.2f 倍）——"+
			"布点规模与需求量表不在同一量级，需在契约层重标定",
			kShould, st.demandScale, kShould/st.demandScale)
	}
	_ = model.Goods
}

func abs64(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}

// TestAuditBuildingRecon 校验建筑现金池的逐项对账残差恒为 0。
//
// 建筑池的 Δ 必须能被下列各项完全解释：
//
//	消费者收入 + 中间投入收入 + 建造力收入(净) + 经营留存 + 新资本
//	− 工资 − 中间投入付款(含税) − 建造力支出(净)
//
// 【本测试原先只打印、不断言】，于是长期掩盖了一个真缺陷：
// 逐建筑对账的 ConsumerIn 与 InputIn 两个字段从未被填充
// （distributeConsumerRevenue 是死代码，InputIn 根本没有赋值处），
// 实测残差 213 万——而全局的货币守恒与借贷相等审计全部通过。
// 这正是"全局不变量全绿"会掩盖"明细口径分叉"的典型情形。
//
// 修复：book.Consume / book.PayIntermediate 返回它们【实际贷记】的逐卖方金额，
// step 只登记这份记账事实，不再自行重算分摊。
func TestAuditBuildingRecon(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	var before float64
	for i := range st.Buildings {
		before += st.bal(i)
	}
	if _, err := st.Step(); err != nil {
		t.Fatalf("Step: %v", err)
	}
	var after float64
	for i := range st.Buildings {
		after += st.bal(i)
	}
	r := st.Recon
	fmt.Printf("\n建筑池对账\n")
	fmt.Printf("  实际Δ            = %14.0f\n", after-before)
	fmt.Printf("  消费者收入        = %14.0f\n", r.ConsumerRevenue)
	fmt.Printf("  中间投入收入(净)  = %14.0f\n", r.IntermediateIn)
	fmt.Printf("  建造力收入(净)    = %14.0f\n", r.PowerRevenue)
	fmt.Printf("  经营留存          = %14.0f\n", r.RetainedProfit)
	fmt.Printf("  金融区净额        = %14.0f\n", r.FinanceProfit)
	fmt.Printf("  新资本            = %14.0f\n", r.NewCapital)
	fmt.Printf("  −工资             = %14.0f\n", -r.WagePaid)
	fmt.Printf("  −中间投入付款     = %14.0f\n", -r.IntermediateOut)
	fmt.Printf("  −建造力支出(净)   = %14.0f\n", -r.PowerSpend)
	fmt.Printf("  应有Δ            = %14.0f\n", r.ExpectedDelta())
	aggResidual := (after - before) - r.ExpectedDelta()
	fmt.Printf("  残差              = %14.2f\n", aggResidual)

	// 汇总残差必须为 0：建筑池的 Δ 每一笔都必须能归到上列明细之一。
	if math.Abs(aggResidual) > 1e-6 {
		t.Errorf("建筑池汇总对账残差 = %.6f，应为 0（有未记账的资金流）", aggResidual)
	}

	fmt.Printf("\n逐建筑（残差非零者 + 金融区）：\n")
	worstIdx, worstRes := -1, 0.0
	for i := range st.Recon.ByBuilding {
		br := st.Recon.ByBuilding[i]
		if math.Abs(br.Residual()) > math.Abs(worstRes) {
			worstIdx, worstRes = i, br.Residual()
		}
		if br.Residual() == 0 && i != 11 {
			continue
		}
		fmt.Printf("  %-12s 实际Δ=%12.0f 应有Δ=%12.0f 残差=%10.2f\n",
			st.Buildings[i].Spec.Name, br.Actual, br.Expected(), br.Residual())
		fmt.Printf("      留存=%.0f 工资=−%.0f 投入付款=−%.0f 投入收入=%.0f 消费收入=%.0f 力净=%.0f 新资本=%.0f\n",
			br.Retained, br.Wage, br.InputOut, br.InputIn, br.ConsumerIn, br.PowerNet, br.NewCapital)
	}
	// 逐建筑残差同样必须为 0——汇总为 0 不足以排除"两栋楼的误差互相抵消"。
	if worstIdx >= 0 && math.Abs(worstRes) > 1e-6 {
		t.Errorf("逐建筑对账残差最大者 %s = %.6f，应为 0；"+
			"若只有个别建筑非零，说明该建筑的某项收入/支出未被登记",
			st.Buildings[worstIdx].Spec.Name, worstRes)
	}
}
