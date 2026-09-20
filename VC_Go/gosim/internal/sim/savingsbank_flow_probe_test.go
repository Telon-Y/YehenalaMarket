package sim

// 【R70 验证辅助探针】储蓄银行的"存/贷"失衡追踪。
//
// 实测（`TestAuditDebtRepaymentViability`，800 tick）：储蓄银行余额 **18.75 亿**，
// 但 800 tick 只放出 **15 笔 × 50 万 = 750 万** 贷款。
// 本探针逐 tick 记录"本期存入 / 本期放出 / 期末余额 / 未偿负债"，
// 以定位失衡是"放贷节奏太慢"还是"存入端有别的来源"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestSavingsBankFlowProbe -v

import (
	"fmt"
	"testing"
)

func TestSavingsBankFlowProbe(t *testing.T) {
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
	fmt.Printf("%5s %16s %14s %16s %16s %14s %12s\n",
		"tick", "本期存量(储蓄)", "本期放出", "期末银行余额", "未偿负债", "实付累计", "延期累计")
	for i := 0; i < 800; i++ {
		before := st.SavingsBankBalance()
		outBefore := st.Cap.DebtOutstandingTotal()
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		if st.Tick%52 != 0 && st.Tick != 1 && st.Tick != 10 && st.Tick != 100 {
			continue
		}
		after := st.SavingsBankBalance()
		outAfter := st.Cap.DebtOutstandingTotal()
		var defs int
		for _, d := range st.Cap.Debt {
			defs += d.Deferrals
		}
		fmt.Printf("%5d %16.2f %14.2f %16.2f %16.2f %14.2f %12d\n",
			st.Tick, after-before, outAfter-outBefore, after, outAfter,
			st.Cap.DebtPaidTotal(), defs)
	}
	fmt.Printf("\n期末：贷款笔数 %d、未偿 %.2f、储蓄银行 %.2f、投资池 %.2f\n",
		len(st.Cap.Debt), st.Cap.DebtOutstandingTotal(),
		st.SavingsBankBalance(), st.InvestmentPool())
	// 当期"劳动力储蓄"总量（对比银行存量）
	fmt.Printf("本期 savingTick=%.2f savingInvestTick=%.2f\n", st.savingTick, st.savingInvestTick)
}
