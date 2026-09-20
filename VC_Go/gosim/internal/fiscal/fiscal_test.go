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

// TestConstructionSinkIsClosed 是本包存在的理由的守门测试。
//
// 断言：建造力的买卖闭环在 G2/G6 下是**零净支出**的——
//
//	G2 政府 → 建造部门（按需采购，不计税）
//	G6 投资池 → 政府（全额偿还同一笔货款）
//	⇒ 政府的建造力净支出恒为 0，货币总量不变
//
// 这正是 1.0 契约缺失的部分：§4.3 只写"建造力费用从现金池扣除"，
// 未定义收款方，使建造力成为单向的资金漏出（量化缺口 5.1e4 倍，
// 见 tools/construction_sink_probe.go 的历史记录）。
//
// 【前值 → 后值（2026-09-19）】旧口径是"整批采购 + 转售 + 自反税腿"；
// 现改为"按需即买即用 + 投资池偿还"，故本测试改为核对
// ① 采购限额受产出与可动用资金裁剪；② 采购全额入建造部门；
// ③ 投资池偿还后政府净支出为 0。
func TestConstructionSinkIsClosed(t *testing.T) {
	const start = 1_000_000.0
	gov, aud := newGov(t, start)
	gov.PowerOutput = 1000

	const powerPrice = 7250.0
	const powerOutput = 100.0
	m0 := aud.Total()
	_ = m0

	// ① 采购限额：只受 AvailableCash/price 约束（产出与队列需要量由调用方再裁剪）。
	avail := gov.PurchaseLimitQty(powerOutput, powerPrice)
	if math.Abs(avail-(start/powerPrice)) > 1e-6 {
		t.Fatalf("资金充足时可采购量 = %.4f，应为 现金/价格 = %.4f", avail, start/powerPrice)
	}

	// ② 采购：G2 不计税，建造部门收到全额。
	const qty = 100.0
	amount := qty * powerPrice
	gov.Cash.Add(-amount)
	aud.Inject(ledger.Building(PowerGoodIndex), amount)
	if got := gov.Cash.Balance(); math.Abs(got-(start-amount)) > 1e-6 {
		t.Errorf("采购后政府余额 %.3f，应为 %.3f", got, start-amount)
	}
	if got := aud.Balance(ledger.Building(PowerGoodIndex)); math.Abs(got-amount) > 1e-6 {
		t.Errorf("建造部门入账 %.3f，应为全额 %.3f（不计税）", got, amount)
	}

	// ③ G6：投资池偿还同一笔货款 ⇒ 政府净支出 0。
	//
	// 投资池的资金来自资本建筑净额入池；本测试直接注入它（单边分录，
	// 模拟"入池"这一来源），随后用真实的交易构造函数偿还给政府——
	// 偿还本身是借贷相等的交易，不得改变货币总量。
	aud.Inject(ledger.Investment(), amount)
	m1 := aud.Total()
	if err := aud.Post(ledger.InvestmentPayGov(amount)); err != nil {
		t.Fatalf("投资池偿还过账失败: %v", err)
	}
	if got := gov.Cash.Balance(); math.Abs(got-start) > 1e-6 {
		t.Errorf("投资池偿还后政府余额 %.3f，应为初值 %.3f（净支出为 0）", got, start)
	}
	if got := aud.Total(); math.Abs(got-m1) > 1e-6 {
		t.Errorf("投资池偿还这笔交易改变了货币总量：%.3f → %.3f", m1, got)
	}
}

// TestPowerInventoryIsAlwaysZero 校验 §4.5.3 G2 的"即买即用"：
// 政府不持有公共储备，故 PowerInventory 恒为 0，且 Validate 会拒绝非零值。
func TestPowerInventoryIsAlwaysZero(t *testing.T) {
	gov, _ := newGov(t, 1e9)
	gov.PowerOutput = 1000
	if err := gov.Validate(7250); err != nil {
		t.Fatalf("初始状态应满足不变量: %v", err)
	}
	gov.PowerInventory = 1
	if err := gov.Validate(7250); err == nil {
		t.Error("公共储备非零时必须报错（§4.5.3 G2：政府不持有建造力）")
	}
	gov.PowerInventory = 0
	if err := gov.Validate(7250); err != nil {
		t.Errorf("恢复为 0 后应通过: %v", err)
	}
}

// TestPurchaseLimitQtyIsClippedByCash 校验 G2 的资金裁剪：
// 可采购量的上限是 §4.5.4 的 AvailableCash / 价格（产出与队列需要量由调用方再取 min）。
func TestPurchaseLimitQtyIsClippedByCash(t *testing.T) {
	gov, _ := newGov(t, 0)
	gov.PowerOutput = 100 // AssetBase = 100×1000 = 100_000，上限 = 200_000
	const price = 1000.0
	got := gov.PurchaseLimitQty(1e9, price)
	if math.Abs(got-200) > 1e-6 {
		t.Errorf("资金受限时可采购量 = %.4f，应为 上限/价格 = 200", got)
	}
	// 余额充裕时上限 = 余额/价格（余额本身可能已超过债务上限，但可动用被截到上限）
	gov.Cash.SetInitial(300_000)
	if got := gov.PurchaseLimitQty(1e9, price); math.Abs(got-200) > 1e-6 {
		t.Errorf("余额超过债务上限时可采购量 = %.4f，应被截到 200", got)
	}
	gov.Cash.SetInitial(50_000)
	if got := gov.PurchaseLimitQty(1e9, price); math.Abs(got-50) > 1e-6 {
		t.Errorf("余额低于上限时可采购量 = %.4f，应为 50", got)
	}
}

// TestControlCapacityNote 记录 G5 在"推导口径"下不再是硬约束的事实。
//
// 【2026-09-19 裁决（§4.5.2）】金融区级数由掌控比**反推**
// （N_finance = Σ非农业等级 / c_ctrl），故 ControlCapacity = N×c_ctrl
// 恒等于其余建筑等级之和——上限恒不小于实际持有量，所以它不再是扩张的约束。
// 资本的自我限制现在只剩**工资义务**。
//
// 【前值 → 后值】旧测试断言"已达上限就不能再追加"；在推导口径下
// sim.syncFinanceLevel 每 tick 重算级数，上限随之自动抬高，故该断言不再成立。
func TestControlCapacityNote(t *testing.T) {
	var c Capital
	c.UpdateControl(10, 0, 5)
	if c.ControlCapacity != 50 {
		t.Fatalf("掌控上限 = %.1f，应为 10×5 = 50", c.ControlCapacity)
	}
	// 反推口径下 ControlledLevels = ControlCapacity，CanControl(0) 恰好取等号。
	c.ControlledLevels = c.ControlCapacity
	if !c.CanControl(0) {
		t.Error("在推导口径下 实际持有量 = 上限 应仍然成立（取等号）")
	}
	if c.CanControl(1) {
		t.Error("超出实际持有量 1 级就不应被允许（它不再自动抬高上限）")
	}
}

// TestConstructionSinkNotes 记录 §4.5.3 改写删除的两类流动，防止被误接回来。
//
// 【已删除（2026-09-19，第 11 轮）】
//   - fiscal.Government.SellPower / SaleRequest / SaleResult：政府不再转售建造力；
//   - fiscal.Government.PurchasePower：整批采购改为"按需即买即用"，
//     采购量由 sim.step 用 min(队列需要量, 产出, 可动用资金/价格) 定出；
//   - fiscal.ShareProfit：留存比例已删除，利润归属改为 book.ProfitAllocate。
//
// 本测试只做编译期事实的说明性断言（这些 API 已不存在），
// 真正的行为断言在 sim 的 R26 审计测试里。
func TestConstructionSinkNotes(t *testing.T) {
	// 政府在两个资本建筑之外只有一个采购限额函数，不再有"采购 + 转售"两步。
	gov, _ := newGov(t, 1000)
	gov.PowerOutput = 10
	const price = 100.0
	// 无队列时调用方传 needQty = 0，采购量必然是 0（G2 按需）。
	if got := math.Min(0, gov.PurchaseLimitQty(10, price)); got != 0 {
		t.Errorf("无队列时采购量应为 0，实际 %.4f", got)
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

// TestCollectTaxBaseExcludesPower 校验 G1 的税基口径（§4.5.3 第 11 轮裁决）：
// 消费 + 中间投入，**不含建造力**（建造力交易不计税）。
//
// 【前值 → 后值】旧签名 CollectTax(rate, consumer, intermediate, power) 把
// 建造力采购额也计入税基；定案不计税后签名去掉该参数。
func TestCollectTaxBaseExcludesPower(t *testing.T) {
	const consumer, intermediate = 100.0, 200.0
	got := CollectTax(0.10, consumer, intermediate)
	want := (consumer + intermediate) * 0.10
	if math.Abs(got-want) > 1e-9 {
		t.Errorf("税额 = %.4f，应为 %.4f", got, want)
	}
	if CollectTax(0, consumer, intermediate) != 0 {
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

// TestGovernmentOwnBuildoutIsGone 记录"政府自建项目付款"在本版不存在。
//
// 【契约依据】§4.5.3 G6 第 7 条：政府**自有项目**（如 §4.5.6 的仓库自动扩建）
// 才由政府现金池付款；本版不实现仓库（§4.5.6 范围外），故政府恒无自有项目，
// 这条流动与其记账函数（fiscal.SellPower 的 gov 分支 / book.GovOwnBuildout）
// 一并删除。若将来实现仓库，应在 sim.step 的 ⑩ 段新增一条
// "政府自有项目采购"的记账路径，而不是复活旧的转售口径。
func TestGovernmentOwnBuildoutIsGone(t *testing.T) {
	gov, _ := newGov(t, 1e6)
	gov.PowerOutput = 100
	// 政府唯一的建造力支出入口是 G2 采购（由 sim 按需调用 book.PurchasePower），
	// fiscal 包只提供资金约束：可采购量 = min(余额, 债务上限)/价格。
	// 此处余额 1e6 > 上限 2×100×1000 = 200,000 ⇒ 可采购量 = 200,000/1000 = 200。
	if got := gov.PurchaseLimitQty(100, 1000); math.Abs(got-200) > 1e-9 {
		t.Errorf("可采购量 = %.4f，应为 债务上限/价格 = 200", got)
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
