package sim

// 【R79 诊断】1.2 四个开关**全开**时，模型还健康吗？
//
// 此前的验证都是**逐特性孤立**做的（M7 单独、M8 单独、M4.2 单独）。
// 但 1.2 的最终形态是**四个一起开**，而它们会互相耦合：
//
//	OwnershipRestructure → 劳动力分红 → 储蓄 → 储蓄银行 → 放贷 → 投资池
//	BankEnabled          → 存款进银行、放贷进投资池
//	LoanFromBalance      → 放贷额随银行余额
//	WageBidEnabled       → 稀缺时抬价 → 成本 → 利润率 → 扩建
//
// 本探针把全开局与基线并排跑，检查：
//  1. 是否发生 invariant 破坏 / 借贷不等 / NaN / Inf；
//  2. 人口是否塌陷（= 模型不可运行）；
//  3. 各现金池与货币守恒是否仍然成立。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAll12FlagsProbe -v

import (
	"fmt"
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

func TestAll12FlagsProbe(t *testing.T) {
	auditEnabled(t)
	type cfg struct {
		name            string
		pop             float64
		bank, loan, own bool
		bid             bool
	}
	cases := []cfg{
		{"基线（全关）", 5_000_000, false, false, false, false},
		{"M8 银行", 5_000_000, true, false, false, false},
		{"M8+R71 锚余额", 5_000_000, true, true, false, false},
		{"M4.2 重构", 5_000_000, false, false, true, false},
		{"M4.2+银行+锚余额", 5_000_000, true, true, true, false},
		{"**全开**（四开关）", 5_000_000, true, true, true, true},
		// 【R79 关键对照】把人口压到 50 万（R67 的稀缺口径）再看 M7 是否还是惰性。
		// 若"全开"与"全开但不开 M7"在稀缺局里仍然逐位相同，则 M7 在**任何**
		// 现实参数下都惰性——那是对 M7 实用价值的重大结论。
		{"稀缺基线(50万)", 500_000, false, false, false, false},
		{"稀缺 M4.2+银行+锚", 500_000, true, true, true, false},
		{"稀缺**全开**", 500_000, true, true, true, true},
	}
	for _, c := range cases {
		st, err := New(Options{
			Population:           c.pop,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			BankEnabled:          c.bank,
			LoanFromBalance:      c.loan,
			OwnershipRestructure: c.own,
			WageBidEnabled:       c.bid,
		})
		if err != nil {
			t.Fatalf("%s: New: %v", c.name, err)
		}
		money0 := st.TotalMoney()
		var bad string
		for i := 0; i < 1200; i++ {
			if _, err := st.Step(); err != nil {
				bad = fmt.Sprintf("Step %d 报错: %v", i, err)
				break
			}
			if st.InvariantErr != nil {
				bad = fmt.Sprintf("不变量破坏 @%d: %v", i, st.InvariantErr)
				break
			}
			if v := len(st.Aud.Violations()); v != 0 {
				bad = fmt.Sprintf("借贷不等 %d 笔 @%d", v, i)
				break
			}
			if m := st.TotalMoney(); math.IsNaN(m) || math.IsInf(m, 0) {
				bad = fmt.Sprintf("货币总量非有限值 @%d: %v", i, m)
				break
			}
			if math.IsNaN(st.Population) || st.Population <= 0 {
				bad = fmt.Sprintf("人口塌陷 @%d: %v", i, st.Population)
				break
			}
		}
		delta := st.TotalMoney() - money0
		want := st.TickNewCapitalTotal() + st.InfusionTotal()
		cons := delta - want
		status := "OK"
		if bad != "" {
			status = "**" + bad + "**"
		}
		fmt.Printf("%-20s %-28s 人口 %9.0f 投资池 %16.2f 资本池 %17.2f 银行 %16.2f 分红池 %12.2f 守恒差 %+.3e\n",
			c.name, status, st.Population, st.InvestmentPool(),
			st.Aud.Balance(ledger.Capital()), st.SavingsBankBalance(),
			st.Aud.Balance(ledger.LaborDividend()), cons)
	}
}
