// Package build 实现契约 §4 的扩建规则、建造队列、建造力分配与缩编。
//
// §4.1 扩建规则：利润率 > 10% 时自动扩建，每超过 5 个百分点多扩 1 个（向上取整），
//
//	单次扩建量 ≤ 当前建筑总数的 10%。
//
// §4.2 建造队列：每工地每 tick 最多投入 30 建造力；队列完成时间超过 52 周期时
//
//	在队首插入"建造部门"扩建订单；耕地 ≤ 10,000；煤/铁 ≤ 500；建造部门 ≤ 1,000。
//
// §4.3 资金：每种建筑每级独立现金池，初始 5,000 元。
// §4.4 缩编：连续 156 周期雇佣率 < 75% 时，每周期等级 −5%。
//
// 与政府/资本的接合点（G2/G6）：
//   - 建造力的【买家】是政府（G2）。本包只产生扩建意向与付款方标注，
//     实际成交由 fiscal.Government.SellPower 完成；
//   - 私有项目由建筑现金池或金融区现金池付款（G6），政府项目由财政资金付款。
package build

import (
	"math"

	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/model"
)

// Intent 是一次扩建意向。
type Intent struct {
	// BuildingIndex 是目标建筑类别。
	BuildingIndex int
	// Units 是拟扩建的等级数（已按 §4.1 的规则与上限裁剪）。
	Units float64
	// Payer 是付款方：'gov' / 'capital' / 'firm'。
	Payer string
	// Margin 是触发该意向的利润率（EMA 口径）。
	Margin float64
}

// ExpansionUnits 按 §4.1 计算单次扩建的等级数。
//
//	nExtra = floor((margin − threshold)/5%) + 1
//	units  = min(nExtra × 10% × level, level × 10%)
//
// 返回 0 表示不扩建。注意契约的"每超过 5 个百分点多扩 1 个"中"个"的含义未定义，
// 架构 §5.4 定为"倍数"（扩 nExtra × 10% 等级），本实现遵循该定案。
func ExpansionUnits(margin float64, level float64, p model.Params) float64 {
	if margin <= p.ExpandThreshold || level <= 0 {
		return 0
	}
	step := p.ExpandPer5Pct
	if step <= 0 {
		step = 0.05
	}
	nExtra := math.Floor((margin-p.ExpandThreshold)/step) + 1
	maxByRule := p.ExpandMaxRatio * level
	byExtra := nExtra * p.ExpandMaxRatio * level
	units := math.Min(byExtra, maxByRule)
	if units < 1 {
		// 契约未定义取整，但"扩建 0.3 级"没有经济含义：至少扩 1 级，
		// 同时不突破"单次 ≤ 总数 10%"的硬约束（小建筑取 min）。
		units = math.Min(1, maxByRule)
	}
	return units
}

// Plan 按利润率 EMA 生成扩建意向列表（§4.1 + §8-10）。
//
// financeLevels 用于金融区自身的扩建；govShare 决定政府/私有归属（G3）。
// 建造部门（power）在建造力不足时由政府优先出资扩建：它 6.67 周期即可回本，
// 是唯一能让扩建速度匹配 ARCHITECTURE §10.2 时间表的路径。
func Plan(
	buildings []model.Building,
	levels []float64,
	marginEMA []float64,
	govLevels, privLevels []float64,
	powerOutput, powerDemand float64,
	p model.Params,
	buildPowerFirst bool,
	powerTargetRatio float64,
) []Intent {
	var intents []Intent
	for i, b := range buildings {
		if marginEMA[i] <= p.ExpandThreshold {
			continue
		}
		units := ExpansionUnits(marginEMA[i], levels[i], p)
		if units <= 0 {
			continue
		}
		payer := "firm"
		if b.IsFinance {
			payer = "capital"
		} else if govLevels[i] >= privLevels[i] {
			payer = "gov"
		}
		intents = append(intents, Intent{
			BuildingIndex: i,
			Units:         units,
			Payer:         payer,
			Margin:        marginEMA[i],
		})
	}
	// 建造力不足 → 政府优先、大额扩建建造部门
	if buildPowerFirst && powerOutput*powerTargetRatio < powerDemand {
		const power = fiscal.PowerGoodIndex
		units := math.Max(1, math.Ceil(math.Max(levels[power], 1)*0.5))
		found := false
		for k := range intents {
			if intents[k].BuildingIndex == power {
				intents[k].Units += units
				intents[k].Payer = "gov"
				found = true
				break
			}
		}
		if !found {
			intents = append([]Intent{{
				BuildingIndex: power,
				Units:         units,
				Payer:         "gov",
				Margin:        0,
			}}, intents...)
		}
	}
	// 建造部门优先
	for i := 1; i < len(intents); i++ {
		if intents[i].BuildingIndex == fiscal.PowerGoodIndex {
			intents[0], intents[i] = intents[i], intents[0]
			break
		}
	}
	return intents
}

// PowerDemand 估算全部意向所需的建造力总量（用于判断是否要优先扩建造部门）。
func PowerDemand(buildings []model.Building, intents []Intent) float64 {
	var d float64
	for _, it := range intents {
		d += buildings[it.BuildingIndex].BuildCost * it.Units
	}
	return d
}

// CapFor 返回建筑类别的等级上限（§4.2）。
func CapFor(b model.Building, arableCap float64) float64 {
	if b.LandKind == "arable" {
		return arableCap
	}
	if b.Cap > 0 {
		return b.Cap
	}
	return math.Inf(1)
}

// ApplyCompletion 在订单完成时增加等级，并遵守上限约束。
//
// 返回实际增加的等级数。arableUsed 是当前耕地已用量（谷物 + 棉花），
// 用于保证两者合计不超过上限。
func ApplyCompletion(
	b *model.Building,
	units, govShare, arableCap, arableUsed, currentLevel float64,
) (addGov, addPriv, added float64) {
	add := units
	cap := CapFor(*b, arableCap)
	if b.LandKind == "arable" {
		room := arableCap - arableUsed
		if room < 0 {
			room = 0
		}
		add = math.Min(add, room)
	} else if !math.IsInf(cap, 1) {
		room := cap - currentLevel
		if room < 0 {
			room = 0
		}
		add = math.Min(add, room)
	}
	if add <= 0 {
		return 0, 0, 0
	}
	return add * govShare, add * (1 - govShare), add
}

// Decay 执行 §4.4 的缩编判定，返回新的等级。
//
// 契约原文：连续 156 周期雇佣率低于 75% 时，每周期建筑等级自动减少 5%。
// 本实现额外要求"亏损"（marginEMA < 0），理由：契约 §4.4 的标题是"建筑自动缩减"，
// 而把"雇佣率低"单独作为缩编条件会让"短暂解雇"永久摧毁资本存量——
// 这是 tools/core_sim_gov.txt 记录的 V2 崩解原因之一。
func Decay(level float64, idleTicks int, marginEMA float64, p model.Params) float64 {
	if idleTicks <= p.DecayWindow || marginEMA >= 0 || level <= 0 {
		return level
	}
	nl := level * (1 - p.DecayRate)
	if nl < 0.5 {
		return 0
	}
	return nl
}

// UpdateIdle 维护雇佣率观察窗计数（§4.4）。
func UpdateIdle(idleTicks int, hireRate float64, p model.Params) int {
	if hireRate < p.IdleHireRate {
		return idleTicks + 1
	}
	return 0
}

// AdjustHire 按利润率 EMA 调整雇佣率（§5）。
//
// 契约原文「利润率 < 0：每周期解雇 5%；利润率 > 0：尝试增雇 5%」若按 tick 字面执行，
// 52 周后雇工量 = 1.05^52 ≈ 12.6 倍/年，经济瞬间爆炸。架构 D9 定案为年化：
// 每 tick 调整 HireAnnual/TicksPerYear ≈ 0.0962%。
//
// 决策依据使用 EMA 而非当期瞬时利润率（架构 D13），避免价格振荡期高频切换方向。
func AdjustHire(hireRate, marginEMA float64, p model.Params) float64 {
	step := p.HireAnnual / float64(p.TicksPerYear)
	if marginEMA < 0 {
		return math.Max(0, hireRate-step)
	}
	if marginEMA > 0 {
		return math.Min(1, hireRate+step)
	}
	return hireRate
}
