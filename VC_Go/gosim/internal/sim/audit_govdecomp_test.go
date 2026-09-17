package sim

import (
	"fmt"
	"testing"
)

// TestAuditGovDecomp 逐环节核对政府池的资金流分解，定位残差来源。
//
// 分解式（含"政府收自己的税"扣除）：
//
//	Δ政府 = (GovTax − GovSelfTax) + GovOperating + GovPowerRevenue
//	        − GovPowerSpend − GovBuildoutPaid
func TestAuditGovDecomp(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	for tick := 1; tick <= 3; tick++ {
		prev := st.balGov()
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", tick, err)
		}
		f := snap.Flow
		delta := st.balGov() - prev
		explained := (f.GovTax - f.GovSelfTax) + f.GovOperating - f.GovPowerSpend +
			f.GovPowerRevenue - f.GovBuildoutPaid
		fmt.Printf("\n── tick %d ──\n", tick)
		fmt.Printf("  Δ政府            = %14.2f\n", delta)
		fmt.Printf("  GovTax           = %14.2f\n", f.GovTax)
		fmt.Printf("  GovSelfTax       = %14.2f\n", f.GovSelfTax)
		fmt.Printf("  GovOperating     = %14.2f\n", f.GovOperating)
		fmt.Printf("  GovPowerRevenue  = %14.2f\n", f.GovPowerRevenue)
		fmt.Printf("  −GovPowerSpend   = %14.2f\n", -f.GovPowerSpend)
		fmt.Printf("  −GovBuildoutPaid = %14.2f\n", -f.GovBuildoutPaid)
		fmt.Printf("  已解释            = %14.2f\n", explained)
		fmt.Printf("  残差              = %14.2f\n", delta-explained)
		fmt.Printf("  税分项：消费%.2f 中间%.2f 采购%.2f 自建%.2f （合计%.2f，自收%.2f）\n",
			f.ConsumerTax, f.InputTax, f.PowerPurchaseTax,
			f.GovSelfTax-f.PowerPurchaseTax, f.GovTax, f.GovSelfTax)
	}
}
