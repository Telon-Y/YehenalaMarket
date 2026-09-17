package book

// flow.go —— 1.0 契约【全部资金流动】的唯一实现处。
//
// 本文件与 book.go 合起来构成"离散记账的集中地"：sim.step 不再自己
// 读写任何余额，只调用这里的方法。每类流动一个方法，借贷两侧写在同一处。
//
// 覆盖清单（对应契约条文）：
//
//	① PayWages          工资：建筑 → 人群（按阶级）        §5 / §5.1
//	② Consume           消费：人群 → 卖方建筑 + 政府税     §6 / §4.5.3
//	③ PayIntermediate   中间投入：买方建筑 → 卖方 + 政府税  §3.3 / §4.5.3
//	④ PurchasePower     政府采购建造力：政府 → 建造力部门   §4.5.3 G2
//	⑤ SellPowerExternal 售力给外部付款方：付款方 → 政府     §4.5.3 G6
//	⑥ GovOwnBuildout    政府自建付款：政府 → 建造力部门     §4.5.3 G6
//	⑦ ProfitSplit       利润划分（book.go）                §4.5.1
//	⑧ NewCapital        新建营运本金（book.go，唯一注入）   §4.3

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
	for site, amount := range bySite {
		t.Debit(ledger.Building(site), amount)
	}
	for _, l := range legs {
		if l.Amount > 0 {
			t.Credit(b.House(l.Site, l.Class), l.Amount)
		}
	}
	b.mustPost(t)
	return total
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
// 买方现金不足时按可用资金等比缩减（这是"上游恒亏"能真正约束住生产链的机制）。
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
		if avail := b.BalBld(l.Buyer); gross > avail {
			gross = avail // 买不起就按可用资金缩减
		}
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

// ===== ④ 政府采购建造力 =====

// PurchasePowerResult 是一次政府采购的结果。
type PurchasePowerResult struct {
	// Qty 是实际采购的建造力单位数。
	Qty float64
	// Net 是建造力部门实收的净额。
	Net float64
	// Tax 是政府自己收自己的税额。
	Tax float64
	// Gross 是政府池的扣款总额（= Net + Tax）。
	Gross float64
}

// PurchasePower 过账政府采购建造力（§4.5.3 G2）。
//
//	借 政府        Gross
//	贷 建筑[建造力] Net
//	贷 政府        Tax
//
// 贷方中的 Tax 是"政府自己收自己"：资金不离开政府池，
// 故政府对这笔交易的净支出恰为 Net。显式写出这笔（而不是省掉）
// 才能让借贷相等——此前反复出错正是省掉它的结果
// （省掉后政府净减 Gross 而建造部门只收 Net，差 Net·t）。
//
// qty 由调用方按 AvailableCash 裁剪后传入（那是资金约束，不是记账）。
func (b *Book) PurchasePower(qty, price, taxRate float64) PurchasePowerResult {
	if qty <= 0 || price <= 0 {
		return PurchasePowerResult{}
	}
	gross := qty * price
	net := gross / (1 + taxRate)
	tax := gross - net
	t := &ledger.Txn{Name: "政府采购建造力"}
	t.Debit(ledger.Gov(), gross)
	t.Credit(ledger.Building(b.PowerIdx), net)
	t.Credit(ledger.Gov(), tax)
	b.mustPost(t)
	return PurchasePowerResult{Qty: qty, Net: net, Tax: tax, Gross: gross}
}

// ===== ⑤ 售力给外部付款方 =====

// SellPowerExternal 过账政府向外部付款方（建筑或资本）出售建造力。
//
//	借 付款方  Gross
//	贷 政府    Net + Tax
//
// 付款方余额不足时按可用资金等比缩减，返回实际成交的净额。
// payerIsCapital 决定借方是资本池还是某类建筑池。
func (b *Book) SellPowerExternal(payerIsCapital bool, payerBuilding int, wantNet, price, taxRate float64) (grantedNet, tax float64) {
	if wantNet <= 0 || price <= 0 {
		return 0, 0
	}
	avail := b.BalCapital()
	debit := ledger.Capital()
	if !payerIsCapital {
		avail = b.BalBld(payerBuilding)
		debit = ledger.Building(payerBuilding)
	}
	gross := wantNet * (1 + taxRate)
	if gross > avail {
		gross = avail
	}
	if gross <= 0 {
		return 0, 0
	}
	net := gross / (1 + taxRate)
	tax = net * taxRate
	t := &ledger.Txn{Name: "政府售力"}
	t.Debit(debit, gross)
	t.Credit(ledger.Gov(), gross)
	b.mustPost(t)
	return net, tax
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
	if wantUnits <= 0 || unitPrice <= 0 {
		return PrivatizeResult{}
	}
	avail := b.BalCapital()
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
	t.Debit(ledger.Capital(), gross)
	t.Credit(ledger.Gov(), gross)
	b.mustPost(t)
	return PrivatizeResult{Units: units, Paid: gross}
}

// ===== ⑥ 政府自建项目付款 =====

// GovOwnBuildout 过账政府为自有项目支付的建造力货款。
//
//	借 政府        Gross
//	贷 建筑[建造力] Net
//	贷 政府        Tax
//
// 【口径】wantNet 必须是【净额】（grant × salePrice），本方法内部再乘 (1+t)
// 得含税总额。若调用方把含税总额当净额传进来，税会被算两遍——
// 实测残差恰等于自建税额（2217.22 = 22172.20×10%）。
//
// availableCash > 0 时按它裁剪含税总额（资金约束）；传 0 表示不裁剪。
//
// 返回实际支付的净额与含税总额。
func (b *Book) GovOwnBuildout(wantNet, price, taxRate, availableCash float64) (net, gross float64) {
	if wantNet <= 0 || price <= 0 {
		return 0, 0
	}
	gross = wantNet * (1 + taxRate)
	if availableCash > 0 && gross > availableCash {
		gross = availableCash
	}
	if gross <= 0 {
		return 0, 0
	}
	net = gross / (1 + taxRate)
	tax := net * taxRate
	t := &ledger.Txn{Name: "政府自建付款"}
	t.Debit(ledger.Gov(), gross)
	t.Credit(ledger.Building(b.PowerIdx), net)
	t.Credit(ledger.Gov(), tax)
	b.mustPost(t)
	return net, gross
}
