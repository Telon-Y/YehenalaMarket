// Package cohort 实现 1.0 契约 §5.1「人群与阶级现金池」。
//
// ============================ 契约变动（本次并入） ============================
//
// 原契约 §5 只为每【级建筑】记一笔工资成本，§8 主循环里也没有任何一步
// 把这笔钱记入居民账户。后果是工资只作为成本从利润里扣减，钱从未真正付出：
// 实测工资总额 2.44e7/tick 而消费支出只有 1.93e7，差额凭空蒸发，
// 货币总量每 tick 净损 15%~34%，5 个周期后变为负数（见 docs/ACTIVE.md §6.5）。
//
// 本包建立【真实的人群】：
//
//	每个劳动场地（每类建筑）× 每个阶级 = 一个独立现金池。
//	3 个阶级 × 12 类建筑 = 36 个池。
//
// 工资从建筑现金池【实际划转】到这些池；消费从这些池【实际扣款】。
// 于是"居民"第一次在模型里拥有了资产负债表，货币闭环才可能成立。
//
// ============================ 财富档口径 ============================
//
// 契约 §6.3：财富等级由平均工资插值确定（wage ≤ 5 → 档 5；5~10 与 10~20 线性；
// wage ≥ 20 → 档 20）。
//
// 本包按【池内人均工资】定价，而不是按全场地的平均工资：
//
//	每级建筑雇 LaborPerLevel 人，阶级比例为 75% / 20% / 5%，阶级工资 5 / 10 / 20。
//	每级工资总额 = 5000 × 6.75 = 33,750，与契约 §5 完全一致（口径不变）。
//
// 但三个阶级的人均工资分别是 5 / 10 / 20，落在【三个不同的财富档】上。
// 用全场地的平均工资 6.75 会让三个阶级落到同一个档（6.75 插值后约等于档 7），
// 阶级差异被平均掉——这既不符合 §5 的工资表，也让"按档发福利金"失去意义。
//
// 因此：逐池查表 → 按人口加权汇总，等价于对全场地的"平均后财富等级"套用需求表，
// 但保留阶级间的差异。契约定为 §5.1 的正式口径。
package cohort

import (
	"yehenala/market/internal/ledger"
	"yehenala/market/internal/model"
)

// ClassCount 是阶级数（§5：劳工 / 工程师 / 资本家）。
const ClassCount = 3

// Pool 是一个人群现金池：某类建筑的某个阶级。
type Pool struct {
	// Worksite 是劳动场地（建筑类别下标）。
	Worksite int
	// Class 是阶级下标（0 劳工 / 1 工程师 / 2 资本家）。
	Class int
	// Population 是该池的人口数。每级建筑 = LaborPerLevel × 阶级比例 × 雇佣率 × 级数。
	Population float64
	// Acc 是该池在共享审计账本中的账户标识。
	//
	// 【§4.5.3 修订】现金余额不再由本结构自行持有，而是存放在
	// ledger.Auditor 里——余额只有一份，"同一笔钱被两个池各记一次"
	// 在结构上不可能发生。
	Acc ledger.Account
	// Auditor 是共享的审计账本。
	Auditor *ledger.Auditor
}

// Cash 返回该池的现金余额（读自审计账本）。
func (p *Pool) Cash() float64 {
	if p == nil || p.Auditor == nil {
		return 0
	}
	return p.Auditor.Balance(p.Acc)
}

// SetInitial 设定该池的开局余额（仅用于开局注资）。
func (p *Pool) SetInitial(v float64) {
	if p == nil || p.Auditor == nil {
		return
	}
	p.Auditor.SetBalance(p.Acc, v)
}

// Ledger 持有全部人群现金池，并负责工资划转与消费扣款。
type Ledger struct {
	Pools []Pool
	// Auditor 是共享的审计账本（与政府、资本、建筑共用同一实例）。
	Auditor *ledger.Auditor
	// Classes 是阶级数（用于账户下标编码）。
	Classes int
}

// NewLedger 建立全部池。worksites 是建筑类别数（含金融区）。
//
// 初始现金为 0：居民的第一次收入来自第一期工资。若给初始现金，
// 等于在系统里凭空注入一笔没有来源的货币，违反 §4.5.3 的货币守恒。
func NewLedger(a *ledger.Auditor, worksites int) *Ledger {
	pools := make([]Pool, 0, worksites*ClassCount)
	for w := 0; w < worksites; w++ {
		for c := 0; c < ClassCount; c++ {
			pools = append(pools, Pool{
				Worksite: w,
				Class:    c,
				Acc:      ledger.Household(w, c, ClassCount),
				Auditor:  a,
			})
		}
	}
	return &Ledger{Pools: pools, Auditor: a, Classes: ClassCount}
}

// Account 返回某池在审计账本中的账户。
func (l *Ledger) Account(i int) ledger.Account {
	if i < 0 || i >= len(l.Pools) {
		return ledger.Account{}
	}
	return l.Pools[i].Acc
}

// TotalCash 返回全部人群现金池之和。
func (l *Ledger) TotalCash() float64 {
	var s float64
	for i := range l.Pools {
		s += l.Pools[i].Cash()
	}
	return s
}

// TotalPopulation 返回全部池的人口之和。
func (l *Ledger) TotalPopulation() float64 {
	var s float64
	for i := range l.Pools {
		s += l.Pools[i].Population
	}
	return s
}

// WealthTier 返回一个池的财富档（§6.3：由人均工资插值）。
//
// 池内人均工资 = 阶级工资（5 / 10 / 20），人口为 0 时无意义，返回档下限。
func (l *Ledger) WealthTier(i int) float64 {
	p := &l.Pools[i]
	if p.Population <= 1e-12 {
		return model.DemandTierMin
	}
	// 池是单一阶级，故人均工资就是该阶级工资。
	wage := model.CohortWages[p.Class]
	if wage <= model.DemandTierMin {
		return model.DemandTierMin
	}
	if wage >= model.DemandTierMax {
		return model.DemandTierMax
	}
	return wage
}

// Targets 按人口与财富档汇总四个消费组的目标需求量（§6.3 离散表）。
//
// target_g = Σ_pools DemandAt(tier_pool)[g] × population_pool / 100000 × demandScale
//
// 这等价于"对全场地的平均后财富等级套用需求表再按人口加权"，
// 但保留各阶级落在不同档位的差异。
func (l *Ledger) Targets(demandScale float64) [4]float64 {
	var out [4]float64
	for i := range l.Pools {
		p := &l.Pools[i]
		if p.Population <= 1e-12 {
			continue
		}
		d := model.DemandAt(l.WealthTier(i))
		for g := 0; g < 4; g++ {
			out[g] += d[g] * p.Population / 100000 * demandScale
		}
	}
	return out
}

// Budgets 返回各池的消费预算（【税前】口径，元）。
//
//	budget = 池内现金 / (1 + t)
//
// 之所以除以 (1+t)：交易税是【价外税】（§4.5.3 的 G1；§4.5.6 定案的两段税
// ν=τ=2.5% 尚未实现，实现以单一税率 t 作替身，2026-09-19 起默认 0.05），
// 居民支付的含税总额不得超过其现金。消费结算按税前额成交，
// 扣款与税额拆分由 ledger 包的 ConsumerPurchase 交易完成。
func (l *Ledger) Budgets(taxRate float64) []float64 {
	out := make([]float64, len(l.Pools))
	for i := range l.Pools {
		out[i] = l.Pools[i].Cash() / (1 + taxRate)
	}
	return out
}

// Reset 在每个 tick 开头清零人口（现金是存量，保留）。
func (l *Ledger) Reset() {
	for i := range l.Pools {
		l.Pools[i].Population = 0
	}
}

// Index 返回 (场地, 阶级) 对应的池下标。
func Index(worksites, worksite, class int) int {
	_ = worksites
	return worksite*ClassCount + class
}

// Credit 登记某池覆盖的人口。
//
// 注意：它【不再改动现金】——现金的划转必须走 ledger 包的 Wage 交易，
// 该交易同时给建筑池记账并保证借贷相等。人口是流量登记，与记账无关。
func (l *Ledger) Credit(worksite, class int, population float64) {
	idx := Index(0, worksite, class)
	if idx < 0 || idx >= len(l.Pools) {
		return
	}
	l.Pools[idx].Population += population
}

// SetPoolCash 直接设定某池余额（仅用于开局注资与调试）。
func (l *Ledger) SetPoolCash(i int, v float64) {
	if i < 0 || i >= len(l.Pools) {
		return
	}
	l.Pools[i].SetInitial(v)
}

// Spend 返回从某池扣减一笔【税前】消费支出的可行额度。
//
// 【本函数只做可行性判断，不改动余额】——真正的扣款由
// ledger.ConsumerPurchase 交易完成（它同时给卖方与政府记账）。
// 拆开的原因是：先算出"能买多少"，再一次性过账，避免"扣了钱
// 却发现卖方没收到"这类中间态。
//
// 返回实际可行的税前额与对应的税额；现金不足时按可用现金等比缩减。
func (l *Ledger) Spend(i int, net float64, taxRate float64) (paid, tax float64, ok bool) {
	if i < 0 || i >= len(l.Pools) || net <= 0 {
		return 0, 0, false
	}
	cash := l.Pools[i].Cash()
	gross := net * (1 + taxRate)
	if gross > cash+1e-9 {
		gross = cash
		net = gross / (1 + taxRate)
	}
	if net <= 1e-12 {
		return 0, 0, false
	}
	return net, net * taxRate, true
}

// WageShares 返回某场地每级建筑的阶级人口构成（§5）。
//
//   - population[c] = LaborPerLevel × st.Shares[c]
//   - wage[c]       = population[c] × st.Wages[c]
//
// 两者之和 = LaborPerLevel × st.AverageWage() = model.Building.WagePerLevel()。
//
// 【§5 第 20 轮】结构由调用方传入（`b.Spec.StructureOf()`）：城镇类 33,750 元/级、
// 农业类 28,250 元/级（5,000 × 5.65）、庄园 5,650 元/级（1,000 × 5.65）。
func WageShares(laborPerLevel float64, st model.LaborStructure) (population, wage [ClassCount]float64) {
	for c := 0; c < ClassCount; c++ {
		population[c] = laborPerLevel * st.Shares[c]
		wage[c] = population[c] * st.Wages[c]
	}
	return
}

// TotalWagePerLevel 校验用：每级工资总额 = LaborPerLevel × 结构人均工资。
func TotalWagePerLevel(laborPerLevel float64, st model.LaborStructure) float64 {
	_, wage := WageShares(laborPerLevel, st)
	var s float64
	for c := 0; c < ClassCount; c++ {
		s += wage[c]
	}
	return s
}

// SatisfactionSnapshot 是分组满足度的轻量记录（诊断用）。
type SatisfactionSnapshot struct {
	// ByClass 是各阶级的必需品满足度（简朴衣物、基础食物两项均值）。
	ByClass [ClassCount]float64
}

// AggregateSatisfaction 按人口加权汇总各阶级的满足度，供 §6.5 人口增长使用。
//
// 人口增长必须用【全人口】的必需品满足度，不能只用一个阶级。
func (l *Ledger) AggregateSatisfaction(byClass [ClassCount]float64) float64 {
	var num, den float64
	for i := range l.Pools {
		p := &l.Pools[i]
		if p.Population <= 1e-12 {
			continue
		}
		num += byClass[p.Class] * p.Population
		den += p.Population
	}
	if den <= 1e-12 {
		return 0
	}
	return num / den
}
