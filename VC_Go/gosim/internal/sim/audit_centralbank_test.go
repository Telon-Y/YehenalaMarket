package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
	"yehenala/market/internal/model"
)

// TestAuditCentralBankMinting 校验 1.2 §1.2-5 的**购金造币**。
//
// 【断言】
//  1. 关闭时一切为 0（1.0 基线保护）；
//  2. 开启后金矿**真的产金**、央行**真的造币**；
//  3. **造币额锚到工资流量**（R86 的收口）：量级是"周工资的一小部分"，
//     而不是 R85 实测的"每 tick 印出货币存量的 58%"；
//  4. **二分之一分成**（§1.2-5 原文"其中一半…剩余部分进央行池"）：
//     金矿实收 ≈ 造币额的一半，央行留存 ≈ 另一半；
//  5. 造币是**注入**，计入 `InfusionTotal` —— 货币守恒（A8）不得报假残差；
//  6. 黄金**不进商品市场**：11 种商品的价格表长度不变。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditCentralBankMinting -v
func TestAuditCentralBankMinting(t *testing.T) {
	auditEnabled(t)
	run := func(on bool, ticks int) *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			CentralBankEnabled:   on,
		})
		if err != nil {
			t.Fatalf("New(on=%v): %v", on, err)
		}
		money0 := st.TotalMoney()
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
		// ⑤ 货币守恒：ΔM 必须等于"新建本金 + 全部注入"。
		// 造币若漏计 InfusionTotal，这里会报一个恰等于造币额的残差。
		delta := st.TotalMoney() - money0
		want := st.TickNewCapitalTotal() + st.InfusionTotal()
		if d := delta - want; d > 1e-3 || d < -1e-3 {
			t.Errorf("货币守恒差 %.6f（ΔM=%.2f，NewCapital+Infusion=%.2f；造币=%.2f）",
				d, delta, want, st.MintedTotal())
		}
		return st
	}
	const ticks = 600
	off := run(false, ticks)
	on := run(true, ticks)

	gi := on.goldMineIndex()
	if gi < 0 {
		t.Fatalf("开启央行后找不到金矿场地")
	}
	t.Logf("%d tick：\n"+
		"  关闭 : 产金 %.2f、造币 %.2f、央行池 %.2f\n"+
		"  开启 : 产金 %.2f、造币 %.2f、央行池 %.2f、金矿池 %.2f、货币总量 %.2f",
		ticks,
		off.GoldProducedTotal(), off.MintedTotal(), off.Aud.Balance(ledger.CentralBank()),
		on.GoldProducedTotal(), on.MintedTotal(), on.Aud.Balance(ledger.CentralBank()),
		on.bal(gi), on.TotalMoney())

	// ① 关闭 ⇒ 全为 0
	if off.GoldProducedTotal() != 0 || off.MintedTotal() != 0 {
		t.Errorf("关闭央行时产金 %.6f / 造币 %.6f 非 0", off.GoldProducedTotal(), off.MintedTotal())
	}
	if v := off.Aud.Balance(ledger.CentralBank()); v != 0 {
		t.Errorf("关闭央行时央行池 = %.6f ≠ 0", v)
	}
	// ② 开启 ⇒ 真的在产金与造币
	if on.GoldProducedTotal() <= 0 {
		t.Fatalf("开启后累计产金 %.6f ≤ 0", on.GoldProducedTotal())
	}
	if on.MintedTotal() <= 0 {
		t.Fatalf("开启后累计造币 %.6f ≤ 0", on.MintedTotal())
	}
	// ③ 量级：造币率相对**货币存量**的上界
	//
	// 【口径随裁决演进，这里记录完整链条，免得后人误判】
	//
	//	R85  固定 400,000/20 黄金        ⇒ **58%/tick**（灾难）
	//	R86  工资锚 f=0.005              ⇒  2.83%/tick
	//	R87  裁决 f=0.015（金矿扩张优先）⇒  8.48%/tick
	//	R96  修好"重复订单"⇒ 金矿**真的开始扩张**（5→22.97 级）⇒ **22.90%/tick**
	//
	// 【22.90% 是**预期的**，不是缺陷】用户第 72 轮裁决明确接受"金矿扩张 = 通胀"，
	// 并给出了应对方案：**把工资除以通胀比例后套入需求表**（用原始工资按原始价格
	// 购买消费品）。在该方案实现之前，金矿扩张局**必然**是高通胀的。
	//
	// 故上界放宽到 **40%**，仍低于 R85 的 58%：它现在的职责是**抓"发散"**
	// （NaN/Inf/指数爆炸），而不是禁止"金矿扩张带来的通胀"。
	// 等"工资平减"落地后，这个上界应当重新收紧——那是后续裁决的事。
	avgPerTick := on.MintedTotal() / float64(ticks)
	moneyStock := off.TotalMoney() // 用**关闭央行**的存量作分母（不受造币膨胀影响）
	rate := avgPerTick / math.Max(moneyStock, 1)
	if rate > 0.40 {
		t.Errorf("平均每 tick 造币 %.2f = 货币存量的 %.2f%%（上界 40%%；R85 的失败口径是 58%%）—— "+
			"造币发散", avgPerTick, 100*rate)
	}
	t.Logf("量级校验：平均每 tick 造币 %.2f = 货币存量的 %.3f%%"+
		"（R85 固定口径 58%% / R86 工资锚 2.83%% / R87 裁决 8.48%% / R96 金矿扩张后 22.90%%）",
		avgPerTick, 100*rate)
	// ④ 二分之一分成：金矿的**累计到账**应约等于造币额的一半
	//
	// 用"金矿池余额 + 它支付出去的投入与工资"不好算，故用**守恒式**：
	//	造币额 = 央行池余额 + 付给金矿的累计额
	// 而"付给金矿的累计额" = 造币额 − 央行池余额。断言央行池 ≈ 造币额的一半。
	bankBal := on.Aud.Balance(ledger.CentralBank())
	ratio := bankBal / math.Max(on.MintedTotal(), 1)
	if math.Abs(ratio-0.5) > 0.05 {
		t.Errorf("央行留存 / 造币额 = %.4f，应 ≈ 0.5（§1.2-5【其中一半…剩余部分进央行池】）", ratio)
	}
	t.Logf("分成校验：央行留存 %.2f / 造币额 %.2f = %.4f（应 ≈ 0.5）",
		bankBal, on.MintedTotal(), ratio)
	// ⑥ 黄金不进商品市场：价格表仍是 11 维
	if n := len(on.Market.Prices()); n != model.Goods {
		t.Errorf("开启央行后价格表长度 %d，应为 %d（黄金不进 A 矩阵）", n, model.Goods)
	}
	if n := len(on.Goods); n != model.Goods {
		t.Errorf("开启央行后商品表长度 %d，应为 %d", n, model.Goods)
	}
	// ⑦ 【§1.2-5 第三项】"黄金有剩余时央行自动扩建"
	//
	// 判据：产出 > 吞吐量（级数 × GoldPerBankLevel）⇒ 央行该扩到能吃下全部产出。
	// 实测：金矿 5 级产 125/τ，央行由起始 5 级扩到 **7 级**（吞吐量 140 ≥ 125）。
	//
	// 【为什么央行起始是 5 级】它在"统一起始等级"里被显式**跳过**
	//（见 New 的第 3b 步），但走的是"需求驱动布点"那条路径，故仍得到 5 级。
	// 关键不是起始值，而是**它会不会因为黄金有剩余而上调**。
	ci := on.centralBankIndex()
	if ci < 0 {
		t.Fatalf("开启央行后找不到央行场地")
	}
	bankLevel := on.Buildings[ci].Level
	capacity := bankLevel * on.Params.GoldPerBankLevel
	produced := on.goldPerTick()
	if capacity < produced*(1-1e-9) {
		t.Errorf("央行吞吐量 %.2f < 本期产出 %.2f —— 与§1.2-5【黄金有剩余则自动扩建】矛盾",
			capacity, produced)
	}
	t.Logf("央行扩建校验：级数 %.3f ⇒ 吞吐量 %.2f ≥ 本期产出 %.2f",
		bankLevel, capacity, produced)
	// 关闭央行时不得有任何扩建
	offCI := off.centralBankIndex()
	if offCI >= 0 {
		t.Errorf("关闭央行时仍存在央行场地（下标 %d）", offCI)
	}
}

// TestMineProfitabilityThreshold 定量给出"金矿要多大造币率才盈利"，
// 把这个**权衡**写成可执行的结论，而不是留在叙述里。
//
// 【为什么需要它】金矿的收入 = 造币额 / 2（§1.2-5 的二分之一分成），
// 而成本 = 每级工资 + 投入（煤 15 + 工具 15）。造币额由 `MintWageFraction` 决定，
// 于是"金矿是否盈利"完全由这个比例决定。用户需要知道**阈值在哪**。
func TestMineProfitabilityThreshold(t *testing.T) {
	auditEnabled(t)
	// 在若干 MintWageFraction 下跑，报告金矿的利润率
	fracs := []float64{0.005, 0.012, 0.015, 0.02, 0.03}
	for _, f := range fracs {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			CentralBankEnabled:   true,
			MintWageFraction:     f,
		})
		if err != nil {
			t.Fatalf("New(f=%g): %v", f, err)
		}
		for i := 0; i < 400; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
		}
		gi := st.goldMineIndex()
		b := &st.Buildings[gi]
		t.Logf("MintWageFraction=%.4f ⇒ 金矿收入 %12.2f 纯利 %12.2f 利润率 %+8.2f%% 等级 %.3f 造币 %.4g",
			f, b.LastRevenue, b.LastProfit, 100*b.LastProfit/math.Max(b.LastRevenue, 1),
			b.Level, st.MintedTotal())
	}
}
