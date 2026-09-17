// cmd/diag_govcash —— 政府现金池与债务上限的诊断工具
//
// 用途：逐 tick 校验政府现金池的变化能否被已知资金流完全解释：
//
//	Δ现金 = 税收 + 经营净额 − 采购支出 + 售力收入 − 政府自建项目付款
//
// 若出现残差，说明有未记账的资金流出。本工程的三个历史 bug 都是这样定位的：
//  ① PowerPurchased 写成赋值而非累加（同 tick 第二次采购覆盖第一次记录）
//  ② 流量计数器未在 tick 开头清零（跨 tick 累加成存量）
//  ③ 政府经营净额被记两次（DistributeProfit 内部已入账，调用方又累加再入账）
//
// 【§4.5.3 修订后的口径变化】
// 自本次起，余额由唯一的审计账本持有（ledger.Auditor），政府现金池通过
// st.BalanceOf(政府) 读取；资金流分解式的各项也改为"账户实际变动"计量，
// 不再用公式推算。因此本工具报告的残差若仍非零，一定是真正的未知资金流。
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
		FinanceBuildCost:     400,
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
	fmt.Printf("全社会货币存量        = %.0f\n", st.Aud.Total())

	fmt.Printf("\n%5s %14s %12s %12s %13s %13s %12s %12s %14s\n",
		"tick", "Δ现金池", "税收", "经营净额", "采购支出", "售力收入", "自建付款", "残差", "货币存量")
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
			f.GovPowerRevenue - f.GovBuildoutPaid
		resid := delta - explained
		if i < 8 || i == 59 {
			fmt.Printf("%5d %14.0f %12.0f %12.0f %13.0f %13.0f %12.0f %12.2f %14.0f\n",
				st.Tick, delta, f.GovTax, f.GovOperating, -f.GovPowerSpend,
				f.GovPowerRevenue, -f.GovBuildoutPaid, resid, st.Aud.Total())
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
}
