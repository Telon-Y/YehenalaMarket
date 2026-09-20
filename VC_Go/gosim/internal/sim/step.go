package sim

import (
	"fmt"
	"math"

	"yehenala/market/internal/book"
	"yehenala/market/internal/build"
	"yehenala/market/internal/calibrate"
	"yehenala/market/internal/cohort"
	"yehenala/market/internal/consume"
	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/ledger"
	"yehenala/market/internal/model"
	"yehenala/market/internal/produce"
	"yehenala/market/internal/product"
)

// Step 推进一个 tick，严格按契约 §8 的管线（含架构 M1–M3 修正）。
//
// 管线顺序与理由（§8 主循环；★ = 政府/资本相关，☆ = 顺序敏感）：
//
//	⓪  重算金融区与宅邸庄园级数（§8-0）      ☆
//	①  生产与配给（§8-2）
//	②  价格结算（§8-3，M1 提前）             —— 消费者用【本 tick】的价格购买
//	③  工资（§8-4，M2 显式 wagePool）         —— 工资在同一 tick 全部支出
//	④  消费（§8-5）
//	⑤  中间投入交易（§8-6）
//	⑥  利润归属（§8-7）★☆                    —— 先补足自身营运资金，余额按当期持股分配
//	⑦  私有化（§8-8）★☆
//	⑦b 资本建筑结算与投资池入账（§8-7b）★☆    —— 庄园/金融区净额全额入池，累计 K_m/K_f
//	⑦c 政府补贴（§4.5.7）★                    —— 只影响雇佣决策，不影响扩建判定
//	⑧  EMA 更新（含补贴 MarginEMA + 不含补贴 ProfitEMA）
//	⑨  两条投资栈的扩建意向（§8-9）★           —— 庄园栈只对农业建筑，金融栈对其余
//	⑩  政府采购建造力（§8-10，G2 按需即买即用）★
//	⑪  按队列推进施工 + 投资池偿还政府（§8-11/12，G6）★
//	⑫  完工（§8-13）                          —— 新增等级全归出资资本建筑，政府被稀释
//	⑬  缩编（§8-14）—— 本版冻结
//	⑭  人口（§8-15）、雇佣调整（§8-16）
//	⑮  触发式重算零利润价（§8-17，由 market/calibrate 侧负责）
//
// 【顺序敏感的三处（§8 的实现提示）】
//   - ⑩ 的采购预算必须在利润与税收（⑥⑦）入账**之后**决定；
//   - ⑨ 必须早于 ⑩，因为采购量由队列当期需要量决定；
//   - ⑦b 必须早于 ⑨，否则本期投资能力只反映上期余额。
func (s *State) Step() (*Snapshot, error) {
	// ⓪ 重置本 tick 的流量计数器。
	//
	// 政府与资本的流量字段（税收、经营净额、采购、偿还、转移）都是"每 tick 流量"，
	// 必须在 tick 开头清零。若不清零，它们会跨 tick 累加成存量，使
	// "投入 ≤ 采购" 这类不变量检查与全部报告失真。
	//
	// 【顺序】powerBoughtPrev 必须在 ResetFlow 之前抓取：它是【上一 tick】的采购量，
	// 用于本 tick 建造部门的收入确认（见 ⑥ 的说明）。
	s.powerBoughtPrev = s.Gov.PowerPurchased
	// 【§4.5.8】公共工程的上一期采购量同样要在清零前抓取（建造部门收入确认用）。
	s.powerPublicWorksPrev = s.publicWorksSoldTick
	s.Gov.ResetFlow()
	s.Cap.ResetFlow()
	s.tickNewCapital = 0
	s.Recon = Recon{}
	s.flowGovOperatingDelta, s.flowGovPurchaseDelta, s.flowGovInvestDelta = 0, 0, 0
	s.powerSpendActual = 0
	s.subsidyPaidTick = 0
	s.infusionTick = 0
	s.tickInflowManor, s.tickInflowFinance, s.tickInvestmentPaid = 0, 0, 0
	s.powerPaidManor, s.powerPaidFinance = 0, 0
	s.powerNeedTick, s.powerAvailTick = 0, 0
	// 【2026-09-19 第 15 轮裁决】三条新资金流的每 tick 计数器。
	s.savingTick, s.welfarePaidTick = 0, 0
	s.savingInvestTick = 0
	// 【1.2 M8】本 tick 的放贷额必须每 tick 清零——否则它会**留存上一笔的值**，
	// 任何按"本 tick 是否 > 0"计数的诊断（含审计断言）都会把一笔贷款数成全年的笔数。
	// （实测：未清零时 400 tick 被数成 349 笔，而真实节奏是 7 笔。）
	s.loanIssuedTick = 0
	// 【1.2 M4.2】劳动力分红同理每 tick 清零（它是"本 tick 贷记了多少"的诊断量）。
	s.laborDividendTick = 0
	s.laborDividendPaidTick = 0
	// 【1.2 §1.2-5】造币与购金的本 tick 流量（诊断量，每 tick 清零）。
	// 【顺序很重要】先把上一 tick 的购金款存进 `goldPaidPrev`，再清零——
	// 金矿的收入信号要读它（见 `goldPaidPrev` 的字段注释）。
	s.goldPaidPrev = s.goldPaidTick
	s.mintTick, s.goldPaidTick = 0, 0
	// 【1.2 M5 ①】政府债务利息的本 tick 流量（每 tick 清零）
	s.govDebtInterestTick = 0
	// 【R94】金矿本 tick 拿到的建造力额度（诊断量）
	s.goldMineSlotTick = 0
	s.goldMineInSlotsTick = false
	// 【1.2 M4.2】本 tick 归属给"私人"的全部份额（= Σ res.Owner，含盈亏两向）。
	// 它是验证"资本 30% / 劳动力 70%"拆分口径的**源头量**——
	// 用它而不是期末池余额，避免派发经储蓄通道回流造成的二阶污染。
	s.privateShareTick = 0
	s.publicWorksSpendTick, s.publicWorksUnitsTick, s.publicWorksSoldTick = 0, 0, 0
	s.budgetShareManor = 0
	// 【§4.5.6 第 16 轮】仓库路径的每 tick 计数器。
	s.tradeVolumeTick, s.warehouseQuotaTick = 0, 0
	s.warehouseGoodsInTick, s.warehouseGoodsOutTick = 0, 0
	s.warehouseVATTick, s.warehouseConsumeTaxTick = 0, 0
	s.warehouseExpandUnitsTick, s.warehouseExpandSpendTick = 0, 0
	s.warehouseProfitTick, s.warehouseWageTick, s.lastManorDeposit = 0, 0, 0

	// ⓪b 金融区级数按掌控比推导（§3.2/§4.5.2 裁决：金融区不建造）。
	// 放在读 levels 之前，使本 tick 的生产、工资、掌控上限用同一个金融区级数。
	s.syncFinanceLevel()
	// ⓪c 宅邸庄园级数按自给农场级数 ÷ 掌控比推导（§4.5.5：庄园不建造）。
	s.syncManorLevel()

	levels := s.levels()
	hire := s.hireRates()
	// ⓪d 劳动力分配（§4.2 修订）：生产建筑 → 自给农场 → 失业。
	s.allocateSubsistenceLabor(levels, hire)
	// 【§5.2 第 23 轮人口约束】allocateSubsistenceLabor 会算出配给系数 k
	// （= 人口 / 各场地申报雇佣量，未触限时为 1）。**下面一律用生效雇佣率
	// `effHire = k × HireRate`**，使生产、工资、人口登记与快照四者同源——
	// 否则会出现"按申报量付工资、按配给量报就业"的分叉。
	if r := s.LaborMarketRatio; r > 0 && r < 1 {
		for i := range hire {
			hire[i] *= r
		}
	}
	specs := s.buildingSpecs()

	var buildingDeltaBefore float64
	for i := range s.Buildings {
		buildingDeltaBefore += s.bal(i)
	}
	govDeltaBefore := s.balGov()
	capDeltaBefore := s.balCap()
	houseDeltaBefore := s.Houses.TotalCash()
	investDeltaBefore := s.balInvest()
	// 逐建筑对账基准（诊断用）
	s.reconBefore = make([]float64, len(s.Buildings))
	for i := range s.Buildings {
		s.reconBefore[i] = s.bal(i)
	}
	s.Recon.ByBuilding = make([]BuildingRecon, len(s.Buildings))

	// ① 生产与配给
	plan := produce.Settle(specs, levels, hire, s.subsistence())

	// ①b 实际工农生产总值（§7.2，2026-09-19 第 18 轮）
	//
	// 【为什么在 ① 之后、价格之前】本口径只用**实物量**（级数 × 雇佣率 × 配方 × 配给比）
	// 与**固定的 P_ref**（§3.4 的零利润价解，开局解一次），故它与本 tick 的价格、
	// 税率、货币存量、政府债务**完全无关**——这正是"去除货币的干扰"的含义。
	s.productTick = product.Compute(product.Input{
		Specs:       specs,
		Levels:      levels,
		Hire:        hire,
		Shortage:    plan.ShortageFactor,
		AllocRatio:  plan.AllocRatio,
		Subsistence: s.subsistence(),
		RefPrices:   s.refPrices(),
		Population:  s.Population,
	})
	if s.Tick == 0 {
		// 基期取**首个 tick**（开局尚未生产，理论值为 0 不能作分母）。
		s.productBaseAdded = s.productTick.Added
	}

	// ② 价格结算：用实际产出 S（含短缺惩罚）驱动 E = D − S
	//
	// 方案 C′（实验开关，§3.4 候选）：没有家庭最终需求的商品按【派生需求】重锚，
	// 使中间品需求随下游产能增长，而不是钉在开局净供给上。
	if s.Params.AnchorDerivedDemand {
		s.reanchorDerived(levels)
	}
	// 【§七 R32】动态零利润价：用**上一 tick 结算后的价格**重算当期 P⁰，
	// 供本 tick 的价格方程（需求归一化、刚度 K、钳制带）使用。
	if s.dynamicPcost {
		s.refreshPzero()
	}
	s.Market.SettleAll(plan.ActualOutput, nil)

	// ③ 工资：从建筑现金池【实际划转】到人群现金池（§5.1 修订）
	prices := s.Market.Prices()
	wages := produce.WageBill(specs, levels, hire)
	totalWage := produce.TotalWage(wages)
	// 【R86】把本 tick 的**工资总额**记为造币锚。
	//
	// 用途见 `Params.MintWageFraction`：§1.2-5 的固定造币额（400,000/20 黄金）
	// **从未与 1.0 的货币存量对齐**，实测会每 tick 印出存量的 58%。
	// 本锚让"新增货币"始终是**当期工资流量**的一个比例——
	// 与 1.0 自己"初始货币按一周工资标定"（§4.3 修订）是同一条纪律。
	//
	// 注意 `mintGold` 在 ⑥-e 之后才跑，晚于这里 ⇒ 本字段在造币时已是**本 tick** 的值。
	s.wageBillTick = totalWage
	// 人群池的人口是本 tick 的【流量】，先清零再随工资划转登记。
	// 现金是存量，保留。注意顺序：必须在 payWages 之前 Reset，
	// 否则会把刚划入的工资人口清掉。
	s.Houses.Reset()
	// ⓪d 已经把劳动力分成三档（生产建筑 → 自给农场 → 失业）；此处把**失业人口**
	// 登记到独立的虚拟劳动场地（§6.5，第 15 轮裁决）：失业算劳工，但没有工资，
	// 其池只能收到 §4.5.8 的福利金。
	if s.Unemployed > 0 {
		s.Houses.Credit(s.UnemployedSite, 0, s.Unemployed)
	}
	s.payWages(levels, hire, wages)

	// ③b 福利金（§4.5.8，2026-09-19 第 15 轮裁决）：政府 → 人群池的转移支付。
	//
	// 【为什么在工资之后、消费之前】福利金必须在本期消费结算之前到账，
	// 否则它只能影响下一期的购买；而它要修正是"失业者当期无法消费"这件事。
	// 人口登记（payWages 与上面的失业登记）也必须在它之前完成。
	s.payWelfare(prices)

	// ④ 消费（§5.1：按人群池逐个结算，每池用自己的现金池）
	//
	// 【§4.5.6 价格口径】居民从**仓库**买货：仓库卖出价 = 生产者售价 ×(1+仓库加价)，
	// 消费代理再按该价 ×(1+消费税) 向居民收款。故：
	//   - 传给消费结算的是**仓库卖出价**（retail），预算约束用消费税 τ；
	//   - `outcome.SpendNet` 因此是"仓库从消费代理收到的 base"（含加价、不含 τ）。
	groups := model.ConsumeGroupSpecs()
	retail := make([]float64, len(prices))
	for i := range prices {
		retail[i] = prices[i] * (1 + s.Params.WarehouseMarkup)
	}
	poolSpecs := make([]consume.PoolSpec, len(s.Houses.Pools))
	budgets := s.deflatedBudgets(s.Params.ConsumeTaxRate)
	for i := range s.Houses.Pools {
		poolSpecs[i] = consume.PoolSpec{
			Population: s.Houses.Pools[i].Population,
			Tier:       s.Houses.WealthTier(i),
			Budget:     budgets[i],
			// 阶级下标供幸福度按阶级报告（§6.5 第 15 轮裁决）。
			Class: s.Houses.Pools[i].Class,
		}
	}
	outcome := consume.PurchaseByPoolsScaled(groups, poolSpecs, plan.NetSupply, retail, s.demandScale, s.Params.BasketScale)
	s.Last = outcome

	// ④b 消费扣款与入账：人群池 → 消费代理 → 仓库（§4.5.6）
	//
	//	① 借 人群池[pi]   base·(1+τ)   贷 消费代理 base   贷 政府 τ·base
	//	② 借 消费代理      base         贷 仓库     base（逐池透传）
	//
	// 消费代理的净余额恒为 0：它收到多少就付出多少。
	// 逐池收集：每个池的可支付额由 cohort.Spend 按"现金是否够"裁剪（只做判断、不改余额）。
	var consumerBase, consumerTax float64
	for pi := range s.Houses.Pools {
		base := outcome.Pools[pi].SpendNet
		if base <= 0 {
			continue
		}
		paidBase, _, ok := s.Houses.Spend(pi, base, s.Params.ConsumeTaxRate)
		if !ok {
			continue
		}
		got, tax := s.Bk().WithdrawViaAgent(s.Houses.Account(pi), paidBase, s.Params.ConsumeTaxRate)
		consumerBase += got
		consumerTax += tax
	}
	// ② 代理把收到的钱原额付给仓库（代理余额因此归零）。
	s.Bk().AgentPassThrough(consumerBase)
	s.Recon.ConsumerRevenue = consumerBase
	s.Recon.ConsumerTaxWh = consumerTax
	s.warehouseGoodsInTick += consumerBase
	// 【税收口径】出库的消费税已真实打进政府账（那是交易的一半），
	// 但它不维护 Gov.TaxCollected——那是 sim 的统计口径，必须在此同步。
	s.Gov.TaxCollected += consumerTax
	s.warehouseConsumeTaxTick += consumerTax
	// 登记进仓库的逐建筑对账（它是出库款的收款方）。
	s.recordRecon(model.WarehouseIndex, func(r *BuildingRecon) { r.WarehouseIn += consumerBase })

	// ④c 居民储蓄（§5.3，2026-09-19 第 15 轮裁决；**第 20 轮改口径**）。
	//
	// 消费结算之后，各人群池仍未花掉的现金（工资与福利金的结余）**全额**先进入
	// **储蓄固定账户**（第一步），再按储蓄率 σ_save 从该账户转入**总投资池**
	// （第二步），供两条投资栈扩建（§4.5.1b 的同一账户）。
	//
	// 【裁决原文（第 20 轮）】"工资结余当前计入某固定账户，随后每周期全部转移入
	// 投资池，维护货币循环，但记账"。σ_save 默认 1.0 ⇒ 第二步每周期把储蓄账户
	// **全部**转入投资池，该账户期末归零；两笔交易都借贷相等，货币总量不变。
	//
	// 【与第 15 轮旧口径的差别】旧实现直接「借人群池 σ×结余、贷投资池 σ×结余」，
	// (1−σ) 留在人群池、账本上读不出"本期结余"。现在结余有一个具名过手方。
	//
	// 【它修的是什么】R29/R30 实测：§6.3 的数量篮子只值该档工资的 21.5%~28.8%，
	// 而 §5 原规定"全部工资用于消费"——结余此前没有任何出口，只能沉积在人群池
	// （tick 1000 达 2.58e9），在 R26 的资金口径下直接表现为"投资池恒为 0、
	// 经济零建造"。（§5 的"全部工资用于消费"一句已在第 20 轮删除，见契约 §5。）
	//
	// 【顺序】放在消费扣款之后，故本 tick 的储蓄可以在本 tick 的 ⑩ 被用于扩建。
	rateSave := s.Params.SavingsRate
	if rateSave < 0 {
		rateSave = 0
	}
	if rateSave > 1 {
		rateSave = 1
	}
	// 【σ = 0 的语义】储蓄渠道整体关闭：第一步也不发生，结余留在人群池
	// （这正是 R29 诊断臂"回到封闭口径、看结余沉积"所需要的措辞，见审计测试）。
	if rateSave > 0 {
		var legs []book.SavingLeg
		for i := range s.Houses.Pools {
			cash := s.Houses.Pools[i].Cash()
			if cash <= 1e-9 {
				continue
			}
			legs = append(legs, book.SavingLeg{Pool: s.Houses.Account(i), Amount: cash})
		}
		s.savingTick = s.Bk().SaveToInvestment(legs)
		if s.savingTick > 0 {
			if s.Params.BankEnabled {
				// 【1.2 M4.3/M8：储蓄银行路径】**劳动力多余的资金 → 储蓄银行**
				// 裁决原文："劳动力储蓄进入储蓄银行，储蓄银行代买；先前资本不再能
				// 使用劳动力储蓄购买。"
				//
				// 故 1.2 口径下，第二步的目的地由"投资池"改为"**储蓄银行**"——
				// 于是资本无法再用这笔钱去收购（满足第 31 轮裁决），
				// 而储蓄银行再把钱**放贷给金融区**（见本 tick 的 ④e）。
				s.savingInvestTick = s.Bk().SavingsToBank(s.savingTick * rateSave)
			} else {
				s.savingInvestTick = s.Bk().SavingsToInvestment(s.savingTick * rateSave)
			}
		}
		// 【1.2 M1 第 3 条：存量口径】累计"攒下"的结余，以及其中"已分配为投资"的部分。
		//
		// 【为什么用**实际**过账额而不是申报额】`savingInvestTick` 是第二步的**实付额**
		// （受储蓄账户余额与 σ_save 约束）。用它累计才能让
		// "已分配 ≤ 已积累"这条不变量**恒成立**；用申报额会在资金不足时破坏它。
		//
		// 【默认不生效】开关关闭时本块整体跳过 ⇒ 两个字段恒为 0 ⇒ 1.0 逐位不变。
		if s.Params.SavingsStockTrack {
			s.savingsStock += s.savingTick
			s.savingsAlloc += s.savingInvestTick
		}
	}

	// ⑤ 中间投入交易（§4.5.6：买家 → 仓库的**出库**）
	//
	// 投入品不再"从卖方建筑直接划到买方建筑"，而是：
	//
	//	借 建筑[买方]  base·(1+τ)      贷 仓库 base        贷 政府 τ·base
	//
	// 其中 base = 投入量 × 当期市价 × (1+仓库加价)。买方的现金池因此实际减少
	// （这是"上游恒亏"能被真实约束住的前提），而卖方**不再**在本步收款——
	// 生产者的收入统一由下一步的入库款结算（按供给份额分摊）。
	inputValue := produce.InputValue(specs, levels, hire, prices, plan.AllocRatio)
	// inputPaidGross[i] 是该买方本 tick 的**实际扣款**（base·(1+τ)），
	// 供 ⑥ 计算实际纯利用——不能用理论申报额 inputValue 代替。
	inputPaidGross := make([]float64, len(s.Buildings))
	var inputBase, inputTax float64
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if !b.Spec.Produces() || inputValue[i] <= 0 {
			continue
		}
		base := inputValue[i] * (1 + s.Params.WarehouseMarkup)
		got, tax := s.Bk().WithdrawFromWarehouse(ledger.Building(i), base, s.Params.ConsumeTaxRate)
		if got <= 0 {
			continue
		}
		inputBase += got
		inputTax += tax
		inputPaidGross[i] = got + tax
		s.recordRecon(i, func(r *BuildingRecon) { r.InputOut += got + tax })
	}
	s.warehouseGoodsInTick += inputBase
	s.warehouseConsumeTaxTick += inputTax
	s.Recon.IntermediateIn = inputBase
	s.Recon.IntermediateOut = inputBase + inputTax
	s.Recon.InputTaxWh = inputTax
	s.Gov.TaxCollected += inputTax
	s.recordRecon(model.WarehouseIndex, func(r *BuildingRecon) { r.WarehouseIn += inputBase })

	// ⑤b 入库：仓库 → 生产者（§4.5.6）
	//
	//	借 仓库     Σ V_g·(1+ν)     贷 建筑[生产者] V_g（按供给份额）    贷 政府 ν·Σ V_g
	//
	// 过库量 = 中间投入实际取用量 + 消费者实际购买量（两者都在本 tick 已经结算），
	// 故"入库 = 出库"：仓库不持有商品库存（与 §4.5.3 的建造力"即买即用"同一条简化纪律）。
	//
	// 【它同时关闭了 R21 的缺口】入库款按**供给份额**分摊：某一商品的过库量由
	// 专业生产者与自给农场（所有者是宅邸庄园）共同供给，故中间投入那一路的货款
	// 不再全额记到"该商品的唯一专业生产者"账上。
	depositLegs := s.producerDepositLegs(plan, outcome, prices)
	depositNet, depositVAT := s.Bk().DepositToWarehouse(depositLegs, s.Params.VATRate)
	s.warehouseGoodsOutTick = depositNet + depositVAT
	s.warehouseVATTick = depositVAT
	s.Recon.DepositNet = depositNet
	s.Recon.DepositGross = depositNet + depositVAT
	s.Gov.TaxCollected += depositVAT
	for _, l := range depositLegs {
		if l.Net == 0 {
			continue
		}
		s.recordRecon(l.Producer, func(r *BuildingRecon) { r.DepositIn += l.Net })
	}
	s.recordRecon(model.WarehouseIndex, func(r *BuildingRecon) { r.WarehouseOut += depositNet + depositVAT })
	// 宅邸庄园的入库收入（§4.5.5）：它是庄园 ⑦b 净额的第一项。
	manorDepositIn := s.lastManorDeposit

	// ⑤c 贸易量与额度（§4.5.6）
	//
	//	贸易量 = Σ_{商品 ≠ 建造力}（中间投入取用量 + 消费者购买量）   —— 单向过手量
	//	额度   = 仓库当期级数 × WarehouseQuotaPerLevel
	//
	// 【口径】入库量 = 出库量（同一 tick 内完成），故按**单向**计一次，不重复计双向。
	// 建造力不经仓库（政府按 §4.5.3 G2 / §4.5.8.1 直接采购），故不计入。
	for g := 0; g < model.Goods; g++ {
		if g == powerGoodIndex {
			continue
		}
		if g < len(plan.UsedInputs) {
			s.tradeVolumeTick += plan.UsedInputs[g]
		}
		if g < len(outcome.Bought) {
			s.tradeVolumeTick += outcome.Bought[g]
		}
	}
	// 额度按**本 tick 开始时的**仓库级数计（本 tick 的扩建订单要到之后的 tick 才完工）。
	s.warehouseQuotaTick = s.Buildings[model.WarehouseIndex].Level * s.Params.WarehouseQuotaPerLevel

	// ⑥ 利润归属（§4.5.1，2026-09-19 新口径）
	//
	// 【必须遵守的不变量】一笔运营纯利 π_i 只在三条腿之间【归属】，三者之和恒等于 π_i：
	//
	//	R_i      = min(max(π_i,0), max(0, C*_i − B_i))   → 补足建筑自身现金池
	//	政府份额  = π^net_i · s_gov                       → 政府现金池
	//	所有者份额= π^net_i · (1 − s_gov)                 → 该建筑【所属资本建筑】池
	//
	// 其中 π^net_i = max(π_i,0) − R_i，s_gov 是**当期**持股（GovLevel/Level）。
	// **不存在留存比例**：建筑池只负责营运资金（补足到 C*_i），扩建资金统一由
	// 投资池提供（§4.5.1b）。
	//
	// 所属资本建筑（§4.5.1 的归属表）：
	//
	//	占用农业用地（LandKind == "arable"，谷物农场/棉花种植园） → 宅邸庄园
	//	其余生产建筑（含建造部门）                              → 金融区
	//
	// 【亏损（π_i < 0）】不补池、不分配：政府池 −|π_i|·s_gov、所属资本建筑池
	// −|π_i|·(1−s_gov)，同额回补建筑现金池；三条腿之和恒为 0。
	//
	// 修订前实现为"政府份额 + 建筑留存（比例）+ 资本份额"三分，且留存比例参数
	// 让建筑池同时承担营运资金与扩建资金两种职能（契约 §4.5.1 已废止该口径）。
	powerIdx := fiscal.PowerGoodIndex
	// 【§4.5.6】生产者的收入 = **入库款**（按供给份额分摊，见 ⑤b 的 depositLegs），
	// 建造力例外：它由政府采购（G2/公共工程）直接付款，不经仓库。
	//
	// 【为什么建造力取上一 tick 的采购量】建造力的采购发生在 ⑩，而利润归属在 ⑥ 之前，
	// 故本期 ⑥ 只能看到**已经确认**的收入——即上一 tick 的采购量
	// （s.powerBoughtPrev，在 ⓪ 清零流量计数前保存）。这与"那笔钱确实已经进入
	// 建造部门现金池"一一对应；若在这里读当期的 PowerPurchased（此刻恒为 0），
	// 建造部门会永远显示 −100% 的利润率、永不扩建。
	revenue := make([]float64, len(s.Buildings))
	for _, l := range depositLegs {
		if l.Producer >= 0 && l.Producer < len(revenue) {
			revenue[l.Producer] += l.Net
		}
	}
	revenue[powerIdx] = (s.powerBoughtPrev + s.powerPublicWorksPrev) * prices[powerIdx]

	// 【1.2 §1.2-5：金矿的**外生价**收入】
	//
	// 金矿**不在**商品市场里（裁决：黄金外生价格、不进 A 矩阵），
	// 故它的收入不来自 `depositLegs`，而来自**央行购金**。
	//
	// 【R86 更正：收入信号必须等于**实际到账的钱**，不能用"名义市价 × 产出"】
	//
	// 第一版写成 `产出 × GoldPrice`（= 125 × 10,000 = **1,250,000**/tick），
	// 而央行受货币锚约束、每 tick 实际只付出约 **142,000**。
	// 两者相差近一个数量级 ⇒ 记账上"收 125 万、只到账 14 万"
	// ⇒ 金矿现金池被持续抽干（实测 −16.77e6）⇒ 而 `LastProfit` 却显示 +1.08e6
	// ⇒ **账实不符**（利润信号与现金流脱节）。
	//
	// 正确口径：金矿的收入 = **央行本期实际付给它的购金款**
	//（`s.goldPaidTick`，上一 tick 的值——与 §8 的"用上一期结算量做信号"同一纪律）。
	if s.Params.CentralBankEnabled {
		for i := range s.Buildings {
			if s.Buildings[i].Spec.ProducesGold {
				revenue[i] = s.goldPaidPrev
				break
			}
		}
	}

	margins := make([]float64, len(specs))
	// profitMargins 是**不含补贴**的即时利润率（§4.5.7 校验条款）。
	// ⑦c 的补贴只加进 margins，不加进它，故 ProfitEMA 不会被补贴污染。
	profitMargins := make([]float64, len(specs))
	var capitalIncome, manorIncome, govOperating float64

	for i := range s.Buildings {
		b := &s.Buildings[i]
		// 【R86】金矿必须参与利润计算 —— 它是"有产出、要利润"的生产建筑，
		// 只是产出不进商品市场（`Produces()` 为 false）。
		//
		// 【实测教训】只用 `!b.Spec.Produces()` 过滤时，金矿的
		// `LastProfit` / `MarginEMA` 恒为 **0**（探针实测：600 tick 全是 0）
		// ⇒ §4.1 的扩建判定看不到它 ⇒ 它永远停在起始 5 级、永不扩建。
		// 而它的收入已由上面那段（`revenue[金矿] = 产出 × GoldPrice`）给出。
		if !b.Spec.Produces() && !b.Spec.ProducesGold {
			continue
		}
		saleValue := revenue[i]
		b.LastRevenue = saleValue
		// 【§4.5.6】投入按**买家实付**扣减：base·(1+τ) 已含仓库加价与消费税
		// （inputPaidGross 是 ⑤ 的实际扣款额，不是 inputValue 的理论申报额）。
		b.LastProfit = saleValue - inputPaidGross[i] - wages[i]

		// margin 用【名义满编口径】（§3.1 的"实际成本基"）：与雇佣率无关，
		// 只由当期价格与配方决定，故不会因雇佣率趋零而数值爆炸。
		//
		// 【2026-09-19 第 15 轮订正】旧实现用"实际收入/雇佣率"外推，在公共工程
		// 让建造部门于低雇佣率下仍有收入时爆到 1e10%（见 fullCapacityCosts 的说明）。
		revFull, costFull := s.fullCapacityCosts(i, prices)
		if costFull > 1e-9 {
			margins[i] = (revFull - costFull) / costFull
			profitMargins[i] = margins[i]
		}

		// 营运资金目标 C*_i（§4.5.1）= WorkingCapitalPeriods × 一个周期的满编营运成本
		// （满编工资 + 满编中间投入）。costFull 正是这个满编营运成本。
		cstar := s.Params.WorkingCapitalPeriods * costFull

		// 所属资本建筑（§4.5.1 的归属表）。
		owner := ledger.Capital()
		if build.IsArable(b.Spec) {
			owner = ledger.Building(model.ManorIndex)
		}

		// 利润归属：【统一记账簿】负责三条腿的计算与过账，
		// 借贷两侧在同一处构造，故"三条腿之和 ≠ 纯利"在结构上不可能发生。
		//
		// 【1.2 M4.2】`laborShare` 是"私人份额中归劳动力"的比例：
		// 未开重构 ⇒ 0（逐位复现 1.0）；开启 ⇒ 1 − OwnershipCapitalShare（默认 0.70）。
		res := s.Bk().ProfitAllocate(
			i, b.LastProfit, s.govShare(i), cstar, s.bal(i), owner, s.laborPrivateShare())
		s.laborDividendTick += res.Labor
		s.privateShareTick += res.Owner
		govOperating += res.Gov
		if build.IsArable(b.Spec) {
			manorIncome += res.Owner
			// 农业建筑的私人份额贷记**庄园现金池**，故必须登记进庄园的逐建筑对账
			// （§4.5.1 的归属表）。漏记它会留下恰等于该笔利润的残差（实测 761,834）。
			// 金融区的同类收入直接进 ledger.Capital()，不在任何建筑池里，故不登记。
			s.Recon.ProfitIn += res.Owner
			s.recordRecon(model.ManorIndex, func(r *BuildingRecon) { r.ProfitIn += res.Owner })
		} else {
			capitalIncome += res.Owner
		}
		s.Recon.RetainedProfit += res.Retain
		// 【M4.2 说明】现金池实际净腿 = −(政府份额 + 所有者份额)。
		// 注意它**与是否拆分无关**：`ProfitAllocateSplit` 的借方仍是
		// `retain + gov + owner`，两腿之和逐位等于 `owner` ⇒ 建筑侧净腿不变。
		// 劳动力那一腿只是在**贷方**多了一个去向（`ledger.LaborDividend()`），
		// 不动建筑自身的余额。
		leg := res.Retain - b.LastProfit // 现金池实际净腿 = −(政府份额 + 所有者份额)
		s.Recon.ProfitLegTotal += leg
		s.recordRecon(i, func(r *BuildingRecon) {
			r.Retained += res.Retain
			r.ProfitLeg += leg
		})
	}
	// 金融区自身：收入 = 资本纯利，成本 = 金融区工资（工资已从 Capital 池付出）。
	//
	// 金融区的现金池就是 ledger.Capital()，其私人份额纯利已由 ⑥ 的 ProfitAllocate
	// 直接贷记进 Capital（owner = ledger.Capital()），故这里不再需要一笔内部转移；
	// 只登记诊断字段与利润率的 EMA 输入。
	{
		fin := &s.Buildings[model.FinanceIndex]
		finProfit := capitalIncome - wages[model.FinanceIndex]
		fin.LastProfit = finProfit
		if wages[model.FinanceIndex] > 1e-9 {
			margins[model.FinanceIndex] = finProfit / wages[model.FinanceIndex]
			profitMargins[model.FinanceIndex] = margins[model.FinanceIndex]
		}
		s.Recon.FinanceProfit = finProfit
		s.Cap.OperatingProfit = capitalIncome
		s.Cap.WageBill = wages[model.FinanceIndex]
	}

	// ⑥-e 【1.2 M4.2】劳动力分红派发：把分红池的余额发给**在职人口**。	//
	// 【为什么紧跟在利润归属之后】分红是本期利润归属的产物，同 tick 派发
	// 使"本期挣的、本期到居民手里"，与 M8.6 ③ 的"利息当期分配"同一节奏。
	//
	//	借 劳动力分红池   余额
	//	贷 人群池[p]       按人头分摊（**排除失业池**，2026-09-20 第 51 轮裁决）
	//
	// 【默认不生效】`OwnershipRestructure=false` 时分红池恒为 0 ⇒ 直接返回。
	s.laborDividendPaidTick = s.distributeLaborDividend()

	// ⑥-f 【1.2 §1.2-5】央行购金造币。
	//
	//	造币额 M   = floor(本期产出黄金 / 20) × 400,000      ← **货币注入**
	//	付给金矿 P = 造币消耗的黄金 × 10,000                 ← 借 央行 / 贷 金矿
	//	央行留存   = M − P
	//
	// 【为什么放在利润归属之后】金矿的收入（`revenue[金矿]`）已在 ⑥ 参与利润率计算，
	// 这里再实际造币与付款 ⇒ 顺序上"先算利润、再收钱"，
	// 与 §4.5.6 仓库"先算纯利、再结算"的既有做法一致。
	//
	// 【默认不生效】`CentralBankEnabled=false` 时 `mintGold` 直接返回 (0,0,0)。
	s.mintGold()

	// ⑥-g 【1.2 §1.2-5 第三项】"当黄金有剩余时，中央银行自动扩建"。
	//
	// 判据见 `expandCentralBank`：金矿本期产出 > 央行吞吐量（级数 × GoldPerBankLevel）
	// ⇒ 央行吃不完 ⇒ 自动扩建到能全部吃下。
	//
	// 【为什么放在造币之后】先用**本期产出**造币（消耗掉能消耗的部分），
	// 再看"产出是否超过吞吐量"决定扩容——顺序上"先尽力、再补产能"。
	s.centralBankExpandedTick = 0
	if s.Params.CentralBankEnabled {
		s.expandCentralBank()
	}

	// ⑥-h 【1.2 M5 ①】政府债务计息 → 中央银行（第 67 轮裁决）。
	//
	//	借 政府 / 贷 央行
	//
	// 【为什么放在这里】它读的是政府**当期**的现金池（= 债务本金），
	// 而本 tick 的政府收支（税收、经营、补贴、公共工程）都已过账 ⇒ 本金是最新值。
	//
	// 【它不创造货币】货币总量不变（借贷双方都是既有账户），
	// 故它**不进** `InfusionTotal`——若误计，货币守恒会多出一个恰等于利息的残差。
	s.payGovDebtInterest()

	// ⑥-d 仓库（§4.5.6）：它**不生产商品**，但有完整的贸易收支。
	//
	//	收入 = 出库收款（消费者 base + 中间投入 base，含 5% 加价、不含消费税）
	//	成本 = 入库付款（生产者货款 + 增值税）
	//	纯利 = 收入 − 成本 − 自身工资  （= 加价 − 增值税 − 工资）
	//
	// s_gov = 1 ⇒ 纯利**全部**归政府（亏损也全部由政府承担）：
	// 加价收入先付自身工资、余额上缴国库，与普通国有生产建筑同构。
	// 仓库不设营运资金目标（C* = 0）：它的收付在同一 tick 内完成，不需要垫资。
	{
		whIdx := model.WarehouseIndex
		wh := &s.Buildings[whIdx]
		whRevenue := s.warehouseGoodsInTick
		whCost := s.warehouseGoodsOutTick
		wh.LastRevenue = whRevenue
		wh.LastProfit = whRevenue - whCost - wages[whIdx]
		if den := whCost + wages[whIdx]; den > 1e-9 {
			margins[whIdx] = wh.LastProfit / den
			profitMargins[whIdx] = margins[whIdx]
		}
		res := s.Bk().ProfitAllocate(whIdx, wh.LastProfit, 1.0, 0, s.bal(whIdx), ledger.Capital(), 0)
		govOperating += res.Gov
		s.warehouseProfitTick = wh.LastProfit
		s.warehouseWageTick = wages[whIdx]
		s.Recon.RetainedProfit += res.Retain
		leg := res.Retain - wh.LastProfit
		s.Recon.ProfitLegTotal += leg
		s.recordRecon(whIdx, func(r *BuildingRecon) {
			r.Retained += res.Retain
			r.ProfitLeg += leg
		})
	}

	// ⑥b 政府收入入账
	//
	// 税收由各笔交易在扣款时同步登记（消费 ④b、中间投入 ⑤、建造力 ⑦⑨），
	// 此处只把【政府经营净额】入账。若在此处再加一次 "税收"，
	// 就是历史上"政府经营净额被记两次"那类重复入账的复发。
	//
	// 符号约定：govOperating 以【正数 = 政府收入】表示，为负即政府持有的部门整体亏损。
	//
	// 【不要在这里再加一次 govOperating】
	//
	// 政府份额已经由上面的 s.Bk().ProfitAllocate(...) 作为贷方入账——
	// 那是"利润归属"这笔交易的一半。若此处再 s.Gov.Cash.Add(govOperating)，
	// 政府份额就被记了两遍，正是历史上"Δ货币/Σ利润 = 2.00"那类重复记账的复发。
	//
	// 这里只登记资金流分解所需的分项，不再动任何余额。
	s.flowGovOperatingDelta = govOperating

	// ⑦ 私有化（§4.5.1 修订；**2026-09-19 第 27 轮：改为"收购订单进建造列表"**）
	//
	// 契约语义："对于盈利建筑，市场可以私有化"。
	//
	// 两个层次的开关：
	//   - 总开关 Params.PrivatizeEnabled：关闭时整个机制不运行；
	//   - 逐建筑开关 Building.AllowPrivatize：单个种类可独立配置
	//     （第 26 轮起 11 种生产建筑全部开放）。
	//
	// 【前值 → 后值（第 27 轮）】旧口径是**瞬时成交**：每个满足条件的建筑每 tick
	// 直接划一笔资本池现金、当场转让 `GovLevel × PrivatizeStep` 级。
	// 实测（R51）：资本池被 §4.5.1b 抽干（−2.77e9）⇒ 对价付不出 ⇒ **零成交**。
	//
	// 新口径：**收购也是一张建造列表里的订单**（`Order.Acquire`）——
	//   - 下单条件不变：该建筑盈利（`marginEMA > PrivatizeMargin`）且政府仍有持股；
	//   - 拟收购级数 = `GovLevel × PrivatizeStep`，**每级对价在下单时冻结**；
	//   - 与扩建订单**同列同 FIFO**，但**不消耗建造力**（买的是存量股权）；
	//   - 每 tick 用**资本留存**里的钱推进，付够对价即完工，完工时才转让股权；
	//   - 已有未完工的收购订单时不再重复下单（一张订单只对应一个部门）。
	s.privatizeUnits, s.privatizePaid = 0, 0
	s.privatizePriceSeen, s.privatizePriceTick = 0, 0
	s.acquireCommitted = 0
	if s.Params.PrivatizeEnabled {
		for i := range s.Buildings {
			b := &s.Buildings[i]
			if !b.Spec.AllowPrivatize || b.Spec.IsNonMarket() {
				continue
			}
			if b.GovLevel <= 1e-9 {
				continue
			}
			if b.MarginEMA <= s.Params.PrivatizeMargin {
				continue
			}
			if s.hasAcquireOrder(i) {
				continue // 该部门已有一张未完工的收购单
			}
			// 本 tick 拟收购的等级数 = 政府持股 × 步长
			want := b.GovLevel * s.Params.PrivatizeStep
			if want > b.GovLevel {
				want = b.GovLevel
			}
			if want <= 1e-9 {
				continue
			}
			// 每级对价 = 建造成本（建造力）× 建造力当期价格 × 估值倍数，**下单时冻结**。
			unitPrice := b.Spec.BuildCost * prices[fiscal.PowerGoodIndex] * s.Params.PrivatizePriceMult
			if unitPrice <= 0 {
				continue
			}
			s.privatizePriceSeen = unitPrice
			s.privatizePriceTick = prices[fiscal.PowerGoodIndex]
			s.Orders = append(s.Orders, Order{
				BuildingIndex: i,
				Units:         want,
				UnitPrice:     unitPrice,
				Stack:         build.StackFinance,
				Acquire:       true,
			})
		}
		// 本 tick 为收购**留存**的资本额（在 ⑨b 处理资本入池时扣除，见那里的说明）。
		s.acquireCommitted = s.acquireNeed()
	}

	// ⑦b 政府补贴（§4.5.7，2026-09-19 新增）。
	//
	// 【为什么在这里】必须在 margin EMA 更新（⑧）之前把钱付到位，否则 EMA 仍看到亏损，
	// §5.2 的"亏损 ⇒ 解雇"会先行一步，补贴就永远追不上。
	//
	// 触发条件（三条同时满足）：① 总开关打开且该类建筑 AllowSubsidy；
	// ② 满编口径亏损（margin < 0）且雇佣率 < 1；③ 政府可动用资金 > 0。
	// 补贴额 = min(满编口径缺口, SubsidyCap, 可动用资金)，补到 margin 回到 0 为止。
	if s.Params.SubsidyEnabled {
		powerPrice := prices[fiscal.PowerGoodIndex]
		avail := s.Gov.AvailableCash(powerPrice)
		for i := range s.Buildings {
			b := &s.Buildings[i]
			if !b.Spec.AllowSubsidy || hire[i] >= 1-1e-9 || margins[i] >= 0 || avail <= 0 {
				continue
			}
			revFull, costFull := s.fullCapacityCosts(i, prices)
			gap := costFull - revFull
			if gap <= 0 {
				continue
			}
			pay := gap
			if s.Params.SubsidyCap > 0 && pay > s.Params.SubsidyCap {
				pay = s.Params.SubsidyCap
			}
			if pay > avail {
				pay = avail
			}
			if pay <= 1e-9 {
				continue
			}
			paid := s.Bk().PaySubsidy(i, pay)
			avail -= paid
			s.subsidyPaidTick += paid
			// 补贴计入该建筑本期"收入"，使 margin 回到 0 ⇒ ⑧ 的 EMA 不再为负 ⇒ 增雇。
			if costFull > 1e-9 {
				margins[i] += paid / costFull
			}
			s.recordRecon(i, func(r *BuildingRecon) { r.Subsidy += paid })
		}
	}

	// ⑦c 资本建筑结算与投资池入账（§4.5.1b，§8-7b）
	//
	// 两个**资本建筑**（宅邸庄园 / 金融区）每期扣除自身工资后的纯利
	// **全额进入总投资池**，并按来源累计贡献 K_m（庄园）/ K_f（金融区）：
	//
	//	入池_m = max(0, 庄园本期净额)   = max(0, 农业私人份额纯利 + 自给产出货款 − 庄园工资)
	//	入池_f = max(0, 金融区本期净额) = max(0, 非农业私人份额纯利 − 金融区工资)
	//
	// 负净额由该资本建筑现金池自行承担：不进池、也**不向投资池倒抽**
	//（投资池不得透支）。
	//
	// 【场地账户口径（§4.5.1b）】庄园用 ledger.Building(ManorIndex)（§4.3 的
	// "宅邸庄园现金池"），金融区用 ledger.Capital()（§4.3 的"资本现金池"）。
	//
	// 【必须在 ⑨ 之前】否则本期投资能力只反映上期余额（§8 的实现提示）。
	manorNet := manorIncome + manorDepositIn - wages[model.ManorIndex]
	financeNet := capitalIncome - wages[model.FinanceIndex]
	if manorNet > 0 {
		s.tickInflowManor = s.Bk().InvestmentInflow(ledger.Building(model.ManorIndex), manorNet)
	}
	// 【§4.5.1a 第 27 轮：收购留存】金融区（资本）的净额里，先扣掉本 tick
	// **为收购订单预留**的那部分，再转入投资池。理由是用户裁决"收购同样进建造列表"：
	// 若资本的全部净额都按 §4.5.1b 转去付建造力（扩建），收购单就永远排不上队
	// ——实测（R51）资本池被抽到 −2.77e9、私有化**零成交**。
	//
	// 这笔留存**不创造货币**：它只是"留在资本池而不是转去投资池"，
	// 后续由 `processAcquisitions` 以「借 资本、贷 政府」的真实交易付掉。
	if financeNet > 0 {
		committed := s.acquireCommitted
		if committed > financeNet {
			committed = financeNet
		}
		financeNet -= committed
	}
	// 【1.2 M8.4 / M8.4.1：**入池之前先扣本期还款额**】
	//
	// 裁决原文（M8.4）："金融区在入池之前先扣下本期还款额"；
	// （M8.4.1）"**违约则延期**"——付不出的部分滚入未偿余额并继续按 5%/年计息，
	// **不核销、不加速、不没收**。
	//
	// 【为什么在这里】必须在 `InvestmentInflow`（下一条语句）之前：
	// 否则钱先进投资池，金融区就"没有钱还贷"，而 1.0 实测金融区营运净额**长期为负**。
	//
	// 【默认不生效】`BankEnabled = false` 时本块整体跳过 ⇒ 1.0 逐位不变。
	if s.Params.BankEnabled {
		s.serviceDebt(&financeNet)
		// 【放贷节奏】每 `LoanIssueInterval` 个 tick 发放一笔新贷款（默认 52 = 每年一笔）。
		//
		// 【必须用 `(Tick+1) % iv == 0` 而不是 `Tick % iv == 0`】
		// `Tick` 在 step 的**末尾**才自增，故 step 开头 `Tick = 0` 是**第一个 tick**；
		// 若写成 `Tick % iv == 0`，则 tick 0 **每局必然**满足 ⇒ 首个 tick 就放一笔。
		// 更糟的是它让"节奏"从第一 tick 起就偏一格。用 `(Tick+1)%iv` 使
		// "第 iv、2iv、3iv… 个 tick"各放一笔，语义干净。
		//
		// 【实测教训】改前（`Tick%iv`）配合"每 tick 递减 iv 到 1"的误用，
		// 400 tick 内发放了 **400 笔 / 2e8 元**，把资本池抽到 −6.7e8 且全额延期
		// （见 ACTIVE §七 R65 的前值）。
		iv := s.Params.LoanIssueInterval
		if iv <= 0 {
			iv = 1
		}
		if (int(s.Tick)+1)%iv == 0 {
			s.loanIssuedTick = s.issueLoan()
		}
	}
	// ⑦b 【1.2 M8.6 ③】利息**当期分配**给劳动力。
	//
	//	借 储蓄银行     Σ金额
	//	贷 人群池[p]     按人头分摊（**排除失业池**，2026-09-20 第 51 轮裁决）
	//
	// 【与分红派发共用 `laborLegs`】两者是同一个机制（"统一入口 → 按人头分摊"），
	// 只是入口账户不同（储蓄银行 vs 劳动力分红池）。
	//
	// 【为什么现在恒为 0】本 tick 收到的利息 = `DebtService` 的实付额，
	// 而 **R73 已证明实付恒为 0**（金融区可还额恒负 ⇒ 债务台账对经济惰性）。
	// 故本调用**结构上已接好、但在当前参数下不可达**——
	// 一旦 R70 的收口 (b)（给金融区独立收入）落地，它就会自动开始分配。
	// 显式接上的意义：让"利息当期分配"不再是一行**没有调用点**的死代码。
	if s.Params.BankEnabled {
		if paid := s.Cap.DebtPaid; paid > 0 {
			if legs := s.laborLegs(paid); len(legs) > 0 {
				s.Bk().SavingsBankToLabor(legs)
			}
		}
	}
	if financeNet > 0 {
		s.tickInflowFinance = s.Bk().InvestmentInflow(ledger.Capital(), financeNet)
	}
	s.KManor += s.tickInflowManor
	s.KFinance += s.tickInflowFinance
	s.Cap.InvestInflow = s.tickInflowFinance
	s.Recon.ManorInflow = s.tickInflowManor
	s.Recon.FinanceInflow = s.tickInflowFinance
	// 入池款对【建筑池】的净腿只可能是庄园那一笔（金融区从 Capital 转出）。
	s.Recon.InvestmentOut += s.tickInflowManor
	if s.tickInflowManor != 0 {
		s.recordRecon(model.ManorIndex, func(r *BuildingRecon) { r.InvestmentOut += s.tickInflowManor })
	}

	// ⑧b 推进**收购订单**（§4.5.1a 第 27 轮）。
	//
	// 【顺序】它必须紧跟资本入池（⑧，含"收购留存"）之后：留存把钱留在资本池里，
	// 这里才付得出去。放在 EMA 更新（⑨）之前与其无耦合，选此处是为了让
	// "本期资本部署"两件事（扩建出资 / 收购出资）在步序上相邻可比。
	// 它**不消耗建造力**，故与 ⑩ 的建造力分配互不干扰。
	{
		u, p := s.processAcquisitions()
		s.privatizeUnits += u
		s.privatizePaid += p
	}

	// ⑧ EMA 更新：含补贴 / 不含补贴两条口径（§4.5.7 的校验条款）
	//
	//	MarginEMA = EMA(含补贴的即时利润率)   → §5.2 的雇佣调整
	//	ProfitEMA = EMA(不含补贴的即时利润率) → §4.1 的扩建判定
	//
	// 两个 EMA 必须分开：否则"亏损建筑靠补贴把 margin 抬到 0 以上"会被扩建逻辑
	// 误读为盈利，用真金白银继续扩产（重复补贴 + 违背"救急不救扩"）。
	marginEMA := make([]float64, len(specs))
	profitEMA := make([]float64, len(specs))
	for i := range s.Buildings {
		w := s.Params.MarginEMA
		if w <= 0 {
			w = 12
		}
		b := &s.Buildings[i]
		b.MarginEMA = b.MarginEMA*(1-1/float64(w)) + margins[i]/float64(w)
		b.ProfitEMA = b.ProfitEMA*(1-1/float64(w)) + profitMargins[i]/float64(w)
		marginEMA[i] = b.MarginEMA
		profitEMA[i] = b.ProfitEMA
		b.LastMargin = margins[i]
		b.LastProfitMargin = profitMargins[i]
	}

	// ⑨ 两条投资栈的扩建意向（§8-9）
	//
	// 庄园栈只对**农业建筑**立项、金融栈对其余生产建筑（含建造部门）立项；
	// 两条队列并行推进、互不抢占对方资金。判定用 ProfitEMA（不含补贴）。
	manorIntents, financeIntents := build.Plan(specs, levels, profitEMA, s.Params)

	// §4.2 的队列告警：**队列完成时间 > 52 周期**时，在【金融栈队首】插入一条
	// 建造部门扩建订单（建造力供给跟不上扩建需求）。它归金融栈，
	// 故同样受金融栈预算约束（§4.5.1b：两条栈互不抢占资金）。
	//
	// 【第 23 轮改口径】扩建量不再固定为"当前级数 × 50%"（那本身就是个盲目的棘轮，
	// 与 §4.1 的"利润率 > 10%"叠加会把建造部门推到 860 级），而是由
	// **队列深度 ÷ 目标周期**算出"刚好能在 52 周期内消化完"的产能缺口
	// （`build.PowerCapacityUnits`）。`Params.PowerByQueue = false` 时回退旧行为。
	if s.queueTicks(manorIntents, financeIntents, plan.ActualOutput[powerIdx]) > s.Params.QueueWarnTicks {
		powerUnits := math.Max(1, math.Ceil(math.Max(levels[powerIdx], 1)*0.5))
		if s.Params.PowerByQueue {
			qty := 0.0
			if specs[powerIdx].Recipe.Qty > 0 {
				qty = specs[powerIdx].Recipe.Qty
			}
			powerUnits = build.PowerCapacityUnits(
				levels[powerIdx], hire[powerIdx], s.powerBacklog(),
				s.Params.QueueWarnTicks, qty, s.Params,
			)
		}
		if powerUnits > 0 {
			financeIntents = append([]build.Intent{{
				BuildingIndex: powerIdx,
				Units:         powerUnits,
				Stack:         build.StackFinance,
			}}, financeIntents...)
		}
	}

	// ⑩ 两条栈的预算与 G2 按需采购（§4.5.3 G2 + §4.5.1b）
	//
	// 【预算分配】投资池可动用额 P = max(0, 投资池余额)，按**累计贡献** K_m : K_f 分配：
	//
	//	庄园栈 = P·K_m/(K_m+K_f)，金融栈 = P − 庄园栈；K_m+K_f ≤ 0 时各 0.5
	//
	// 预算是**硬约束**（投资池不得透支），故"需要量"里已经扣掉了"本栈付得起多少"。
	//
	// 【队列语义】s.Orders 是持久队列：每 tick 先服务【已存在的订单】（FIFO），
	// 每个订单最多推进 min(剩余需求, SitePowerLimit)；再在各自栈预算内**新建订单**。
	// 【本次修复的缺陷】旧实现在创建订单时把当次投入一次性写进 Progress、
	// 之后永不再推进，于是昂贵订单永远不完工；现在每 tick 真实累加 Progress。
	powerPrice := prices[powerIdx]
	s.powerPriceNow = powerPrice
	powerOut := plan.ActualOutput[powerIdx]
	s.Gov.PowerOutput = powerOut

	// 【诊断】无限资金对照实验（Options.UnlimitedFunds，§七 R28）：
	// 把投资池补到哨兵水位，使两条栈的预算永不成为约束。
	// 注入额单独计量（infuse → infusionTotal），故货币守恒仍可核。
	if s.unlimitedFunds {
		const infFundTarget = 1e12
		if b := s.balInvest(); b < infFundTarget {
			s.infuse(ledger.Investment(), infFundTarget-b)
		}
	}
	pool := s.balInvest()
	if pool < 0 {
		pool = 0
	}
	// 【§4.5.1b 修订（2026-09-19 第 15 轮裁决，§0.4 第 8 项选 (a)）】
	// 预算按**当期意向需求**比例分配，不再按两条栈的累计贡献 K_m : K_f。
	//
	// 需求口径 = 该栈本 tick 的**建造力需要量**：
	//  ① 已在建的订单：min(剩余需求, 每工地上限)；
	//  ② 本 tick 新立项的意向：min(BuildCost × Units, 每工地上限)。
	//
	// 即"谁当期真的想花钱，谁就按比例拿到预算"——这修掉 R34 记录的
	// "有钱的栈没项目、有项目的栈没钱"（末期 K_m = 0 而 K_f 持有全部资金）。
	// K_m / K_f 仍照常累计，降级为诊断量。
	demandManor := s.stackDemand(manorIntents, build.StackManor, specs)
	demandFinance := s.stackDemand(financeIntents, build.StackFinance, specs)
	s.demandManorTick, s.demandFinanceTick = demandManor, demandFinance
	shareManor := 0.5
	if demandManor+demandFinance > 0 {
		shareManor = demandManor / (demandManor + demandFinance)
	}
	// 【1.2 M3/M6：玩家投资接口】
	//
	// 裁决（M6）："玩家投资额度**即**政府投资额度；设置**政府投资 AI 开关**，
	// 当玩家控制时**自动关闭**。"（M3）："把投资额度与方向从 AI 托管改为**玩家可决策**"。
	//
	// 故当 `InvestAIEnabled = false` 且玩家给了 `InvestManorShare`（≥ 0）时，
	// **用玩家的方向**替换需求比例；否则保持 1.0 的自动口径（逐位不变）。
	//
	// 【为什么"额度"不需要额外参数】额度**就是**这一期的 `pool`
	//（= 投资池可动用额 = 居民储蓄 + 两条栈的入池净额）——
	// 与 M6"玩家投资额度即政府投资额度"一致：同一个池，不是两份额度。
	// 玩家接管的是它的**方向**（两条栈各拿多少）。
	//
	// 【两个条件缺一不可】只关 AI 而不给占比（−1）时仍走自动口径——
	// 否则"关闭 AI"会隐式变成"50/50"，那是一个玩家没有做过的选择。
	if !s.Params.InvestAIEnabled && s.Params.InvestManorShare >= 0 {
		shareManor = s.Params.InvestManorShare
		if shareManor > 1 {
			shareManor = 1
		}
	}
	s.budgetShareManor = shareManor
	budgetManor := pool * shareManor
	budgetFinance := pool - budgetManor

	type stackSlot struct {
		orderIdx int // ≥ 0 指向 s.Orders；-1 表示本 tick 新建
		intent   build.Intent
		stack    string
		want     float64
	}
	var slots []stackSlot
	var usedManor, usedFinance float64
	budgetOf := func(stack string) float64 {
		if stack == build.StackManor {
			return budgetManor
		}
		return budgetFinance
	}
	usedOf := func(stack string) float64 {
		if stack == build.StackManor {
			return usedManor
		}
		return usedFinance
	}
	addUse := func(stack string, q float64) {
		if stack == build.StackManor {
			usedManor += q
		} else {
			usedFinance += q
		}
	}
	// capByBudget 把本栈尚未花掉的预算折算成建造力单位，并据此裁剪 want。
	capByBudget := func(stack string, want float64) float64 {
		if powerPrice <= 0 {
			return 0
		}
		room := budgetOf(stack) - usedOf(stack)*powerPrice
		if room <= 0 {
			return 0
		}
		if afford := room / powerPrice; want > afford {
			want = afford
		}
		return want
	}
	for oi := range s.Orders {
		o := &s.Orders[oi]
		// 【§4.5.8】公共工程订单由国库在 ⑩b 单独推进，**不占用投资池预算**，
		// 故在这里跳过——否则同一笔建造力会被推进两次。
		if o.PublicWorks {
			continue
		}
		stack := o.Stack
		if stack != build.StackManor {
			stack = build.StackFinance
		}
		want := o.Remaining(s.Buildings)
		if want > s.Params.SitePowerLimit {
			want = s.Params.SitePowerLimit // §4.2：每工地每 tick 最多投入 30 建造力
		}
		want = capByBudget(stack, want)
		if want <= 1e-9 {
			continue
		}
		addUse(stack, want)
		slots = append(slots, stackSlot{orderIdx: oi, stack: stack, want: want})
	}
	for _, list := range []struct {
		stack   string
		intents []build.Intent
	}{{build.StackManor, manorIntents}, {build.StackFinance, financeIntents}} {
		for _, it := range list.intents {
			// 【R96：同一建筑**已有在途订单**时不再开新单】
			//
			// 【缺陷（用户指出"为什么建造好的建筑没有从队列中清空"）】原实现
			// 只要 `build.Plan` 给出意向就**无条件**追加一条新订单，
			// **不检查该建筑是否已有在途订单**。实测（600 tick，默认参数）末期队列：
			//
			//	高档服装厂  90.2%  ← 进度已经很高，却没被清空
			//	高档服装厂  65.7%  ⎫
			//	高档服装厂  35.8%  ⎬ 同一建筑的**四条**并行订单
			//	高档服装厂  12.0%  ⎭
			//
			// 后果有两条，都是真缺陷：
			//   ① **重复立项**把建造力预算摊薄到多条同建筑订单上 ⇒ 谁都不完工、
			//      队列越来越长（"队列卡住"的直接原因）；
			//   ② `stackDemand` 把**每条**重复订单都计入需求 ⇒ 需求被系统性高估。
			//
			// 【修法】已有在途订单（`orderIdx >= 0` 或任何 `Orders` 里指向同一建筑）
			// ⇒ 本 tick 不为它开新单。**进度推进不受影响**：已有订单仍在上面的
			// "已有订单"循环里拿到额度（它计数 `o.Remaining`，与意向无关）。
			//
			// 【为什么不改 1.0 基线】本块只在 `CentralBankEnabled`（1.2 分支）生效。
			if s.Params.CentralBankEnabled && s.hasOrderFor(it.BuildingIndex) {
				continue
			}
			want := capByBudget(list.stack, build.PowerNeed(specs, it, s.Params.SitePowerLimit))
			if want <= 1e-9 {
				continue
			}
			addUse(list.stack, want)
			// 【R94 诊断】金矿的意向是否进了 slots（见 `goldMineInSlotsTick` 的注释）
			if specs[it.BuildingIndex].ProducesGold {
				s.goldMineInSlotsTick = true
			}
			slots = append(slots, stackSlot{orderIdx: -1, intent: it, stack: list.stack, want: want})
		}
	}
	// needQty = 队列本 tick 实际要投入的建造力量（已按每工地上限与各栈预算裁剪）。
	var needQty float64
	for _, sl := range slots {
		needQty += sl.want
	}
	s.powerNeedTick = needQty

	// ⑩a 建造力产出的**买家分配**（§4.5.8，2026-09-19 第 15 轮裁决）
	//
	// 建造力现在有两类买家：私人扩建队列（G2，投资池全额偿还 ⇒ 政府净支出 0）
	// 与**公共工程**（国库直接出资、不偿还）。当两者合计需求超过当期产出时，
	// 按**当期意向需求**比例分配（与 §4.5.1b 的预算分配同一原则）：
	//
	//	私人得 powerOut × needQty/(needQty + pubWant)，公共工程得其剩余。
	//
	// 【为什么不能让公共工程只吃"剩余产出"】实测（短缺起步、20m 口径）私人队列
	// 在开局就把 300 单位产出吃满，剩余恒为 0 ⇒ 公共工程支出恒为 0，
	// "政府支出端"等于没接上。按需求比例切分才能使国库的建造需求真实存在，
	// 同时不整体挤掉私人扩建。
	pubBudget := 0.0
	pubWant := 0.0
	if s.Params.PublicWorksShare > 0 {
		pubBudget = s.Params.PublicWorksShare * s.Gov.TaxCollected
		if availCash := s.Gov.AvailableCash(powerPrice); pubBudget > availCash {
			pubBudget = availCash
		}
		if pubBudget > 0 && powerPrice > 0 {
			pubWant = pubBudget / powerPrice
		}
	}
	// 仓库额度扩建的需求（§4.5.6）：它是**强制**的政府建造需求（贸易量超过额度即下达），
	// 因此也进入 ⑩a 的买家分配——否则私人队列会把产出吃满、仓库永远扩不起来
	// （与公共工程同一个"只吃剩余产出就恒为 0"的坑）。
	whWant := s.warehouseExpansionWant()
	privWant := needQty
	govWant := pubWant + whWant
	if total := privWant + govWant; govWant > 0 && total > powerOut {
		privWant = powerOut * privWant / total
		govWant = powerOut - privWant
		// 政府内部的优先级：**先满足仓库扩建**（它是额度规则的硬需求），余额给公共工程。
		if whWant > govWant {
			whWant = govWant
			pubWant = 0
		} else {
			pubWant = govWant - whWant
		}
	}
	_ = pubBudget

	// 【本 tick 的采购量】qty = min(privWant, 产出, 可动用资金/价格)。
	// 无队列 ⇒ needQty = 0 ⇒ 不采购（政府不持有公共储备，PowerInventory ≡ 0）。
	s.powerAvailTick = s.Gov.AvailableCash(powerPrice)
	availQty := s.Gov.PurchaseLimitQty(powerOut, powerPrice)
	// 【§4.5.1b 的资金可行性：把同一 tick 的偿还计入可动用额】
	//
	// 私人扩建的建造力货款由**投资池在同一 tick 内全额偿还**（G6，见下方【记账 ⑤】），
	// 因此对国库而言这是一笔"过手"，可动用额应为 国库可动用资金 + 投资池余额。
	// 不把偿还计入时会出现一个结构性死锁：国库一旦触及债务上限（默认参数下
	// 约 tick 50 就会因被动承担亏损部门的亏损而触顶），G2 立刻停摆 ⇒
	// 政府采购恒为 0 ⇒ 队列冻结 ⇒ §5.3 的储蓄渠道筹到的投资资金**根本花不出去**
	// （实测：tick 52 起采购为 0、总级数冻结在 335）。
	//
	// 这个加法不创造货币：政府付出 X 后在同一 tick 收到 X，tick 末的债务与
	// 货币总量都逐位不变；而公共工程（不被偿还）仍然只受**真实**可动用资金约束。
	if pool > 0 && powerPrice > 0 {
		availQty += pool / powerPrice
	}
	// 【诊断】无限资金对照实验（§七 R28）：国库恒 ∞ ⇒ 解除「可动用资金」裁剪，
	// 采购量只由**队列需要量**与**建造力当期产出**决定。
	// 政府仍在同一 tick 内收到投资池的全额偿还，故其建造力净支出仍为 0。
	if s.unlimitedFunds {
		availQty = privWant
	}
	qty := privWant
	if qty > powerOut {
		qty = powerOut
	}
	if qty > availQty {
		qty = availQty
	}
	if qty < 0 {
		qty = 0
	}
	// 【记账 ③】政府买建造力（不计税）：借 政府、贷 建筑[建造力]。
	pr := s.Bk().PurchasePower(qty, powerPrice)
	if pr.Qty > 0 {
		s.Gov.PowerPurchased += pr.Qty
		s.Recon.PowerRevenue += pr.Amount
		s.recordRecon(powerIdx, func(r *BuildingRecon) { r.PowerNet += pr.Amount })
	}
	s.powerSpendActual = pr.Amount
	s.flowGovPurchaseDelta = pr.Amount

	// 【记账 ④】按 qty 实际推进各订单，并按各订单所属栈累计要偿还的货款。
	// 即买即用：qty 全部分配给 slots（Σ want ≥ qty，故 remaining 必然归零）。
	for oi := range s.Orders {
		s.Orders[oi].Advance = 0
	}
	remaining := qty
	var paidManor, paidFinance float64
	var newOrders []Order
	// 【R95：金矿"优先分配"实现过、**已回滚**——因为它重新引爆了造币】
	//
	// 【根因（R94 定位）】本循环按 `slots` 顺序消耗 `remaining`，用尽即 `break`，
	// 而顺序 = **建筑下标顺序** ⇒ 金矿（下标 15，最靠后）系统性挨饿：
	// 600 tick 里意向进 slots **535** 次，却拿到 **0** 额度、等级恒为 5。
	//
	// 【R95 的收口（有效）】把金矿的槽位提到队首 ⇒ 它开始扩建：
	// 金矿 5 → **50** 级（触顶）、央行 7 → **63** 级、黄金产出 125 → **1250**。
	// 且**基线逐位不变**（金矿规格只在 `CentralBankEnabled` 时存在 ⇒ 关闭时本块不可达）。
	//
	// 【但必须回滚：它重新引爆了造币量级】
	// 实测（600 tick）：平均每 tick 造币 **2,627,516.65 = 货币存量的 52.43%**，
	// 全栈局 900 tick 货币总量增长 **564.7 倍**——正是 **R85 的 58% 灾难**，
	// 也正是"工资锚"（R86）要防的那件事。
	//
	// 【机理：造币额锚在工资总额上，而金矿/央行**本身雇人**】
	//
	//	金矿扩到 50 级 ⇒ 25 万矿工 + 央行 63 级 ⇒ 工资总额暴涨
	//	⇒ `MintWageFraction × 工资总额` 随之暴涨 ⇒ 造币暴涨
	//	⇒ **金矿扩张与造币量通过"工资"互相放大**（正反馈）
	//
	// ⇒ **"让金矿扩张"与"工资锚造币"在当前算式下不相容。**
	// 要二者兼得，必须先改**造币的锚**（例如锚到人口或到存量，而不是工资总额），
	// 那是口径裁决 —— 与 R68/R94 同类，不是我该单方面定的。
	//
	// 【所以 R94 的倾向 (d)"接受现状"是对的】金矿不扩张「不影响 R88 已验证的造币链」：
	// 产出→造币→二分之一分成→"黄金有剩余则扩央行"全程不依赖金矿扩建。
	// 本行以下的分配顺序**保持 1.0 原样**。
	for _, sl := range slots {
		if remaining <= 1e-9 {
			break
		}
		q := sl.want
		if q > remaining {
			q = remaining
		}
		if q <= 1e-9 {
			continue
		}
		value := q * powerPrice
		if sl.stack == build.StackManor {
			paidManor += value
		} else {
			paidFinance += value
		}
		// 【R94 诊断】记录金矿本 tick 拿到的建造力额度（见 `goldMineSlotTick` 的注释）。
		if sl.orderIdx < 0 && specs[sl.intent.BuildingIndex].ProducesGold {
			s.goldMineSlotTick += q
		}
		if sl.orderIdx >= 0 {
			o := &s.Orders[sl.orderIdx]
			o.Progress += q
			o.Advance = q
		} else {
			newOrders = append(newOrders, Order{
				BuildingIndex: sl.intent.BuildingIndex,
				Units:         sl.intent.Units,
				Progress:      q,
				Advance:       q,
				Stack:         sl.stack,
			})
		}
		remaining -= q
		s.Gov.PowerSold += q
	}
	s.Orders = append(s.Orders, newOrders...)
	// PowerInventory 恒为 0：采购量与投入量在本版口径下恒等（即买即用）。
	s.Gov.PowerInventory = 0

	// 【记账 ⑤】投资池付政府（§4.5.3 G6）：总额 = 本 tick 实际投入的建造力价值。
	//
	// 它按构造不超过两栈预算之和（= 投资池可动用额），故投资池不透支；
	// 政府先按 G2 把同一笔钱付给建造部门、再收这笔全额偿还，
	// 故政府的建造力**净支出恒为 0**，也**没有**"政府自己收自己"的自反税腿。
	paidPower := paidManor + paidFinance
	s.powerPaidManor, s.powerPaidFinance = paidManor, paidFinance
	s.Cap.InvestPaid = paidFinance
	if paidPower > 0 {
		s.tickInvestmentPaid = s.Bk().InvestmentPayGov(paidPower)
		s.Gov.PowerRevenue += s.tickInvestmentPaid
		s.flowGovInvestDelta = s.tickInvestmentPaid
	}
	// ⑩b 公共工程（§4.5.8，2026-09-19 第 15 轮裁决）
	//
	// 政府用**当期税收的一部分**（Params.PublicWorksShare，受 §4.5.4 可动用资金约束）
	// 直接采购建造力，投入**开发类建筑**的扩建；新增等级归**政府**（GovLevel），
	// 且**没有**投资池偿还——这是政府的真实支出端（R27/R34 记录的"只有收入、
	// 没有支出端"）。
	//
	// 【与 G2 的关系】私人扩建的建造力由 G2 采购、G6 由投资池全额偿还（政府净支出 0）；
	// 公共工程是同一商品的**第二类买家**，但钱不再回来。二者共用当期产出：
	// 私人队列先按 needQty 取用，**剩余的产出**才可由公共工程购买。
	//
	// 【为什么投向建造部门】§3.2 的开发类建筑目前只有建造部门；它同时是
	// R31/R34 记录的"产能自锁"（长期 −100% ⇒ 一直解雇 ⇒ 产出衰减到 0）的
	// 需求端出口：只要国库还在买，该部门就有收入、margin 回到正、雇佣回升。
	if s.Params.PublicWorksShare > 0 {
		// pubWant 已经在 ⑩a 按"当期意向需求比例"分配好（受预算与产出双重限制）；
		// 这里只需再检查一次"剩余产出"（私人采购可能因可动用资金被裁得更少，
		// 从而留下更多给公共工程）。
		leftover := powerOut - qty
		if leftover < 0 {
			leftover = 0
		}
		bi := s.developmentTarget()
		qtyPub := pubWant
		if qtyPub > leftover {
			qtyPub = leftover
		}
		// 建造部门本期从**两类买家**收到的货款都要登记（私人 G2 与公共工程），
		// 否则逐建筑汇总对账会漏掉恰好等于公共工程支出的残差
		// （实测 45,425.96，而逐建筑残差全为 0）。govBuildOrder 内部登记。
		pwUnits := math.Max(1, math.Ceil(s.Buildings[bi].Level*0.10))
		spent, sold, created := s.govBuildOrder(bi, qtyPub, powerPrice, pwUnits)
		s.publicWorksSpendTick += spent
		s.publicWorksSoldTick += sold
		s.publicWorksUnitsTick += created
	}

	// ⑩c 仓库的**额度驱动自动扩建**（§4.5.6，2026-09-19 第 16 轮）
	//
	// 当本 tick 的贸易量超过"现有级数 × 每级额度"时，政府自动下达仓库扩建订单：
	// 走 §4.2 的常规建造队列、由国库付款（与公共工程同一条政府订单通道）、
	// 完工等级归政府（仓库本来就是国有的）。
	//
	// 【额度是信号不是硬约束】契约只说"超过额度 ⇒ 自动扩建"，没有说"超过额度就不能贸易"。
	// 本版按**信号**实现：贸易照常发生，扩建用来把容量补上——若把它做成硬约束，
	// 开局仓库级数不足会让全部贸易被卡死（经济在 tick 1 冻结），与"可校验大部分功能"相悖。
	if s.Params.WarehouseQuotaPerLevel > 0 && s.tradeVolumeTick > s.warehouseQuotaTick {
		whIdx := model.WarehouseIndex
		cost := s.Buildings[whIdx].Spec.BuildCost
		// 目标级数 = 把本 tick 贸易量装进额度所需的级数。
		wantLevel := math.Ceil(s.tradeVolumeTick / s.Params.WarehouseQuotaPerLevel)
		// 在建订单已覆盖的级数（剩余需求 ÷ 每级造价）。
		orderedUnits := 0.0
		hasOrder := false
		if cost > 0 {
			for oi := range s.Orders {
				o := &s.Orders[oi]
				if o.PublicWorks && o.BuildingIndex == whIdx {
					hasOrder = true
					orderedUnits += o.Remaining(s.Buildings) / cost
				}
			}
		}
		needUnits := wantLevel - s.Buildings[whIdx].Level - orderedUnits
		if needUnits > 1e-9 || hasOrder {
			// 投入量已在 ⑩a 按"私人需求 : 政府需求"比例分配好（whWant）。
			// 只再受国库可动用资金约束（仓库扩建不被偿还，是真实支出）。
			want := whWant
			if powerPrice > 0 {
				if b := s.Gov.AvailableCash(powerPrice) / powerPrice; b < want {
					want = b
				}
			}
			// 新订单规模：补足差额，但单笔不超过现有规模的 50%（0 级时即 1 级）。
			newUnits := math.Max(1, math.Ceil(needUnits))
			if maxNew := math.Max(1, math.Ceil(s.Buildings[whIdx].Level*0.5)); newUnits > maxNew {
				newUnits = maxNew
			}
			spent, _, created := s.govBuildOrder(whIdx, want, powerPrice, newUnits)
			s.warehouseExpandSpendTick += spent
			s.warehouseExpandUnitsTick += created
		}
	}

	// 掌控上限（G5）：只统计**非农业**生产建筑（农业归宅邸庄园，§4.5.2）。
	var otherLevels float64
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if b.Spec.IsNonMarket() || b.Spec.LandKind == "arable" {
			continue
		}
		otherLevels += b.Level
	}
	s.Cap.UpdateControl(s.Buildings[model.FinanceIndex].Level, otherLevels, s.Params.ControlPerFinance)

	// ⑫ 完工（§8-13）
	s.completeOrders()

	// ⑬ 缩编（§4.4）——2026-09-19 裁决：**冻结**，不做等级缩减。
	//    观察窗计数照常维护（诊断与后续版本需要），等级只增不减。
	//    要恢复原行为：把 Params.DecayFrozen 置 false。
	for i := range s.Buildings {
		b := &s.Buildings[i]
		b.IdleTicks = build.UpdateIdle(b.IdleTicks, b.HireRate, s.Params)
		if !s.Params.DecayFrozen {
			b.Level = build.Decay(b.Level, b.IdleTicks, b.MarginEMA, s.Params)
		}
	}
	s.applyArableCap()

	// ⑭ 人口与雇佣调整
	//
	// 【雇佣用含补贴的 MarginEMA】§4.5.7 的效果条款：补贴使 margin ≥ 0 ⇒
	// 增雇分支生效 ⇒ 受补贴建筑主动追求满员。扩建判定则在 ⑨ 用 ProfitEMA。
	satNec := outcome.NecessarySatisfaction()
	growth := consume.PopulationGrowth(satNec, s.Params)
	if s.Tick > 0 && int(s.Tick)%s.Params.TicksPerYear == 0 {
		s.Population *= 1 + growth
	}
	for i := range s.Buildings {
		b := &s.Buildings[i]
		b.HireRate = build.AdjustHire(b.HireRate, b.MarginEMA, s.expectedMarginRatio(i), s.Params)
	}
	// 【1.2 M7：更新各场地的竞标溢价】放在 tick 末，用本 tick 的配置结果
	// （`wageShortTick`）与利润额决定**下一 tick** 的溢价——每 tick 只抬一次，
	// 避免"抬价 → 成本 → 利润率 → 抬价"在同一 tick 内递归（M7.2 的设计约束）。
	//
	// 【默认不生效】`WageBidEnabled=false` 时本块跳过，`wagePremium` 恒为 0。
	if s.Params.WageBidEnabled {
		s.updateWageBids()
	}

	s.Tick++
	tickRecon := &TickRecon{
		GovDelta:    s.balGov() - govDeltaBefore,
		CapDelta:    s.balCap() - capDeltaBefore,
		HouseDelta:  s.Houses.TotalCash() - houseDeltaBefore,
		InvestDelta: s.balInvest() - investDeltaBefore,
	}

	// 政府池的资金流分解：每一路都取【账户实际变动】，不用公式推算。
	//
	// 这样"政府池 Δ = 各路之和"按构造成立，不会因含税/不含税口径差异
	// 留下假残差；若仍有残差，那就是真正未知的资金流——这正是我们要抓的。
	// 政府池的资金流分解全部由 fillFlow 统一构造，此处不再单独赋值。

	// 建筑现金池对账：把实际 Δ 与按明细算出的应有 Δ 对比，残差即漏点。
	{
		var after float64
		for i := range s.Buildings {
			after += s.bal(i)
		}
		s.Recon.NewCapital = s.tickNewCapital
		s.Recon.actual = after - buildingDeltaBefore
		s.tickRecon()
	}
	tickRecon.BuildDelta = s.Recon.actual
	tickRecon.BuildExpected = s.Recon.ExpectedDelta()
	s.TickRecon = tickRecon

	// 政府与资本的账目基本不变量（§4.5.3）：公共储备恒为 0、投入不得超过采购。
	if err := s.Gov.Validate(s.powerPriceNow); err != nil {
		s.InvariantErr = err
	}
	s.fillFlow(flowInput{
		govOperating:  govOperating,
		capitalIncome: capitalIncome,
		manorIncome:   manorIncome + manorDepositIn,
		wageBill:      totalWage,
		spendNet:      outcome.SpendNet,
		powerOut:      powerOut,
		margins:       margins,
		consumerTax:   consumerTax,
		inputNet:      inputBase,
		inputTax:      inputTax,
		sat:           outcome.Sat,
	})
	return s.snapshot(plan, margins, marginEMA, profitEMA, profitMargins, totalWage, manorDepositIn), nil
}

// payWages 把工资从场地营运现金池实际划转到各阶级的人群现金池（§5.1 修订）。
//
// 记账：
//
//	场地现金池 −工资
//	人群池     +工资（按阶级拆分）
//
// 【场地账户口径（§4.5.1b）】金融区是一个**资本建筑**，其营运现金池就是
// ledger.Capital()，故它的工资从资本池支付（book.PayWages 内部按 sitePayer 分流）。
// 因此金融区的工资不计入 Recon.WagePaid（后者只统计**建筑池**的工资支出，
// 用于逐建筑对账），也不进 BuildingRecon[FinanceIndex].Wage。
//
// 两者严格等额，故货币守恒。现金池允许透支（§4.3 的"资金不足时停工"
// 只约束建造力支出，不约束工资发放），但透支会被记入 OverdraftTicks 供诊断。
func (s *State) payWages(levels, hire, wages []float64) {
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if wages[i] <= 0 {
			continue
		}
		popShare, wageShare := cohort.WageShares(b.Spec.LaborPerLevel, b.Spec.StructureOf())
		// 【1.2 M7.2 第 5 步】竞标溢价按比例放大到各阶级：
		//	实际人均工资 = baseWage + p_i ⇒ 工资份额整体乘以 (1 + p_i/baseWage)。
		// `WageBidEnabled=false` 时 wagePremium[i]==0 ⇒ 系数恒为 1，逐位不变。
		if p := s.WagePremium(i); p > 0 {
			if base := s.BaseWage(i); base > 1e-9 {
				k := 1 + p/base
				wageShare[0] *= k
				wageShare[1] *= k
				wageShare[2] *= k
			}
		}
		eff := levels[i] * hire[i]
		var paid float64
		// 【唯一记账入口】工资是一笔借贷相等的交易：
		//
		//	借 场地现金池     工资总额
		//	贷 人群[i][c]    各阶级份额
		//
		// 先按阶级收集份额，再用一笔 Txn 同时登记两侧——
		// "场地扣了多少、人群池收了多少"由同一笔交易保证相等。
		classAmounts := make([]float64, cohort.ClassCount)
		for c := 0; c < cohort.ClassCount; c++ {
			amount := eff * wageShare[c]
			if amount <= 0 {
				continue
			}
			s.Houses.Credit(i, c, eff*popShare[c])
			classAmounts[c] = amount
			paid += amount
		}
		if paid > 0 {
			// 【统一记账簿】工资的借贷两侧在同一处构造——
			// 借方是按场地汇总的同一批金额，贷方是同一批按阶级拆开，
			// 因此"漏记一半"在结构上不可能发生。
			legs := make([]book.WageLeg, 0, cohort.ClassCount)
			for c := range classAmounts {
				if classAmounts[c] > 0 {
					legs = append(legs, book.WageLeg{
						Site: i, Class: c, Amount: classAmounts[c],
					})
				}
			}
			s.Bk().PayWages(legs)
			// 只有**建筑池**的工资进入逐建筑对账；金融区从资本池支付，故跳过。
			if i != model.FinanceIndex {
				s.Recon.WagePaid += paid
				s.recordRecon(i, func(r *BuildingRecon) { r.Wage += paid })
			}
		}
	}
}

// payWelfare 发放政府福利金（§4.5.8，2026-09-19 第 15 轮裁决）。
//
// 发放公式（裁决原文：**以平均工资为标准，将不足平均工资的人口补贴差距的
// 20%×档位**，共 6 档、最高 120%）：
//
//	每人每周期补贴 = min(1.2, 0.2·tier) · max(0, w̄ − w_i)
//
// 其中 w̄ = 平均工资标准 = model.AverageWage() = 6.75 元（§5 的城镇通用劳动结构），
// w_i = 该池人口的**本人工资**——按该场地的劳动结构取（§5 第 20 轮）：
//
//	城镇类（默认）  劳工 5 / 工程师 10 / 资本家 20
//	农业类与庄园    劳工 5 / 农民 7 / 工程师 10
//
// 失业者（虚拟场地）工资恒为 0。因此农业劳工每档拿 0.2·tier·(6.75−5)，
// 农业农民/工程师的差距分别为 0.2·tier·(6.75−7)（为负 ⇒ 不发）、(6.75−10)（不发），
// 失业者拿全额 0.2·tier·6.75。
//
// 【资金约束】总额受 §4.5.4 可动用资金（现金余额与债务上限）限制；
// 不足时**按比例**裁剪（而不是先到先得），使结果与人群池的遍历顺序无关。
func (s *State) payWelfare(prices []float64) {
	tier := s.Params.WelfareTier
	if tier <= 0 || s.Houses == nil {
		return
	}
	if tier > 6 {
		tier = 6
	}
	rate := 0.2 * float64(tier)
	if rate > 1.2 {
		rate = 1.2
	}
	wbar := model.AverageWage()
	type want struct {
		idx int
		amt float64
	}
	var wants []want
	var total float64
	for i := range s.Houses.Pools {
		p := &s.Houses.Pools[i]
		if p.Population <= 1e-12 {
			continue
		}
		w := model.CohortWages[p.Class]
		if p.Worksite == s.UnemployedSite {
			// 失业人口算劳工（阶级 0），但没有工资 ⇒ 差距 = 全额平均工资。
			w = 0
		} else if p.Worksite >= 0 && p.Worksite < len(s.Buildings) {
			// 【§5 第 20 轮】本人工资取**该场地的劳动结构**：
			// 农业类与庄园的第二阶级是"农民 7 元"，不是"工程师 10 元"。
			w = s.Buildings[p.Worksite].Spec.StructureOf().Wages[p.Class]
		}
		gap := wbar - w
		if gap <= 0 {
			continue
		}
		amt := rate * gap * p.Population
		if amt <= 0 {
			continue
		}
		wants = append(wants, want{idx: i, amt: amt})
		total += amt
	}
	if total <= 0 {
		return
	}
	avail := s.Gov.AvailableCash(prices[fiscal.PowerGoodIndex])
	if avail <= 0 {
		return
	}
	scale := 1.0
	if total > avail {
		scale = avail / total
	}
	legs := make([]book.WelfareLeg, 0, len(wants))
	var payTotal float64
	for _, w := range wants {
		amt := w.amt * scale
		if amt <= 1e-9 {
			continue
		}
		legs = append(legs, book.WelfareLeg{Pool: s.Houses.Account(w.idx), Amount: amt})
		payTotal += amt
	}
	if payTotal <= 0 {
		return
	}
	s.welfarePaidTick = s.Bk().PayWelfare(legs)
}

// stackDemand 返回某条投资栈本 tick 的**建造力需要量**（§4.5.1b 的预算分配依据）。
//
// 计入两项：① 已在建订单的剩余需求；② 本 tick 新立项意向的需求。
// 两者都按 §4.2 的"每工地每 tick 最多投入 SitePowerLimit"裁剪——
// 预算分配应当与实际可推进的速度同尺度，而不是与订单名义总量同尺度。
//
// 公共工程订单（o.PublicWorks）**不计入**：它由国库直接出资，不占用投资池预算。
func (s *State) stackDemand(intents []build.Intent, stack string, specs []model.Building) float64 {
	var total float64
	for i := range s.Orders {
		o := &s.Orders[i]
		if o.PublicWorks {
			continue
		}
		st := o.Stack
		if st != build.StackManor {
			st = build.StackFinance
		}
		if st != stack {
			continue
		}
		want := o.Remaining(s.Buildings)
		if want > s.Params.SitePowerLimit {
			want = s.Params.SitePowerLimit
		}
		if want > 0 {
			total += want
		}
	}
	for _, it := range intents {
		if it.Stack != stack {
			continue
		}
		want := build.PowerNeed(specs, it, s.Params.SitePowerLimit)
		if want > 0 {
			total += want
		}
	}
	return total
}

// developmentTarget 返回公共工程（§4.5.8）当前应投向的开发类建筑下标。
//
// 判据：Category == CatDevelopment 且可建造（BuildCost > 0）且未达等级上限。
// 目前 1.0 只有"建造部门"一个开发类建筑；仓库 / 公共工程落地后会自动成为候选。
// 返回 -1 表示没有可投的目标（此时公共工程不采购）。
func (s *State) developmentTarget() int {
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if !model.IsDevelopment(b.Spec) || b.Spec.BuildCost <= 0 {
			continue
		}
		if cap := build.CapFor(b.Spec, s.Params.ArableCap); !math.IsInf(cap, 1) && b.Level >= cap-1e-9 {
			continue
		}
		return i
	}
	return -1
}

// warehouseExpansionWant 返回本 tick 仓库额度扩建所需的**建造力量**（§4.5.6）。
//
// 规则：贸易量 > 现有级数 × 每级额度时，政府下达仓库扩建订单把容量补上。
//
//	目标级数  = ceil(贸易量 / 每级额度)
//	缺口级数  = 目标级数 − 现有级数 − 在建订单已覆盖的级数
//	本 tick 需要量 = min(缺口级数 × 每级造价, 每工地每 tick 上限)
//
// 返回 0 表示无需扩建（贸易量在额度内，或订单已覆盖缺口）。
// 它进入 ⑩a 的买家分配，故仓库扩建与私人扩建按需求比例分享当期建造力。
func (s *State) warehouseExpansionWant() float64 {
	if s.Params.WarehouseQuotaPerLevel <= 0 || s.tradeVolumeTick <= s.warehouseQuotaTick {
		return 0
	}
	whIdx := model.WarehouseIndex
	cost := s.Buildings[whIdx].Spec.BuildCost
	if cost <= 0 {
		return 0
	}
	wantLevel := math.Ceil(s.tradeVolumeTick / s.Params.WarehouseQuotaPerLevel)
	orderedUnits := 0.0
	for oi := range s.Orders {
		o := &s.Orders[oi]
		if o.PublicWorks && o.BuildingIndex == whIdx {
			orderedUnits += o.Remaining(s.Buildings) / cost
		}
	}
	needUnits := wantLevel - s.Buildings[whIdx].Level - orderedUnits
	if needUnits <= 0 {
		return 0
	}
	want := needUnits * cost
	if want > s.Params.SitePowerLimit {
		want = s.Params.SitePowerLimit // §4.2：每工地每 tick 最多投入 30 建造力
	}
	return want
}

// govBuildOrder 用国库资金为建筑 bi 推进或新建一条**政府订单**（§4.5.8 / §4.5.6）。
//
// 政府订单与私人扩建的三点区别（§4.5.1b）：
//  1. 出资方是**国库**（直接采购建造力，不走投资池，也没有 G6 偿还）；
//  2. 完工等级归**政府**（GovLevel += added）；
//  3. 只可能投向开发类建筑（公共工程 §4.5.8.1）或仓库（额度扩建 §4.5.6）。
//
// wantPower 是本 tick 愿意投入的建造力量（已由调用方按预算与剩余产出裁剪）。
// newOrderUnits 是**新建**订单的等级规模（已有同目标订单时忽略，只推进它）。
//
// 返回：本 tick 实际花掉的国库金额、实际投入的建造力量、新建订单的等级数
// （未新建时为 0——调用方据此统计"本 tick 新立项了多少级"）。
func (s *State) govBuildOrder(bi int, wantPower, powerPrice, newOrderUnits float64) (spent, sold, created float64) {
	if bi < 0 || bi >= len(s.Buildings) || wantPower <= 1e-9 || powerPrice <= 0 {
		return 0, 0, 0
	}
	pr := s.Bk().PurchasePower(wantPower, powerPrice)
	if pr.Amount <= 0 {
		return 0, 0, 0
	}
	// 建造部门本期从**两类买家**收到的货款都要登记（私人 G2 与政府订单），
	// 否则逐建筑汇总对账会漏掉恰好等于政府支出的残差（实测 45,425.96）。
	s.Recon.PowerRevenue += pr.Amount
	powerIdx := fiscal.PowerGoodIndex
	s.recordRecon(powerIdx, func(r *BuildingRecon) { r.PowerNet += pr.Amount })
	// 已有同目标的政府订单 → 继续推进；否则新开一条。
	for oi := range s.Orders {
		o := &s.Orders[oi]
		if !o.PublicWorks || o.BuildingIndex != bi {
			continue
		}
		o.Progress += pr.Qty
		o.Advance += pr.Qty
		return pr.Amount, pr.Qty, 0
	}
	units := math.Max(1, newOrderUnits)
	s.Orders = append(s.Orders, Order{
		BuildingIndex: bi,
		Units:         units,
		Progress:      pr.Qty,
		Advance:       pr.Qty,
		Stack:         build.StackFinance,
		PublicWorks:   true,
	})
	return pr.Amount, pr.Qty, units
}

// fullCapacityCosts 返回某建筑**满编口径**的收入与成本（§3.1 的"实际成本基"）。
//
// 【2026-09-19 第 15 轮订正：改为**名义每级口径**，与契约 §3.1 的定义逐字一致】
//
//	revFull  = 级数 × 单级产出 × 当期售价
//	costFull = 级数 × ( Σ_投入 单级投入量×当期价 + 每级雇佣×平均工资 )
//
// 契约 §3.1 的定义就是"利润率 = 单级收入 /(工资 + Σ q_ij·P_j) − 1，P_j 取当期市价"，
// 即**纯价格-成本口径**，与雇佣率、成交量都无关（价格是 AI 唯一可用的信号）。
//
// 【前值 → 后值】旧实现用"实际收入 / 雇佣率"作满编外推：
//
//	revFull = LastRevenue / hire,  costFull = inputValue / hire + 满编工资
//
// 它只在"收入与投入都随雇佣率等比缩放"时等价。2026-09-19 第 15 轮之后有两处破坏该前提：
//  1. §4.5.8 的公共工程让建造部门在雇佣率趋零时仍有收入（国库照买）⇒ revFull 爆到 1e10%；
//  2. 配给/短缺使实际投入与雇佣率脱钩 ⇒ costFull 可以缩到接近 0 ⇒ 利润率再次炸开。
//
// 实测（10m 口径、10,000 tick）旧式在建造部门给出 8.7e4% 的峰值；名义口径下它等于
// "当期建造力价格 / 当期零利润价 − 1"，与 §3.4 的 P⁰(t) 定义自洽，且天然有界。
//
// 注意：**现金流仍然用实际量**（LastRevenue / LastProfit / 利润归属都不变）——
// 变的只是"AI 决策与 §8.4 判据所使用的利润率"这一信号口径。
func (s *State) fullCapacityCosts(i int, prices []float64) (revFull, costFull float64) {
	b := &s.Buildings[i]
	lv := b.Level
	// 【R86：金矿的产出价格是**外生**的，不在 `prices` 里】
	//
	// `prices` 只有 `model.Goods = 11` 项（商品市场），而金矿的产出下标是
	// `GoldGood = 11` ⇒ 直接索引会 `index out of range [11] with length 11`。
	// 黄金按裁决是"外生价格的商品"，故取 `Params.GoldPrice`。
	if b.Spec.ProducesGold {
		revFull = lv * b.Spec.Recipe.Qty * s.Params.GoldPrice
	} else {
		revFull = lv * b.Spec.Recipe.Qty * prices[b.Spec.Recipe.Output]
	}
	var unitInput float64
	for g, q := range b.Spec.Recipe.Inputs {
		if g >= 0 && g < len(prices) {
			unitInput += q * prices[g]
		}
	}
	// 【§4.5.6】投入按**买家实付**计价：仓库加价与消费税都落在买家身上，
	// 故单位成本 = BuyerWedge × Σ q·P + 每级工资。这与 §3.4 的标定方程
	// p = w·Aᵀp + l 是同一个 w，故开局利润率仍恰为 20%。
	//
	// 【§5 第 20/24 轮：每级工资取**逐建筑** WagePerLevel()，不用全局标准工资表】
	//
	// 本函数算的是利润率**信号**，它必须反映"该部门真实的工资成本"：
	//
	//	城镇类（默认结构）5,000 × 6.75 = 33,750
	//	农业类（谷物 / 棉花）5,000 × 5.65 = 28,250   ← 劳动结构改了，成本就该跟着改
	//
	// 若这里继续用统一标准工资 6.75×LaborPerLevel，会出现**系统性错配**：
	// 农业的真实单位成本已因农民 7 元而下降，信号却按旧工资抬高其成本，
	// 于是**整个农业部门的信号利润率被压低**、投资与用工反而被错误地挤出去。
	//
	// 【历史记录（必须知道，防止再开倒车）】2026-09-19 曾一度把本行改回
	// `LaborPerLevel × model.AverageWage()`，理由是"改后 1,000 tick 判据由 8/9 掉到 5/9"。
	// **那个理由不成立**：判据下降只说明"当时的其他参数与该口径不匹配"，
	// 不能用短期指标回落来推翻一个成本口径——用户裁决（第 24 轮）：
	// "**不能因为短期目标数量减少了就开倒车**；利润率信号口径的工资**不**用统一标准工资表；
	// 农业被过度吸走人力是**其他部门**的问题。"
	// 故本节保持逐建筑口径，并把"其他部门为什么弱"作为独立问题调查。
	costFull = lv * (s.Params.BuyerWedge()*unitInput + b.Spec.WagePerLevel())
	return revFull, costFull
}

// expectedMarginRatio 返回"扩招一步之后的利润率 / 当期利润率"（§5.2，2026-09-19 第 22 轮）。
//
// 推导（唯一假设：产量与雇佣率成正比，价格按 §2.1 的常弹性需求下降）：
//
//	ΔO/O = (HireAnnual/TicksPerYear) / hireRate
//	P'/P = (1 + ΔO/O)^(−1/ε)        ε = 该商品的需求弹性（§3.1）
//	margin' = (q·P' − c) / c = margin · (P'/P)   （每单位成本 c 与雇佣率无关）
//
// 故折现系数就是 P'/P，与工资、投入价格、级数都无关——只取决于弹性与当前雇佣率。
//
// 【边界】雇佣率已满（= 1）⇒ 无法再扩招，返回 0（调用方视作"不增雇"）；
// 弹性缺失/非正、或雇佣率 ≤ 0 ⇒ 返回 1（不做折现，退化为旧口径）。
// `Params.NoExpandDilution` 为 true 时一律返回 1（历史对照臂，见 ACTIVE §七 R46）。
func (s *State) expectedMarginRatio(i int) float64 {
	if s.Params.NoExpandDilution {
		return 1
	}
	if i < 0 || i >= len(s.Buildings) {
		return 1
	}
	b := &s.Buildings[i]
	hire := b.HireRate
	if hire <= 1e-9 {
		return 1
	}
	step := s.Params.HireAnnual / float64(s.Params.TicksPerYear)
	if hire >= 1-step {
		// 已满编：没有扩招空间 ⇒ 折现系数 0 ⇒ 不增雇。
		return 0
	}
	// 前瞻步数（默认 1 = 只折现本 tick 的一步；见 Params.ExpandPlanHorizon）。
	horizon := 1
	if s.Params.ExpandPlanHorizon > 1 {
		horizon = s.Params.ExpandPlanHorizon
	}
	g := b.Spec.Recipe.Output
	if g < 0 || g >= len(s.Goods) {
		return 1
	}
	eps := s.Goods[g].Eps
	if eps <= 0 {
		return 1
	}
	// ΔO/O（相对产量增幅）与折现系数。
	dOut := step * float64(horizon) / hire
	return math.Pow(1+dOut, -1/eps)
}

// producerDepositLegs 构造"生产者 → 仓库"的**入库腿**（§4.5.6，2026-09-19 第 16 轮）。
//
// 每条腿 = 该商品的**过库量** × 生产者售价 × 该生产者的**供给份额**。
//
//	过库量 = 中间投入实际取用量 + 消费者实际购买量（本 tick 已两次结算）
//	供给份额 = 该生产者的当期实际供给 /（全部专业供给 + 自给产出）
//
// 【为什么按供给份额】某一商品的过库量由**专业生产者**与**自给农场**（其所有者是
// 宅邸庄园，§4.5.5）共同供给。入库款必须按实际供给拆分，否则：
//   - 自给产出的货款会全额落到专业生产者账上（R16 缺陷 1 的旧形态）；
//   - 中间投入那一路会全额记到"该商品的唯一专业生产者"账上（R21 记录的缺口）。
//
// 本函数同时关闭这两处 —— 因为入库款是**该商品全部去向**的唯一收款入口。
//
// 【建造力除外】建造力由政府采购（§4.5.3 G2/G6 与 §4.5.8.1 公共工程）直接付款，
// 不经仓库，故这里跳过；它的收入在 ⑥ 由上一 tick 的实际采购量确认。
func (s *State) producerDepositLegs(plan *produce.Plan, outcome consume.PooledOutcome, prices []float64) []book.DepositLeg {
	subs := s.subsistence()
	legs := make([]book.DepositLeg, 0, len(s.Buildings)+3)
	s.lastManorDeposit = 0
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if !b.Spec.Produces() {
			continue
		}
		g := b.Spec.Recipe.Output
		if g == powerGoodIndex || g < 0 || g >= model.Goods {
			continue
		}
		qty := 0.0
		if g < len(plan.UsedInputs) {
			qty += plan.UsedInputs[g]
		}
		if g < len(outcome.Bought) {
			qty += outcome.Bought[g]
		}
		if qty <= 0 {
			continue
		}
		prof := 0.0
		if g < len(plan.ActualOutput) {
			prof = plan.ActualOutput[g] - subs[g]
		}
		if prof < 0 {
			prof = 0
		}
		share := 1.0
		if d := prof + subs[g]; d > 0 {
			share = prof / d
		} else {
			share = 1.0
		}
		net := qty * prices[g] * share
		if net > 0 {
			legs = append(legs, book.DepositLeg{Producer: i, Net: net})
		}
	}
	// 自给农场那一份归宅邸庄园（§4.5.5）：谷物 / 织物 / 服装。
	for g, sub := range subs {
		if sub <= 0 || g == powerGoodIndex || g >= model.Goods {
			continue
		}
		qty := 0.0
		if g < len(plan.UsedInputs) {
			qty += plan.UsedInputs[g]
		}
		if g < len(outcome.Bought) {
			qty += outcome.Bought[g]
		}
		if qty <= 0 {
			continue
		}
		prof := 0.0
		if g < len(plan.ActualOutput) {
			prof = plan.ActualOutput[g] - sub
		}
		if prof < 0 {
			prof = 0
		}
		d := prof + sub
		if d <= 0 {
			continue
		}
		net := qty * prices[g] * sub / d
		if net > 0 {
			legs = append(legs, book.DepositLeg{Producer: model.ManorIndex, Net: net})
			s.lastManorDeposit += net
		}
	}
	return legs
}

// reanchorDerived 按 §3.4 候选方案 C′ 重锚"没有家庭最终需求"的商品。
//
// 【口径】
//   - 中间品（煤/铁/钢/工具等）：派生需求 = Σ_j 等级_j × q_gj（下游**产能**口径，
//     与开局标定用的"满编净供给"同量纲），故下游扩产会等比例抬高它们的需求锚；
//   - 建造力：需求是投资需求，取【在手订单尚未投入的建造力量 **÷ 队列目标周期**】
//     （2026-09-19 第 23 轮改口径，见下）；
//   - 最终品（出现在任一消费组里的商品）不动，仍用 §3.4 步骤 3 的现行锚；
//   - 派生需求为 0 时【回退】到现行锚——锚为 0 会让需求恒为 0、价格失去均衡。
//
// 【第 23 轮改动：建造力锚由"在手订单绝对量"改为"÷ 队列目标周期"】
//
// 旧口径把**存量**（在手订单未投入量）直接当**流量**（每期需求）用，于是产生正反馈：
//
//	队列越长 ⇒ 建造力需求越大 ⇒ 建造部门利润率越高 ⇒ §4.1 自己立项扩产
//	⇒ 产能更大 ⇒ 承接更多订单 ⇒ 队列更长 …
//
// 实测（R45/R46）：建造部门 39 → 860 级、用工 430 万（超过总人口），
// 且它**恒为满编**，故不是 §5.2 的问题；§5.2 的折价规则对它也无效（折现系数恒 0）。
//
// 新口径把存量摊到目标周期上，得到**可消化的每期需求**：
//
//	建造力需求锚 = (Σ 在手订单剩余需求) / QueueWarnTicks      （§4.2 的 52 周期）
//
// 于是"队列越深 ⇒ 需求越高"仍然成立（这是对的：积压确实该多买），但**不再自指放大**：
// 产能一旦追上"52 周期内消化完"的水平，需求就停在那个流量上，价格与利润率随之回落。
func (s *State) reanchorDerived(levels []float64) {
	specs := s.buildingSpecs()

	hasFinalDemand := make([]bool, model.Goods)
	for _, g := range model.ConsumeGroupSpecs() {
		for good := range g.Uses {
			hasFinalDemand[good] = true
		}
	}

	for i := range s.Goods {
		if hasFinalDemand[i] {
			continue
		}
		var anchor float64
		if i == fiscal.PowerGoodIndex {
			backlog := s.powerBacklog()
			if s.Params.PowerAnchorByQueue {
				// 【第 23 轮】存量 ÷ 队列目标周期 = 可消化的每期需求（见函数注释）。
				period := s.Params.QueueWarnTicks
				if period < 1 {
					period = 1
				}
				anchor = backlog / period
			} else {
				// 旧口径：在手订单**绝对量**（存量当流量用 ⇒ 自指正反馈）。
				anchor = backlog
			}
		} else {
			for j, b := range specs {
				if b.IsNonMarket() {
					continue
				}
				if q, ok := b.Recipe.Inputs[i]; ok && j < len(levels) {
					anchor += levels[j] * q
				}
			}
		}
		if anchor <= 0 {
			continue // 回退：保持现行锚
		}
		eps := s.Goods[i].Eps
		if s.Market.UnitElastic {
			eps = 1
		}
		s.Market.SetAnchor(i, calibrate.DemandConstantAt(s.Goods[i], anchor, eps))
	}
}

// recordRecon 在逐建筑对账表上登记一项。
func (s *State) recordRecon(idx int, fn func(*BuildingRecon)) {
	if idx < 0 || idx >= len(s.Recon.ByBuilding) {
		return
	}
	fn(&s.Recon.ByBuilding[idx])
}

// tickRecon 在 tick 末把各建筑的实际 Δ 填入对账表。
func (s *State) tickRecon() {
	for i := range s.Buildings {
		if i >= len(s.Recon.ByBuilding) || i >= len(s.reconBefore) {
			break
		}
		s.Recon.ByBuilding[i].Actual = s.bal(i) - s.reconBefore[i]
	}
}

// debitPayer 与 SellPower 【已随 §4.5.3 改写删除】。
//
// 旧口径里政府先整批采购建造力、再把它转售给扩建方，转售环节要另收一道交易税，
// 于是需要一个"从付款方账户扣税"的落地点。G6 改为"投资池全额付给政府 + 建造力
// 交易不计税"之后，政府不再是建造力的卖方，这笔税与这个扣款点都不存在了。

// distributeConsumerRevenue 【已删除】。
//
// 它曾把消费者净支付额按购买量占比重算一遍，然后直接 `BuildingCash(idx).Add(amount)`
// 并登记逐建筑对账。这是【危险的空转代码】：
//
//   - 记账职责已在 ④b 由 book.Consume 完成（那里借贷两侧写在同一处）；本函数若被调用，
//     就会把同一笔钱贷记两次，直接破坏货币守恒；
//   - 它重算的分摊与 book.Consume 的"残差归最后一家"规则平行存在，
//     一旦口径分叉，逐建筑对账会挂上假残差，而全局守恒审计仍然全绿——
//     缺陷因此被"全局不变量通过"掩盖（实测逐建筑残差 213 万，全局却全绿）。
//
// 现在改为：book.Consume 返回它【实际贷记的逐卖方金额】，sim 只登记这份"记账事实"，
// 不再重算。空转的实现连同它的重算逻辑一并删除，避免日后被误接回去。

// payIntermediate 已废弃：中间投入交易改由 book.PayIntermediate 统一过账。
//
// 旧实现把借贷两侧【分两处写】（买方在一处扣、卖方在另一处入），
// 虽然两处都在，但绕过 Post 校验，因此无法保证 Σ借 == Σ贷。
// 现在 step 的 ⑤ 步直接调用 book.PayIntermediate，本函数保留仅为兼容签名。
//
// Deprecated: 使用 book.PayIntermediate。
func (s *State) payIntermediate(buyer int, need float64) (net, tax float64) {
	sellerOf := func(b int) (int, bool) {
		for good := range s.Buildings[b].Spec.Recipe.Inputs {
			for j := range s.Buildings {
				if s.Buildings[j].Spec.IsNonMarket() {
					continue
				}
				if s.Buildings[j].Spec.Recipe.Output == good {
					return j, true
				}
			}
		}
		return -1, false
	}
	n, t, _, _ := s.Bk().PayIntermediate(
		[]book.IntermediateLeg{{Buyer: buyer, Need: need}}, s.Params.TaxRate, sellerOf)
	return n, t
}

// powerBacklog 返回在手订单**尚未投入**的建造力总量（§3.4 方案 C′ / §4.2）。
//
// 它是两个第 23 轮规则的共同输入：
//   - 建造力需求锚 = 本值 ÷ QueueWarnTicks（`reanchorDerived`）；
//   - 建造部门产能缺口 = 由本值反解"52 周期内能消化完"的级数（`build.PowerCapacityUnits`）。
func (s *State) powerBacklog() float64 {
	specs := s.buildingSpecs()
	var backlog float64
	for _, o := range s.Orders {
		if o.BuildingIndex < 0 || o.BuildingIndex >= len(specs) {
			continue
		}
		need := specs[o.BuildingIndex].BuildCost*o.Units - o.Progress
		if need > 0 {
			backlog += need
		}
	}
	return backlog
}

// queueTicks 估算当前队列（含本 tick 新立项）的完成时间（周期数）。
//
// 【§4.2】队列完成时间超过 QueueWarnTicks（52）时，在金融栈队首插入一条
// 建造部门扩建订单：建造力供给跟不上扩建需求，是先扩建造部门的唯一信号。
// 估算口径 = 在手全部剩余需求 + 本期新立项目需求，除以当期建造力产出。
func (s *State) queueTicks(manor, finance []build.Intent, powerOut float64) float64 {
	if powerOut <= 1e-9 {
		return math.Inf(1)
	}
	var total float64
	for i := range s.Orders {
		if r := s.Orders[i].Remaining(s.Buildings); r > 0 {
			total += r
		}
	}
	for _, list := range [][]build.Intent{manor, finance} {
		for _, it := range list {
			if it.BuildingIndex < 0 || it.BuildingIndex >= len(s.Buildings) {
				continue
			}
			total += s.Buildings[it.BuildingIndex].Spec.BuildCost * it.Units
		}
	}
	return total / powerOut
}

// completeOrders 处理达到成本门槛的订单（§8-13）。
//
// 【§4.5.1b 定案：谁出资、谁拥有】新增等级**全部**计入被扩建建筑，
// 并全部记为**私有**（PrivLevel += added；GovLevel 不变）——它们归出资的资本建筑
// （庄园栈 → 宅邸庄园；金融栈 → 金融区），政府不按 s_gov 分得新增等级。
// 政府持股比例因此被**稀释**，§4.5.1 的分红仍按当期持股计算。
func (s *State) completeOrders() {
	var kept []Order
	var arableUsed float64
	for i := range s.Buildings {
		if s.Buildings[i].Spec.LandKind == "arable" {
			arableUsed += s.Buildings[i].Level
		}
	}
	for _, o := range s.Orders {
		bi := o.BuildingIndex
		if bi < 0 || bi >= len(s.Buildings) {
			continue
		}
		b := &s.Buildings[bi]
		need := b.Spec.BuildCost * o.Units
		if o.Progress < need-1e-9 {
			kept = append(kept, o)
			continue
		}
		added := build.ApplyCompletion(
			&b.Spec, o.Units,
			s.Params.ArableCap, arableUsed, b.Level,
		)
		if added <= 0 {
			s.BlockedBuilds++
			continue
		}
		// 【前值 → 后值】旧口径把 added 按 govShare 拆成 (addGov, addPriv) 两腿；
		// 本次改为"全部归出资资本建筑"⇒ addGov ≡ 0、addPriv ≡ added。
		//
		// 【§4.5.8 公共工程】出资方是国库，故完工等级归**政府**（GovLevel）——
		// 这是"谁出资、谁拥有"在政府支出端的对称表述。
		if o.PublicWorks {
			b.GovLevel += added
		} else {
			b.PrivLevel += added
		}
		b.Level += added
		// §4.3：新建筑的营运本金。这是系统里【唯一】允许的货币创造，
		// 故必须单独计量（NewCapital），否则货币守恒审计会把它误判为漏出。
		injected := added * 5000
		s.Bk().NewCapital(bi, injected)
		s.tickNewCapital += injected
		s.totalNewCapital += injected
		s.recordRecon(bi, func(r *BuildingRecon) { r.NewCapital += injected })
		if b.Spec.LandKind == "arable" {
			arableUsed += added
		}
		if added < o.Units {
			s.BlockedBuilds++
		}
	}
	s.Orders = kept
}

func (s *State) levels() []float64 {
	out := make([]float64, len(s.Buildings))
	for i := range s.Buildings {
		out[i] = s.Buildings[i].Level
	}
	return out
}
func (s *State) hireRates() []float64 {
	out := make([]float64, len(s.Buildings))
	for i := range s.Buildings {
		out[i] = s.Buildings[i].HireRate
	}
	return out
}
func (s *State) buildingSpecs() []model.Building {
	out := make([]model.Building, len(s.Buildings))
	for i := range s.Buildings {
		out[i] = s.Buildings[i].Spec
	}
	return out
}

// govShare 返回某建筑**当期**的政府持股比例 s_gov = GovLevel / Level（§4.5.1）。
//
// 【2026-09-19 口径】分红与亏损承担都用**当期**持股：
// 扩建把新增等级全部记入私有一侧（§4.5.1b），故 s_gov 随扩建被**稀释**；
// §4.5.1a 的私有化是方向相反的股权转让。
//
// 边界情形：
//   - 金融区与宅邸庄园是非市场建筑 ⇒ 恒 0（它们 100% 私有）；
//   - Level ≤ 0 ⇒ 回退到 Params.GovInitialShare（默认 0.30）。
//
// 【前值 → 后值】旧实现回退值是硬编码 0.70；本次改为 Params.GovInitialShare
// （与 §4.5.1 把初始政府持股下调到 0.30 同步），避免回退口径与契约脱节。
func (s *State) govShare(i int) float64 {
	if i < 0 || i >= len(s.Buildings) {
		return 0
	}
	if s.Buildings[i].Spec.IsNonMarket() {
		return 0
	}
	if s.Buildings[i].Level <= 0 {
		return s.Params.GovInitialShare
	}
	return s.Buildings[i].GovLevel / s.Buildings[i].Level
}

// flowInput 汇总 fillFlow 的入参，避免长参数列表错位。
type flowInput struct {
	govOperating  float64
	capitalIncome float64
	// manorIncome 是归宅邸庄园的收入（农业私人份额纯利 + 自给产出货款）。
	manorIncome float64
	wageBill    float64
	spendNet    float64
	powerOut    float64
	margins     []float64
	consumerTax float64
	inputNet    float64
	inputTax    float64
	sat         [4]float64
}

// fillFlow 记录本期资金流分解。
//
// 这一步是诊断的核心：政府现金池的变化必须能由
//
//	税收 + 经营净额 + 售力收入 − 采购支出 − 政府自有项目付款
//
// 完全解释，否则说明有未记账的资金漏出。
func (s *State) fillFlow(in flowInput) {
	// 【税收口径】Gov.TaxCollected 由各交易处【同步累加】（消费税 ④b、
	// 中间投入税 ⑤），故这里直接使用它，不再另行汇总——否则会与同步累加重复计数。
	//
	// 【§4.5.3 改写后的分解式】建造力交易不计税、也没有自反税腿，故
	//
	//	Δ政府 = GovTax + GovOperating + GovPowerRevenue
	//	        − GovPowerSpend − GovSubsidy
	//
	// 其中 GovPowerRevenue 是投资池偿还的货款（G6），GovPowerSpend 是 G2 的采购支出。
	// 二者在本版口径下**逐位相等**（政府净支出为 0，本版无政府自有项目）。
	f := FlowDiag{
		GovOperating: s.flowGovOperatingDelta,
		GovTax:       s.Gov.TaxCollected,
		// GovPowerSpend 是政府现金池为采购建造力【实际流出】的金额
		// （= 采购量 × 价格；建造力不计税，故它就是全额）。
		GovPowerSpend:   s.flowGovPurchaseDelta,
		GovPowerRevenue: s.flowGovInvestDelta,
		CapitalProfit:   in.capitalIncome,
		ManorProfit:     in.manorIncome,
		WageTotal:       in.wageBill,
		SpendNet:        in.spendNet,
		PowerOut:        in.powerOut,
		PowerBought:     s.Gov.PowerPurchased,
		PowerSold:       s.Gov.PowerSold,
		PowerNeed:       s.powerNeedTick,
		PowerAvail:      s.powerAvailTick,
		ConsumerTax:     in.consumerTax,
		InputNet:        in.inputNet,
		InputTax:        in.inputTax,
		GovSubsidy:      s.subsidyPaidTick,
		GovWelfare:      s.welfarePaidTick,
		GovPublicWorks:  s.publicWorksSpendTick,
		HouseSaving:     s.savingTick,
		SavingsToInvest: s.savingInvestTick,
		Happiness:       s.Last.Happiness(),
		NoIncomePools:   s.Last.NoIncome,
		Sat:             in.sat,

		InvestmentInflowManor:   s.tickInflowManor,
		InvestmentInflowFinance: s.tickInflowFinance,
		InvestmentPaid:          s.tickInvestmentPaid,
		InvestmentPool:          s.balInvest(),
		KManor:                  s.KManor,
		KFinance:                s.KFinance,
		PowerPaidManor:          s.powerPaidManor,
		PowerPaidFinance:        s.powerPaidFinance,

		PrivatizeEnabled: s.Params.PrivatizeEnabled,
		PrivatizeUnits:   s.privatizeUnits,
		PrivatizePaid:    s.privatizePaid,
		GovShareAfter:    govShareWeighted(s),

		// ===== §4.5.6 仓库与消费代理（2026-09-19 第 16 轮）=====
		TradeVolume:          s.tradeVolumeTick,
		TradeQuota:           s.warehouseQuotaTick,
		WarehouseIn:          s.warehouseGoodsInTick,
		WarehouseOut:         s.warehouseGoodsOutTick,
		VATCollected:         s.warehouseVATTick,
		ConsumeTaxCollected:  s.warehouseConsumeTaxTick,
		WarehouseCash:        s.bal(model.WarehouseIndex),
		AgentCash:            s.bal(model.AgentIndex),
		WarehouseExpandSpend: s.warehouseExpandSpendTick,
		WarehouseExpandUnits: s.warehouseExpandUnitsTick,
		GovWarehouseExpand:   s.warehouseExpandSpendTick,
	}
	if s.Houses != nil {
		f.HouseCash = s.Houses.TotalCash()
	}
	// 幸福度按阶级拆分（§6.5，第 15 轮裁决）：失业池算劳工，
	// 故"劳工"一列会随失业规模下降，这正是要能观察到的量。
	for c := 0; c < 3; c++ {
		f.HappinessByClass[c] = s.Last.HappinessByClass(c)
	}
	f.WorstSector, f.WorstMargin = -1, 0
	f.BestSector, f.BestMargin = -1, 0
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsNonMarket() || i >= len(in.margins) {
			continue
		}
		if f.WorstSector < 0 || in.margins[i] < f.WorstMargin {
			f.WorstSector, f.WorstMargin = i, in.margins[i]
		}
		if f.BestSector < 0 || in.margins[i] > f.BestMargin {
			f.BestSector, f.BestMargin = i, in.margins[i]
		}
	}
	s.Flow = f
}

// govShareWeighted 返回按级数加权的政府持股比例（私有化与扩建稀释诊断用）。
//
// 口径：Σ GovLevel / Σ Level，金融区与宅邸庄园不参与（它们本来就是私有的）。
func govShareWeighted(s *State) float64 {
	var govLv, total float64
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsNonMarket() {
			continue
		}
		govLv += s.Buildings[i].GovLevel
		total += s.Buildings[i].Level
	}
	if total <= 1e-12 {
		return 0
	}
	return govLv / total
}

func (s *State) snapshot(
	plan *produce.Plan,
	margins, marginEMA, profitEMA, profitMargins []float64,
	wageBill, manorDepositIn float64,
) *Snapshot {
	prices := s.Market.Prices()
	// 现金池总额（契约 §7 修订口径）：
	//   建筑现金池 + max(0, 政府现金池) + 资本现金池 + 人群现金池 + 投资池 + 储蓄账户
	//
	// 政府现金池为负时是债务，不计入（否则一笔举债会被扣两次，使 GDP 必然为负）。
	// 人群现金池是 §5.1 修订新增的持有主体，属于居民部门，必须计入。
	// 投资池（§4.5.1b）是第 5 类账户，钱仍在系统内部，故计入 GDP 的存量口径。
	// 储蓄固定账户（§5.3 第 20 轮）是居民结余的过手账户，同样在系统内部——
	// 默认 σ_save = 1 时它每 tick 归零，但 σ<1 或未结转时必须计入，否则 GDP 漏计。
	var buildingCash float64
	for i := range s.Buildings {
		buildingCash += s.bal(i)
	}
	govCashPositive := s.balGov()
	if govCashPositive < 0 {
		govCashPositive = 0
	}
	houseCash := 0.0
	if s.Houses != nil {
		houseCash = s.Houses.TotalCash()
	}
	cashTotal := buildingCash + govCashPositive + s.balCap() + houseCash + s.balInvest() + s.balSavings()

	snap := &Snapshot{
		Tick:            s.Tick,
		Prices:          prices,
		Supply:          plan.ActualOutput,
		Demand:          make([]float64, len(prices)),
		Margins:         margins,
		MarginsEMA:      marginEMA,
		ProfitEMAs:      profitEMA,
		ProfitMargins:   profitMargins,
		Levels:          s.levels(),
		GovCash:         s.balGov(),
		CapitalCash:     s.balCap(),
		ManorCash:       s.bal(model.ManorIndex),
		SubsistenceHire: s.SubsistenceHireRate,
		Unemployed:      s.Unemployed,
		HouseCash:       houseCash,
		Tax:             s.Gov.TaxCollected,
		PowerPurchased:  s.Gov.PowerPurchased,
		PowerSold:       s.Gov.PowerSold,
		PowerNeed:       s.powerNeedTick,
		PowerInventory:  s.Gov.PowerInventory,
		Population:      s.Population,
		Sat:             s.Last.Sat,
		SpendNet:        s.Last.SpendNet,
		WageBill:        wageBill,
		CashTotal:       cashTotal,
		TotalMoney:      s.TotalMoney(),
		NewCapital:      s.tickNewCapital,
		GovDebt:         s.Gov.Debt(),
		GovDebtCap:      s.Gov.DebtCap(s.powerPriceNow),
		GovPowerOutput:  s.Gov.PowerOutput,
		// 私有化诊断（§4.5.1 修订）
		PrivatizeUnits: s.privatizeUnits,
		PrivatizePaid:  s.privatizePaid,
		GovShareAfter:  govShareWeighted(s),
		// §4.5.1b 投资池与两条栈
		InvestmentPool:          s.balInvest(),
		KManor:                  s.KManor,
		KFinance:                s.KFinance,
		InvestmentInflowManor:   s.tickInflowManor,
		InvestmentInflowFinance: s.tickInflowFinance,
		InvestmentPaid:          s.tickInvestmentPaid,
		ManorConsumerIn:         manorDepositIn,
		// 2026-09-19 第 15 轮裁决新增的资金流与口径
		Saving:           s.savingTick,
		SavingInvest:     s.savingInvestTick,
		SavingsAccount:   s.balSavings(),
		AcquirePaid:      s.privatizePaid,
		Welfare:          s.welfarePaidTick,
		PublicWorks:      s.publicWorksSpendTick,
		PublicWorksUnits: s.publicWorksUnitsTick,
		NoIncomePools:    s.Last.NoIncome,
		Happiness:        s.Last.Happiness(),
		BudgetShareManor: s.budgetShareManor,
		ShortageStart:    s.ShortageStart,
		Infusion:         s.infusionTotal,
		// ===== §4.5.6 仓库与消费代理（2026-09-19 第 16 轮）=====
		TradeVolume:          s.tradeVolumeTick,
		TradeQuota:           s.warehouseQuotaTick,
		WarehouseCash:        s.bal(model.WarehouseIndex),
		AgentCash:            s.bal(model.AgentIndex),
		WarehouseLevel:       s.Buildings[model.WarehouseIndex].Level,
		WarehouseIn:          s.warehouseGoodsInTick,
		WarehouseOut:         s.warehouseGoodsOutTick,
		WarehouseProfit:      s.warehouseProfitTick,
		WarehouseWage:        s.warehouseWageTick,
		VAT:                  s.warehouseVATTick,
		ConsumeTax:           s.warehouseConsumeTaxTick,
		WarehouseExpandSpend: s.warehouseExpandSpendTick,
		WarehouseExpandUnits: s.warehouseExpandUnitsTick,
		// §7.2 实际工农生产总值（固定 P_ref 计价，与货币量无关）
		GrossAgri:            s.productTick.GrossAgri,
		GrossIndustry:        s.productTick.GrossIndustry,
		GrossProduct:         s.productTick.Gross,
		ProductInput:         s.productTick.Inter,
		ProductAdded:         s.productTick.Added,
		ProductAddedAgri:     s.productTick.AddedAgri,
		ProductAddedIndustry: s.productTick.AddedIndustry,
		ProductPerCapita:     s.productTick.PerCapita,
		ProductIndex:         product.Index(s.productTick.Added, s.productBaseAdded),
		ProductBaseAdded:     s.productBaseAdded,
		ProductSubsistence:   s.productTick.Subsistence,
		Flow:                 s.Flow,
	}
	for c := 0; c < 3; c++ {
		snap.HappinessByClass[c] = s.Last.HappinessByClass(c)
	}
	snap.PriceRatio = make([]float64, len(prices))
	snap.Pzero = make([]float64, len(prices))
	for i := range prices {
		// §七 R32：比值分母走 s.Market.PriceRatio —— 动态模式下是**当期**零利润价。
		snap.PriceRatio[i] = s.Market.PriceRatio(i)
		snap.Pzero[i] = s.Market.Markets[i].Pzero
		snap.Demand[i] = s.Market.Markets[i].Demand
	}
	snap.OverdraftTick = s.Gov.Cash.OverdraftTicks + s.Cap.Cash.OverdraftTicks
	return snap
}

// Run 连续推进 n 个 tick 并返回全部快照。
func (s *State) Run(n int) ([]*Snapshot, error) {
	out := make([]*Snapshot, 0, n)
	for i := 0; i < n; i++ {
		snap, err := s.Step()
		if err != nil {
			return out, fmt.Errorf("tick %d: %w", s.Tick, err)
		}
		out = append(out, snap)
	}
	return out, nil
}
