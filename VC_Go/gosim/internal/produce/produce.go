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
	//
	// 【判据是 `Produces()` 而不是 `IsNonMarket()`】本循环会把
	// `b.Recipe.Output` 直接当**商品下标**用 ⇒ 任何"没有真实商品配方"的建筑都必须跳过，
	// 否则它的零值/越界配方会污染产出表或**越界 panic**。
	//
	// 历史上两者等价（非市场 ⇔ 非生产者）；但仓库/消费代理落地后，
	// `Produces()` 才表达"是真正的商品生产者"，它排除了：
	//   - 非市场建筑（金融区 / 宅邸庄园）；
	//   - 贸易节点（仓库 / 消费代理，`Recipe` 为零值 ⇒ 会被误加进 0 号商品"谷物"）；
	//   - **金矿与央行**（1.2 §1.2-5，2026-09-20 第 58 轮）：金矿的
	//     `Recipe.Output = GoldGood = 11`，而产出表只有 `Goods = 11` 项
	//     ⇒ 用 `IsNonMarket()` 判会直接 `index out of range [11] with length 11`。
	for i, b := range buildings {
		if !b.Produces() {
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
	//
	// 【判据同 ①：必须用 `Produces()`】本循环同样把 `b.Recipe.Output` 当商品下标。
	for i, b := range buildings {
		if !b.Produces() {
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
		if !b.Produces() {
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
		// 【判据统一为 `Produces()`】与 ①②③④ 一致：金矿/央行没有可计价商品配方，
		// 贸易节点（仓库/代理）的配方是零值。用 `IsNonMarket()` 会让金矿的
		// 投入项被照常计价（其实它的煤/工具**应当**计价）——这一点需要注意：
		// 金矿**确实**消耗煤与工具，故它的投入价值**必须**计入，
		// 否则 §3.4 的成本基会漏掉金矿的总需求。
		// 而 `ProducesGold` 的建筑**有**真实的 `Recipe.Inputs`，
		// 故这里不能把金矿排除掉——只有"完全没有配方"的才跳过。
		if b.IsNonMarket() || b.IsTradeNode() || b.IsCentralBank {
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
