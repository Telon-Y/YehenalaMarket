package fiscal

import (
	"math"
	"testing"

	"yehenala/market/internal/ledger"
)

// newGov 构造一个挂接到新审计账本的政府，并给出开局注资。
func newGov(t *testing.T, cash float64) (*Government, *ledger.Auditor) {
	t.Helper()
	aud := ledger.NewAuditor()
	gov := &Government{}
	gov.Cash = NewLedger(aud, ledger.Gov())
	gov.Cash.SetInitial(cash)
	return gov, aud
}

// noopCredit 是 SellPower 的收款方入账回调占位（不关心卖方时使用）。
func noopCredit(float64) {}

// TestConstructionSinkIsClosed 是本包存在的理由的守门测试。
//
// 断言：在 G1–G6 的规则下，一个 tick 的货币总量守恒——
// 政府采购建造力支付的货币，等于建造部门收到的货款加政府代扣的税；
// 企业购买建造力支付的货币，全额回到政府现金池。
//
// 这正是 1.0 契约缺失的部分：§4.3 只写"建造力费用从现金池扣除"，
// 未定义收款方，使建造力成为单向的资金漏出（量化缺口 5.1e4 倍，
// 见 tools/construction_sink_probe.js）。
func TestConstructionSinkIsClosed(t *testing.T) {
	const start = 1_000_000.0
	gov, aud := newGov(t, start)
	gov.PowerOutput = 1000
	gov.TaxRate = 0.10

	const powerPrice = 7250.0
	const powerOutput = 100.0
	m0 := aud.Total()

	// 政府向建造部门采购
	bought := gov.PurchasePower(powerOutput, powerPrice)
	t.Logf("诊断: TaxRate=%.4f bought=%.4f 余额=%.2f 税收=%.2f",
		gov.TaxRate, bought, gov.Cash.Balance(), gov.TaxCollected)
	if bought != powerOutput {
		t.Fatalf("政府应买下全部 %v 单位，实际 %v", powerOutput, bought)
	}
	spent := bought * powerPrice
	// 政府池净减少 = Gross − Tax = Net（税是自己收自己，留在池内）
	net := spent / (1 + gov.TaxRate)
	if got := gov.Cash.Balance(); math.Abs(got-(start-net)) > 1e-6 {
		t.Fatalf("采购后政府余额 %.3f，应为 %.3f", got, start-net)
	}
	if got := gov.TaxCollected; math.Abs(got-(spent-net)) > 1e-6 {
		t.Errorf("采购环节税收 %.3f，应为 %.3f", got, spent-net)
	}
	// 单边分录会改变总量，故此处只校验"借贷两侧已配对"：
	// 政府池的 Net 流出必须由建造部门的 Net 流入承接（下一行模拟该入账）。
	aud.Inject(ledger.Building(PowerGoodIndex), net)
	if got := aud.Total(); math.Abs(got-m0) > 1e-6 {
		t.Errorf("采购环节货币不守恒：%.3f → %.3f", m0, got)
	}

	// 政府把建造力卖给一个私有扩建方。
	// 借 付款方 Gross；贷 政府 Gross——付款方的扣款由调用方完成，此处用一个
	// 独立钱包模拟，并同样记入审计账本，以便整体校验守恒。
	wallets := map[string]float64{"firm0": 10_000_000}
	firmAcc := ledger.Building(0)
	aud.SetBalance(firmAcc, wallets["firm0"])
	m1 := aud.Total()

	sales := gov.SellPower(
		[]SaleRequest{{BuildingIndex: 0, Units: 1, PowerPerLevel: 30, Payer: "firm0"}},
		powerPrice, 30,
		func(payer string, _ int) float64 { return wallets[payer] / (1 + gov.TaxRate) },
		noopCredit,
	)
	if len(sales) != 1 {
		t.Fatalf("应有 1 笔成交，实际 %d", len(sales))
	}
	sold := sales[0].Granted
	if math.Abs(sold-30) > 1e-9 {
		t.Fatalf("§4.2 规定每工地每 tick 最多 30 建造力，实际成交 %.3f", sold)
	}
	// 调用方按含税口径给付款方扣款（这是借贷相等的另一半）
	gross := sales[0].Paid * (1 + gov.TaxRate)
	aud.Withdraw(firmAcc, gross)
	if got := aud.Total(); math.Abs(got-m1) > 1e-6 {
		t.Errorf("售力环节货币不守恒：%.3f → %.3f（Δ=%.6f）", m1, got, got-m1)
	}
	if math.Abs(gov.PowerInventory-(powerOutput-sold)) > 1e-9 {
		t.Fatalf("政府库存应为 %.3f，实际 %.3f", powerOutput-sold, gov.PowerInventory)
	}
	if err := gov.Validate(powerPrice); err != nil {
		t.Fatalf("账目不变量被破坏: %v", err)
	}
}

// TestSoldCannotExceedPurchased 校验建造力不会被凭空出售。
func TestSoldCannotExceedPurchased(t *testing.T) {
	gov, _ := newGov(t, 1e12)
	gov.PowerOutput = 1000
	gov.PurchasePower(10, 1000)

	wallets := map[string]float64{"firm0": 1e12}
	gov.SellPower(
		[]SaleRequest{
			{BuildingIndex: 0, Units: 100, PowerPerLevel: 1, Payer: "firm0"},
			{BuildingIndex: 0, Units: 100, PowerPerLevel: 1, Payer: "firm0"},
		},
		1000, 1000,
		func(payer string, _ int) float64 { return wallets[payer] },
		noopCredit,
	)
	if gov.PowerSold > gov.PowerPurchased+1e-9 {
		t.Fatalf("售出 %.3f 超过采购 %.3f", gov.PowerSold, gov.PowerPurchased)
	}
	if gov.PowerInventory < -1e-9 {
		t.Fatalf("库存为负: %.6f", gov.PowerInventory)
	}
}

// TestSitePowerLimit 校验 §4.2 的"每工地每 tick 最多 30 建造力"硬约束。
func TestSitePowerLimit(t *testing.T) {
	gov, _ := newGov(t, 1e12)
	gov.PowerOutput = 1000
	gov.PurchasePower(1000, 100)

	wallets := map[string]float64{"firm0": 1e12}
	sales := gov.SellPower(
		[]SaleRequest{{BuildingIndex: 0, Units: 100, PowerPerLevel: 100, Payer: "firm0"}},
		100, 30,
		func(payer string, _ int) float64 { return wallets[payer] },
		noopCredit,
	)
	if len(sales) != 1 {
		t.Fatalf("应有 1 笔成交，实际 %d", len(sales))
	}
	if sales[0].Granted > 30+1e-9 {
		t.Fatalf("单笔成交 %.3f 超过每工地上限 30", sales[0].Granted)
	}
}

// TestControlCapacityIsHardConstraint 校验 G5：金融区每级只能掌控 5 级其余建筑。
func TestControlCapacityIsHardConstraint(t *testing.T) {
	var c Capital
	c.UpdateControl(10, 0, 5)
	if c.ControlCapacity != 50 {
		t.Fatalf("掌控上限 = %.1f，应为 10×5 = 50", c.ControlCapacity)
	}
	for i := 0; i < 10; i++ {
		if !c.CanControl(5) {
			t.Fatalf("第 %d 次追加 5 级应被允许", i+1)
		}
		c.ControlledLevels += 5
	}
	if c.CanControl(1) {
		t.Error("已达上限 50 级，不应还能追加")
	}
}

// TestProfitDistributionSplitsByOwnership 校验 G3/G4 的分账是【划分】而非复制。
//
// 修订说明（docs/AUDIT-1.0.md §4）：原实现让建筑现金池拿全额利润、
// 政府/资本池再各拿一份，实测 Δ货币/Σ利润 = 2.00（重复入账）。
// 正确语义下三个份额之和必须恰好等于利润本身。
func TestProfitDistributionSplitsByOwnership(t *testing.T) {
	const profit = 1000.0
	gov, capital, retain := ShareProfit(profit, 0.70, 0.50)

	if math.Abs(gov-700) > 1e-9 {
		t.Errorf("政府份额 = %.3f，应为 1000×0.70 = 700", gov)
	}
	// 私有份额 300，其中一半留存于建筑、一半归资本
	if math.Abs(retain-150) > 1e-9 {
		t.Errorf("建筑留存 = %.3f，应为 300×0.50 = 150", retain)
	}
	if math.Abs(capital-150) > 1e-9 {
		t.Errorf("资本份额 = %.3f，应为 300×0.50 = 150", capital)
	}
	// 核心不变量：三者之和恒等于利润
	if sum := gov + capital + retain; math.Abs(sum-profit) > 1e-9 {
		t.Errorf("三份额之和 = %.6f，必须恰好等于利润 %.6f（否则即为重复记账/漏记）", sum, profit)
	}
}

// TestShareProfitSumsToProfitForAllRatios 遍历边界比例，确认"划分"语义恒成立。
func TestShareProfitSumsToProfitForAllRatios(t *testing.T) {
	for _, share := range []float64{-0.5, 0, 0.3, 0.7, 1.0, 1.5} {
		for _, retain := range []float64{-1, 0, 0.25, 0.5, 1, 2} {
			for _, profit := range []float64{-1234.5, 0, 999.99} {
				g, c, r := ShareProfit(profit, share, retain)
				if math.Abs((g+c+r)-profit) > 1e-9 {
					t.Errorf("share=%.2f retain=%.2f profit=%.2f: 和 = %.9f ≠ %.9f",
						share, retain, profit, g+c+r, profit)
				}
			}
		}
	}
}

// TestTransactionSplitsIntoNetAndTax 校验 §4.5.3 的交易拆分：
// 买方支付 Net(1+t)，卖方收到 Net，政府收到 Net·t，三者之和为零。
func TestTransactionSplitsIntoNetAndTax(t *testing.T) {
	for _, tc := range []struct{ net, rate float64 }{
		{0, 0.1}, {100, 0}, {100, 0.10}, {1234.56, 0.10}, {-5, 0.1},
	} {
		tx := NewTransaction(tc.net, tc.rate)
		if tc.net <= 0 {
			if tx.Net != 0 || tx.Tax != 0 || tx.Gross != 0 {
				t.Errorf("非正货款 %.2f 应得零交易，实际 %+v", tc.net, tx)
			}
			continue
		}
		if math.Abs(tx.Net+tx.Tax-tx.Gross) > 1e-9 {
			t.Errorf("Net(%.6f) + Tax(%.6f) ≠ Gross(%.6f)", tx.Net, tx.Tax, tx.Gross)
		}
		if math.Abs(tx.Tax-tc.net*tc.rate) > 1e-9 {
			t.Errorf("税额 %.6f，应为 %.6f", tx.Tax, tc.net*tc.rate)
		}
		// 双边记账：买方 −Gross、卖方 +Net、政府 +Tax，合计恒为 0
		if sum := -tx.Gross + tx.Net + tx.Tax; math.Abs(sum) > 1e-9 {
			t.Errorf("双边记账不守恒：%.9f", sum)
		}
	}
}

// TestTaxBaseIsTotalTransactionValue 校验 G1 的税基是"全部交易额"。
func TestTaxBaseIsTotalTransactionValue(t *testing.T) {
	const consumer, intermediate, power = 100, 200, 50
	got := CollectTax(0.10, consumer, intermediate, power)
	want := (consumer + intermediate + power) * 0.10
	if math.Abs(got-want) > 1e-9 {
		t.Errorf("税额 = %.4f，应为 %.4f", got, want)
	}
	if CollectTax(0, consumer, intermediate, power) != 0 {
		t.Error("税率为 0 时不应产生税收")
	}
	if TaxOn(-1, 0.1) != 0 {
		t.Error("非正货款不应产生税额")
	}
}

// TestAvailableCashThreeSegment 校验 §4.5.4 的"可动用资金"三段式。
//
// 这是最容易写错的一处：若写成"余额为正就返回全额余额"，
// 则政府只要手里有钱就能在上限之外继续支出，上限形同虚设。
func TestAvailableCashThreeSegment(t *testing.T) {
	gov, _ := newGov(t, 0)
	gov.PowerOutput = 100 // AssetBase = 100×1000 = 100_000，上限 = 200_000
	const price = 1000.0
	cap := gov.DebtCap(price)
	if math.Abs(cap-200_000) > 1e-6 {
		t.Fatalf("债务上限 = %.2f，应为 200000", cap)
	}

	// B > 0 且 B < cap：可动用 = B
	gov.Cash.SetInitial(50_000)
	if got := gov.AvailableCash(price); math.Abs(got-50_000) > 1e-6 {
		t.Errorf("B=50000: 可动用 = %.2f，应为 50000", got)
	}

	// B > cap：可动用被截到 cap（不得超出上限）
	gov.Cash.SetInitial(900_000)
	if got := gov.AvailableCash(price); math.Abs(got-cap) > 1e-6 {
		t.Errorf("B=900000(>上限): 可动用 = %.2f，应被截到 %.2f", got, cap)
	}

	// B = 0：可动用 = cap（全部举债空间可用）
	gov.Cash.SetInitial(0)
	if got := gov.AvailableCash(price); math.Abs(got-cap) > 1e-6 {
		t.Errorf("B=0: 可动用 = %.2f，应为 %.2f", got, cap)
	}

	// B < 0：可动用 = cap + B（剩余举债空间）
	gov.Cash.SetInitial(-50_000)
	if got := gov.AvailableCash(price); math.Abs(got-(cap-50_000)) > 1e-6 {
		t.Errorf("B=-50000: 可动用 = %.2f，应为 %.2f", got, cap-50_000)
	}

	// 三段在 B=0 处连续：B→0⁻ 的极限必须等于 B=0 的取值。
	// 注意不能用 B=1e-9 验证——switch 的 b==0 分支是浮点精确比较，
	// 故"连续"指的是数学意义上的左极限，而非可任意取小值。
	gov.Cash.SetInitial(-1e-6)
	if got := gov.AvailableCash(price); math.Abs(got-cap) > 0.01 {
		t.Errorf("B→0⁻ 的极限 = %.4f，应趋近 %.2f（三段须在此连续）", got, cap)
	}
	gov.Cash.SetInitial(0)
	if got := gov.AvailableCash(price); math.Abs(got-cap) > 1e-6 {
		t.Errorf("B=0 的值 = %.4f，应恰为 %.2f", got, cap)
	}
}

// TestDebtCapScalesWithPowerOutput 校验 §4.5.4 的策略含义：
// 提高债务上限的唯一途径是扩建建造部门。
func TestDebtCapScalesWithPowerOutput(t *testing.T) {
	gov, _ := newGov(t, 0)
	const price = 7250.0
	gov.PowerOutput = 300
	c1 := gov.DebtCap(price)
	gov.PowerOutput = 1500
	c2 := gov.DebtCap(price)
	if math.Abs(c2/c1-5) > 1e-9 {
		t.Fatalf("建造力产出 ×5 后上限应 ×5，实际比值 %.6f", c2/c1)
	}
}

// TestGovOwnBuildoutDoesFullDoubleEntry 校验政府自建项目的付款是【完整双边记账】。
//
// 历史缺陷：旧实现把政府自建项目当"内部转账"，只累加 BuildoutPaid
// 而【不动任何账户余额】——工程是白得的，货币守恒审计会看到一个无人认领的差额。
//
// 修订后：借 政府 Gross；贷 建造力卖方 Net；贷 政府 Tax。
// 三本账合账为零，且政府池净减少恰为 Net。
func TestGovOwnBuildoutDoesFullDoubleEntry(t *testing.T) {
	gov, aud := newGov(t, 1_000_000)
	gov.PowerOutput = 1000
	gov.TaxRate = 0.10
	const price = 1000.0
	gov.PurchasePower(100, price)

	// 把建造部门账户挂进审计账本，使 credit 回调能真实入账
	powerAcc := ledger.Building(PowerGoodIndex)
	aud.SetBalance(powerAcc, 0)

	cashAfterPurchase := gov.Cash.Balance()
	taxAfterPurchase := gov.TaxCollected
	m0 := aud.Total()

	sales := gov.SellPower(
		[]SaleRequest{{BuildingIndex: 0, Units: 1, PowerPerLevel: 10, Payer: "gov"}},
		price, 1000,
		func(string, int) float64 { return gov.AvailableCash(price) },
		func(net float64) { aud.Inject(powerAcc, net) },
	)
	if len(sales) != 1 {
		t.Fatalf("应有 1 笔政府自建成交，实际 %d", len(sales))
	}
	paid := sales[0].Paid

	// 建造力卖方必须真实收到净额
	if got := aud.Balance(powerAcc); math.Abs(got-paid) > 1e-9 {
		t.Errorf("卖方入账 %.4f，应为净额 %.4f", got, paid)
	}
	// 政府按含税总额扣款、又收回自己那笔税，故净减少恰为 Net
	if got := gov.Cash.Balance(); math.Abs(got-(cashAfterPurchase-paid)) > 1e-6 {
		t.Errorf("政府付款后余额 %.4f，应为 %.4f（净减少 Net=%.4f）",
			got, cashAfterPurchase-paid, paid)
	}
	// 税必须入库
	if got := gov.TaxCollected; math.Abs(got-(taxAfterPurchase+paid*gov.TaxRate)) > 1e-9 {
		t.Errorf("政府自建项目的税未入库：%.4f → %.4f", taxAfterPurchase, got)
	}
	// 货币总量不变（买卖双方都在审计账本内）
	if got := aud.Total(); math.Abs(got-m0) > 1e-6 {
		t.Errorf("政府自建项目不守恒：%.4f → %.4f（Δ=%.6f）", m0, got, got-m0)
	}
}

// TestPowerGoodIndexMatchesContract 校验"建造力"的下标与契约 §3.1 表格一致
// （建造力是 11 种商品的最后一项）。这个下标被 sim 与 build 包硬编码引用，
// 一旦商品表顺序变动而没有同步，整套财政闭环会静默错位。
func TestPowerGoodIndexMatchesContract(t *testing.T) {
	if PowerGoodIndex != 10 {
		t.Fatalf("建造力下标 = %d，契约 §3.1 规定它是第 11 项（下标 10）", PowerGoodIndex)
	}
}
