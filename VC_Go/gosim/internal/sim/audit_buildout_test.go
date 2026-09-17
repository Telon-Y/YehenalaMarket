package sim

import (
	"fmt"
	"testing"
)

// TestAuditBuildoutCash 单独核对政府自建付款的现金口径：
// 政府池实际减少多少、建造力部门实际收到多少、税收登记多少。
func TestAuditBuildoutCash(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	for tick := 1; tick <= 2; tick++ {
		govBefore := st.balGov()
		powerBefore := st.bal(fiscal_PowerIdx())
		taxBefore := st.Gov.TaxCollected
		if _, err := st.Step(); err != nil {
			t.Fatalf("tick %d: %v", tick, err)
		}
		f := st.Flow
		fmt.Printf("\n── tick %d 自建付款口径 ──\n", tick)
		fmt.Printf("  flowGovBuildoutDelta      = %12.2f\n", st.flowGovBuildoutDelta)
		fmt.Printf("  GovBuildoutPaid(F.Low)    = %12.2f\n", f.GovBuildoutPaid)
		fmt.Printf("  自收税 flowGovBuildoutSelfTax = %12.2f\n", st.flowGovBuildoutSelfTax)
		fmt.Printf("  Gov.TaxCollected 增量     = %12.2f\n", st.Gov.TaxCollected-taxBefore)
		fmt.Printf("  建造力部门现金增量         = %12.2f\n", st.bal(fiscal_PowerIdx())-powerBefore)
		fmt.Printf("  政府池增量                = %12.2f\n", st.balGov()-govBefore)
		fmt.Printf("  采购支出 powerSpendActual = %12.2f\n", st.powerSpendActual)
	}
}

func fiscal_PowerIdx() int { return 10 }
