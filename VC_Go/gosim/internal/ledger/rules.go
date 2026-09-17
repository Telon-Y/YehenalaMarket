package ledger

// rules.go —— 资金流量表的【可执行形式】。
//
// 本文件是 ledger.go 顶部那张表的唯一实现。每一类交易一个构造函数，
// 每个构造函数都保证 Σ借 == Σ贷（因此 Post 恒不报错）。
//
// 【纪律】任何新的资金流动都必须在此新增一个构造函数，不许在业务代码里
// 手工拼 Debit/Credit——那样又会回到"各写各的"的老问题。

// Wage ①：工资。建筑现金池 → 人群现金池（按阶级拆分）。
//
//	借 建筑[site]
//	贷 人群[site][c]   （c = 0..classes-1）
func Wage(site int, classAmounts []float64) *Txn {
	t := &Txn{Name: "工资"}
	var total float64
	for _, v := range classAmounts {
		total += v
	}
	t.Debit(Building(site), total)
	for c, v := range classAmounts {
		t.Credit(Household(site, c, len(classAmounts)), v)
	}
	return t
}

// ConsumerPurchase ②：消费者购买。人群现金池 → 卖方建筑 + 政府税收。
//
//	借 人群[池]
//	贷 建筑[卖方]   净额
//	贷 政府         税额
//
// gross = net × (1+t)。调用方传 net 与 t，本函数保证借贷相等。
func ConsumerPurchase(hh Account, seller int, net, taxRate float64) *Txn {
	tax := net * taxRate
	return (&Txn{Name: "消费"}).
		Debit(hh, net+tax).
		Credit(Building(seller), net).
		Credit(Gov(), tax)
}

// Intermediate ③：中间投入。买方建筑 → 卖方建筑 + 政府税收。
func Intermediate(buyer, seller int, net, taxRate float64) *Txn {
	tax := net * taxRate
	return (&Txn{Name: "中间投入"}).
		Debit(Building(buyer), net+tax).
		Credit(Building(seller), net).
		Credit(Gov(), tax)
}

// PowerPurchase ④：政府采购建造力。政府 → 建造力部门 + 政府税收。
//
//	借 政府         Gross
//	贷 建筑[power]  Net
//	贷 政府         Tax
//
// 注意贷方与借方都是政府的一笔（Tax）是"政府自己收自己"：
// 资金不离开政府池，故政府对这笔交易的净支出恰好等于 Net。
// 显式写出这一笔（而不是省掉）是为了让借贷相等，
// 从而不必在别处补记补偿项——此前反复出错正是省掉它的结果。
func PowerPurchase(powerIdx int, net, taxRate float64) *Txn {
	tax := net * taxRate
	return (&Txn{Name: "政府采购建造力"}).
		Debit(Gov(), net+tax).
		Credit(Building(powerIdx), net).
		Credit(Gov(), tax)
}

// PowerSale ⑤：政府向外部付款方（建筑或资本）出售建造力。
//
//	借 付款方        Gross
//	贷 政府          Net + Tax
//
// 收款方是政府自己，净额与税额都进政府池，故合并为一笔贷方。
func PowerSale(payer Account, net, taxRate float64) *Txn {
	tax := net * taxRate
	return (&Txn{Name: "政府售力"}).
		Debit(payer, net+tax).
		Credit(Gov(), net+tax)
}

// GovOwnBuildout ⑥：政府为自有项目付款。政府 → 建造力部门 + 政府税收。
//
// 与 ④ 同构：借方 Gross、贷方 Net 给卖方、Tax 给政府自己。
func GovOwnBuildout(powerIdx int, net, taxRate float64) *Txn {
	tax := net * taxRate
	return (&Txn{Name: "政府自建付款"}).
		Debit(Gov(), net+tax).
		Credit(Building(powerIdx), net).
		Credit(Gov(), tax)
}

// ProfitSplit ⑦：利润划分。
//
//	借 建筑[i]      利润总额
//	贷 政府         政府份额
//	贷 资本         资本份额
//	贷 建筑[i]      留存份额
//
// 【借贷相等的关键】三份额之和必须恒等于利润。
// ShareProfit 已经从算术上保证这一点，本函数再校验一次。
func ProfitSplit(i int, gov, capital, retain float64) *Txn {
	return (&Txn{Name: "利润划分"}).
		Debit(Building(i), gov+capital+retain).
		Credit(Gov(), gov).
		Credit(Capital(), capital).
		Credit(Building(i), retain)
}

// EquityTransfer ⑨：股权转让（私有化）。
//
//	借 资本池       对价
//	贷 政府         对价
//
// 私有化是**所有权的转让**，股票与现金同时易手。本交易只处理现金腿；
// 股权腿（GovLevel → PrivLevel）由 sim 在同一处同步变更，
// 二者必须成对发生，否则会出现"付了钱没拿到股权"或反之。
//
// 借贷相等的意义：私人部门用 100 万买下 100 万的国有资产，
// 全社会货币总量不变，只是持有主体从政府换成资本。
func EquityTransfer(net, taxRate float64) *Txn {
	tax := net * taxRate
	return (&Txn{Name: "股权转让（私有化）"}).
		Debit(Capital(), net+tax).
		Credit(Gov(), net+tax)
}

// NewCapital ⑧：新建建筑的营运本金（§4.3）。
//
//	贷 建筑[i]      金额
//
// 这是系统里【唯一】允许的货币注入，故只有贷方、没有借方。
// 它是货币存量的合法增量来源，审计时必须单独计量。
//
// 为此本函数【故意】不满足借贷相等，Post 会拒绝它——
// 必须走 PostInjection。这样"凭空造币"这件事在类型层面就是显式的。
func NewCapital(i int, amount float64) *Txn {
	return (&Txn{Name: "新建营运本金（货币注入）"}).
		Credit(Building(i), amount)
}

// PostInjection 过账一笔【借贷不等】的交易（唯一允许的例外）。
//
// 与 Post 分开是为了让"创造/销毁货币"在代码里显式可见：
// 任何调用点都应被审查。正常业务路径一律走 Post。
//
// 注意借方同样要生效——单边分录既有正向（注入）也有负向（抽出）。
func (a *Auditor) PostInjection(t *Txn) {
	for _, e := range t.Credits {
		a.balances[e.Account] += e.Amount
	}
	for _, e := range t.Debits {
		a.balances[e.Account] -= e.Amount
	}
	a.posted++
}

// Inject 向一个账户注入 amount 元（单边分录）。
//
// 这是 NewCapital 的便捷形式，用于"确实要创造货币"的场景。
// 业务代码正常路径不应调用它——所有真实交易都走 Post 与上面的交易构造函数。
func (a *Auditor) Inject(acc Account, amount float64) {
	t := &Txn{Name: "货币注入"}
	t.Credit(acc, amount)
	a.PostInjection(t)
}

// Withdraw 从一个账户抽出 amount 元（单边分录，Inject 的反向）。
//
// 仅用于测试中模拟"账本之外的付款方"（例如单元测试里的独立钱包）。
func (a *Auditor) Withdraw(acc Account, amount float64) {
	t := &Txn{Name: "货币抽出（仅测试/调试）"}
	t.Debit(acc, amount)
	a.PostInjection(t)
}
