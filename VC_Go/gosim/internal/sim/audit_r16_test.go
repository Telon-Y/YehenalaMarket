package sim

// audit_r16_test.go —— R16 两项度量缺陷的**现状特征化测试**。
//
// ============================ 前值 → 后值（2026-09-19 第 15 轮） ============================
//
// 本文件原为**特征化 tripwire**：断言"缺陷仍然存在"，一旦被修好就失败，
// 提醒同步更新 docs/ACTIVE.md §七 R16 与契约的"已知缺陷"注。
// 第 15 轮的裁决（§0.4 第 4、6 项）**正是修掉这两条**，故 tripwire 按设计触发，
// 现改写为对**修复后状态**的特征化断言（函数名同步由
// `TestAuditR16KnownDefectsAreStillPresent` 改为 `TestAuditR16DefectsAreAddressed`）：
//
//	缺陷 1（归属 + 利润率爆炸）
//	  前值：自给/专业满编产出倍数 谷物 79.92×、织物 44.40×、服装 9.99×；
//	        末 2,000 周期内最大 |margin| = **9.774e7%**（服装厂，tick 8317）。
//	  处置：R20 把自给产出的消费者货款按**供给份额**划给宅邸庄园（归属修复）；
//	        第 15 轮的储蓄渠道 + 政府支出端让经济真的扩建（专业农场满编产出上升）。
//	  后值：倍数降到 5.37×，最大 |margin| = 100%（有界，= 契约 A2 修订后的容忍上限）。
//
//	缺陷 2（A5 抓不到人口爆炸）
//	  前值：人口 10,000 tick 涨 11,704 倍（年化 +4.99%）而 A5 判"通过"。
//	  处置：§6.5 的零预算池不再记满足度 1（失业者无收入 ⇒ 无法消费 ⇒ 幸福度 0），
//	        并新增独立的**失业资金池**；§8.4 的 A5 增加"窗口 max/min ≤ 4"的有界性判定。
//	  后值：人口 10,000 tick 仅增 2.4 倍（年化 +0.45%），A5 的有界性判定生效。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR16 -v

import (
	"fmt"
	"math"
	"testing"
)

// TestAuditR16DefectsAreAddressed 校验 R16 两条缺陷已被处置（见文件头的前值→后值）。
func TestAuditR16DefectsAreAddressed(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	initPop := st.Population
	// 取与验收同长的 10,000 tick：R16 记录的 A2 `max=9.77e7%` 正是在该窗口末出现的。
	// 该窗口同时是"人口是否仍有界"的最强检验（原口径在此涨 11,704 倍）。
	const ticks = 10000
	snaps, err := st.Run(ticks)
	if err != nil {
		t.Fatalf("Run: %v", err)
	}

	prices := st.Market.Prices()
	subs := st.subsistence()

	fmt.Printf("\n[R16 现状] 自给农场产出 vs 专业农场满编产出（末 tick）\n")
	fmt.Printf("%-10s %14s %16s %10s %14s\n", "商品", "自给产出", "专业满编产出", "倍数", "自给影子收入")
	var maxRatio float64
	for good, vol := range subs {
		var pro float64
		for i, b := range st.buildingSpecs() {
			if b.IsNonMarket() || b.Recipe.Output != good {
				continue
			}
			pro += st.Buildings[i].Level * b.Recipe.Qty
		}
		ratio := math.Inf(1)
		if pro > 0 {
			ratio = vol / pro
			if ratio > maxRatio {
				maxRatio = ratio
			}
		}
		fmt.Printf("%-10s %14.1f %16.1f %10.2f %14.1f\n",
			st.Goods[good].Name, vol, pro, ratio, vol*prices[good])
	}

	// 缺陷 1a 的**诊断量**：自给产出 / 专业满编产出。
	//
	// 【口径说明】这个比值会随经济是否真的扩建而漂移（专业农场满编产出随等级增长）：
	// 实测在 5.4~20 之间浮动，而前值是稳定的 ~80（因为当时专业农场从不扩建）。
	// 它不再是可靠判据，故只作报告量；真正的判据是下面的"利润率不爆炸"。
	fmt.Printf("[R16 现状] 自给/专业满编产出最大倍数 = %.2f（前值 79.92；随扩建程度在 5~20 之间浮动，仅作报告）\n",
		maxRatio)

	// 缺陷 1b：满编利润率不再爆炸。
	//
	// 必须扫【判据 A2 的同一个窗口】（末 2,000 周期）——爆炸是窗口内的瞬时峰值。
	// 口径与 A2 一致：只看 11 种商品部门，上限取价格钳制带对应的合法区间
	// [−80%, +400%] 加松弛（4.5）。
	maxAbs, maxName := 0.0, ""
	var maxTick int64
	win := snaps[len(snaps)-2000:]
	for _, sn := range win {
		for i, m := range sn.Margins {
			if i >= len(st.Goods) {
				break
			}
			if math.Abs(m) > maxAbs {
				maxAbs, maxName, maxTick = math.Abs(m), st.Buildings[i].Spec.Name, sn.Tick
			}
		}
	}
	fmt.Printf("[R16 现状] 末 2,000 周期内最大 |margin| = %.4g%%（%s，tick %d）\n", maxAbs*100, maxName, maxTick)
	if maxAbs > 4.5 {
		t.Errorf("窗口内最大 |margin| = %.4g%%（前值 9.77e7%%）——超出价格钳制带对应的合法区间 ±400%%；"+
			"请复核 §3.1 的实际成本基口径与 fullCapacityCosts", maxAbs*100)
	}

	// 缺陷 2：人口不再爆炸（前值 10,000 tick 涨 11,704 倍、年化 +4.99%）。
	// 处置后由"失业者零收入 ⇒ 满足度 0"驱动，人口增速回落到 +0.5%/年量级。
	growth := st.Population / initPop
	years := float64(ticks) / float64(st.Params.TicksPerYear)
	annual := math.Pow(growth, 1/years) - 1
	fmt.Printf("[R16 现状] 人口 %.0f → %.0f（%.2f 倍 / %.0f 年，年化 %+.2f%%；前值 11,704 倍 / +4.99%%）\n",
		initPop, st.Population, growth, years, annual*100)
	fmt.Printf("            失业 %.0f 人；幸福度 %.4f（劳工 %.4f）\n",
		st.Unemployed, st.Last.Happiness(), st.Last.HappinessByClass(0))
	if growth >= 4 {
		t.Errorf("人口 10,000 tick 增长 %.1f 倍（年化 %+.2f%%）——有界性未生效；"+
			"请复核 §6.5 的零预算满足度口径与 §8.4 A5 的人口有界判定", growth, annual*100)
	}
}
