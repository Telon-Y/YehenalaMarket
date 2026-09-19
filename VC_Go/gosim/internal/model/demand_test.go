package model

import (
	"math"
	"testing"
)

// TestDemandTableMatchesCurves 锁定"离散表 ↔ 生成曲线"的一致性。
//
// 契约 §6.3 修订后，基础食物与住宅不再是线性插值，而是由两条曲线生成：
//
//	基础食物  D(w) = 420 / (1 + exp(-0.25(w-10)))     拐点在财富 10
//	住宅      D(w) = C - (C-20)·exp(-b(w-5))          C=311.35, b=0.115726
//
// 表里存的是这两条曲线在整数档上的取整值。若有人只改公式不改表（或反之），
// 本测试会失败——这正是"实现与契约走样"最常见的形态。
func TestDemandTableMatchesCurves(t *testing.T) {
	for w := 5; w <= 20; w++ {
		got := DemandAt(float64(w))

		wantFood := math.Round(FoodDemandAt(float64(w)))
		if math.Abs(got[GroupBasicFood]-wantFood) > 1e-9 {
			t.Errorf("财富 %d 基础食物: 表值 %.0f，曲线取整 %.0f", w, got[GroupBasicFood], wantFood)
		}

		wantHousing := math.Round(HousingDemandAt(float64(w)))
		if math.Abs(got[GroupHousing]-wantHousing) > 1e-9 {
			t.Errorf("财富 %d 住宅: 表值 %.0f，曲线取整 %.0f", w, got[GroupHousing], wantHousing)
		}
	}
}

// TestFoodCurveShape 校验基础食物的 S 形特征：
// 单调递增、拐点恰在财富 10（此处一阶差分最大）、D(10) = 210 保留原锚点。
func TestFoodCurveShape(t *testing.T) {
	if math.Abs(FoodDemandAt(FoodLogisticMid)-210) > 1e-9 {
		t.Errorf("D(10) = %.4f，应等于原锚点 210", FoodDemandAt(FoodLogisticMid))
	}

	prev := FoodDemandAt(5)
	maxDiff, maxAt := -1.0, 0.0
	for w := 6.0; w <= 20; w++ {
		cur := FoodDemandAt(w)
		if cur <= prev {
			t.Errorf("财富 %.0f 处非单调递增：%.4f → %.4f", w, prev, cur)
		}
		if d := cur - prev; d > maxDiff {
			maxDiff, maxAt = d, w
		}
		prev = cur
	}
	if maxAt != FoodLogisticMid {
		t.Errorf("一阶差分最大处 = 财富 %.0f，契约要求拐点在财富 %.0f", maxAt, FoodLogisticMid)
	}
}

// TestHousingCurveShape 校验住宅饱和型指数的三个锚点与饱和上界。
//
// 特别锁定 D(10) = 148 —— 这是契约修订时明确要求的"原表 74 的两倍"。
//
// 容差说明：C 与 b 是截断到 2 / 6 位小数的常数，代回公式后 D(10) 与 148 的
// 偏差在 1e-3 量级，故锚点校验用 0.05（远小于表值取整的 0.5 步长），
// 仍能抓住"参数被改错"这类问题。
func TestHousingCurveShape(t *testing.T) {
	if math.Abs(HousingDemandAt(5)-20) > 0.05 {
		t.Errorf("D(5) = %.4f，应为 20（与原表左端一致）", HousingDemandAt(5))
	}
	if math.Abs(HousingDemandAt(10)-148) > 0.05 {
		t.Errorf("D(10) = %.4f，应为 148（原表 74 的两倍）", HousingDemandAt(10))
	}
	if math.Abs(HousingDemandAt(20)-260) > 0.05 {
		t.Errorf("D(20) = %.4f，应为 260", HousingDemandAt(20))
	}

	// 单调递增，且恒不超过饱和值
	prev := HousingDemandAt(5)
	for w := 6.0; w <= 20; w++ {
		cur := HousingDemandAt(w)
		if cur <= prev {
			t.Errorf("财富 %.0f 处非单调递增", w)
		}
		if cur >= HousingSaturation {
			t.Errorf("财富 %.0f 处 %.4f 达到或超过饱和值 %.2f", w, cur, HousingSaturation)
		}
		prev = cur
	}

	// 增量应单调递减（"先陡起后长尾饱和"）
	prevDiff := HousingDemandAt(6) - HousingDemandAt(5)
	for w := 7.0; w <= 20; w++ {
		d := HousingDemandAt(w) - HousingDemandAt(w-1)
		if d > prevDiff+1e-9 {
			t.Errorf("财富 %.0f 处增量 %.4f 大于前一档 %.4f，与饱和型形态不符", w, d, prevDiff)
		}
		prevDiff = d
	}
}

// TestDiscreteTableBoundaries 校验边界规则（§6.3：低于 5 取 5，达到或超过 20 取 20）。
func TestDiscreteTableBoundaries(t *testing.T) {
	low := DemandAt(0)
	if low != demandTableLow[0] {
		t.Errorf("财富档 0 的取值未回落到下限")
	}
	if DemandAt(-5) != demandTableLow[0] {
		t.Errorf("负财富档的取值未回落到下限")
	}
	high := DemandAt(20)
	if high != demandTableHigh {
		t.Errorf("财富档 20 的取值与上限行不一致")
	}
	if DemandAt(100) != demandTableHigh {
		t.Errorf("超高财富档的取值未回落到上限")
	}
}

// TestDiscreteVsInterpolatedAgreeOnIntegerTiers 校验两个入口在整档处相等。
func TestDiscreteVsInterpolatedAgreeOnIntegerTiers(t *testing.T) {
	for w := 5; w <= 20; w++ {
		a := DemandAt(float64(w))
		b := InterpolatedDemandAt(float64(w))
		for k := 0; k < 4; k++ {
			if math.Abs(a[k]-b[k]) > 1e-9 {
				t.Errorf("财富 %d 第 %d 组: 离散 %.4f ≠ 插值 %.4f", w, k, a[k], b[k])
			}
		}
	}
}

// TestContractAnchorsPreserved 记录哪些契约原值被保留、哪些被本次修订改动。
//
// 保留：简朴衣物与标准衣物（表格第 0、2 列）在三个原锚点处与原表逐项相等；
// 改动：基础食物（原恒为 210）与住宅（原 74）在财富 5、20 处已不同。
// 这条测试把"改了什么"固化下来，避免日后被误认为实现偏差。
func TestContractAnchorsPreserved(t *testing.T) {
	for i, w := range ContractAnchorTiers {
		got := DemandAt(w)
		anchor := ContractDemandAnchors[i]

		// 简朴衣物、标准衣物：必须与原表一致
		if math.Abs(got[0]-anchor[0]) > 1e-9 {
			t.Errorf("财富 %.0f 简朴衣物: %.0f，契约原表 %.0f（不应改动）", w, got[0], anchor[0])
		}
		if math.Abs(got[2]-anchor[2]) > 1e-9 {
			t.Errorf("财富 %.0f 标准衣物: %.0f，契约原表 %.0f（不应改动）", w, got[2], anchor[2])
		}
	}

	// 基础食物：仅财富 10 与原表相同（该锚点被保留），5 与 20 已改动
	if math.Abs(DemandAt(10)[GroupBasicFood]-210) > 1e-9 {
		t.Error("财富 10 的基础食物应保留原锚点 210")
	}
	if DemandAt(5)[GroupBasicFood] == 210 {
		t.Error("财富 5 的基础食物仍为 210 —— S 形曲线未生效")
	}

	// 住宅：财富 10 应为原表的两倍
	if math.Abs(DemandAt(10)[GroupHousing]-148) > 1e-9 {
		t.Errorf("财富 10 的住宅应为 148（原表 74 的两倍），实际 %.0f", DemandAt(10)[GroupHousing])
	}
}
