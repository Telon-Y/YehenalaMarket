package book

// flow.go —— 1.0 契约【全部资金流动】的唯一实现处。
//
// 本文件与 book.go 合起来构成"离散记账的集中地"：sim.step 不再自己
// 读写任何余额，只调用这里的方法。每类流动一个方法，借贷两侧写在同一处。
//
// 覆盖清单（对应契约条文）：
//
//	① PayWages          工资：建筑 → 人群（按阶级）        §5 / §5.1
//	                     金融区场地取 ledger.Capital()（§4.5.1b 的营运现金池口径）
//	② Consume           消费：人群 → 卖方建筑 + 政府税     §6 / §4.5.3
//	③ PayIntermediate   中间投入：买方建筑 → 卖方 + 政府税  §3.3 / §4.5.3
//	④ PurchasePower     政府采购建造力：政府 → 建造力部门   §4.5.3 G2（按需、不计税）
//	⑤ InvestmentInflow  资本建筑净额入投资池：庄园/金融区   §4.5.1b
//	⑥ InvestmentPayGov  投资池偿还政府：投资池 → 政府       §4.5.3 G6
//	⑦ PaySubsidy        政府补贴：政府 → 建筑              §4.5.7
//	⑧ Privatize         股权转让：资本池 → 政府             §4.5.1a
//	⑨ ProfitAllocate    利润归属（book.go）                §4.5.1
//	⑩ NewCapital        新建营运本金（book.go，唯一注入）   §4.3
//	⑪ PayWelfare        政府福利金（转移支付）：政府 → 人群   §4.5.8（第 15 轮裁决）
//	⑫ SaveToInvestment  居民储蓄入投资池：人群 → 投资池      §5.3（第 15 轮裁决）
//
// 【2026-09-19 裁决删除的两类流动】SellPowerExternal（政府转售建造力）与
// GovOwnBuildout（政府自建付款）本版**不再存在**：G6 改为"投资池全额付给政府"，
// 储备层与两个自反交易同时消失。
//
// 【公共工程（§4.5.8）不新增交易类型】政府为开发类建筑采购建造力，
// 与 G2 的政府采购共用 PurchasePower（借政府 / 贷建造部门）；区别只在
// **订单所有权**：公共工程新完工的等级归政府（GovLevel），
// 且**没有**投资池偿还（政府净支出 > 0，这是它的支出端）。

import (
	"yehenala/market/internal/ledger"
)

// ===== ① 工资 =====

// WageLeg 是一个建筑对某阶级的工资支出。
type WageLeg struct {
	Site  int
	Class int
	// Population 是该阶级本期覆盖的人口（流量登记，不参与记账）。
	Population float64
	// Amount 是该阶级本期工资额。
	Amount float64
}

// PayWages 过账一批工资。
//
//	借 建筑[site]      该场地工资合计（各阶级之和）
//	贷 人群[site][c]   各阶级份额
//
// 【金融区的场地账户（§4.5.1b）】金融区是资本建筑，其营运现金池就是
// ledger.Capital()（§4.3 的"资本现金池"），故 site == FinanceIdx 时借方取
// 资本池而不是 Building[FinanceIdx]。宅邸庄园与其他生产建筑都取自己的
// ledger.Building(site)。
//
// 借贷两侧由【同一个循环】从同一份 WageLeg 列表累加，
// 因此"建筑池扣了多少、人群池收了多少"必然相等——不存在漏记一半的可能。
//
// 返回实际过账的工资总额。
func (b *Book) PayWages(legs []WageLeg) float64 {
	t := &ledger.Txn{Name: "工资"}
	bySite := make(map[int]float64, len(legs))
	var total float64
	for _, l := range legs {
		if l.Amount <= 0 {
			continue
		}
		bySite[l.Site] += l.Amount
		total += l.Amount
	}
	if total <= 0 {
		return 0
	}
	// 【R72 已登记但**未修**】这里遍历 `bySite` map 发出借方 ⇒ 分录顺序随机。
	// 曾尝试改为"按首次出现顺序"（确定性），但实测**并未消除**非确定性
	//（仍有 ~2.4e-07 分歧）——因为随机序来源不止这一处：**每一笔**交易的
	// `Post` 都会把浮点残差并入"最大的一笔分录"，而残差合并本身受余额的
	// 最后一位影响。要根治须在账本层统一处理，属独立工程。
	// 故此处回滚为原样，保持基线不受无谓扰动；完整登记见 ACTIVE §七 R72。
	for site, amount := range bySite {
		t.Debit(b.sitePayer(site), amount)
	}
	for _, l := range legs {
		if l.Amount > 0 {
			t.Credit(b.House(l.Site, l.Class), l.Amount)
		}
	}
	b.mustPost(t)
	return total
}

// sitePayer 返回某个劳动场地的**营运现金池**账户（§4.5.1b）。
//
//	金融区（资本建筑） → ledger.Capital()
//	其余场地           → ledger.Building(site)
func (b *Book) sitePayer(site int) ledger.Account {
	if site == b.FinanceIdx {
		return ledger.Capital()
	}
	return ledger.Building(site)
}

// ===== ② 消费 =====

// ConsumerLeg 是一个人群池的一次消费。
type ConsumerLeg struct {
	// Pool 是该池的账户。
	Pool ledger.Account
	// Net 是税前购买额；Gross = Net·(1+t) 从该池扣除。
	Net float64
}

// Consume 过账一批消费。
//
//	借 人群池[pi]     Gross = Net·(1+t)
//	贷 建筑[卖方]     按 sellerShare 分摊 Net
//	贷 政府           Σ Tax
//
// sellerShare 是【全部池共享】的卖方占比（按各建筑卖出商品的购买量×价格计算），
// 与池无关；用累计残差把浮点误差归给最后一家，保证 Σ贷方 = Σ借方（逐位相等）。
//
// 返回实际的税前消费总额、税额总额、以及【每个卖方账户实际收到的净额】。
//
// 【为什么要把逐卖方金额返回来】这笔金额是"记账事实"，只能由记账处给出。
// 调用方若自己按占比重算一遍，两处的残差归集规则一旦不同（一个归最后一家、
// 一个四舍五入），逐建筑对账就会长期挂着一个假的残差——而全局守恒审计仍然通过，
// 于是缺陷会被"全局不变量全绿"掩盖。实测正出现过这个情形：逐建筑对账残差 213 万
// 而货币守恒与借贷相等全部通过。
func (b *Book) Consume(legs []ConsumerLeg, taxRate float64, sellers []ledger.Account, sellerShare []float64) (spendNet, taxTotal float64, credited map[ledger.Account]float64) {
	credited = make(map[ledger.Account]float64, len(sellers))
	t := &ledger.Txn{Name: "消费"}
	var grossTotal float64
	for _, l := range legs {
		if l.Net <= 0 {
			continue
		}
		tax := l.Net * taxRate
		t.Debit(l.Pool, l.Net+tax)
		spendNet += l.Net
		taxTotal += tax
		grossTotal += l.Net + tax
	}
	if spendNet <= 0 {
		return 0, 0, credited
	}
	// 卖方净额：按占比分摊，残差归最后一家
	var allocated float64
	for k, acc := range sellers {
		var amount float64
		if k == len(sellers)-1 {
			amount = spendNet - allocated
		} else {
			amount = spendNet * sellerShare[k]
			allocated += amount
		}
		if amount != 0 {
			t.Credit(acc, amount)
			credited[acc] += amount
		}
	}
	t.Credit(ledger.Gov(), taxTotal)
	b.mustPost(t)
	return spendNet, taxTotal, credited
}

// ===== ③ 中间投入 =====

// IntermediateLeg 是一个建筑本期申报的中间投入付款。
type IntermediateLeg struct {
	Buyer int
	// Need 是税前应付额（按当期价格 × 实际取用量）。
	Need float64
}

// PayIntermediate 过账全部中间投入交易，并返回每家实际成交额。
//
//	借 建筑[买方]  Gross = Net·(1+t)
//	贷 建筑[卖方]  Net          （同一商品的生产者）
//	贷 政府        Tax
//
// 【2026-09-19 裁决】不再按买方可用资金缩减：§4.3 的"资金不足时停工/等比缩减"
// 已删除，建筑现金池允许透支（与政府池一致），故此处恒按申报额成交。
// 透支额由逐建筑对账与诊断输出反映。
//
// 商品→卖方的映射由 sellerOf 提供（同一商品只有一类建筑生产，§3.3 一一对应）。
// 无法归属的卖方（理论上不存在）会把净额记入政府，以保持借贷相等。
//
// 返回：实际成交的税前总额、税额总额、每家建筑的实际付款额，
// 以及【每个卖方建筑实际收到的净额】（同样是"记账事实"，供逐建筑对账使用，
// 避免调用方按自己的口径重算而产生假残差；理由同 Consume）。
func (b *Book) PayIntermediate(
	legs []IntermediateLeg,
	taxRate float64,
	sellerOf func(buyer int) (int, bool),
) (netTotal, taxTotal float64, paid, income map[int]float64) {
	paid = make(map[int]float64, len(legs))
	income = make(map[int]float64, len(legs))
	t := &ledger.Txn{Name: "中间投入"}
	for _, l := range legs {
		if l.Need <= 0 {
			continue
		}
		gross := l.Need * (1 + taxRate)
		if gross <= 0 {
			continue
		}
		net := gross / (1 + taxRate)
		tax := net * taxRate
		if net <= 1e-12 {
			continue
		}
		t.Debit(ledger.Building(l.Buyer), gross)
		if seller, ok := sellerOf(l.Buyer); ok && seller >= 0 {
			t.Credit(ledger.Building(seller), net)
			income[seller] += net
		} else {
			t.Credit(ledger.Gov(), net)
		}
		t.Credit(ledger.Gov(), tax)
		netTotal += net
		taxTotal += tax
		paid[l.Buyer] = gross
	}
	if netTotal <= 0 {
		return 0, 0, paid, income
	}
	b.mustPost(t)
	return netTotal, taxTotal, paid, income
}

// ===== ⑩ 政府补贴（§4.5.7，2026-09-19 新增）=====

// PaySubsidy 过账一笔政府补贴（转移支付）。
//
//	借 政府        补贴额
//	贷 建筑[i]     补贴额
//
// 【口径】补贴**不是利润**：它不参与 §4.5.1 的三份额划分、也不计入 §7 的 GDP；
// 它只把政府的钱转到受补贴建筑的现金池，使该建筑在满编口径下不再显示亏损，
// 从而让 §5.2 的增雇分支生效（该建筑主动追求满员）。
//
// 补贴额由调用方按"缺口"与"可动用资金"裁剪后传入——那是资金约束，不是记账。
func (b *Book) PaySubsidy(i int, amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	t := &ledger.Txn{Name: "政府补贴"}
	t.Debit(ledger.Gov(), amount)
	t.Credit(ledger.Building(i), amount)
	b.mustPost(t)
	return amount
}

// ===== ④ 政府采购建造力（§4.5.3 G2，按需即买即用、不计税）=====
// PurchasePowerResult 是一次政府采购的结果。
type PurchasePowerResult struct {
	// Qty 是实际采购的建造力单位数。
	Qty float64
	// Amount 是政府池的扣款额，也是建造力部门实收额（净额 = 含税额）。
	Amount float64
}

// PurchasePower 过账政府采购建造力（§4.5.3 G2）。
//
//	借 政府         amount = qty × price
//	贷 建筑[建造力] amount
//
// 【2026-09-19 改写】**不再整批采购、不再计税**：
//   - 采购量由调用方按"队列本 tick 的实际需要量"决定，再受当期产出与
//     §4.5.4 可动用资金裁剪（那是资金约束，不是记账）；无队列时传 0；
//   - 建造力交易不计税（政府既是唯一买家又是税收收款人，计税只会原地回冲），
//     故这里没有"政府自己收自己"的自反税腿。
//
// "即买即用"由调用方保证：本方法只负责把 qty×price 从政府池划到建造部门。
func (b *Book) PurchasePower(qty, price float64) PurchasePowerResult {
	if qty <= 0 || price <= 0 {
		return PurchasePowerResult{}
	}
	amount := qty * price
	b.mustPost(ledger.PowerPurchase(b.PowerIdx, amount))
	return PurchasePowerResult{Qty: qty, Amount: amount}
}

// ===== ⑤ 资本建筑净额入投资池（§4.5.1b）=====

// InvestmentInflow 把资本建筑扣除自身工资后的净额转入投资池。
//
//	借 来源（资本建筑账户）  amount
//	贷 投资池                amount
//
// 来源由调用方按 §4.5.1b 指定：庄园 → ledger.Building(ManorIndex)，
// 金融区 → ledger.Capital()。入池额必须由调用方取 max(0, 净额)——
// 负净额由资本建筑自己的现金池承担，**不倒抽投资池**（投资池不得透支）。
func (b *Book) InvestmentInflow(source ledger.Account, amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.InvestmentInflow(source, amount))
	return amount
}

// ===== ⑥ 投资池偿还政府（§4.5.3 G6）=====

// InvestmentPayGov 过账投资池向政府偿还的建造力货款。
//
//	借 投资池   amount
//	贷 政府     amount
//
// G6 不再是"政府把储备卖给扩建方"：私人扩建所消耗的建造力，由投资池按两条
// 投资栈的贡献比例**全额**付给政府。政府先按 G2 把同一笔钱付给建造部门，
// 再收这笔偿还，故政府的建造力净支出为 0，且不存在自反税腿。
func (b *Book) InvestmentPayGov(amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.InvestmentPayGov(amount))
	return amount
}

// ===== ⑨ 私有化：股权转让 =====

// PrivatizeResult 是一次私有化的结果。
type PrivatizeResult struct {
	// Units 是转让的等级数（股权腿）。
	Units float64
	// Paid 是资本池支付的对价（含税）。
	Paid float64
}

// Privatize 把政府持有的 level 级股权转让给私人部门。
//
//	借 资本池   对价（含税）
//	贷 政府     对价（含税）
//
// 所有权变更（GovLevel → PrivLevel）由调用方在同一处同步执行——
// 本方法只负责资金腿，且保证借贷相等。
//
// wantUnits 是拟转让等级数；unitPrice 是每级对价。
// 资本池余额不足时按可用资金等比缩减，故"私有化受资本实力约束"。
//
// 返回实际成交的等级数与对价。
func (b *Book) Privatize(wantUnits, unitPrice, taxRate float64) PrivatizeResult {
	return b.privatizeFrom(ledger.Capital(), wantUnits, unitPrice, taxRate)
}

// PrivatizeFromInvestment 与 Privatize 同，但**出资方是投资池**（§4.5.1a 第 28 轮）。
//
//	借 投资池   对价（含税）
//	贷 政府     对价（含税）
//
// 【为什么需要它】用户裁决"允许收购用投资池余额出资"：默认参数下资本池长期为负
//（实测 −3.38e9）⇒ `Privatize` 的 `avail = BalCapital() ≤ 0` ⇒ 收购**零成交**；
// 而投资池里闲置着居民储蓄（实测 3.0e9）。本方法让"可动用于投资的资金"也能用于
// **买存量股权**，而不只是新建产能。
//
// 【所有权不变】股权腿仍归资本建筑（金融区 / 宅邸庄园）——投资池不是所有权主体。
func (b *Book) PrivatizeFromInvestment(wantUnits, unitPrice, taxRate float64) PrivatizeResult {
	return b.privatizeFrom(ledger.Investment(), wantUnits, unitPrice, taxRate)
}

// privatizeFrom 是私有化资金腿的**唯一实现**：付款方由 payer 决定。
//
// 【为什么合并成一个实现】两条腿的差别只有借方账户，其余（可用额裁剪、含税口径、
// 按可用资金等比缩减）必须逐字相同——否则"用资本池买"与"用投资池买"会算出
// 不同的成交规则，正是本项目反复记录的"两处口径分叉"。
func (b *Book) privatizeFrom(payer ledger.Account, wantUnits, unitPrice, taxRate float64) PrivatizeResult {
	if wantUnits <= 0 || unitPrice <= 0 {
		return PrivatizeResult{}
	}
	avail := b.Aud.Balance(payer)
	if avail <= 0 {
		return PrivatizeResult{}
	}
	gross := wantUnits * unitPrice * (1 + taxRate)
	if gross > avail {
		gross = avail
	}
	if gross <= 0 {
		return PrivatizeResult{}
	}
	units := gross / (unitPrice * (1 + taxRate))
	t := &ledger.Txn{Name: "股权转让（私有化）"}
	t.Debit(payer, gross)
	t.Credit(ledger.Gov(), gross)
	b.mustPost(t)
	return PrivatizeResult{Units: units, Paid: gross}
}

// ===== ⑬ 仓库：入库 / 出库 / 代理透传（§4.5.6，2026-09-19 第 16 轮）=====

// DepositLeg 是一个生产者本 tick 从【入库】中应得的货款（净额，不含增值税）。
type DepositLeg struct {
	// Producer 是收款的生产建筑下标。
	Producer int
	// Net 是该生产者按供给份额分得的货款（= 其过库量 × 生产者售价）。
	Net float64
}

// DepositToWarehouse 过账"生产者 → 仓库"的入库（§4.5.6）。
//
//	借 仓库                Σ Net·(1+ν)
//	贷 建筑[生产者]        各 Net
//	贷 政府                ν·Σ Net
//
// 返回实际过账的**净额合计**（生产者实收总额）与增值税额。
//
// 【口径】净额按**供给份额**分摊：某一商品的过库量由专业生产者与自给农场
// （其所有者是宅邸庄园）共同供给，入库款因此按当期实际供给量拆分。
// 这同时关闭了 R21 记录的"中间投入那一路归属未接"的缺口——
// 中间投入的货款不再全额记到"该商品的唯一专业生产者"账上。
func (b *Book) DepositToWarehouse(legs []DepositLeg, vatRate float64) (net, tax float64) {
	credits := make([]ledger.Entry, 0, len(legs))
	for _, l := range legs {
		if l.Net <= 0 {
			continue
		}
		credits = append(credits, ledger.Entry{Account: ledger.Building(l.Producer), Amount: l.Net})
		net += l.Net
	}
	if net <= 0 {
		return 0, 0
	}
	tax = net * vatRate
	b.mustPost(ledger.DepositGoods(ledger.Building(b.WarehouseIdx), net+tax, credits))
	return net, tax
}

// WithdrawFromWarehouse 过账一笔"仓库 → 买家"的出库（§4.5.6）。
//
//	借 买家账户            base·(1+τ)
//	贷 卖方账户            base（仓库；居民消费时先由消费代理收取，再透传给仓库）
//	贷 政府                τ·base
//
// 返回实收的 base（= 含仓库加价、不含消费税的货值）与消费税额。
// 买方为生产建筑时卖方是仓库；买方为居民池时卖方是**消费代理**（契约的两段路径）。
func (b *Book) WithdrawFromWarehouse(buyer ledger.Account, base, tau float64) (got, tax float64) {
	return b.withdraw(buyer, ledger.Building(b.WarehouseIdx), base, tau)
}

// WithdrawViaAgent 过账一笔"仓库 → 消费代理 → 居民池"的出库（§4.5.6）。
//
// 与 WithdrawFromWarehouse 同式，只是卖方换成**消费代理**：
// 居民把钱付给代理，代理随后逐池付给仓库（AgentPassThrough），余额因此恒为 0。
func (b *Book) WithdrawViaAgent(pool ledger.Account, base, tau float64) (got, tax float64) {
	return b.withdraw(pool, ledger.Building(b.AgentIdx), base, tau)
}

func (b *Book) withdraw(buyer, seller ledger.Account, base, tau float64) (got, tax float64) {
	if base <= 0 {
		return 0, 0
	}
	b.mustPost(ledger.WithdrawGoods(buyer, seller, base, tau))
	return base, base * tau
}

// AgentPassThrough 把消费代理收到的货款原额透传给仓库（§4.5.6）。
//
//	借 消费代理            amount
//	贷 仓库                amount
//
// 代理的余额因此逐 tick 归零（审计断言 AgentCash ≡ 0）。
func (b *Book) AgentPassThrough(amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.AgentPassThrough(ledger.Building(b.AgentIdx), ledger.Building(b.WarehouseIdx), amount))
	return amount
}

// ===== ⑪ 福利金（转移支付，§4.5.8）=====

// WelfareLeg 是一笔对某人群池的福利金。
type WelfareLeg struct {
	// Pool 是收款的人群池账户。
	Pool ledger.Account
	// Amount 是该池本期补贴额（≤ 政府可动用资金）。
	Amount float64
}

// PayWelfare 过账一批政府福利金（§4.5.8，2026-09-19 第 15 轮裁决）。
//
//	借 政府             Σ Amount
//	贷 人群池[p]        各池 Amount
//
// 【转移支付口径】福利金不计入 GDP、不参与利润划分、不改变货币总量；
// 它只改变**分配**：政府池减少、居民池增加。发放额由调用方按
// "档位 × (平均工资标准 − 本人工资)" 算出，并受 §4.5.4 可动用资金约束。
//
// 返回实际过账的总额。
func (b *Book) PayWelfare(legs []WelfareLeg) float64 {
	var credits []ledger.Entry
	var total float64
	for _, l := range legs {
		if l.Amount <= 0 {
			continue
		}
		credits = append(credits, ledger.Entry{Account: l.Pool, Amount: l.Amount})
		total += l.Amount
	}
	if total <= 0 {
		return 0
	}
	b.mustPost(ledger.Welfare(total, credits))
	return total
}

// SavingsToBank 把储蓄固定账户的余额转入**储蓄银行**（1.2 M4.3，2026-09-20 第 40 轮）。
//
//	借 储蓄账户   amount     贷 储蓄银行   amount
//
// 【与 SavingsToInvestment 的区别】1.0 的第二步把劳动力结余**直接转入投资池**；
// 1.2 改为转入**储蓄银行**——因为裁决要求"**资本不再能使用劳动力储蓄购买**"：
// 钱一旦进了投资池，资本就能用它收购（第 28 轮的实现正是如此）。
// 转入储蓄银行后，这笔钱只能由储蓄银行**放贷**出去（见 SavingsBankLend）。
//
// 返回实际过账金额（受储蓄账户余额约束）。
func (b *Book) SavingsToBank(amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	bal := b.BalSavings()
	if amount > bal {
		amount = bal
	}
	if amount <= 0 {
		return 0
	}
	b.mustPost((&ledger.Txn{Name: "储蓄转入储蓄银行"}).
		Debit(ledger.Savings(), amount).
		Credit(ledger.SavingsBank(), amount))
	return amount
}

// ===== 1.2 M8：借贷台账的两条资金腿与利息分配（2026-09-20 第 40 轮）=====

// SavingsBankLend 过账"储蓄银行 → 投资池"的放贷腿（1.2 M8.3 的 ①）。
//
// 返回实际过账金额（受储蓄银行余额约束，不透支）。
func (b *Book) SavingsBankLend(amount float64) float64 {
	bal := b.Aud.Balance(ledger.SavingsBank())
	if amount > bal {
		amount = bal
	}
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.SavingsBankLend(amount))
	return amount
}

// DebtService 过账"金融区 → 储蓄银行"的还本息腿（1.2 M8.3 的 ③）。
//
// amount 由调用方按 M8.4.1 算好（= min(应付, 金融区净额)）；
// **付不出的部分不走账**（它滚入 Loan.Outstanding，见 M8.4.1）。
func (b *Book) DebtService(amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.DebtService(amount))
	return amount
}

// SavingsBankToLabor 把储蓄银行本期收到的利息**当期分配**给劳动力（1.2 M8.6 ③）。
//
// legs 的金额由调用方按人群池人口分摊算好（与 §5.1 同口径）。
// 返回实际过账总额。
func (b *Book) SavingsBankToLabor(legs []WelfareLeg) float64 {
	return b.payToLabor(ledger.SavingsBank(), legs, ledger.SavingsBankToLabor)
}

// PayLaborDividend 把**劳动力分红池**的余额派发给在职人口（1.2 M4.2）。//
// 与 `SavingsBankToLabor` 共用同一个 `payToLabor` 实现 —— 两条腿只在
// **借方账户**与**交易名**上不同（见 `ledger.LaborDividendToLabor` 的说明）。
func (b *Book) PayLaborDividend(legs []WelfareLeg) float64 {
	return b.payToLabor(ledger.LaborDividend(), legs, ledger.LaborDividendToLabor)
}

// payToLabor 是"统一入口 → 人群池"派发的**共用实现**（1.2 M4.2 / M8.6）。
//
//	借 source     Σ金额
//	贷 人群池[p]   各池金额
//
// 【按比例裁剪的纪律】入口余额不足时**同乘一个 scale**，而不是"先到先得"——
// 与福利金同一纪律（`PayWelfare`），保证同样条件下的池之间不产生人为差异。
func (b *Book) payToLabor(
	source ledger.Account,
	legs []WelfareLeg,
	mk func(total float64, credits []ledger.Entry) *ledger.Txn,
) float64 {
	var credits []ledger.Entry
	var total float64
	for _, l := range legs {
		if l.Amount <= 0 {
			continue
		}
		credits = append(credits, ledger.Entry{Account: l.Pool, Amount: l.Amount})
		total += l.Amount
	}
	if total <= 0 {
		return 0
	}
	bal := b.Aud.Balance(source)
	if total > bal {
		if bal <= 0 {
			return 0
		}
		scale := bal / total
		for i := range credits {
			credits[i].Amount *= scale
		}
		total = bal
	}
	if total <= 0 {
		return 0
	}
	b.mustPost(mk(total, credits))
	return total
}

// ===== ⑫ 居民储蓄：结余 → 固定账户 → 投资池（§5.3）=====

// SavingLeg 是一个人群池本期转入储蓄固定账户的金额。
type SavingLeg struct {
	// Pool 是储蓄的资金来源池。
	Pool ledger.Account
	// Amount 是本池消费结算后的**全部**结余（不是 σ×结余）。
	Amount float64
}

// SaveToInvestment 把各人群池的消费结余**全额**转入储蓄固定账户（§5.3 第一步）。
//
//	借 人群池[p]        各池结余
//	贷 储蓄账户         Σ 结余
//
// 【渠道定义】"工资结余 → 储蓄 → 投资"：消费结算后仍未花掉的钱先离开居民钱包，
// 进入具名的储蓄账户；下一步 SaveToInvestmentStep2 再按储蓄率转入投资池。
//
// 【第 20 轮前值 → 后值】旧口径一步到位：借人群池 σ×结余、贷投资池 σ×结余，
// 其余 (1−σ) 留在人群池。现在第一步搬**全部**结余，第二步按 σ 转投资——
// 这样"结余多少"在账本上始终读得出来（储蓄账户余额 = 本期结余 − 已转投资）。
//
// 返回实际过账的结余总额。
func (b *Book) SaveToInvestment(legs []SavingLeg) float64 {
	var debits []ledger.Entry
	var total float64
	for _, l := range legs {
		if l.Amount <= 0 {
			continue
		}
		debits = append(debits, ledger.Entry{Account: l.Pool, Amount: l.Amount})
		total += l.Amount
	}
	if total <= 0 {
		return 0
	}
	b.mustPost(ledger.Saving(debits, total))
	return total
}

// SavingsToInvestment 把储蓄固定账户的余额按储蓄率转入投资池（§5.3 第二步）。
//
//	借 储蓄账户   amount     贷 投资池   amount
//
// amount 由调用方给出（= σ_save × 储蓄账户余额，σ 默认 1.0 = 全额）。
// 返回实际过账金额。
func (b *Book) SavingsToInvestment(amount float64) float64 {
	if amount <= 0 {
		return 0
	}
	bal := b.BalSavings()
	if amount > bal {
		amount = bal
	}
	if amount <= 0 {
		return 0
	}
	b.mustPost(ledger.SavingsToInvestment(amount))
	return amount
}
