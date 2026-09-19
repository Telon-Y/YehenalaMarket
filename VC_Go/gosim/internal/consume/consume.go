// Package consume 实现契约 §6 的消费模型。
//
// 四个消费组（§6.1）、使用价值表（§6.2）、财富档插值（§6.3）、
// 组内权重 w_i = S_i/(2P_i) 与降序购买（§6.4）、满足度。
//
// 本包补上了契约未写、但货币守恒必需的一条约束：
//
//	§5 规定"全部工资用于消费"，而工资是居民唯一的资金来源（1.0 无银行、无信贷、
//	无政府转移、无出口）。因此消费者支出总额 ≤ 当期工资总额（税后）。
//	不加这条约束，消费者会凭空支付超出其收入的金额，货币不守恒。
//
// 该约束的后果是全局总利润恒等于 0（会计恒等式，见 fiscal 包的包注释），
// 这是 1.0 契约 §3.4「各建筑利润率恰好 20%」无法成立的根本原因。
package consume

import (
	"math"
	"sort"

	"yehenala/market/internal/model"
	"yehenala/market/internal/num"
)

// Outcome 是一次消费结算的结果。
type Outcome struct {
	// Bought 是每商品的实际购买量（实际成交量，非需求量）。
	Bought []float64
	// Sat 是每消费组的满足度 ∈[0,1]。
	Sat [4]float64
	// SpendNet 是税前支出，等于各建筑实收的消费者货款。
	SpendNet float64
	// BudgetScale 是预算压缩系数；<1 表示工资买不起当期全部供给。
	BudgetScale float64
	// Target 是各消费组按 §6.3 计算的目标量。
	Target [4]float64
}

// WealthTier 由平均工资插值确定财富等级（§6.3）。
//
//	wage ≤ 5           → 5
//	5 < wage < 10      → 线性插值
//	10 < wage < 20     → 线性插值
//	wage ≥ 20          → 20
//
// 注意：这里插值的是"工资 → 财富档"的映射，与 §6.3 表内各档的取值是两件事。
// 档位边界取自 model.DemandTierMin / DemandTierMax，避免两处各写一套数字。
func WealthTier(wage float64) float64 {
	return num.Clamp(wage, model.DemandTierMin, model.DemandTierMax)
}

// Targets 返回各消费组在给定人口与财富档下的目标需求量（§6.3）。
//
// 口径：直接查 §6.3 的离散表（取整档）。契约修订后该表已补全为 5–20 共 16 档，
// 故不再需要运行时插值；这也保证消费结算与价格标定（calibrate 包）使用同一张表。
//
// demandScale 是三表联合标定系数（见 calibrate.jointDemandScale）：
// 工资与需求都随人口线性缩放，因此必须整体缩放需求量表才能让货币守恒成立。
func Targets(groups []model.ConsumeGroup, pop, tier, demandScale float64) [4]float64 {
	per100k := model.DemandAt(tier)
	var out [4]float64
	for gi := 0; gi < 4 && gi < len(groups); gi++ {
		out[gi] = per100k[gi] * pop / 100000 * demandScale
	}
	return out
}

// WelfareCell 是一个"消费单元"：若干就业人口 + 其所在财富档。
//
// 它把连续的"平均工资 → 财富档"映射离散化：
// 每一级建筑的就业人口构成一个单元，其财富档由该级工资（可含福利金）决定。
// 政府按档位发放福利金时，每个单元的需求量都落在确定档位上，
// 于是"发多少福利 → 需求增加多少"可以逐档对账。
type WelfareCell struct {
	// Workers 是该单元覆盖的就业人口数。
	Workers float64
	// Tier 是该人口的财富档。
	Tier float64
}

// TargetsDiscrete 按离散财富档汇总目标需求量（§6.3 补全表的整数档口径）。
//
// 与 Targets 的区别：
//   - Targets 用连续插值，把全体人口当作同一档（平均工资口径）；
//   - TargetsDiscrete 把人口按财富档分成若干单元，逐档查表再汇总。
//
// 后者是政府发放福利金所必需的：福利金只覆盖部分人口（例如仅就业者），
// 若仍用"全体平均工资"，就无法区分"拿到福利金的人"与"没拿到的人"，
// 刺激消费的效果会被平均掉。
func TargetsDiscrete(
	groups []model.ConsumeGroup,
	cells []WelfareCell,
	demandScale float64,
) [4]float64 {
	var out [4]float64
	for _, c := range cells {
		if c.Workers <= 0 {
			continue
		}
		d := model.DemandAt(c.Tier)
		for gi := 0; gi < 4 && gi < len(groups); gi++ {
			out[gi] += d[gi] * c.Workers / 100000 * demandScale
		}
	}
	return out
}

// CellsFromLevels 把"每级建筑的就业人数"折算为消费单元。
//
// levels[i] 是第 i 类建筑的等级，laborPerLevel[i] 是其每级雇佣人数
// （普通建筑 §5 = 5,000；金融区 G5 = 1,000），wagePerLevel[i] 是其每级工资总额。
//
// welfarePerWorker 是政府发放的福利金（元/人/tick），默认必须为 0——
// 发放规则属契约裁决项，实现不擅自设定。
//
// 财富档由 §6.3 的"平均工资插值"确定：(wagePerLevel/laborPerLevel) + welfarePerWorker。
func CellsFromLevels(levels, laborPerLevel, wagePerLevel []float64, welfarePerWorker float64) []WelfareCell {
	out := make([]WelfareCell, 0, len(levels))
	for i := range levels {
		if levels[i] <= 0 || laborPerLevel[i] <= 0 {
			continue
		}
		workers := levels[i] * laborPerLevel[i]
		wage := wagePerLevel[i]/laborPerLevel[i] + welfarePerWorker
		out = append(out, WelfareCell{Workers: workers, Tier: WealthTier(wage)})
	}
	return out
}


// Purchase 执行组内按权重降序的购买（§6.4）。
//
// supply 是各商品的净供给（会被本函数消耗）；prices 是当期结算价（T6：与本 tick
// 的生产、工资同价，保证会计恒等式闭环）；budget 是消费者的可支配收入。
// taxRate 是政府税率（G1），消费者承担其消费部分。
//
// 【保留说明】本函数是单预算单元（全体人口当作一个消费单元）的旧口径，
// 仍供单元测试与单群体场景使用。§8 主循环走 PurchaseByPools——
// 后者按 §5.1 的人群池逐个结算，是契约口径。
func Purchase(
	groups []model.ConsumeGroup,
	targets [4]float64,
	supply, prices []float64,
	budget, taxRate float64,
) Outcome {
	stock := make([]float64, len(supply))
	copy(stock, supply)
	out := Outcome{
		Bought:      make([]float64, len(supply)),
		Target:      targets,
		BudgetScale: 1,
	}
	bought := buyOneUnit(groups, targets, stock, prices, budget, taxRate)
	copy(out.Bought, bought)
	for gi := 0; gi < 4; gi++ {
		out.Sat[gi] = satisfaction(groups, gi, bought, targets[gi])
	}
	// 保留旧的 BudgetScale 语义：目标量的税前支出超过可支配预算时为压缩系数。
	var want float64
	for i := range bought {
		want += bought[i] * prices[i]
	}
	affordable := budget / (1 + taxRate)
	if want > affordable && want > 1e-9 {
		out.BudgetScale = affordable / want
	}
	for i := range out.Bought {
		out.SpendNet += out.Bought[i] * prices[i]
	}
	return out
}

// PoolOutcome 是一个人群池的消费结算结果。
type PoolOutcome struct {
	// Bought 是该池各商品的实际购买量。
	Bought []float64
	// SpendNet 是该池的税前消费支出。
	SpendNet float64
	// Sat 是该池四个消费组的满足度。
	Sat [4]float64
}

// PooledOutcome 是全部人群池的消费结算结果（§5.1 口径）。
type PooledOutcome struct {
	// Pools 与传入的池一一对应。
	Pools []PoolOutcome
	// Bought 是各商品的总购买量（全部池之和）。
	Bought []float64
	// SpendNet 是税前消费总支出（全部池之和）——即 §7 GDP 的消费者支出口径。
	SpendNet float64
	// Sat 是按人口加权的总满足度（供 §6.5 人口增长使用，只取必需品两项）。
	Sat [4]float64
	// Target 是各消费组的目标需求量之和（按池汇总）。
	Target [4]float64
	// Starved 是"有目标需求但因现金不足未买满"的池数，供诊断。
	Starved int
	// Constrained 是"想买但供给不足"的池数，供诊断。
	Constrained int
	// PoolSpend 是各池的税前支出（诊断用，与入参 pools 一一对应）。
	PoolSpend []float64
	// PoolBudget 是各池的税前预算（诊断用）。
	PoolBudget []float64
	// PoolTargetValue 是各池按目标量的税前支出（诊断用）。
	//
	// 三者对比即可判定消费受限的原因：
	//
	//	PoolSpend ≈ PoolTargetValue < PoolBudget  ⇒ 供给不足（缺货）
	//	PoolSpend ≈ PoolBudget     < PoolTargetValue ⇒ 预算不足（缺钱）
	PoolTargetValue []float64
	// NoIncome 是"有人口但可支配预算 ≤ 0 ⇒ 无法消费"的池数（诊断，§6.5）。
	//
	// 【2026-09-19 第 15 轮裁决】失业人口没有工资（其资金池只可能收到福利金），
	// 在福利金关闭时预算恒为 0 ⇒ 满足度为 0 ⇒ 拉低幸福度与人口增长。
	// 修正前这类池被当作"满足度 = 1"（正是"人口增长条件不生效"的根因）。
	NoIncome int
	// SatByClass 是按阶级人口加权的四组满足度（c = 0 劳工 / 1 工程师 / 2 资本家）。
	SatByClass [3][4]float64
	// PopByClass 是各阶级的人口合计，用于把 SatByClass 折算为幸福度。
	PopByClass [3]float64
}

// PoolSpec 描述参与消费的一个人群池。
//
// 之所以由调用方传入纯数值而不是直接传 cohort.Ledger：
// consume 包不应依赖人群包的内部表示，只关心"谁有多少预算、要买什么、人口多少"。
type PoolSpec struct {
	// Population 是该池人口（用于满足度加权）。
	Population float64
	// Tier 是该池的财富档（§6.3）。
	Tier float64
	// Budget 是该池的税前消费预算（元）。
	Budget float64
	// Class 是该池的阶级下标（0 劳工 / 1 工程师 / 2 资本家）。
	//
	// 【2026-09-19 第 15 轮裁决】新增：幸福度要按阶级分别报告
	// （失业人口算劳工，其"无收入 ⇒ 无法消费 ⇒ 幸福度 0"必须能在报告里看到）。
	Class int
}

// PurchaseByPools 按 §5.1 的人群池逐个结算消费。
//
// 与 Purchase 的区别：
//   - 每个池有自己的预算与财富档，目标需求量逐池查 §6.3 的离散表后汇总；
//   - 各池共享同一份净供给；池按【财富档从高到低】顺序购买，
//     高价档先买（出得起价、且现实中高收入者确实优先获得稀缺品）；
//   - 满足度按池计算后按人口加权汇总，使 §6.5 的人口增长反映真实的人群差异。
//
// supply 会被本函数消耗（复制内部副本，不修改入参）。
func PurchaseByPools(
	groups []model.ConsumeGroup,
	pools []PoolSpec,
	supply, prices []float64,
	demandScale float64,
) PooledOutcome {
	return PurchaseByPoolsScaled(groups, pools, supply, prices, demandScale, 1.0)
}

// PurchaseByPoolsScaled 与 PurchaseByPools 同，但带一个**篮子系数**（§6.3 第 24 轮）。
//
// basketScale 把每档的目标需求量整体缩放（1.0 = 契约原表）。它用于检验
// "§6.3 的数量篮子太小 ⇒ 消费/工资只有 0.2 ⇒ 消费品部门长期亏损"这一假设：
// 系数调大后，同样的工资能买到更多条目，价格才可能停在覆盖成本的水平。
func PurchaseByPoolsScaled(
	groups []model.ConsumeGroup,
	pools []PoolSpec,
	supply, prices []float64,
	demandScale, basketScale float64,
) PooledOutcome {
	n := len(supply)
	out := PooledOutcome{
		Pools:           make([]PoolOutcome, len(pools)),
		Bought:          make([]float64, n),
		PoolSpend:       make([]float64, len(pools)),
		PoolBudget:      make([]float64, len(pools)),
		PoolTargetValue: make([]float64, len(pools)),
	}
	stock := make([]float64, n)
	copy(stock, supply)

	// 逐池目标需求量 → 汇总
	targetsByPool := make([][4]float64, len(pools))
	for pi, p := range pools {
		d := model.DemandAtScaled(p.Tier, basketScale)
		for g := 0; g < 4; g++ {
			t := d[g] * p.Population / 100000 * demandScale
			targetsByPool[pi][g] = t
			out.Target[g] += t
		}
		// 目标量的税前价值按使用价值占比折算：组 g 的目标由若干商品共同满足，
		// 各商品按 Uses 权重分担。这里只用它做诊断，不参与结算。
		var tv float64
		for g := range groups {
			if g >= 4 {
				break
			}
			var totalUse float64
			for _, v := range groups[g].Uses {
				totalUse += v
			}
			if totalUse <= 0 {
				continue
			}
			for idx, v := range groups[g].Uses {
				if idx >= 0 && idx < len(prices) {
					tv += targetsByPool[pi][g] * v / totalUse * prices[idx]
				}
			}
		}
		out.PoolTargetValue[pi] = tv
		out.PoolBudget[pi] = p.Budget
	}

	// 购买顺序：财富档从高到低。同档按池下标稳定排序，保证确定性（架构 §7.1）。
	order := make([]int, len(pools))
	for i := range order {
		order[i] = i
	}
	sort.SliceStable(order, func(a, b int) bool {
		return pools[order[a]].Tier > pools[order[b]].Tier
	})

	// 按池购买
	var popWeighted [4]float64
	var popTotal float64
	for _, pi := range order {
		p := pools[pi]
		po := PoolOutcome{Bought: make([]float64, n)}
		cls := p.Class
		if cls < 0 || cls >= 3 {
			cls = 0
		}
		if p.Population > 1e-12 && p.Budget > 0 {
			bought := buyOneUnit(groups, targetsByPool[pi], stock, prices, p.Budget, 0)
			copy(po.Bought, bought)
			for i := range bought {
				po.SpendNet += bought[i] * prices[i]
				out.Bought[i] += bought[i]
				out.SpendNet += bought[i] * prices[i]
			}
			out.PoolSpend[pi] = po.SpendNet
			for g := 0; g < 4; g++ {
				po.Sat[g] = satisfaction(groups, g, bought, targetsByPool[pi][g])
			}
			// 诊断：有目标却没买满，是缺钱还是缺货？
			for g := 0; g < 4; g++ {
				if targetsByPool[pi][g] > 1e-9 && po.Sat[g] < 1-1e-9 {
					// 该组商品的可得量是否已耗尽？
					if groupStockExhausted(groups, g, stock) {
						out.Constrained++
					} else {
						out.Starved++
					}
					break
				}
			}
			if p.Population > 1e-12 {
				for g := 0; g < 4; g++ {
					popWeighted[g] += po.Sat[g] * p.Population
					out.SatByClass[cls][g] += po.Sat[g] * p.Population
				}
				popTotal += p.Population
				out.PopByClass[cls] += p.Population
			}
		} else if p.Population > 1e-12 {
			// 【2026-09-19 第 15 轮裁决：零预算 ⇒ 无法消费 ⇒ 满足度 0】
			//
			// 修正前这里把"有人口但没钱"的池记为满足度 1（空真通过），
			// 于是失业人口（无工资、福利金关闭时预算为 0）被当作"过得很好"，
			// 人口增长条件（§6.5 由必需品满足度驱动）因此**完全不生效**——
			// 实测人口仍以 +5%/年复利到 2.34e11（R16 缺陷 2、R34 实测）。
			// 现在的口径：没有收入就没有消费，满足度 = 0，幸福度 = 0，
			// 人口增长随之转为负值（§6.5 的 −20%/年锚点）。
			for g := 0; g < 4; g++ {
				po.Sat[g] = 0
			}
			popTotal += p.Population
			out.PopByClass[cls] += p.Population
			out.NoIncome++
		} else {
			for g := 0; g < 4; g++ {
				po.Sat[g] = 1
			}
		}
		out.Pools[pi] = po
	}

	if popTotal > 1e-12 {
		for g := 0; g < 4; g++ {
			out.Sat[g] = popWeighted[g] / popTotal
		}
	}
	// 阶级内的满足度按该阶级人口折算（幸福度的分母）。
	for c := 0; c < 3; c++ {
		if out.PopByClass[c] <= 1e-12 {
			continue
		}
		for g := 0; g < 4; g++ {
			out.SatByClass[c][g] /= out.PopByClass[c]
		}
	}
	return out
}

// buyOneUnit 让一个预算单元按 §6.4 的权重降序购买。
//
// stock 会被就地消耗（跨池共享同一份净供给）。
// taxRate 只影响预算压缩判断；调用方传 0 表示 budget 已是税前口径。
func buyOneUnit(
	groups []model.ConsumeGroup,
	targets [4]float64,
	stock, prices []float64,
	budget, taxRate float64,
) []float64 {
	bought := make([]float64, len(stock))

	for gi := range groups {
		g := groups[gi]
		if gi >= 4 {
			break
		}
		// 组内权重 w_i = S_i / (2·P_i)，按降序（§6.4）
		type item struct {
			idx int
			w   float64
			per float64
		}
		items := make([]item, 0, len(g.Uses))
		var totalUse float64
		for idx, per := range g.Uses {
			if idx < 0 || idx >= len(stock) {
				continue
			}
			totalUse += per
			w := 0.0
			if prices[idx] > 0 {
				w = stock[idx] / (2 * prices[idx])
			}
			items = append(items, item{idx: idx, w: w, per: per})
		}
		if totalUse <= 0 {
			continue
		}
		sort.Slice(items, func(a, b int) bool { return items[a].w > items[b].w })

		target := targets[gi]
		var got float64
		for _, it := range items {
			if got >= target-1e-12 {
				break
			}
			avail := stock[it.idx]
			if avail <= 1e-12 {
				continue
			}
			want := (target - got) / it.per
			take := math.Min(want, avail)
			stock[it.idx] -= take
			bought[it.idx] += take
			got += take * it.per
		}
	}

	// 预算约束：含税支出不得超过预算。
	var want float64
	for i := range bought {
		want += bought[i] * prices[i]
	}
	affordable := budget / (1 + taxRate)
	if want > affordable && want > 1e-9 {
		scale := affordable / want
		for i := range bought {
			bought[i] *= scale
		}
	}
	return bought
}

// satisfaction 返回某消费组的满足度 = 实得使用价值 / 目标使用价值。
func satisfaction(groups []model.ConsumeGroup, gi int, bought []float64, target float64) float64 {
	if target <= 1e-9 {
		return 1
	}
	if gi >= len(groups) {
		return 1
	}
	var got float64
	for idx, per := range groups[gi].Uses {
		if idx >= 0 && idx < len(bought) {
			got += bought[idx] * per
		}
	}
	return math.Min(1, got/target)
}

// groupStockExhausted 判断某消费组涉及的商品是否已全部售罄。
func groupStockExhausted(groups []model.ConsumeGroup, gi int, stock []float64) bool {
	if gi >= len(groups) {
		return false
	}
	for idx := range groups[gi].Uses {
		if idx >= 0 && idx < len(stock) && stock[idx] > 1e-9 {
			return false
		}
	}
	return true
}

// NecessarySatisfaction 返回必需品满足度（§6.5：简朴衣物与基础食物）。
func (o Outcome) NecessarySatisfaction() float64 {
	return (o.Sat[model.GroupPlainClothes] + o.Sat[model.GroupBasicFood]) / 2
}

// NecessarySatisfaction 返回按人口加权的必需品满足度（§6.5 人口增长的输入）。
//
// 修订说明：旧口径把全体人口当作一个消费单元，故"必需品满足度"只有一个值。
// §5.1 建立人群池后，各池的满足度不同（资本家与劳工的需求档不同），
// 必须按人口加权汇总，否则人口增长会只反映某一个阶级的处境。
func (o PooledOutcome) NecessarySatisfaction() float64 {
	return (o.Sat[model.GroupPlainClothes] + o.Sat[model.GroupBasicFood]) / 2
}

// Happiness 返回按人口加权的**幸福度**（§6.5，2026-09-19 第 15 轮裁决）。
//
// 定义：四个消费组满足度的算术平均 ∈ [0,1]。
//
//	幸福度 = (Sat[简朴衣物] + Sat[基础食物] + Sat[标准衣物] + Sat[住宅]) / 4
//
// 与 §6.5 的人口增长口径的分工：
//   - **人口增长**仍由必需品满足度（简朴衣物 + 基础食物）驱动（契约原文）；
//   - **幸福度**是给玩家看的综合指标（1.1 UI），也是"失业 ⇒ 无收入 ⇒ 无法消费"
//     这一后果的可观察量：失业池的幸福度恒为 0，会把总体幸福度拉下来。
func (o PooledOutcome) Happiness() float64 {
	var s float64
	for g := 0; g < 4; g++ {
		s += o.Sat[g]
	}
	return s / 4
}

// HappinessByClass 返回某一阶级的幸福度（四组满足度均值）。
func (o PooledOutcome) HappinessByClass(c int) float64 {
	if c < 0 || c >= 3 {
		return 0
	}
	var s float64
	for g := 0; g < 4; g++ {
		s += o.SatByClass[c][g]
	}
	return s / 4
}

// PopulationGrowth 返回年化人口增长率（§6.5）。
//
// 契约原文「满足度 ≥75% → +5% / 0% → −20% / 之间线性插值 / 75% 时增长率为 0」
// 三段表述无法由单一斜率同时满足。本实现取两段线性（架构 §4.5 的 T7 定案）：
//
//	sat ≥ t0: r = hiRate · (sat − t0)/(1 − t0)
//	sat <  t0: r = loRate · (t0 − sat)/t0
//
// 三个锚点（+5% @100%、0 @75%、−20% @0%）全部满足。
func PopulationGrowth(satNec float64, p model.Params) float64 {
	if satNec >= p.PopSatTarget {
		den := 1 - p.PopSatTarget
		if den <= 0 {
			return p.PopGrowthHi
		}
		return p.PopGrowthHi * (satNec - p.PopSatTarget) / den
	}
	if p.PopSatTarget <= 0 {
		return p.PopGrowthLo
	}
	return p.PopGrowthLo * (p.PopSatTarget - satNec) / p.PopSatTarget
}
