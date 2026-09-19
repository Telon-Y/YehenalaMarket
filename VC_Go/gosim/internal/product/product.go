// Package product 实现契约 §7.2 的「**实际工农生产总值**」核算。
//
// ============================ 为什么要单独一个口径 ============================
//
// §7 的 GDP 是**名义**口径（消费者支出 + 各池期末余额），它随价格水平一起漂移：
// 实测 10,000 tick 长跑里价格指数 ×98,704，GDP 会跟着涨到 2.6e13，
// 而居民的实际货币余额从 6.7e6 掉到 319——**名义数字看不出经济到底是在增长还是在通胀**。
//
// 本包用**固定参考价**（$P_{ref}$，§3.4 的零利润价解，开局解一次后不再变）给
// **实物产量**计价，得到与货币量无关的"实际"口径：
//
//	工农总产值（实际）  = Σ_{农业/工业建筑} 实物产出 × P_ref
//	工农增加值（实际）  = 工农总产值 − 中间投入 × P_ref
//	人均实际增加值      = 工农增加值 / 人口
//
// 三者只依赖**实物量**（级数 × 雇佣率 × 配方 × 配给比）与**固定的价格权重**，
// 故对价格水平、税率、货币存量、政府债务**完全不敏感**。
//
// ============================ 部门划分 ============================
//
//	农业 = §3.2 的**农村**类建筑（谷物农场 / 棉花种植园）+ 自给农场产出
//	工业 = §3.2 的**城镇 / 资源 / 开发**类建筑（加工食品 … 工具、住房、建造力、煤、铁）
//
// 不参与：金融区、宅邸庄园（纯所有权载体）、仓库、消费代理（贸易/记账节点，不生产商品）。
//
// 【自给农场的处理】它的产出（谷物/织物/服装）计入**农业**，中间投入为 0
// （土地与劳动，无货币投入）——与 §3.3 的"净产出"口径一致。
package product

import "yehenala/market/internal/model"

// Sector 是工农两大部类。
type Sector uint8

const (
	// SectorAgri 是农业（农村类建筑 + 自给农场）。
	SectorAgri Sector = iota
	// SectorIndustry 是工业（城镇 / 资源 / 开发类建筑）。
	SectorIndustry
)

// String 返回部类中文名（报告用）。
func (s Sector) String() string {
	if s == SectorAgri {
		return "农业"
	}
	return "工业"
}

// Result 是一次「实际工农生产总值」核算的结果（单位：**元，固定参考价**）。
type Result struct {
	// GrossAgri / GrossIndustry 是总产值（实物产出 × P_ref，含中间投入，会重复计算）。
	GrossAgri     float64
	GrossIndustry float64
	// Gross 是工农总产值 = 农业 + 工业。
	Gross float64
	// InterAgri / InterIndustry 是中间投入价值（实物取用量 × P_ref）。
	InterAgri     float64
	InterIndustry float64
	// Inter 是中间投入合计。
	Inter float64
	// AddedAgri / AddedIndustry 是增加值（总产值 − 中间投入），即"实际 GDP"的那一项。
	AddedAgri     float64
	AddedIndustry float64
	// Subsistence 是**自给农场**那一部分产出（已含在上面的 GrossAgri / AddedAgri 里）。
	//
	// 【为什么单列】自给农场按"每级未使用土地"生成（§4.2：耕地上限 5,000 ⇒ 约 4,990 级），
	// 其产出（谷物/织物/服装）在参考价下量级很大（实测约 1.2e7 元/tick，占农业增加值的 90%+）。
	// 单列它，才能把"专业农业 + 工业"的真实增长与"自给部门随人口/土地联动"分开看。
	Subsistence float64
	// Added 是工农增加值 = 总产值 − 中间投入。
	Added float64
	// PerCapita 是人均实际增加值（Added / 人口）；人口 ≤ 0 时为 0。
	PerCapita float64
}

// Input 是核算所需的全部实物量（全部来自某一 tick 的生产结算，不含任何货币收支）。
type Input struct {
	// Specs 是建筑类别定义（用于取配方与部类）。
	Specs []model.Building
	// Levels / Hire 是各类建筑的当期级数与雇佣率。
	Levels, Hire []float64
	// Shortage 是各类建筑的短缺惩罚系数（§3.3），用于把名义产出折成实际产出。
	Shortage []float64
	// AllocRatio 是各商品的配给比（§3.3），用于算中间投入的实际取用量。
	AllocRatio []float64
	// Subsistence 是自给农场的实物产出（商品 → 数量）。
	Subsistence map[int]float64
	// RefPrices 是固定的参考价 P_ref（§3.4 的零利润价解）；长度 ≥ model.Goods。
	RefPrices []float64
	// Population 是人口（仅用于人均值）。
	Population float64
}

// Compute 计算实际工农生产总值。
//
// 口径（逐项都可追溯到 §3.3 的配方与 §3.4 的 P_ref，不涉及任何货币余额）：
//
//	建筑 i 的实际产出量  = levels_i × hire_i × 单级产出 × shortage_i
//	建筑 i 的实际投入量  = levels_i × hire_i × 单级投入 × allocRatio（逐投入品）
//	自给农场产出        = Subsistence（已按雇佣率缩放）
func Compute(in Input) Result {
	var r Result
	n := model.Goods
	ref := func(g int) float64 {
		if g >= 0 && g < len(in.RefPrices) {
			return in.RefPrices[g]
		}
		return 0
	}

	for i, b := range in.Specs {
		if !b.Produces() {
			continue
		}
		if i >= len(in.Levels) || i >= len(in.Hire) {
			break
		}
		eff := in.Levels[i] * in.Hire[i]
		sf := 1.0
		if i < len(in.Shortage) && in.Shortage[i] > 0 {
			sf = in.Shortage[i]
		}
		// 实际产出（按固定参考价计价）
		gross := eff * b.Recipe.Qty * sf * ref(b.Recipe.Output)
		// 实际中间投入
		var inter float64
		for g, qty := range b.Recipe.Inputs {
			ar := 1.0
			if g < len(in.AllocRatio) {
				ar = in.AllocRatio[g]
			}
			inter += eff * qty * ar * ref(g)
		}
		switch b.Category {
		case model.CatRural:
			r.GrossAgri += gross
			r.InterAgri += inter
		default:
			// 城镇 / 资源 / 开发 都归工业（建造力属"建筑业"，§3.2）
			r.GrossIndustry += gross
			r.InterIndustry += inter
		}
	}

	// 自给农场的产出归农业（无货币中间投入）
	for g, qty := range in.Subsistence {
		if g < 0 || g >= n || qty <= 0 {
			continue
		}
		v := qty * ref(g)
		r.GrossAgri += v
		r.Subsistence += v
	}

	r.Gross = r.GrossAgri + r.GrossIndustry
	r.Inter = r.InterAgri + r.InterIndustry
	r.AddedAgri = r.GrossAgri - r.InterAgri
	r.AddedIndustry = r.GrossIndustry - r.InterIndustry
	r.Added = r.AddedAgri + r.AddedIndustry
	if in.Population > 0 {
		r.PerCapita = r.Added / in.Population
	}
	return r
}

// Index 返回相对基期的指数（基期值 ≤ 0 时返回 0，避免除零）。
func Index(current, base float64) float64 {
	if base <= 0 {
		return 0
	}
	return current / base
}
