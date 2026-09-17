// Package model 定义模拟内核的领域模型与不变量。
//
// 契约映射（1.0 生产与市场模拟.md）：
//
//	Good      §3.1 商品（含需求弹性 eps 与零利润价 Pcost）
//	Recipe    §3.3 单级建筑的投入产出（每周期）
//	Building  建筑类别（等级 level 是唯一规模状态）
//	Consumer  §6.1–§6.4 消费组、使用价值、财富档需求
//	Owner     §4.1/G3 所有权（政府 / 私有）
//	Finance   G5 金融区：每级雇 1000 人，每级掌控 ctrl 级其余建筑
//
// 本包只放数据与不变量，不放任何模拟逻辑（ARCHITECTURE §2 的 L1 领域层）。
package model

import "math"

import "errors"

// Goods 是 1.0 选定的 11 种商品（§3.1）。
const Goods = 11

// FinanceIndex 是金融区在建筑类别数组中的下标。
// 约定：普通建筑占 [0, Goods)，金融区固定为最后一个。
const FinanceIndex = Goods

// BuildingTypes 是建筑类别总数（11 种普通建筑 + 1 个金融区）。
const BuildingTypes = Goods + 1

// Owner 是建筑的所有权（G3）。
type Owner uint8

const (
	// OwnerPrivate 表示私有建筑：运营纯利归金融区（G4）。
	OwnerPrivate Owner = iota
	// OwnerGovernment 表示政府建筑：运营净额进政府现金池。
	OwnerGovernment
)

// Metric 是商品的计量单位（仅用于报告）。
type Metric string

// Recipe 是一种建筑的投入产出配方（§3.3，每周期、每级）。
type Recipe struct {
	// Output 是产出品下标。
	Output int
	// Qty 是单级每周期产出量（§3.3 的"产出 ×N"）。
	Qty float64
	// Inputs 是投入品：下标 → 数量。空表示无中间投入（纯劳动部门）。
	Inputs map[int]float64
}

// Building 是一种建筑类别的静态定义。
type Building struct {
	// Name 是建筑名（同时用于商品名的默认值）。
	Name string
	// BuildCost 是建造成本，单位是"建造力"（§3.2）。
	BuildCost float64
	// Recipe 是投入产出配方。
	Recipe Recipe
	// LaborPerLevel 是每级雇佣人数（§5 为 5000；金融区 G5 为 1000）。
	LaborPerLevel float64
	// Cap 是等级上限（§4.2）。0 表示无上限。
	Cap float64
	// LandKind 标记是否占用耕地："" / "arable"（谷物与棉花共用 10000）。
	LandKind string
	// IsFinance 标记这是金融区（不参与商品市场）。
	IsFinance bool
	// AllowPrivatize 是该类建筑【是否允许被私有化】的独立开关。
	//
	// 契约 §4.5.1 的所有权模型把它加为逐建筑的开关（而不是全局开关），
	// 原因是不同部门的经济含义不同：
	//
	//	谷物农场、住房等民生产业 —— 可设为不可私有化，保持政府控制；
	//	加工食品、高档服装等竞争性行业 —— 允许私有化。
	//
	// 默认全部允许，由契约数据逐项覆盖。
	AllowPrivatize bool
}

// Cohorts 是 §5 的阶层比例与工资。
var (
	CohortShares = [3]float64{0.75, 0.20, 0.05}
	CohortWages  = [3]float64{5, 10, 20}
)

// AverageWage 返回按阶层比例加权的平均工资（§5 = 6.75）。
func AverageWage() float64 {
	var s float64
	for i := range CohortShares {
		s += CohortShares[i] * CohortWages[i]
	}
	return s
}

// Good 是一种商品的市场状态与参数。
type Good struct {
	Name string
	// Eps 是需求价格弹性（§2.1，逐商品）。
	Eps float64
	// Pcost 是零利润价（§3.4，由 Leontief 方程解出）。
	Pcost float64
	// Pinit 是开局价（§3.4 的加成价方程解）。
	Pinit float64
	// PriceFloorRatio / PriceCeilRatio 是价格钳制比例（§2.4 默认 0.2 / 5）。
	PriceFloorRatio float64
	PriceCeilRatio  float64
}

// Market 是一种商品的市场动态状态。
type Market struct {
	// Price 是当期价格，DP 是 dP/dt（§2.3 的状态向量）。
	Price, DP float64
	// Supply 是本 tick 实际产出，Demand 是名义需求（§2.2）。
	Supply, Demand float64
	// Excess 是过剩需求 E = D − S。
	Excess float64
	// A 是需求标定常数，使 P=Pinit 时 E=0（§3.4 步骤 3）。
	A float64
	// ClampTicks 是触价钳制的累计 tick 数（§2.4）。
	ClampTicks int64
}

// ConsumeGroup 是一个消费组（§6.1）。
type ConsumeGroup struct {
	Name string
	// Uses 是使用价值表：商品下标 → 每单位商品对该组的贡献（§6.2）。
	Uses map[int]float64
}

// WealthTier 是 §6.3 的一个财富档。
type WealthTier struct {
	// Wealth 是财富等级。
	Wealth float64
	// Per100k 是每 10 万人口的各消费组标准需求量（下标与 Groups 对应）。
	Per100k [4]float64
	// Anchor 标记该档是否为契约原表直接给出的锚点。
	// 契约只给 3 个锚点（5 / 10 / 20），其余档位由档间线性插值补全。
	Anchor bool
}

// 四个消费组的固定下标（§6.1）。
const (
	GroupPlainClothes = 0 // 简朴衣物
	GroupBasicFood    = 1 // 基础食物
	GroupStdClothes   = 2 // 标准衣物
	GroupHousing      = 3 // 住宅
)

// Params 是全局参数。全部字段对应契约条文，便于逐条核对。
type Params struct {
	// TicksPerYear 是 1 年的 tick 数（D9：1 tick = 1 周 ⇒ 52）。
	TicksPerYear int

	// PricePeriod 是价格波动周期 T（§2.5 建议 26）。
	PricePeriod float64
	// Damping 是阻尼比 zeta（§2.5 建议 0.7）。
	Damping float64
	// DT 是每 tick 的等效时间步（§2.4 的 dt = 0.5）。
	DT float64
	// Substeps 是每 tick 的 RK4 子步数（§2.4 的 n_sub = 10）。
	Substeps int
	// ScaleGamma 是规模对周期的指数（§2.5，默认 0 = 尺度不变）。
	ScaleGamma float64

	// ExpandThreshold 是扩建利润率阈值（§4.1 的 10%）。
	ExpandThreshold float64
	// ExpandPer5Pct 是"每超过 5 个百分点多扩 1 个"的步数（§4.1）。
	ExpandPer5Pct float64
	// ExpandMaxRatio 是单次扩建上限占当前总数的比例（§4.1 的 10%）。
	ExpandMaxRatio float64

	// SitePowerLimit 是每工地每 tick 最多投入的建造力（§4.2 的 30）。
	SitePowerLimit float64
	// QueueWarnTicks 是队列告警阈值（§4.2 的 52 周期）。
	QueueWarnTicks float64

	// DecayWindow 是缩编观察窗（§4.4 的 156 周期）。
	DecayWindow int
	// DecayRate 是每周期缩编比例（§4.4 的 5%）。
	DecayRate float64
	// IdleHireRate 是触发缩编的雇佣率上限（§4.4 的 75%）。
	IdleHireRate float64

	// HireAnnual 是年化雇佣调整幅度（§5 的 5%，D9 折算为 5%/52 每 tick）。
	HireAnnual float64
	// MarginEMA 是 margin 的 EMA 窗口（用于抑制抖动）。
	MarginEMA int

	// TaxRate 是政府税率（G1，对全部交易额征收）。
	TaxRate float64
	// ControlPerFinance 是每级金融区可掌控的其余建筑级数（G5）。
	ControlPerFinance float64
	// GovPowerMarkup 是政府向扩建方出售建造力的加价率（G2/G6）。
	GovPowerMarkup float64

	// PopGrowthHi 是必需品满足度 100% 时的年化增长率（§6.5 的 +5%）。
	PopGrowthHi float64
	// PopGrowthLo 是满足度 0% 时的年化增长率（§6.5 的 −20%）。
	PopGrowthLo float64
	// PopSatTarget 是人口零增长的满足度（§6.5 的 75%）。
	PopSatTarget float64

	// ArableCap 是耕地上限（谷物 + 棉花，§4.2 的 10000）。
	ArableCap float64
	// SubsistenceScale 是每单位未利用耕地对应的自给农场数。
	//
	// 【本值取契约/架构写定的 0.05，不得为让模拟好看而改动。】
	//
	// 需要记录的是：实测表明该值会让系统崩解（见 out/gosim_diag.txt）。
	// 契约 §4.2 的耕地上限是 10,000 级，而 §6.3 的需求量级只用到约 5 级耕地，
	// 于是 99.95% 的耕地都算"未利用"，按 0.05 会生成约 9,600 份自给农场，
	// 其谷物产出是专业农场的数倍，把谷物价格永久压在地板价，
	// 专业农场（谷物 +30% → −80%）无法盈利。这正是架构 §5.3 记的 R3 风险。
	//
	// 这是【契约的参数标定问题】，应由契约 T-1.0-14 的结构标定解决，
	// 不能在实现里偷偷改默认值。命令行可用 -subsist 做诊断性覆盖。
	SubsistenceScale float64

	// InitialPowerLevel 是建造部门的起步等级。
	//
	// 【本字段不是契约参数，不得与契约值混淆】：契约 §3.2/§4.2 只规定
	// "建造力每级产出 15、建造成本 100、上限 1,000"，没有规定开局建筑数量。
	// 之所以需要它：1 级建造部门仅产 15 建造力/tick，而 1 级普通建筑平均需要
	// 600 建造力，起步过小会让任何扩建都被 6.7 tick/级的回本周期卡住。
	// 默认 20 级（= 300 建造力/tick）是本工程为使扩建可行而选的建模值，
	// 不是契约结论；命令行可用 -power-init 覆盖。
	InitialPowerLevel float64

	// ProductionInitLevel 是【每种生产建筑】的起始等级（契约修订：统一定为 5）。
	//
	// 大于 0 时，开局布点改为"每种生产建筑一律取该等级"，
	// 不再用需求驱动的物质平衡解算。理由：
	//
	//  1. 需求驱动解出的级数差异极大（棉花 0.94 级 vs 铁矿 16.5 级），
	//     使开局的资本有机构成完全由模型内部决定，不便于对照实验；
	//  2. 统一起步值让"后续扩建把结构推向均衡"这一过程成为可观察对象，
	//     而不是一开始就已经在均衡点上；
	//  3. 5 级是一个刻意偏小的起点，给小经济体留出更大的扩建空间，
	//     也让"从短缺起步、靠市场自行补齐"成为可观察的过程。
	//
	// 取 0 表示回退到需求驱动的物质平衡解算。
	ProductionInitLevel float64

	// RetainRatio 是私人份额中留存于建筑现金池、用于自身扩建的比例（§4.3 修订）。
	//
	// 一笔运营纯利按 §4.5.1 划分给三个归属，三者之和恒等于利润本身
	// （这是消除重复记账的充要条件，见 docs/AUDIT-1.0.md §4）：
	//
	//	政府份额   = 利润 × govShare
	//	建筑留存   = 利润 × (1−govShare) × RetainRatio
	//	资本份额   = 利润 × (1−govShare) × (1−RetainRatio)
	//
	// 契约 §4.3 要求"私有建筑的扩建资金从其自身现金池划拨"，
	// 故必须为建筑留出扩建资金；其余作为纯利划归资本方（金融区）。
	RetainRatio float64

	// ===== §4.5.1 修订：私有化机制（本次新增） =====

	// PrivatizeEnabled 是私有化机制的【总开关】。
	//
	// 关闭时整个机制不运行，所有权保持开局状态——用于对照实验与回退。
	PrivatizeEnabled bool
	// PrivatizeMargin 是触发私有化的利润率下限（EMA 口径，满编）。
	//
	// 契约语义："对于盈利建筑，市场可以私有化"。
	// 取正数而不是"利润 > 0"是为了避免在零利润附近反复私有化/回购——
	// 只有稳定盈利的建筑才会被市场接手。
	PrivatizeMargin float64
	// PrivatizeStep 是每 tick 私有化的股权比例（占政府持股的比例）。
	//
	// 私有化是渐进过程而非一次性买断：每 tick 转让一部分股权，
	// 使价格与利润率有时间反馈。取 0.05 表示政府持股每 tick 减少 5%
	// （按年化读，见 §5 的时间语义定案）。
	PrivatizeStep float64
	// PrivatizePriceMult 是私有化对价的估值倍数。
	//
	// 每级对价 = 建造成本（建造力）× 建造力当期价格 × PrivatizePriceMult。
	//
	// 取 1.0 表示严格按【重置成本】成交：一处 600 建造力的工厂，
	// 在建造力价格 P_power 下值 600 × P_power。
	// 这正是"私有化成本就是建造成本"的口径——买下 N 级必须支付
	// 相当于把这 N 级重新建一遍的建造力价值。
	//
	// 该口径的直接后果：私有化速度由【资本池规模 / 重置成本】决定，
	// 而资本池由资本收入（私有建筑的纯利份额）累积。
	// 若要让私有化更快，应调大 startupCapFraction 或 PrivateRetainRatio，
	// 而不是改动本倍数——否则对价就不再等于建造成本了。
	PrivatizePriceMult float64
}

// DefaultParams 返回契约的默认参数集。
func DefaultParams() Params {
	return Params{
		TicksPerYear: 52,

		PricePeriod: 26,
		Damping:     0.7,
		DT:          0.5,
		Substeps:    10,
		ScaleGamma:  0,

		ExpandThreshold: 0.10,
		ExpandPer5Pct:   0.05,
		ExpandMaxRatio:  0.10,

		SitePowerLimit: 30,
		QueueWarnTicks: 52,

		DecayWindow:  156,
		DecayRate:    0.05,
		IdleHireRate: 0.75,

		HireAnnual: 0.05,
		MarginEMA:  12,

		TaxRate:           0.10,
		ControlPerFinance: 5,
		GovPowerMarkup:    0.0,

		PopGrowthHi:  0.05,
		PopGrowthLo:  -0.20,
		PopSatTarget: 0.75,

		ArableCap:        10000,
		SubsistenceScale: 0.05,

		// 建模选择，非契约参数：见 InitialPowerLevel 的说明。
		InitialPowerLevel: 20,

		// 契约修订：每种生产建筑起始 5 级（见 ProductionInitLevel 的说明）。
		ProductionInitLevel: 5,

		// §4.5.1 修订：私人份额的一半留存建筑用于扩建，一半作为纯利归资本。
		RetainRatio: 0.5,

		// §4.5.1 修订：私有化机制（本次新增）。
		// 总开关默认关闭，需显式开启——所有权变动是结构性变化，
		// 不应在未声明的情况下发生。
		PrivatizeEnabled:   false,
		PrivatizeMargin:    0.05,
		PrivatizeStep:      0.05,
		PrivatizePriceMult: 1.0,
	}
}

// GoodSpecs 返回 §3.1 的 11 种商品定义（弹性与两个价格列均来自契约表格）。
func GoodSpecs() []Good {
	names := []string{
		"谷物", "加工食品", "织物", "服装", "高档服装",
		"煤", "铁", "钢", "工具", "住房", "建造力",
	}
	eps := []float64{0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2}
	pcost := []float64{675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250}
	pinit := []float64{810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917}
	out := make([]Good, Goods)
	for i := range out {
		out[i] = Good{
			Name:            names[i],
			Eps:             eps[i],
			Pcost:           pcost[i],
			Pinit:           pinit[i],
			PriceFloorRatio: 0.2,
			PriceCeilRatio:  5.0,
		}
	}
	return out
}

// BuildingSpecs 返回 §3.2/§3.3 的 11 种建筑 + 金融区定义。
func BuildingSpecs(financeLabor, financeBuildCost float64) []Building {
	// 商品下标常量，便于核对 §3.3 的表格。
	const (
		grain, food, fabric, clothes, luxury = 0, 1, 2, 3, 4
		coal, iron, steel, tools, housing    = 5, 6, 7, 8, 9
		power                                = 10
	)
	// AllowPrivatize 默认全部允许，由契约数据逐项覆盖。
	// 下面按"民生产业 vs 竞争性行业"给出默认值：
	// 谷物农场、棉花种植园、住房保持政府控制（食品与居住的稳定性优先），
	// 其余可私有化。金融区不适用（它本来就是私有的）。
	specs := []Building{
		{Name: "谷物农场", BuildCost: 200, Recipe: Recipe{Output: grain, Qty: 50}, LaborPerLevel: 5000, LandKind: "arable", AllowPrivatize: false},
		{Name: "加工食品厂", BuildCost: 600, Recipe: Recipe{Output: food, Qty: 45, Inputs: map[int]float64{grain: 40}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "棉花种植园", BuildCost: 200, Recipe: Recipe{Output: fabric, Qty: 45}, LaborPerLevel: 5000, LandKind: "arable", AllowPrivatize: false},
		{Name: "服装厂", BuildCost: 600, Recipe: Recipe{Output: clothes, Qty: 100, Inputs: map[int]float64{fabric: 60}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "高档服装厂", BuildCost: 600, Recipe: Recipe{Output: luxury, Qty: 30, Inputs: map[int]float64{fabric: 25}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "煤矿", BuildCost: 600, Recipe: Recipe{Output: coal, Qty: 60, Inputs: map[int]float64{tools: 15, coal: 15}}, LaborPerLevel: 5000, Cap: 500, AllowPrivatize: true},
		{Name: "铁矿", BuildCost: 600, Recipe: Recipe{Output: iron, Qty: 60, Inputs: map[int]float64{tools: 15, coal: 15}}, LaborPerLevel: 5000, Cap: 500, AllowPrivatize: true},
		{Name: "炼钢厂", BuildCost: 800, Recipe: Recipe{Output: steel, Qty: 90, Inputs: map[int]float64{iron: 60, coal: 30}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "工具厂", BuildCost: 800, Recipe: Recipe{Output: tools, Qty: 80, Inputs: map[int]float64{steel: 20}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "住房", BuildCost: 800, Recipe: Recipe{Output: housing, Qty: 60, Inputs: map[int]float64{steel: 5, tools: 5}}, LaborPerLevel: 5000, AllowPrivatize: false},
		{Name: "建造部门", BuildCost: 100, Recipe: Recipe{Output: power, Qty: 15, Inputs: map[int]float64{steel: 25, iron: 25, tools: 20}}, LaborPerLevel: 5000, Cap: 1000, AllowPrivatize: true},
		{Name: "金融区", BuildCost: financeBuildCost, LaborPerLevel: financeLabor, Cap: 1000, IsFinance: true, AllowPrivatize: false},
	}
	return specs
}

// ConsumeGroupSpecs 返回 §6.1/§6.2 的四个消费组。
//
// 注意（契约未明说的补全）：§6.2 的使用价值表把"织物"与"服装"都列入"简朴衣物"组，
// 把"谷物"与"加工食品"都列入"基础食物"组。本实现按使用价值在组内等权分摊，
// 这与 tools/gov_capital_accounting.js 的口径一致。
func ConsumeGroupSpecs() []ConsumeGroup {
	return []ConsumeGroup{
		{Name: "简朴衣物", Uses: map[int]float64{2: 1, 3: 1}},
		{Name: "基础食物", Uses: map[int]float64{0: 1, 1: 1.5}},
		{Name: "标准衣物", Uses: map[int]float64{3: 1, 4: 1}},
		{Name: "住宅", Uses: map[int]float64{9: 1}},
	}
}

// ContractDemandAnchors 记录契约 §6.3 修订【之前】的原表（3 档锚点）。
//
//	财富等级   简朴衣物   基础食物   标准衣物   住宅
//	   5         39        210         0        20
//	  10         41        210         7        74
//	  20          0        210       122       130
//
// 保留它仅供追溯与对照，不参与任何计算——运行时一律走 DemandAt 查离散表。
// 之所以不在代码里继续提供"按三锚点插值"的入口：契约修订后基础食物已改为
// S 形、住宅已改为饱和型指数，三锚点插值会给出与 §6.3 新表不同的结果，
// 留着它极易被误用（历史上 calibrate 与 fiscal 正是这样各算了一套）。
var ContractDemandAnchors = [3][4]float64{
	{39, 210, 0, 20},
	{41, 210, 7, 74},
	{0, 210, 122, 130},
}

// ContractAnchorTiers 是上述历史锚点对应的财富档。
var ContractAnchorTiers = [3]float64{5, 10, 20}

// demandTableLow / demandTableHigh 是与契约 §6.3 逐项对齐的离散需求表
// （财富 5–20 共 16 档）。
//
// 【本表已于契约修订中更新，不再是"线性补全"】：
//   - 简朴衣物、标准衣物：档间线性插值（原规则不变）；
//   - 基础食物：改为 S 形 logistic，拐点在财富 10；
//   - 住宅：改为饱和型指数（指数速率逼近饱和值 C），非纯指数。
//
// 两条曲线的公式与参数（契约修订时反解确定，此处必须与文档一致）：
//
//	基础食物  D(w) = 420 / (1 + exp(-0.25·(w - 10)))
//	          系数 420 由 D(10) = 210 反解（L/2 = 210），保留原锚点值。
//	          拐点在财富 10，即此处增长最快。
//
//	住宅      D(w) = C - (C - 20)·exp(-b·(w - 5))
//	          C = 311.35、b = 0.115726，由 D(5)=20、D(10)=148、D(20)=260 反解。
//	          D(10)=148 是原表 74 的两倍；取饱和型而非纯指数，是因为纯指数若要
//	          满足 D(5)=20 则 b≈0.4003，外推到财富 20 会得到 8104（原表 130 的 62 倍）。
//
// 下面表内数值是上述公式在整数档上的取整结果，由
// TestDemandTableMatchesCurves 守门，防止表与公式走样。
//
// 结构特征：
//   - 基础食物 94 → 388（S 形，增量峰值在财富 10）；
//   - 简朴衣物 39 → 0（递减），标准衣物 0 → 122（递增），二者是替代关系；
//   - 住宅 20 → 260（饱和型指数，增量从起点 31.8 单调递减到 6.3）。
var demandTableLow = [16][4]float64{
	{39.0, 94.0, 0.0, 20.0},   // 财富 5
	{39.0, 113.0, 1.0, 52.0},  // 财富 6
	{40.0, 135.0, 3.0, 80.0},  // 财富 7
	{40.0, 159.0, 4.0, 105.0}, // 财富 8
	{41.0, 184.0, 6.0, 128.0}, // 财富 9
	{41.0, 210.0, 7.0, 148.0}, // 财富 10  ● D(10)=210（食物锚点）、D(10)=148=74×2（住宅两倍点）
	{37.0, 236.0, 19.0, 166.0},
	{33.0, 261.0, 30.0, 182.0},
	{29.0, 285.0, 42.0, 196.0},
	{25.0, 307.0, 53.0, 209.0},
	{21.0, 326.0, 65.0, 220.0},
	{16.0, 343.0, 76.0, 230.0},
	{12.0, 358.0, 88.0, 239.0},
	{8.0, 370.0, 99.0, 247.0},
	{4.0, 380.0, 111.0, 254.0},
	{0.0, 388.0, 122.0, 260.0}, // 财富 20
}

// 住宅饱和型指数曲线的参数（契约 §6.3 修订时反解确定）。
//
// 保留为常量而非硬编码进表，是为了让"契约改了公式"时能一眼看出
// 公式与表必须同时更新，也便于测试直接按公式复算全表。
const (
	// HousingSaturation 是住宅需求的饱和值 C。
	HousingSaturation = 311.35
	// HousingExpRate 是住宅饱和型指数的速率参数 b。
	HousingExpRate = 0.115726
	// FoodLogisticScale 是基础食物 logistic 的渐近上界 L。
	FoodLogisticScale = 420.0
	// FoodLogisticRate 是基础食物 logistic 的速率参数 k。
	FoodLogisticRate = 0.25
	// FoodLogisticMid 是基础食物 logistic 的拐点（财富档）。
	FoodLogisticMid = 10.0
	// HousingFloor 是住宅饱和型指数的起点值 D(5)。
	HousingFloor = 20.0
	// HousingBaseTier 是住宅饱和型指数的起点财富档。
	HousingBaseTier = 5.0
)

// FoodDemandAt 按契约修订后的 logistic 公式计算基础食物需求（未取整）。
// 供标定与测试复算，不直接参与消费结算（结算走离散表）。
func FoodDemandAt(tier float64) float64 {
	return FoodLogisticScale / (1 + math.Exp(-FoodLogisticRate*(tier-FoodLogisticMid)))
}

// HousingDemandAt 按契约修订后的饱和型指数公式计算住宅需求（未取整）。
func HousingDemandAt(tier float64) float64 {
	return HousingSaturation - (HousingSaturation-HousingFloor)*
		math.Exp(-HousingExpRate*(tier-HousingBaseTier))
}

// DemandTierMin / DemandTierMax 是离散表的财富档边界。
const (
	DemandTierMin = 5.0
	DemandTierMax = 20.0
)

// demandTableHigh 是财富档达到或超过 20 时的取值
// （契约 §6.3 规定 wage ≥ 20 → 财富 20，故取表末行）。
var demandTableHigh = demandTableLow[15]

// DemandTable 返回与契约 §6.3 对齐的完整离散表（16 档）。
// 返回的是副本，调用方修改它不会影响内核。
func DemandTable() []WealthTier {
	out := make([]WealthTier, len(demandTableLow))
	for i := range demandTableLow {
		out[i] = WealthTier{
			Wealth:  float64(int(DemandTierMin) + i),
			Per100k: demandTableLow[i],
			// 契约修订时确定的三个关键档：财富 5（起点）、财富 10（食物锚点 + 住宅两倍点）、
			// 财富 20（曲线右端）。
			Anchor: i == 0 || i == int(FoodLogisticMid-DemandTierMin) || i == len(demandTableLow)-1,
		}
	}
	return out
}

// DemandAt 返回任意财富档的每 10 万人标准需求（离散表取整档口径）。
//
// 低于财富 5 取下限、达到或超过财富 20 取上限（§6.3 的边界规则）。
// 若需要档间连续值，用 InterpolatedDemandAt。
//
// 之所以同时提供离散与连续两个入口：福利金结算要求"发多少 → 需求增加多少"可对账，
// 必须落在确定档位上；而价格方程与既有标定沿用的是连续插值口径。
func DemandAt(tier float64) [4]float64 {
	if tier <= DemandTierMin {
		return demandTableLow[0]
	}
	if tier >= DemandTierMax {
		return demandTableHigh
	}
	idx := int(tier) - int(DemandTierMin)
	if idx < 0 {
		idx = 0
	}
	if idx >= len(demandTableLow) {
		return demandTableHigh
	}
	return demandTableLow[idx]
}

// InterpolatedDemandAt 返回档间连续口径的每 10 万人标准需求。
//
// 口径说明：契约 §6.3 的表是"每档一组数"，档间没有定义解析式；
// 本函数在相邻两档之间做线性插值，因此在整档处与 DemandAt 完全相等。
// 注意它插值的是【表值】，不是上面那两条生成曲线——后者只用于生成表，
// 不作为运行时口径（否则会出现"表与公式各算一套"的不一致）。
func InterpolatedDemandAt(tier float64) [4]float64 {
	if tier <= DemandTierMin {
		return demandTableLow[0]
	}
	if tier >= DemandTierMax {
		return demandTableHigh
	}
	lo := int(tier) - int(DemandTierMin)
	hi := lo + 1
	if hi >= len(demandTableLow) {
		return demandTableHigh
	}
	frac := tier - float64(int(tier))
	var out [4]float64
	for k := 0; k < 4; k++ {
		out[k] = demandTableLow[lo][k] + (demandTableLow[hi][k]-demandTableLow[lo][k])*frac
	}
	return out
}

// TotalDemandAt 返回该财富档下每 10 万人的需求合计（四个消费组之和）。
// 用于快速评估"福利金把人口推到更高档位后总需求变化多少"。
func TotalDemandAt(tier float64) float64 {
	d := DemandAt(tier)
	return d[0] + d[1] + d[2] + d[3]
}

// SubsistenceOutput 返回自给农场的产出（§3.3：谷物 ×2、织物 ×1、服装 ×0.5 每份）。
func SubsistenceOutput(units float64) map[int]float64 {
	return map[int]float64{0: units * 2, 2: units * 1, 3: units * 0.5}
}

// ErrInvalid 表示领域数据不满足不变量。
var ErrInvalid = errors.New("model: invalid domain data")

// ValidateGoods 校验商品定义（§3.4 的可行性判据在这里之外，由 calibration 负责）。
func ValidateGoods(gs []Good) error {
	if len(gs) != Goods {
		return ErrInvalid
	}
	for i := range gs {
		if gs[i].Eps <= 0 {
			return ErrInvalid
		}
		if gs[i].Pcost <= 0 || gs[i].Pinit <= 0 {
			return ErrInvalid
		}
		if gs[i].PriceFloorRatio <= 0 || gs[i].PriceFloorRatio >= 1 {
			return ErrInvalid
		}
		if gs[i].PriceCeilRatio <= 1 {
			return ErrInvalid
		}
	}
	return nil
}
