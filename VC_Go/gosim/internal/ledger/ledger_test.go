package ledger

import (
	"math"
	"testing"
)

// TestAllTxnTypesBalance 校验资金流量表里每一类交易的借贷都相等。
//
// 这是本包存在的全部理由：只要每一类交易都借贷相等，
// "货币守恒"就是构造性事实，不需要靠事后审计去追残差。
func TestAllTxnTypesBalance(t *testing.T) {
	const t0 = 0.10
	cases := []struct {
		name string
		txn  *Txn
	}{
		{"①工资", Wage(3, []float64{1000, 400, 100})},
		{"②消费", ConsumerPurchase(Household(3, 1, 3), 5, 2000, t0)},
		{"③中间投入", Intermediate(2, 7, 1500, t0)},
		{"④政府采购建造力", PowerPurchase(10, 5000, t0)},
		{"⑤政府售力", PowerSale(Building(4), 3000, t0)},
		{"⑤政府售力(资本)", PowerSale(Capital(), 3000, t0)},
		{"⑥政府自建付款", GovOwnBuildout(10, 2500, t0)},
		{"⑦利润划分", ProfitSplit(6, 700, 150, 150)},
		{"⑦利润划分(亏损)", ProfitSplit(6, -700, -150, -150)},
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
		PowerPurchase(10, 6000, t0),
		PowerSale(Building(0), 2000, t0),
		GovOwnBuildout(10, 1500, t0),
		ProfitSplit(0, 700, 150, 150),
		ProfitSplit(10, -200, -40, -40),
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

// TestPowerPurchaseNetCostEqualsNet 校验"政府自己收自己"的税额不影响政府净支出。
//
// 这是此前最易错的一处：政府自建/自购时，借方是 Gross、贷方有一笔 Tax 回政府，
// 故政府池的净变动恰好等于 Net。若实现里省掉那笔 Tax 贷方，
// 政府池就会净减 Gross，与卖方收到的 Net 不对等，留下 Net·t 的残差。
func TestPowerPurchaseNetCostEqualsNet(t *testing.T) {
	const net, t0 = 4000.0, 0.10
	a := NewAuditor()
	a.SetBalance(Gov(), 100_000)
	a.SetBalance(Building(10), 0)

	if err := a.Post(PowerPurchase(10, net, t0)); err != nil {
		t.Fatalf("过账失败: %v", err)
	}
	if got := a.Balance(Gov()); math.Abs(got-(100_000-net)) > Epsilon {
		t.Errorf("政府净支出 = %.4f，应恰为 Net = %.4f", 100_000-got, net)
	}
	if got := a.Balance(Building(10)); math.Abs(got-net) > Epsilon {
		t.Errorf("建造力部门入账 = %.4f，应恰为 Net = %.4f", got, net)
	}

	// GovOwnBuildout 同构
	a2 := NewAuditor()
	a2.SetBalance(Gov(), 100_000)
	if err := a2.Post(GovOwnBuildout(10, net, t0)); err != nil {
		t.Fatalf("过账失败: %v", err)
	}
	if got := a2.Balance(Gov()); math.Abs(got-(100_000-net)) > Epsilon {
		t.Errorf("政府自建净支出 = %.4f，应恰为 Net = %.4f", 100_000-got, net)
	}
}

// TestProfitSplitSumsToProfit 校验利润划分的借贷相等条件。
func TestProfitSplitSumsToProfit(t *testing.T) {
	a := NewAuditor()
	for _, tc := range []struct{ profit, govShare, retainRatio float64 }{
		{1000, 0.70, 0.50}, {-500, 0.70, 0.50}, {0, 0.70, 0.50},
		{1234.56, 0, 1}, {999.99, 1, 0},
	} {
		gov, capital, retain := shareProfit(tc.profit, tc.govShare, tc.retainRatio)
		tx := ProfitSplit(0, gov, capital, retain)
		if !tx.Balanced() {
			t.Errorf("profit=%.2f: 借贷不等（净 %.9f）", tc.profit, tx.Net())
		}
		if err := a.Post(tx); err != nil {
			t.Errorf("profit=%.2f: %v", tc.profit, err)
		}
	}
}

// shareProfit 是本包对 §4.5.1 划分规则的复刻，用于自洽测试。
//
// 之所以在这里重写而不 import fiscal：ledger 是被所有业务包依赖的底层包，
// 不能反向依赖 fiscal，否则会形成循环依赖。
func shareProfit(profit, govShare, retainRatio float64) (gov, capital, retain float64) {
	if govShare < 0 {
		govShare = 0
	}
	if govShare > 1 {
		govShare = 1
	}
	if retainRatio < 0 {
		retainRatio = 0
	}
	if retainRatio > 1 {
		retainRatio = 1
	}
	gov = profit * govShare
	priv := profit * (1 - govShare)
	retain = priv * retainRatio
	capital = priv - retain
	return
}

// TestAccountTotalMatchesSumOfPools 校验"货币总量 = 各类账户之和"是表的定义。
func TestAccountTotalMatchesSumOfPools(t *testing.T) {
	a := NewAuditor()
	a.SetBalance(Gov(), 1000)
	a.SetBalance(Capital(), 2000)
	a.SetBalance(Building(0), 3000)
	a.SetBalance(Household(0, 0, 3), 4000)

	sum := a.TotalOf(KindGovernment) + a.TotalOf(KindCapital) +
		a.TotalOf(KindBuilding) + a.TotalOf(KindHousehold)
	if math.Abs(sum-a.Total()) > Epsilon {
		t.Errorf("分类合计 %.2f ≠ 总量 %.2f", sum, a.Total())
	}
}
