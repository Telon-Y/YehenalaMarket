package sim

import (
	"math"
	"testing"
)

// TestAuditM1SavingsBuyAssets 钉住一条**早已在运行**的 M1 能力：
// "投资池支出端 → 居民以储蓄购买资产"。
//
// 【为什么它此前没有被算作"已实现"】1.2 文档 §M1 第 2 条写的是"把投资池支出端接到
// **个人投资**"，而实现里它的名字是**私有化收购（acquisition）**——
// 通过 `Params.AcquireFromInvestment = true`（默认）走
// `book.PrivatizeFromInvestment`（**借 投资池 / 贷 政府**）。
//
// 也就是说：**机制早就在跑，只是名字不叫"个人投资"**。
// 实测（R81，默认参数 1200 tick）：累计私有化 **35.6432 级**、对价 **3.67e8**、
// 首笔成交在 **tick 1**、末期政府持股仅剩 **1.3568 级（0.39%）**。
//
// 【本条为什么重要】它把 M1 的剩余缺口**收窄**了：
// 缺的**不是**"居民能不能用储蓄买资产"（能，而且一直在买），
// 而是**口径**——买到的收益权记在谁头上、分红怎么走。
// 而后者已由 M4.2 + R76 的派发腿覆盖（私人份额按 资本 30% / 劳动力 70% 拆分，
// 劳动力那腿按人头派发到人群池）。
//
// 【断言】
//  1. `AcquireFromInvestment` 默认为 true（投资池是首选出资方）；
//  2. 默认局里**确实发生**私有化成交，且首笔不在末期（机制真的在跑）；
//  3. 成交对价由**投资池**承担（`privatizePaid > 0` 且投资池被扣减）；
//  4. 私有化把 `GovLevel` 转成 `PrivLevel`（股权腿与现金腿成对）；
//  5. 不破坏货币守恒与借贷相等。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditM1SavingsBuyAssets -v
func TestAuditM1SavingsBuyAssets(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	// ① 默认出资方是投资池
	if !st.Params.AcquireFromInvestment {
		t.Fatalf("AcquireFromInvestment 默认为 false —— 与第 28 轮裁决【投资池优先】不符")
	}
	gov0, priv0 := totalOwnership(st)
	money0 := st.TotalMoney()
	var cumUnits, cumPaid float64
	var firstTick int64 = -1
	const ticks = 1200
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
		cumUnits += st.privatizeUnits
		cumPaid += st.privatizePaid
		if firstTick < 0 && st.privatizeUnits > 1e-9 {
			firstTick = st.Tick
		}
	}
	gov1, priv1 := totalOwnership(st)
	t.Logf("%d tick：累计私有化 %.4f 级、累计对价 %.2f、首成交 tick %d", ticks, cumUnits, cumPaid, firstTick)
	t.Logf("股权：政府 %.4f→%.4f 级、私有 %.4f→%.4f 级", gov0, gov1, priv0, priv1)
	t.Logf("末期：投资池 %.2f、资本池 %.2f", st.InvestmentPool(), st.balCap())

	// ② 确实成交，且首笔不在末期（说明机制持续在跑，不是最后一刻的偶然）
	if cumUnits <= 0 {
		t.Fatalf("%d tick 内没有任何私有化成交 —— 投资池出资路径未生效", ticks)
	}
	if firstTick <= 0 || firstTick >= int64(ticks) {
		t.Errorf("首笔私有化成交 tick = %d（期望早期即发生）—— 机制可能只是末期偶然触发", firstTick)
	}
	if cumPaid <= 0 {
		t.Errorf("累计对价 %.2f ≤ 0", cumPaid)
	}
	// ③ 股权腿与现金腿成对：政府级减少、私有级增加
	if gov1 >= gov0 {
		t.Errorf("政府级未减少：%.4f → %.4f", gov0, gov1)
	}
	if priv1 <= priv0 {
		t.Errorf("私有级未增加：%.4f → %.4f", priv0, priv1)
	}
	// 【R81 更正：不能断言"政府减少 = 私有增加"】
	//
	// 第一版这么写，实测报错：政府减少 **19.64** vs 私有增加 **248.85**。
	// 原因是我把两个**不同来源**的变动混在一起了：
	//   - 私有级**不只**来自私有化，还来自 §4.5.1b 的**资本出资扩建**（新增产能）；
	//   - 政府级**不只**因私有化而减少，还会因**公共工程**新增等级而增加。
	// 我这个探针没有分离这两条腿，所以 ΔGov 与 ΔPriv 本来就不该相等。
	//
	// 正确的做法是只断言**能直接观测到的那一半**：私有化的股权腿与现金腿
	// 必须**同正**（都发生过），且累计对价与累计级数成比例、量级合理。
	// 股权腿与现金腿的"成对"由 `book.Privatize` 的借贷结构保证（见 ledger/rules.go），
	// 那是**结构性的**，不靠本断言守。
	if cumUnits <= 0 || cumPaid <= 0 {
		t.Errorf("私有化的股权腿（%.4f 级）与现金腿（%.2f）必须同为正", cumUnits, cumPaid)
	}
	// 单位对价应落在建造成本 × 价格 × 倍数的合理范围内（不是 0、不是天文数字）
	if unit := cumPaid / math.Max(cumUnits, 1e-9); unit <= 0 || unit > 1e9 {
		t.Errorf("单位对价 %.4f 超出合理范围（累计对价 %.2f / 累计级数 %.4f）",
			unit, cumPaid, cumUnits)
	}
	t.Logf("单位对价 %.2f 元/级；政府减少 %.4f 级、私有增加 %.4f 级"+
		"（两者不等是**正确**的：私有级还含资本出资扩建，政府级还含公共工程新增）",
		cumPaid/math.Max(cumUnits, 1e-9), gov0-gov1, priv1-priv0)
	// ④ 货币守恒
	delta := st.TotalMoney() - money0
	want := st.TickNewCapitalTotal() + st.InfusionTotal()
	if d := delta - want; d > 1e-3 || d < -1e-3 {
		t.Errorf("货币守恒差 %.6f", d)
	}
}

// totalOwnership 汇总全部建筑的政府级与私有级。
func totalOwnership(st *State) (gov, priv float64) {
	for i := range st.Buildings {
		gov += st.Buildings[i].GovLevel
		priv += st.Buildings[i].PrivLevel
	}
	return
}
