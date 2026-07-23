// 叶赫那拉市场模拟 - Go 完整翻译版（最终修正）
// 用法: go run main.go
package main

import (
	"encoding/csv"
	"fmt"
	"math"
	"os"
	"sort"
	"strconv"
)

const (
	TOTAL_STEPS = 6000
	AI_INTERVAL = 1
	NUM_GOODS   = 11
)

var commodityNames = []string{
	"谷物", "加工食品", "织物", "服装", "高档服装",
	"煤", "铁", "钢", "工具", "住房", "建造力",
}

type BuildingType int

const (
	FARM_GRAIN BuildingType = iota
	FOOD_PROC
	COTTON
	CLOTHES
	LUXURY_CLOTHES
	COAL_MINE
	IRON_MINE
	STEEL_MILL
	TOOL_FACT
	HOUSING
	CONST_DEPT
	TYPE_COUNT
)

type ConsGroup int

const (
	GRP_SIMPLE_CLOTHES ConsGroup = iota
	GRP_BASIC_FOOD
	GRP_STANDARD_CLOTHES
	GRP_HOUSING
	GROUP_COUNT
)

type BuildingTemplate struct {
	Name         string
	OutputGood   int
	OutputRate   float64
	Inputs       [NUM_GOODS]float64
	LaborPerUnit float64
}

func (bt *BuildingTemplate) GetUnitCost(prices []float64, wageRate float64) float64 {
	cost := bt.LaborPerUnit * wageRate
	for g := 0; g < NUM_GOODS; g++ {
		cost += prices[g] * bt.Inputs[g]
	}
	return cost
}

func (bt *BuildingTemplate) GetProfitFactor(prices []float64, wageRate float64) float64 {
	unitCost := bt.GetUnitCost(prices, wageRate)
	if unitCost < 1e-6 {
		return 1.0
	}
	margin := prices[bt.OutputGood] - unitCost
	ratio := margin / unitCost
	factor := 1.0 + ratio*2.0
	if factor < 0.2 {
		factor = 0.2
	}
	if factor > 1.0 {
		factor = 1.0
	}
	return factor
}

type ConstructionOrder struct {
	TypeIndex     int
	TotalCost     float64
	RemainingCost float64
}

type MarketSimulation struct {
	goodIndex      map[string]int
	prices         []float64
	v              []float64
	m              []float64
	b              []float64
	referencePrice []float64
	averageWage    float64
	dt             float64
	population     float64
	laborPerCapita float64
	maxLabor       float64
	inertiaCoeff   float64
	dampRatio      float64

	buildingTypeNames []string
	buildingTemplates []BuildingTemplate
	buildingCost      []float64

	buildingCounts         []int
	constructionQueue      []ConstructionOrder
	avgProfitRates         []float64
	smoothedProfitRate     []float64
	employmentRatio        []float64
	cashPools              []float64
	consecutiveLowEmpWeeks []int
	stepCount              int

	maxTotalFarms int
	maxCoalMines  int
	maxIronMines  int
	maxConstDept  int

	satisfaction float64

	demandTable [3][4]float64
	groupGoods  [][]int

	// 历史记录
	priceHist      [][]float64
	outputHist     [][]float64
	profitRateHist [][]float64
	buildingHist   [][]int
	gdpHist        []float64
	cashPoolHist   [][]float64
	populationHist []float64
}

func NewMarketSimulation() *MarketSimulation {
	sim := &MarketSimulation{}
	sim.goodIndex = make(map[string]int)
	for i, name := range commodityNames {
		sim.goodIndex[name] = i
	}

	sim.prices = make([]float64, NUM_GOODS)
	sim.v = make([]float64, NUM_GOODS)
	sim.m = make([]float64, NUM_GOODS)
	sim.b = make([]float64, NUM_GOODS)
	sim.referencePrice = make([]float64, NUM_GOODS)
	for i := 0; i < NUM_GOODS; i++ {
		sim.prices[i] = 1.0
		sim.v[i] = 0.0
		sim.m[i] = 1.0
		sim.b[i] = 0.15
	}
	referenceVals := []float64{5.0, 10.0, 5.0, 12.0, 25.0, 8.0, 8.0, 15.0, 20.0, 30.0, 15.0}
	copy(sim.referencePrice, referenceVals)
	sim.b[sim.goodIndex["加工食品"]] = 0.18
	sim.b[sim.goodIndex["高档服装"]] = 0.12
	sim.b[sim.goodIndex["建造力"]] = 0.25
	copy(sim.prices, sim.referencePrice)

	sim.averageWage = 6.75
	sim.dt = 0.5
	sim.population = 10_000_000.0
	sim.laborPerCapita = 1.0
	sim.maxLabor = sim.population * sim.laborPerCapita
	sim.inertiaCoeff = 0.5
	sim.dampRatio = 0.3

	sim.buildingTypeNames = []string{
		"谷物农场", "加工食品厂", "棉花种植园", "服装厂", "高档服装厂",
		"煤矿", "铁矿", "炼钢厂", "工具厂", "住房", "建造部门",
	}
	sim.buildingCost = []float64{200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100}

	sim.buildingTemplates = make([]BuildingTemplate, TYPE_COUNT)
	for i := 0; i < int(TYPE_COUNT); i++ {
		sim.buildingTemplates[i].Inputs = [NUM_GOODS]float64{}
	}
	setTemplate := func(t BuildingType, name string, outIdx int, rate float64) {
		bt := &sim.buildingTemplates[t]
		bt.Name = name
		bt.OutputGood = outIdx
		bt.OutputRate = rate
		bt.LaborPerUnit = 5000.0 / rate
	}

	setTemplate(FARM_GRAIN, "谷物农场", sim.goodIndex["谷物"], 50)
	setTemplate(FOOD_PROC, "加工食品厂", sim.goodIndex["加工食品"], 45)
	sim.buildingTemplates[FOOD_PROC].Inputs[sim.goodIndex["谷物"]] = 40.0 / 45.0
	setTemplate(COTTON, "棉花种植园", sim.goodIndex["织物"], 45)
	setTemplate(CLOTHES, "服装厂", sim.goodIndex["服装"], 100)
	sim.buildingTemplates[CLOTHES].Inputs[sim.goodIndex["织物"]] = 60.0 / 100.0
	setTemplate(LUXURY_CLOTHES, "高档服装厂", sim.goodIndex["高档服装"], 30)
	sim.buildingTemplates[LUXURY_CLOTHES].Inputs[sim.goodIndex["织物"]] = 25.0 / 30.0
	setTemplate(COAL_MINE, "煤矿", sim.goodIndex["煤"], 40)
	sim.buildingTemplates[COAL_MINE].Inputs[sim.goodIndex["工具"]] = 10.0 / 40.0
	setTemplate(IRON_MINE, "铁矿", sim.goodIndex["铁"], 40)
	sim.buildingTemplates[IRON_MINE].Inputs[sim.goodIndex["工具"]] = 10.0 / 40.0
	setTemplate(STEEL_MILL, "炼钢厂", sim.goodIndex["钢"], 90)
	sim.buildingTemplates[STEEL_MILL].Inputs[sim.goodIndex["铁"]] = 60.0 / 90.0
	sim.buildingTemplates[STEEL_MILL].Inputs[sim.goodIndex["煤"]] = 30.0 / 90.0
	setTemplate(TOOL_FACT, "工具厂", sim.goodIndex["工具"], 80)
	sim.buildingTemplates[TOOL_FACT].Inputs[sim.goodIndex["钢"]] = 20.0 / 80.0
	setTemplate(HOUSING, "住房", sim.goodIndex["住房"], 60)
	sim.buildingTemplates[HOUSING].Inputs[sim.goodIndex["钢"]] = 5.0 / 60.0
	sim.buildingTemplates[HOUSING].Inputs[sim.goodIndex["工具"]] = 5.0 / 60.0
	setTemplate(CONST_DEPT, "建造部门", sim.goodIndex["建造力"], 15)
	sim.buildingTemplates[CONST_DEPT].Inputs[sim.goodIndex["钢"]] = 50.0 / 15.0
	sim.buildingTemplates[CONST_DEPT].Inputs[sim.goodIndex["工具"]] = 20.0 / 15.0

	sim.maxLabor = sim.population * sim.laborPerCapita
	sim.buildingCounts = make([]int, TYPE_COUNT)
	for i := 0; i < int(TYPE_COUNT); i++ {
		sim.buildingCounts[i] = 10
	}
	sim.avgProfitRates = make([]float64, TYPE_COUNT)
	sim.smoothedProfitRate = make([]float64, TYPE_COUNT)
	sim.employmentRatio = make([]float64, TYPE_COUNT)
	sim.consecutiveLowEmpWeeks = make([]int, TYPE_COUNT)
	sim.cashPools = make([]float64, TYPE_COUNT)
	for i := range sim.employmentRatio {
		sim.employmentRatio[i] = 1.0
		sim.cashPools[i] = float64(sim.buildingCounts[i]) * 20000.0
	}

	sim.maxTotalFarms = 10000
	sim.maxCoalMines = 500
	sim.maxIronMines = 500
	sim.maxConstDept = 1000

	sim.satisfaction = 0.75

	sim.demandTable = [3][4]float64{
		{39.0, 106.0, 0.0, 20.0},
		{41.0, 137.0, 7.0, 74.0},
		{0.0, 163.0, 122.0, 130.0},
	}

	sim.groupGoods = make([][]int, GROUP_COUNT)
	sim.groupGoods[GRP_SIMPLE_CLOTHES] = []int{sim.goodIndex["织物"]}
	sim.groupGoods[GRP_BASIC_FOOD] = []int{sim.goodIndex["谷物"], sim.goodIndex["加工食品"]}
	sim.groupGoods[GRP_STANDARD_CLOTHES] = []int{sim.goodIndex["服装"], sim.goodIndex["高档服装"]}
	sim.groupGoods[GRP_HOUSING] = []int{sim.goodIndex["住房"]}

	sim.priceHist = make([][]float64, 0)
	sim.outputHist = make([][]float64, 0)
	sim.profitRateHist = make([][]float64, 0)
	sim.buildingHist = make([][]int, 0)
	sim.gdpHist = make([]float64, 0)
	sim.cashPoolHist = make([][]float64, 0)
	sim.populationHist = make([]float64, 0)

	return sim
}

func (sim *MarketSimulation) getGroupDemandPer100k() [4]float64 {
	w := sim.averageWage
	var res [4]float64
	if w <= 5.0 {
		for g := 0; g < int(GROUP_COUNT); g++ {
			res[g] = sim.demandTable[0][g]
		}
	} else if w >= 20.0 {
		for g := 0; g < int(GROUP_COUNT); g++ {
			res[g] = sim.demandTable[2][g]
		}
	} else if w <= 10.0 {
		t := (w - 5.0) / 5.0
		for g := 0; g < int(GROUP_COUNT); g++ {
			res[g] = sim.demandTable[0][g]*(1-t) + sim.demandTable[1][g]*t
		}
	} else {
		t := (w - 10.0) / 10.0
		for g := 0; g < int(GROUP_COUNT); g++ {
			res[g] = sim.demandTable[1][g]*(1-t) + sim.demandTable[2][g]*t
		}
	}
	return res
}

func (sim *MarketSimulation) placeOrder(typeIdx int) {
	sim.constructionQueue = append(sim.constructionQueue, ConstructionOrder{
		TypeIndex:     typeIdx,
		TotalCost:     sim.buildingCost[typeIdx],
		RemainingCost: sim.buildingCost[typeIdx],
	})
}

func (sim *MarketSimulation) Step() {
	sim.stepCount++

	// 人口动态
	s := sim.satisfaction
	if s < 0.0 {
		s = 0.0
	} else if s > 1.0 {
		s = 1.0
	}
	var r52 float64
	if s <= 0.75 {
		r52 = (0.20/0.75)*s - 0.20
	} else {
		r52 = 0.2 * (s - 0.75)
	}
	if r52 < -0.20 {
		r52 = -0.20
	}
	if r52 > 0.05 {
		r52 = 0.05
	}
	rStep := math.Pow(1.0+r52, 1.0/52.0) - 1.0
	sim.population *= (1.0 + rStep)

	sim.maxLabor = sim.population * sim.laborPerCapita
	constrIdx := sim.goodIndex["建造力"]

	// 1. 利润因子
	profitFactor := make([]float64, TYPE_COUNT)
	for t := 0; t < int(TYPE_COUNT); t++ {
		if sim.buildingCounts[t] == 0 {
			profitFactor[t] = 0.0
			continue
		}
		profitFactor[t] = sim.buildingTemplates[t].GetProfitFactor(sim.prices, sim.averageWage)
	}

	// 2. 期望产出与劳动力需求
	estOutputPerBuilding := make([]float64, TYPE_COUNT)
	totalDesiredLabor := 0.0
	for t := 0; t < int(TYPE_COUNT); t++ {
		if sim.buildingCounts[t] == 0 {
			estOutputPerBuilding[t] = 0.0
			continue
		}
		bt := &sim.buildingTemplates[t]
		estOutputPerBuilding[t] = bt.OutputRate * profitFactor[t] * sim.employmentRatio[t]
		totalDesiredLabor += float64(sim.buildingCounts[t]) * bt.LaborPerUnit * estOutputPerBuilding[t]
	}
	laborFactor := 1.0
	if totalDesiredLabor > 0 {
		laborFactor = math.Min(1.0, sim.maxLabor/totalDesiredLabor)
	}

	// 3. 实际产出、中间投入
	totalOut := make([]float64, NUM_GOODS)
	totalIn := make([]float64, NUM_GOODS)
	actualLabor := 0.0
	for t := 0; t < int(TYPE_COUNT); t++ {
		if sim.buildingCounts[t] == 0 {
			continue
		}
		bt := &sim.buildingTemplates[t]
		actualPerBuilding := estOutputPerBuilding[t] * laborFactor
		typeTotalProd := float64(sim.buildingCounts[t]) * actualPerBuilding
		totalOut[bt.OutputGood] += typeTotalProd
		for g := 0; g < NUM_GOODS; g++ {
			totalIn[g] += float64(sim.buildingCounts[t]) * bt.Inputs[g] * actualPerBuilding
		}
		actualLabor += float64(sim.buildingCounts[t]) * bt.LaborPerUnit * actualPerBuilding
	}

	// 自给农场
	inQueueCount := make([]int, TYPE_COUNT)
	for _, ord := range sim.constructionQueue {
		inQueueCount[ord.TypeIndex]++
	}
	usedFarms := sim.buildingCounts[FARM_GRAIN] + sim.buildingCounts[COTTON] +
		inQueueCount[FARM_GRAIN] + inQueueCount[COTTON]
	subsistCount := sim.maxTotalFarms - usedFarms
	if subsistCount < 0 {
		subsistCount = 0
	}
	totalOut[sim.goodIndex["谷物"]] += float64(subsistCount) * 2.0
	totalOut[sim.goodIndex["织物"]] += float64(subsistCount) * 1.0
	totalOut[sim.goodIndex["服装"]] += float64(subsistCount) * 0.5

	// 4. 利润入现金池（除建造部门）
	for t := 0; t < int(TYPE_COUNT); t++ {
		if t == int(CONST_DEPT) {
			continue
		}
		if sim.buildingCounts[t] == 0 {
			continue
		}
		bt := &sim.buildingTemplates[t]
		actualPerBuilding := estOutputPerBuilding[t] * laborFactor
		totalProd := float64(sim.buildingCounts[t]) * actualPerBuilding
		revenue := totalProd * sim.prices[bt.OutputGood]
		inputCost := 0.0
		for g := 0; g < NUM_GOODS; g++ {
			inputCost += float64(sim.buildingCounts[t]) * bt.Inputs[g] * actualPerBuilding * sim.prices[g]
		}
		laborCost := float64(sim.buildingCounts[t]) * bt.LaborPerUnit * actualPerBuilding * sim.averageWage
		profit := revenue - inputCost - laborCost
		sim.cashPools[t] += profit
		if sim.cashPools[t] < 0 {
			sim.cashPools[t] = 0
		}
	}

	// 5. 消费组分配
	netSupply := make([]float64, NUM_GOODS)
	for i := 0; i < NUM_GOODS; i++ {
		netSupply[i] = totalOut[i] - totalIn[i]
		if netSupply[i] < 0 {
			netSupply[i] = 0
		}
	}

	weight := make([]float64, NUM_GOODS)
	for i := 0; i < NUM_GOODS; i++ {
		weight[i] = (totalOut[i] - totalIn[i]) / (2.0*sim.prices[i] + 1e-9)
	}

	per100k := sim.getGroupDemandPer100k()
	scale := sim.population / 100000.0
	groupDemand := make([]float64, GROUP_COUNT)
	for g := 0; g < int(GROUP_COUNT); g++ {
		groupDemand[g] = per100k[g] * scale
	}

	avail := make([]float64, NUM_GOODS)
	copy(avail, netSupply)
	consumerTarget := make([]float64, NUM_GOODS)
	consumerActual := make([]float64, NUM_GOODS)

	for g := 0; g < int(GROUP_COUNT); g++ {
		remain := groupDemand[g]
		if remain <= 0 {
			continue
		}
		goods := make([]int, len(sim.groupGoods[g]))
		copy(goods, sim.groupGoods[g])
		sort.Slice(goods, func(a, b int) bool {
			return weight[goods[a]] > weight[goods[b]]
		})
		for _, good := range goods {
			if remain <= 0 {
				break
			}
			want := remain
			consumerTarget[good] += want
			sold := math.Min(want, avail[good])
			consumerActual[good] += sold
			avail[good] -= sold
			remain -= sold
		}
	}

	// 必需品满足度
	essentialsDemand := groupDemand[GRP_SIMPLE_CLOTHES] + groupDemand[GRP_BASIC_FOOD]
	essentialsActual := 0.0
	for _, good := range sim.groupGoods[GRP_SIMPLE_CLOTHES] {
		essentialsActual += consumerActual[good]
	}
	for _, good := range sim.groupGoods[GRP_BASIC_FOOD] {
		essentialsActual += consumerActual[good]
	}
	if essentialsDemand > 1e-9 {
		sim.satisfaction = essentialsActual / essentialsDemand
	} else {
		sim.satisfaction = 1.0
	}
	if sim.satisfaction > 1.0 {
		sim.satisfaction = 1.0
	}

	// 6. 建造力分配与交易闭环
	constrDem := 0.0
	for _, ord := range sim.constructionQueue {
		constrDem += ord.RemainingCost
	}

	E := make([]float64, NUM_GOODS)
	for i := 0; i < NUM_GOODS; i++ {
		totalDemand := totalIn[i] + consumerTarget[i]
		E[i] = totalDemand - totalOut[i]
	}
	E[constrIdx] += constrDem

	availConstr := totalOut[constrIdx]
	pConstr := sim.prices[constrIdx]
	soldConstr := 0.0

	for idx := range sim.constructionQueue {
		ord := &sim.constructionQueue[idx]
		if availConstr <= 0 {
			break
		}
		maxByCash := sim.cashPools[ord.TypeIndex] / pConstr
		invest := math.Min(availConstr, 30.0)
		invest = math.Min(invest, ord.RemainingCost)
		invest = math.Min(invest, maxByCash)
		if invest <= 0 {
			continue
		}
		sim.cashPools[ord.TypeIndex] -= invest * pConstr
		soldConstr += invest
		ord.RemainingCost -= invest
		availConstr -= invest
	}

	// 建造部门利润
	bt := &sim.buildingTemplates[CONST_DEPT]
	actualPerBuilding := estOutputPerBuilding[CONST_DEPT] * laborFactor
	inputCost := 0.0
	for g := 0; g < NUM_GOODS; g++ {
		inputCost += float64(sim.buildingCounts[CONST_DEPT]) * bt.Inputs[g] * actualPerBuilding * sim.prices[g]
	}
	laborCost := float64(sim.buildingCounts[CONST_DEPT]) * bt.LaborPerUnit * actualPerBuilding * sim.averageWage
	profit := soldConstr*pConstr - inputCost - laborCost
	sim.cashPools[CONST_DEPT] += profit
	if sim.cashPools[CONST_DEPT] < 0 {
		sim.cashPools[CONST_DEPT] = 0
	}

	// 移除已完成订单
	newQueue := make([]ConstructionOrder, 0, len(sim.constructionQueue))
	for _, ord := range sim.constructionQueue {
		if ord.RemainingCost <= 0 {
			sim.buildingCounts[ord.TypeIndex]++
		} else {
			newQueue = append(newQueue, ord)
		}
	}
	sim.constructionQueue = newQueue

	// GDP 生产法
	totalOutputValue := 0.0
	for i := 0; i < NUM_GOODS; i++ {
		outVal := totalOut[i]
		if i == constrIdx {
			outVal = soldConstr
		}
		totalOutputValue += outVal * sim.prices[i]
	}
	totalIntermediate := 0.0
	for i := 0; i < NUM_GOODS; i++ {
		totalIntermediate += totalIn[i] * sim.prices[i]
	}
	gdp := totalOutputValue - totalIntermediate

	// 价格更新
	for i := 0; i < NUM_GOODS; i++ {
		G := math.Abs(totalOut[i]) + math.Abs(totalIn[i]) + math.Abs(consumerTarget[i]) + 1.0
		sim.m[i] = sim.inertiaCoeff * G
		restoring := sim.b[i] * (sim.prices[i] - sim.referencePrice[i])
		rho := sim.dampRatio * 2.0 * math.Sqrt(sim.m[i]*sim.b[i])
		acc := (E[i] - rho*sim.v[i] - restoring) / sim.m[i]
		sim.v[i] += acc * sim.dt
		sim.prices[i] += sim.v[i] * sim.dt
		if sim.prices[i] < 0.1 {
			sim.prices[i] = 0.1
		}
	}

	// 利润率（当周瞬时）
	profitRates := make([]float64, TYPE_COUNT)
	for t := 0; t < int(TYPE_COUNT); t++ {
		if sim.buildingCounts[t] == 0 {
			continue
		}
		bt := &sim.buildingTemplates[t]
		unitCost := bt.LaborPerUnit * sim.averageWage
		for g := 0; g < NUM_GOODS; g++ {
			unitCost += sim.prices[g] * bt.Inputs[g]
		}
		if unitCost > 1e-6 {
			profitRates[t] = (sim.prices[bt.OutputGood] - unitCost) / unitCost
		}
	}
	copy(sim.avgProfitRates, profitRates)
	sim.profitRateHist = append(sim.profitRateHist, append([]float64{}, profitRates...))

	// 平滑利润率（EMA）
	if sim.stepCount == 1 {
		copy(sim.smoothedProfitRate, profitRates)
	} else {
		alpha := 0.2
		for t := 0; t < int(TYPE_COUNT); t++ {
			sim.smoothedProfitRate[t] = sim.smoothedProfitRate[t]*(1.0-alpha) + profitRates[t]*alpha
		}
	}

	// 雇佣调整
	for t := 0; t < int(TYPE_COUNT); t++ {
		if sim.buildingCounts[t] == 0 {
			continue
		}
		if sim.avgProfitRates[t] < 0.0 {
			sim.employmentRatio[t] *= 0.95
		} else {
			sim.employmentRatio[t] += (1.0 - sim.employmentRatio[t]) * 0.05
		}
		if sim.employmentRatio[t] < 0.1 {
			sim.employmentRatio[t] = 0.1
		}
		if sim.employmentRatio[t] > 1.0 {
			sim.employmentRatio[t] = 1.0
		}
	}

	// 建筑衰退保护
	if sim.stepCount > 52 {
		for t := 0; t < int(TYPE_COUNT); t++ {
			if sim.buildingCounts[t] == 0 {
				sim.consecutiveLowEmpWeeks[t] = 0
				continue
			}
			if sim.employmentRatio[t] <= 0.75 {
				sim.consecutiveLowEmpWeeks[t]++
				if sim.consecutiveLowEmpWeeks[t] >= 52 {
					reduce := int(math.Ceil(float64(sim.buildingCounts[t]) * 0.05))
					if reduce < 1 {
						reduce = 1
					}
					sim.buildingCounts[t] -= reduce
					if sim.buildingCounts[t] < 1 {
						sim.buildingCounts[t] = 1
					}
				}
			} else {
				sim.consecutiveLowEmpWeeks[t] = 0
			}
		}
	} else {
		for t := 0; t < int(TYPE_COUNT); t++ {
			sim.consecutiveLowEmpWeeks[t] = 0
		}
	}

	// 记录历史
	sim.gdpHist = append(sim.gdpHist, gdp)
	bCounts := make([]int, TYPE_COUNT)
	copy(bCounts, sim.buildingCounts)
	sim.buildingHist = append(sim.buildingHist, bCounts)

	outCopy := make([]float64, NUM_GOODS)
	copy(outCopy, totalOut)
	sim.outputHist = append(sim.outputHist, outCopy)

	priceCopy := make([]float64, NUM_GOODS)
	copy(priceCopy, sim.prices)
	sim.priceHist = append(sim.priceHist, priceCopy)

	cpCopy := make([]float64, TYPE_COUNT)
	copy(cpCopy, sim.cashPools)
	sim.cashPoolHist = append(sim.cashPoolHist, cpCopy)

	sim.populationHist = append(sim.populationHist, sim.population)
}

func (sim *MarketSimulation) AiBuild() {
	inQueueCount := make([]int, TYPE_COUNT)
	for _, ord := range sim.constructionQueue {
		inQueueCount[ord.TypeIndex]++
	}

	totalFarms := sim.buildingCounts[FARM_GRAIN] + sim.buildingCounts[COTTON] +
		inQueueCount[FARM_GRAIN] + inQueueCount[COTTON]
	farmCapReached := totalFarms >= sim.maxTotalFarms
	coalCapReached := (sim.buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE]) >= sim.maxCoalMines
	ironCapReached := (sim.buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE]) >= sim.maxIronMines
	constrCapReached := (sim.buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT]) >= sim.maxConstDept

	constrCapacity := 0.0
	if sim.buildingCounts[CONST_DEPT] > 0 {
		constrCapacity = float64(sim.buildingCounts[CONST_DEPT]) *
			sim.buildingTemplates[CONST_DEPT].GetProfitFactor(sim.prices, sim.averageWage) *
			sim.employmentRatio[CONST_DEPT] * sim.buildingTemplates[CONST_DEPT].OutputRate
	}
	totalRemainingCost := 0.0
	for _, ord := range sim.constructionQueue {
		totalRemainingCost += ord.RemainingCost
	}
	stepsNeeded := 1e9
	if constrCapacity > 0 {
		stepsNeeded = totalRemainingCost / constrCapacity
	}
	if stepsNeeded > 52.0 && inQueueCount[CONST_DEPT] == 0 && !constrCapReached {
		sim.constructionQueue = append(
			[]ConstructionOrder{{TypeIndex: int(CONST_DEPT), TotalCost: sim.buildingCost[CONST_DEPT], RemainingCost: sim.buildingCost[CONST_DEPT]}},
			sim.constructionQueue...,
		)
	}

	for t := 0; t < int(TYPE_COUNT); t++ {
		if (t == int(FARM_GRAIN) || t == int(COTTON)) && farmCapReached {
			continue
		}
		if t == int(COAL_MINE) && coalCapReached {
			continue
		}
		if t == int(IRON_MINE) && ironCapReached {
			continue
		}
		if t == int(CONST_DEPT) && constrCapReached {
			continue
		}

		smoothedRate := sim.smoothedProfitRate[t]

		// 零建筑启动
		if sim.buildingCounts[t] == 0 && inQueueCount[t] == 0 {
			bt := &sim.buildingTemplates[t]
			estCost := bt.GetUnitCost(sim.prices, sim.averageWage)
			var estProfitRate float64
			if estCost > 1e-6 {
				estProfitRate = (sim.prices[bt.OutputGood] - estCost) / estCost
			}
			if estProfitRate > 0.1 {
				sim.placeOrder(t)
			}
			continue
		}

		if smoothedRate > 0.1 && sim.buildingCounts[t] > 0 {
			N_wanted := int(math.Ceil((smoothedRate - 0.1) / 0.05))
			if N_wanted < 0 {
				N_wanted = 0
			}
			N_remaining := N_wanted - inQueueCount[t]
			if N_remaining < 0 {
				N_remaining = 0
			}

			// 容量检查
			if t == int(FARM_GRAIN) || t == int(COTTON) {
				slots := sim.maxTotalFarms - totalFarms
				if slots <= 0 {
					continue
				}
				if N_remaining > slots {
					N_remaining = slots
				}
			} else if t == int(COAL_MINE) {
				slots := sim.maxCoalMines - (sim.buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE])
				if slots <= 0 {
					continue
				}
				if N_remaining > slots {
					N_remaining = slots
				}
			} else if t == int(IRON_MINE) {
				slots := sim.maxIronMines - (sim.buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE])
				if slots <= 0 {
					continue
				}
				if N_remaining > slots {
					N_remaining = slots
				}
			} else if t == int(CONST_DEPT) {
				slots := sim.maxConstDept - (sim.buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT])
				if slots <= 0 {
					continue
				}
				if N_remaining > slots {
					N_remaining = slots
				}
			}

			maxExpand := int(math.Floor(float64(sim.buildingCounts[t]) * 0.1))
			if maxExpand < 1 {
				maxExpand = 1
			}
			N_final := N_remaining
			if N_final > maxExpand {
				N_final = maxExpand
			}
			for j := 0; j < N_final; j++ {
				sim.placeOrder(t)
			}
		}
	}
}

// CSV 输出函数
func writePriceCsv(sim *MarketSimulation) {
	f, _ := os.Create("prices.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	header := []string{"Step"}
	for _, name := range commodityNames {
		header = append(header, name)
	}
	w.Write(header)
	for row, prices := range sim.priceHist {
		record := []string{strconv.Itoa(row)}
		for _, v := range prices {
			record = append(record, fmt.Sprintf("%f", v))
		}
		w.Write(record)
	}
}

func writeBuildingCsv(sim *MarketSimulation) {
	f, _ := os.Create("buildings.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	header := []string{"Step"}
	for _, name := range sim.buildingTypeNames {
		header = append(header, name)
	}
	w.Write(header)
	for row, counts := range sim.buildingHist {
		record := []string{strconv.Itoa(row)}
		for _, v := range counts {
			record = append(record, strconv.Itoa(v))
		}
		w.Write(record)
	}
}

func writeOutputCsv(sim *MarketSimulation) {
	f, _ := os.Create("outputs.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	header := []string{"Step"}
	for _, name := range commodityNames {
		header = append(header, name+"产量")
	}
	w.Write(header)
	for row, out := range sim.outputHist {
		record := []string{strconv.Itoa(row)}
		for _, v := range out {
			record = append(record, fmt.Sprintf("%f", v))
		}
		w.Write(record)
	}
}

func writeProfitRateCsv(sim *MarketSimulation) {
	f, _ := os.Create("profitRate.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	header := []string{"Step"}
	for _, name := range sim.buildingTypeNames {
		header = append(header, name+"利润率")
	}
	w.Write(header)
	for row, rates := range sim.profitRateHist {
		record := []string{strconv.Itoa(row)}
		for _, v := range rates {
			record = append(record, fmt.Sprintf("%f", v))
		}
		w.Write(record)
	}
}

func writeGdpCsv(sim *MarketSimulation) {
	f, _ := os.Create("gdp.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	w.Write([]string{"Step", "GDP"})
	for row, gdp := range sim.gdpHist {
		w.Write([]string{strconv.Itoa(row), fmt.Sprintf("%f", gdp)})
	}
}

func writeCashPoolCsv(sim *MarketSimulation) {
	f, _ := os.Create("cashPool.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	header := []string{"Step"}
	for _, name := range sim.buildingTypeNames {
		header = append(header, name)
	}
	w.Write(header)
	for row, cp := range sim.cashPoolHist {
		record := []string{strconv.Itoa(row)}
		for _, v := range cp {
			record = append(record, fmt.Sprintf("%f", v))
		}
		w.Write(record)
	}
}

func writePopulationCsv(sim *MarketSimulation) {
	f, _ := os.Create("population.csv")
	defer f.Close()
	f.Write([]byte{0xEF, 0xBB, 0xBF})
	w := csv.NewWriter(f)
	defer w.Flush()
	w.Write([]string{"Step", "Population"})
	for row, pop := range sim.populationHist {
		w.Write([]string{strconv.Itoa(row), fmt.Sprintf("%f", pop)})
	}
}

func main() {
	sim := NewMarketSimulation()

	fmt.Printf("模拟开始，共 %d 周...\n", TOTAL_STEPS)
	for t := 0; t < TOTAL_STEPS; t++ {
		sim.Step()
		if t%AI_INTERVAL == 0 {
			sim.AiBuild()
		}
		if t%52 == 0 {
			fmt.Printf("周数: %d/%d     \r", t, TOTAL_STEPS)
		}
	}
	fmt.Println("\n写入CSV...")

	writePriceCsv(sim)
	writeBuildingCsv(sim)
	writeOutputCsv(sim)
	writeProfitRateCsv(sim)
	writeGdpCsv(sim)
	writeCashPoolCsv(sim)
	writePopulationCsv(sim)

	fmt.Println("CSV文件写入完毕。")
}
