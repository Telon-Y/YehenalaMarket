package calibrate

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// TestSpectralRadiusAndPriceTable 校验契约 §3.4 的两项硬断言：
//
//	① A 的谱半径 < 1（存在正的零利润价）；
//	② 解出的 11 个零利润价与 §3.1 表格逐项吻合。
//
// 这三张表的自洽性已由 tools/leontief_probe.js 独立验证过，
// 此处把结论固化成 Go 测试，防止后续改动破坏它。
func TestSpectralRadiusAndPriceTable(t *testing.T) {
	specs := model.BuildingSpecs(1000, 400)
	res, err := Run(specs, 1.0/6.0)
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	if res.SpectralRadius >= 1 {
		t.Fatalf("谱半径 = %.6f，必须 < 1", res.SpectralRadius)
	}
	if math.Abs(res.SpectralRadius-0.5) > 1e-3 {
		t.Errorf("谱半径 = %.6f，期望 0.5000", res.SpectralRadius)
	}

	want := []float64{675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250}
	for i, w := range want {
		if math.Abs(res.Pcost[i]-w) > 0.5 {
			t.Errorf("Pcost[%d] = %.4f，契约表格为 %.0f", i, res.Pcost[i], w)
		}
	}
}

// TestOpenerMarginIsExactly20Pct 校验 §3.4 的核心声明：
// 加成价方程的解使每种建筑在【实际成本基】下利润率恰好为 m/(1−m) = 20%。
//
// 这是最容易实现错的一处（详见契约 §3.4 的口径注）：
// 若误把"20% 利润率价"实现成 P_init = 1.2·Pcost，则钢只有 4.74%、
// 建造部门只有 5.45%，低于 §4.1 的 10% 扩建阈值，t=0 不会扩建。
func TestOpenerMarginIsExactly20Pct(t *testing.T) {
	specs := model.BuildingSpecs(1000, 400)
	res, err := Run(specs, 1.0/6.0)
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	const want = 0.20
	for _, b := range specs {
		if b.IsFinance {
			continue
		}
		m := Margin(b, res.Pinit)
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
	specs := model.BuildingSpecs(1000, 400)
	res, err := Run(specs, 1.0/6.0)
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	naive := make([]float64, len(res.Pcost))
	for i := range naive {
		naive[i] = 1.2 * res.Pcost[i]
	}
	below := 0
	for _, b := range specs {
		if b.IsFinance {
			continue
		}
		if Margin(b, naive) <= 0.10 {
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
	specs := model.BuildingSpecs(1000, 400)
	res, err := Run(specs, 1.0/6.0)
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
	specs := model.BuildingSpecs(1000, 400)
	res, err := Run(specs, 1.0/6.0)
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
