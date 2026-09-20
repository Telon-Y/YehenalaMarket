package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
	"yehenala/market/internal/model"
)

// TestAudit12FullStack 是 1.2 的**全栈集成验证**：把**全部 1.2 特性一起打开**跑一局，
// 断言模型仍然健康，并且每个特性都**确实在起作用**（不是被别的特性掩盖掉的死代码）。
//
// 【为什么需要它（而不是更多单特性测试）】到 R89 为止，1.2 的每个特性都有独立的
// 审计测试，但**没有一局**是把它们全部同时打开并检查"谁都还在工作"的。
// 单特性测试各自通过，并不能排除"只在孤立时有效、组合后被掩盖"。
// R79 已经证明组合是**数值健康**的；本测试进一步证明组合下**各特性仍然活跃**。
//
// 【覆盖的 1.2 特性】
//
//	M4.2  所有权重构         → 劳动力分红被派发到人群池
//	M8    借贷台账           → 储蓄银行放贷、投资池收到钱
//	M8.6  利息当期分配       → 结构已接（R73 证明当前参数下实付恒 0，故此处只验结构）
//	§1.2-5 央行/金矿         → 金矿产金、央行造币、货币总量增长
//	M7    工资竞标           → 在稀缺局生效；默认局惰性（裁决①）
//	M1    储蓄存量口径       → savingsStock/savingsAlloc 在跑
//	M3/M6 玩家投资接口       → AI 关闭时按玩家方向分配
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAudit12FullStack -v
func TestAudit12FullStack(t *testing.T) {
	auditEnabled(t)
	const ticks = 900
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		// 全部 1.2 特性
		BankEnabled:          true,
		LoanFromBalance:      true,
		OwnershipRestructure: true,
		WageBidEnabled:       true,
		CentralBankEnabled:   true,
		SavingsStockTrack:    true,
		// M3/M6：玩家接管方向（各半）
		InvestAIOff:      true,
		InvestManorShare: f64ptr(0.5),
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	money0 := st.TotalMoney()
	var (
		cumLaborDiv  float64 // 累计劳动力分红**实发**
		anyShort     bool    // 是否出现过缺员（M7 的前置）
		leadPaid     float64 // 累计储蓄银行放贷
		firstPaidDiv int64   = -1
	)
	for i := 0; i < ticks; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		// 一、每一 tick 都必须数值健康 —— 这是组合局的核心断言
		if st.InvariantErr != nil {
			t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
		}
		if v := len(st.Aud.Violations()); v != 0 {
			t.Fatalf("借贷不等 %d 笔 @%d", v, i)
		}
		m := st.TotalMoney()
		if math.IsNaN(m) || math.IsInf(m, 0) {
			t.Fatalf("货币总量非有限 @%d: %v", i, m)
		}
		if math.IsNaN(st.Population) || st.Population <= 0 {
			t.Fatalf("人口塌陷 @%d: %v", i, st.Population)
		}
		// 逐建筑：溢价非负、实际工资 ≥ 基准（M7 的口径）
		for j := range st.Buildings {
			if p := st.WagePremium(j); p < -1e-12 {
				t.Fatalf("溢价为负 %.6g（场地 %d @%d）", p, j, i)
			}
			if w, base := st.EffectiveWage(j), st.BaseWage(j); w < base-1e-12 {
				t.Fatalf("实际工资 %.6f < 基准 %.6f（场地 %d @%d）", w, base, j, i)
			}
		}
		cumLaborDiv += st.laborDividendPaidTick
		leadPaid += st.loanIssuedTick
		if st.laborDividendPaidTick > 0 && firstPaidDiv < 0 {
			firstPaidDiv = st.Tick
		}
		for j := range st.wageShortTick {
			if st.wageShortTick[j] {
				anyShort = true
			}
		}
	}

	// 二、货币守恒（A8）：ΔM == NewCapital + InfusionTotal
	delta := st.TotalMoney() - money0
	want := st.TickNewCapitalTotal() + st.InfusionTotal()
	if d := delta - want; d > 1e-3 || d < -1e-3 {
		t.Errorf("货币守恒差 %.6f（ΔM=%.2f，NewCapital+Infusion=%.2f）", d, delta, want)
	}

	t.Logf("%d tick 全栈局：\n"+
		"  人口 %.0f、货币总量 %.2f（初始 %.2f）\n"+
		"  投资池 %.2f、资本池 %.2f、央行池 %.2f、储蓄银行 %.2f\n"+
		"  累计劳动力分红（实发）%.2f、首笔派发 tick %d\n"+
		"  累计造币 %.2f、累计产金 %.2f、累计放贷 %.2f、未偿负债 %.2f\n"+
		"  储蓄存量：已积累 %.2f、已分配 %.2f、余额 %.2f\n"+
		"  累计私有化 %.4f 级、货币守恒差 %.3g",
		ticks, st.Population, st.TotalMoney(), money0,
		st.InvestmentPool(), st.balCap(),
		st.Aud.Balance(ledger.CentralBank()), st.SavingsBankBalance(),
		cumLaborDiv, firstPaidDiv,
		st.MintedTotal(), st.GoldProducedTotal(), leadPaid, st.Cap.DebtOutstandingTotal(),
		st.SavingsStockTotal(), st.SavingsAllocatedTotal(), st.SavingsStockBalance(),
		st.privatizeUnits, delta-want)

	// ── 三、各特性"确实在工作"的断言 ──
	//
	// ① M4.2：劳动力分红**必须被派发过**（否则 30 亿会沉淀，见 R75/R76）
	if cumLaborDiv == 0 {
		t.Errorf("累计劳动力分红（实发）= 0 —— M4.2 的派发腿在组合局里没有生效")
	}
	// ② §1.2-5：金矿产金、央行造币
	if st.GoldProducedTotal() <= 0 {
		t.Errorf("累计产金 = 0 —— 金矿在组合局里没有生产")
	}
	if st.MintedTotal() <= 0 {
		t.Errorf("累计造币 = 0 —— 央行在组合局里没有造币")
	}
	// ③ M8：储蓄银行放过贷（钱真正流到投资池）
	if leadPaid <= 0 {
		t.Errorf("累计放贷 = 0 —— M8 的放贷通道在组合局里没有生效")
	}
	// ④ M1：储蓄存量口径在跑，且"已分配 ≤ 已积累"成立
	if st.SavingsStockTotal() <= 0 {
		t.Errorf("储蓄已积累 = 0 —— M1 的存量口径没有写入")
	}
	if st.SavingsAllocatedTotal() > st.SavingsStockTotal()+1e-6 {
		t.Errorf("已分配 %.2f > 已积累 %.2f —— 违反 M1 的不变量",
			st.SavingsAllocatedTotal(), st.SavingsStockTotal())
	}
	// ⑤ M3/M6：玩家方向生效（各半）
	if math.Abs(st.budgetShareManor-0.5) > 1e-9 {
		t.Errorf("budgetShareManor = %.6f，应为玩家指定的 0.5 —— M3/M6 未生效",
			st.budgetShareManor)
	}
	// ⑥ 货币总量必须**增长**（有造币），但不得发散
	//
	// 【上界随裁决演进】R96 修好"重复订单"后金矿**真的开始扩张**（5→22.97 级）
	// ⇒ 造币率由 8.48% 升到 **22.90%/tick**、900 tick 货币增长 **369.6 倍**。
	//
	// 这是**预期的**：用户第 72 轮裁决明确接受"金矿扩张 = 通胀"，
	// 并给出应对（把工资除以通胀比例后套入需求表）。在该方案落地前，
	// 金矿扩张局必然是这种量级。
	//
	// 故上界由 100 倍放宽到 **10000 倍**——它现在只负责抓"发散"（NaN/Inf/指数爆炸），
	// 不再禁止"扩张带来的通胀"。**等工资平减落地后应重新收紧。**
	if st.TotalMoney() <= money0 {
		t.Errorf("货币总量未增长（%.2f → %.2f）—— 有造币却不增长，口径矛盾",
			money0, st.TotalMoney())
	}
	growth := st.TotalMoney() / math.Max(money0, 1)
	if growth > 10000 {
		t.Errorf("货币总量增长 %.1f 倍（> 10000 倍）—— 造币发散", growth)
	} else {
		t.Logf("货币总量增长 %.1f 倍（金矿扩张期的**预期**通胀；R96 上界 10000 倍）", growth)
	}
	// ⑦ 黄金不进商品市场：价格表仍是 11 维
	if n := len(st.Market.Prices()); n != model.Goods {
		t.Errorf("价格表长度 %d，应为 %d", n, model.Goods)
	}
	// ⑧ 记录 M7 在组合局里的状态（默认局**不应**缺员 ⇒ 竞标惰性，这是裁决①的正确后果）
	t.Logf("M7 状态：组合局曾出现缺员 = %v（默认参数下预期 false ⇒ 竞标按裁决①惰性）", anyShort)
}
