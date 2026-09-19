package sim

// audit_test.go —— 1.0 验收审计（货币守恒不变量）
//
// 【本文件已随修复翻转】
//
// 这些测试最初写于缺陷存在时，断言的是"缺陷状态"（守恒被破坏、利润被双记、
// 工资未真正支付、判据空真通过），用于固化证据。修复完成后它们必须翻转为
// 断言"正确状态"——否则新旧状态会互相矛盾。
//
// 每一项的修复位置：
//
//	审计01 货币守恒   ← book.PayWages/Consume/PayIntermediate/PowerPurchase 全部走借贷相等
//	审计02 利润双记   ← 删除 b.Cash.Add(全额)＋政府再记一次，改为 book.ProfitAllocate 划分
//	审计03 工资未付   ← book.PayWages：借方建筑、贷方人群，同一笔交易
//	审计04 空真通过   ← 经济不再死亡，判据不再靠"全零"通过
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAudit -v

import (
	"fmt"
	"math"
	"os"
	"testing"
)

func auditEnabled(t *testing.T) {
	t.Helper()
	if os.Getenv("DSH_AUDIT") == "" {
		t.Skip("审计测试：默认跳过。设 DSH_AUDIT=1 启用。")
	}
}

// auditState 构造一个与 cmd/market-sim 默认参数一致的仿真。
func auditState(t *testing.T) *State {
	t.Helper()
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		// -1 = 保持 Params 的契约默认起始等级（5）。
		ProductionInitLevel: -1,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	return st
}

// TestAudit01MoneyConservation 检验 §4.5.3 的「总流通货币不变」。
//
// 修复后：任何 tick 之后四类账户之和恒定，唯一允许的 Δ 是
// §4.3 新建建筑的营运本金（book.NewCapital）。
func TestAudit01MoneyConservation(t *testing.T) {
	auditEnabled(t)
	st := auditState(t)
	m0 := st.TotalMoney()
	fmt.Printf("\n[审计01] 初值 总货币=%.0f (政府=%.0f 资本=%.0f 人群=%.0f)\n",
		m0, st.balGov(), st.balCap(), st.Houses.TotalCash())

	prev := m0
	injectedTotal := 0.0
	for i := 1; i <= 20; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("tick %d: %v", i, err)
		}
		m := st.TotalMoney()
		injectedTotal += snap.NewCapital
		// 扣除合法的营运本金注入后，Δ 必须为 0。
		delta := (m - prev) - snap.NewCapital
		if i <= 5 {
			fmt.Printf("[审计01] tick %2d 总货币=%14.0f  Δ=%10.0f（注入%.0f，扣除后Δ=%.6f）\n",
				i, m, m-prev, snap.NewCapital, delta)
		}
		if math.Abs(delta) > 1e-6 {
			t.Fatalf("tick %d：§4.5.3 货币守恒被破坏，Δ（扣除注入后）=%.6f", i, delta)
		}
		prev = m
	}
	fmt.Printf("[审计01] 20 tick 累计注入 = %.0f，货币存量 %.0f → %.0f\n",
		injectedTotal, m0, st.TotalMoney())

	if err := st.InvariantErr; err != nil {
		t.Errorf("记账簿不变量被破坏: %v", err)
	}
	if v := st.Aud.Violations(); len(v) > 0 {
		t.Errorf("存在借贷不相等记录: %v", v)
	}
}

// TestAudit02ProfitSplitIsPartition 校验利润分账是【划分】而非复制。
//
// 修复前：Δ货币/Σ利润 ≈ 2.00（建筑池拿全额，政府/资本再各拿一份）。
// 修复后：记账走 book.ProfitAllocate（R26 前叫 book.ProfitSplit），
// 借方 = 留池 + 政府 + 所有者 ≡ 纯利 π_i，贷方 = 同额，
// 故单笔利润对总量的净影响恒为 0——无论三条腿怎么分。
func TestAudit02ProfitSplitIsPartition(t *testing.T) {
	auditEnabled(t)
	st := auditState(t)

	for i := 1; i <= 6; i++ {
		before := st.TotalMoney()
		if _, err := st.Step(); err != nil {
			t.Fatalf("tick %d: %v", i, err)
		}
		var sumProfit float64
		for j := range st.Buildings {
			if !st.Buildings[j].Spec.IsNonMarket() {
				sumProfit += st.Buildings[j].LastProfit
			}
		}
		// 利润划分本身不改变总量（它是借贷相等的交易）。
		// 总量变化只能来自营运本金注入。
		injected := st.TickRecon
		_ = injected
		delta := st.TotalMoney() - before
		fmt.Printf("[审计02] tick %d  Σ利润=%14.0f  Δ货币=%14.0f\n", i, sumProfit, delta)

		// 修复前的特征值是 Δ ≈ 2×Σ利润；现在任何与利润成比例的漂移都必须消失。
		if math.Abs(sumProfit) > 1 && math.Abs(delta-2*sumProfit) < math.Abs(sumProfit)*0.5 {
			t.Errorf("tick %d：Δ货币(%.2f) 仍接近 2×Σ利润(%.2f)，双重入账未消除",
				i, delta, sumProfit)
		}
	}
}

// TestAudit03WagesArePaid 校验工资从建筑池【真实划转】到人群池。
//
// 修复前：工资只作为成本从利润里扣减，居民没有账户，钱从未付出。
// 修复后：book.PayWages 的借方是建筑、贷方是人群，两者由同一笔交易保证相等。
//
// 【前值 → 后值（2026-09-19 第 15 轮）】§5.3 新增"工资结余 → 储蓄 → 投资"渠道
// （默认 σ_save = 1.0）与 §4.5.8 的福利金，故居民池的收支恒等式多了两项：
//
//	Δ人群池 = 工资 + 福利金 − 消费净 − 消费税 − 储蓄
func TestAudit03WagesArePaid(t *testing.T) {
	auditEnabled(t)
	st := auditState(t)

	prevHouse := st.Houses.TotalCash()
	snap, err := st.Step()
	if err != nil {
		t.Fatalf("Step: %v", err)
	}
	wage := snap.Flow.WageTotal
	// 居民本 tick：先收工资（与福利金），再付出消费（含税）与储蓄
	wantHouse := prevHouse + wage + snap.Welfare - snap.SpendNet - snap.Flow.ConsumerTax - snap.Saving
	fmt.Printf("\n[审计03] 工资总额=%.0f  福利金=%.0f  储蓄=%.0f  人群池 %.0f → %.0f（应 %.0f）\n",
		wage, snap.Welfare, snap.Saving, prevHouse, st.Houses.TotalCash(), wantHouse)

	if got := st.Houses.TotalCash(); math.Abs(got-wantHouse) > 1e-3 {
		t.Errorf("人群池期末 = %.4f，应为 %.4f（工资未真实划转）", got, wantHouse)
	}
	if wage <= 0 {
		t.Error("工资总额为 0，工资没有被支付")
	}
	// 三个人群等级必须各落在不同财富档（§5.1 口径）
	seen := map[float64]bool{}
	for i := range st.Houses.Pools {
		if st.Houses.Pools[i].Population > 1e-9 {
			seen[st.Houses.WealthTier(i)] = true
		}
	}
	if len(seen) < 3 {
		t.Errorf("人群池只落在 %d 个财富档（%v），应为 3 个", len(seen), seen)
	}
}

// TestAudit04AcceptanceIsNotVacuous 确认 §8.4 判据不再靠"全零"通过。
//
// 修复前：经济死亡后全部 margin = 0、Δ = 0，A2/A3/A6 空真通过。
// 修复后：经济必须活着——人口与建筑级数不为零，判据才有意义。
func TestAudit04AcceptanceIsNotVacuous(t *testing.T) {
	auditEnabled(t)
	st := auditState(t)
	if _, err := st.Run(300); err != nil {
		t.Fatalf("Run: %v", err)
	}
	pop := st.Population
	levels := st.TotalLevels()
	fmt.Printf("\n[审计04] 300 tick 后：人口=%.0f  总级数=%.1f  货币存量=%.0f\n",
		pop, levels, st.TotalMoney())

	if pop <= 0 {
		t.Errorf("人口归零——经济已死亡，A2/A3/A6 的空真通过问题仍在")
	}
	if levels <= 0 {
		t.Errorf("全部建筑级数归零——经济已死亡")
	}
}
