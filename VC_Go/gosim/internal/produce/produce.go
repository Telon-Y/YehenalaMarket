// Package produce 实现契约 §3.3 的生产、原料短缺惩罚与投入配给。
//
// 契约原文只写了"所需原料不足时产出按比例缩减"，没有定义多个建筑竞争同一投入品
// 时的分配规则。若不定义，总量会不守恒。本包采用【按需求等比配给 pro-rata】：
//
//	R(g) = Σ_b 各建筑申报的投入需求
//	A(g) = 本 tick 可分配供给
//	若 R(g) ≤ A(g)：alloc_b(g) = demand_b(g)
//	否则：          alloc_b(g) = demand_b(g) · A(g)/R(g)
//	shortageFactor_b = min over g ( alloc_b(g)/demand_b(g) )，无投入则 = 1
//
// 该规则保证 Σ alloc = A（物质守恒），且单建筑情形退化为契约的"供给 80% → 产出 80%"。
package produce

import "yehenala/market/internal/model"

// Plan 是一次生产结算的全部中间结果。
type Plan struct {
	// GrossOutput 是施加配给与短缺惩罚【之前】的潜在产出（含自给农场）。
	GrossOutput []float64
	// ActualOutput 是实际产出（含短缺惩罚与自给农场）。
	ActualOutput []float64
	// ProposedInputs 是各商品被申报的投入总需求。
	ProposedInputs []float64
	// AllocRatio 是每种商品的配给比例 ∈[0,1]。1 表示供给充足。
	AllocRatio []float64
	// UsedInputs 是各商品被中间投入实际取用的量。
	UsedInputs []float64
	// ShortageFactor 是每种建筑的实际产出系数（投入不足的惩罚，§3.3）。
	ShortageFactor []float64
	// NetSupply 是可售净供给 = 实际产出 − 中间投入取用（§6.4 的 S_i）。
	NetSupply []float64
}

// Settle 计算一个 tick 的生产与配给。
//
// levels/hire 与 buildings 等长；subsistence 是自给农场的额外产出（不参与配给）。
func Settle(buildings []model.Building, levels, hire []float64, subsistence map[int]float64) *Plan {
	n := model.Goods
	p := &Plan{
		GrossOutput:    make([]float64, n),
		ActualOutput:   make([]float64, n),
		ProposedInputs: make([]float64, n),
		AllocRatio:     make([]float64, n),
		UsedInputs:     make([]float64, n),
		ShortageFactor: make([]float64, len(buildings)),
		NetSupply:      make([]float64, n),
	}
	for i := range p.AllocRatio {
		p.AllocRatio[i] = 1
	}
	for i := range p.ShortageFactor {
		p.ShortageFactor[i] = 1
	}

	// ① 潜在产出与投入申报
	for i, b := range buildings {
		if b.IsNonMarket() {
			continue
		}
		eff := levels[i] * hire[i]
		y := eff * b.Recipe.Qty
		p.GrossOutput[b.Recipe.Output] += y
		for good, qty := range b.Recipe.Inputs {
			p.ProposedInputs[good] += eff * qty
		}
	}
	for good, v := range subsistence {
		p.GrossOutput[good] += v
	}

	// ② 等比配给：配给比例按"可分配供给 / 申报需求"
	for good := 0; good < n; good++ {
		if p.ProposedInputs[good] <= 1e-12 {
			continue
		}
		ratio := p.GrossOutput[good] / p.ProposedInputs[good]
		if ratio > 1 {
			ratio = 1
		}
		if ratio < 0 {
			ratio = 0
		}
		p.AllocRatio[good] = ratio
	}

	// ③ 实际产出：对每种投入取配给比例的最小值作为短缺惩罚，
	//    并按 §3.3 定案设下限 model.ShortageFloor（惩罚最高 75%，即至少保留 25% 产出）。
	for i, b := range buildings {
		if b.IsNonMarket() {
			continue
		}
		sf := 1.0
		for good := range b.Recipe.Inputs {
			if p.AllocRatio[good] < sf {
				sf = p.AllocRatio[good]
			}
		}
		if sf < model.ShortageFloor {
			sf = model.ShortageFloor
		}
		p.ShortageFactor[i] = sf
		eff := levels[i] * hire[i]
		p.ActualOutput[b.Recipe.Output] += eff * b.Recipe.Qty * sf
	}
	for good, v := range subsistence {
		p.ActualOutput[good] += v
	}

	// ④ 中间投入的实际取用与净供给
	for i, b := range buildings {
		if b.IsNonMarket() {
			continue
		}
		eff := levels[i] * hire[i]
		for good, qty := range b.Recipe.Inputs {
			p.UsedInputs[good] += eff * qty * p.AllocRatio[good]
		}
	}
	for good := 0; good < n; good++ {
		net := p.ActualOutput[good] - p.UsedInputs[good]
		if net < 0 {
			net = 0
		}
		p.NetSupply[good] = net
	}
	return p
}

// InputValue 返回每种建筑在给定价格下的中间投入成本（单级、满编口径）。
//
// 契约 §3.4 的"实际成本基"要求投入品按【当期市价】计价，这是 AI 决策唯一可用的口径。
func InputValue(buildings []model.Building, levels, hire, prices []float64, allocRatio []float64) []float64 {
	out := make([]float64, len(buildings))
	for i, b := range buildings {
		if b.IsNonMarket() {
			continue
		}
		eff := levels[i] * hire[i]
		var c float64
		for good, qty := range b.Recipe.Inputs {
			c += eff * qty * allocRatio[good] * prices[good]
		}
		out[i] = c
	}
	return out
}

// WageBill 返回每种建筑的工资支出（当期、实际雇佣口径）。
//
// §5：每级雇佣 LaborPerLevel 人，人均工资 = 该建筑**劳动结构**的加权平均。
// 城镇类（默认）0.75×5 + 0.20×10 + 0.05×20 = 6.75 元；农业类（谷物 / 棉花）
// 0.75×5 + 0.20×7 + 0.05×10 = 5.65 元（§5 第 20 轮）。故逐建筑取 WagePerLevel()，
// 不再乘全局平均工资。
func WageBill(buildings []model.Building, levels, hire []float64) []float64 {
	out := make([]float64, len(buildings))
	for i, b := range buildings {
		out[i] = levels[i] * hire[i] * b.WagePerLevel()
	}
	return out
}

// TotalWage 对 WageBill 求和。
func TotalWage(wages []float64) float64 {
	var s float64
	for _, w := range wages {
		s += w
	}
	return s
}
