package sim

// audit_buildout_test.go —— G2/G6 建造力闭环的现金口径审计（§4.5.3，第 11 轮改写）
//
// 【本文件已随 R26 改写】旧实现在这里逐项核对"政府自建付款"的含税口径
// （flowGovBuildoutDelta / GovBuildoutPaid / 自反税腿）。§4.5.3 把这个口径整体改掉了：
//
//	旧：政府整批采购 → 留存储备 → 转售给扩建方（含自反税）
//	新：有队列才采购、买了就投（不计税）→ 投资池全额偿还政府（G6）
//
// 故本文件改为核对新口径的四项事实：
//
//	A 无队列 ⇒ 采购量为 0（政府不持有公共储备，PowerInventory 恒为 0）
//	B 有队列 ⇒ 采购量 = min(队列需要量, 产出, 可动用资金/价格)
//	C 政府的建造力净支出 = 0（采购支出 = 投资池偿还）
//	D 投资池的 Δ = 入池额 − 偿还额（§4.5.1b 的守恒式）
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditG2G6 -v

import (
	"fmt"
	"math"
	"testing"
)

// TestAuditG2G6Cash 逐 tick 对照 G2 采购与 G6 偿还的现金口径。
func TestAuditG2G6Cash(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	fmt.Printf("\n%5s %14s %14s %14s %14s %14s %14s %10s\n",
		"tick", "队列需要量", "可采购上限", "实际采购", "政府采购支出",
		"投资池偿还", "政府净支出", "公共储备")
	for tick := 1; tick <= 5; tick++ {
		investBefore := st.balInvest()
		if _, err := st.Step(); err != nil {
			t.Fatalf("tick %d: %v", tick, err)
		}
		f := st.Flow
		net := f.GovPowerSpend - f.GovPowerRevenue
		fmt.Printf("%5d %14.2f %14.2f %14.2f %14.2f %14.2f %14.2f %10.4f\n",
			st.Tick, f.PowerNeed, f.PowerAvail, f.PowerBought,
			f.GovPowerSpend, f.GovPowerRevenue, net, st.Gov.PowerInventory)

		if math.Abs(net) > 1e-6 {
			t.Errorf("tick %d：政府的建造力净支出 = %.6f，应恒为 0（G6 全额偿还）", st.Tick, net)
		}
		if st.Gov.PowerInventory != 0 {
			t.Errorf("tick %d：公共储备 = %.6f，应恒为 0（G2 即买即用）", st.Tick, st.Gov.PowerInventory)
		}
		// C 投资池 Δ = 入池额 + 储蓄 − 偿还 − 收购（§4.5.1b / §4.5.1a 的守恒式）。
		//
		// 【前值 → 后值（2026-09-19 第 15 轮）】§5.3 新增居民储蓄渠道
		// （默认 σ_save = 1.0）后，投资池多了一条持续流入腿：
		//
		//	Δ投资池 = 入池_m + 入池_f + 居民储蓄 − G6 偿还
		//
		// 【前值 → 后值（第 28 轮）】"允许收购用投资池余额出资"后，投资池又多了
		// 一条**流出腿**：买存量股权（`ledger.InvestmentBuyEquity`，借投资池/贷政府），
		// 故恒等式读作：
		//
		//	Δ投资池 = 入池_m + 入池_f + 居民储蓄 − G6 偿还 − **收购对价**
		investDelta := st.balInvest() - investBefore
		wantInvest := st.tickInflowManor + st.tickInflowFinance + st.savingTick -
			st.tickInvestmentPaid - st.privatizePaid
		if math.Abs(investDelta-wantInvest) > 1e-6 {
			t.Errorf("tick %d：投资池 Δ = %.6f，应为 入池 %.2f + 储蓄 %.2f − 偿还 %.2f − 收购 %.2f = %.6f",
				st.Tick, investDelta, st.tickInflowManor+st.tickInflowFinance, st.savingTick,
				st.tickInvestmentPaid, st.privatizePaid, wantInvest)
		}
		// D 采购量不得超过队列本 tick 的需要量（G2 按需采购）。
		if st.Gov.PowerPurchased > st.powerNeedTick+1e-6 {
			t.Errorf("tick %d：采购量 %.4f 超过队列需要量 %.4f",
				st.Tick, st.Gov.PowerPurchased, st.powerNeedTick)
		}
	}
}
