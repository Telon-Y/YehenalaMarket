// Package calibrate 实现契约 §3.4 的零利润价、加成价与需求标定。
//
// 这一步是 1.0 的三表自洽性来源（§3.3 投入产出表 + §5 工资表 + §6.3 人口表）：
//
//	零利润价：  p = (I − Aᵀ)⁻¹ · l
//	加成价：    p = ((1−m)I − Aᵀ)⁻¹ · l      （m = 1/6 保证实际成本基利润率恰为 20%）
//	需求标定：  a = S₀ · (Pinit / Pcost)^ε   （使 t=0 时 E = 0）
//
// 以及 1.0 契约缺失、但货币守恒必需的一步：三表联合标定。
//
//	工资与需求都随人口线性缩放，因此"居民税后工资 = 最终需求价值"这个约束
//	对人口是齐次的（缩放人口不改变比值）。必须解出一个【需求缩放系数 k】，
//	或者等价地调整工资/产出水平。参见 tools/calibration_probe.js。
package calibrate

import (
	"fmt"
	"math"

	"yehenala/market/internal/model"
	"yehenala/market/internal/num"
)

// Result 保存一次标定的全部中间结果。
type Result struct {
	// A[i][j] 是"生产 1 单位商品 j 所需的商品 i 数量"（§3.3 的投入量 ÷ 产出量）。
	A num.Matrix
	// LeontiefInv = (I − A)⁻¹，用于把最终需求换算为总产出。
	LeontiefInv num.Matrix
	// SpectralRadius 是 A 的谱半径；< 1 才存在正的零利润价（§3.4 硬校验）。
	SpectralRadius float64
	// Pcost / Pinit 是零利润价与加成价（元/单位）。
	Pcost, Pinit []float64
	// Margins 是 Pinit 下每种建筑在实际成本基下的利润率（应全部等于 m/(1−m)）。
	Margins []float64
	// DemandScale 是三表联合标定解出的 §6.3 需求缩放系数。
	DemandScale float64
}

// InputMatrix 由配方构造投入系数矩阵 A。
// A[i][j] = 建筑 j 每单位产出所消耗的商品 i 数量。
func InputMatrix(specs []model.Building) num.Matrix {
	a := num.NewMatrix(model.Goods)
	for j, b := range specs {
		if b.IsFinance {
			continue
		}
		for i, qty := range b.Recipe.Inputs {
			a[i][j] = qty / b.Recipe.Qty
		}
	}
	return a
}

// UnitLaborCost 返回单位劳动成本 l_j = 工资总额 / 单级产出（§3.4）。
// 工资总额 = LaborPerLevel × 平均工资（§5：5000 × 6.75 = 33750）。
func UnitLaborCost(specs []model.Building) []float64 {
	l := make([]float64, model.Goods)
	for i, b := range specs {
		if b.IsFinance {
			continue
		}
		l[i] = b.LaborPerLevel * model.AverageWage() / b.Recipe.Qty
	}
	return l
}

// PriceEquation 解 p = (M − Aᵀ)⁻¹ · l，其中 M = diag(mCoeff)。
// mCoeff = 1 得零利润价；mCoeff = 1 − m 得"利润率恰为 m/(1−m)"的加成价。
func PriceEquation(a num.Matrix, l []float64, mCoeff float64) ([]float64, error) {
	n := model.Goods
	mat := num.NewMatrix(n)
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			// (M − Aᵀ)[i][j] = mCoeff·δ_ij − A[j][i]
			mat[i][j] = -a[j][i]
			if i == j {
				mat[i][j] += mCoeff
			}
		}
	}
	return num.Solve(mat, l)
}

// InputCost 返回建筑 b 在价格向量 p 下的单级中间投入成本。
func InputCost(b model.Building, p []float64) float64 {
	var c float64
	for i, qty := range b.Recipe.Inputs {
		c += qty * p[i]
	}
	return c
}

// Margin 返回建筑 b 在"收入用自身价格、投入用当期价格"口径下的利润率（§3.4 的实际成本基）。
//
//	利润率 = 单级收入 / (工资 + Σ 投入量 × 投入品当期价) − 1
func Margin(b model.Building, p []float64) float64 {
	revenue := b.Recipe.Qty * p[b.Recipe.Output]
	cost := b.LaborPerLevel*model.AverageWage() + InputCost(b, p)
	if cost <= 0 {
		return 0
	}
	return revenue/cost - 1
}

// Run 执行完整标定。
//
// openerMarkup 是 §3.4 的开局加成率 m0（默认 1/6，使各建筑利润率恰为 20%）。
// demandScale 为 0 时自动求解三表联合标定系数。
func Run(specs []model.Building, openerMarkup float64) (*Result, error) {
	a := InputMatrix(specs)
	l := UnitLaborCost(specs)

	pcost, err := PriceEquation(a, l, 1.0)
	if err != nil {
		return nil, fmt.Errorf("零利润价方程无解: %w", err)
	}
	pinit, err := PriceEquation(a, l, 1.0-openerMarkup)
	if err != nil {
		return nil, fmt.Errorf("加成价方程无解: %w", err)
	}
	r := num.SpectralRadius(a, 200)
	if !(r < 1) {
		return nil, fmt.Errorf("A 的谱半径 = %.4f >= 1，不存在正的零利润价（§3.4 可行性判据失败）", r)
	}
	leontief, err := num.Invert(subtractIdentity(a))
	if err != nil {
		return nil, err
	}

	margins := make([]float64, model.Goods)
	for i, b := range specs {
		if b.IsFinance {
			continue
		}
		margins[i] = Margin(b, pinit)
	}

	res := &Result{
		A:              a,
		LeontiefInv:    leontief,
		SpectralRadius: r,
		Pcost:          pcost,
		Pinit:          pinit,
		Margins:        margins,
	}
	res.DemandScale = jointDemandScale(specs, res)
	return res, nil
}

func subtractIdentity(a num.Matrix) num.Matrix {
	n := len(a)
	out := num.NewMatrix(n)
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			out[i][j] = -a[i][j]
			if i == j {
				out[i][j] += 1
			}
		}
	}
	return out
}

// TotalOutput 把最终需求 f 换算为每种商品的总产出 Y = (I−A)⁻¹·f。
func (r *Result) TotalOutput(f []float64) []float64 {
	return r.LeontiefInv.MulVec(f)
}

// PerCapitaFinalDemand 返回 §6.2/§6.3 决定的、每 1 人每 tick 的最终需求（商品 × 数量）。
//
// tier 是财富档（由平均工资插值，§6.3），scale 是联合标定系数。
//
// 口径：直接查 §6.3 的离散表（取整档），与消费结算（consume 包）使用同一张表。
// 这一点必须严格一致——契约 §2.5 的口径提醒指出，价格方程的名义需求 D 与实际
// 成交量必须在参考价处相等，否则价格方程与真实成交脱节。若此处改用旧的三锚点
// 插值，就会与消费表各算一套，正是该提醒警告的情形。
func PerCapitaFinalDemand(tier, scale float64) []float64 {
	per100k := model.DemandAt(tier)
	groups := model.ConsumeGroupSpecs()
	f := make([]float64, model.Goods)
	for gi, g := range groups {
		target := per100k[gi] / 100000 * scale
		var total float64
		for _, v := range g.Uses {
			total += v
		}
		if total <= 0 {
			continue
		}
		for idx, v := range g.Uses {
			f[idx] += target * v / total
		}
	}
	return f
}

// jointDemandScale 解出使"居民税后工资 = 最终需求价值"成立的需求缩放系数。
//
// 推导（见 tools/calibration_probe.js）：
//
//	每人所需建筑级数  ℓ = Σ_i Y_i/q_i ，其中 Y = (I−A)⁻¹ f ，f 由 §6.3 给出（scale=1）
//	每人工资成本      w = ℓ × 建筑工资 + (ℓ/ctrl) × 金融区工资
//	每人最终需求价值  v = Σ f_i × Pcost_i
//	约束              v × k = w × (1 − t) × k   ⇒   k = w(1−t)/v
//
// 注意：k 与人口无关（两边都线性于人口），这正是"人口不是自由参数"的体现。
func jointDemandScale(specs []model.Building, r *Result) float64 {
	const tier = 10.0
	f := PerCapitaFinalDemand(tier, 1.0)
	y := r.TotalOutput(f)
	var levels float64
	for i, b := range specs {
		if b.IsFinance {
			continue
		}
		levels += y[i] / b.Recipe.Qty
	}
	finLevels := levels / 5.0
	wage := levels*5000*model.AverageWage() + finLevels*1000*model.AverageWage()
	var value float64
	for i := range f {
		value += f[i] * r.Pcost[i]
	}
	if value <= 0 {
		return 1
	}
	// 税后工资应等于需求价值。这里不含税率（税率由调用方在运行时决定），
	// 故基准情形按 t = 0 标定，使"工资 = 需求价值"。
	return wage / value
}

// DemandConstant 计算市场方程的标定常数 a = S₀·(Pinit/Pcost)^ε（§3.4 步骤 3）。
//
// netSupply 是本商品开局的实际可售量（净供给，不是总产出）。
func DemandConstant(g model.Good, netSupply float64) float64 {
	if netSupply <= 0 {
		return 1e-9
	}
	return netSupply * math.Pow(g.Pinit/g.Pcost, g.Eps)
}
