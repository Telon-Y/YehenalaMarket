package sim

// 【R92 诊断】把 M7 的"量级问题"写成**可读的数字**，并用实验比较三种收口方案。
//
// 【为什么单独做这件事】用户对 M7 量级的反馈是"**没看懂**"。R68/R69/R87 的文字
// 解释没能说清，故本轮把三种方案**跑出来并排**，让人一眼看出差别：
//
//	A. 现状（profit 基数）        A_i = cap × max(0, 纯利)
//	B. 工资基数（R69 的 wagebill）A_i = cap × 工资总额 × max(0, 利润率)
//	C. 利润率封顶（R69 建议方向1）A_i = cap × 纯利，但利润率因子取 min(利润率, 上限)
//
// 三者都在**同一个稀缺局**（人口 50 万，R67 的对照口径）上跑，比较：
// 峰值溢价、峰值人均工资、以及"人均工资 ÷ 基准工资"这个倍数。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestM7OptionsCompared -v

import (
	"fmt"
	"math"
	"testing"
)

func TestM7OptionsCompared(t *testing.T) {
	auditEnabled(t)
	type variant struct {
		name      string
		base      string
		marginCap float64 // >0 时封顶利润率因子
	}
	variants := []variant{
		{"A 现状（纯利基数）", "profit", 0},
		{"B 工资基数（wagebill）", "wagebill", 0},
		{"C 纯利 + 利润率≤0.2", "profit", 0.2},
		{"D 纯利 + 利润率≤0.05", "profit", 0.05},
	}
	baseWage := 6.75 // 城镇基准工资（§5 城镇结构）
	fmt.Printf("%-26s %16s %18s %14s %12s\n",
		"方案", "峰值溢价", "峰值人均工资", "倍数(÷基准)", "峰值利润率")
	for _, v := range variants {
		st, err := New(Options{
			Population:           500_000, // 稀缺局（R67 的对照口径）
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			WageBidEnabled:       true,
			WageBidBase:          v.base,
			WageBidMarginCap:     v.marginCap,
		})
		if err != nil {
			t.Fatalf("%s: New: %v", v.name, err)
		}
		var maxPrem, maxWage, maxMargin float64
		for i := 0; i < 900; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", v.name, i, err)
			}
			for j := range st.Buildings {
				if p := st.WagePremium(j); p > maxPrem {
					maxPrem = p
				}
				if w := st.EffectiveWage(j); w > maxWage {
					maxWage = w
				}
				// 记录"参与抬价"的场地里出现过的最大利润率
				if st.WagePremium(j) > 0 {
					if m := math.Abs(st.Buildings[j].LastMargin); m > maxMargin {
						maxMargin = m
					}
				}
			}
		}
		fmt.Printf("%-26s %16.4f %18.4f %14.1f %12.4f\n",
			v.name, maxPrem, maxWage, maxWage/baseWage, maxMargin)
	}
	fmt.Printf("\n参照：§5 城镇基准工资 = %.2f 元/人/周期；§4.1 扩建阈值 10%%\n", baseWage)
	fmt.Printf("说明：'倍数'= 峰值人均工资 ÷ 基准工资。稀缺局的合理量级应与基准**同阶**，\n")
	fmt.Printf("      而不是 10 倍以上（10 倍意味着'工资'这个名义量失去可比性）。\n")
}
