package sim

import (
	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/model"
)

// CalibrateOnly 只执行 §3.4 的标定，不构造仿真状态。
//
// demandScale 为 0 时采用 calibrate 解出的三表联合标定系数。
// 该函数供 CLI 的 -calibrate-only 与测试使用。
func CalibrateOnly(specs []model.Building, demandScale float64) (*calibrate.Result, error) {
	cal, err := calibrate.Run(specs, 1.0/6.0)
	if err != nil {
		return nil, err
	}
	if demandScale > 0 {
		cal.DemandScale = demandScale
	}
	return cal, nil
}

// CalibrateDefault 用契约默认参数执行标定，便于在测试与报告中直接调用。
func CalibrateDefault() (*calibrate.Result, error) {
	return CalibrateOnly(model.BuildingSpecs(1000, 400), 0)
}
