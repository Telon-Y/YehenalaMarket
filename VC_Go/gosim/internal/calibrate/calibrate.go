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
	// SpectralRadius 是 A 的谱半径；**w·A 的谱半径 < 1** 才存在正的零利润价（§3.4 硬校验）。
	SpectralRadius float64
	// BuyerWedge 是本次标定使用的买家加载系数 w（§4.5.6）。
	BuyerWedge float64
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
		if !b.Produces() {
			continue
		}
		for i, qty := range b.Recipe.Inputs {
			a[i][j] = qty / b.Recipe.Qty
		}
	}
	return a
}

// UnitLaborCost 返回单位劳动成本 l_j = 工资总额 / 单级产出（§3.4）。
//
// 工资总额 = b.WagePerLevel() = LaborPerLevel × 该建筑劳动结构的人均工资。
//
// 【§5 第 20 轮】它**不再是全局常数 33,750**：
//
//	城镇类（默认结构）5,000 × 6.75 = 33,750
//	农业类（谷物 / 棉花）5,000 × 5.65 = 28,250
func UnitLaborCost(specs []model.Building) []float64 {
	l := make([]float64, model.Goods)
	for i, b := range specs {
		if !b.Produces() {
			continue
		}
		l[i] = b.WagePerLevel() / b.Recipe.Qty
	}
	return l
}

// PriceEquation 解 (M − w·Aᵀ)·p = l，其中 M = diag(mCoeff)、w = 买家加载系数。
//
//	mCoeff = 1    得零利润价（生产者零利润）
//	mCoeff = 1−m₀ 得"利润率恰为 m₀/(1−m₀)"的加成价
//	w      = 买家加载系数（§4.5.6：w = (1+仓库加价)(1+消费税)，默认 1.076）
//
// 【为什么投入项要乘 w（2026-09-19 第 16 轮）】仓库落地后，生产者卖出 1 单位收 P，
// 但买进 1 单位投入要付 P·w（仓库加价 + 消费税都落在买家身上）。于是
// "零利润"的条件变成 P_j = w·Σ_i A_ij P_i + l_j，即本式。
// w = 1 时退化为仓库落地前的口径（历史对照）。
func PriceEquation(a num.Matrix, l []float64, mCoeff, w float64) ([]float64, error) {
	n := model.Goods
	mat := num.NewMatrix(n)
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			// (M − w·Aᵀ)[i][j] = mCoeff·δ_ij − w·A[j][i]
			mat[i][j] = -w * a[j][i]
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

// Margin 返回建筑 b 在"收入用自身价格、投入用买家实际成本"口径下的利润率（§3.1 实际成本基）。
//
//	利润率 = 单级收入 / (工资 + w·Σ 投入量 × 投入品当期价) − 1
//
// w 是买家加载系数（§4.5.6）：投入品是从仓库买的，实付 = 市价 × w。
func Margin(b model.Building, p []float64, w float64) float64 {
	revenue := b.Recipe.Qty * p[b.Recipe.Output]
	cost := b.WagePerLevel() + w*InputCost(b, p)
	if cost <= 0 {
		return 0
	}
	return revenue/cost - 1
}

// Run 执行完整标定。
//
// openerMarkup 是 §3.4 的开局加成率 m₀（默认 1/6，使各建筑利润率恰为 20%）。
// buyerWedge 是 §4.5.6 的买家加载系数 w（默认取 DefaultParams().BuyerWedge()；
// 传 1 可回退到仓库落地前的历史口径）。
func Run(specs []model.Building, openerMarkup, buyerWedge float64) (*Result, error) {
	if buyerWedge <= 0 {
		buyerWedge = 1
	}
	a := InputMatrix(specs)
	l := UnitLaborCost(specs)

	pcost, err := PriceEquation(a, l, 1.0, buyerWedge)
	if err != nil {
		return nil, fmt.Errorf("零利润价方程无解: %w", err)
	}
	pinit, err := PriceEquation(a, l, 1.0-openerMarkup, buyerWedge)
	if err != nil {
		return nil, fmt.Errorf("加成价方程无解: %w", err)
	}
	r := num.SpectralRadius(a, 200)
	// 【可行性判据】零利润价存在正解的条件是 **w·A 的谱半径 < 1**（原判据是对 A 本身）。
	if !(buyerWedge*r < 1) {
		return nil, fmt.Errorf("w·A 的谱半径 = %.4f（w = %.6f，A 的谱半径 = %.4f）>= 1，"+
			"不存在正的零利润价（§3.4 可行性判据失败）", buyerWedge*r, buyerWedge, r)
	}
	leontief, err := num.Invert(subtractIdentity(a))
	if err != nil {
		return nil, err
	}

	margins := make([]float64, model.Goods)
	for i, b := range specs {
		if !b.Produces() {
			continue
		}
		margins[i] = Margin(b, pinit, buyerWedge)
	}

	res := &Result{
		A:              a,
		LeontiefInv:    leontief,
		SpectralRadius: r,
		BuyerWedge:     buyerWedge,
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

// ClassWeightedPerCapitaFinalDemand 返回按**实际阶级人口分布**加权的
// "每人每 tick 最终需求"（商品 × 数量）。
//
// 【为什么不能只用单一财富档（2026-09-19 第 24 轮修正）】
//
// 旧实现用固定 `tier = 10`（人均工资 10 元）算每人需求，再要求它与人均工资相等。
// 但 §5.1 的人群池是**按阶级分档**的：劳工 75% 落档 5、工程师 20% 档 10、资本家 5% 档 20
// （§6.3 的"工资 → 财富档"插值）。于是真实的需求篮子比"所有人都是档 10"**小得多**：
//
//	档 10 每 10 万人需求价值 = 361,360
//	按 75/20/5 加权       = 211,358   ⇒ 只有档 10 的 **58.5%**
//
// 这个 1.71 倍的错配是**系统性**的：价格方程的名义需求被高估 ⇒ 价格与利润率被系统性压低。
// 实测（`docs/ACTIVE.md` §七 R48）：修正前所有消费品部门长期在 −30% ~ −70% 的亏损区，
// 农业、加工食品、服装、高档服装、建造部门**全部亏损并持续解雇**，
// 而"农业过度吸走人力"只是这个错配的表象——农业自己也在萎缩。
//
// 三档的锚点直接取 §5 的阶级工资（5 / 10 / 20）：它们对**所有**劳动结构都不变
// （城镇 5/10/20、农业 5/7/10 的**占比**都是 75/20/5，而档位由**阶级**决定）。
func ClassWeightedPerCapitaFinalDemand(scale float64) []float64 {
	// 阶级比例与"该阶级的财富档"（§5.1 的表 + §6.3 的工资→档插值）。
	tiers := [3]float64{5, 10, 20}
	f := make([]float64, model.Goods)
	for c := 0; c < 3; c++ {
		share := model.CohortShares[c]
		if share <= 0 {
			continue
		}
		fc := PerCapitaFinalDemand(tiers[c], share*scale)
		for i := range f {
			f[i] += fc[i]
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
//	每人最终需求价值  v = Σ f_i × Pcost_i × BuyerWedge
//	约束              v × k = w × k   ⇒   k = w / v
//
// 【§4.5.6 的口径】需求价值按**买家实付价**计（Pcost × w）：仓库加价与消费税
// 落在买家（居民）身上，故同样的数量篮子要花更多的钱——k 因此比仓库落地前小。
// 若漏掉 w，标定出来的 k 会让居民"按生产者价格"买满篮子，而实际付不起。
//
// 【第 24 轮修正：f 改为按**实际阶级分布**加权（`ClassWeightedPerCapitaFinalDemand`）】
// 旧实现用固定档 10，把每人需求的价值高估了 1/0.585 ≈ 1.71 倍，于是 k 被低估同样的倍数，
// 价格方程的名义需求长期高于真实需求 ⇒ 各部门系统性亏损。详见该函数的注释与 §七 R48。
//
// 注意：k 与人口无关（两边都线性于人口），这正是"人口不是自由参数"的体现。
func jointDemandScale(specs []model.Building, r *Result) float64 {
	f := ClassWeightedPerCapitaFinalDemand(1.0)
	y := r.TotalOutput(f)
	var levels, wage float64
	for i, b := range specs {
		if !b.Produces() {
			continue
		}
		lv := y[i] / b.Recipe.Qty
		levels += lv
		// 【§5 第 20 轮】逐建筑取自己的 WagePerLevel()：
		// 谷物 / 棉花的每级工资是 28,250（农业结构），其余是 33,750（城镇结构）。
		wage += lv * b.WagePerLevel()
	}
	finLevels := levels / 5.0
	wage += finLevels * 1000 * model.AverageWage()
	var value float64
	for i := range f {
		value += f[i] * r.Pcost[i] * r.BuyerWedge
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
	return DemandConstantAt(g, netSupply, g.Eps)
}

// DemandConstantAt 与 DemandConstant 同式，但显式给出弹性。
//
// 用于 §3.4 的候选方案：方案 A（支出份额锚，docs/ACTIVE.md §6.4）取 eps ≡ 1，
// 此时 a = S₀·P_init/P_cost，配合 ε ≡ 1 得 P*(λ) = P_init/λ。
// 方案 C′（派生需求锚）则把 netSupply 换成当期派生需求，弹性仍取该商品的 ε（或 A 的 1）。
func DemandConstantAt(g model.Good, netSupply, eps float64) float64 {
	if netSupply <= 0 {
		return 1e-9
	}
	return netSupply * math.Pow(g.Pinit/g.Pcost, eps)
}
