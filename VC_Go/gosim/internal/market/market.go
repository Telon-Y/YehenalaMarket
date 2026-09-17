// Package market 实现契约 §2 的价格动力学。
//
//	m·P̈ + ρ·Ṗ = E + F_ext          （§2.3 二阶弹性系统）
//	E = D − S                        （§2.2 过剩需求）
//	D = a·(P/P₀)^(−ε)                （§2.1 常弹性需求，P₀ ≡ Pcost）
//	K(P) = −E′(P) = (ε·a/P₀)·(P/P₀)^(−ε−1)   （§2.3 局部刚度）
//	m = K·T²/(4π²),  ρ = ζ·K·T/π     （§2.3 由 T、ζ 反推，每 tick 用当期价格重估）
//
// 数值积分：RK4 固定步长 h = DT/Substeps，价格钳制到 [floor, ceil]（§2.4）。
//
// 已独立验证（tools/price_dynamics_probe.js）：固定产能下本积分收敛到 §2.4 的
// 解析平衡态 P* = P₀(a/S)^(1/ε)，最大相对误差 7.2e-13。
package market

import (
	"math"

	"yehenala/market/internal/model"
)

// State 是全部市场与产能状态。
//
// 设计要点：Market 只负责"给定供给 S 求价格 P"，不关心 S 是怎么来的；
// 产能（Levels / Hire）由 sim 包维护，生产与配给由 produce 包计算。
type State struct {
	Goods    []model.Good
	Markets  []model.Market
	Params   model.Params
	FinanceA float64 // 金融区不进入商品市场，这里保留占位以对齐下标
}

// NewState 由商品定义与全局参数构造初始状态。
func NewState(goods []model.Good, p model.Params) *State {
	s := &State{
		Goods:  goods,
		Params: p,
	}
	s.Markets = make([]model.Market, len(goods))
	for i, g := range goods {
		s.Markets[i].Price = g.Pinit
	}
	return s
}

// Stiffness 返回价格 P 处的局部刚度 K(P)（§2.3）。
func Stiffness(g model.Good, a, price float64) float64 {
	ratio := price / g.Pcost
	if ratio <= 0 {
		ratio = 1e-9
	}
	return g.Eps * a / g.Pcost * math.Pow(ratio, -g.Eps-1)
}

// Demand 返回名义需求 D = a(P/P₀)^(−ε)（§2.1）。
func Demand(g model.Good, a, price float64) float64 {
	ratio := price / g.Pcost
	if ratio <= 0 {
		ratio = 1e-9
	}
	return a * math.Pow(ratio, -g.Eps)
}

// Equilibrium 返回 §2.4 的解析平衡态 P* = P₀(a/(S−Fext))^(1/ε)，并应用钳制。
// 第二个返回值表示是否因 S ≤ Fext 而进入"产能不足"分支（价格顶到上限）。
func Equilibrium(g model.Good, a, supply, fExt float64) (float64, bool) {
	den := supply - fExt
	ceil := g.PriceCeilRatio * g.Pcost
	if den <= 0 {
		return ceil, true
	}
	p := g.Pcost * math.Pow(a/den, 1.0/g.Eps)
	return clampPrice(g, p), false
}

func clampPrice(g model.Good, p float64) float64 {
	return math.Min(math.Max(p, g.PriceFloorRatio*g.Pcost), g.PriceCeilRatio*g.Pcost)
}

// Integrate 对单一商品做一次 tick 内的 RK4 积分（§2.3/§2.4）。
//
// supply 是本 tick 的实际供给 S，fExt 是外部冲击（单位：单位/周期，与 E 同量纲）。
// 返回新的价格与 dP/dt。触界时把 dP/dt 置 0（模拟涨跌停）并返回 clamped=true。
func (s *State) Integrate(i int, supply, fExt float64) (price, dPrice float64, clamped bool) {
	g := s.Goods[i]
	mk := &s.Markets[i]
	p, v := mk.Price, mk.DP
	h := s.Params.DT / float64(s.Params.Substeps)

	// 每 tick 用当期价格重估 K、m、ρ（§2.3 参数标定）
	k := Stiffness(g, mk.A, p)
	if !isFinite(k) || k <= 0 {
		k = 1e-9
	}
	mass := k * s.Params.PricePeriod * s.Params.PricePeriod / (4 * math.Pi * math.Pi)
	rho := s.Params.Damping * k * s.Params.PricePeriod / math.Pi
	if !isFinite(mass) || mass <= 0 {
		mass = 1e-9
	}
	if !isFinite(rho) || rho < 0 {
		rho = 0
	}

	// 状态 y = (P, dP/dt)，dy/dt = (dP/dt, (E + F_ext − ρ·dP/dt)/m)
	deriv := func(pv, vv float64) (float64, float64) {
		e := Demand(g, mk.A, pv) - supply + fExt
		return vv, (e - rho*vv) / mass
	}

	for sub := 0; sub < s.Params.Substeps; sub++ {
		k1a, k1b := deriv(p, v)
		k2a, k2b := deriv(p+h/2*k1a, v+h/2*k1b)
		k3a, k3b := deriv(p+h/2*k2a, v+h/2*k2b)
		k4a, k4b := deriv(p+h*k3a, v+h*k3b)
		p += h / 6 * (k1a + 2*k2a + 2*k3a + k4a)
		v += h / 6 * (k1b + 2*k2b + 2*k3b + k4b)

		if !isFinite(p) {
			p, v = g.Pcost, 0
			break
		}
		floor := g.PriceFloorRatio * g.Pcost
		ceil := g.PriceCeilRatio * g.Pcost
		if p < floor {
			p, v, clamped = floor, 0, true
		} else if p > ceil {
			p, v, clamped = ceil, 0, true
		}
	}

	mk.Price, mk.DP = p, v
	mk.Supply = supply
	mk.Demand = Demand(g, mk.A, p)
	mk.Excess = mk.Demand - supply
	if clamped {
		mk.ClampTicks++
	}
	return p, v, clamped
}

// SettleAll 对全部商品完成本 tick 的价格结算。
func (s *State) SettleAll(supply []float64, fExt []float64) {
	for i := range s.Markets {
		var fe float64
		if fExt != nil && i < len(fExt) {
			fe = fExt[i]
		}
		s.Integrate(i, supply[i], fe)
	}
}

// Prices 返回当前价格切片（只读用途）。
func (s *State) Prices() []float64 {
	out := make([]float64, len(s.Markets))
	for i := range s.Markets {
		out[i] = s.Markets[i].Price
	}
	return out
}

// PriceRatio 返回 P / P_cost，用于 §8.4 判据 A1/A3。
func (s *State) PriceRatio(i int) float64 {
	return s.Markets[i].Price / s.Goods[i].Pcost
}

func isFinite(v float64) bool {
	return !math.IsNaN(v) && !math.IsInf(v, 0)
}
