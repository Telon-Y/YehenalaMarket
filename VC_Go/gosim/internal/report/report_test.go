// Package report_test 校验 §8.4 的判据接入（2026-09-19 第 15 轮裁决，§0.4 第 6 项）。
//
// 用**外部测试包**（report_test）：report 依赖 sim，而审计需要同时构造 sim 状态，
// 若用 report 包内部测试会造成 import cycle。
package report_test

import (
	"testing"

	"yehenala/market/internal/model"
	"yehenala/market/internal/report"
	"yehenala/market/internal/sim"
)

// TestAssessWiresA1ToA8 校验：
//  1. Assess 现在判定 **9 条**判据（A1–A9），而不是 R35 时的 6 条；
//  2. A7/A8 已接入（第 15 轮），**A9（实际工农生产总值）**为第 18 轮新增；
//  3. A8（货币守恒）在干净跑中必须通过——它是构造性事实，不是运气。
func TestAssessWiresA1ToA8(t *testing.T) {
	st, err := sim.New(sim.Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("构造仿真失败: %v", err)
	}
	snaps, err := st.Run(250)
	if err != nil {
		t.Fatalf("Run: %v", err)
	}
	sum := report.Assess(snaps, model.GoodSpecs(), st.Params)
	if len(sum.Verdicts) != 9 {
		t.Fatalf("判据条数 = %d，应为 9（A1–A9；R35 时只有 6 条、R39 为 8 条）", len(sum.Verdicts))
	}
	seen := map[string]bool{}
	for _, v := range sum.Verdicts {
		seen[v.ID] = true
	}
	for _, id := range []string{"A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9"} {
		if !seen[id] {
			t.Errorf("判据 %s 未出现在 Assess 结果里", id)
		}
	}
	for _, v := range sum.Verdicts {
		if v.ID == "A8" && !v.Pass {
			t.Errorf("A8（货币守恒）未通过：%s —— 它是构造性事实，不应依赖参数", v.Detail)
		}
		if v.ID == "A7" || v.ID == "A9" {
			t.Logf("%s：%s", v.ID, v.Detail)
		}
	}
	t.Logf("判据汇总：通过 %d/9", sum.PassCount)
}
