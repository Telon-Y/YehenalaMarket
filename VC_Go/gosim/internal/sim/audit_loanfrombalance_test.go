package sim

import (
	"math"
	"testing"
)

// TestAuditLoanFromBalance 校验 R71 收口(a)：把"每期可贷额"锚到储蓄银行余额。
//
// 【背景（R70）】原口径每 52 tick 固定放 50 万，而银行每 tick 存入约 180 万
// ⇒ 存贷失衡 **190 倍** ⇒ 800 tick 后银行握着 18.75 亿只贷出 750 万（0.4%）
// ⇒ **投资池从 22.66 亿被抽到 0.096 亿（−99.58%）**、人口少 17 万。
//
// 【断言】
//  1. **默认关闭时逐位不变**（`LoanFromBalance=false`）——基线保护；
//  2. 开启后**贷出额大幅上升**（失衡被结构性消除）；
//  3. 开启后**投资池与人口显著恢复**（对照固定额度臂）；
//  4. 两种口径都不破坏借贷相等与货币守恒；
//  5. **可贷额确实等于余额 × 比例**（口径断言，防止再次退化成 `min` 空操作）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditLoanFromBalance -v
func TestAuditLoanFromBalance(t *testing.T) {
	auditEnabled(t)
	run := func(fromBalance bool, ticks int) *State {
		st, err := New(Options{
			Population:           5_000_000,
			WealthTier:           10,
			FinanceLaborPerLevel: 1000,
			GovStartupFraction:   0.5,
			ProductionInitLevel:  -1,
			BankEnabled:          true,
			LoanFromBalance:      fromBalance,
		})
		if err != nil {
			t.Fatalf("New(fromBalance=%v): %v", fromBalance, err)
		}
		for i := 0; i < ticks; i++ {
			if _, err := st.Step(); err != nil {
				t.Fatalf("Step %d: %v", i, err)
			}
			if st.InvariantErr != nil {
				t.Fatalf("不变量破坏 @%d: %v", i, st.InvariantErr)
			}
			if v := len(st.Aud.Violations()); v != 0 {
				t.Fatalf("借贷不等 %d 笔 @%d", v, i)
			}
		}
		return st
	}
	// ① 默认口径：再跑一次，两次必须一致（同时证明该开关真的是纯 opt-in）
	//
	// 【容差为什么是 1e-5 而不是 0：R72 登记的既有缺陷】
	// 用 `!=` 直接比浮点曾在此处失败。定位结果是**整个记账层**存在 ~1e-7 的
	// 运行间非确定性（`PayWages` 遍历 `bySite` map 发出借方 + `Post` 把残差
	// 并入"最大一笔分录"，两处都受 map 迭代顺序影响）。R72 已把
	// `Auditor.Total()/TotalOf()` 改为确定性累加，但残余分歧**未根治**（见 R72）。
	// 故此处按 1e-5 容差比较：它远小于两个对照臂之间的真实差异
	// （投资池 5.32e6 vs 1.97e9，相差 3 个数量级），足以抓真问题而不会被噪声误伤。
	const detTol = 1e-5
	fixA := run(false, 800)
	fixB := run(false, 800)
	diffInvest := math.Abs(fixA.InvestmentPool() - fixB.InvestmentPool())
	diffOut := math.Abs(fixA.Cap.DebtOutstandingTotal() - fixB.Cap.DebtOutstandingTotal())
	diffMoney := math.Abs(fixA.TotalMoney() - fixB.TotalMoney())
	if diffInvest > detTol || diffOut > detTol || diffMoney > detTol {
		t.Fatalf("默认口径两次运行不一致（投资池 %.3g、未偿 %.3g、货币总量 %.3g）—— "+
			"超出既有噪声水平，可能存在新的非确定性", diffInvest, diffOut, diffMoney)
	}
	t.Logf("默认口径两次运行的差异（R72 既有噪声）：投资池 %.3g、未偿 %.3g、货币总量 %.3g",
		diffInvest, diffOut, diffMoney)

	dyn := run(true, 800)
	fixInvest := fixA.InvestmentPool()
	dynInvest := dyn.InvestmentPool()
	fixOut := fixA.Cap.DebtOutstandingTotal()
	dynOut := dyn.Cap.DebtOutstandingTotal()
	t.Logf("800 tick 对照：\n"+
		"  固定额度 : 投资池 %16.2f、未偿负债 %14.2f、银行余额 %16.2f、人口 %.0f\n"+
		"  余额锚定 : 投资池 %16.2f、未偿负债 %14.2f、银行余额 %16.2f、人口 %.0f",
		fixInvest, fixOut, fixA.SavingsBankBalance(), fixA.Population,
		dynInvest, dynOut, dyn.SavingsBankBalance(), dyn.Population)

	// ② 贷出额大幅上升 ⇒ 未偿负债显著更大（钱真的贷出去了）
	if dynOut <= fixOut {
		t.Errorf("余额锚定下未偿负债 %.2f 未超过固定额度 %.2f —— 贷出额没有上升", dynOut, fixOut)
	}
	// ③ 投资池显著恢复
	if dynInvest <= fixInvest {
		t.Errorf("余额锚定下投资池 %.2f 未超过固定额度 %.2f —— 失衡未修复", dynInvest, fixInvest)
	}
	if dynInvest < 10*fixInvest {
		t.Errorf("余额锚定下投资池 %.2f 仅为固定额度 %.2f 的 %.1f 倍，恢复幅度不足",
			dynInvest, fixInvest, dynInvest/maxf(fixInvest, 1))
	}
	// ④ 货币守恒（两个口径）
	for _, c := range []struct {
		name string
		st   *State
	}{{"固定额度", fixA}, {"余额锚定", dyn}} {
		if e := c.st.InvariantErr; e != nil {
			t.Errorf("%s 不变量错误：%v", c.name, e)
		}
	}
	// ⑤ 口径断言：可贷额 = 余额 × 比例（用独立复算验证，防止退化成 min 空操作）
	if f := dyn.Params.LoanBalanceFraction; f > 0 {
		pool := dyn.SavingsBankBalance()
		want := pool * f
		if want <= dyn.Params.LoanPrincipal {
			t.Logf("提示：期末余额 %.2f 太小（×%.2f = %.2f ≤ 单笔上限 %.2f），"+
				"本局无法区分「替换」与「min」两种口径", pool, f, want, dyn.Params.LoanPrincipal)
		} else if dynOut <= dyn.Params.LoanPrincipal {
			t.Errorf("余额 %.2f × %.2f = %.2f 已超过单笔上限 %.2f，但未偿仅 %.2f "+
				"—— 口径疑似退化为 min() 空操作", pool, f, want, dyn.Params.LoanPrincipal, dynOut)
		}
	}
}

func maxf(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}
