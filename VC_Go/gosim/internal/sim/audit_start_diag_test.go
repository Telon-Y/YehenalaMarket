package sim

// audit_start_diag_test.go —— 起点活动度诊断
//
// 目的：把"为什么大多数部门在 t=1 就亏损"拆成可核对的四项：
//
//	A 生产能力   grossOutput（潜在产出）与净供给 NetSupply
//	B 名义需求   Target（§6.3 表 × 人口 × k）及其在 P_cost / P_init 下的价值
//	C 购买力     工资总额、税后可用额、各池预算
//	D 实际成交   outcome.Bought 与满足度
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditStartDiag -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/model"
	"yehenala/market/internal/produce"
)

func TestAuditStartDiag(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	fmt.Printf("\n人口 %.0f   需求缩放 k = %.4f   税率 t = %.2f\n",
		st.Population, st.demandScale, st.Params.TaxRate)
	fmt.Printf("起始建筑：生产 %d 种 × 5 级 + 建造部门 20 级 + 金融区 14 级\n\n", model.Goods-1)

	// ---- A 生产 ----
	plan := produce.Settle(st.buildingSpecs(), st.levels(), st.hireRates(), st.subsistence())
	var totGrossCost, totNetCost float64
	for g := 0; g < model.Goods; g++ {
		totGrossCost += plan.GrossOutput[g] * st.Goods[g].Pcost
		totNetCost += plan.NetSupply[g] * st.Goods[g].Pcost
	}

	// ---- B 名义需求（§6.3 表口径，与 consume 完全一致）----
	groups := model.ConsumeGroupSpecs()
	var target [4]float64
	for c := range model.CohortShares {
		d := model.DemandAt(model.CohortWages[c])
		sub := st.Population * model.CohortShares[c]
		for g := 0; g < 4; g++ {
			target[g] += d[g] * sub / 100000 * st.demandScale
		}
	}
	// 目标需求按使用价值权重落到具体商品
	byGood := make([]float64, model.Goods)
	for g := range groups {
		var tot float64
		for _, v := range groups[g].Uses {
			tot += v
		}
		if tot <= 0 {
			continue
		}
		for idx, v := range groups[g].Uses {
			byGood[idx] += target[g] * v / tot
		}
	}
	var targetCostVal, targetInitVal, netCostVal float64
	for g := 0; g < model.Goods; g++ {
		targetCostVal += byGood[g] * st.Goods[g].Pcost
		targetInitVal += byGood[g] * st.Goods[g].Pinit
		netCostVal += plan.NetSupply[g] * st.Goods[g].Pcost
	}

	// ---- C 购买力 ----
	wages := produce.WageBill(st.buildingSpecs(), st.levels(), st.hireRates())
	totalWage := produce.TotalWage(wages)
	avail := totalWage / (1 + st.Params.TaxRate)

	fmt.Printf("【A 生产（t=1 潜在产能，全部满编）】\n")
	fmt.Printf("  潜在产出价值(P_cost)   = %14.2f\n", totGrossCost)
	fmt.Printf("  中间投入价值(P_cost)   = %14.2f\n", totGrossCost-totNetCost)
	fmt.Printf("  净供给价值(P_cost)     = %14.2f\n", netCostVal)

	fmt.Printf("\n【B 名义需求（§6.3 表 × 人口 × k）】\n")
	fmt.Printf("  四组目标量             = 简朴衣物 %10.1f  基础食物 %10.1f  标准衣物 %10.1f  住宅 %10.1f\n",
		target[0], target[1], target[2], target[3])
	fmt.Printf("  折成商品后价值(P_cost) = %14.2f\n", targetCostVal)
	fmt.Printf("  折成商品后价值(P_init) = %14.2f\n", targetInitVal)

	fmt.Printf("\n【C 购买力】\n")
	fmt.Printf("  工资总额               = %14.2f\n", totalWage)
	fmt.Printf("  税后可购价值(÷1+t)     = %14.2f\n", avail)

	fmt.Printf("\n【比值：需求 ÷ 供给】\n")
	fmt.Printf("  名义需求(P_cost) / 净供给(P_cost)   = %8.4f\n", targetCostVal/netCostVal)
	fmt.Printf("  名义需求(P_init) / 净供给(P_cost)   = %8.4f\n", targetInitVal/netCostVal)
	fmt.Printf("  名义需求(P_init) / 税后购买力       = %8.4f\n", targetInitVal/avail)
	fmt.Printf("  净供给(P_cost)   / 税后购买力       = %8.4f\n", netCostVal/avail)

	fmt.Printf("\n【D 逐商品明细（P_cost 计价）】\n")
	fmt.Printf("%-10s %12s %12s %12s %10s %10s %9s\n",
		"商品", "潜在产出", "中间投入", "净供给", "需求", "需求×Pc", "净供给×Pc")
	for g := 0; g < model.Goods; g++ {
		fmt.Printf("%-10s %12.1f %12.1f %12.1f %10.1f %10.0f %9.0f\n",
			st.Goods[g].Name, plan.GrossOutput[g],
			plan.GrossOutput[g]-plan.NetSupply[g],
			plan.NetSupply[g], byGood[g],
			byGood[g]*st.Goods[g].Pcost, plan.NetSupply[g]*st.Goods[g].Pcost)
	}

	// ---- 前 3 tick 实际成交 ----
	// Snapshot 已带 Sat（四组满足度）、SpendNet（税前成交额）、WageBill、Supply、Demand。
	fmt.Printf("\n【前 3 tick 实际成交】\n")
	fmt.Printf("%5s %14s %14s %14s %9s %9s %9s %9s\n",
		"tick", "工资总额", "税后可用", "税前成交", "成交/可用", "简朴衣物", "基础食物", "住宅")
	for i := 0; i < 3; i++ {
		sn, err := st.Step()
		if err != nil {
			t.Fatal(err)
		}
		av := sn.WageBill / (1 + st.Params.TaxRate)
		fmt.Printf("%5d %14.0f %14.0f %14.0f %9.4f %9.3f %9.3f %9.3f\n",
			sn.Tick, sn.WageBill, av, sn.SpendNet, sn.SpendNet/av,
			sn.Sat[0], sn.Sat[1], sn.Sat[3])
	}
}
