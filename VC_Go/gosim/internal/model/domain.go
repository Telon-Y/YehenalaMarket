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
// 约定：普通建筑占 [0, Goods)，金融区与宅邸庄园固定为最后两个。
const FinanceIndex = Goods

// ManorIndex 是宅邸庄园在建筑类别数组中的下标（§4.5.5）。
const ManorIndex = Goods + 1

// BuildingTypes 是建筑类别总数（11 种普通建筑 + 金融区 + 宅邸庄园 + 仓库 + 消费代理）。
//
// 【§4.5.6 仓库落地后（2026-09-19 第 16 轮）】由 13 增至 **15**：
//
//	[0,11)  11 种生产建筑
//	11      金融区（所有权的显式表达，不参与商品市场）
//	12      宅邸庄园（农业版金融区）
//	13      仓库（国有的贸易枢纽，参与货币流但不生产商品）
//	14      消费代理（虚构的记账主体，零余额、不雇人）
const BuildingTypes = Goods + 4

// WarehouseIndex 是仓库在建筑类别数组中的下标（§4.5.6）。
const WarehouseIndex = Goods + 2

// AgentIndex 是消费代理在建筑类别数组中的下标（§4.5.6）。
const AgentIndex = Goods + 3

// UnemployedSite 是"失业"这一**虚拟劳动场地**的下标（§5.1 / §6.5，2026-09-19 第 15 轮裁决）。
//
// 它不是一个建筑，而是人群池账本里的第 BuildingTypes 个场地：
//
//	失业人口算劳工（阶级 0），但资金池与就业者严格分开；
//	该池没有工资收入，只能收到 §4.5.8 的福利金；
//	福利金关闭时预算为 0 ⇒ 无法消费 ⇒ 满足度与幸福度均为 0
//	⇒ 通过 §6.5 的人口增长条件拉低人口增速。
//
// 由于它是"场地"而不是建筑，凡按建筑下标遍历的循环都不能碰它；
// 人群池账本的 worksites 参数因此取 BuildingTypes + 1。
const UnemployedSite = BuildingTypes

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

// Category 是建筑类别（§3.2，2026-09-19 第 15 轮裁决）。
//
// 【裁决原文】"设置开发类建筑（与农村、城镇、资源并列），其中建造部门显式保持补贴。"
// 于是 1.0 的建筑分成四类，取代此前"只有 arable / 非 arable"的二分：
//
//	农村（CatRural）     ：占用农业用地的产业 + 宅邸庄园（农业资本载体）
//	城镇（CatTown）      ：加工制造、消费品工业、住房与金融区
//	资源（CatResource）  ：采掘业（煤 / 铁）
//	开发（CatDevelopment）：建造部门（以及后续的仓库 / 公共工程）
//
// 类别的**契约作用**（不是装饰性标签）：
//  1. 补贴资格：开发类**显式保持补贴**（AllowSubsidy = true，§4.5.7）；
//  2. 公共工程：政府的公共工程支出只投向开发类（§4.5.8）；
//  3. 投资栈：农村类（占用农业用地者）走庄园栈，其余走金融栈（§4.5.1b）；
//     注意"投资栈判据"仍是 LandKind == "arable"，与本类别是两件事（庄园区分为农村，
//     但它由掌控比推导、不参与扩建）。
type Category uint8

const (
	// CatRural 是农村类建筑。
	CatRural Category = iota
	// CatTown 是城镇类建筑。
	CatTown
	// CatResource 是资源类（采掘业）建筑。
	CatResource
	// CatDevelopment 是开发类建筑（建造部门 / 仓库 / 公共工程）。
	CatDevelopment
)

// String 返回类别的中文名（报告与 CLI 用）。
func (c Category) String() string {
	switch c {
	case CatRural:
		return "农村"
	case CatTown:
		return "城镇"
	case CatResource:
		return "资源"
	case CatDevelopment:
		return "开发"
	}
	return "未知"
}

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
	// LaborPerLevel 是每级雇佣人数（§5 为 5000；金融区 G5 与宅邸庄园为 1000）。
	LaborPerLevel float64
	// Cap 是等级上限（§4.2）。0 表示无上限。
	Cap float64
	// LandKind 标记是否占用耕地："" / "arable"（谷物与棉花共用 ArableCap = 1,000，
	// 2026-09-19 第 21 轮由 500 上调；第 20 轮前为 5,000）。
	LandKind string
	// Category 是建筑类别（农村 / 城镇 / 资源 / 开发，§3.2）。
	//
	// 【2026-09-19 第 15 轮裁决】新增类别维度，与 LandKind（耕地占用）正交：
	// LandKind 决定投资栈归属与耕地上限，Category 决定补贴资格与公共工程投向。
	Category Category
	// IsFinance 标记这是金融区（不参与商品市场）。
	IsFinance bool
	// IsManor 标记这是宅邸庄园（§4.5.5：自给农场的所有权载体，不参与商品市场）。
	//
	// 它是"农业版金融区"：级数由自给农场级数 ÷ 掌控比推导、不建造；
	// 每级雇 1,000 人（独立阶级结构 [ManorWage]：劳工 75% / 农民 20% / 工程师 5%，
	// 本版暂与农业建筑相同），拿货币工资并在市场消费；其收入是【自给农场产出的全部销售收入】。
	IsManor bool
	// IsWarehouse 标记这是**仓库**（§4.5.6 的第 14 类建筑）。
	//
	// 【2026-09-19 第 16 轮落地】仓库是**国有的贸易枢纽**：
	//   - **不生产商品**（没有配方），但**有大量货币流**——它是全部商品贸易的必经节点；
	//   - 可建造（建造成本 100 建造力/级）、每级雇 1,000 人、每级提供 10,000 单位贸易额度；
	//   - 买入价 = 生产者售价（当期市价），卖出价 = 买入价 ×(1+WarehouseMarkup)；
	//   - 加价收入先付自身工资，余额按 §4.5.1 归政府（s_gov = 1）。
	//
	// 因此它与金融区/庄园的语义**不同**：后两者"完全不参与商品市场"，
	// 仓库"不生产商品但参与全部货币流"。见 IsNonMarket / IsTradeNode / Produces 的分工。
	IsWarehouse bool
	// IsAgent 标记这是**消费代理**（§4.5.6 的第 15 类"建筑"，虚构）。
	//
	// 它**不留钱、不雇人、无利润**，纯粹是记账主体：各人群池把钱付给代理，
	// 代理同额付给仓库（逐池透传）。它的账本余额恒为 0（审计断言）。
	IsAgent bool
	// AllowSubsidy 标记该类建筑可否接受政府补贴（§4.5.7）。
	//
	// 【2026-09-19 第 15 轮裁决】开发类建筑**显式保持补贴**：
	// 建造部门 AllowSubsidy = true（此前 R26 把它与其余生产建筑一起置为 false）。
	// 其余类别一律 false。总开关 Params.SubsidyEnabled 仍默认关闭，
	// 由玩家在 1.1 UI 中开启（本版用 CLI `-subsidy` 作校验臂）。
	//
	// 【校验条款】补贴只影响雇佣决策（用含补贴的 MarginEMA），**不得触发扩建**
	// ——§4.1 的扩建判定必须用不含补贴的 ProfitEMA（见 BuildingState.ProfitEMA）。
	AllowSubsidy bool
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
	// Structure 是该建筑的**劳动结构**（§5，2026-09-19 第 20 轮）。
	//
	// 零值（Name 为空）表示"沿用 §5 的城镇通用结构" [UrbanWage]：
	// 75% 劳工 5 元 / 20% 工程师 10 元 / 5% 资本家 20 元 ⇒ 6.75 元/人。
	// 农业建筑与宅邸庄园显式指向 [AgriculturalWage] / [ManorWage]。
	//
	// 它与 LaborPerLevel 是两件事：LaborPerLevel 决定"每级几人"，
	// Structure 决定"这几人是谁、拿多少"。两者相乘的 WagePerLevel()
	// 才是 §3.4 零利润价方程里 l_j 的分子。
	Structure *LaborStructure
}

// StructureOf 返回建筑的劳动结构（零值 ⇒ 城镇通用结构）。
func (b Building) StructureOf() LaborStructure {
	if b.Structure == nil {
		return UrbanWage
	}
	return *b.Structure
}

// WagePerLevel 返回该建筑**每级每周期**的工资总额（元）。
//
// 它是 §3.4 零利润价方程里 l_j = 该值 / 单级产出 的分子，也是 §4.3 初始货币
// 存量标定的基准流量。第 20 轮后它**不再是全局常数**：
//
//	城镇类（默认）  5,000 × 6.75 = 33,750
//	农业类          5,000 × 5.65 = 28,250（谷物农场 / 棉花种植园）
//	宅邸庄园        1,000 × 5.65 =  5,650
//	金融区 / 仓库    1,000 × 6.75 =  6,750
func (b Building) WagePerLevel() float64 {
	return b.LaborPerLevel * b.StructureOf().AverageWage()
}

// Cohorts 是 §5 的阶层比例与工资。
var (
	CohortShares = [3]float64{0.75, 0.20, 0.05}
	CohortWages  = [3]float64{5, 10, 20}
)

// AverageWage 返回按阶层比例加权的平均工资（§5 = 6.75）。
//
// 它是**金融区 / 仓库 / 失业标准**等"非农业劳动结构"的通用平均值，
// 也就是 §5 的城镇劳动结构。**农业建筑与宅邸庄园不再用它**——
// 他们走 [AgriculturalWage]，见 LaborStructure 的说明（§5 第 20 轮口径）。
func AverageWage() float64 {
	var s float64
	for i := range CohortShares {
		s += CohortShares[i] * CohortWages[i]
	}
	return s
}

// LaborStructure 是一个劳动场地的**阶级结构**（§5，2026-09-19 第 20 轮）。
//
// 1.0 的"每级 5,000 人 / 75-20-5 / 5-10-20 元"原本是**全部**建筑的统一口径。
// 第 20 轮裁决把农业部门单列：农业建筑（谷物农场 / 棉花种植园）与宅邸庄园
// 使用"劳工 / 农民 / 工程师"结构，其中**农民工资 7 元**（介于劳工与工程师之间）。
//
// 【为什么用结构而不是再写一套常量】工资总额、零利润价 l_j、人群池拆分、
// 财富档、福利金标准全部依赖"每级的阶级构成"。把它做成一个结构体后，
// 每个建筑只需指向自己那一套，所有下游计算都自动跟着走——
// 否则每处 `AverageWage()` 都要重新问一遍"这个建筑属于哪一类劳动"。
type LaborStructure struct {
	// Name 是结构的可读名（报告与错误信息用）。
	Name string
	// Shares 是三个阶级的人口占比（和应为 1）。
	Shares [3]float64
	// Wages 是三个阶级的工资（元/人/周期）。
	Wages [3]float64
}

func (s LaborStructure) String() string {
	if s.Name == "" {
		return "未命名"
	}
	return s.Name
}

// AverageWage 返回该结构下的人均工资（按占比加权）。
func (s LaborStructure) AverageWage() float64 {
	var v float64
	for i := range s.Shares {
		v += s.Shares[i] * s.Wages[i]
	}
	return v
}

// Equivalent 报告两个结构是否等价（占比与工资逐项相同）。
//
// 用途：报告与文档要说清"宅邸庄园的两种新阶级**暂时**与农业建筑的两种阶级相同"。
// 等价性是**可断言的实现事实**，而不是注释里的承诺。
func (s LaborStructure) Equivalent(o LaborStructure) bool {
	for i := range s.Shares {
		if s.Shares[i] != o.Shares[i] || s.Wages[i] != o.Wages[i] {
			return false
		}
	}
	return true
}

// 三个命名劳动结构（§5，2026-09-19 第 20 轮裁决）。
//
//	城镇  UrbanWage        ：75% 劳工 5 元 / 20% 工程师 10 元 / 5% 资本家 20 元 ⇒ 6.75 元
//	农业  AgriculturalWage ：75% 劳工 5 元 / 20% 农民   7 元 / 5% 工程师 10 元 ⇒ 5.65 元
//	庄园  ManorWage        ：75% 劳工 5 元 / 20% 农民   7 元 / 5% 工程师 10 元 ⇒ 5.65 元
//
// 【为什么农业均薪反而更低】农业结构把城镇结构的"工程师 10 元 / 资本家 20 元"
// 换成了"农民 7 元 / 工程师 10 元"——**后两个阶级的工资总额从 20.00 元降到
// 17.00 元**，故人均从 6.75 降到 5.65（= 0.75×5 + 0.20×7 + 0.05×10）。
// 每级工资总额因此从 33,750 降到 **28,250**，谷物与织物的零利润价随之下移
// （675 → 565 / 750 → 627.78）。这是裁决的**直接算术后果**，不是实现误差。
//
// 【庄园（§4.5.5）】它**不是**城镇结构的改名，而是**一套独立的阶级定义**；
// 本版让它的占比与工资**暂时**与农业建筑相同（ManorWage.Equivalent(AgriculturalWage)
// 为 true）。"暂时相同"是显式登记的过渡状态：后续版本要给庄园自己的
// 地主/佃农口径时，只改 ManorWage 一处，不需要动任何计算。
var (
	// UrbanWage 是城镇劳动结构（也是 §5 的通用口径）。
	UrbanWage = LaborStructure{
		Name:   "劳工/工程师/资本家",
		Shares: [3]float64{0.75, 0.20, 0.05},
		Wages:  [3]float64{5, 10, 20},
	}
	// AgriculturalWage 是农业建筑（谷物农场 / 棉花种植园）的劳动结构。
	AgriculturalWage = LaborStructure{
		Name:   "劳工/农民/工程师",
		Shares: [3]float64{0.75, 0.20, 0.05},
		Wages:  [3]float64{5, 7, 10},
	}
	// ManorWage 是宅邸庄园（§4.5.5）的独立劳动结构，本版暂与农业相同。
	ManorWage = LaborStructure{
		Name:   "劳工/农民/工程师（庄园，暂同农业）",
		Shares: [3]float64{0.75, 0.20, 0.05},
		Wages:  [3]float64{5, 7, 10},
	}
)

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
	// Pzero 是**当期零利润价**（§七 R32 的动态口径）。
	//
	// 静态口径下它恒等于 Good.Pcost（§3.4 由 Leontief 方程解出的那条"长期"零利润价）；
	// 动态口径下每 tick 重算为
	//
	//	P⁰_j(t) = Σ_i A[i][j]·P_i(t) + l_j
	//
	// 即"**让该生产单位在当期投入价格与满编工资下恰好 0 利润**的售价"。
	// 它替代静态 Pcost 进入：需求归一化 D = a(P/P⁰)^(−ε)、价格钳制带 [0.2,5]×P⁰、
	// 以及 A1/A3 的 |ln(P/P⁰)| 判据。
	Pzero float64
	// Mass 是当期惯性 m ≡ 当期市场内流通商品量（= 本 tick 的供给 S，§2.3）。
	Mass float64
	// Period 是由 m 与 K 内生的价格波动周期 T = 2π√(m/K)（§2.3，派生量、只读诊断用）。
	Period float64
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

// ShortageFloor 是原料短缺惩罚的下限（§3.3 定案：惩罚最高 75%）。
//
// 短缺系数 shortageFactor = max(ShortageFloor, min_投入(配给比))，
// 即无论原料多么短缺，建筑至少保留 25% 的名义产出。
//
// 【口径声明】该下限是**玩法规则**：当配给比低于 25% 时，产出高于所投入原料
// 所能支持的数量，投入-产出的严格物质平衡被有意打破。1.0 的硬不变量只约束
// 货币（§4.5.3），不约束物质，故这是裁决接受的后果，不是实现缺陷。
const ShortageFloor = 0.25

// IsNonMarket 报告该建筑**完全不参与商品市场**（金融区、宅邸庄园）。
//
// 二者的共同点：没有投入产出配方、不进任何商品的生产/消费/扩建循环，
// 只是所有权或收入的载体。凡"跳过非生产建筑"的循环都应当用它，
// 只跳过金融区会把宅邸庄园错误地当成生产建筑（曾导致下标越界）。
//
// 【与仓库/消费代理的区别（§4.5.6）】仓库与消费代理**不生产商品，但有货币流**：
// 仓库是全部商品贸易的必经节点（收付货款、赚加价、缴增值税），消费代理是零余额的透传主体。
// 因此：
//
//	IsNonMarket()  —— 完全离场（金融区 / 宅邸庄园）：连货币流也不经过商品路径
//	IsTradeNode()  —— 贸易节点（仓库 / 消费代理）：有货币流，但没有配方
//	Produces()     —— 真正的商品生产者：有配方，进生产 / 配给 / 利润率 / 扩建循环
func (b Building) IsNonMarket() bool { return b.IsFinance || b.IsManor }

// IsTradeNode 报告该建筑是 §4.5.6 的贸易节点（仓库 / 消费代理）。
func (b Building) IsTradeNode() bool { return b.IsWarehouse || b.IsAgent }

// Produces 报告该建筑是**商品生产者**（有投入产出配方）。
//
// 【为什么必须有这个方法】生产、配给、中间投入计价、利润率、扩建意向等循环
// 此前一律写 `if b.IsNonMarket() { continue }`。仓库与消费代理落地后，
// 这个条件不再等价于"是商品生产者"：仓库/代理没有配方，若被当作生产者，
// 它们的零值 Recipe.Output(=0，即谷物) 与零值 Qty 会污染谷物市场与对账。
// 因此上述循环一律改用 `!b.Produces()` 判据。
func (b Building) Produces() bool { return !b.IsNonMarket() && !b.IsTradeNode() }

// Params 是全局参数。全部字段对应契约条文，便于逐条核对。
type Params struct {
	// TicksPerYear 是 1 年的 tick 数（D9：1 tick = 1 周 ⇒ 52）。
	TicksPerYear int

	// PricePeriod 是价格波动周期 T 的**参考值**（§2.5，默认 26）。
	//
	// 【2026-09-19 起不再参与动力学】§2.3 已改为 m = 当期流通商品量、
	// T = 2π√(m/K) 为内生量；本字段只作诊断与对照用的参考周期保留。
	PricePeriod float64
	// Damping 是阻尼比 zeta（§2.5 建议 0.7）。§2.3 改后 ζ 是周期侧唯一的旋钮。
	Damping float64
	// DT 是每 tick 的等效时间步（§2.4 的 dt = 0.5）。
	DT float64
	// Substeps 是每 tick 的 RK4 子步数（§2.4 的 n_sub = 10）。
	Substeps int
	// ScaleGamma 是规模对周期的指数（§2.5，默认 0 = 尺度不变）。
	//
	// 【2026-09-19 起不生效】m 与 K 都随产出（人口）同比缩放，
	// T = 2π√(m/K) 因而天然尺度不变，无需 γ 修正；保留待后续版本使用。
	ScaleGamma float64

	// ExpandThreshold 是扩建利润率阈值（§4.1 的 10%）。
	ExpandThreshold float64
	// ExpandPer5Pct 是"每超过 5 个百分点多扩 1 个"的步数（§4.1）。
	ExpandPer5Pct float64
	// ExpandMaxRatio 是单次扩建上限占当前总数的比例（§4.1 的 10%）。
	ExpandMaxRatio float64

	// BasketScale 是 §6.3 需求篮子的**整体缩放系数**（2026-09-19 第 24 轮新增）。
	//
	// 默认 **1.0**（不改动现行基线）。把它调大 ⇒ 每档的目标消费量按比例提高 ⇒
	// 篮子价值向"该档工资"靠拢 ⇒ 消费/工资回升、价格能停在覆盖成本的水平。
	//
	// 【依据】R30 实测三档篮子只值该档工资的 18.6% / 26.4% / 25.6%，即**统一的量级缺口**；
	// R48 归因证实"消费品部门集体亏损"与"消费/工资 ≈ 0.2"是同一件事。
	// 故这是"不改门类、只调数量"的最小实验旋钮：把它逐档调到 1.0 附近即可检验
	// "篮子太小"是否就是涨不动的主因。
	//
	// 【注意它不改变门类】扩大品类属契约 §0.4 第 23/25 项的独立决策（需要 v3 目录之类的输入）。
	BasketScale float64

	// PowerByQueue 让建造部门的产能由"队列深度 ÷ 目标周期"驱动（§4.1，第 23 轮新增）。
	//
	// 默认 **true**：建造部门**豁免** §4.1 的"利润率 > 10% 就扩建"，改走
	// `build.PowerCapacityUnits`——只在"按当前产能无法在 QueueWarnTicks 个周期内
	// 消化在手订单"时才扩产。置 false 可回退到"建造部门也按利润率扩建"的旧口径
	// （R45/R46 实测的自指正反馈形态），供审计对照。
	PowerByQueue bool

	// PowerAnchorByQueue 把建造力的**需求锚**改为"在手订单 ÷ 队列目标周期"
	// （§3.4 方案 C′ 的口径修改，第 23 轮新增）。
	//
	// 默认 **true**。置 false 可回退到"在手订单**绝对量**"的旧口径，供审计对照——
	// 实测两者的长跑形态差异很大，见 docs/ACTIVE.md §七 R47。
	PowerAnchorByQueue bool

	// AcquireFromInvestment 允许**收购用投资池余额出资**（§4.5.1a，第 28 轮新增）。
	//
	// 默认 **true**（用户裁决："允许收购用投资池余额出资"）。
	// 置 false 时收购只能用资本池现金（第 27 轮口径）。
	//
	// 【为什么需要这个开关】有一条审计断言要在**受控条件下反解对价公式**
	// （`TestAuditTask1PriceIsBuildCost`：把资本池注到 1e15 以排除"付不起"，
	// 于是 `privatizePaid / privatizeUnits` 必须恰好等于"建造成本 × 建造力价"）。
	// 若投资池也参与出资，同一 tick 的成交额会是两个付款方的混合，公式反解失效。
	AcquireFromInvestment bool

	// ExpectedMarginFloor 是 §5.2 **增雇判定**的下限（2026-09-19 第 22 轮新增）。
	//
	// 第 22 轮把增雇条件从"当期利润率 > 0"改为"**期望扩招后**利润率 > 本值"：
	//
	//	ΔO/O   = (HireAnnual/TicksPerYear) / hireRate
	//	P'/P   = (1+ΔO/O)^(−1/ε)          ε = §3.1 逐商品需求弹性
	//	margin'= margin · P'/P
	//	增雇 ⟺ margin' > ExpectedMarginFloor
	//
	// 默认 **0**（只有仍为正才扩招）。取负值会让建筑"亏着也扩招"（不建议）；
	// 把它与"扩招压价折现"一起看：折现系数恒为 1（`NoExpandDilution` 对照臂）
	// 时本参数就退化为旧口径的"当期利润率 > 0"。
	ExpectedMarginFloor float64

	// NoExpandDilution 关闭 §5.2 的"扩招压价折现"（2026-09-19 第 22 轮新增）。
	//
	// 默认 false（折现生效，即第 22 轮的新口径）。置 true 时折现系数恒为 1，
	// §5.2 的增雇判定退化为**第 20 轮的旧口径**（只看当期利润率 EMA > 0）——
	// 它是审计与对照实验用的"历史臂"，不是可推荐的默认值。
	NoExpandDilution bool

	// ExpandPlanHorizon 是 §5.2 增雇判定的**前瞻步数**（2026-09-19 第 22 轮新增）。
	//
	// 它决定"扩招一步"指的是扩多少：折现用的相对产量增幅为
	//
	//	ΔO/O = (HireAnnual/TicksPerYear × Horizon) / hireRate
	//
	//	= 1：只折现"本 tick 的那一小步"（0.096%）。**对消费类商品几乎没有约束力**
	//	  （ε=0.2 时只折 0.48%），但对**建造部门**有效：它 100% 满编 ⇒ 折现系数 0
	//	  ⇒ 直接判为不增雇（见 ExpectedMarginFloor 与 ACTIVE §七 R46 的实测）。
	//	= TicksPerYear（52，**默认**）：按"一年后的雇佣率"前瞻（ΔO/O 达 5%），
	//	  对消费类商品也开始有约束力（ε=0.3 折 14.9%、ε=1.5 折 3.3%）。
	//
	// 【默认值定案（第 22 轮）】取 **52**：裁决的措辞是"期望**扩招后**的利润率"——
	// "扩招"是一个年度政策动作（HireAnnual = 5%/年），不是 0.096% 的单步微调；
	// 单步折现对全部商品都近乎无效（A/B 逐位相同），等于没改。
	ExpandPlanHorizon int

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
	//
	// 【2026-09-19 裁决（R27）】由 0.10 下调到 **0.05**（"统税 5%"）。
	//
	// 【2026-09-19 第 16 轮（仓库落地）】**商品贸易不再使用本参数**：
	// 全过程税 $t$ 正式由 §4.5.6 的两段税 $(\nu,\tau)$ 取代（见 VATRate / ConsumeTaxRate），
	// 商品路径的每一笔货款都按两段税计征。
	// 本字段仅保留给**非商品交易**——当前只有私有化（§4.5.1a 的股权转让对价按含税口径支付）；
	// 取 0.05 与统税口径一致。
	TaxRate float64

	// ===== §4.5.6 仓库与消费代理（2026-09-19 第 16 轮落地）=====

	// VATRate 是**增值税** $\nu$：生产者 → 仓库环节的税率（§4.5.6）。
	//
	// 计征方式：仓库按"生产者售价 ×(1+ν)"付款给生产者（生产者收净额、政府收税额）。
	// 默认 **0.025**（R27 定案：ν = τ = 2.5%，综合税负 ≈4.94%）。
	VATRate float64
	// ConsumeTaxRate 是**消费税** $\tau$：仓库 → 买家环节的税率（§4.5.6）。
	//
	// 计征方式：买家按"仓库卖出价 ×(1+τ)"付款（仓库收净额、政府收税额）。
	// 买家包括**消费代理**（居民最终消费）与**生产建筑**（中间投入）——本版不设进项抵扣。
	// 默认 **0.025**。
	ConsumeTaxRate float64
	// WarehouseMarkup 是仓库的**固定加价率**（§4.5.6）：卖出价 = 买入价 ×(1+WarehouseMarkup)。
	//
	// 默认 **0.05**（契约 2026-09-19 定案 5%）。加价收入先付仓库自身工资，余额归政府。
	// 【必须 > VATRate】加价是仓库的毛利，增值税是它代缴的税：净毛利 = (markup − ν)×货值。
	WarehouseMarkup float64
	// WarehouseQuotaPerLevel 是**每级仓库提供的贸易额度**（单位数/周期，§4.5.6）。
	//
	// 默认 **10,000**。当某 tick 的贸易量超过"现有级数 × 本参数"时，
	// 政府自动下达仓库扩建订单（走 §4.2 的常规建造队列，国库付款）。
	WarehouseQuotaPerLevel float64
	// InitialWarehouseLevel 是仓库的起步等级（建模值，与 InitialPowerLevel 同性质）。
	//
	// 契约没有规定开局仓库级数。取 **0 级**：开局不建仓库，由 §4.5.6 的
	// "贸易量 > 级数×额度 ⇒ 政府自动扩建"规则在 **tick 1** 就下达第一张仓库订单——
	// 这样自动扩建这条机制在默认参数下**始终被激活并可校验**
	//（与第 15 轮"显式标注短缺起步，这样才能校验大部分功能"同一思路）。
	// 20m 人口下贸易量约 3,300~4,500 单位/tick，故扩建后 1 级（额度 10,000）即可覆盖；
	// 人口或贸易量增长到 10,000 单位以上时会继续自动扩建。
	InitialWarehouseLevel float64

	// BuyerWedge 是买家的**总加载系数**（§4.5.6 的方法，见文件末尾）：
	//
	//	买家实付 = 生产者售价 ×(1+WarehouseMarkup)×(1+ConsumeTaxRate)
	//
	// 它同时是 §3.4 标定方程里投入项与需求价值的加载系数——因为**买家的实际成本**
	// 才是"实际成本基"利润率（§3.1）的口径，而消费者的购买力也按这个价格衡量。
	// （见 calibrate.Run 的 buyerWedge 形参：价格方程由 p = w·Aᵀp + l 给出。）

	// ControlPerFinance 是每级金融区（以及宅邸庄园，§4.5.5）可掌控的其余建筑级数。
	//
	// 契约 §4.5.2 的 c_ctrl：金融区级数 N_finance = Σ非农业生产建筑等级 / c_ctrl；
	// §4.5.5 的宅邸庄园同理 N_manor = (N_subsistence + Σ农业建筑等级) / c_ctrl
	//（2026-09-19 第 11 轮改写：求和里新增农业建筑与自给农场）。
	//
	// 【2026-09-19 裁决】由 5 上调到 20：掌控比越大 ⇒ 同样的资产只需更少的
	// 金融区/庄园级数 ⇒ 金融与庄园的工资义务同比下降到 1/4。
	ControlPerFinance float64

	// PopGrowthHi 是必需品满足度 100% 时的年化增长率（§6.5 的 +5%）。
	PopGrowthHi float64
	// PopGrowthLo 是满足度 0% 时的年化增长率（§6.5 的 −20%）。
	PopGrowthLo float64
	// PopSatTarget 是人口零增长的满足度（§6.5 的 75%）。
	PopSatTarget float64

	// ArableCap 是耕地上限（谷物 + 棉花，§4.2）。
	//
	// 【2026-09-19 第 21 轮】由 500 上调到 1,000（"耕地上限 1,000 级"），配合同轮把开局
	// 人口默认降到 5m：自给农场级数 = 未使用耕地 × 1.0 约 **994 级**，满编劳动力约
	// **497 万**，而 5m 人口的备用劳动力约 474 万 ⇒ 自给农场未满编（雇佣率 ≈0.95）、
	// 失业约 4.6%。对照"耕地 500 + 人口 10m"：容量 245 万 vs 备用 974 万 ⇒ 失业 71.7%。
	// 历史：第 20 轮由 5,000 下调到 500（当时失业率因此升到 71.7%），再前一轮为 10,000。
	ArableCap float64
	// SubsistenceScale 是每单位未使用耕地对应的自给农场数。
	//
	// 【契约 §4.2 定案（2026-09-19）：1 级未使用土地 → 1 级自给农场，故本值为 1.0。】
	//
	// 历史：本字段原取 0.05（架构 §5.3 的 R3 建议值），并在注释中记录"该值会让系统
	// 崩解"。裁决改为 1:1 后的已知后果（实测见 docs/ACTIVE.md §七 R15）：
	// 契约 §4.2 的耕地上限是 10,000 级，而 §6.3 的需求量级只用到约 5 级耕地，
	// 于是 99.95% 的耕地都算"未使用"，会生成约 9,995 级自给农场，其谷物产出
	// 远高于专业农场，把谷物价格压到地板价。这是**裁决后的既定后果**，
	// 不是实现缺陷；命令行仍可用 -subsist 做诊断性覆盖。
	SubsistenceScale float64

	// SubsistenceLaborPerLevel 是每级自给农场需要的自给农人数（§4.2 修订：5,000）。
	//
	// 自给农场是**备用劳动力池**：劳动力先满足一般生产建筑（含金融区与宅邸庄园），
	// 余量再配置给自给农场，仍有余量即为失业。自给农的消费已经在 §3.3 的配方里
	// 约去（谷物2/织物1/服装0.5 是【净产出】），故他们不领货币工资、不进市场购买。
	SubsistenceLaborPerLevel float64

	// DecayFrozen 冻结 §4.4 的建筑自动缩减（2026-09-19 裁决）。
	//
	// 为 true 时主循环不执行缩编：等级只增不减，`IdleTicks` 仍照常统计
	// （诊断与后续版本需要）。设为 false 可恢复 §4.4 的原始行为。
	DecayFrozen bool

	// ===== §3.4 价格锚定：已裁决采用的方案 A + C′（2026-09-19）=====
	//
	// 裁决：采用 A（支出份额锚，ε≡1）+ C′（中间品/建造力按派生需求每 tick 重锚）。
	// 这两个字段现在是**契约默认行为**（默认 true）；置 false 可回退到历史口径
	// （常弹性锚 ε 取自 §3.1 + 中间品锚钉在开局净供给上），仅用于对照实验。
	// 背景与量化：docs/ACTIVE.md §6.4（价格锚定备选方案）。

	// AnchorExpenditureShare 启用【方案 A：支出份额锚】（默认开启）。
	//
	// 需求取 ε ≡ 1（等支出份额，D_i = a_i/P_i），标定常数 a_i = S₀_i·P_init_i，
	// 于是 P*(λ) = P_init/λ：价格与短缺直接挂钩，贴底点从 1.5~3.4 倍产能抬到 6~8 倍。
	// 代价（已裁决接受）：§2.4 的"必需品扩产迅速抹平自身利润"（∂lnP*/∂lnS = −1/ε）失效，
	// 所有商品统一为 −1。
	AnchorExpenditureShare bool

	// AnchorDerivedDemand 启用【方案 C′：中间品派生需求锚】（默认开启）。
	//
	// 没有家庭最终需求的商品（煤/铁/钢/工具/建造力）不再用开局净供给作锚，
	// 而是每 tick 按派生需求重锚：
	//   中间品：a_g = (Σ_j 等级_j · q_gj) · r_g^ε      （下游产能口径）
	//   建造力：a_g = (在手订单的建造力需求) · r_g^ε     （投资需求口径）
	// 派生需求为 0 时回退到开局标定锚（避免零需求 ⇒ 无均衡）。
	//
	// 【为什么不是"纯派生需求"】若 D 与价格完全无关，则 K = −E′(P) ≡ 0、ρ = 0，
	// 价格方程退化为 m·P̈ = E ⇒ 等加速发散（实测钢 393 tick 撞底锁死）。
	// 故 C′ 用派生需求定 **锚的位置**、仍用价格弹性定 **锚的响应**。
	AnchorDerivedDemand bool

	// ===== §4.5.7 政府补贴（2026-09-19 裁决）=====

	// SubsidyEnabled 是补贴机制的【总开关】（默认 false）。
	//
	// 补贴是转移支付（借政府 / 贷建筑），不参与 §4.5.1 的利润划分，也不计入 GDP；
	// 它受 §4.5.4 的可动用资金（现金余额与债务上限）约束。
	SubsidyEnabled bool
	// SubsidyCap 是每周期对【单个建筑类别】的补贴上限（元）；0 表示不限（仅受债务上限约束）。
	SubsidyCap float64

	// ===== §5.3 储蓄渠道（2026-09-19 第 15 轮裁决）=====

	// SavingsRate 是居民**工资结余的储蓄率** σ_save ∈ [0,1]（§5.3）。
	//
	// 【裁决原文】"显式定义「工资结余 → 储蓄 → 投资」渠道"。
	// 每个 tick 消费结算之后，各人群池尚未花掉的现金按 σ_save 比例转入**总投资池**
	// （§4.5.1b 的同一账户），构成扩建资金的一部分；剩余的 (1−σ_save) 留在池中
	// 作为现金持有。默认 **1.0**——即"结余全部储蓄"，这是裁决的字面口径；
	// 1.1 UI 阶段把它交给玩家作政策旋钮。
	//
	// 【为什么必须有这条】R29/R30 实测：§6.3 的数量篮子只值该档工资的 21.5%~28.8%，
	// 而 §5 又规定"全部工资用于消费"——两者的差额此前无处可去，只能沉积在人群池
	// （R29：tick 1000 达 2.58e9），表现就是"投资池进不来钱、经济不扩建"。
	SavingsRate float64

	// ===== §4.5.8 政府支出端（2026-09-19 第 15 轮裁决）=====

	// WelfareTier 是福利金档位，取值 **0–6**（§4.5.8）。
	//
	//	档 0 = 关闭；档 t∈[1,6] 的补贴率 = 20%·t（最高 120%）
	//	每人每周期补贴 = min(1.2, 0.2·t) · max(0, w̄ − w_i)
	//
	// 其中 w̄ = 平均工资标准 = 6.75 元（§5 的阶层加权平均，model.AverageWage()），
	// w_i 是该人口的工资（劳工 5 / 工程师 10 / 资本家 20 / 失业 0）。
	// 即"将不足平均工资的人口，补贴其差距的 20%×档位"。
	// 默认 0（关闭）：转移支付不应在未声明时发生（与 §4.5.7 同一条纪律）。
	WelfareTier int
	// PublicWorksShare 是**公共工程支出占当期税收的比例** ∈ [0,1]（§4.5.8）。
	//
	// 政府每 tick 用该比例的本期税收（受 §4.5.4 可动用资金约束）直接采购建造力，
	// 投入**开发类建筑**的扩建；新增等级归**政府**（GovLevel），
	// 与私人扩建（投资池出资、归资本建筑）区分开。它是政府的**真实支出**
	// （不是转移支付），给 R27/R34 暴露的"政府只有收入没有支出端"补上出口。
	// 默认 0.5；CLI `-public-works` 可覆盖。
	PublicWorksShare float64

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

	// GovInitialShare 是**开局**政府持股比例 s_gov（§4.5.1）。
	//
	// 【2026-09-19 裁决】由 0.70 下调到 0.30。同一裁决同时**删除了留存比例
	// RetainRatio**：一笔运营纯利 π_i 先补足建筑自身现金池到营运资金目标 C*_i，
	// 补足之后的余额 π^net_i 才按**当期**持股比例支付（政府 → 政府池，
	// 私人份额 → 该建筑所属的资本建筑池），不存在"留存比例"这个参数。
	//
	// 它只在两处生效：① 开局所有权拆分；② 持股比例无法从级数算出时的兜底
	// （Level ≤ 0），见 sim.govShare。
	GovInitialShare float64

	// WorkingCapitalPeriods 是营运资金目标 C*_i 覆盖的周期数（§4.5.1）。
	//
	//	C*_i = WorkingCapitalPeriods × (满编工资_i + 满编中间投入_i)
	//
	// 契约 §4.5.1 把 C*_i 定义为"该建筑一个周期的满编营运成本"，故默认 1.0。
	// 它只影响"利润先补足自身现金池"这一步的补足上限 R_i = min(max(π_i,0),
	// max(0, C*_i − B_i))，不影响利润总额与守恒。
	WorkingCapitalPeriods float64

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
	// 若要让私有化更快，应调大 startupCapFraction 或 Params.PrivatizeStep，
	// 而不是改动本倍数——否则对价就不再等于建造成本了。
	PrivatizePriceMult float64
}

// BuyerWedge 返回买家的**总加载系数**（§4.5.6）：
//
//	买家实付 = 生产者售价 × BuyerWedge，  BuyerWedge = (1+WarehouseMarkup)·(1+ConsumeTaxRate)
//
// 【为什么它是一个全局口径而不是局部细节】仓库加价与消费税都落在**买家**身上：
// 生产者卖出 1 单位收 P，而任何买家（消费代理或生产建筑）买 1 单位要付 P·w。
// 于是：
//   - §3.4 的价格方程变成 p = w·Aᵀp + l（投入按买家的实际成本计价）；
//   - §3.1 的"实际成本基"利润率同样按 w·Σ q_ij P_j 计；
//   - §6.3 的"居民工资 = 最终需求价值"里的需求价值也按 w·P 计。
//
// 三者必须用同一个 w，否则标定、判据与成交三套口径分叉。
func (p Params) BuyerWedge() float64 {
	return (1 + p.WarehouseMarkup) * (1 + p.ConsumeTaxRate)
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
		// §6.3 篮子整体系数：默认 1.0（不改基线）；>1 用于检验"篮子太小"假设。
		BasketScale: 1.0,
		// §5.2 第 22 轮：增雇要求"期望扩招后利润率"严格为正。
		ExpectedMarginFloor: 0,
		// 前瞻步数：52 = 按"一年后的雇佣率"前瞻（"扩招"的年度语义，见字段说明）。
		ExpandPlanHorizon: 52,
		// §4.1 第 23 轮：建造部门的产能由"队列深度 ÷ 目标周期"驱动（豁免利润率扩建）。
		PowerByQueue: true,
		// §3.4 第 23 轮：建造力需求锚 = 在手订单 ÷ 队列目标周期（而不是订单绝对量）。
		PowerAnchorByQueue: true,
		// §4.5.1a 第 28 轮：收购可用投资池余额出资（用户裁决）。
		AcquireFromInvestment: true,

		SitePowerLimit: 30,
		QueueWarnTicks: 52,

		DecayWindow:  156,
		DecayRate:    0.05,
		IdleHireRate: 0.75,
		// §4.4 冻结（2026-09-19 裁决）：缩编不执行，等级只增不减。
		DecayFrozen: true,

		HireAnnual: 0.05,
		MarginEMA:  12,

		TaxRate:           0.05,
		// §4.5.2 的掌控比：2026-09-19 由 5 上调到 20（金融区与宅邸庄园共用）。
		ControlPerFinance: 20,

		PopGrowthHi:  0.05,
		PopGrowthLo:  -0.20,
		PopSatTarget: 0.75,

		// §4.2 耕地上限：2026-09-19 第 20 轮由 5,000 下调到 500（"土地等级 500 级"），
		// **第 21 轮回调到 1,000**，并把开局人口默认降到 5m（§8.6）。
		// 自给农场级数 = 未使用耕地 × 1.0：5,000 → 约 494 → 约 **994 级**，
		// 满编劳动力 = 994 × 5,000 ≈ **497 万**。配 5m 人口后：
		// 备用劳动力 ≈ 500 万 − 市场用工（约 26 万）≈ 474 万 < 497 万容量
		// ⇒ 自给农场**未满编**（雇佣率 ≈ 0.95），失业 ≈ 23 万（约 4.6%），
		// 而不是"耕地 500 + 人口 10m"时的 71.7% 失业。
		ArableCap: 1000,
		// §4.2 定案：1 级未使用土地 → 1 级自给农场。
		SubsistenceScale: 1.0,
		// §4.2 修订：每级自给农场 5,000 自给农（备用劳动力池）。
		SubsistenceLaborPerLevel: 5000,

		// 建模选择，非契约参数：见 InitialPowerLevel 的说明。
		InitialPowerLevel: 20,

		// ===== §4.5.6 仓库与消费代理（2026-09-19 第 16 轮落地）=====
		// ν = τ = 2.5%（R27 定案：综合税负 1−(1−ν)(1−τ) ≈ 4.94%）
		VATRate:        0.025,
		ConsumeTaxRate: 0.025,
		// 仓库加价 5%（契约 2026-09-19 定案）
		WarehouseMarkup: 0.05,
		// 每级 10,000 单位贸易额度；开局 0 级（由额度规则自动扩建，见字段说明）。
		WarehouseQuotaPerLevel: 10000,
		InitialWarehouseLevel:  0,

		// 契约修订：每种生产建筑起始 5 级（见 ProductionInitLevel 的说明）。
		ProductionInitLevel: 5,

		// §3.4 裁决（2026-09-19）：价格锚定采用 A（支出份额锚）+ C′（派生需求锚）。
		AnchorExpenditureShare: true,
		AnchorDerivedDemand:    true,

		// §4.5.1 修订（2026-09-19）：删除留存比例，改为"先补足自身现金池、
		// 余额按当期持股分配"；政府初始持股由 0.70 下调到 0.30。
		GovInitialShare: 0.30,
		// §4.5.1 的 C*_i = 一个周期的满编营运成本。
		WorkingCapitalPeriods: 1.0,

		// §4.5.1 修订：私有化机制。
		// 【2026-09-19 第 26 轮：开放全行业私有化】总开关由 false 改为 **true**，
		// 同时把三种"民生类"建筑的 AllowPrivatize 由 false 改为 true（见 BuildingSpecs）。
		// 【前值】第 15 轮为"总开关默认关闭，需显式开启"；命令行 `-privatize=false` 可回退。
		PrivatizeEnabled:   true,
		PrivatizeMargin:    0.05,
		PrivatizeStep:      0.05,
		PrivatizePriceMult: 1.0,

		// §5.3（2026-09-19 第 15 轮裁决）：工资结余 → 储蓄 → 投资。
		// 默认 1.0 = 结余全部储蓄；这是裁决的字面口径，1.1 UI 阶段交给玩家。
		SavingsRate: 1.0,
		// §4.5.8（2026-09-19 第 15 轮裁决）：福利金档位 0–6，默认关闭（0）。
		WelfareTier: 0,
		// §4.5.8：公共工程支出占当期税收的比例，默认 50%。
		PublicWorksShare: 0.5,
	}
}

// GoodSpecs 返回 §3.1 的 11 种商品定义（弹性与两个价格列均来自契约表格）。
//
// 【2026-09-19 第 16 轮：价格列改为**含买家加载系数的标定解**】
//
// 仓库落地后，§3.4 的零利润价方程变为 p = w·Aᵀp + l（w = (1+加价)(1+消费税) = 1.07625），
// 故 P_ref / P_init 整体上移（无中间投入的谷物/织物不变，下游越多升得越多：
// 建造力 7,250 → 8,130.49）。本表由 `calibrate.Run(specs, 1/6, DefaultParams().BuyerWedge())`
// 解出后取整填入；重算方式：
//
//	go test ./internal/sim/ -run TestTmpCalibProbe -v   （临时探针，或看 CLI -calibrate-only 的标定表）
//
// 契约 §3.1 的表格与本表同源；两者的取整差异（≤0.1%）在 §3.4 的"回代最大相对偏差"里报告。
func GoodSpecs() []Good {
	names := []string{
		"谷物", "加工食品", "织物", "服装", "高档服装",
		"煤", "铁", "钢", "工具", "住房", "建造力",
	}
	eps := []float64{0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2}
	// P_ref（零利润价，含买家加载系数 w）
	//
	// 【§5 第 20 轮重解】农业结构（农民 7 元）把谷物/棉花的每级工资从 33,750 降到
	// 28,250，其零利润价随之下移（675 → 565、750 → 627.778）；下游含农产品投入的
	// 部门一并下移（加工食品 1395.750 → 1290.517、服装 821.812 → 742.888、
	// 高档服装 1797.656 → 1688.038）。煤/铁/钢/工具/住房/建造力不含农产品，
	// 故**逐项不变**（1076.775 / 1533.879 / 834.584 / 774.922 / 8130.490）。
	pcost := []float64{565.000, 1290.517, 627.778, 742.888, 1688.038, 1076.775, 1076.775, 1533.879, 834.584, 774.922, 8130.490}
	// P_init（加成价，开局利润率恰为 20%）
	pinit := []float64{678.000, 1678.344, 753.333, 988.758, 2160.775, 1632.048, 1632.048, 2557.790, 1332.097, 1093.649, 14012.498}
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
//
// 【2026-09-19 裁决】金融区**不再是建造出来的建筑**：它是所有权的显式表达，
// 级数由掌控比推导（sim.syncFinanceLevel）且 BuildCost 恒为 0，因此
// **没有"金融区建造成本"这个参数**——原先的 financeBuildCost 形参已删除。
func BuildingSpecs(financeLabor float64) []Building {
	// 商品下标常量，便于核对 §3.3 的表格。
	const (
		grain, food, fabric, clothes, luxury = 0, 1, 2, 3, 4
		coal, iron, steel, tools, housing    = 5, 6, 7, 8, 9
		power                                = 10
	)
	// AllowPrivatize：**2026-09-19 第 26 轮起"开放全行业私有化"**——
	// 11 种生产建筑全部 AllowPrivatize = true（含此前的例外：谷物农场、棉花种植园、住房）。
	//
	// 【前值 → 后值】第 15 轮的口径是"民生产业保持政府控制"：
	// 谷物农场 / 棉花种植园 / 住房为 false，其余为 true。第 26 轮用户裁决"开放全行业私有化"，
	// 三类例外一并打开。**注意**：`AllowPrivatize` 只决定"这一种类**可以**被私有化"，
	// 实际转让还受三个约束（§4.5.1 与 sim 的 ⑦）：
	//  ① 该建筑盈利（`marginEMA > PrivatizeMargin`，默认 5%）；
	//  ② 政府在该建筑上仍有持股（`GovLevel > 0`）；
	//  ③ **资本池付得起对价**（对价 = 建造成本 × 建造力当期价 × 估值倍数）——
	//     这是本轮的真实闸门：资本池在现行参数下长期为负，故"开放"未必等于"成交量上升"。
	//
	// 不参与私有化的仍是**非市场建筑**（金融区 / 宅邸庄园 / 仓库 / 消费代理）：
	// 它们不由建造产生、也没有商品产出，不是"行业"。
	//
	// 【2026-09-19 第 15 轮裁决】Category 按四类标注；AllowSubsidy 只有
	// **开发类（建造部门）**为 true——"建造部门显式保持补贴"。
	specs := []Building{
		{Name: "谷物农场", Category: CatRural, AllowSubsidy: false, BuildCost: 200, Recipe: Recipe{Output: grain, Qty: 50}, LaborPerLevel: 5000, LandKind: "arable", AllowPrivatize: true, Structure: &AgriculturalWage},
		{Name: "加工食品厂", Category: CatTown, AllowSubsidy: false, BuildCost: 600, Recipe: Recipe{Output: food, Qty: 45, Inputs: map[int]float64{grain: 40}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "棉花种植园", Category: CatRural, AllowSubsidy: false, BuildCost: 200, Recipe: Recipe{Output: fabric, Qty: 45}, LaborPerLevel: 5000, LandKind: "arable", AllowPrivatize: true, Structure: &AgriculturalWage},
		{Name: "服装厂", Category: CatTown, AllowSubsidy: false, BuildCost: 600, Recipe: Recipe{Output: clothes, Qty: 100, Inputs: map[int]float64{fabric: 60}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "高档服装厂", Category: CatTown, AllowSubsidy: false, BuildCost: 600, Recipe: Recipe{Output: luxury, Qty: 30, Inputs: map[int]float64{fabric: 25}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "煤矿", Category: CatResource, AllowSubsidy: false, BuildCost: 600, Recipe: Recipe{Output: coal, Qty: 60, Inputs: map[int]float64{tools: 15, coal: 15}}, LaborPerLevel: 5000, Cap: 500, AllowPrivatize: true},
		{Name: "铁矿", Category: CatResource, AllowSubsidy: false, BuildCost: 600, Recipe: Recipe{Output: iron, Qty: 60, Inputs: map[int]float64{tools: 15, coal: 15}}, LaborPerLevel: 5000, Cap: 500, AllowPrivatize: true},
		{Name: "炼钢厂", Category: CatTown, AllowSubsidy: false, BuildCost: 800, Recipe: Recipe{Output: steel, Qty: 90, Inputs: map[int]float64{iron: 60, coal: 30}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "工具厂", Category: CatTown, AllowSubsidy: false, BuildCost: 800, Recipe: Recipe{Output: tools, Qty: 80, Inputs: map[int]float64{steel: 20}}, LaborPerLevel: 5000, AllowPrivatize: true},
		{Name: "住房", Category: CatTown, AllowSubsidy: false, BuildCost: 800, Recipe: Recipe{Output: housing, Qty: 60, Inputs: map[int]float64{steel: 5, tools: 5}}, LaborPerLevel: 5000, AllowPrivatize: true},
		// 开发类：建造部门。它是投资品部门（产出的买家只有投资/公共工程），
		// 故显式保持补贴资格——见 §4.5.7 与 §4.5.8 的公共工程。
		{Name: "建造部门", Category: CatDevelopment, AllowSubsidy: true, BuildCost: 100, Recipe: Recipe{Output: power, Qty: 15, Inputs: map[int]float64{steel: 25, iron: 25, tools: 20}}, LaborPerLevel: 5000, Cap: 1000, AllowPrivatize: true},
		// 金融区：所有权的显式表达，不建造、不消耗建造力、不参与缩编。
		// 其级数由掌控比推导（N = Σ其余等级 / c_ctrl），故 BuildCost 恒为 0。
		{Name: "金融区", Category: CatTown, BuildCost: 0, LaborPerLevel: financeLabor, IsFinance: true, AllowPrivatize: false},
		// 宅邸庄园（§4.5.5）：农业版金融区 —— 自给农场的所有权载体，
		// 不建造、不参与商品市场；级数 =（自给农场级数 + 农业建筑等级）÷ 掌控比；
		// 每级 1,000 人（劳工/农民/工程师），收入 = 自给农场产出的销售收入。
		//
		// 【§5 第 20 轮裁决】庄园**不是**"把城镇阶级改个名"，而是一套**独立的
		// 阶级定义**（ManorWage）；本版让它的占比与工资**暂时**与农业建筑相同
		// （ManorWage.Equivalent(AgriculturalWage) == true）。这是显式登记的
		// 过渡状态：后续版本改庄园口径时只动 ManorWage 一处。
		{Name: "宅邸庄园", Category: CatRural, BuildCost: 0, LaborPerLevel: financeLabor, IsManor: true, AllowPrivatize: false, Structure: &ManorWage},
		// 仓库（§4.5.6，2026-09-19 第 16 轮落地）：第 14 类建筑。
		//
		// 国有（s_gov = 1）的贸易枢纽：不生产商品（无配方），但**全部商品贸易都经过它**。
		//   - 可建造：100 建造力/级（与建造部门同价）；
		//   - 每级雇 1,000 人（劳工 75% / 教士 20% / 贵族 5%，均值 6.75 ⇒ 6,750 元/级/周期）；
		//   - 每级 10,000 单位贸易额度（超出即触发政府自动扩建）；
		//   - 卖出价 = 买入价 ×(1+5%)，加价先付自身工资、余额按 §4.5.1 归政府；
		//   - 是开发类建筑，**显式保持补贴**（§4.5.7），且可被公共工程投向（§4.5.8.1）。
		//
		// 注意它的 Recipe 为零值（Output = 0、Qty = 0）：凡"按配方"遍历的循环
		// 必须用 Produces() 而不是 IsNonMarket() 过滤，否则它会被误当作谷物农场。
		{Name: "仓库", Category: CatDevelopment, AllowSubsidy: true, BuildCost: 100, LaborPerLevel: 1000, IsWarehouse: true, AllowPrivatize: false},
		// 消费代理（§4.5.6，第 15 类"建筑"）：虚构的记账主体。
		// 不留钱、不雇人、无利润；人群池把钱付给它，它同额付给仓库（逐池透传）。
		// 它的账本余额恒为 0（审计断言），因此不参与利润归属、不进入人群池的场地循环。
		{Name: "消费代理", Category: CatDevelopment, BuildCost: 0, LaborPerLevel: 0, IsAgent: true, AllowPrivatize: false},
	}
	return specs
}

// IsDevelopment 返回该建筑是否属于开发类（§3.2 裁决）。
//
// 开发类是公共工程支出（§4.5.8）的唯一投向，也是"显式保持补贴"的类别。
func IsDevelopment(b Building) bool { return b.Category == CatDevelopment }

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
	return DemandAtScaled(tier, 1.0)
}

// DemandAtScaled 与 DemandAt 同，但把整张需求表乘一个**篮子系数**（§6.3 第 24 轮新增）。
//
// 【它解决什么问题】§6.3 的数量篮子按成本价只值该档工资的 **18.6% / 26.4% / 25.6%**
// （档 5 / 10 / 20，R30 实测），于是"按档买满"必然剩下大部分工资、价格落不到 P_ref、
// 消费品部门长期亏损（R48 归因，契约 §0.4 第 25 项）。把整表乘以一个系数是**不改门类**、
// 只调"数量"的最小改动：它提高每档的目标消费量，使篮子价值向该档工资靠拢。
//
// 【为什么是"整表统一乘"而不是逐组调】逐组调会改变四组之间的构成（例如把住房抬得比
// 食物还高），而"篮子总量不够"是一个**统一的量级问题**——R30 早已指出三档比率
// 落在很窄的区间（18.6%~26.4%），说明差的正是一个统一系数。
//
// scale ≤ 0 或为 1 时退化为原表（默认，保证不改动现行基线）。
func DemandAtScaled(tier, scale float64) [4]float64 {
	d := demandAtRaw(tier)
	if scale == 1 || scale <= 0 {
		return d
	}
	for k := range d {
		d[k] *= scale
	}
	return d
}

// demandAtRaw 是 §6.3 的原始离散表查表（不含篮子系数）。
func demandAtRaw(tier float64) [4]float64 {
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
