package sim

import (
	"math"
	"testing"
)

// TestAuditPlayerInvestInterface 校验 1.2 M3/M6 的**玩家投资接口**。
//
// 【裁决】M6："玩家投资额度**即**政府投资额度；设置**政府投资 AI 开关**，
// 当玩家控制时**自动关闭**。" M3："把投资额度与方向从 AI 托管改为**玩家可决策**。"
// 第 58 轮裁决补充："**只接政府额度，投资池自动**"。
//
// 【口径】额度**就是**投资池可动用额（同一个池，不是两份）；玩家决定的是**方向**——
// 两条投资栈各拿多少预算。
//
// 【四条断言 —— 每一条都对应一个容易写错的地方】
//  1. **默认（AI 开启）逐位复现基线**；
//  2. **只关 AI、不给方向** ⇒ 仍等于基线（"关闭 AI"**不得**隐式变成 50/50 ——
//     那是玩家没做过的选择）；
//  3. **关 AI + 给方向** ⇒ 结果改变，且 `budgetShareManor` **恰等于**给定值；
//  4. 方向的两端（全给庄园 / 全给金融）都**可运行**、不破坏不变量与货币守恒。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditPlayerInvestInterface -v
func f64ptr(v float64) *float64 { return &v }

func TestAuditPlayerInvestInterface(t *testing.T) {
	auditEnabled(t)
	type res struct {
		pop, invest, capital, gov float64
		shareManor                float64
		// aiEnabled / playerShare 是**参数本身**——判"接口是否被应用"要看它们，
		// 不能只看经济结果（见下面 ③ 的说明）。
		aiEnabled   bool
		playerShare float64
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
		var lastShare float64
		for i := 0; i < 400; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("%s: Step %d: %v", name, i, err)
			}
			if st.InvariantErr != nil {
				t.Fatalf("%s: 不变量破坏 @%d: %v", name, i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("%s: 借贷不等 %d 笔 @%d", name, v, i)
			}
			lastShare = st.budgetShareManor
		}
		// 货币守恒
		delta := st.TotalMoney() - money0
		want := st.TickNewCapitalTotal() + st.InfusionTotal()
		if d := delta - want; d > 1e-3 || d < -1e-3 {
			t.Errorf("%s: 货币守恒差 %.6f", name, d)
		}
		return res{st.Population, st.InvestmentPool(), st.balCap(), st.balGov(), lastShare,
			st.Params.InvestAIEnabled, st.Params.InvestManorShare}
	}
	eq := func(a, b res) bool {
		for _, p := range [][2]float64{{a.pop, b.pop}, {a.invest, b.invest}, {a.capital, b.capital}, {a.gov, b.gov}} {
			// 容差：R72 登记的记账层噪声 ~1e-7；真实差异在 1e5 量级以上
			if math.Abs(p[0]-p[1]) > 1e-6*math.Max(1, math.Max(math.Abs(p[0]), math.Abs(p[1]))) {
				return false
			}
		}
		return true
	}

	base := run("基线(AI)", nil)
	aiOffOnly := run("只关AI", func(o *Options) { o.InvestAIOff = true })
	allManor := run("全给庄园", func(o *Options) { o.InvestAIOff = true; o.InvestManorShare = f64ptr(1.0) })
	allFinance := run("全给金融", func(o *Options) { o.InvestAIOff = true; o.InvestManorShare = f64ptr(0.0) })
	half := run("各半", func(o *Options) { o.InvestAIOff = true; o.InvestManorShare = f64ptr(0.5) })

	t.Logf("%d tick：\n"+
		"  基线(AI)  人口 %.0f 投资池 %.6g 资本池 %.6g 占比 %.4f\n"+
		"  只关AI    人口 %.0f 投资池 %.6g 资本池 %.6g 占比 %.4f\n"+
		"  全给庄园  人口 %.0f 投资池 %.6g 资本池 %.6g 占比 %.4f\n"+
		"  全给金融  人口 %.0f 投资池 %.6g 资本池 %.6g 占比 %.4f\n"+
		"  各半      人口 %.0f 投资池 %.6g 资本池 %.6g 占比 %.4f",
		400,
		base.pop, base.invest, base.capital, base.shareManor,
		aiOffOnly.pop, aiOffOnly.invest, aiOffOnly.capital, aiOffOnly.shareManor,
		allManor.pop, allManor.invest, allManor.capital, allManor.shareManor,
		allFinance.pop, allFinance.invest, allFinance.capital, allFinance.shareManor,
		half.pop, half.invest, half.capital, half.shareManor)

	// ① 默认逐位复现基线（这里用"与另一次默认运行比较"来钉住无副作用）
	base2 := run("基线(AI)#2", nil)
	if !eq(base, base2) {
		t.Errorf("同一默认配置两次运行不一致：%+v vs %+v", base, base2)
	}
	// ② 只关 AI、不给方向 ⇒ 未设定方向，且结果等于基线
	//
	// 【断言要看**参数**，不能只看结果】第一版写成"结果必须与基线相同"时**通过**了，
	// 但那条断言**不足以**说明问题：真正要钉的是"**方向仍未被设定**"（`−1`）。
	if aiOffOnly.playerShare >= 0 {
		t.Errorf("只关 AI 却把方向设成了 %.4f —— 应保持 −1（未设定）", aiOffOnly.playerShare)
	}
	if aiOffOnly.aiEnabled {
		t.Errorf("`InvestAIOff` 未生效：InvestAIEnabled 仍为 true")
	}
	if !eq(base, aiOffOnly) {
		t.Errorf("只关 AI（未给方向）却改变了结果：投资池 %.6g vs 基线 %.6g",
			aiOffOnly.invest, base.invest)
	}
	// ③ 给方向 ⇒ 占比**恰等于**给定值
	//
	// 【为什么这里不能断言"结果必须与基线不同"】第一版那样写时**失败**：
	// `InvestManorShare = 0.0`（全给金融）**恰好等于** AI 当期自己选的方向
	//（基线末期 `budgetShareManor = 0.0000`），于是结果与基线**逐位相同**——
	// 那是**巧合**，不是缺陷。故判"接口是否生效"要看**参数**与 `budgetShareManor`，
	// 而不是"结果有没有变"。
	for _, c := range []struct {
		name string
		want float64
		got  res
	}{
		{"全给庄园", 1.0, allManor},
		{"全给金融", 0.0, allFinance},
		{"各半", 0.5, half},
	} {
		if c.got.aiEnabled {
			t.Errorf("%s：InvestAIEnabled 仍为 true —— 玩家方向被 AI 覆盖", c.name)
		}
		if math.Abs(c.got.playerShare-c.want) > 1e-9 {
			t.Errorf("%s：InvestManorShare = %.6f，应恰为 %.2f", c.name, c.got.playerShare, c.want)
		}
		if math.Abs(c.got.shareManor-c.want) > 1e-9 {
			t.Errorf("%s：budgetShareManor = %.6f，应恰为 %.2f —— 玩家方向未生效",
				c.name, c.got.shareManor, c.want)
		}
	}
	// 三个方向之间必须**互不相同**（否则玩家控制没有意义）
	if eq(allManor, allFinance) {
		t.Errorf("全给庄园与全给金融结果相同 —— 玩家方向对分配无影响")
	}
	if eq(allManor, half) || eq(allFinance, half) {
		t.Errorf("各半与某一端结果相同 —— 方向未真正生效")
	}
	// ④ 两端都可运行：人口未塌陷、货币有限
	for _, c := range []struct {
		name string
		r    res
	}{{"全给庄园", allManor}, {"全给金融", allFinance}} {
		if c.r.pop <= 0 || math.IsNaN(c.r.pop) {
			t.Errorf("%s：人口 %.4f（塌陷或非有限）", c.name, c.r.pop)
		}
	}
}
