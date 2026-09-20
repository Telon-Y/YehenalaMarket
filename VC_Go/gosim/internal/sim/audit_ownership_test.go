package sim

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAuditOwnershipRestructure 校验 1.2 M4.2 所有权重构的落点。
//
// 【裁决（R57 第 3 条）】"资本直接承接政府 30%、劳动力直接承接资本 70%，
// 这是初始化改动"；两条划转各自一跳：① 政府 → 资本 30%；② 资本 → 劳动力 70%。
// 由此私人份额由"资本 0.70"变为 **资本 0.30 + 劳动力 0.70**。
//
// 【断言】
//  1. **默认关闭时逐位不变**（`OwnershipRestructure=false`）——基线保护；
//  2. 开启后 `ledger.LaborDividend()` 确实开始累积（劳动力那腿真的存在）；
//  3. **拆分不改变建筑侧净腿**：资本池的减少额恰等于劳动力分红池的增加额
//     （两腿之和逐位等于 Owner ⇒ 建筑/政府的腿都不变）；
//  4. 私人份额的 30/70 比例确实成立（用资本池减少量与劳动力池累积量反算）；
//  5. 不破坏货币守恒与借贷相等。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditOwnershipRestructure -v
func TestAuditOwnershipRestructure(t *testing.T) {
	auditEnabled(t)
	run := func(restructure bool, ticks int) (*State, float64, float64, float64) {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			OwnershipRestructure: restructure,
		})
		if err != nil {
			t.Fatalf("New(restructure=%v): %v", restructure, err)
		}
		// 【R76】分红池现在**每 tick 派发清零**，故它的**余额**恒为 0，
		// 不能再用余额度量"劳动力拿到了多少"。
		//
		// 又因为派发会经 §5.3 储蓄通道回流投资池，**期末池余额**还含有
		// 二阶效应 ⇒ 用余额反算比例会有 2.75e7 的偏差（实测）。
		// 故改为在**利润归属的源头**累计：每 tick 直接读 `s.Cap.OperatingProfit`
		// 与 `laborDividendTick`（本 tick 归属给劳动力的那一腿），
		// 二者都是"划转本身"的量，不含回流。
		var cumOwner, cumLabor, maxSplitErr float64
		for i := 0; i < ticks; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
			// ⑥ 已跑完：本 tick 的**私人份额合计**与其中**劳动力那一腿**。
			//
			// 【R76】口径断言必须**在同一 tick、同一局内**做：
			// `laborDividendTick` 与 `privateShareTick` 都由同一批 `res` 累加而来，
			// 故 `laborDividendTick == privateShareTick × laborFraction` **逐位**成立。
			//
			// **不能**跨局比较累计量——派发腿会经 §5.3 储蓄通道改变经济
			//（投资池 +147%、利润随之变化）⇒ 两局的 `Σ res.Owner` 本就**不应**相等。
			f := st.laborPrivateShare()
			if d := math.Abs(st.laborDividendTick - st.privateShareTick*f); d > maxSplitErr {
				maxSplitErr = d
			}
			cumOwner += st.privateShareTick
			cumLabor += st.laborDividendTick
			if st.InvariantErr != nil {
				t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("借贷不等 %d 笔 @%d", v, i)
			}
		}
		return st, cumLabor, cumOwner, maxSplitErr
	}
	const ticks = 600
	off, _, cumOwnerOff, offSplitErr := run(false, ticks)
	on, laborDiv, P, maxSplitErr := run(true, ticks)

	offLaborDiv := off.Aud.Balance(ledger.LaborDividend())
	t.Logf("%d tick：\n"+
		"  关闭 : 私人份额合计 %18.2f  劳动力累计分红 %18.2f  拆分误差 %.3g\n"+
		"  开启 : 私人份额合计 %18.2f  劳动力累计分红 %18.2f  拆分误差 %.3g",
		ticks, cumOwnerOff, 0.0, offSplitErr, P, laborDiv, maxSplitErr)
	_ = cumOwnerOff

	// ① 默认关闭：劳动力分红池恒为 0（逐位复现 1.0 的直接证据）
	if offLaborDiv != 0 {
		t.Errorf("未开重构时劳动力分红池 = %.6f，应为 0（1.0 逐位不变被破坏）", offLaborDiv)
	}
	// ② 开启后劳动力那腿**确实在被记账**
	//
	// 【R76 教训】不能断言 `laborDiv > 0`：1.0 的生产建筑**整体亏损**
	//（§0.4 第 25 项），私人份额 `res.Owner` 为负 ⇒ 劳动力按 70% 承担亏损
	// ⇒ 累计分红为**负**是**正确**结果。本断言只问"机制有没有跑"，
	// 不问"符号是正还是负"——后者是经济状态，不是机制正确性。
	if laborDiv == 0 {
		t.Fatalf("开启重构后累计分红 = 0 —— 劳动力那一腿没有落地")
	}
	// 分红池余额：负数时 `distributeLaborDividend` 按设计**不派发**（`bal <= 0` 直接返回），
	// 故余额可能停在负值上；这正是"分配器无法派发亏损"的正确行为。
	// 正数时则必须被**清零**（否则就是 R75 那种沉淀）。
	poolBal := on.Aud.Balance(ledger.LaborDividend())
	t.Logf("累计分红 %.2f、期末分红池余额 %.2f（负值表示亏损累积，派发器按设计不派发）",
		laborDiv, poolBal)
	if poolBal > 1e-6 {
		t.Errorf("分红池期末余额 %.6f > 0 —— 派发未把池清零（R75 的沉淀复现）", poolBal)
	}
	if poolBal < 0 {
		t.Logf("（期末余额为负：600 tick 内建筑业整体亏损，劳动力按 70%% 承担 ⇒ " +
			"分配器无正余额可派发，与资本池在 1.0 里的水下状态同源）")
	}
	// ③ 【R76 核心断言】拆分口径**逐位**成立
	//
	// 在同一 tick、同一局内：`laborDividendTick == privateShareTick × laborFraction`。
	// 这是"两腿之和逐位等于 Owner"的直接后果（`ProfitAllocateSplit` 用减法算资本那腿）。
	//
	// 【重要】**不能**跨局比较累计量：派发腿会经 §5.3 储蓄通道改变经济
	//（实测投资池 +147%，利润随之变化）⇒ 两局的 Σ res.Owner 本就**不应**相等。
	// R76 第一版就是踩了这个坑（写成"关闭局与开启局累计私人份额必须逐位相等"）。
	laborFraction := 1 - on.Params.OwnershipCapitalShare
	if laborFraction <= 0 || laborFraction >= 1 {
		t.Fatalf("laborFraction=%.4f 非法", laborFraction)
	}
	if maxSplitErr > 1e-6 {
		t.Errorf("拆分口径误差 %.6g > 1e-6 —— laborDividendTick ≠ privateShareTick × %.2f",
			maxSplitErr, laborFraction)
	}
	// 关闭局的拆分误差应为 0（此时 laborFraction = 0 且分红恒为 0）
	if offSplitErr > 1e-9 {
		t.Errorf("关闭局拆分误差 %.6g ≠ 0", offSplitErr)
	}
	// ④ 比例断言：用源头累计量算出的占比必须精确等于 laborFraction
	if math.Abs(laborDiv/P-laborFraction) > 1e-9 {
		t.Errorf("劳动力在私人份额中的占比 = %.9f，应为 %.2f（M4.2 裁决）",
			laborDiv/P, laborFraction)
	}
	t.Logf("私人份额 P = %.2f：资本拿 %.2f（%.2f%%）、劳动力拿 %.2f（%.2f%%）",
		P, P-laborDiv, 100*on.Params.OwnershipCapitalShare,
		laborDiv, 100*laborFraction)
	// ⑤ 【R76 改写】重构**现在有真实经济后果**——这正是派发腿带来的变化
	//
	// R75 时三条"中性"断言（人口/投资池/私有化成交全等）是**正确**的，
	// 因为那时 30 亿停在分红池里。R76 接上派发腿后，分红进入居民钱包 →
	// §5.3 储蓄通道 → 投资池，于是重构**必然**改变实物流量。
	// 故这里把断言方向**反过来**：必须**不**相等，否则说明派发腿没接上。
	//
	//	800 tick 实测：投资池 2,169,594,653.81 → 5,143,442,998.73（+137%）
	//	             累计私有化等级 20.2941 → 21.0956（+0.80）
	if math.Abs(on.InvestmentPool()-off.InvestmentPool()) <= 1e-6 {
		t.Errorf("重启后投资池与基线相同（%.2f）—— "+
			"分红派发腿未把分红导入居民储蓄通道，重构仍是 R75 的\"账面重构\"",
			on.InvestmentPool())
	}
	if on.InvestmentPool() <= off.InvestmentPool() {
		t.Errorf("重启后投资池 %.2f 未高于基线 %.2f —— 方向不符预期",
			on.InvestmentPool(), off.InvestmentPool())
	}
	t.Logf("重构的**经济**后果（与 R75 的账面性对比）：投资池 %.2f → %.2f（%+.1f%%）",
		off.InvestmentPool(), on.InvestmentPool(),
		100*(on.InvestmentPool()-off.InvestmentPool())/math.Max(off.InvestmentPool(), 1))
}
