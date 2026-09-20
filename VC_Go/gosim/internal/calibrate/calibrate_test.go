package calibrate

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// testWedge 返回契约默认的**买家加载系数** w（§4.5.6）：
// w = (1+仓库加价)(1+消费税) = 1.05 × 1.025 = 1.07625。
//
// 【2026-09-19 第 16 轮】仓库落地后，§3.4 的价格方程变成 p = w·Aᵀp + l，
// 故本文件的所有标定与利润率断言都必须带上它；用 1.0 可复现仓库落地前的历史口径。
func testWedge() float64 { return model.DefaultParams().BuyerWedge() }

// TestSpectralRadiusAndPriceTable 校验契约 §3.4 的两项硬断言：
//
//	① A 的谱半径 < 1，且 w·A 的谱半径 < 1（存在正的零利润价）；
//	② 解出的 11 个零利润价与 §3.1 表格逐项吻合。
//
// 这三张表的自洽性已由 tools/leontief_probe.js 独立验证过，
// 此处把结论固化成 Go 测试，防止后续改动破坏它。
//
// 【前值 → 后值（2026-09-19 第 16 轮）】仓库落地前 w = 1，谱半径 0.5000、
// 价格表 {675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250}；
// 落地后 w = 1.07625，w·A 的谱半径 0.5381、价格表整体上移（下游越多升得越多）。
//
// 【前值 → 后值（2026-09-19 第 20 轮）】农业劳动结构改为"劳工/农民 7 元/工程师"
// 后，谷物与棉花的每级工资由 33,750 降到 28,250，故其 P_ref 下移。
func TestSpectralRadiusAndPriceTable(t *testing.T) {
	specs := model.BuildingSpecs(1000)
	res, err := Run(specs, 1.0/6.0, testWedge())
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	if res.SpectralRadius >= 1 {
		t.Fatalf("谱半径 = %.6f，必须 < 1", res.SpectralRadius)
	}
	if math.Abs(res.SpectralRadius-0.5) > 1e-3 {
		t.Errorf("A 的谱半径 = %.6f，期望 0.5000（A 本身不随仓库变化）", res.SpectralRadius)
	}
	if got := res.BuyerWedge * res.SpectralRadius; math.Abs(got-0.538125) > 1e-4 {
		t.Errorf("w·A 的谱半径 = %.6f，期望 0.538125（w = %.5f）", got, res.BuyerWedge)
	}

	want := []float64{565, 1290.517, 627.778, 742.888, 1688.038, 1076.775, 1076.775, 1533.879, 834.584, 774.922, 8130.49}
	for i, w := range want {
		if math.Abs(res.Pcost[i]-w) > 0.5 {
			t.Errorf("Pcost[%d] = %.4f，契约表格（含买家加载系数）为 %.3f", i, res.Pcost[i], w)
		}
	}
}

// TestZeroProfitPriceIncludesLaborAndMaterials 核验零利润价的成本构成（用户核验要求，2026-09-19）：
//
//	「确认先前的零利润算法包括了人工成本和原料成本」
//
// 逐商品断言恒等式
//
//	p_j = l_j（人工成本）+ w·Σ_i A[i][j]·p_i（原料/中间投入成本，按买家实付价）
//
// 其中 l_j = LaborPerLevel × 平均工资 ÷ q_j（§5 工资表），A[i][j] = 每级投入量 ÷ q_j（§3.3 配方），
// w = 买家加载系数（§4.5.6：仓库加价 + 消费税都落在买家身上）。
// 若哪天有人把原料项、人工项或 w 从方程里去掉，本测试立刻失败。
//
// 独立复算（含逐项金额与 Neumann 逐层分解）：tools/zeroprofit_cost_probe.js → out/zeroprofit_cost_probe.txt
func TestZeroProfitPriceIncludesLaborAndMaterials(t *testing.T) {
	specs := model.BuildingSpecs(1000)
	res, err := Run(specs, 1.0/6.0, testWedge())
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	l := UnitLaborCost(specs)
	w := res.BuyerWedge

	for j, b := range specs {
		if !b.Produces() {
			continue
		}
		labor := l[j]
		var material float64
		for i := 0; i < model.Goods; i++ {
			material += res.A[i][j] * res.Pcost[i]
		}
		if labor <= 0 {
			t.Errorf("%s：人工成本 = %.3f，应 > 0（§5 工资表必须进入 l）", b.Name, labor)
		}
		if len(b.Recipe.Inputs) > 0 && material <= 0 {
			t.Errorf("%s：配方有 %d 项投入，但原料成本 = %.3f，应 > 0（§3.3 必须进入 Aᵀp）",
				b.Name, len(b.Recipe.Inputs), material)
		}
		sum := labor + w*material
		if rel := math.Abs(sum-res.Pcost[j]) / res.Pcost[j]; rel > 1e-9 {
			t.Errorf("%s：人工 %.3f + w·原料 %.3f（w=%.5f）= %.3f，但解出 P_cost = %.3f（相对差 %.2e）",
				b.Name, labor, w, w*material, sum, res.Pcost[j], rel)
		}
		t.Logf("%-8s 人工 %9.3f + w·原料 %9.3f = P_cost %9.3f（人工占比 %5.1f%%）",
			b.Name, labor, w*material, res.Pcost[j], labor/res.Pcost[j]*100)
	}

	// 工资口径本身也要核对（§5 第 20 轮起它**逐建筑**不同）：
	//
	//	城镇类 5,000 × (0.75×5 + 0.20×10 + 0.05×20) = 33,750 元/级
	//	农业类 5,000 × (0.75×5 + 0.20× 7 + 0.05×10) = 28,250 元/级
	const wantUrban, wantAgri = 33750.0, 28250.0
	urban := specs[1] // 加工食品厂 = 城镇类
	if got := urban.WagePerLevel(); math.Abs(got-wantUrban) > 1e-9 {
		t.Errorf("城镇类每级工资 = %.4f，应为 %.0f（§5 的 5,000 × 6.75）", got, wantUrban)
	}
	agri := specs[0] // 谷物农场 = 农业类
	if got := agri.WagePerLevel(); math.Abs(got-wantAgri) > 1e-9 {
		t.Errorf("农业类每级工资 = %.4f，应为 %.0f（§5 的 5,000 × 5.65：20%% 农民 7 元）", got, wantAgri)
	}
	if got := l[0] * specs[0].Recipe.Qty; math.Abs(got-wantAgri) > 1e-9 {
		t.Errorf("谷物的人工项 l×q = %.4f，应为 %.0f（农业结构）", got, wantAgri)
	}
	if got := l[1] * specs[1].Recipe.Qty; math.Abs(got-wantUrban) > 1e-9 {
		t.Errorf("加工食品的人工项 l×q = %.4f，应为 %.0f（城镇结构）", got, wantUrban)
	}
}

// TestOpenerMarginIsExactly20Pct 校验 §3.4 的核心声明：
// 加成价方程的解使每种建筑在【实际成本基】下利润率恰好为 m/(1−m) = 20%。
//
// 这是最容易实现错的一处（详见契约 §3.4 的口径注）：
// 若误把"20% 利润率价"实现成 P_init = 1.2·Pcost，则钢只有 4.74%、
// 建造部门只有 5.45%，低于 §4.1 的 10% 扩建阈值，t=0 不会扩建。
func TestOpenerMarginIsExactly20Pct(t *testing.T) {
	specs := model.BuildingSpecs(1000)
	res, err := Run(specs, 1.0/6.0, testWedge())
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	const want = 0.20
	for _, b := range specs {
		if !b.Produces() {
			continue
		}
		m := Margin(b, res.Pinit, res.BuyerWedge)
		if math.Abs(m-want) > 1e-3 {
			t.Errorf("%s: 实际成本基利润率 = %.4f%%，期望 %.2f%%", b.Name, m*100, want*100)
		}
		if m <= 0.10 {
			t.Errorf("%s: 利润率 %.4f%% 未超过 §4.1 的 10%% 扩建阈值", b.Name, m*100)
		}
	}
}

// TestNaiveOnePointTwoIsWrong 反证 §3.4 明确禁止的错误口径。
//
// 断言：P_init = 1.2·Pcost 会让部分建筑的利润率跌到 10% 阈值以下。
// 若这条测试失败（即所有建筑都 ≥10%），说明配方或工资被我改动了，
// 契约 §3.4 的口径注需要重新核对。
func TestNaiveOnePointTwoIsWrong(t *testing.T) {
	specs := model.BuildingSpecs(1000)
	res, err := Run(specs, 1.0/6.0, testWedge())
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	naive := make([]float64, len(res.Pcost))
	for i := range naive {
		naive[i] = 1.2 * res.Pcost[i]
	}
	below := 0
	for _, b := range specs {
		if !b.Produces() {
			continue
		}
		if Margin(b, naive, res.BuyerWedge) <= 0.10 {
			below++
		}
	}
	if below == 0 {
		t.Error("统一乘 1.2 应使部分建筑低于 10% 阈值，实测无——契约 §3.4 的对照表需要重算")
	}
}

// TestLeontiefIdentity 校验完全需求矩阵的恒等式：
// 若 Y = (I−A)⁻¹·f，则 Y − A·Y 必须等于 f（净供给等于最终需求）。
// 这是"净供给不是总产出"这一实现要点的守门测试——把总产出当净供给
// 会高估需求标定常数 a，进而让价格方程与实际成交量脱节。
func TestLeontiefIdentity(t *testing.T) {
	specs := model.BuildingSpecs(1000)
	res, err := Run(specs, 1.0/6.0, testWedge())
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	f := PerCapitaFinalDemand(10, 1.0)
	y := res.TotalOutput(f)
	for i := 0; i < model.Goods; i++ {
		var interm float64
		for j := 0; j < model.Goods; j++ {
			interm += res.A[i][j] * y[j]
		}
		net := y[i] - interm
		if math.Abs(net-f[i]) > 1e-6*math.Max(1, math.Abs(f[i])) {
			t.Errorf("商品 %d: 净供给 %.6f 与最终需求 %.6f 不相等（恒等式被破坏）", i, net, f[i])
		}
	}
}

// TestDemandScaleIsPositive 校验三表联合标定解的合理性。
//
// 结论（已由 tools/calibration_probe.js 独立算过）：该比值与人口无关，
// 因此在 1.0 契约的参数下它是一个固定常数，约 0.94 量级。
// 它偏离 1 说明"工资水平"与"§6.3 需求量表"不在同一量纲上，
// 这正是契约必须做三表联合标定的原因。
func TestDemandScaleIsPositive(t *testing.T) {
	specs := model.BuildingSpecs(1000)
	res, err := Run(specs, 1.0/6.0, testWedge())
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	if res.DemandScale <= 0 {
		t.Fatalf("需求缩放系数 = %.6f，应为正", res.DemandScale)
	}
	if res.DemandScale > 2 || res.DemandScale < 0.5 {
		t.Errorf("需求缩放系数 = %.6f，偏离 1 过多，请核对 §5 工资与 §6.3 需求量表的量纲", res.DemandScale)
	}
}
