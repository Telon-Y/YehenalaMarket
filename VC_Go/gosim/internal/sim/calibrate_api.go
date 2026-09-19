package sim

import (
	"math"

	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/model"
)

// CalibrateOnly 只执行 §3.4 的标定，不构造仿真状态。
//
// demandScale 为 0 时采用 calibrate 解出的三表联合标定系数。
// 该函数供 CLI 的 -calibrate-only 与测试使用。
func CalibrateOnly(specs []model.Building, demandScale float64) (*calibrate.Result, error) {
	return CalibrateOnlyScaled(specs, demandScale, 1.0)
}

// CalibrateOnlyScaled 在 CalibrateOnly 之上支持"开局价整体放大"（§七 R31 的对照实验）。
//
// goods 由本函数自行构造（与 New 中的那份同源），故调用方拿到的利润率与
// 实际运行用的那份**逐位一致**——避免 CLI 打印一套、仿真跑另一套。
func CalibrateOnlyScaled(specs []model.Building, demandScale, priceMult float64) (*calibrate.Result, error) {
	return CalibrateOnlyScaledWedge(specs, demandScale, priceMult, model.DefaultParams().BuyerWedge())
}

// CalibrateOnlyScaledWedge 与 CalibrateOnlyScaled 同，但显式给出**买家加载系数** w。
//
// 【2026-09-19 第 17 轮：税制可替换】w = (1+仓库加价)(1+消费税) 进入 §3.4 的价格方程
// （p = w·Aᵀp + l），故调用方若改了 ν/τ/加价，必须把同一个 w 传进来——
// 否则会出现"价格表按默认税制解、交易按新税制记"的口径分叉（开局利润率不再是 20%）。
func CalibrateOnlyScaledWedge(specs []model.Building, demandScale, priceMult, w float64) (*calibrate.Result, error) {
	if w <= 0 {
		w = 1
	}
	cal, err := calibrate.Run(specs, 1.0/6.0, w)
	if err != nil {
		return nil, err
	}
	if demandScale > 0 {
		cal.DemandScale = demandScale
	}
	goods := model.GoodSpecs()
	scaleOpeningPrices(goods, cal, specs, priceMult)
	return cal, nil
}

// scaleOpeningPrices 把开局价 P_init 整体放大 mult 倍（mult ≤ 0 或 = 1 时不动）。
//
// 【口径（§七 R31）】P_init 由加成价方程解出（openerMarkup = 1/6 ⇒ ≈1.2×P_cost）。
// 本函数同时缩放：
//
//	① `goods[i].Pinit` —— 市场初价（market.NewState 读它）；
//	② `cal.Pinit`      —— 报告口径；
//	③ `cal.Margins`    —— margin 的定义就是"P_init 下的售价相对实际成本"，
//	                      抬价即抬开局利润率（3× ⇒ 由统一的 20% 抬到 **38.9%~260%**：
//	                      成本里的**工资项不随价格缩放**，故劳动密集部门抬得最多）。
//
// **不动 P_cost**：零利润价由工资与配方反推（§3.4），是价格体系的地板基准，
// 也是债务上限的计价口径；动了它，实验就不再是"只改初始价格"。
//
// 需求锚 a = S₀·(P_init/P_cost)^ε 由调用方用**已缩放的** goods 计算，
// 故长期均衡价 P* = a/S 同倍放大——这正是"初始价格水平"这一条的作用面。
func scaleOpeningPrices(goods []model.Good, cal *calibrate.Result, specs []model.Building, mult float64) {
	if cal == nil || mult <= 0 || math.Abs(mult-1) <= 1e-12 {
		return
	}
	for i := range goods {
		if i >= len(cal.Pinit) {
			break
		}
		goods[i].Pinit *= mult
		cal.Pinit[i] = goods[i].Pinit
	}
	for i := range specs {
		if i >= len(cal.Margins) || !specs[i].Produces() {
			continue
		}
		cal.Margins[i] = calibrate.Margin(specs[i], cal.Pinit, cal.BuyerWedge)
	}
}

// CalibrateDefault 用契约默认参数执行标定，便于在测试与报告中直接调用。
func CalibrateDefault() (*calibrate.Result, error) {
	return CalibrateOnly(model.BuildingSpecs(1000), 0)
}
