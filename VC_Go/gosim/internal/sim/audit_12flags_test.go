package sim

import (
	"fmt"
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// TestAudit12FlagsAreOptIn 是 1.2 的**单一 opt-in 回归守卫**。
//
// 【为什么要有它】1.2 到现在引入了 **4 个默认关闭的开关**，
// 而项目对它们的纪律是同一条：
//
//	**开关关闭时必须逐位复现 1.0 基线。**
//
// 此前这条纪律是**逐个测试各自断言**的（散在 audit_bank / audit_wagebid /
// audit_ownership 里），没有任何一处**统一**检查"4 个开关任意一个被误改成
// 默认开启"。本测试把这 4 条合成一条守卫：
//
//	① 基线（4 个开关全默认）
//	② 每个开关**单独**显式置 false（必须与基线逐位相同）
//	③ 每个开关**单独**置 true（必须**改变**结果，否则说明该开关是死开关）
//
// ② 防的是"默认值被改动"，③ 防的是"开关接了但没接线"。
//
// 【为什么用"签名"而不是逐字段比较】签名是若干关键量的元组，
// 比较它们既覆盖"实物流量"（人口）也覆盖"货币分布"（三个池）与"税收"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAudit12FlagsAreOptIn -v
func TestAudit12FlagsAreOptIn(t *testing.T) {
	auditEnabled(t)

	// sig 是本局的关键量签名。
	type sig struct {
		Pop      float64
		Invest   float64
		Capital  float64
		Gov      float64
		Tax      float64
		LaborDiv float64
		DebtOut  float64
	}
	snapshot := func(st *State) sig {
		return sig{
			Pop:      st.Population,
			Invest:   st.InvestmentPool(),
			Capital:  st.balCap(),
			Gov:      st.balGov(),
			Tax:      st.Gov.TaxCollected,
			LaborDiv: st.Aud.Balance(ledger.LaborDividend()),
			DebtOut:  st.Cap.DebtOutstandingTotal(),
		}
	}
	run := func(name string, mut func(o *Options)) sig {
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
		return snapshot(st)
	}

	base := run("基线", nil)

	// 【容差】R72 登记的记账层有 ~1e-7 的运行间噪声。
	// 但"某个开关被误开"会造成**数量级**的差异（实测 2e9 级别），
	// 故相对容差 1e-9 足以分辨：噪声 ~1e-7/2e9 ≈ 5e-17 相对量级，远小于 1e-9。
	near := func(a, b float64) bool {
		return math.Abs(a-b) <= 1e-9*math.Max(1, math.Max(math.Abs(a), math.Abs(b)))
	}
	diffFields := func(a, b sig) []string {
		var out []string
		for _, f := range []struct {
			name string
			x, y float64
		}{
			{"人口", a.Pop, b.Pop},
			{"投资池", a.Invest, b.Invest},
			{"资本池", a.Capital, b.Capital},
			{"政府池", a.Gov, b.Gov},
			{"税收", a.Tax, b.Tax},
			{"劳动力分红池", a.LaborDiv, b.LaborDiv},
			{"未偿负债", a.DebtOut, b.DebtOut},
		} {
			if !near(f.x, f.y) {
				out = append(out, fmt.Sprintf("%s %.6g→%.6g", f.name, f.x, f.y))
			}
		}
		return out
	}

	// 四个 1.2 开关。
	//
	// 【R78：本守卫第一次运行就抓到两个我建模错的地方，记录在此免得后人重踩】
	//
	//  1. `LoanFromBalance` **不是**顶层开关——它是 `BankEnabled` 的**子开关**
	//     （见 `issueLoan` 开头 `if !s.Params.BankEnabled { return 0 }`）。
	//     故"显式关闭"必须写成 `BankEnabled=true, LoanFromBalance=false`，
	//     而**不能**与基线（`BankEnabled=false`）比较：
	//     那比的是"银行整体关掉"，当然不同。这正是第一版报
	//     "该开关不是纯 opt-in" 的原因，**是守卫的建模错了，不是代码错了**。
	//
	//  2. `WageBidEnabled` 在**默认参数下是惰性的**——因为裁决①"只在缺员时抬价"，
	//     而 1.0 的劳动力**结构性过剩**（R67 实测：市场申报用工只有人口的 13.9%）
	//     ⇒ 永不缺员 ⇒ 溢价恒 0 ⇒ 开关开了也不改变任何量。
	//     这不是"死开关"，而是**裁决①的正确后果**，但意味着默认局**无法**
	//     用它做"接线检查"。故本守卫对它改用**稀缺局**（人口 50 万，R67 的对照口径）。
	type variant struct {
		name string
		base func(o *Options)
		off  func(o *Options)
		on   func(o *Options)
	}
	flags := []variant{
		{
			name: "BankEnabled（M8 借贷台账）",
			off:  func(o *Options) { o.BankEnabled = false },
			on:   func(o *Options) { o.BankEnabled = true },
		},
		{
			name: "OwnershipRestructure（M4.2 所有权重构）",
			off:  func(o *Options) { o.OwnershipRestructure = false },
			on:   func(o *Options) { o.OwnershipRestructure = true },
		},
		{
			// 子开关：基线里**银行是开的**，只翻 LoanFromBalance。
			name: "LoanFromBalance（R71 收口 a）",
			base: func(o *Options) { o.BankEnabled = true },
			off:  func(o *Options) { o.BankEnabled = true; o.LoanFromBalance = false },
			on:   func(o *Options) { o.BankEnabled = true; o.LoanFromBalance = true },
		},
		{
			// 稀缺局：默认局里该开关按裁决①惰性，验不出接线。
			name: "WageBidEnabled（M7 工资竞标）",
			base: func(o *Options) { o.Population = 500_000 },
			off:  func(o *Options) { o.Population = 500_000; o.WageBidEnabled = false },
			on:   func(o *Options) { o.Population = 500_000; o.WageBidEnabled = true },
		},
	}

	for _, f := range flags {
		b := base
		if f.base != nil {
			b = run(f.name+"/base", f.base)
		}
		// ② 显式关闭 ⇒ 必须与**该开关所属的基线**相同
		offSig := run(f.name+"/off", f.off)
		if d := diffFields(b, offSig); len(d) > 0 {
			t.Errorf("%s 显式关闭时与其基线不同：%v —— 该开关不是纯 opt-in", f.name, d)
		}
		// ③ 开启 ⇒ 必须改变结果（否则是死开关）
		onSig := run(f.name+"/on", f.on)
		if d := diffFields(b, onSig); len(d) == 0 {
			t.Errorf("%s 开启后与其基线**完全相同** —— 该开关没有接线（死开关）", f.name)
		} else {
			t.Logf("%s 开启后改变了 %d 项：%v", f.name, len(d), d)
		}
	}

	// ④ 默认局里 WageBidEnabled **必须**是惰性的——这是裁决①的字面后果，
	// 单独断言一次，免得将来有人"修好"它而破坏 R67 已确立的行为。
	idle := run("WageBid/默认局", func(o *Options) { o.WageBidEnabled = true })
	if d := diffFields(base, idle); len(d) > 0 {
		t.Errorf("默认局（无稀缺）下 WageBidEnabled 改变了 %v —— "+
			"与裁决①【只在缺员时抬价】矛盾（R67 已确立：该开关在无稀缺时惰性）", d)
	}
	t.Logf("默认局里 WageBidEnabled 惰性（无稀缺 ⇒ 溢价恒 0）—— 裁决①的正确后果")

	t.Logf("基线签名：人口 %.0f、投资池 %.2f、资本池 %.2f、政府池 %.2f、税收 %.2f",
		base.Pop, base.Invest, base.Capital, base.Gov, base.Tax)
}
