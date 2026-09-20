package sim

import (
	"math"
	"testing"
)

// TestAuditProfile12 校验 1.2 **配置档**（`Options.Profile12`）。
//
// 【为什么需要配置档】到 R80 为止 1.2 有四个独立开关，默认**全关**（守住 1.0 基线）。
// 但那意味着"1.2 的完整配置"**没有单一入口**——只能靠人工记住四个开关，
// 目标"完成 1.2 的实现"因此无法用一条命令实例化。
//
// 【断言】
//  1. **Profile12=false 时逐位复现基线**（不改任何默认值）；
//  2. **Profile12=true 等价于四个开关全开**（逐位相同）；
//  3. **显式覆盖优先于 Profile12**（`*Override = false` 必须真的关掉该开关）；
//  4. 配置档不破坏不变量 / 借贷相等 / 货币守恒。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditProfile12 -v
func TestAuditProfile12(t *testing.T) {
	auditEnabled(t)
	ptr := func(b bool) *bool { return &b }

	type res struct {
		pop, invest, capital, gov float64
	}
	run := func(name string, mut func(o *Options)) res {
		o := Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
		}
		if mut != nil {
			mut(&o)
		}
		st, err := New(o)
		if err != nil {
			t.Fatalf("%s: New: %v", name, err)
		}
		money0 := st.TotalMoney()
		for i := 0; i < 600; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", name, i, err)
			}
			if st.InvariantErr != nil {
				t.Fatalf("%s: 不变量破坏 @%d: %v", name, i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("%s: 借贷不等 %d 笔 @%d", name, v, i)
			}
		}
		// ④ 货币守恒
		delta := st.TotalMoney() - money0
		want := st.TickNewCapitalTotal() + st.InfusionTotal()
		if d := delta - want; d > 1e-3 || d < -1e-3 {
			t.Errorf("%s: 货币守恒差 %.6f", name, d)
		}
		return res{st.Population, st.InvestmentPool(), st.balCap(), st.balGov()}
	}
	// 【容差】R72 的记账层噪声 ~1e-7 绝对量；本测试比较的是"配置是否等价"，
	// 用相对 1e-9 即可分辨（真实差异在 1e-2 ~ 1e0 量级）。
	eq := func(a, b res) bool {
		for _, p := range [][2]float64{{a.pop, b.pop}, {a.invest, b.invest}, {a.capital, b.capital}, {a.gov, b.gov}} {
			if math.Abs(p[0]-p[1]) > 1e-9*math.Max(1, math.Max(math.Abs(p[0]), math.Abs(p[1]))) {
				return false
			}
		}
		return true
	}

	base := run("基线", nil)
	profOff := run("Profile12=false", func(o *Options) { o.Profile12 = false })
	// ② 配置档 vs **手写全部 1.2 开关**
	//
	// 【R90 更正：对照物变了】R80 时配置档只有 4 个开关，故对照是"手写四开关"。
	// R90 把配置档**补全**为 6 个（+央行/金矿、+储蓄存量口径），
	// ⇒ 对照物必须同步改成"手写六开关"，否则比的是**不同的配置**，
	// 报出来的差异是**配置不同**而不是"档没生效"。
	profOn := run("Profile12=true", func(o *Options) { o.Profile12 = true })
	handOn := run("手写六开关", func(o *Options) {
		o.BankEnabled = true
		o.LoanFromBalance = true
		o.OwnershipRestructure = true
		o.WageBidEnabled = true
		o.CentralBankEnabled = true
		o.SavingsStockTrack = true
	})
	// ③ 显式覆盖（在**配置档的基础上**关掉一个 ⇒ 与"手写六开关减去那一个"比较）
	profNoBank := run("配置档+关银行", func(o *Options) {
		o.Profile12 = true
		o.BankEnabledOverride = ptr(false)
	})
	profNoOwn := run("配置档+关重构", func(o *Options) {
		o.Profile12 = true
		o.OwnershipOverride = ptr(false)
	})

	t.Logf("基线         人口 %.0f 投资池 %.6g 资本池 %.6g 政府池 %.6g", base.pop, base.invest, base.capital, base.gov)
	t.Logf("配置档(全开) 人口 %.0f 投资池 %.6g 资本池 %.6g 政府池 %.6g", profOn.pop, profOn.invest, profOn.capital, profOn.gov)
	t.Logf("手写六开关   人口 %.0f 投资池 %.6g 资本池 %.6g 政府池 %.6g", handOn.pop, handOn.invest, handOn.capital, handOn.gov)
	t.Logf("配置档+关银行 人口 %.0f 投资池 %.6g（应等于【手写(无银行)】）", profNoBank.pop, profNoBank.invest)
	t.Logf("配置档+关重构 人口 %.0f 投资池 %.6g（应等于【手写(无重构)】）", profNoOwn.pop, profNoOwn.invest)

	// ① Profile12=false 不改任何东西
	if !eq(base, profOff) {
		t.Errorf("Profile12=false 与基线不同：%+v vs %+v", base, profOff)
	}
	// ② Profile12=true ≡ 四开关全开
	if !eq(profOn, handOn) {
		t.Errorf("Profile12=true 与手写四开关不同：\n  档 =%+v\n  手写=%+v", profOn, handOn)
	}
	// 配置档确实**改变**了结果（否则档是空操作）
	if eq(base, profOn) {
		t.Errorf("Profile12=true 与基线相同（%+v）—— 配置档是空操作", profOn)
	}
	// 【R90 新增】配置档必须**真的**包含 R90 补进来的两项（央行/金矿、储蓄存量口径）。
	//
	// 只比较"档 vs 手写六开关"还不够：若我把配置档里的字段名写错、
	// 而对照物也照抄了同样的错，两边会"一致地错"。故这里**从产物反查**：
	// 开一局纯配置档，断言金矿/央行**存在**且储蓄存量**非 0**。
	probe, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		Profile12:            true,
	})
	if err != nil {
		t.Fatalf("配置档探针 New: %v", err)
	}
	if !probe.Params.CentralBankEnabled {
		t.Errorf("Profile12 未打开 CentralBankEnabled —— 档未补全")
	}
	if !probe.Params.SavingsStockTrack {
		t.Errorf("Profile12 未打开 SavingsStockTrack —— 档未补全")
	}
	if probe.centralBankIndex() < 0 || probe.goldMineIndex() < 0 {
		t.Errorf("Profile12 下找不到金矿/央行场地 —— 规格未被追加")
	}
	for i := 0; i < 200; i++ {
		if _, err := probe.Step(); err != nil {
			t.Fatalf("配置档探针 Step %d: %v", i, err)
		}
	}
	if probe.MintedTotal() <= 0 {
		t.Errorf("Profile12 下累计造币 = 0 —— 档内的央行没有真正工作")
	}
	if probe.SavingsStockTotal() <= 0 {
		t.Errorf("Profile12 下储蓄存量 = 0 —— 档内的存量口径没有真正工作")
	}
	t.Logf("配置档探针（200 tick）：产金 %.2f、造币 %.2f、储蓄已积累 %.2f",
		probe.GoldProducedTotal(), probe.MintedTotal(), probe.SavingsStockTotal())
	// ③ 显式覆盖必须生效
	if eq(profOn, profNoBank) {
		t.Errorf("`BankEnabledOverride=false` 未生效 —— 配置档覆盖了显式选择")
	}
	if eq(profOn, profNoOwn) {
		t.Errorf("`OwnershipOverride=false` 未生效 —— 配置档覆盖了显式选择")
	}
	// 「配置档+关银行」应等价于「**手写六开关减去银行**」
	//
	// 【R90 更正：第一版把两个对照写反了，且对照物也错了】
	// `Profile12 + BankEnabledOverride=false` ⇒ {重构, 竞标, 央行, 储蓄存量}
	// ⇒ 必须与**同样那组**手写配置比较，而不是"仅重构"。
	noBankHand := run("手写(无银行)", func(o *Options) {
		o.CentralBankEnabled = true
		o.SavingsStockTrack = true
		o.OwnershipRestructure = true
		o.WageBidEnabled = true
	})
	if !eq(profNoBank, noBankHand) {
		t.Errorf("配置档+关银行（%+v）与手写(无银行)（%+v）不同", profNoBank, noBankHand)
	}
	noOwnHand := run("手写(无重构)", func(o *Options) {
		o.BankEnabled = true
		o.LoanFromBalance = true
		o.CentralBankEnabled = true
		o.SavingsStockTrack = true
		o.WageBidEnabled = true
	})
	if !eq(profNoOwn, noOwnHand) {
		t.Errorf("配置档+关重构（%+v）与手写(无重构)（%+v）不同", profNoOwn, noOwnHand)
	}
}
