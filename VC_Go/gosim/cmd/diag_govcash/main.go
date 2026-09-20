// cmd/diag_govcash —— 政府现金池与债务上限的诊断工具
//
// 用途：逐 tick 校验政府现金池的变化能否被已知资金流完全解释：
//
//	Δ现金 = 税收 + 经营净额 − 按需采购支出 + 投资池偿还 − 政府补贴
//
// 若出现残差，说明有未记账的资金流出。本工程的历史 bug 都是这样定位的
// （详见 internal/sim/step.go 与 internal/fiscal 的注释）。
//
// 【§4.5.3 改写后的口径（2026-09-19，第 11 轮）】
// 建造力交易**不计税**，且 G6 由"整批采购 + 转售"改为"按需即买即用 +
// 投资池全额偿还"，故分解式里**没有** GovSelfTax / GovBuildoutPaid 两项——
// 政府不再是自己收自己的税，也没有政府自有项目。政府采购与投资池偿还在
// 本版口径下逐位相等（政府建造力净支出恒为 0）。
//
// 用法（在 gosim 目录下）：
//
//	go run ./cmd/diag_govcash
package main

import (
	"fmt"

	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/ledger"
	"yehenala/market/internal/sim"
)

func main() {
	st, err := sim.New(sim.Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
	})
	if err != nil {
		panic(err)
	}

	govBal := func() float64 { return st.Aud.Balance(ledger.Gov()) }
	pcost := st.Goods[fiscal.PowerGoodIndex].Pcost

	fmt.Printf("=== 初始状态（G7 债务机制）===\n")
	fmt.Printf("建造部门等级          = %.2f\n", st.Buildings[fiscal.PowerGoodIndex].Level)
	fmt.Printf("建造力产出(PowerOutput)= %.2f\n", st.Gov.PowerOutput)
	fmt.Printf("政府现金池            = %.0f\n", govBal())
	fmt.Printf("债务                  = %.0f\n", st.Gov.Debt())
	fmt.Printf("资产基数(P_cost=7250) = %.0f\n", st.Gov.AssetBase(pcost))
	fmt.Printf("债务上限              = %.0f\n", st.Gov.DebtCap(pcost))
	fmt.Printf("可动用资金            = %.0f\n", st.Gov.AvailableCash(pcost))
	fmt.Printf("投资池                = %.0f\n", st.Aud.Balance(ledger.Investment()))
	fmt.Printf("全社会货币存量        = %.0f\n", st.Aud.Total())

	fmt.Printf("\n%5s %14s %12s %12s %13s %13s %12s %12s %14s\n",
		"tick", "Δ现金池", "税收", "经营净额", "采购支出", "投资池偿还", "补贴", "残差", "货币存量")
	prev := govBal()
	m0 := st.Aud.Total()
	for i := 0; i < 60; i++ {
		if _, err := st.Step(); err != nil {
			panic(err)
		}
		cur := govBal()
		f := st.Flow
		delta := cur - prev
		explained := f.GovTax + f.GovOperating - f.GovPowerSpend +
			f.GovPowerRevenue - f.GovSubsidy + f.PrivatizePaid
		resid := delta - explained
		if i < 8 || i == 59 {
			fmt.Printf("%5d %14.0f %12.0f %12.0f %13.0f %13.0f %12.0f %12.2f %14.0f\n",
				st.Tick, delta, f.GovTax, f.GovOperating, -f.GovPowerSpend,
				f.GovPowerRevenue, -f.GovSubsidy, resid, st.Aud.Total())
		}
		prev = cur
	}
	fmt.Printf("\n货币守恒检验（§4.5.3）：初值 %.0f → 末值 %.0f，净变动 %.0f\n"+
		"（唯一允许的来源是新建营运本金，累计 %.0f）\n",
		m0, st.Aud.Total(), st.Aud.Total()-m0, st.TickNewCapitalTotal())
	if v := st.Aud.Violations(); len(v) > 0 {
		fmt.Printf("\n⚠ 借贷不相等记录 %d 条：\n", len(v))
		for _, s := range v {
			fmt.Printf("  %s\n", s)
		}
	} else {
		fmt.Printf("借贷校验：全部交易借贷相等（0 条违规）\n")
	}
	fmt.Printf("\n债务触限 tick 数 = %d / %d\n", st.Gov.DebtCapBoundTicks, st.Tick)
	fmt.Printf("总级数 = %.1f\n", st.TotalLevels())
	fmt.Printf("政府公共储备（应恒为 0）= %.6f\n", st.Gov.PowerInventory)
}
