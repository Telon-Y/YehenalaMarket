package sim

// audit_r40_test.go —— 契约 §4.5.6「仓库与消费代理」的验收测试（2026-09-19 第 16 轮）。
//
// 覆盖 R24 的七步计划：
//
//	① 仓库（第 14 类建筑）：国有、可建造（100 建造力/级）、1,000 人/级、10,000 单位额度/级
//	② 消费代理（第 15 类"建筑"）：虚构、零余额、不雇人
//	③ 贸易路径：生产者 →(增值税 ν) 仓库 →(消费税 τ) 买家
//	④ 生产者收入 = **入库净额**（按供给份额分摊），不再直接来自买家
//	⑤ 中间投入的归属缺口（R21）随之关闭：自给那一份（含中间投入那一路）归宅邸庄园
//	⑥ 政府按额度自动扩建仓库（贸易量 > 级数×额度 ⇒ 下达政府订单）
//	⑦ 两段税取代全过程税 t：增值税在入库环节、消费税在出库环节
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR40 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// TestAuditR40WarehouseAndAgentSpecs 校验两类新建筑的静态定义（① ②）。
func TestAuditR40WarehouseAndAgentSpecs(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	wh := &st.Buildings[model.WarehouseIndex]
	if !wh.Spec.IsWarehouse || wh.Spec.IsNonMarket() {
		t.Fatalf("第 %d 类建筑应为仓库，且不属于 IsNonMarket（它有货币流）", model.WarehouseIndex)
	}
	if wh.Spec.IsTradeNode() != true || wh.Spec.Produces() {
		t.Error("仓库应满足 IsTradeNode() 且 !Produces()（有货币流但没有配方）")
	}
	if wh.Spec.BuildCost != 100 {
		t.Errorf("仓库建造成本 = %.0f，契约 §4.5.6 定为 100 建造力/级", wh.Spec.BuildCost)
	}
	if wh.Spec.LaborPerLevel != 1000 {
		t.Errorf("仓库每级雇佣 = %.0f，契约 §4.5.6 定为 1,000 人/级", wh.Spec.LaborPerLevel)
	}
	if !wh.Spec.AllowSubsidy {
		t.Error("仓库应可接受政府补贴（§4.5.6：它是本版唯一被指定可补贴的国有贸易枢纽）")
	}
	if wh.Spec.Category != model.CatDevelopment {
		t.Errorf("仓库的类别 = %s，应为开发类（§3.2）", wh.Spec.Category)
	}
	if !wh.Spec.Produces() == false && wh.Spec.Recipe.Qty != 0 {
		t.Error("仓库不应有产出配方")
	}
	// 国有：开工布点后政府持股恒为 1。
	//
	// 【0 级时的口径】开局仓库是 **0 级**（InitialWarehouseLevel = 0，由额度规则自动扩建），
	// 此时 GovLevel = PrivLevel = 0；`govShare` 对"级数为 0"的场地回退到 GovInitialShare，
	// 故这里只断言"私人持股恒为 0"，"政府持股 = 1"由 TestAuditR40WarehouseQuotaExpansion
	// 在扩建完成后断言（那时级数 ≥ 1）。
	if wh.PrivLevel != 0 {
		t.Errorf("仓库私人持股 = %.6f，应恒为 0（国有）", wh.PrivLevel)
	}
	if wh.GovLevel+wh.PrivLevel != wh.Level {
		t.Errorf("仓库股权腿不完整：Gov %.2f + Priv %.2f ≠ Level %.2f", wh.GovLevel, wh.PrivLevel, wh.Level)
	}
	if wh.Spec.AllowPrivatize {
		t.Error("仓库不应允许私有化（它归国有）")
	}

	ag := &st.Buildings[model.AgentIndex]
	if !ag.Spec.IsAgent || ag.Spec.Produces() {
		t.Fatal("第 15 类建筑应为消费代理（虚构、无配方）")
	}
	if ag.Spec.LaborPerLevel != 0 {
		t.Errorf("消费代理每级雇佣 = %.0f，应为 0（不雇人）", ag.Spec.LaborPerLevel)
	}
	if ag.Spec.BuildCost != 0 {
		t.Errorf("消费代理建造成本 = %.0f，应为 0（不可建造）", ag.Spec.BuildCost)
	}
	if ag.Level != 0 {
		t.Errorf("消费代理级数 = %.2f，应恒为 0", ag.Level)
	}
	t.Logf("仓库：建造成本 100 建造力/级、1,000 人/级、额度 %.0f 单位/级、国有；消费代理：0 级、0 人、不可建造",
		st.Params.WarehouseQuotaPerLevel)
}

// TestAuditR40TradePathIdentity 校验贸易路径的记账恒等式（③ ④）。
//
// 逐条核对（全部取记账事实，不从公式反推）：
//
//	入库净额 V   ≡ 出库货值 ÷ (1+加价)          （入库 = 出库：仓库不持有商品库存）
//	增值税       ≡ ν·V
//	消费税       ≡ τ·出库货值
//	入库付款     ≡ V·(1+ν)
//	仓库纯利     ≡ 出库货值 − 入库付款 − 仓库工资
//	买家实付     ≡ 出库货值·(1+τ)
func TestAuditR40TradePathIdentity(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	var checked int
	for i := 0; i < 30; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		mk := st.Params.WarehouseMarkup
		nu := st.Params.VATRate
		tau := st.Params.ConsumeTaxRate

		outBase := snap.WarehouseIn // 出库货值（含加价、不含消费税）
		if outBase <= 0 {
			continue
		}
		// ① 入库净额 = 出库货值 / (1+加价)
		wantV := outBase / (1 + mk)
		if rel := math.Abs(snap.Flow.WarehouseOut/(1+nu)-wantV) / wantV; rel > 1e-9 {
			t.Errorf("tick %d：入库净额 = %.4f，应为 出库货值/(1+加价) = %.4f（相对差 %.2e）",
				snap.Tick, snap.Flow.WarehouseOut/(1+nu), wantV, rel)
		}
		// ② 两段税
		if rel := math.Abs(snap.VAT-nu*wantV) / math.Max(1, nu*wantV); rel > 1e-9 {
			t.Errorf("tick %d：增值税 = %.4f，应为 ν·V = %.4f", snap.Tick, snap.VAT, nu*wantV)
		}
		if rel := math.Abs(snap.ConsumeTax-tau*outBase) / math.Max(1, tau*outBase); rel > 1e-9 {
			t.Errorf("tick %d：消费税 = %.4f，应为 τ·出库货值 = %.4f", snap.Tick, snap.ConsumeTax, tau*outBase)
		}
		// ③ 入库付款 = V·(1+ν)
		if rel := math.Abs(snap.Flow.WarehouseOut-wantV*(1+nu)) / (wantV * (1 + nu)); rel > 1e-9 {
			t.Errorf("tick %d：入库付款 = %.4f，应为 V·(1+ν) = %.4f", snap.Tick, snap.Flow.WarehouseOut, wantV*(1+nu))
		}
		// ④ 仓库纯利 = 出库货值 − 入库付款 − 仓库工资
		//    （工资取快照里的 WarehouseWage：仓库可能在**纯利计算之后**完工升级，
		//      用 tick 末 Level 反推会差恰好一级的工资）
		wantProfit := outBase - snap.Flow.WarehouseOut - snap.WarehouseWage
		if math.Abs(snap.WarehouseProfit-wantProfit) > 1e-6*math.Max(1, math.Abs(wantProfit)) {
			t.Errorf("tick %d：仓库纯利 = %.4f，应为 %.4f（出库 %.4f − 入库 %.4f − 工资 %.4f）",
				snap.Tick, snap.WarehouseProfit, wantProfit, outBase, snap.Flow.WarehouseOut, snap.WarehouseWage)
		}
		// ⑤ 仓库毛利 = (加价 − 增值税) × V > 0（加价必须大于增值税，否则仓库结构性亏损）
		if gm := (mk - nu) * wantV; gm <= 0 {
			t.Errorf("tick %d：仓库毛利 = %.4f ≤ 0（加价 %.3f 应大于增值税 %.3f）", snap.Tick, gm, mk, nu)
		}
		checked++
	}
	if checked < 5 {
		t.Fatalf("只有 %d 个 tick 可以核对——断言被空真通过（交易量恒为 0？）", checked)
	}
	t.Logf("已核对 %d 个 tick：入库 = 出库/(1+加价)、增值税 = ν·V、消费税 = τ·出库、纯利 = 出库−入库−工资",
		checked)
}

// TestAuditR40ProducerRevenueComesFromDeposit 校验"生产者收入 = 入库净额"（④）。
//
//	生产建筑的 ConsumerIn / InputIn 恒为 0（它们不再直接面向买家收钱）
//	生产者收入合计 ≡ 入库净额（Recon.DepositNet）
//	逐建筑收入 = 其供给份额 × 该商品过库货值
func TestAuditR40ProducerRevenueComesFromDeposit(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	var sawDeposit bool
	for i := 0; i < 20; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step: %v", err)
		}
		var revenueSum float64
		for j := range st.Buildings {
			b := &st.Buildings[j]
			if !b.Spec.Produces() {
				continue
			}
			recon := st.Recon.ByBuilding[j]
			if math.Abs(recon.ConsumerIn) > 1e-9 || math.Abs(recon.InputIn) > 1e-9 {
				t.Errorf("tick %d：%s 仍有直接的消费者/中间投入货款（%.2f / %.2f）——"+
					"仓库落地后生产者应只从入库收款（§4.5.6）",
					st.Tick, b.Spec.Name, recon.ConsumerIn, recon.InputIn)
			}
			revenueSum += b.LastRevenue
		}
		// 建造力例外：它由政府采购直接付款（revenueSum 已含它，见 ⑥ 的 revenue[]）。
		// 宅邸庄园不是 Produces()（所有权载体），其入库收入需单独加上。
		powerRev := st.Buildings[powerGoodIndex].LastRevenue
		want := st.Recon.DepositNet + powerRev
		got := revenueSum + st.lastManorDeposit
		if rel := math.Abs(got-want) / math.Max(1, want); rel > 1e-9 {
			t.Errorf("tick %d：生产者收入合计 = %.4f，应为 入库净额 %.4f + 建造力政府采购 %.4f = %.4f",
				st.Tick, got, st.Recon.DepositNet, powerRev, want)
		}
		if st.Recon.DepositNet > 0 && st.lastManorDeposit > 0 {
			sawDeposit = true
		}
	}
	if !sawDeposit {
		t.Fatal("20 tick 内没有观察到入库净额与庄园收入——断言被空真通过")
	}
	t.Logf("生产者收入全部来自入库净额：DepositNet = %.0f，庄园份额 = %.0f",
		st.Recon.DepositNet, st.lastManorDeposit)
}

// TestAuditR40SupplyShareClosesR21Gap 校验 R21 的归属缺口已关闭（⑤）。
//
// R21 记录的缺口：中间投入那一路的货款仍全额记在"该商品的唯一专业生产者"账上，
// 自给农场（庄园）只分到消费者货款那一半。仓库落地后入库款覆盖**全部去向**
// （中间投入 + 最终消费），故庄园按供给份额分得的那一份自动包含中间投入。
//
// 断言（可独立复算）：庄园的入库额 **大于**"只划消费者货款"时的份额，
// 因为谷物→食品厂、织物→服装厂这些**中间投入**也过库并按供给份额分摊。
func TestAuditR40SupplyShareClosesR21Gap(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	var checked int
	var maxGap float64
	for i := 0; i < 30; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		subs := st.subsistence()
		prices := st.Market.Prices()
		// "只划消费者货款"的反事实份额：Σ_g（消费者购买的货值）× 自给份额。
		var consumerOnly float64
		for g, sub := range subs {
			if sub <= 0 || g >= model.Goods || g == powerGoodIndex {
				continue
			}
			prof := 0.0
			for j := range st.Buildings {
				b := &st.Buildings[j]
				if !b.Spec.Produces() || b.Spec.Recipe.Output != g {
					continue
				}
				prof += b.Level * b.Spec.Recipe.Qty * b.HireRate
			}
			prof -= sub
			if prof < 0 {
				prof = 0
			}
			d := prof + sub
			if d <= 0 {
				continue
			}
			consumerOnly += st.Last.Bought[g] * prices[g] * sub / d
		}
		if st.lastManorDeposit <= 0 {
			continue
		}
		if st.lastManorDeposit <= consumerOnly+1e-6 {
			t.Errorf("tick %d：庄园入库 %.2f ≤ 仅消费者口径的份额 %.2f——"+
				"中间投入那一路没有并入入库款（R21 缺口未关闭）",
				snap.Tick, st.lastManorDeposit, consumerOnly)
		}
		if gap := st.lastManorDeposit - consumerOnly; gap > maxGap {
			maxGap = gap
		}
		checked++
	}
	if checked < 5 {
		t.Fatalf("只有 %d 个 tick 可核对（庄园入库额为 0？）——断言被空真通过", checked)
	}
	t.Logf("已核对 %d 个 tick：庄园入库额比'仅消费者口径'多出最多 %.0f 元/tick"+
		"（多出部分即中间投入那一路的归属，R21 缺口由此关闭）", checked, maxGap)
}

// TestAuditR40WarehouseQuotaExpansion 校验政府的额度驱动自动扩建（⑥）。
//
// 开局仓库 0 级（额度 0）⇒ tick 1 的贸易量必然超过额度 ⇒ 政府下达扩建订单，
// 完工后仓库级数 ≥ 1，且额度足以覆盖贸易量。
func TestAuditR40WarehouseQuotaExpansion(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	if st.Params.InitialWarehouseLevel != 0 {
		t.Fatalf("本测试假定开局仓库为 0 级（Params.InitialWarehouseLevel = %.1f）", st.Params.InitialWarehouseLevel)
	}
	if st.Buildings[model.WarehouseIndex].Level != 0 {
		t.Fatalf("开局仓库级数 = %.2f，应为 0", st.Buildings[model.WarehouseIndex].Level)
	}
	var firstUnitsTick int64
	var totalSpend, totalUnits float64
	for i := 0; i < 120; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		if snap.WarehouseExpandUnits > 0 && firstUnitsTick == 0 {
			firstUnitsTick = snap.Tick
		}
		totalSpend += snap.WarehouseExpandSpend
		totalUnits += snap.WarehouseExpandUnits
	}
	lv := st.Buildings[model.WarehouseIndex].Level
	if lv < 1 {
		t.Fatalf("120 tick 后仓库级数 = %.2f，自动扩建没有发生（贸易量 %.1f > 额度 %.1f）",
			lv, st.tradeVolumeTick, st.warehouseQuotaTick)
	}
	if totalUnits <= 0 {
		t.Error("从未下达过仓库扩建订单（WarehouseExpandUnits 累计为 0）")
	}
	if totalSpend <= 0 {
		t.Error("仓库扩建没有花掉国库的钱（WarehouseExpandSpend 累计为 0）")
	}
	if st.tradeVolumeTick > lv*st.Params.WarehouseQuotaPerLevel+1e-6 {
		t.Errorf("末态贸易量 %.1f 仍超过额度 %.1f（级数 %.1f × 每级 %.0f）——扩建没有补足容量",
			st.tradeVolumeTick, lv*st.Params.WarehouseQuotaPerLevel, lv, st.Params.WarehouseQuotaPerLevel)
	}
	// 仓库的政府持股必须随扩建保持 100%（它是国有的）。
	if math.Abs(st.govShare(model.WarehouseIndex)-1) > 1e-9 {
		t.Errorf("扩建后仓库政府持股 = %.6f，应仍为 1（国有）", st.govShare(model.WarehouseIndex))
	}
	t.Logf("仓库自动扩建：首次下达订单 tick = %d，累计 %.1f 级 / %.0f 元；末态 %.1f 级（额度 %.0f ≥ 贸易量 %.0f）",
		firstUnitsTick, totalUnits, totalSpend, lv, lv*st.Params.WarehouseQuotaPerLevel, st.tradeVolumeTick)
}

// TestAuditR40AgentBalanceIsZero 校验消费代理恒为零余额（②）。
func TestAuditR40AgentBalanceIsZero(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	var maxAbs float64
	for i := 0; i < 200; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		maxAbs = math.Max(maxAbs, math.Abs(snap.AgentCash))
		if snap.AgentCash != 0 {
			t.Fatalf("tick %d：消费代理余额 = %.10f，必须恒为 0（它不留钱）", snap.Tick, snap.AgentCash)
		}
		// 代理的逐建筑对账残差也必须是 0（它只有"进多少、出多少"两条腿）。
		if r := st.Recon.ByBuilding[model.AgentIndex].Residual(); math.Abs(r) > 1e-6 {
			t.Fatalf("tick %d：消费代理对账残差 = %.6f，应为 0", snap.Tick, r)
		}
	}
	t.Logf("200 tick 内消费代理余额最大值 = %.10f（恒为 0）", maxAbs)
}

// TestAuditR40WarehouseKeepsReconClean 校验仓库与代理都进入逐建筑对账且残差为 0（⑦）。
func TestAuditR40WarehouseKeepsReconClean(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	for i := 0; i < 200; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		if r := st.Recon.Residual(); math.Abs(r) > 1e-6 {
			t.Fatalf("tick %d：建筑池汇总对账残差 = %.6f，应为 0", snap.Tick, r)
		}
		for _, idx := range []int{model.WarehouseIndex, model.AgentIndex} {
			if r := st.Recon.ByBuilding[idx].Residual(); math.Abs(r) > 1e-6 {
				t.Fatalf("tick %d：%s 对账残差 = %.6f，应为 0",
					snap.Tick, st.Buildings[idx].Spec.Name, r)
			}
		}
		if st.InvariantErr != nil {
			t.Fatalf("tick %d：记账不变量被破坏：%v", snap.Tick, st.InvariantErr)
		}
	}
	t.Log("200 tick：建筑池汇总残差、仓库残差、代理残差全部为 0；记账不变量无错")
}

// TestAuditR40PowerStaysOutsideWarehouse 校验建造力不经仓库（口径声明）。
//
// 建造力是政府按 §4.5.3 G2 / §4.5.8.1 直接采购的投资品，其买家只有政府，
// 故不走仓库（否则会在投资链上叠加 5% 加价 + 消费税）。判据落在**记账腿**上：
// 建造部门的收入只出现在 PowerNet（政府采购），三条件（消费/中间/入库）全为 0。
func TestAuditR40PowerStaysOutsideWarehouse(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)
	var checked int
	for i := 0; i < 30; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		r := st.Recon.ByBuilding[powerGoodIndex]
		if math.Abs(r.ConsumerIn) > 1e-9 || math.Abs(r.InputIn) > 1e-9 || math.Abs(r.DepositIn) > 1e-9 {
			t.Errorf("tick %d：建造部门出现了消费/中间/入库腿（%.2f / %.2f / %.2f）——"+
				"建造力不应经仓库，只应由政府采购（PowerNet）付款",
				snap.Tick, r.ConsumerIn, r.InputIn, r.DepositIn)
		}
		if snap.TradeVolume <= 0 {
			t.Errorf("tick %d：贸易量为 0（仓库没有处理任何商品）", snap.Tick)
		}
		checked++
	}
	if checked < 20 {
		t.Fatal("核对次数不足——断言被空真通过")
	}
	t.Logf("已核对 %d 个 tick：建造部门只有 PowerNet（政府采购），不经仓库；贸易量为正", checked)
}
