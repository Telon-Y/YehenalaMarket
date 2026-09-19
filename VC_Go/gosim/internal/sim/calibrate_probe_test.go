package sim

// calibrate_probe_test.go —— §3.1/§3.4 价格表的**再生成探针**。
//
// 用途：`model.GoodSpecs()` 里的 P_ref / P_init 两列是**标定解的取整值**，
// 契约 §3.1 的表格与之同源。改动 §3.3 配方、§5 工资表或 §4.5.6 的买家加载系数
// （仓库加价 / 消费税）之后，这两列都会变，必须重算并同步三处：
//
//	model.GoodSpecs()            （实现用的表）
//	docs/1.0 生产与市场模拟.md §3.1（契约用的表）
//	internal/calibrate/calibrate_test.go 的期望值
//
// 运行：go test ./internal/sim/ -run TestPrintCalibrationTable -v
//
// 【为什么不做成自动同步】标定需要 specs，而 specs 属于 model 包；
// 让 model 反向依赖 calibrate 会形成循环。故采用"显式重算 + 手工同步 + 探针可复核"的做法。

import (
	"fmt"
	"testing"

	"yehenala/market/internal/model"
)

// TestPrintCalibrationTable 打印当前参数下的完整标定表（含买家加载系数）。
func TestPrintCalibrationTable(t *testing.T) {
	cal, err := CalibrateDefault()
	if err != nil {
		t.Fatalf("标定失败: %v", err)
	}
	p := model.DefaultParams()
	fmt.Printf("w = %.6f（加价 %.4f × 消费税 %.4f）\n", cal.BuyerWedge, p.WarehouseMarkup, p.ConsumeTaxRate)
	fmt.Printf("A 的谱半径 = %.6f；w·A 的谱半径 = %.6f（必须 < 1）\n",
		cal.SpectralRadius, cal.BuyerWedge*cal.SpectralRadius)
	fmt.Printf("三表联合标定系数 k = %.6f\n", cal.DemandScale)
	names := []string{"谷物", "加工食品", "织物", "服装", "高档服装", "煤", "铁", "钢", "工具", "住房", "建造力"}
	fmt.Printf("%-10s %14s %14s %10s\n", "商品", "P_ref", "P_init", "开局利润率")
	for i, n := range names {
		fmt.Printf("%-10s %14.3f %14.3f %9.4f%%\n", n, cal.Pcost[i], cal.Pinit[i], cal.Margins[i]*100)
	}
	// 与硬编码表逐项对照，偏差过大即提示需要同步。
	hard := model.GoodSpecs()
	for i := range names {
		if rel := (cal.Pcost[i] - hard[i].Pcost) / hard[i].Pcost; rel > 1e-4 || rel < -1e-4 {
			fmt.Printf("⚠ %s：P_ref %.3f 与 model.GoodSpecs 的 %.3f 偏差 %.2f%%——需要同步价格表\n",
				names[i], cal.Pcost[i], hard[i].Pcost, rel*100)
		}
	}
}
