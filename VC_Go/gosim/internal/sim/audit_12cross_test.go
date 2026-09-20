package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAudit12CrossFeature 校验 1.2 四个开关**互相耦合**时的行为。
//
// 【为什么要单独一局】此前的验证都是**逐特性孤立**的（M7 单独、M8 单独、M4.2 单独）。
// 1.2 的最终形态是**四个一起开**，而它们通过同一条资金链耦合：
//
//	M4.2 重构 → 劳动力分红 → 储蓄 → 储蓄银行 → 放贷 → 投资池
//	M8 银行   → 存款进银行、放贷进投资池
//	R71 收口  → 放贷额随银行余额（否则 R70 的 190 倍失衡把投资池抽干）
//	M7 竞标   → 稀缺时抬价 → 成本 → 利润率 → 扩建
//
// 【断言】
//  1. **任何组合都不破坏**不变量 / 借贷相等 / 货币有限性 / 人口不塌陷；
//  2. **M7 在默认（无稀缺）局里必须惰性**——裁决①的字面后果（R67 已确立）；
//  3. **M7 在稀缺局里必须有效**，且幅度**有界**（不超过投资池的 50%）；
//  4. **M7 绝不改变人口**——裁决④"无迁移成本"意味着人口不因竞标而移动；
//  5. R71 收口在稀缺局里也**必须**有效（否则 M8 单独开启会把投资池抽干）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAudit12CrossFeature -v
func TestAudit12CrossFeature(t *testing.T) {
	auditEnabled(t)
	type opts struct {
		pop             float64
		bank, loan, own bool
		bid             bool
	}
	type result struct {
		pop, invest, bank, capital, div float64
		maxPrem                         float64
	}
	run := func(name string, c opts, ticks int) result {
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
			t.Fatalf("%s: New: %v", name, err)
		}
		money0 := st.TotalMoney()
		r := result{}
		for i := 0; i < ticks; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", name, i, err)
			}
			// ① 任何组合都必须保持数值健康
			if st.InvariantErr != nil {
				t.Fatalf("%s: 不变量破坏 @%d: %v", name, i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("%s: 借贷不等 %d 笔 @%d", name, v, i)
			}
			m := st.TotalMoney()
			if math.IsNaN(m) || math.IsInf(m, 0) {
				t.Fatalf("%s: 货币总量非有限 @%d: %v", name, i, m)
			}
			if math.IsNaN(st.Population) || st.Population <= 0 {
				t.Fatalf("%s: 人口塌陷 @%d: %v", name, i, st.Population)
			}
			for j := range st.Buildings {
				if p := st.WagePremium(j); p > r.maxPrem {
					r.maxPrem = p
				}
			}
		}
		// 货币守恒（A8）
		delta := st.TotalMoney() - money0
		want := st.TickNewCapitalTotal() + st.InfusionTotal()
		if d := delta - want; d > 1e-3 || d < -1e-3 {
			t.Errorf("%s: 货币守恒差 %.6f", name, d)
		}
		r.pop, r.invest = st.Population, st.InvestmentPool()
		r.bank = st.SavingsBankBalance()
		r.capital = st.Aud.Balance(ledger.Capital())
		r.div = st.Aud.Balance(ledger.LaborDividend())
		return r
	}

	const ticks = 1200
	// 默认局（无稀缺）
	defBase := run("默认基线", opts{pop: 5_000_000}, ticks)
	defBid := run("默认+M7", opts{pop: 5_000_000, bid: true}, ticks)
	defAll := run("默认全开", opts{pop: 5_000_000, bank: true, loan: true, own: true, bid: true}, ticks)
	// 稀缺局（人口 50 万，R67 的对照口径）
	scBase := run("稀缺基线", opts{pop: 500_000}, ticks)
	scBid := run("稀缺+M7", opts{pop: 500_000, bid: true}, ticks)
	scNoLoan := run("稀缺+银行(无锚)", opts{pop: 500_000, bank: true}, ticks)
	scLoan := run("稀缺+银行+锚", opts{pop: 500_000, bank: true, loan: true}, ticks)
	scAll := run("稀缺全开", opts{pop: 500_000, bank: true, loan: true, own: true, bid: true}, ticks)

	t.Logf("默认局 %d tick：基线投资池 %.6g、+M7 %.6g、全开 %.6g",
		ticks, defBase.invest, defBid.invest, defAll.invest)
	t.Logf("稀缺局 %d tick：基线投资池 %.6g、+M7 %.6g、银行(无锚) %.6g、银行+锚 %.6g、全开 %.6g",
		ticks, scBase.invest, scBid.invest, scNoLoan.invest, scLoan.invest, scAll.invest)

	// ② 默认局里 M7 必须惰性（裁决①：只在缺员时抬价；默认局无稀缺 ⇒ 溢价恒 0）
	if defBid.maxPrem != 0 {
		t.Errorf("默认局（无稀缺）峰值溢价 %.6f ≠ 0 —— 与裁决①矛盾", defBid.maxPrem)
	}
	if defBid.invest != defBase.invest || defBid.pop != defBase.pop {
		t.Errorf("默认局里 M7 改变了结果（投资池 %.6g→%.6g、人口 %.4f→%.4f）—— "+
			"预期惰性（R67 已确立）", defBase.invest, defBid.invest, defBase.pop, defBid.pop)
	}
	// ③ 稀缺局里 M7 必须有效，且幅度有界
	if scBid.maxPrem <= 0 {
		t.Errorf("稀缺局里峰值溢价 %.6f ≤ 0 —— M7 未生效", scBid.maxPrem)
	}
	if scBid.invest == scBase.invest {
		t.Errorf("稀缺局里 M7 未改变投资池（%.6g）—— 抬价未传导到扩建", scBid.invest)
	}
	rel := math.Abs(scBid.invest-scBase.invest) / math.Max(scBase.invest, 1)
	if rel > 0.5 {
		t.Errorf("稀缺局里 M7 把投资池改变了 %.1f%%（> 50%%）—— 幅度失控", 100*rel)
	}
	// ④ M7 对人口只应有**间接的二阶影响**，不应有直接的人口移动
	//
	// 【R79 更正：第一版断言"人口必须逐位不变"是错的】
	// 裁决④说的是"**无迁移成本**"（配给按工资降序**瞬时完成**、不设迁移摩擦），
	// **不是**"人口不受影响"。M7 抬价 → 工资成本 → 利润率 → 扩建 → §6.5 的人口增长
	// 是一条**合法的间接通道**。实测差异 **7.72 人 / 592,434 = 0.0013%**，
	// 正是"间接二阶效应"的量级；断言"逐位为 0"会把正确的间接效应误判为 bug。
	//
	// 故改为断言**量级**：影响应 < 0.1%。若它变大，说明竞标直接挪动了人口
	// （那才是裁决④真正禁止的事），才应当报错。
	const popTol = 1e-3
	relPop := math.Abs(scBid.pop-scBase.pop) / math.Max(scBase.pop, 1)
	if relPop > popTol {
		t.Errorf("M7 改变人口 %.3f%%（%.4f → %.4f）—— 超出间接二阶效应的量级，"+
			"疑似竞标直接挪动了人口（裁决④禁止）",
			100*relPop, scBase.pop, scBid.pop)
	}
	t.Logf("M7 对人口的影响 = %.6g 人（%.4f%%）—— 间接二阶效应，裁决④允许",
		scBid.pop-scBase.pop, 100*relPop)
	// ⑤ R71 收口在稀缺局里必须有效：不开锚定会把投资池抽干（R70 的 190 倍失衡）
	if scNoLoan.invest >= scLoan.invest {
		t.Errorf("稀缺局里【银行+锚余额】的投资池 %.6g 未高于【仅银行】的 %.6g —— "+
			"R71 收口未生效", scLoan.invest, scNoLoan.invest)
	}
	if scNoLoan.invest < 0.01*scBase.invest {
		t.Logf("（对照）仅银行时投资池仅 %.4g = 基线的 %.2f%% —— R70 的抽干效应在稀缺局同样成立",
			scNoLoan.invest, 100*scNoLoan.invest/math.Max(scBase.invest, 1))
	}
}
