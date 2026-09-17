// tools/zeroprofit —— 1.0 规模杠杆标定（T-1.0-20）分析脚本。
//
// 用途：用 1.0 文档 §3.1 初始价、§3.3 投入产出表、§5 雇佣工资表，
// 解出每种商品的"零利润价" P*_g，并给出 P*_g / 初始价 比值，
// 用于决定规模标定走哪条路（改价 / 改产出量 / 承认重定价）。
//
// 模型：单级建筑、满编、每 tick 结算。
//
//	收入_g   = 产出量_g × P_g
//	材料成本_g = Σ_i 投入量_i × P_i          （完全成本，投入品按其自身 P 计价）
//	工资成本_g = perLevelWorkers × 平均工资 × 份额(0.75×5 + 0.20×10 + 0.05×20)/100
//	零利润条件：P_g × 产出量_g = Σ_i 投入量_i × P_i + 工资成本_g
//
// 该线性方程组用不动点迭代求解（等价于按投入产出链逐轮累加完全成本）：
//
//	P_g ← (Σ_i req_i × P_i + unitLabor_g) / (1 − selfReq_g)
//
// 其中 req_i = 投入量_i / 产出量_g，selfReq_g 为自耗系数。
// 收敛条件为投入矩阵谱半径 < 1（即该经济是"生产性的"）。
//
// 运行：go run ./tools/zeroprofit
package main

import "fmt"

const (
	perLevelWorkers = 5000.0
	avgWage         = 0.75*5.0 + 0.20*10.0 + 0.05*20.0 // = 6.75
)

type good struct {
	name    string
	initP   float64 // 文档 §3.1 初始价
	output  float64 // 单级每 tick 产出
	inputs  map[string]float64
	hasWage bool // 该建筑是否雇工（建造部门也雇工）
}

// 文档 §3.1 + §3.3 + §5 的原始数据。顺序按生产链自下而上。
var goods = []good{
	{"grain", 2400, 50, nil, true},
	{"food", 4000, 45, map[string]float64{"grain": 40}, true},
	{"fabric", 5000, 45, nil, true},
	{"clothes", 12000, 100, map[string]float64{"fabric": 60}, true},
	{"luxury", 40000, 30, map[string]float64{"fabric": 25}, true},
	{"coal", 4000, 60, map[string]float64{"tools": 15, "coal": 15}, true},
	{"iron", 4000, 60, map[string]float64{"tools": 15, "coal": 15}, true},
	{"steel", 8000, 90, map[string]float64{"iron": 60, "coal": 30}, true},
	{"tools", 4000, 80, map[string]float64{"steel": 20}, true},
	{"housing", 1600, 60, map[string]float64{"steel": 5, "tools": 5}, true},
	{"power", 24000, 15, map[string]float64{"steel": 25, "iron": 25, "tools": 20}, true},
}

// 建造成本（建造力），用于估算"是否值得建"，非零利润价必需。
var buildCost = map[string]float64{
	"grain": 200, "food": 600, "fabric": 200, "clothes": 600, "luxury": 600,
	"coal": 600, "iron": 600, "steel": 800, "tools": 800, "housing": 800, "power": 100,
}

func main() {
	wageBill := perLevelWorkers * avgWage // 33,750 元/级/tick

	// 单位产出的工资含量
	unitLabor := make(map[string]float64)
	idx := make(map[string]int)
	for i, g := range goods {
		idx[g.name] = i
		if g.hasWage {
			unitLabor[g.name] = wageBill / g.output
		}
	}

	// 初始价向量
	P := make(map[string]float64)
	for _, g := range goods {
		P[g.name] = g.initP
	}

	// 不动点迭代解零利润价
	const tol = 1e-10
	for iter := 0; iter < 5000; iter++ {
		maxDelta := 0.0
		next := make(map[string]float64)
		for _, g := range goods {
			matPerUnit := 0.0
			selfReq := 0.0
			for in, q := range g.inputs {
				coef := q / g.output
				if in == g.name {
					selfReq = coef
				} else {
					matPerUnit += coef * P[in]
				}
			}
			var v float64
			if 1.0-selfReq <= 1e-12 {
				v = 0 // 退化：该建筑自耗率 >= 1，无法正利润生产
			} else {
				v = (matPerUnit + unitLabor[g.name]) / (1.0 - selfReq)
			}
			next[g.name] = v
			if d := abs(v - P[g.name]); d > maxDelta {
				maxDelta = d
			}
		}
		P = next
		if maxDelta < tol {
			break
		}
	}

	// 报告
	fmt.Printf("参数：每级 %v 人，平均工资 %.4f 元，工资总额 %.0f 元/级/tick\n\n",
		perLevelWorkers, avgWage, wageBill)

	fmt.Println("=== 零利润价 P*_g 与初始价比值（完全成本口径，含中间投入） ===")
	fmt.Printf("%-9s %10s %12s %12s %10s %10s\n",
		"商品", "初始价", "零利润价P*", "P*/初始价", "工资成本", "材料成本")
	fmt.Println(repeat('-', 70))
	var loRatio, hiRatio float64 = 1e18, -1e18
	var loName, hiName string
	for _, g := range goods {
		// 重算该商品的成本构成（在收敛后的价格下）
		mat := 0.0
		self := 0.0
		for in, q := range g.inputs {
			if in == g.name {
				self += q / g.output
			} else {
				mat += (q / g.output) * P[in]
			}
		}
		pstar := P[g.name]
		ratio := pstar / g.initP
		fmt.Printf("%-9s %10.0f %12.1f %12.3f %10.0f %10.0f\n",
			g.name, g.initP, pstar, ratio, unitLabor[g.name], mat)
		if ratio < loRatio {
			loRatio, hiRatio = ratio, hiRatio
			loRatio, loName = ratio, g.name
		}
		if ratio > hiRatio {
			hiRatio, hiName = ratio, g.name
		}
	}

	fmt.Printf("\n比值范围：%.3f (%s) ~ %.3f (%s)\n", loRatio, loName, hiRatio, hiName)
	fmt.Println("含义：比值 < 1 表示初始价高于零利润价（有利可图，会扩建、价格需下行）；")
	fmt.Println("      比值 > 1 表示初始价低于零利润价（亏本，会缩编、价格需上行）。")

	// 初始价下的隐含利润率
	fmt.Println("\n=== 初始价下的隐含利润率（成本加成口径） ===")
	fmt.Printf("%-9s %12s %12s %12s %12s\n", "商品", "收入", "成本合计", "利润", "利润率")
	fmt.Println(repeat('-', 62))
	for _, g := range goods {
		mat := 0.0
		self := 0.0
		for in, q := range g.inputs {
			if in == g.name {
				self += q / g.output
			} else {
				mat += (q / g.output) * P[in]
			}
		}
		// 注意：此处用"初始价"评估收入与材料成本，才能反映开局真实激励
		matInit := 0.0
		for in, q := range g.inputs {
			if in != g.name {
				matInit += q * initPrice(in)
			}
		}
		rev := g.initP * g.output
		cost := matInit + unitLabor[g.name]*g.output
		profit := rev - cost
		margin := 0.0
		if cost > 1e-9 {
			margin = profit / cost
		}
		flag := ""
		if margin > 0.10 {
			flag = "  ← 触发扩建(>10%)"
		} else if margin < 0 {
			flag = "  ← 亏损(触发解雇+缩编)"
		}
		fmt.Printf("%-9s %12.0f %12.0f %12.0f %11.1f%%%s\n",
			g.name, rev, cost, profit, margin*100, flag)
		_ = self
	}
}

func initPrice(name string) float64 {
	for _, g := range goods {
		if g.name == name {
			return g.initP
		}
	}
	return 0
}

func abs(x float64) float64 {
	if x < 0 {
		return -x
	}
	return x
}

func repeat(c rune, n int) string {
	s := make([]rune, n)
	for i := range s {
		s[i] = c
	}
	return string(s)
}
