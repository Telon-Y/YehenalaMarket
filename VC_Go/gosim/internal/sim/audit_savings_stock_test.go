package sim

import (
	"math"
	"testing"
)

// TestAuditSavingsStockTrack 校验 1.2 M1 第 3 条的**存量**口径。
//
// 【M1 第 3 条原文】"**拆账户**：把『储蓄账户余额』与『已分配投资』拆成两个口径。"
//
// 【为什么 1.0 的流量口径不够】§5.3 只有 `savingTick`（本期结余）与
// `savingInvestTick`（本期转入）两个**每 tick 流量**；而"储蓄账户余额"是一个
// **存量**概念。机械储蓄账户（`ledger.Savings()`）按设计**每期归零**
// （σ_save = 1 ⇒ 第二步全额转出），故它**不表达**"居民累计攒了多少"。
//
// 【断言】
//  1. **默认关闭时两个存量恒为 0** —— 1.0 逐位不变；
//  2. 开启后 `savingsStock ≥ savingsAlloc ≥ 0`（"已分配 ≤ 已积累"恒成立）；
//  3. 开启后两个存量**确实在增长**（口径真的被写入）；
//  4. 存量之差 = 未分配余额，且 σ_save = 1 时它应当很小
//     （因为每期结余当期就被转出，只有最后一期的尾巴留在"余额"里）；
//  5. 不破坏货币守恒与借贷相等——**特别是存量不进账本**（否则会重复计量）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditSavingsStockTrack -v
func TestAuditSavingsStockTrack(t *testing.T) {
	auditEnabled(t)
	run := func(track bool, ticks int) *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			SavingsStockTrack:    track,
		})
		if err != nil {
			t.Fatalf("New(track=%v): %v", track, err)
		}
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
			// ② 不变量：已分配 ≤ 已积累（逐 tick 检查，不只查末态）
			if st.savingsAlloc > st.savingsStock+1e-6 {
				t.Fatalf("已分配 %.6f > 已积累 %.6f @tick %d —— "+
					"违反【已分配 ≤ 已积累】", st.savingsAlloc, st.savingsStock, i)
			}
			if st.savingsStock < -1e-6 || st.savingsAlloc < -1e-6 {
				t.Fatalf("存量出现负值 @tick %d：stock=%.6f alloc=%.6f",
					i, st.savingsStock, st.savingsAlloc)
			}
		}
		return st
	}
	const ticks = 600
	off := run(false, ticks)
	on := run(true, ticks)

	t.Logf("%d tick：\n"+
		"  关闭 : 已积累 %.2f、已分配 %.2f、余额 %.2f\n"+
		"  开启 : 已积累 %.2f、已分配 %.2f、余额 %.2f（= 最后一期的尾巴）",
		ticks,
		off.SavingsStockTotal(), off.SavingsAllocatedTotal(), off.SavingsStockBalance(),
		on.SavingsStockTotal(), on.SavingsAllocatedTotal(), on.SavingsStockBalance())

	// ① 默认关闭 ⇒ 恒为 0
	if off.SavingsStockTotal() != 0 || off.SavingsAllocatedTotal() != 0 {
		t.Errorf("默认关闭时存量非 0（%.6f / %.6f）—— 1.0 逐位不变被破坏",
			off.SavingsStockTotal(), off.SavingsAllocatedTotal())
	}
	// ③ 开启后确实在增长
	if on.SavingsStockTotal() <= 0 {
		t.Fatalf("开启后已积累 %.6f ≤ 0 —— 存量口径没有被写入", on.SavingsStockTotal())
	}
	if on.SavingsAllocatedTotal() <= 0 {
		t.Fatalf("开启后已分配 %.6f ≤ 0", on.SavingsAllocatedTotal())
	}
	// ④ σ_save = 1 时"未分配余额"应只占极小比例
	//
	// 理由：每期结余（第一步）**当期**就被第二步转出（σ_save = 1），
	// 故累计差值只剩**最后一期**的转出量级，相对总量应 < 1%。
	if on.SavingsStockTotal() > 0 {
		frac := on.SavingsStockBalance() / on.SavingsStockTotal()
		if frac > 0.01 {
			t.Errorf("未分配余额占已积累的 %.4f%%（> 1%%）—— "+
				"σ_save=1 时结余应当期转出，余额只应是最后一期的尾巴", 100*frac)
		}
		t.Logf("未分配余额占比 = %.6f%%（σ_save=1 ⇒ 只应是最后一期尾巴）", 100*frac)
	}
	// ⑤ 存量**不进账本**：两个局的货币总量必须一致
	//
	// 这是最关键的一条——若把存量做成 ledger 账户，它会进 `Auditor.Total()`，
	// 而那些钱**已经**在投资池/储蓄银行里 ⇒ **重复计量**。
	if d := math.Abs(off.TotalMoney() - on.TotalMoney()); d > 1e-6*math.Max(1, math.Abs(off.TotalMoney())) {
		t.Errorf("开启存量口径改变了货币总量：%.6f vs %.6f（差 %.6g）—— "+
			"存量**不得**进账本（会重复计量）", off.TotalMoney(), on.TotalMoney(), d)
	}
	// 同时确认它**不改变任何实物流量**（纯诊断）
	if off.Population != on.Population || off.InvestmentPool() != on.InvestmentPool() {
		t.Errorf("开启存量口径改变了实物量（人口 %.4f→%.4f、投资池 %.2f→%.2f）—— "+
			"它应当是**纯诊断**", off.Population, on.Population,
			off.InvestmentPool(), on.InvestmentPool())
	}
}
