// Package report 聚合诊断指标并判定契约 §8.4 的验收判据 A1–A8。
//
// 判据原文（1.0 生产与市场模拟.md §8.4）已由 **2026-09-19 第 15 轮裁决**修订，
// 修订要点（§0.4 第 6 项）：
//
//	A1 价格不长期贴边
//	   只查**贴边占比 < 5%**（删除原 |ln(P/P⁰)| ≤ ln5 的判定——该阈值与价格
//	   钳制带 [0.2,5]×P⁰ 的上限完全重合，价格一旦触边即取等号，判据恒不通过）。
//	   |ln(P/P⁰)| 的极值仍作为**报告量**输出。
//	A2 部门不普遍亏损（原"逐部门 margin ∈ [−10%,+25%]"已废止）
//	   封闭经济下全局总利润恒为 0 ⇒ 必然有部门亏损；改为按**等级加权的亏损占比**
//	   ≤ 50%，并附加"全窗口 max|margin| ≤ 100%"以防数值爆炸。
//	A3 均衡点不漂移（观察窗移到**人口增长停止之后**）
//	   末 500 周期的 |Δ(P/P⁰)| 与 |Δ(S/D)| 均值小于阈值，且仅在人口年化
//	   增长率 |r| < 0.1% 时才判定；人口仍在增长时判为未通过并注明"不适用"。
//	A4 GDP 正值且不衰退
//	A5 无爆炸与无坍缩 + **人口有界**
//	   末 2,000 周期内人口 max/min ≤ 4（R16 缺陷 2 的修复：原判据抓不到
//	   +4.99%/年 复利至 2.34e11 的爆炸），且人口年化增长率 ∈ [0%, +5%]。
//	A6 建造队列不饥饿
//	A7 政府债务不突破上限（**第 15 轮接入**）
//	   末 2,000 周期逐 tick 满足 GovDebt ≤ GovDebtCap。
//	A8 货币守恒（**第 15 轮接入**）
//	   逐 tick 满足 ΔM == NewCapital + 诊断注入增量（§4.5.3）。
//	A9 实际工农生产总值不衰退（**第 18 轮新增**）
//	   用 §7.2 的实际口径（实物量 × 固定 $P_{ref}$）判定真实增长：全程 > 0 且
//	   末 500 周期斜率 ≥ 0。它与 A4（名义 GDP）并列，专用于**剔除货币/价格的干扰**——
//	   实测长跑里名义 GDP 涨到 2.6e13，而居民实际货币余额从 6.7e6 掉到 319。
//
// 本包【同时报告判据自身的可实现性问题】，不掩盖矛盾。
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
			// 【§七 R32】参考价 P⁰ 取**快照里的当期零利润价**（静态模式下它 ≡ Good.Pcost）。
			ref := g.Pcost
			if i < len(sn.Pzero) && sn.Pzero[i] > 0 {
				ref = sn.Pzero[i]
			}
			floor := g.PriceFloorRatio * ref
			ceil := g.PriceCeilRatio * ref
			if sn.Prices[i] <= floor*1.001 || sn.Prices[i] >= ceil*0.999 {
				clampShare[i]++
			}
			l := math.Abs(math.Log(sn.Prices[i] / ref))
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
		if clampShare[i] > worstClamp {
			worstClamp = clampShare[i]
		}
		if maxLog[i] > worstLog {
			worstLog = maxLog[i]
		}
	}
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A1", Pass: a1Pass,
		Text:   "价格不长期贴边：贴边占比 < 5%（各商品）",
		Detail: fmt.Sprintf("最大贴边率=%.2f%%  max|ln(P/P⁰)|=%.4f（仅报告，不判定）", worstClamp*100, worstLog),
		Note:   "2026-09-19 第 15 轮裁决：删除原 |ln(P/P⁰)| ≤ ln5 的判定——它与钳制带上限完全重合，对任何触边情形恒为未通过。极值仍作报告量。",
	})

	// ---- A2（修订：按等级加权的亏损占比）----
	//
	// 【口径】只扫 **11 种商品部门**（i < len(goods)）：金融区与宅邸庄园不是商品生产者，
	// 它们的"利润率"分母是自身工资（工资→0 时会数值爆炸），不属本条判据。
	// 前值实现用 len(sn.Margins)（13），会把金融区卷进来，实测 A2 min = −1.34e10%。
	mMin, mMax := math.Inf(1), math.Inf(-1)
	// lossShare：逐 tick 的"亏损等级占全部等级的比例"，取窗口内**平均**。
	var lossShareSum float64
	var lossTicks float64
	for _, sn := range tail {
		var lossLv, allLv float64
		for i := 0; i < n && i < len(sn.Margins) && i < len(sn.Levels); i++ {
			if sn.Levels[i] <= 0 {
				continue
			}
			allLv += sn.Levels[i]
			if sn.Margins[i] < 0 {
				lossLv += sn.Levels[i]
			}
			if sn.Margins[i] < mMin {
				mMin = sn.Margins[i]
			}
			if sn.Margins[i] > mMax {
				mMax = sn.Margins[i]
			}
		}
		if allLv > 0 {
			lossShareSum += lossLv / allLv
			lossTicks++
		}
	}
	lossShare := 0.0
	if lossTicks > 0 {
		lossShare = lossShareSum / lossTicks
	}
	var num, den float64
	for _, sn := range tail {
		for i := 0; i < n && i < len(sn.Margins) && i < len(sn.Levels); i++ {
			num += sn.Margins[i] * sn.Levels[i]
			den += sn.Levels[i]
		}
	}
	wAvg := 0.0
	if den > 0 {
		wAvg = num / den
	}
	// 【防爆上限 4.5 的来源】§3.1 的实际成本基利润率 = P/P⁰ − 1，而价格被钳制在
	// [0.2, 5]×P⁰(t)（§2.4）⇒ 利润率合法区间是 [−80%, +400%]。留 0.5 的松弛：
	// 钳制带用的是**上一 tick 结算后**的 P⁰，而 margin 用当期价格，两者可微幅错位。
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A2", Pass: lossShare <= 0.50+1e-9 && math.Abs(mMin) <= 4.5 && mMax <= 4.5,
		Text:   "部门不普遍亏损：等级加权亏损占比 ≤ 50%，且 |margin| ≤ 450%",
		Detail: fmt.Sprintf("亏损等级占比=%.2f%% min=%.2f%% max=%.2f%% 加权=%.2f%%", lossShare*100, mMin*100, mMax*100, wAvg*100),
		Note:   "2026-09-19 第 15 轮裁决：原「逐部门 margin ∈ [−10%,+25%]」与「封闭经济全局总利润恒为 0」数学互斥，已废止；改为 ① 按等级加权的亏损占比 ≤ 50%，② 防爆上限 ±450%（= 价格钳制带 [0.2,5]×P⁰ 对应的利润率区间 [−80%,+400%] 加松弛）。同时把口径限定在 11 种商品部门（金融区/宅邸庄园的\"利润率\"分母是自身工资，不属本条）。",
	})

	// ---- A3（修订：观察窗移到人口增长停止之后）----
	// 人口年化增长率取**末 500 周期窗口**（与漂移量同窗口）的端点计算。
	popAnnualA3 := 0.0
	if len(last500) >= 2 {
		f0 := last500[0]
		last := last500[len(last500)-1]
		if f0.Population > 0 && last.Tick > f0.Tick {
			popAnnualA3 = math.Pow(last.Population/f0.Population, float64(params.TicksPerYear)/float64(last.Tick-f0.Tick)) - 1
		}
	}
	popStopped := math.Abs(popAnnualA3) < 0.001
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
	a3Note := "2026-09-19 第 15 轮裁决：观察窗移到**人口增长停止之后**——人口仍在增长时本判据不可判定（原判据与 §6.5 的人口增长数学互斥：人口年增 5% 时 S/a 每 tick 漂移约 9.4e-4，超限一个数量级）。"
	if !popStopped {
		a3Note += fmt.Sprintf(" 当前窗口人口年化 %.3f%%，故判为未通过（不适用）。", popAnnualA3*100)
	}
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A3", Pass: popStopped && dPR < 1e-4 && dSa < 2e-4,
		Text:   "均衡点不漂移：人口停增后 |Δ(P/P⁰)| < 1e-4 且 |Δ(S/D)| < 2e-4 每周期",
		Detail: fmt.Sprintf("Δ(P/P⁰)=%.3e  Δ(S/D)=%.3e  人口年化=%.3f%%（停增=%v）", dPR, dSa, popAnnualA3*100, popStopped),
		Note:   a3Note,
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

	// ---- A5（修订：增加"人口有界"）----
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
	// 人口有界：末 2,000 周期的 max/min。
	popMin, popMax := math.Inf(1), 0.0
	for _, sn := range tail {
		if sn.Population < popMin {
			popMin = sn.Population
		}
		if sn.Population > popMax {
			popMax = sn.Population
		}
	}
	popRatio := 1.0
	if popMin > 1e-9 {
		popRatio = popMax / popMin
	}
	popBounded := popRatio <= 4.0
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A5", Pass: popAnnual >= -1e-9 && popAnnual <= 0.05+1e-9 && lvT > 0 && popBounded,
		Text:   "无爆炸无坍缩：人口有界（窗口 max/min ≤ 4）且年化 ∈ [0%, +5%]",
		Detail: fmt.Sprintf("人口年化=%.2f%%  窗口 max/min=%.2f  总级数 %.0f→%.0f", popAnnual*100, popRatio, lv0, lvT),
		Note:   "2026-09-19 第 15 轮裁决（修 R16 缺陷 2）：原判据只有增长率区间，而上限 +5%/年 本身就是爆炸的成因——R15/R34 实测人口复利到 1.17e11 / 2.34e11 仍判「通过」。现增加「窗口 max/min ≤ 4」的有界性判定。",
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

	// ---- A7（本轮接入）：政府债务不突破上限 ----
	worstDebtRatio := 0.0
	debtBreach := 0
	for _, sn := range tail {
		cap := sn.GovDebtCap
		ratio := 0.0
		if cap > 1e-9 {
			ratio = sn.GovDebt / cap
		} else if sn.GovDebt > 1e-9 {
			ratio = math.Inf(1)
		}
		if ratio > worstDebtRatio {
			worstDebtRatio = ratio
		}
		if sn.GovDebt > cap+1e-6 {
			debtBreach++
		}
	}
	a7Pass := debtBreach == 0
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A7", Pass: a7Pass,
		Text:   "政府债务不突破上限（末 2,000 周期逐 tick）",
		Detail: fmt.Sprintf("最大债务/上限=%.4f  越界 tick 数=%d/%d", worstDebtRatio, debtBreach, len(tail)),
		Note:   "2026-09-19 第 15 轮裁决：A7 本轮接入 report.Assess（此前只由审计套件独立守住）。",
	})

	// ---- A8（本轮接入）：货币守恒 ----
	// 逐 tick 恒等式：ΔM == NewCapital + 诊断注入增量（§4.5.3）。
	//
	// 【容差口径（2026-09-19 第 16 轮）】用**相对容差 1e-8 × 货币存量**（下限 1e-3 元）：
	//
	//   - 逐 tick 的残差来自双精度在几十万笔过账上的累积舍入，量级约为
	//     "每笔 ~1 ulp(余额) × √笔数"：实测 10,000 tick 长跑在货币 1.5e7 时
	//     单 tick 残差最大 **2.88e-02 元 = 相对 1.9e-9**（`out/verify/r40-warehouse-10000.txt`）；
	//   - 双精度有效位是 2.2e-16，故 1e-8 相对容差 = 约 1e8 倍有效位，
	//     足以覆盖"1e4 tick × 每 tick 1e5 笔"的累积舍入；
	//   - 真实缺陷的量级完全不同：历史实测的漏记是 2.1e6 元（逐建筑对账）与
	//     每 tick 净损 15%~34%（工资未付出），都远超该容差。
	maxMoneyResidual, maxMoneyTol := 0.0, 1e-3
	for k := 1; k < len(snaps); k++ {
		prev, cur := snaps[k-1], snaps[k]
		dM := cur.TotalMoney - prev.TotalMoney
		expect := cur.NewCapital + (cur.Infusion - prev.Infusion)
		if r := math.Abs(dM - expect); r > maxMoneyResidual {
			maxMoneyResidual = r
		}
		if tol := 1e-8 * math.Abs(cur.TotalMoney); tol > maxMoneyTol {
			maxMoneyTol = tol
		}
	}
	a8Pass := maxMoneyResidual <= maxMoneyTol
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A8", Pass: a8Pass,
		Text:   "货币守恒：逐 tick ΔM == NewCapital + 诊断注入（§4.5.3）",
		Detail: fmt.Sprintf("最大逐 tick 残差=%.3e 元（容差 %.3e = max(1e-3, 1e-8×货币存量)，全程 %d tick）", maxMoneyResidual, maxMoneyTol, len(snaps)-1),
		Note:   "2026-09-19 第 15 轮裁决：A8 本轮接入 report.Assess（此前只由审计套件与 cmd/diag_govcash 独立守住）。容差为相对口径（1e-8 × 货币存量），以容纳上万 tick 的浮点累积舍入；真实记账缺陷（漏一条腿）的量级是元~万元，远超该容差。",
	})

	// ---- A9（本轮新增）：实际工农生产总值不衰退 ----
	//
	// 【为什么需要它】A1–A8 里与经济表现有关的那几条用的是**名义**口径（价格、GDP、债务）：
	// 10,000 tick 长跑实测价格指数 ×98,704、货币量 3.2e7，而居民**实际**货币余额从 6.7e6
	// 掉到 319——名义数字看不出经济是在增长还是在通胀（`docs/ACTIVE.md` §七 R41）。
	//
	// A9 用 §7.2 的**实际工农生产总值**（实物量 × 固定 P_ref）：
	//   - 全程 > 0，且末 500 周期线性斜率 ≥ 0；
	//   - 与价格水平、税率、货币存量、政府债务**完全无关**。
	minAdded := math.Inf(1)
	addedSeries := make([]float64, len(last500))
	for k, sn := range snaps {
		if sn.ProductAdded < minAdded {
			minAdded = sn.ProductAdded
		}
		if k >= len(snaps)-len(last500) {
			addedSeries[k-(len(snaps)-len(last500))] = sn.ProductAdded
		}
	}
	a9Slope := linearSlope(addedSeries)
	lastSnap := snaps[len(snaps)-1]
	a9Pass := minAdded > 0 && a9Slope >= 0
	sum.Verdicts = append(sum.Verdicts, Verdict{
		ID: "A9", Pass: a9Pass,
		Text: "实际工农生产总值不衰退（固定参考价，与货币无关）",
		Detail: fmt.Sprintf("最低实际增加值=%.0f  末 500 周期斜率=%.3e  末态=%.0f（指数 %.3f，人均 %.4f）",
			minAdded, a9Slope, lastSnap.ProductAdded, lastSnap.ProductIndex, lastSnap.ProductPerCapita),
		Note: "2026-09-19 第 18 轮新增：用 §7.2 的实际口径（实物量 × 固定 P_ref）判断真实增长，" +
			"以剔除名义通胀/通缩的干扰。它与 A4（名义 GDP）并列：A4 看货币面，A9 看实物面。",
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
