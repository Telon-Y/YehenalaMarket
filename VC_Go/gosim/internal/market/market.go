// Package market 实现契约 §2 的价格动力学。
//
//	m·P̈ + ρ·Ṗ = E + F_ext          （§2.3 二阶弹性系统）
//	E = D − S                        （§2.2 过剩需求）
//	D = a·(P/P₀)^(−ε)                （§2.1 常弹性需求，P₀ ≡ Pcost）
//	K(P) = −E′(P) = (ε·a/P₀)·(P/P₀)^(−ε−1)   （§2.3 局部刚度）
//	m = S                            （§2.3 惯性 ≡ 当期市场内流通商品量）
//	ρ = 2ζ·√(m·K)                    （§2.3 ζ 是唯一旋钮，故阻尼比恒为 ζ）
//	T = 2π·√(m/K)                    （§2.3 周期为【内生量】：流通量越大、价格越难推动）
//
// 数值积分：RK4 固定步长 h = DT/Substeps，价格钳制到 [floor, ceil]（§2.4）。
//
// 已独立验证（tools/price_dynamics_probe.js）：固定产能下本积分收敛到 §2.4 的
// 解析平衡态 P* = P₀(a/S)^(1/ε)。2026-09-19（R14：m ≡ S）复测的最大相对误差
// 为 4.05e-13%（旧口径 m = K·T²/4π² 时为 7.2e-13%），两者同量级。
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
	// UnitElastic 打开 §3.4 的候选方案 A（支出份额锚）：需求弹性取 ε ≡ 1。
	// 默认 false —— 与契约现行条文一致；语义见 docs/ACTIVE.md §6.4。
	UnitElastic bool
}

// EpsOf 返回该商品在当前锚定方案下的需求弹性。
func (s *State) EpsOf(g model.Good) float64 {
	if s.UnitElastic {
		return 1
	}
	return g.Eps
}

// SetAnchor 重锚某种商品的需求常数 a（§3.4 候选方案 C′：按派生需求重锚）。
//
// 不做钳制：非正值由调用方负责回退（锚为 0 会让需求恒为 0、价格失去均衡）。
func (s *State) SetAnchor(i int, a float64) {
	if i >= 0 && i < len(s.Markets) {
		s.Markets[i].A = a
	}
}

// Anchor 返回某种商品当前的需求常数（只读诊断用）。
func (s *State) Anchor(i int) float64 {
	if i < 0 || i >= len(s.Markets) {
		return 0
	}
	return s.Markets[i].A
}

// DemandOf 用当前锚定方案的需求弹性计算名义需求（§2.1）。
func (s *State) DemandOf(g model.Good, a, price float64) float64 {
	ratio := price / g.Pcost
	if ratio <= 0 {
		ratio = 1e-9
	}
	return a * math.Pow(ratio, -s.EpsOf(g))
}

// StiffnessOf 用当前锚定方案的需求弹性计算局部刚度 K(P)（§2.3）。
func (s *State) StiffnessOf(g model.Good, a, price float64) float64 {
	ratio := price / g.Pcost
	if ratio <= 0 {
		ratio = 1e-9
	}
	eps := s.EpsOf(g)
	return eps * a / g.Pcost * math.Pow(ratio, -eps-1)
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
		// 静态口径的默认值：当期零利润价 = 契约的零利润价（§3.4 的解）。
		s.Markets[i].Pzero = g.Pcost
	}
	return s
}

// SetPzero 设定第 i 种商品的**当期零利润价**（§七 R32 的动态口径）。
//
// 只在动态模式（Options.DynamicPcost）下由 sim 每 tick 调用；非正值被忽略，
// 以免把参考价打穿（参考价是价格带与需求归一化的分母）。
func (s *State) SetPzero(i int, v float64) {
	if i < 0 || i >= len(s.Markets) || v <= 0 || !isFinite(v) {
		return
	}
	s.Markets[i].Pzero = v
}

// ref 返回第 i 种商品的**有效参考价 P₀**：动态模式下是当期零利润价，
// 静态模式下是契约的 Good.Pcost。
func (s *State) ref(i int) float64 {
	if i >= 0 && i < len(s.Markets) && s.Markets[i].Pzero > 0 {
		return s.Markets[i].Pzero
	}
	return s.Goods[i].Pcost
}

// goodAt 返回把 Pcost 替换为**有效参考价**的商品副本。
//
// 价格方程的全部子式（需求 D、刚度 K、解析平衡态 P*、钳制带）都以 g.Pcost
// 作为参考价 P₀，故只在入口处替换一次，其余代码无需知道口径。
func (s *State) goodAt(i int) model.Good {
	g := s.Goods[i]
	g.Pcost = s.ref(i)
	return g
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
	// 【§七 R32：P_cost 的两个角色必须分开】
	//
	//	g   —— 契约的固定零利润价 P_cost：**需求归一化** D = a(P/P_cost)^(−ε) 与
	//	       局部刚度 K 的参考价。它必须固定：a 是在 t=0 用 P_init/P_cost 定标的，
	//	       若把 P_cost 换成"当期成本"（与 P 正相关），D 会随 P 上升而上升，
	//	       形成 **价格→成本→需求→价格 的自指正反馈**（实测：价格水平自我抬升，
	//	       税收被放大 5 倍）。
	//	gb  —— 当期零利润价 P⁰(t) = ΣA·P(t) + l：**价格钳制带**与 A1/A3 判据的参考价。
	//	       它就该是动态的——这正是"让生产单位 0 利润的价格"的用处：
	//	       成本变了，地板/天花板要跟着动，否则会出现 §3.4 记录的"地板脱节"。
	g := s.Goods[i]
	gb := s.goodAt(i)
	mk := &s.Markets[i]
	p, v := mk.Price, mk.DP
	h := s.Params.DT / float64(s.Params.Substeps)

	// 每 tick 用当期价格重估 K，并取【当期市场内流通商品量】作为惯性 m（§2.3）
	k := s.StiffnessOf(g, mk.A, p)
	if !isFinite(k) || k <= 0 {
		k = 1e-9
	}
	mass := supply
	if !isFinite(mass) || mass <= 0 {
		// 市场内没有流通量：等效质量取极小值，价格行为交给钳制接管（涨停/跌停）。
		mass = 1e-9
	}
	// 阻尼由阻尼比 ζ 与 m、K 联立确定：ζ = ρ/(2√(m·K))
	rho := 2 * s.Params.Damping * math.Sqrt(mass*k)
	if !isFinite(rho) || rho < 0 {
		rho = 0
	}

	// 状态 y = (P, dP/dt)，dy/dt = (dP/dt, (E + F_ext − ρ·dP/dt)/m)
	deriv := func(pv, vv float64) (float64, float64) {
		e := s.DemandOf(g, mk.A, pv) - supply + fExt
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
		// 钳制带走**当期零利润价** gb（§七 R32）：动态模式下它随投入成本移动。
		floor := gb.PriceFloorRatio * gb.Pcost
		ceil := gb.PriceCeilRatio * gb.Pcost
		if p < floor {
			p, v, clamped = floor, 0, true
		} else if p > ceil {
			p, v, clamped = ceil, 0, true
		}
	}

	mk.Price, mk.DP = p, v
	mk.Supply = supply
	mk.Mass = mass
	mk.Period = 2 * math.Pi * math.Sqrt(mass/k)
	mk.Demand = s.DemandOf(g, mk.A, p)
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

// PriceRatio 返回 P / P₀，用于 §8.4 判据 A1/A3（P₀ 见 State.ref：§七 R32）。
func (s *State) PriceRatio(i int) float64 {
	return s.Markets[i].Price / s.ref(i)
}

func isFinite(v float64) bool {
	return !math.IsNaN(v) && !math.IsInf(v, 0)
}
