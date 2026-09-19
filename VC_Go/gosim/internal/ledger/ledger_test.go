package ledger

import (
	"math"
	"testing"
)

// TestAllTxnTypesBalance 校验资金流量表里每一类交易的借贷都相等。
//
// 这是本包存在的全部理由：只要每一类交易都借贷相等，
// "货币守恒"就是构造性事实，不需要靠事后审计去追残差。
//
// 【前值 → 后值（2026-09-19，第 11 轮）】
//   - ④政府采购建造力：旧签名 PowerPurchase(idx, net, taxRate) 含"政府自己收自己"
//     的税腿；建造力交易现已定案**不计税**，改为 PowerPurchase(idx, amount)。
//   - ⑤政府售力（PowerSale）与⑥政府自建付款（GovOwnBuildout）：两条流动随
//     "整批采购 + 转售"口径一并删除，替换为投资池入池（InvestmentInflow）
//     与投资池付政府（InvestmentPayGov）。
//   - ⑦利润划分（ProfitSplit）→ ⑦利润归属（ProfitAllocate）：三条腿从
//     "政府 / 资本 / 留存比例"改为"补足自身现金池 / 政府 / 所属资本建筑"。
func TestAllTxnTypesBalance(t *testing.T) {
	const t0 = 0.10
	_ = t0
	cases := []struct {
		name string
		txn  *Txn
	}{
		{"①工资", Wage(3, []float64{1000, 400, 100})},
		{"②消费", ConsumerPurchase(Household(3, 1, 3), 5, 2000, t0)},
		{"③中间投入", Intermediate(2, 7, 1500, t0)},
		{"④政府采购建造力（不计税）", PowerPurchase(10, 5000)},
		{"⑤投资池入池（庄园）", InvestmentInflow(Building(12), 3000)},
		{"⑤投资池入池（金融区）", InvestmentInflow(Capital(), 3000)},
		{"⑥投资池付政府", InvestmentPayGov(2500)},
		{"⑦利润归属（盈利）", ProfitAllocate(6, 300, 700, Capital(), 0)},
		{"⑦利润归属（私人份额）", ProfitAllocate(6, 0, 210, Building(12), 490)},
		{"⑦利润归属（亏损）", ProfitAllocate(6, 0, -210, Capital(), -490)},
	}
	for _, c := range cases {
		if !c.txn.Balanced() {
			t.Errorf("%s：借 %.6f ≠ 贷 %.6f（净 %.6f）",
				c.name, c.txn.SumDebits(), c.txn.SumCredits(), c.txn.Net())
		}
	}
}

// TestPostRejectsUnbalanced 校验 Post 会拒绝借贷不等的交易。
//
// 记错账必须立刻暴露，而不是静默修正后留下一个要追的残差。
func TestPostRejectsUnbalanced(t *testing.T) {
	a := NewAuditor()
	bad := (&Txn{Name: "故意不平衡"}).
		Debit(Building(0), 100).
		Credit(Building(1), 99)
	if err := a.Post(bad); err == nil {
		t.Fatal("借贷不等的交易应被拒绝，实际通过了")
	}
	if len(a.Violations()) != 1 {
		t.Errorf("违规记录数 = %d，应为 1", len(a.Violations()))
	}
	// 被拒绝的交易不得改动任何余额
	if a.Total() != 0 {
		t.Errorf("被拒绝的交易改动了余额：总量 = %.6f", a.Total())
	}
}

// TestMoneyIsConservedByConstruction 是核心测试：
// 任意序列的非注入交易之后，全社会货币存量必须逐位不变。
//
// 这个断言【不依赖】任何业务逻辑，只依赖"每笔交易借贷相等"。
// 因此它天然覆盖全部交易类型，包括未来新增的类型——
// 只要新类型也走 Post，这个测试就不需要修改。
func TestMoneyIsConservedByConstruction(t *testing.T) {
	a := NewAuditor()
	// 开局注资（§4.3 修订：按一周工资流量标定）
	a.SetBalance(Gov(), 1_000_000)
	a.SetBalance(Capital(), 500_000)
	a.SetBalance(Building(0), 300_000)
	a.SetBalance(Building(10), 200_000)
	m0 := a.Total()

	const t0 = 0.10
	seq := []*Txn{
		Wage(0, []float64{1800, 480, 120}),
		ConsumerPurchase(Household(0, 0, 3), 0, 800, t0),
		ConsumerPurchase(Household(0, 1, 3), 0, 400, t0),
		Intermediate(10, 5, 1200, t0),
		PowerPurchase(10, 6000),
		InvestmentPayGov(2000),
		InvestmentInflow(Capital(), 1500),
		ProfitAllocate(0, 300, 700, Capital(), 0),
		ProfitAllocate(10, 0, -40, Capital(), -160),
		Wage(5, []float64{3000, 800, 200}),
		ConsumerPurchase(Household(5, 2, 3), 3, 950, t0),
	}
	for i, tx := range seq {
		if err := a.Post(tx); err != nil {
			t.Fatalf("第 %d 笔（%s）过账失败: %v", i+1, tx.Name, err)
		}
		if got := a.Total(); math.Abs(got-m0) > Epsilon {
			t.Fatalf("第 %d 笔（%s）之后货币不守恒：%.6f → %.6f（Δ=%.6f）",
				i+1, tx.Name, m0, got, got-m0)
		}
	}
	if len(a.Violations()) != 0 {
		t.Errorf("存在借贷不相等记录: %v", a.Violations())
	}
}

// TestInjectionIsOnlySourceOfGrowth 校验货币存量的唯一合法增量是营运本金注入。
func TestInjectionIsOnlySourceOfGrowth(t *testing.T) {
	a := NewAuditor()
	a.SetBalance(Gov(), 100_000)
	m0 := a.Total()

	// 注入：存量增加
	a.PostInjection(NewCapital(3, 25_000))
	if got := a.Total(); math.Abs(got-(m0+25_000)) > Epsilon {
		t.Fatalf("注入后存量 = %.2f，应为 %.2f", got, m0+25_000)
	}

	// 注入之后的普通交易仍然不改变存量
	if err := a.Post(Wage(3, []float64{600, 160, 40})) ; err != nil {
		t.Fatalf("工资过账失败: %v", err)
	}
	if got := a.Total(); math.Abs(got-(m0+25_000)) > Epsilon {
		t.Fatalf("普通交易改动了存量：%.2f", got)
	}

	// NewCapital 直接 Post 必须被拒绝（它故意借贷不等）
	if err := a.Post(NewCapital(4, 1000)); err == nil {
		t.Error("货币注入必须走 PostInjection，Post 应拒绝它")
	}
}

// TestPowerPurchaseIsTaxFreeAndBalanced 校验 §4.5.3 的两条裁决：
//
//	① 建造力交易**不计税**（政府既是唯一买家又是税收收款人，计税只会原地回冲），
//	   故政府池的净支出恰等于建造部门收到的全额；
//	② 投资池偿还同一笔货款后，政府的建造力**净支出为 0**（G6）。
//
// 【前值 → 后值】旧口径下 PowerPurchase 是"借政府 Gross、贷卖方 Net、贷政府 Tax"，
// 政府净支出恰为 Net；改造后 Gross ≡ Net（无税腿），且新增 InvestmentPayGov
// 把同一笔钱从投资池收回政府，使净支出进一步归零。
func TestPowerPurchaseIsTaxFreeAndBalanced(t *testing.T) {
	const amount = 4000.0
	a := NewAuditor()
	a.SetBalance(Gov(), 100_000)
	a.SetBalance(Building(10), 0)
	a.SetBalance(Investment(), 10_000)

	if err := a.Post(PowerPurchase(10, amount)); err != nil {
		t.Fatalf("过账失败: %v", err)
	}
	if got := a.Balance(Gov()); math.Abs(got-(100_000-amount)) > Epsilon {
		t.Errorf("政府支出 = %.4f，应恰为全额 %.4f（不计税）", 100_000-got, amount)
	}
	if got := a.Balance(Building(10)); math.Abs(got-amount) > Epsilon {
		t.Errorf("建造力部门入账 = %.4f，应恰为全额 %.4f", got, amount)
	}

	// G6：投资池偿还同一笔货款 ⇒ 政府净支出 = 0
	if err := a.Post(InvestmentPayGov(amount)); err != nil {
		t.Fatalf("投资池偿还失败: %v", err)
	}
	if got := a.Balance(Gov()); math.Abs(got-100_000) > Epsilon {
		t.Errorf("政府建造力净支出应为 0，实际余额 %.4f（初值 100000）", got)
	}
	if got := a.Balance(Investment()); math.Abs(got-(10_000-amount)) > Epsilon {
		t.Errorf("投资池余额 = %.4f，应为 %.4f", got, 10_000-amount)
	}
}

// TestProfitAllocateSumsToProfit 校验利润归属的借贷相等条件。
//
// 【前值 → 后值】旧测试遍历 (govShare, retainRatio) 两个比例；删除留存比例后，
// 三条腿由 book.ProfitAllocate 按"先补池、再按持股"算出，本测试改为
// 直接遍历三条腿的组合，验证 ProfitAllocate 恒满足 Σ借 = Σ贷。
func TestProfitAllocateSumsToProfit(t *testing.T) {
	a := NewAuditor()
	cases := []struct {
		retain, gov, owner float64
	}{
		{300, 700, 0},      // 全额补池后无可分配（极端）
		{0, 210, 490},      // 不补池，全部按 0.30 持股分配
		{100, 270, 630},    // 部分补池
		{0, -210, -490},    // 亏损：政府与所有者按持股承担
		{0, 0, 0},          // 零利润
		{-100, 210, 490},   // 负数补池（不应出现，但借贷仍须相等）
	}
	for _, tc := range cases {
		tx := ProfitAllocate(0, tc.retain, tc.gov, Building(12), tc.owner)
		if !tx.Balanced() {
			t.Errorf("retain=%.2f gov=%.2f owner=%.2f: 借贷不等（净 %.9f）",
				tc.retain, tc.gov, tc.owner, tx.Net())
		}
		if err := a.Post(tx); err != nil {
			t.Errorf("retain=%.2f gov=%.2f owner=%.2f: %v", tc.retain, tc.gov, tc.owner, err)
		}
	}
}

// TestAccountTotalMatchesSumOfPools 校验"货币总量 = 各类账户之和"是表的定义。
//
// 【前值 → 后值】§4.5.1b 新增投资池账户后，五类账户（政府/资本/投资池/建筑/人群）
// 之和才是货币总量；旧断言只加了四类。
func TestAccountTotalMatchesSumOfPools(t *testing.T) {
	a := NewAuditor()
	a.SetBalance(Gov(), 1000)
	a.SetBalance(Capital(), 2000)
	a.SetBalance(Investment(), 500)
	a.SetBalance(Building(0), 3000)
	a.SetBalance(Household(0, 0, 3), 4000)

	sum := a.TotalOf(KindGovernment) + a.TotalOf(KindCapital) + a.TotalOf(KindInvestment) +
		a.TotalOf(KindBuilding) + a.TotalOf(KindHousehold)
	if math.Abs(sum-a.Total()) > Epsilon {
		t.Errorf("分类合计 %.2f ≠ 总量 %.2f", sum, a.Total())
	}
}
