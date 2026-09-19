package sim

import (
	"fmt"
	"testing"
)

// TestAuditGovDecomp 逐环节核对政府池的资金流分解，定位残差来源。
//
// 【§4.5.3 第 11 轮改写后的分解式】建造力交易不计税、G6 改为投资池偿还，
// 故自反税腿与政府自有项目付款都不存在：
//
//	Δ政府 = GovTax + GovOperating + GovPowerRevenue(投资池偿还)
//	        − GovPowerSpend(G2 按需采购) − GovSubsidy (+ PrivatizePaid)
//
// 【前值 → 后值】旧式是
//
//	Δ政府 = (GovTax − GovSelfTax) + GovOperating + GovPowerRevenue
//	        − GovPowerSpend − GovBuildoutPaid − GovSubsidy
//
// 改动原因：GovSelfTax（政府自己收自己的税）与 GovBuildoutPaid（政府自建付款）
// 对应"整批采购 + 转售 + 政府自建"的旧口径，该口径已由 §4.5.3 删除。
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
		explained := f.GovTax + f.GovOperating - f.GovPowerSpend +
			f.GovPowerRevenue - f.GovSubsidy + f.PrivatizePaid
		fmt.Printf("\n── tick %d ──\n", tick)
		fmt.Printf("  Δ政府            = %14.2f\n", delta)
		fmt.Printf("  GovTax           = %14.2f\n", f.GovTax)
		fmt.Printf("  GovOperating     = %14.2f\n", f.GovOperating)
		fmt.Printf("  GovPowerSpend    = %14.2f（G2 按需采购，不计税）\n", f.GovPowerSpend)
		fmt.Printf("  GovPowerRevenue  = %14.2f（G6 投资池偿还）\n", f.GovPowerRevenue)
		fmt.Printf("  GovSubsidy       = %14.2f\n", f.GovSubsidy)
		fmt.Printf("  PrivatizePaid    = %14.2f\n", f.PrivatizePaid)
		fmt.Printf("  已解释            = %14.2f\n", explained)
		fmt.Printf("  残差              = %14.2f\n", delta-explained)
		fmt.Printf("  税分项：消费%.2f 中间%.2f （合计%.2f；建造力不计税 ⇒ 自反税腿 = 0）\n",
			f.ConsumerTax, f.InputTax, f.GovTax)
		fmt.Printf("  建造力：需要量%.2f 采购%.2f 投入%.2f 公共储备%.6f\n",
			f.PowerNeed, f.PowerBought, f.PowerSold, st.Gov.PowerInventory)
		fmt.Printf("  投资池：余额%.0f K_m=%.0f K_f=%.0f 本 tick 入池 %.0f/%.0f 偿还 %.0f\n",
			snap.InvestmentPool, snap.KManor, snap.KFinance,
			snap.InvestmentInflowManor, snap.InvestmentInflowFinance, snap.InvestmentPaid)
	}
}
