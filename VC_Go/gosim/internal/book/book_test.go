package book

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// newBook 建立一个带开局注资的记账簿。
func newBook(t *testing.T) *Book {
	t.Helper()
	b := New(12, 3, 10, 11)
	b.Endow(ledger.Gov(), 1_000_000)
	b.Endow(ledger.Capital(), 500_000)
	for i := 0; i < 12; i++ {
		b.Endow(ledger.Building(i), 300_000)
	}
	return b
}

// TestAllFlowsConserveMoney 是核心测试：把七类资金流动连续跑一遍，
// 每个方法调用之后全社会货币存量必须逐位不变。
//
// 这个断言【不依赖任何业务逻辑】，只依赖"每类流动都借贷相等"。
// 因此它天然覆盖全部流动类型，包括未来新增的——
// 只要新类型也写在本包里，这个测试就不需要修改。
func TestAllFlowsConserveMoney(t *testing.T) {
	b := newBook(t)
	m0 := b.Total()
	const taxRate = 0.10

	check := func(step string) {
		t.Helper()
		if got := b.Total(); math.Abs(got-m0) > ledger.Epsilon {
			t.Fatalf("%s 之后货币不守恒：%.6f → %.6f（Δ=%.6f）",
				step, m0, got, got-m0)
		}
	}

	// ① 工资
	wage := b.PayWages([]WageLeg{
		{Site: 0, Class: 0, Population: 3750, Amount: 18_750},
		{Site: 0, Class: 1, Population: 1000, Amount: 10_000},
		{Site: 0, Class: 2, Population: 250, Amount: 5_000},
		{Site: 10, Class: 0, Population: 3750, Amount: 18_750},
		{Site: 10, Class: 1, Population: 1000, Amount: 10_000},
		{Site: 10, Class: 2, Population: 250, Amount: 5_000},
	})
	if wage <= 0 {
		t.Fatal("工资过账额为 0")
	}
	check("① 工资")

	// ② 消费（卖方为建筑 0 与 3）
	spend, tax, consumed := b.Consume([]ConsumerLeg{
		{Pool: b.House(0, 0), Net: 12_000},
		{Pool: b.House(0, 1), Net: 6_000},
		{Pool: b.House(10, 2), Net: 3_000},
	}, taxRate,
		[]ledger.Account{ledger.Building(0), ledger.Building(3)},
		[]float64{0.6, 0.4})
	if spend <= 0 || tax <= 0 {
		t.Fatalf("消费过账异常：spend=%.2f tax=%.2f", spend, tax)
	}
	// 逐卖方返还额必须与消费净额逐位相等（否则逐建筑对账会挂假残差）
	var consumedSum float64
	for _, v := range consumed {
		consumedSum += v
	}
	if math.Abs(consumedSum-spend) > 1e-9 {
		t.Fatalf("消费逐卖方返还额合计 %.6f ≠ 消费净额 %.6f", consumedSum, spend)
	}
	if got := consumed[ledger.Building(3)]; math.Abs(got-spend*0.4) > 1e-9 {
		t.Errorf("建筑 3 的消费收入 %.6f，应为其占比 0.4×%.6f = %.6f", got, spend, spend*0.4)
	}
	check("② 消费")

	// ③ 中间投入（买方 9 向卖方 8 采购）
	sellerOf := func(buyer int) (int, bool) {
		switch buyer {
		case 9:
			return 8, true
		case 10:
			return 8, true
		}
		return -1, false
	}
	net, taxIn, paid, income := b.PayIntermediate([]IntermediateLeg{
		{Buyer: 9, Need: 20_000},
		{Buyer: 10, Need: 15_000},
	}, taxRate, sellerOf)
	if net <= 0 || taxIn <= 0 || len(paid) == 0 {
		t.Fatalf("中间投入过账异常：net=%.2f tax=%.2f paid=%d", net, taxIn, len(paid))
	}
	// 卖方 8 收到的净额必须等于中间投入净额（两个买方的卖方都是 8）
	if got := income[8]; math.Abs(got-net) > 1e-9 {
		t.Errorf("卖方 8 的中间投入收入 %.6f ≠ 净额 %.6f", got, net)
	}
	check("③ 中间投入")

	// ④ 政府采购建造力（§4.5.3 G2：不计税、按需采购）
	pr := b.PurchasePower(5, 1000)
	if pr.Amount <= 0 {
		t.Fatalf("采购过账异常：%+v", pr)
	}
	// 政府支出必须恰等于建造部门收到的全额（无税腿）
	check("④ 政府采购建造力")

	// ⑤ 投资池入池（庄园与金融区）——§4.5.1b
	b.InvestmentInflow(ledger.Building(12), 3_000)
	check("⑤ 投资池入池（庄园）")
	b.InvestmentInflow(ledger.Capital(), 2_000)
	check("⑤b 投资池入池（金融区）")

	// ⑥ 投资池付政府（G6）：政府净支出归零
	b.InvestmentPayGov(4_000)
	check("⑥ 投资池付政府")

	// ⑦ 利润归属（含亏损）：三条腿之和恒等于纯利
	//（站点 0 的现金池已在上面的消费环节被改动，故按实际余额传入 B_i；
	// 这里只校验"借贷相等 + 三条腿之和 = 纯利"，C* 取一个较大的目标值。）
	b.ProfitAllocate(0, 8_000, 0.30, 1e12, b.BalBld(0), ledger.Capital())
	check("⑦ 利润归属（盈利）")
	b.ProfitAllocate(9, -5_000, 0.30, 1e12, b.BalBld(9), ledger.Capital())
	check("⑦ 利润归属（亏损）")

	// ⑧ 货币注入：这是唯一【允许】改变存量的操作
	before := b.Total()
	b.NewCapital(3, 25_000)
	if got := b.Total(); math.Abs(got-(before+25_000)) > ledger.Epsilon {
		t.Fatalf("注入后存量 = %.2f，应为 %.2f", got, before+25_000)
	}
	if got := b.TotalInjected(); math.Abs(got-25_000) > ledger.Epsilon {
		t.Fatalf("累计注入 = %.2f，应为 25000", got)
	}

	if v := b.Violations(); len(v) != 0 {
		t.Errorf("存在借贷不相等记录：%v", v)
	}
}

// TestPayWagesBalancesByConstruction 校验工资的借贷两侧由同一份数据得出。
//
// 这正是"漏记一半"在结构上不可能发生的原因：借方是按场地汇总的同一批金额，
// 贷方是同一批金额按阶级拆开。
func TestPayWagesBalancesByConstruction(t *testing.T) {
	b := New(12, 3, 10, 11)
	legs := []WageLeg{
		{Site: 2, Class: 0, Amount: 1000},
		{Site: 2, Class: 1, Amount: 400},
		{Site: 2, Class: 2, Amount: 100},
		{Site: 5, Class: 0, Amount: 700},
	}
	total := b.PayWages(legs)
	if math.Abs(total-2200) > 1e-9 {
		t.Fatalf("工资总额 = %.2f，应为 2200", total)
	}
	// 建筑侧应恰好减少各自汇总额
	if got := b.BalBld(2); math.Abs(got-(-1500)) > 1e-9 {
		t.Errorf("建筑 2 余额 = %.2f，应为 -1500", got)
	}
	if got := b.BalBld(5); math.Abs(got-(-700)) > 1e-9 {
		t.Errorf("建筑 5 余额 = %.2f，应为 -700", got)
	}
	// 人群侧应恰好收到各自份额
	if got := b.BalHouse(2, 0); math.Abs(got-1000) > 1e-9 {
		t.Errorf("人群(2,0) 余额 = %.2f，应为 1000", got)
	}
	if got := b.BalHouse(2, 2); math.Abs(got-100) > 1e-9 {
		t.Errorf("人群(2,2) 余额 = %.2f，应为 100", got)
	}
}

// TestConsumeResidualGoesToLastSeller 校验卖方分摊的累计残差机制：
// 即使占比之和因浮点不精确等于 1，Σ贷方净额仍与 Σ借方净额逐位相等。
func TestConsumeResidualGoesToLastSeller(t *testing.T) {
	b := New(12, 3, 10, 11)
	b.Endow(b.House(0, 0), 1_000_000)
	// 占比之和 = 0.9999999999（故意不精确）
	sellers := []ledger.Account{ledger.Building(0), ledger.Building(1), ledger.Building(2)}
	shares := []float64{0.333_333_333_3, 0.333_333_333_3, 0.333_333_333_3}
	m0 := b.Total()
	spend, _, _ := b.Consume([]ConsumerLeg{{Pool: b.House(0, 0), Net: 100_000}}, 0.10, sellers, shares)
	if math.Abs(spend-100_000) > 1e-9 {
		t.Fatalf("消费净额 = %.6f，应为 100000", spend)
	}
	// 三家之和必须恰好等于净额
	sum := b.BalBld(0) + b.BalBld(1) + b.BalBld(2)
	if math.Abs(sum-100_000) > 1e-9 {
		t.Errorf("卖方入账合计 = %.9f，应恰为 100000", sum)
	}
	if got := b.Total(); math.Abs(got-m0) > ledger.Epsilon {
		t.Errorf("消费后货币不守恒：%.6f → %.6f", m0, got)
	}
}

// TestIntermediateIsNotShrunkByCash 校验 §4.3 裁决后的口径：
// **资金不足不再缩减中间投入付款**——买方按申报额全额成交，现金池允许透支。
//
// 【前值 → 后值】本条测试原为 TestIntermediateShrinksWhenCashShort，
// 断言"余额 1,100 时只成交净额 1,000"。2026-09-19 删除"资金不足时停工"后，
// 改为断言全额成交 50,000 且余额变为 1,100 − 55,000 = −53,900（透支）。
func TestIntermediateIsNotShrunkByCash(t *testing.T) {
	b := New(12, 3, 10, 11)
	b.Endow(ledger.Building(5), 1_100)
	sellerOf := func(int) (int, bool) { return 4, true }
	net, tax, paid, _ := b.PayIntermediate([]IntermediateLeg{{Buyer: 5, Need: 50_000}}, 0.10, sellerOf)
	if math.Abs(net-50_000) > 1e-6 {
		t.Errorf("净额 = %.6f，应恒为申报额 50000（不再按可用资金缩减）", net)
	}
	if math.Abs(tax-5_000) > 1e-6 {
		t.Errorf("税额 = %.6f，应为 5000", tax)
	}
	if math.Abs(paid[5]-55_000) > 1e-6 {
		t.Errorf("买方实付 = %.6f，应为含税全额 55000", paid[5])
	}
	if got := b.BalBld(5); math.Abs(got-(-53_900)) > 1e-6 {
		t.Errorf("买方余额 = %.6f，应为 −53900（允许透支）", got)
	}
	if got := b.Total(); math.Abs(got-1_100) > ledger.Epsilon {
		t.Errorf("中间投入交易后货币不守恒：%.6f", got)
	}
}

// TestGovPurchaseIsTaxFreeAndInvestmentRepays 校验 §4.5.3 的两条裁决：
//
//	① 建造力交易**不计税**：政府支出恰等于建造部门收到的全额；
//	② G6 由投资池全额偿还 ⇒ 政府的建造力**净支出为 0**（本版无政府自有项目）。
//
// 【前值 → 后值】旧测试断言"政府净支出恰为 Net（隐含 Net·t 的自反税留在池内）"；
// 建造力交易定案不计税后，Gross ≡ Net，且投资池偿还把它再收回来。
func TestGovPurchaseIsTaxFreeAndInvestmentRepays(t *testing.T) {
	b := New(13, 3, 10, 11)
	b.Endow(ledger.Gov(), 100_000)
	b.Endow(ledger.Investment(), 100_000)

	pr := b.PurchasePower(10, 1000)
	want := 100_000 - pr.Amount
	if got := b.BalGov(); math.Abs(got-want) > 1e-6 {
		t.Errorf("政府支出后余额 = %.4f，应为 %.4f（支出应恰为全额 %.4f，不计税）",
			got, want, pr.Amount)
	}
	if got := b.BalBld(10); math.Abs(got-pr.Amount) > 1e-6 {
		t.Errorf("建造力部门入账 = %.4f，应恰为全额 %.4f", got, pr.Amount)
	}

	// G6：投资池偿还同一笔货款，政府净支出归零
	b.InvestmentPayGov(pr.Amount)
	if got := b.BalGov(); math.Abs(got-100_000) > 1e-6 {
		t.Errorf("投资池偿还后政府余额 = %.4f，应为初值 100000（净支出为 0）", got)
	}
	if got := b.BalInvestment(); math.Abs(got-(100_000-pr.Amount)) > 1e-6 {
		t.Errorf("投资池余额 = %.4f，应为 %.4f", got, 100_000-pr.Amount)
	}
}

// TestProfitAllocatePartitionAndLoss 校验 §4.5.1 的"先补池、再按持股"口径。
//
// 【前值 → 后值】旧测试断言 700 / 150 / 150 的三份额（含留存比例 0.5）；
// 留存比例已删除，改为：
//
//	R_i = min(max(π,0), max(0, C*−B))，余额按当期持股 0.30 分配
//
// 并新增亏损情形：政府与所有者按持股承担、建筑池同额回补，三腿之和恒为 0。
func TestProfitAllocatePartitionAndLoss(t *testing.T) {
	const profit, share, cstar = 1000.0, 0.30, 10_000.0
	b := New(13, 3, 10, 11)
	b.Endow(ledger.Building(0), 0)

	// 现金池为空 ⇒ 全额补足（R = min(1000, 10000-0) = 1000），可分配余额为 0。
	res := b.ProfitAllocate(0, profit, share, cstar, b.BalBld(0), ledger.Capital())
	if math.Abs(res.Retain-profit) > 1e-9 {
		t.Errorf("补足额 = %.4f，应为 %.4f（现金池为空 ⇒ 全额留池）", res.Retain, profit)
	}
	if math.Abs(res.Gov+res.Owner) > 1e-9 {
		t.Errorf("可分配余额 = %.4f，应为 0（利润已被补足吸收）", res.Gov+res.Owner)
	}
	if sum := res.Retain + res.Gov + res.Owner; math.Abs(sum-profit) > 1e-9 {
		t.Errorf("三腿之和 = %.6f，必须恰好等于纯利 %.6f", sum, profit)
	}

	// 现金池已达标 ⇒ 不补池，余额按 0.30 / 0.70 分配。
	b2 := New(13, 3, 10, 11)
	b2.Endow(ledger.Building(1), 10_000)
	res2 := b2.ProfitAllocate(1, profit, share, cstar, b2.BalBld(1), ledger.Capital())
	if math.Abs(res2.Retain) > 1e-9 {
		t.Errorf("补足额 = %.4f，应为 0（现金池已达 C*）", res2.Retain)
	}
	if math.Abs(res2.Gov-300) > 1e-9 {
		t.Errorf("政府份额 = %.3f，应为 1000×0.30 = 300", res2.Gov)
	}
	if math.Abs(res2.Owner-700) > 1e-9 {
		t.Errorf("所有者份额 = %.3f，应为 1000−300 = 700", res2.Owner)
	}

	// 亏损：三腿之和恒为 0（政府 −300、所有者 −700、建筑池 +1000）。
	b3 := New(13, 3, 10, 11)
	res3 := b3.ProfitAllocate(2, -1000, share, cstar, b3.BalBld(2), ledger.Capital())
	if math.Abs(res3.Retain) > 1e-9 {
		t.Errorf("亏损时不应补池，补足额 = %.4f", res3.Retain)
	}
	if math.Abs(res3.Gov-(-300)) > 1e-9 {
		t.Errorf("政府承担的亏损 = %.3f，应为 −300", res3.Gov)
	}
	if math.Abs(res3.Owner-(-700)) > 1e-9 {
		t.Errorf("所有者承担的亏损 = %.3f，应为 −700", res3.Owner)
	}
	if sum := res3.Retain + res3.Gov + res3.Owner; math.Abs(sum-(-1000)) > 1e-9 {
		t.Errorf("亏损三腿之和 = %.6f，必须恰好等于纯利 −1000", sum)
	}
}

// TestPrivatizePriceIsUnitTimesBuildCost 量出 Privatize 的对价口径：
// 实付必须恰好等于 转让级数 × 每级对价 × (1+t)。
func TestPrivatizePriceIsUnitTimesBuildCost(t *testing.T) {
	b := New(12, 3, 10, 11)
	const capitalInit = 1e15
	b.Endow(ledger.Capital(), capitalInit)

	const wantUnits = 14.0
	const unitPrice = 1_191_700.0
	const taxRate = 0.10

	res := b.Privatize(wantUnits, unitPrice, taxRate)
	wantPaid := wantUnits * unitPrice * (1 + taxRate)

	t.Logf("wantUnits=%.4f unitPrice=%.2f t=%.2f", wantUnits, unitPrice, taxRate)
	t.Logf("实付 = %.4f，应 = %.4f", res.Paid, wantPaid)
	t.Logf("转让级数 = %.6f，应 = %.6f", res.Units, wantUnits)

	if math.Abs(res.Paid-wantPaid) > 1e-6 {
		t.Errorf("实付 %.4f ≠ %.4f（比率 %.6f）", res.Paid, wantPaid, res.Paid/wantPaid)
	}
	if math.Abs(res.Units-wantUnits) > 1e-9 {
		t.Errorf("转让级数 %.9f ≠ %.9f", res.Units, wantUnits)
	}
	// 资金腿：资本池减少 = 政府池增加 = 实付
	if got := capitalInit - b.BalCapital(); math.Abs(got-res.Paid) > 1e-6 {
		t.Errorf("资本池减少 %.4f，应等于实付 %.4f", got, res.Paid)
	}
	if got := b.BalGov(); math.Abs(got-res.Paid) > 1e-6 {
		t.Errorf("政府池增加 %.4f，应等于实付 %.4f", got, res.Paid)
	}
}

// TestViolationsAreRecorded 校验借贷不等的交易会被记录（而非静默通过）。
func TestViolationsAreRecorded(t *testing.T) {
	b := New(12, 3, 10, 11)
	bad := &ledger.Txn{Name: "故意不平衡"}
	bad.Debit(ledger.Building(0), 100)
	bad.Credit(ledger.Building(1), 99)
	if err := b.Post(bad); err == nil {
		t.Fatal("借贷不等的交易应被拒绝")
	}
	if len(b.Violations()) != 1 {
		t.Errorf("违规记录数 = %d，应为 1", len(b.Violations()))
	}
}
