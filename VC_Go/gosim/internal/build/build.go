// Package build 实现契约 §4 的扩建规则、建造队列、建造力分配与缩编。
//
// §4.1 扩建规则：利润率 > 10% 时自动扩建，每超过 5 个百分点多扩 1 个（向上取整），
//
//	单次扩建量 ≤ 当前建筑总数的 10%。
//
// §4.2 建造队列：每工地每 tick 最多投入 30 建造力；队列完成时间超过 52 周期时
//
//	在队首插入"建造部门"扩建订单；耕地 ≤ 5,000；煤/铁 ≤ 500；建造部门 ≤ 1,000。
//
// §4.3 资金：建筑池只负责营运资金（补足到 C*_i），扩建资金统一由投资池提供。
// §4.4 缩编：连续 156 周期雇佣率 < 75% 时，每周期等级 −5%（本版冻结）。
//
// 与政府/资本的接合点（§4.5.1b / §4.5.3）：
//   - 本包只产生**两条投资栈**的扩建意向（庄园栈 / 金融栈），不再自行判定付款方；
//   - 建造力的买家恒为政府（G2 按需采购），货款由投资池偿还（G6）；
//   - §4.1 的扩建判定必须用**不含补贴**的利润率 EMA（ProfitEMA），
//     而 §5.2 的雇佣调整用含补贴的 MarginEMA（§4.5.7 的校验条款）。
package build

import (
	"math"

	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/model"
)

// 两条投资栈的标识（§4.5.1b）。
const (
	// StackManor 是庄园栈：唯一出资方是宅邸庄园，**只能**对农业建筑立项。
	StackManor = "manor"
	// StackFinance 是金融栈：唯一出资方是金融区，对其余生产建筑（含建造部门）立项。
	StackFinance = "finance"
)

// Intent 是一次扩建意向。
type Intent struct {
	// BuildingIndex 是目标建筑类别。
	BuildingIndex int
	// Units 是拟扩建的等级数（已按 §4.1 的规则与上限裁剪）。
	Units float64
	// Stack 是所属投资栈："manor"（庄园栈）或 "finance"（金融栈）。
	//
	// 【2026-09-19 裁决】意向不再携带 Payer：谁出资由一个二值事实决定——
	// 农业建筑（占用农业用地）只能由庄园栈立项，其余生产建筑只能由金融栈立项；
	// 而出资方恒为**投资池**（§4.5.1b：投资池付全额，政府只是代购）。
	Stack string
	// Margin 是触发该意向的利润率（**不含补贴**的 ProfitEMA 口径，§4.5.7）。
	Margin float64
}

// IsArable 返回该建筑是否占用农业用地（§4.5.1 的归属判据）。
//
// 这是"两条投资栈"的唯一划分依据，实现侧必须统一用它，不得写成别名的判断。
func IsArable(b model.Building) bool { return b.LandKind == "arable" }

// ExpansionUnits 按 §4.1 计算单次扩建的等级数。
//
//	nExtra = floor((margin − threshold)/5%) + 1
//	units  = min(nExtra × 10% × level, level × 10%)
//
// 返回 0 表示不扩建。注意契约的"每超过 5 个百分点多扩 1 个"中"个"的含义未定义，
// 架构 §5.4 定为"倍数"（扩 nExtra × 10% 等级），本实现遵循该定案。
//
// 【调用方口径（§4.5.7）】margin 必须是**不含补贴**的 ProfitEMA——
// 否则"亏损建筑靠补贴把 margin 抬到 0 以上"会被扩建逻辑误读为盈利。
func ExpansionUnits(margin float64, level float64, p model.Params) float64 {
	if margin <= p.ExpandThreshold || level <= 0 {
		return 0
	}
	step := p.ExpandPer5Pct
	if step <= 0 {
		step = 0.05
	}
	nExtra := math.Floor((margin-p.ExpandThreshold)/step) + 1
	maxByRule := p.ExpandMaxRatio * level
	byExtra := nExtra * p.ExpandMaxRatio * level
	units := math.Min(byExtra, maxByRule)
	if units < 1 {
		// 契约未定义取整，但"扩建 0.3 级"没有经济含义：至少扩 1 级，
		// 同时不突破"单次 ≤ 总数 10%"的硬约束（小建筑取 min）。
		units = math.Min(1, maxByRule)
	}
	return units
}

// Plan 按**不含补贴的利润率 EMA** 生成两条投资栈的扩建意向（§4.1 + §4.5.1b + §8-9）。
//
//	庄园栈（manor）  ：只能对**农业建筑**立项——占用农业用地者（谷物农场/棉花种植园）
//	金融栈（finance）：对其余生产建筑（含建造部门）立项
//
// 金融区与宅邸庄园**都不建造**（§4.5.2 / §4.5.5）：它们是所有权的显式表达，
// 级数由掌控比推导，故不产生任何扩建意向。
//
// 【§4.1 第 23 轮：建造部门豁免】建造部门（建造力部门）**不再**由利润率驱动扩建。
// 它的产出的唯一买家是扩建/公共工程，需求锚是"在手订单"（§3.4 方案 C′），
// 于是"利润率 > 10% 就扩产"会形成自指正反馈（队列越长 ⇒ 需求越大 ⇒ 扩产 ⇒ 产能更大）。
// 它的产能改由 `PowerCapacityUnits` 按"队列深度 ÷ 目标周期"决定，见该函数。
//
// 【为什么按"谁出资谁立项"分栈】投资池可动用额按两条栈的**累计贡献** K_m : K_f
// 分配（§4.5.1b），两条队列并行推进、互不抢占对方资金。分配与预算裁剪
// 由调用方（sim.step）完成——本函数只按规则产出意向，不碰资金。
func Plan(
	buildings []model.Building,
	levels []float64,
	profitEMA []float64,
	p model.Params,
) (manor, finance []Intent) {
	for i, b := range buildings {
		if b.IsNonMarket() {
			continue
		}
		if i >= len(levels) || i >= len(profitEMA) {
			break
		}
		// 【§4.1 第 23 轮豁免】建造部门不由利润率立项（改走队列驱动的产能规则）。
		// 判据用"产出的是建造力"而不是建筑名，避免名称变更后失效。
		if p.PowerByQueue && b.Recipe.Output == PowerIndex() {
			continue
		}
		// 【§4.5.7 校验条款】判定用 ProfitEMA（不含补贴），不是 MarginEMA。
		units := ExpansionUnits(profitEMA[i], levels[i], p)
		if units <= 0 {
			continue
		}
		it := Intent{
			BuildingIndex: i,
			Units:         units,
			Margin:        profitEMA[i],
		}
		if IsArable(b) {
			it.Stack = StackManor
			manor = append(manor, it)
		} else {
			it.Stack = StackFinance
			finance = append(finance, it)
		}
	}
	return manor, finance
}

// PowerCapacityUnits 按"队列深度 ÷ 目标周期"给出建造部门的扩建意向（§4.1 第 23 轮）。
//
// 【规则】建造部门的产能只需覆盖"在 QueueTargetTicks 个周期内把在手订单做完"：
//
//	requiredOut  = backlog / targetTicks            （每 tick 需要的建造力产出）
//	requiredLv   = requiredOut / (q_power × hire)   （满编折算的级数）
//	units        = clamp(requiredLv − currentLevel, 0, ExpandMaxRatio × currentLevel)
//
// 于是：队列积压 ⇒ 扩产能（这是对的）；产能追上"52 周期消化完" ⇒ **停止扩建**；
// 队列变浅（requiredLv < currentLevel）⇒ 不扩建（本版缩编冻结，也不会拆）。
//
// 【为什么用"当前雇佣率"折算】级数是产能的名义量，实际产出还要乘雇佣率。
// 若用满编折算，会在"雇佣率低"时低估所需级数、让队列拖长——故按实际雇佣率折算，
// 与 §4.2 的队列完成时间估算同源。
//
// 返回 0 表示不扩建。
func PowerCapacityUnits(
	powerLevel, powerHire, backlog, targetTicks, qtyPerLevel float64,
	p model.Params,
) float64 {
	if targetTicks <= 0 || qtyPerLevel <= 0 || powerHire <= 1e-9 {
		return 0
	}
	requiredOut := backlog / targetTicks
	requiredLv := requiredOut / (qtyPerLevel * powerHire)
	if requiredLv <= powerLevel {
		return 0
	}
	units := requiredLv - powerLevel
	if maxByRule := p.ExpandMaxRatio * powerLevel; units > maxByRule {
		units = maxByRule
	}
	if units < 1 {
		// 与 ExpansionUnits 同一条口径：至少 1 级，但不突破单次上限。
		units = math.Min(1, p.ExpandMaxRatio*powerLevel)
	}
	return units
}

// PowerNeed 返回一条意向本 tick 最多可投入的建造力（受 §4.2 的每工地上限约束）。
func PowerNeed(buildings []model.Building, it Intent, sitePowerLimit float64) float64 {
	if it.BuildingIndex < 0 || it.BuildingIndex >= len(buildings) {
		return 0
	}
	need := buildings[it.BuildingIndex].BuildCost * it.Units
	if need > sitePowerLimit {
		need = sitePowerLimit
	}
	return need
}

// PowerIndex 暴露建造部门的下标语义，供 sim 侧插入 §4.2 的队列告警订单。
func PowerIndex() int { return fiscal.PowerGoodIndex }

// CapFor 返回建筑类别的等级上限（§4.2）。
func CapFor(b model.Building, arableCap float64) float64 {
	if b.LandKind == "arable" {
		return arableCap
	}
	if b.Cap > 0 {
		return b.Cap
	}
	return math.Inf(1)
}

// ApplyCompletion 在订单完成时增加等级，并遵守上限约束。
//
// 返回**实际增加的等级数** added。调用方必须把 added 全部计入
// `b.PrivLevel += added; b.Level += added`，而 **GovLevel 不变**：
//
//	【§4.5.1b 定案：谁出资、谁拥有】扩建新增的等级全部归出资的资本建筑
//	（庄园栈 → 宅邸庄园；金融栈 → 金融区），政府不再按 s_gov 分得新增等级。
//	政府持股比例因此被**稀释**（分红仍按当期持股计算）。
//
// 【前值 → 后值】旧签名为 `ApplyCompletion(b, units, govShare, arableCap,
// arableUsed, currentLevel) (addGov, addPriv, added)`，把新增等级按当时的
// 政府/私有持股比例切分；本次改为只返回 added，切分口径整体删除。
//
// arableUsed 是当前耕地已用量（谷物 + 棉花），用于保证两者合计不超过上限。
func ApplyCompletion(
	b *model.Building,
	units, arableCap, arableUsed, currentLevel float64,
) (added float64) {
	add := units
	cap := CapFor(*b, arableCap)
	if b.LandKind == "arable" {
		room := arableCap - arableUsed
		if room < 0 {
			room = 0
		}
		add = math.Min(add, room)
	} else if !math.IsInf(cap, 1) {
		room := cap - currentLevel
		if room < 0 {
			room = 0
		}
		add = math.Min(add, room)
	}
	if add <= 0 {
		return 0
	}
	return add
}

// Decay 执行 §4.4 的缩编判定，返回新的等级。
//
// 契约原文：连续 156 周期雇佣率低于 75% 时，每周期建筑等级自动减少 5%。
// 本实现额外要求"亏损"（marginEMA < 0），理由：契约 §4.4 的标题是"建筑自动缩减"，
// 而把"雇佣率低"单独作为缩编条件会让"短暂解雇"永久摧毁资本存量——
// 这是 tools/core_sim_gov.txt 记录的 V2 崩解原因之一。
func Decay(level float64, idleTicks int, marginEMA float64, p model.Params) float64 {
	if idleTicks <= p.DecayWindow || marginEMA >= 0 || level <= 0 {
		return level
	}
	nl := level * (1 - p.DecayRate)
	if nl < 0.5 {
		return 0
	}
	return nl
}

// UpdateIdle 维护雇佣率观察窗计数（§4.4）。
func UpdateIdle(idleTicks int, hireRate float64, p model.Params) int {
	if hireRate < p.IdleHireRate {
		return idleTicks + 1
	}
	return 0
}

// AdjustHire 按**期望扩招后利润率**调整雇佣率（§5.2，2026-09-19 第 22 轮改口径）。
//
// 【前值 → 后值（第 22 轮裁决）】原口径只看**当期**利润率 EMA：
//
//	marginEMA > 0 ⇒ 增雇；marginEMA < 0 ⇒ 解雇
//
// 这是一个**没有上限的棘轮**：只要当期利润率还是正的，就继续增雇，而增雇本身
// 会压低价格、把利润率推向 0 以下——于是"当期为正"会一直成立到用工量远超人口，
// 再用一轮暴跌把整条产业链打断（R45 实测：tick 1,500 市场用工 519 万 > 总人口 229 万）。
//
// 新口径改为**前瞻**：只有在"扩招一步之后利润率仍为正"时才增雇：
//
//	step      = HireAnnual / TicksPerYear          （每 tick 的调整幅度）
//	ΔO/O      = step / hireRate                    （产量随雇佣率等比缩放）
//	P'        = P · (1 + ΔO/O)^(−1/ε)              （价格按需求弹性下降，ε = §3.1 的逐商品弹性）
//	margin'   = margin · P'/P                      （成本与工资不随雇佣率变化）
//	增雇 ⟺ margin' > 0（且当期 margin > 0）
//
// 每单位产出的成本（w·Σq·P + 每级工资）与雇佣率无关（价格是外生信号），
// 故 margin' / margin = P' / P，这一等价关系是本实现的核心。
//
// 【为什么它能解决棘轮】增雇的收益被"自身增产压价"折现：与 0 利润的距离越近，
// 增雇一步越可能把它推成负数，于是**停在零点附近而不是一路加到人口耗尽**。
// 收缩侧不加前瞻（亏损时先减雇是安全方向），仍按原速率解雇。
//
// 【参数（第 22 轮新增）】
//   - ExpectedMarginRatio 是"扩招一步后的利润率 / 当期利润率" ∈ (0,1]，
//     由调用方按上式算好传入（它需要价格与弹性，属 sim 侧信息）；
//     传 1 表示"不考虑压价"，退化为旧口径（对照臂）。
//   - ExpectedMarginFloor 是增雇判定的下限（默认 0：只有仍为正才扩招）。
func AdjustHire(hireRate, marginEMA, expectedMarginRatio float64, p model.Params) float64 {
	step := p.HireAnnual / float64(p.TicksPerYear)
	if marginEMA < 0 {
		return math.Max(0, hireRate-step)
	}
	if marginEMA > 0 {
		ratio := expectedMarginRatio
		if ratio <= 0 || ratio > 1 {
			// 非法输入（含旧口径的"不折现"）⇒ 按 1 处理，保持保守：
			// 折现系数为 1 意味着"扩招后利润率不变"，仍要 > floor 才增雇。
			ratio = 1
		}
		if marginEMA*ratio > p.ExpectedMarginFloor {
			return math.Min(1, hireRate+step)
		}
		// 扩招会把利润率压到下限以下 ⇒ 维持现状（不增也不减）。
		return hireRate
	}
	return hireRate
}
