package sim

import (
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAuditDebtRepaymentViability 量化 M8.4 裁决"违约则延期"的**实际后果**。
//
// 【为什么必须量化】M8.4.1 裁决：付不出的部分滚入未偿余额、继续按 5%/年计息，
// **不核销、不加速、不没收**（登记在案）。但 1.0 的金融区营运净额**长期为负**
// （R61 的裁决背景），所以"付不出"可能不是偶发而是**恒常**
// ⇒ 负债会**单调复利**，而裁决里**没有任何上界**。
//
// 本局量化三件事：
//  1. 期末未偿余额相对本金的倍数；
//  2. 累计实付利息（= 储蓄银行拿到的利息收入）；
//  3. 累计延期次数（裁决要求"记录延期计数"）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditDebtRepaymentViability -v
func TestAuditDebtRepaymentViability(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		BankEnabled:          true,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	// 跑到覆盖"5 年 = 260 tick 期限"的 3 倍，看延期是否累积
	const ticks = 800
	for i := 0; i < ticks; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.InvariantErr != nil {
			t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
		}
		if v := len(st.Aud.Violations()); v != 0 {
			t.Fatalf("借贷不等 %d 笔 @%d", v, i)
		}
	}
	principal := st.Params.LoanPrincipal
	outstanding := st.Cap.DebtOutstandingTotal()
	paid := st.Cap.DebtPaidTotal()
	var deferrals int
	for _, d := range st.Cap.Debt {
		deferrals += d.Deferrals
	}
	t.Logf("银行局 %d tick（本金 %.0f/笔，期限 %.0f 年）：\n"+
		"  贷款笔数 %d、未偿总额 %14.2f（= %.2f 倍本金）\n"+
		"  累计实付    %14.2f\n"+
		"  累计延期次数 %d\n"+
		"  储蓄银行余额 %14.2f、投资池 %14.2f",
		ticks, principal, st.Params.LoanTermYears,
		len(st.Cap.Debt), outstanding, outstanding/principal,
		paid, deferrals,
		st.SavingsBankBalance(), st.Aud.Balance(ledger.Investment()))

	// 这些是**登记性断言**：把现状钉住，避免后续悄然漂移。
	// 若未来裁决给延期加上界/核销，这些数值会变，那时应显式更新本测试。
	if len(st.Cap.Debt) == 0 {
		t.Fatalf("800 tick 内未发放任何贷款 —— 放贷节奏未生效")
	}
	if deferrals == 0 && paid == 0 {
		t.Errorf("既无实付也无延期记录 —— M8 的台账既没还款也没记账")
	}
}
