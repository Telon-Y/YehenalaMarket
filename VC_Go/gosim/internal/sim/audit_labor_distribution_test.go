package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAuditLaborDistribution 校验 1.2 的"统一入口 → 人群池"派发机制
// （M4.2 分红派发 + M8.6 ③ 利息当期分配）。
//
// 【裁决（2026-09-20 第 51 轮）】受益人集合 = "**统一：在职人口，失业者不参与**"。
//
// 【断言】
//  1. **默认关闭时分红池恒为 0**（1.0 基线保护）；
//  2. 开启重构后**分红池每 tick 归零**（派发腿真的在跑，钱不再沉淀）；
//  3. **失业池不参与派发**——这是裁决的直接后果，也是最容易写错的一条；
//  4. **各池按人头等分**——同一类内的池人均分红相同（M4.2"按人头平分"）；
//  5. 派发不破坏货币守恒与借贷相等；
//  6. 派发使重构**产生真实经济后果**（投资池显著上升），
//     证明 R75 的"账面重构"已变成"经济重构"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditLaborDistribution -v
func TestAuditLaborDistribution(t *testing.T) {
	auditEnabled(t)
	newSt := func(restructure bool) *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			OwnershipRestructure: restructure,
		})
		if err != nil {
			t.Fatalf("New(restructure=%v): %v", restructure, err)
		}
		return st
	}
	off := newSt(false)
	on := newSt(true)

	// 逐 tick 检查：分红池在**派发之后**必须归零。
	// 注意 `distributeLaborDividend` 在 ⑥-e（利润归属之后）执行，
	// 而 Step() 返回时整 tick 已跑完 ⇒ 检查点放在 Step 之后是正确口径。
	var maxResidual float64
	var anyPaid bool
	for i := 0; i < 400; i++ {
		for _, st := range []*State{off, on} {
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
		if r := math.Abs(on.Aud.Balance(ledger.LaborDividend())); r > maxResidual {
			maxResidual = r
		}
		if on.laborDividendPaidTick > 0 {
			anyPaid = true
		}
		// ① 关闭时恒为 0
		if v := off.Aud.Balance(ledger.LaborDividend()); v != 0 {
			t.Fatalf("未开重构时分红池 = %.6f ≠ 0 @tick %d（1.0 逐位不变被破坏）", v, i)
		}
	}
	t.Logf("400 tick：分红池最大残差 %.6f、曾发生派发 %v、投资池 关 %.2f / 开 %.2f、人口 关 %.0f / 开 %.0f",
		maxResidual, anyPaid, off.InvestmentPool(), on.InvestmentPool(), off.Population, on.Population)

	// ② 派发腿确实在跑，且每 tick 归零（钱不再沉淀）
	if !anyPaid {
		t.Fatalf("400 tick 内从未发生分红派发 —— 派发腿没有生效")
	}
	if maxResidual > 1e-6 {
		t.Errorf("分红池期末残差 %.6f > 1e-6 —— 派发未把池清零（钱仍在沉淀）", maxResidual)
	}

	// ③ 失业池不参与派发（裁决的直接后果）
	//
	// 【怎么验】给失业池注入一笔"只有它有钱"的场景不现实，改为**结构性检查**：
	// `laborLegs` 构造出的腿里不得出现失业池。用一个独立的小局直接调它。
	probe := newSt(true)
	if _, err := probe.Step(); err != nil {
		t.Fatalf("probe Step: %v", err)
	}
	legs := probe.laborLegs(1000)
	if len(legs) == 0 {
		t.Fatalf("laborLegs 返回空 —— 无法验证受益人集合")
	}
	for _, l := range legs {
		// 由账户反查 worksite：人群池下标 = worksite*classes + class
		ws := -1
		for i := range probe.Houses.Pools {
			if probe.Houses.Account(i) == l.Pool {
				ws = probe.Houses.Pools[i].Worksite
				break
			}
		}
		if ws == probe.UnemployedSite {
			t.Errorf("派发腿里出现了失业池（账户 %v）—— 与第 51 轮裁决【在职人口，失业者不参与】矛盾",
				l.Pool)
		}
		if ws < 0 {
			t.Errorf("无法把派发腿账户 %v 反查回人群池", l.Pool)
		}
	}
	// ④ 按人头等分：同一个 worksite 内各阶级的**人均**分红应相同
	sum := 0.0
	for _, l := range legs {
		sum += l.Amount
	}
	if d := math.Abs(sum - 1000); d > 1e-6 {
		t.Errorf("分红腿合计 %.6f ≠ 分配总额 1000（差 %.6g）", sum, d)
	}
	perCapita := map[float64]bool{}
	for _, l := range legs {
		var pop float64
		for i := range probe.Houses.Pools {
			if probe.Houses.Account(i) == l.Pool {
				pop = probe.Houses.Pools[i].Population
				break
			}
		}
		if pop <= 0 {
			continue
		}
		// 按人头等分 ⇒ 各腿金额 / 该池人口 必须**处处相同**
		perCapita[math.Round(l.Amount/pop*1e9)/1e9] = true
	}
	if len(perCapita) > 1 {
		t.Errorf("人均分红出现 %d 种不同值 —— 未按人头等分（M4.2 裁决）", len(perCapita))
	}

	// ⑤ 派发使重构产生真实经济后果（R75 的"账面重构"→"经济重构"）
	if on.InvestmentPool() <= off.InvestmentPool() {
		t.Errorf("开启重构后投资池 %.2f 未高于基线 %.2f —— "+
			"派发未把分红导入居民储蓄通道，重构仍是账面性的",
			on.InvestmentPool(), off.InvestmentPool())
	}
}
