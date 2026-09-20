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

// PowerPurchase ④：政府采购建造力。政府 → 建造力部门。
//
//	借 政府         amount
//	贷 建筑[power]  amount
//
// 【2026-09-19 裁决（§4.5.3 G2）：建造力交易不计税】政府是建造力的唯一买家、
// 同时又是税收的收款人，对这笔交易计税只会原地回冲；故按净额成交，
// 历史上那笔"政府自己收自己的税"（自反税腿）从此不再存在。
func PowerPurchase(powerIdx int, amount float64) *Txn {
	return (&Txn{Name: "政府采购建造力"}).
		Debit(Gov(), amount).
		Credit(Building(powerIdx), amount)
}

// InvestmentPayGov ⑤：投资池向政府偿还建造力货款（§4.5.3 G6）。
//
//	借 投资池   amount
//	贷 政府     amount
//
// G6 不再是"政府把储备卖给扩建方"：私人扩建所消耗的建造力由投资池全额
// 付给政府。政府先按 G2 把同一笔钱付给建造部门，再收这笔全额偿还，
// 故政府的建造力**净支出恒为 0**，也不存在自反税腿。
func InvestmentPayGov(amount float64) *Txn {
	return (&Txn{Name: "投资池付政府（建造力货款）"}).
		Debit(Investment(), amount).
		Credit(Gov(), amount)
}

// InvestmentBuyEquity ⑨b：投资池出资收购政府股权（§4.5.1a，2026-09-19 第 28 轮）。
//
//	借 投资池   amount
//	贷 政府     amount
//
// 【为什么单独一条腿，而不复用 Privatize】`Privatize` 的借方是**资本池**
// （`ledger.Capital()`）。默认参数下资本池长期为负（实测 −3.38e9），
// 于是"收购"在资金上永远排不上——而**投资池里躺着闲置的储蓄**（实测 3.0e9）。
// 用户裁决（第 28 轮）："允许收购用投资池余额出资"。
//
// 【所有权口径（必须写明）】出资方是投资池，但**新增的私有股权仍记在资本建筑
// （金融区 / 宅邸庄园）名下**——因为投资池不是所有权主体，它只是"可动用于投资的
// 资金"（§4.5.1b）。于是本交易只搬钱，不动股权；股权腿（GovLevel → PrivLevel）
// 由调用方在同一处同步执行。这与 §4.5.1b 的"谁出资谁拥有"是**有意的例外**：
// 那里的"出资方"是资本建筑本身，而这里资本建筑没有钱、钱来自居民储蓄。
func InvestmentBuyEquity(amount float64) *Txn {
	return (&Txn{Name: "投资池出资收购政府股权"}).
		Debit(Investment(), amount).
		Credit(Gov(), amount)
}

// InvestmentInflow ⑦：资本建筑的净额入投资池（§4.5.1b）。
//
//	借 来源（资本建筑账户）  amount
//	贷 投资池                amount
//
// 来源是**资本建筑的营运现金池**：宅邸庄园用 Building(ManorIndex)，
// 金融区用 Capital()。入池额取 max(0, 该资本建筑本期净额) ——负净额由资本建筑
// 自己的现金池承担，不倒抽投资池。
func InvestmentInflow(source Account, amount float64) *Txn {
	return (&Txn{Name: "资本建筑净额入投资池"}).
		Debit(source, amount).
		Credit(Investment(), amount)
}

// ===== 1.2 M8：借贷台账的两条资金腿（2026-09-20 第 40 轮）=====

// SavingsBankLend 是"储蓄银行 → 投资池"的**放贷腿**（1.2 M8.3 的 ①）。
//
//	借 储蓄银行   amount
//	贷 投资池     amount
//
// 【为什么钱直接进投资池、而不是先过金融区的现金池】
// 1.0 实测金融区营运净额**长期为负**；若贷款先进它的池，会被**先拿去补亏损**，
// 到不了投资池。故 1.2 裁决把资金腿与负债腿**分开**：
//   - **资金**：储蓄银行 → 投资池（本函数）；
//   - **负债**：记在金融区头上，但它是**非现金科目**（`fiscal.Capital.Debt` 字段，
//     见 M8.7 的裁决），**不进账本、不参与借贷相等**。
//
// 【货币守恒】本交易借贷相等，故货币总量不变；它只是把储蓄银行手里的钱
// 搬到投资池（可动用于扩建的资金）。
func SavingsBankLend(amount float64) *Txn {
	return (&Txn{Name: "储蓄银行放贷（→ 投资池）"}).
		Debit(SavingsBank(), amount).
		Credit(Investment(), amount)
}

// DebtService 是"金融区 → 储蓄银行"的**还本息腿**（1.2 M8.3 的 ③）。
//
//	借 金融区（资本池）   amount
//	贷 储蓄银行           amount
//
// 【资金来源】按 M8.4 裁决："金融区在**入池之前先扣下**本期还款额"——
// 即调用方从"金融区净额"里先扣掉 amount，再把余额转入投资池。
// 于是还款**不来自投资池、也不来自政府**（谁借谁还）。
//
// 【违约（M8.4.1）】本期付不出的部分**不核销、不加速、不没收**，
// 而是滚入 `Loan.Outstanding` 并按同一利率继续计息 ⇒ **只过账实付额**。
func DebtService(amount float64) *Txn {
	return (&Txn{Name: "金融区还本息（→ 储蓄银行）"}).
		Debit(Capital(), amount).
		Credit(SavingsBank(), amount)
}

// GovDebtInterest 是"**政府债务利息 → 中央银行**"腿（1.2 M5 ①，2026-09-20 第 67 轮裁决）。
//
//	借 政府            amount
//	贷 中央银行         amount
//
// 【裁决】M5（第 67 轮）："政府债务计息，**利息付给央行**"。
// 与 M8.5"四条腿同一利率曲线（年化 5%）"一致——央行是货币当局，
// 它同时是**造币方**与**政府债务的债权人**。
//
// 【政府侧的记账含义】政府的"现金池"允许为负，而"债务"就定义为
// `max(0, −现金池)`（1.0 §4.5.4）。故**借记政府** = 现金池更负 = **债务增加**；
// 同时贷记央行 = 央行的现金池实增。即
//
//	"政府以举债方式付息，债权人拿到现金"
//
// ——借贷两侧都真实变化，**货币总量不变**（它不是创造，只是转移）。
func GovDebtInterest(amount float64) *Txn {
	return (&Txn{Name: "政府债务利息（→ 中央银行）"}).
		Debit(Gov(), amount).
		Credit(CentralBank(), amount)
}

// SavingsBankToLabor 是"储蓄银行 → 劳动力"的**利息当期分配腿**（1.2 M8.6 ③）。
//
//	借 储蓄银行   amount
//	贷 人群池[p]  各池按人头分摊
//
// 【口径】储蓄银行是劳动力储蓄的代理，故它收的利息按裁决"**当期分配**"给劳动力。
// 分摊方式与 §5.1 的人群池一致（按池人口），由调用方算好金额传入。
func SavingsBankToLabor(total float64, credits []Entry) *Txn {
	t := &Txn{Name: "储蓄银行利息分配（→ 劳动力）"}
	t.Debit(SavingsBank(), total)
	for _, c := range credits {
		t.Credit(c.Account, c.Amount)
	}
	return t
}

// LaborDividendToLabor 是"**劳动力分红池 → 劳动力**"的派发腿（1.2 M4.2）。
//
//	借 劳动力分红池   total（各池之和）
//	贷 人群池[p]       各池按人头分摊额
//
// 【与 SavingsBankToLabor 的关系】两者结构相同、只差借方账户——
// 因为它们表达的是**同一个机制**："统一入口 → 按人头分摊到人群池"
// （2026-09-20 第 51 轮裁决："统一：在职人口，失业者不参与"）。
// 分红来自所有权（M4.2），利息来自储蓄银行放贷（M8.6 ③）。
//
// 【记账意义】这**不是**转移支付的重新分配，而是把已经贷记给劳动力的钱
// 从"统一入口"发到各池——货币总量不变，只是持有位置从"劳动力（合计）"
// 变为"各人群池"。没有它，钱会永久停在分红池里（R75 实测 30.13 亿）。
func LaborDividendToLabor(total float64, credits []Entry) *Txn {
	t := &Txn{Name: "劳动力分红派发"}
	t.Debit(LaborDividend(), total)
	for _, c := range credits {
		t.Credit(c.Account, c.Amount)
	}
	return t
}

// Welfare ⑩：政府福利金（**转移支付**，§4.5.8，2026-09-19 第 15 轮裁决）。
//
//	借 政府                total（各池之和）
//	贷 人群池[p]           各池补贴额
//
// 【口径】福利金是转移支付，不是产出：它不进 §7 的 GDP、不参与利润划分，
// 只把政府的钱按"平均工资标准 − 本人工资"的差距比例转给居民。
// 受 §4.5.4 的可动用资金（债务上限）约束——国库没钱就发不动。
//
// 【为什么按池发放】§5.1 的人群池是"场地 × 阶级"的细粒度账户，
// 各阶级工资不同（5 / 10 / 20）、失业者工资为 0，故补贴率逐池不同；
// 由调用方按池算好金额传入，本函数只保证借贷相等。
func Welfare(total float64, credits []Entry) *Txn {
	t := &Txn{Name: "福利金（转移支付）"}
	t.Debit(Gov(), total)
	for _, c := range credits {
		t.Credit(c.Account, c.Amount)
	}
	return t
}

// Saving ⑪a：居民结余 → **储蓄固定账户**（§5.3，2026-09-19 第 20 轮改口径）。
//
//	借 人群池[p]            各池消费结算后的结余
//	贷 储蓄账户             total（各池之和）
//
// 【语义】"工资结余 → 储蓄 → 投资"的第一步：消费结算之后仍未花掉的工资与福利金
// **全部离开居民钱包**，先进入一个具名的固定账户（ledger.Savings()）。
// 它对应第 20 轮裁决原文「工资结余当前计入某固定账户」。
//
// 【与旧口径的差别（前值 → 后值）】旧口径直接把 σ_save × 结余划给投资池，
// 其余 (1−σ)·结余**留在人群池**——"结余进固定账户"这句话在账本上没有落点，
// σ<1 时也读不出"结余总额"。现在第一步搬全部结余，第二步才按 σ 转投资。
func Saving(debits []Entry, total float64) *Txn {
	t := &Txn{Name: "居民结余入储蓄账户"}
	for _, d := range debits {
		t.Debit(d.Account, d.Amount)
	}
	t.Credit(Savings(), total)
	return t
}

// SavingsToInvestment ⑪b：储蓄账户 → 投资池（§5.3，2026-09-19 第 20 轮）。
//
//	借 储蓄账户            amount
//	贷 投资池              amount
//
// 【语义】第二步：储蓄账户本期收到的结余，按储蓄率 σ_save 转入总投资池
// （与 §4.5.1b 的资本净额入池共用同一账户），成为扩建资金。
// σ_save 默认 1.0 ⇒ 每周期**全部**转入，储蓄账户随之归零；
// 这正是裁决原文的「随后每周期全部转移入投资池，维护货币循环，但记账」。
//
// 两笔交易都借贷相等，故货币总量的守恒不依赖 σ 的取值。
func SavingsToInvestment(amount float64) *Txn {
	return (&Txn{Name: "储蓄转入投资池"}).
		Debit(Savings(), amount).
		Credit(Investment(), amount)
}

// DepositGoods ⑬：生产者 → 仓库的**入库**（§4.5.6，2026-09-19 第 16 轮）。
//
//	借 仓库                gross = Σ V_g·(1+ν)   （仓库付出的含增值税总额）
//	贷 建筑[生产者]        各生产者应得的净额 V_g（按供给份额）
//	贷 政府                ν·Σ V_g             （增值税）
//
// 【价格口径】V_g = 该商品本 tick 的**生产者售价**总额 = P_g × 过库量。
// 增值税由仓库代缴（仓库的购买成本因此是 V·(1+ν)），它从卖出加价里收回。
//
// warehouse 由调用方给出（book 持有账户下标），与 InvestmentInflow 同风格——
// ledger 不依赖 model 包的建筑下标常量。
func DepositGoods(warehouse Account, gross float64, credits []Entry) *Txn {
	t := &Txn{Name: "入库存货（生产者→仓库）"}
	t.Debit(warehouse, gross)
	var credited float64
	for _, c := range credits {
		t.Credit(c.Account, c.Amount)
		credited += c.Amount
	}
	// 差额即增值税（ν·Σnet）：由仓库代缴给政府。
	// 【口径】用减法而不是再乘一次 ν，保证 Σ贷 = Σ借 逐位成立（浮点乘法不满足结合律）。
	if tax := gross - credited; tax != 0 {
		t.Credit(Gov(), tax)
	}
	return t
}

// WithdrawGoods ⑭：买家 → 仓库的**出库**（§4.5.6）。
//
//	借 买家账户            gross = base·(1+τ)
//	贷 卖方账户            base（= Σ q·P·(1+仓库加价)）
//	贷 政府                τ·base             （消费税）
//
// 卖方账户由调用方给出，对应契约的两种路径：
//
//	生产建筑买中间投入：卖方 = **仓库**（买家直接与仓库交易）
//	居民买消费品：      卖方 = **消费代理**（代理再逐池透传给仓库，见 AgentPassThrough）
//
// 本版不设进项抵扣：中间投入同样计征消费税（口径见 §4.5.6）。
func WithdrawGoods(buyer, seller Account, base, tau float64) *Txn {
	tax := base * tau
	return (&Txn{Name: "出库存货（仓库→买家）"}).
		Debit(buyer, base+tax).
		Credit(seller, base).
		Credit(Gov(), tax)
}

// AgentPassThrough ⑮：消费代理的**逐池透传**（§4.5.6）。
//
//	借 消费代理            amount
//	贷 仓库                amount
//
// 代理不留钱、不雇人、无利润：它收到各人群池的货款后同额付给仓库。
// 借贷相等与两条腿同额 ⇒ 代理的账本余额**恒为 0**（审计断言）。
func AgentPassThrough(agent, warehouse Account, amount float64) *Txn {
	return (&Txn{Name: "消费代理透传"}).
		Debit(agent, amount).
		Credit(warehouse, amount)
}

// ProfitAllocate ⑥：利润归属（§4.5.1，2026-09-19 新口径，无留存比例）。
//
//	借 建筑[i]          留池 + 政府 + 所有者（恒等于纯利 π_i）
//	贷 建筑[i]          补足自身现金池 R_i
//	贷 政府             政府份额 π^net_i · s_gov
//	贷 所属资本建筑      私人份额 π^net_i · (1 − s_gov)
//
// 【借贷相等的关键】三条腿之和恒等于纯利：留池 + 政府 + 所有者 ≡ π_i。
// 调用方必须从同一笔纯利按"先补池、后按持股分配"的顺序算出三者，
// 本函数再按它们的和构造借方，因此"忘了把某一份额算进去"在结构上不可能发生。
//
// 【亏损（π_i < 0）】留池 = 0，政府与所有者两条腿为**负数**：
// 贷方记负数即"该账户余额减少"，借方记负数即"建筑现金池余额增加"，
// 于是 建筑 +|π_i| / 政府 −|π_i|s_gov / 所有者 −|π_i|(1−s_gov)，
// 三条腿之和仍恒为 0（= π_i 的划分），符合契约"亏损由所有者按持股承担"。
func ProfitAllocate(site int, retain float64, govAmt float64, owner Account, ownerAmt float64) *Txn {
	return (&Txn{Name: "利润归属"}).
		Debit(Building(site), retain+govAmt+ownerAmt).
		Credit(Building(site), retain).
		Credit(Gov(), govAmt).
		Credit(owner, ownerAmt)
}

// ProfitAllocateSplit ⑥：利润归属的**两腿所有者**版本（1.2 M4.2）。
//
//	借 建筑[site]      retain + govAmt + ownerAmt
//	贷 建筑[site]      retain
//	贷 政府            govAmt
//	贷 owner            ownerAmt − laborAmt
//	贷 劳动力分红池      laborAmt
//
// 【为什么要它】M4.2 的所有权重构把"资本现行的 70% 所有权"转给劳动力，
// 于是同一笔私人份额纯利要在**资本**与**劳动力**两个主体之间拆开。
// 三条腿的总和必须**逐位**等于纯利，所以这里**不重新乘比例**，
// 而是让 owner 腿取 `ownerAmt − laborAmt`（减法而非乘法，与
// `book.ProfitAllocate` 里"残差归所有者"的理由相同：浮点乘法不满足结合律）。
//
// 【默认不生效】调用方仅在 `Params.OwnershipRestructure` 开启时走本函数；
// 关闭时仍走 `ProfitAllocate`（单腿），故 1.0 逐位不变。
func ProfitAllocateSplit(
	site int, retain, govAmt float64,
	owner Account, ownerAmt float64,
	laborAmt float64,
) *Txn {
	if laborAmt <= 0 {
		return ProfitAllocate(site, retain, govAmt, owner, ownerAmt)
	}
	return (&Txn{Name: "利润归属（资本/劳动力两腿）"}).
		Debit(Building(site), retain+govAmt+ownerAmt).
		Credit(Building(site), retain).
		Credit(Gov(), govAmt).
		Credit(owner, ownerAmt-laborAmt).
		Credit(LaborDividend(), laborAmt)
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
