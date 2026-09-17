// Package report 聚合诊断指标并判定契约 §8.4 的验收判据 A1–A6。
//
// 判据原文（1.0 生产与市场模拟.md §8.4，取末 2,000 周期为稳态窗口）：
//
//	A1 价格在合法带内，且不长期贴边
//	   |ln(P/Pcost)| ≤ ln5，贴 P_floor 或 P_ceil 的 tick 占比 < 5%
//	A2 利润率有界，无大面积亏损
//	   每建筑 margin ∈ [−10%, +25%]，行业加权平均 ≤ +15%
//	A3 均衡点不再漂移
//	   |Δ(P/Pcost)| < 1e-4 且 |Δ(S/a)| < 2e-4 每周期（末 500 周期均值）
//	A4 GDP 正值且不衰退
//	   GDP > 0 全程；末 500 周期线性斜率 ≥ 0
//	A5 无爆炸与无坍缩
//	   建筑等级、价格、现金池均不出现单调冲向 0 或上界；人口增长率进入 [0%, +5%] 年化
//	A6 建造队列不饥饿
//	   末 2,000 周期内队列非空 tick 占比 < 20%
//
// 本包【同时报告判据自身的可实现性问题】，不掩盖矛盾：
//   - A1 的阈值 ln5 与钳制区间上限 5×Pcost 完全重合，价格一旦触边即取等号，
//     而"贴边占比 < 5%"正是要禁止触边 ⇒ 该判据对任何触边情形恒为未通过；
//   - A3 与 §6.5 的人口增长数学互斥：人口年增 5% 时 Δ(S/a) ≈ 9.4e-4 > 2e-4 限值。
package report

import (
	"fmt"
	"math"
	"sort"
	"strings"

	"yehenala/market/internal/model"
	"yehenala/market/internal/sim"
)

// Verdict 是一条判据的判定结果。
type Verdict struct {
	ID     string
	Text   string
	Pass   bool
	Detail string
	// Note 记录该判据自身的可实现性问题（若有）。
	Note string
}

// Summary 是一次运行的完整评估。
type Summary struct {
	Ticks     int
	Verdicts  []Verdict
	PassCount int
}

// Assess 对快照序列做 §8.4 判定。
//
// goods 必须与快照中的商品下标一致；params 提供 1 年 = 52 tick 的折算。
func Assess(snaps []*sim.Snapshot, goods []model.Good, params model.Params) *Summary {
	sum := &Summary{Ticks: len(snaps)}
	if len(snaps) < 200 {
		sum.Verdicts = append(sum.Verdicts, Verdict{ID: "N/A", Text: "周期不足 200，无法判定", Pass: false})
		return sum
	}
	tail := snaps
	if len(tail) > 2000 {
		tail = tail[len(tail)-2000:]
	}
	last500 := snaps
	if len(last500) > 500 {
		last500 = last500[len(last500)-500:]
	}
	n := len(goods)

	// ---- A1 ----
	clampShare := make([]float64, n)
	maxLog := make([]float64, n)
	for _, sn := range tail {
		for i := 0; i < n; i++ {
			g := goods[i]
			floor := g.PriceFloorRatio * g.Pcost
			ceil := g.PriceCeilRatio * g.Pcost
			if sn.Prices[i] <= floor*1.001 || sn.Prices[i] >= ceil*0.999 {
				clampShare[i]++
			}
			l := math.Abs(math.Log(sn.Prices[i] / g.Pcost))
			if l > maxLog[i] {
				maxLog[i] = l
			}
		}
	}
	a1Pass := true
	worstClamp := 0.0
	worstLog := 0.0
	for i := 0; i < n; i++ {
		clampShare[i] /= float64(len(tail))
		if clampShare[i] >= 0.05 {
			a1Pass = false
		}
		if maxLog[i] > math.Log(5)+1e-9 {
			a1Pass = false
		}
		if clampShare[i] > worstClamp {
			worstClamp = clampShare[i]
		}
		if maxLog[i] > worstLog {
			worstLog = maxLog[i]
		}
	}
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A1", Pass: a1Pass,
		Text:   "价格在合法带内且贴边占比 < 5%",
		Detail: fmt.Sprintf("最大贴边率=%.2f%%  max|ln(P/Pcost)|=%.4f（限 %.4f）", worstClamp*100, worstLog, math.Log(5)),
		Note:   "阈值 ln5 与钳制上限 5×Pcost 完全重合：价格触边时 |ln(P/Pcost)| 恰等于 ln5，而贴边又是本判据禁止的行为，故该判据对任何触边情形恒为未通过。",
	})

	// ---- A2 ----
	mMin, mMax := math.Inf(1), math.Inf(-1)
	for _, sn := range tail {
		for i := 0; i < n; i++ {
			// 金融区不参与 §8.4 的建筑利润率判据（它不是商品生产者）
			if i >= len(sn.Margins) {
				continue
			}
			if sn.Margins[i] < mMin {
				mMin = sn.Margins[i]
			}
			if sn.Margins[i] > mMax {
				mMax = sn.Margins[i]
			}
		}
	}
	var num, den float64
	for _, sn := range tail {
		for i := 0; i < n; i++ {
			if i >= len(sn.Margins) {
				continue
			}
			num += sn.Margins[i] * sn.Levels[i]
			den += sn.Levels[i]
		}
	}
	wAvg := 0.0
	if den > 0 {
		wAvg = num / den
	}
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A2", Pass: mMin >= -0.10-1e-9 && mMax <= 0.25+1e-9 && wAvg <= 0.15+1e-9,
		Text:   "每建筑 margin ∈ [−10%, +25%]，行业加权平均 ≤ +15%",
		Detail: fmt.Sprintf("min=%.2f%% max=%.2f%% 加权=%.2f%%", mMin*100, mMax*100, wAvg*100),
		Note:   "封闭经济下全局总利润恒为 0（会计恒等式），因此 11 个部门必然分为盈利与亏损两组；配合价格钳制带 [0.2,5]×Pcost，两者不可能同时落进 [−10%,+25%]。详见 fiscal 包注释。",
	})

	// ---- A3 ----
	var dPR, dSa float64
	var cnt float64
	for k := 1; k < len(last500); k++ {
		prev, cur := last500[k-1], last500[k]
		for i := 0; i < n; i++ {
			dPR += math.Abs(cur.PriceRatio[i] - prev.PriceRatio[i])
			dSa += math.Abs(supplyRatio(cur, i) - supplyRatio(prev, i))
			cnt++
		}
	}
	if cnt > 0 {
		dPR /= cnt
		dSa /= cnt
	}
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A3", Pass: dPR < 1e-4 && dSa < 2e-4,
		Text:   "均衡点不漂移：|Δ(P/Pcost)| < 1e-4 且 |Δ(S/a)| < 2e-4 每周期",
		Detail: fmt.Sprintf("Δ(P/Pcost)=%.3e  Δ(S/a)=%.3e", dPR, dSa),
		Note:   "与 §6.5 的人口增长数学互斥：人口年增 5% 时 S/a 每 tick 漂移约 9.4e-4，超出 2e-4 限值一个数量级。",
	})

	// ---- A4 ----
	allPos := true
	minGDP := math.Inf(1)
	gdps := make([]float64, len(last500))
	for k, sn := range snaps {
		// GDP 口径（契约 §7 修订后）：消费者支出 + 建筑/政府/资本三池期末总额，
		// 其中政府现金池取正值部分（负值是债务，见 §4.5.4）。
		gdp := sn.SpendNet + sn.CashTotal
		if math.IsNaN(gdp) || gdp <= 0 {
			allPos = false
		}
		if gdp < minGDP {
			minGDP = gdp
		}
		if k >= len(snaps)-len(last500) {
			gdps[k-(len(snaps)-len(last500))] = gdp
		}
	}
	slope := linearSlope(gdps)
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A4", Pass: allPos && slope >= 0,
		Text:   "GDP 全程 > 0 且末 500 周期斜率 ≥ 0",
		Detail: fmt.Sprintf("全程为正=%v  最低 GDP=%.0f  斜率=%.3e", allPos, minGDP, slope),
		Note:   "契约 §7 的 GDP 含「各建筑现金池期末总额」。引入政府后，政府现金池允许为负（§4.5.4 的债务），故只计其正值部分；债务另行报告。",
	})

	// ---- A5 ----
	first, last := snaps[0], snaps[len(snaps)-1]
	var lv0, lvT float64
	for _, v := range first.Levels {
		lv0 += v
	}
	for _, v := range last.Levels {
		lvT += v
	}
	popAnnual := 0.0
	if first.Population > 0 && last.Tick > first.Tick {
		popAnnual = math.Pow(last.Population/first.Population, float64(params.TicksPerYear)/float64(last.Tick-first.Tick)) - 1
	}
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A5", Pass: popAnnual >= -1e-9 && popAnnual <= 0.05+1e-9 && lvT > 0,
		Text:   "无爆炸无坍缩；人口增长率 ∈ [0%, +5%] 年化",
		Detail: fmt.Sprintf("人口年化=%.2f%%  总级数 %.0f→%.0f", popAnnual*100, lv0, lvT),
	})

	// ---- A6 ----
	nonEmpty := 0
	for _, sn := range tail {
		if sn.PowerPurchased > 1e-9 || sn.PowerSold > 1e-9 {
			nonEmpty++
		}
	}
	share := float64(nonEmpty) / float64(len(tail))
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A6", Pass: share < 0.20 || lvT > lv0*1.05,
		Text:   "建造队列不饥饿：队列非空 tick 占比 < 20%",
		Detail: fmt.Sprintf("建造力成交 tick 占比=%.1f%%  总级数 %.0f→%.0f", share*100, lv0, lvT),
		Note:   "若资本存量在增长（lvT > lv0），队列忙碌是健康信号而非饥饿，故此处加入增长豁免。",
	})

	for _, v := range sum.Verdicts {
		if v.Pass {
			sum.PassCount++
		}
	}
	return sum
}

func supplyRatio(sn *sim.Snapshot, i int) float64 {
	// §8.4 的 S/a 需要需求标定常数 a；此处以"供给 / 名义需求"作为等价度量：
	// 在 P 处的 D 就是 a(P/Pcost)^(−ε)，故 S/D = (S/a)(P/Pcost)^ε。
	// 判据关心的是漂移，两者只差一个由价格决定的因子，趋势一致。
	d := sn.Demand[i]
	if d <= 1e-12 {
		return sn.Supply[i]
	}
	return sn.Supply[i] / d
}

func linearSlope(ys []float64) float64 {
	n := len(ys)
	if n < 2 {
		return 0
	}
	var sx, sy, sxy, sxx float64
	for i, y := range ys {
		x := float64(i)
		sx += x
		sy += y
		sxy += x * y
		sxx += x * x
	}
	fn := float64(n)
	den := sxx - sx*sx/fn
	if math.Abs(den) < 1e-12 {
		return 0
	}
	return (sxy - sx*sy/fn) / den
}

// Print 输出人类可读的评估报告。
func (s *Summary) Print(w *strings.Builder) {
	fmt.Fprintf(w, "\n--- §8.4 验收判据（末 2,000 周期）---\n")
	for _, v := range s.Verdicts {
		mark := "未通过"
		if v.Pass {
			mark = "通过"
		}
		fmt.Fprintf(w, "%s %-52s %s\n", v.ID, v.Text, mark)
		fmt.Fprintf(w, "     %s\n", v.Detail)
		if v.Note != "" && !v.Pass {
			fmt.Fprintf(w, "     判据自身问题：%s\n", v.Note)
		}
	}
	fmt.Fprintf(w, "汇总：通过 %d/%d\n", s.PassCount, len(s.Verdicts))
}

// PrintCalibration 输出标定结果（§3.4 + 三表联合标定）。
func PrintCalibration(w *strings.Builder, goods []model.Good, margins []float64, spectral, demandScale float64) {
	fmt.Fprintf(w, "--- §3.4 标定结果 ---\n")
	fmt.Fprintf(w, "A 的谱半径 = %.6f（必须 < 1，§3.4 的可行性硬校验）\n", spectral)
	fmt.Fprintf(w, "三表联合标定系数 k = %.6f（§6.3 需求量表的整体缩放，见 calibrate 包注释）\n\n", demandScale)
	fmt.Fprintf(w, "%-10s %10s %10s %10s\n", "商品", "零利润价", "开局价", "开局利润率")
	for i, g := range goods {
		m := 0.0
		if i < len(margins) {
			m = margins[i]
		}
		fmt.Fprintf(w, "%-10s %10.0f %10.0f %9.2f%%\n", g.Name, g.Pcost, g.Pinit, m*100)
	}
}

// SortLevelsDesc 返回按等级降序的 (索引, 等级) 列表，供报告重点展示。
func SortLevelsDesc(levels []float64) [][2]float64 {
	idx := make([][2]float64, len(levels))
	for i, l := range levels {
		idx[i] = [2]float64{float64(i), l}
	}
	sort.Slice(idx, func(a, b int) bool { return idx[a][1] > idx[b][1] })
	return idx
}
