package sim

// 【1.2 M7 验证辅助探针】寻找"市场用工 > 人口"的稀缺对照场景。
//
// 目的：M7 工资竞标只在**缺员**时抬价（裁决①）。默认参数下劳动力**结构性过剩**
// （失业 ~18 万），溢价恒为 0 ⇒ 抬价路径从未被验证。本探针扫描几组参数，
// 找出真正出现稀缺的组合，供 TestAuditWageBidScarcity 使用。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestScarceProbe -v

import (
	"fmt"
	"testing"
)

func TestScarceProbe(t *testing.T) {
	auditEnabled(t)
	type cfg struct {
		name string
		pop  float64
		pil  float64
		fin  float64
		fl   float64
		arbl float64
	}
	cases := []cfg{
		{"base", 5_000_000, -1, 1000, 1000, 0},
		{"pop1m", 1_000_000, -1, 1000, 1000, 0},
		{"pop500k", 500_000, -1, 1000, 1000, 0},
		{"pop1m-fin50k", 1_000_000, -1, 50000, 1000, 0},
		{"pop1m-pil50", 1_000_000, 50, 1000, 1000, 0},
		{"pop2m-pil200-fin20k", 2_000_000, 200, 20000, 1000, 0},
	}
	for _, c := range cases {
		opt := Options{
			Population:           c.pop,
			WealthTier:           10,
			FinanceLaborPerLevel: c.fin,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  c.pil,
			WageBidEnabled:       true,
		}
		st, err := New(opt)
		if err != nil {
			t.Fatalf("%s: New: %v", c.name, err)
		}
		if c.arbl > 0 {
			st.Params.ArableCap = c.arbl
		}
		var firstShort, firstPrem int = -1, -1
		var maxRatio, maxPrem float64
		for i := 0; i < 600; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step: %v", c.name, err)
			}
			if st.LaborMarketRatio > maxRatio {
				maxRatio = st.LaborMarketRatio
			}
			if firstShort < 0 {
				for j := range st.wageShortTick {
					if st.wageShortTick[j] {
						firstShort = i
						break
					}
				}
			}
			for j := range st.Buildings {
				if p := st.WagePremium(j); p > maxPrem {
					maxPrem = p
					if firstPrem < 0 {
						firstPrem = i
					}
				}
			}
		}
		var market float64
		for i, b := range st.buildingSpecs() {
			if i < len(st.Buildings) {
				market += st.Buildings[i].Level * st.Buildings[i].HireRate * b.LaborPerLevel
			}
		}
		fmt.Printf("%-22s 人口 %10.0f 市场用工 %10.0f 比值 %.4f 失业 %9.0f "+
			"首缺员 tick %5d 首溢价 tick %5d 最大溢价 %.6f 最大配给比 %.4f\n",
			c.name, st.Population, market, market/st.Population, st.Unemployed,
			firstShort, firstPrem, maxPrem, maxRatio)
	}
}
