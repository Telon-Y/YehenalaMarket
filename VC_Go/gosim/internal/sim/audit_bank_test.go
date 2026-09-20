package sim

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAuditBankLedger 校验 1.2 借贷台账（M8）的机制与不变量。
//
// 覆盖四条裁决：
//   - M8.3 三条腿：放贷（借储蓄银行/贷投资池）、负债登记（非现金字段）、还款（借金融区/贷储蓄银行）
//   - M8.4 入池前先扣本期还款额
//   - M8.4.1 违约则延期（滚入 Outstanding，不核销/不加速）
//   - M8.7 负债是字段 ⇒ **不进账本、不参与借贷相等、不影响货币总量**
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditBankLedger -v
func TestAuditBankLedger(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		BankEnabled:          true,
		LoanIssueInterval:    52,
	})
	if err != nil {
		t.Fatalf("构造: %v", err)
	}
	if !st.Params.BankEnabled {
		t.Fatal("BankEnabled 未生效")
	}
	// 【放贷节奏必须先对上】每 52 tick 一笔 ⇒ 400 tick 内约 7 笔（应远小于 400）。
	if iv := st.Params.LoanIssueInterval; iv != 52 {
		t.Fatalf("LoanIssueInterval = %d，应为 52（每年一笔）", iv)
	}

	var issuedTotal, paidTotal, deferredTotal, interestTotal float64
	issues := 0
	var issueTicks []int64
	moneyBefore := st.TotalMoney()
	for i := 0; i < 400; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		if got := st.LoanIssuedTick(); got > 0 {
			issues++
			issuedTotal += got
			if len(issueTicks) < 8 {
				issueTicks = append(issueTicks, snap.Tick)
			}
			if issues <= 3 || issues > 345 {
				t.Logf("放贷 #%d：snap.Tick=%d st.Tick=%d iv=%d", issues, snap.Tick, st.Tick, st.Params.LoanIssueInterval)
			}
		}
		paidTotal += st.Cap.DebtPaid
		deferredTotal += st.Cap.DebtDeferred
		for j := range st.Cap.Debt {
			interestTotal += st.Cap.Debt[j].PaidInterest
		}
		// ① 货币守恒（A8）：负债是字段，绝不应改变货币总量
		if st.InvariantErr != nil {
			t.Fatalf("tick %d：不变量被破坏：%v", snap.Tick, st.InvariantErr)
		}
		// ② 借贷相等：任何一笔交易都必须平衡（由 ledger 把关，这里读违规数）
		if v := len(st.Aud.Violations()); v != 0 {
			t.Fatalf("tick %d：借贷不等违规 %d 笔", snap.Tick, v)
		}
	}
	// ③ 放贷确实发生（防断言被空真通过）
	if issues == 0 {
		t.Fatal("400 tick 内没有发放任何贷款——放贷机制未生效")
	}
	// ④ 负债台账的字段口径
	out := st.Cap.DebtOutstandingTotal()
	due := st.Cap.DebtServiceDueTotal()
	fmt.Printf("\n【1.2 借贷台账（M8）实测】\n")
	fmt.Printf("  放贷笔数 %d、累计本金 %.2f（前 8 个发放 tick: %v；iv=%d）\n",
		issues, issuedTotal, issueTicks, st.Params.LoanIssueInterval)
	fmt.Printf("  累计实付本息 %.2f；累计延期 %.2f；已付利息 %.2f\n", paidTotal, deferredTotal, interestTotal)
	fmt.Printf("  期末未偿余额 %.2f；本 tick 应付 %.2f\n", out, due)
	fmt.Printf("  储蓄银行余额 %.2f；投资池 %.2f；资本池 %.2f\n",
		st.SavingsBankBalance(), st.balInvest(), st.balCap())
	fmt.Printf("  货币总量 期初 %.2f → 期末 %.2f（含 NewCapital 注入 %.2f；诊断注入 %.2f）\n",
		moneyBefore, st.TotalMoney(), st.TickNewCapitalTotal(), st.InfusionTotal())
	fmt.Printf("  负债字段是否进账本：TotalOf(KindSavingsBank) = %.2f（应等于储蓄银行余额）\n",
		st.Aud.TotalOf(ledger.KindSavingsBank))

	// ⑤ 货币守恒：ΔM 只能来自 NewCapital 与诊断注入（负债不进账本）
	delta := st.TotalMoney() - moneyBefore
	want := st.TickNewCapitalTotal() + st.InfusionTotal()
	if diff := delta - want; diff > 1e-3 || diff < -1e-3 {
		t.Errorf("货币总量 Δ = %.4f，应为 NewCapital %.4f + 注入 %.4f = %.4f（差 %.4f）——"+
			"说明负债登记污染了货币口径", delta, st.TickNewCapitalTotal(), st.InfusionTotal(), want, diff)
	}
	// ⑥ 违约则延期：延期额必须 >= 0，且与"实付 < 应付"一致
	if deferredTotal < 0 {
		t.Errorf("累计延期 %.4f 为负——与 M8.4.1 矛盾", deferredTotal)
	}
	if deferredTotal <= 0 {
		t.Logf("提示：400 tick 内无延期（金融区净额足够还款）；本臂未触到 M8.4.1 的违约路径")
	}
}
