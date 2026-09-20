package sim

// 【R75 诊断】M4.2 劳动力分红池的"沉淀"有多大？
//
// R74 实现了所有权重构：私人份额按 资本 0.30 / 劳动力 0.70 拆分，
// 劳动力那腿贷记 `ledger.LaborDividend()`。但**没有任何派发腿**
// ⇒ 钱停在那个账户里，劳动力拿不到、花不掉。
//
// 本探针量化这个沉淀的规模，并与"投资池"对比——R70/R73 已经证明
// "钱停在某个中介账户里"会让经济挨饿（储蓄银行曾把投资池抽干 99.58%）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestLaborDividendSinkProbe -v

import (
	"fmt"
	"testing"

	"yehenala/market/internal/ledger"
)

func TestLaborDividendSinkProbe(t *testing.T) {
	auditEnabled(t)
	mk := func(restructure, bank bool) *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			OwnershipRestructure: restructure,
			BankEnabled:          bank,
			LoanFromBalance:      bank,
		})
		if err != nil {
			t.Fatalf("New: %v", err)
		}
		return st
	}
	report := func(name string, st *State) {
		fmt.Printf("%-28s 人口 %9.0f 投资池 %18.2f 资本池 %18.2f 劳动力分红池 %18.2f 银行 %18.2f\n",
			name, st.Population, st.InvestmentPool(),
			st.Aud.Balance(ledger.Capital()),
			st.Aud.Balance(ledger.LaborDividend()),
			st.SavingsBankBalance())
	}
	a, b, c := mk(false, false), mk(true, false), mk(true, true)
	var minPool float64
	var privatizeA, privatizeB float64
	init := true
	for i := 0; i < 800; i++ {
		for _, st := range []*State{a, b, c} {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
		}
		// 累计私有化成交（§4.5.1a：`avail = BalCapital()` ⇒ 资本池越负越买不动）
		privatizeA += a.privatizeUnits
		privatizeB += b.privatizeUnits
		// 追踪分红池是否会出现负值（亏损期 Owner < 0）
		v := b.Aud.Balance(ledger.LaborDividend())
		if init || v < minPool {
			minPool, init = v, false
		}
	}
	fmt.Println("--- 800 tick 末态 ---")
	report("1.0 基线", a)
	report("M4.2 重构", b)
	report("M4.2 + 银行", c)
	fmt.Printf("\nM4.2 分红池最小值（800 tick 内）= %.2f\n", minPool)
	fmt.Printf("重构对投资池的影响：%.2f → %.2f（%+.2f）\n",
		a.InvestmentPool(), b.InvestmentPool(), b.InvestmentPool()-a.InvestmentPool())
	fmt.Printf("重构对人口的影响：%.0f → %.0f（%+.0f）\n",
		a.Population, b.Population, b.Population-a.Population)
	fmt.Printf("重构**真实的**经济后果——累计私有化等级：基线 %.4f → 重构 %.4f（%+.4f）\n",
		privatizeA, privatizeB, privatizeB-privatizeA)
}
