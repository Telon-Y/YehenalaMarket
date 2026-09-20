package sim

import (
	"yehenala/market/internal/model"
	"fmt"
	"testing"
)

// TestAuditPrivatizeCapacity 量出"买断全部可私有化建筑"需要的资金，
// 与资本池规模对比，判断机制是否可达。
func TestAuditPrivatizeCapacity(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	powerPrice := st.Market.Prices()[powerGoodIndex]
	var privatizable, govOwned float64
	fmt.Printf("\n建造力价格 = %.2f\n", powerPrice)
	fmt.Printf("%-12s %10s %14s %16s %6s\n",
		"建筑", "政府持股", "建造成本/级", "重置成本/级", "可私有")
	for i := range st.Buildings {
		b := &st.Buildings[i]
		unit := b.Spec.BuildCost * powerPrice
		fmt.Printf("%-12s %10.2f %14.0f %16.0f %6v\n",
			b.Spec.Name, b.GovLevel, b.Spec.BuildCost, unit, b.Spec.AllowPrivatize)
		if b.Spec.AllowPrivatize && !b.Spec.IsFinance {
			privatizable += b.GovLevel * unit
			govOwned += b.GovLevel * unit
		}
	}
	fmt.Printf("\n买断全部可私有化的政府持股需要 = %.0f\n", privatizable)
	fmt.Printf("资本池当前余额               = %.0f\n", st.balCap())
	fmt.Printf("二者之比                     = %.1f 倍\n", privatizable/st.balCap())

	// 资本池的累积速度：私有建筑纯利份额
	fmt.Printf("\n资本池累积来源：私有建筑的资本份额\n")
	fmt.Printf("（当前资本份额需跑若干 tick 观察）\n")

	m0 := st.balCap()
	// 逐 tick 观察资本池的收支构成
	fmt.Printf("\n%-6s %14s %14s %14s\n", "tick", "资本池", "私有纯利", "金融区工资")
	prev := m0
	for i := 1; i <= 12; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", i, err)
		}
		fmt.Printf("%-6d %14.0f %14.0f %14.0f   Δ=%+.0f\n",
			i, st.balCap(), snap.Flow.CapitalProfit,
			st.Buildings[model.FinanceIndex].LastProfit, st.balCap()-prev)
		prev = st.balCap()
	}
	if st.balCap() <= 0 {
		// 【测量结论，不是断言失败】
		//
		// 私有化机制本身工作正常（见 TestAuditTask1PrivatizeSwitch 与
		// TestAuditTask1PriceIsBuildCost）；但默认经济下资本池为负，
		// 机制在资金上不可达。根因是私有部门整体亏损，
		// 与需求侧的量级匹配问题同源——需在契约层裁决，不是实现缺陷。
		t.Logf("【测量结论】资本池为负（%.0f），私有化在默认参数下不可达。"+
			"根因：私有部门整体亏损（见上方逐 tick 分解）。", st.balCap())
	}
}
