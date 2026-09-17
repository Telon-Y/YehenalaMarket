package sim

import (
	"fmt"

	"yehenala/market/internal/book"
	"yehenala/market/internal/build"
	"yehenala/market/internal/cohort"
	"yehenala/market/internal/consume"
	"yehenala/market/internal/fiscal"
	"yehenala/market/internal/ledger"
	"yehenala/market/internal/model"
	"yehenala/market/internal/produce"
)

// Step 推进一个 tick，严格按契约 §8 的管线（含架构 M1–M3 修正）。
//
// 管线顺序与理由：
//
//	① 生产与配给（§8-2）               —— 由当前 level 与 hireRate 决定
//	② 价格结算（§8-8/9，M1 提前）        —— 消费者用【本 tick】的价格购买
//	③ 工资（M2 显式 wagePool）          —— 工资在同一 tick 全部支出
//	④ 消费（§8-4）                     —— 受"工资是唯一资金来源"约束
//	⑤ 收入结算与利润分账（§8-3 + G3/G4） —— 政府/私有按所有权分账
//	⑥ 征税（G1）                       —— 对全部交易额
//	⑦ 政府采购建造力（G2）              —— 政府向建造部门购买
//	⑧ 扩建意向（§8-10）                —— 用 margin EMA，消除内生性（M3）
//	⑨ 出售建造力给扩建方（G2/G6）        —— 货币回流政府，闭环成立
//	⑩ 完工（§8-6）
//	⑪ 缩编（§8-7）
//	⑫ 人口（§8-11）、雇佣调整（§8-1）
func (s *State) Step() (*Snapshot, error) {
	// ⓪ 重置本 tick 的流量计数器。
	//
	// 政府与资本的流量字段（税收、经营净额、采购/售出建造力、转移）都是"每 tick 流量"，
	// 必须在 tick 开头清零。若不清零，它们会跨 tick 累加成存量，使
	// "售出 ≤ 采购" 这类不变量检查与全部报告失真。
	s.Gov.ResetFlow()
	s.Cap.ResetFlow()
	s.Gov.TaxRate = s.Params.TaxRate
	s.tickNewCapital = 0
	s.Recon = Recon{}
	s.flowGovOperatingDelta, s.flowGovBuildoutDelta, s.flowGovSaleDelta = 0, 0, 0
	s.flowGovBuildoutSelfTax = 0
	s.flowGovPurchaseSelfTax = 0
	s.powerSpendActual = 0

	levels := s.levels()
	hire := s.hireRates()
	specs := s.buildingSpecs()

	var buildingDeltaBefore float64
	for i := range s.Buildings {
		buildingDeltaBefore += s.bal(i)
	}
	govDeltaBefore := s.balGov()
	capDeltaBefore := s.balCap()
	houseDeltaBefore := s.Houses.TotalCash()
	// 逐建筑对账基准（诊断用）
	s.reconBefore = make([]float64, len(s.Buildings))
	for i := range s.Buildings {
		s.reconBefore[i] = s.bal(i)
	}
	s.Recon.ByBuilding = make([]BuildingRecon, len(s.Buildings))

	// ① 生产与配给
	plan := produce.Settle(specs, levels, hire, s.subsistence())

	// ② 价格结算：用实际产出 S（含短缺惩罚）驱动 E = D − S
	s.Market.SettleAll(plan.ActualOutput, nil)

	// ③ 工资：从建筑现金池【实际划转】到人群现金池（§5.1 修订）
	prices := s.Market.Prices()
	wages := produce.WageBill(specs, levels, hire)
	totalWage := produce.TotalWage(wages)
	// 人群池的人口是本 tick 的【流量】，先清零再随工资划转登记。
	// 现金是存量，保留。注意顺序：必须在 payWages 之前 Reset，
	// 否则会把刚划入的工资人口清掉。
	s.Houses.Reset()
	s.payWages(levels, hire, wages)

	// ④ 消费（§5.1：按人群池逐个结算，每池用自己的现金池）
	groups := model.ConsumeGroupSpecs()
	poolSpecs := make([]consume.PoolSpec, len(s.Houses.Pools))
	budgets := s.Houses.Budgets(s.Params.TaxRate)
	for i := range s.Houses.Pools {
		poolSpecs[i] = consume.PoolSpec{
			Population: s.Houses.Pools[i].Population,
			Tier:       s.Houses.WealthTier(i),
			Budget:     budgets[i],
		}
	}
	outcome := consume.PurchaseByPools(groups, poolSpecs, plan.NetSupply, prices, s.demandScale)
	s.Last = outcome

	// ④b 消费扣款与入账（全过程交易税，§4.5.3 修订）
	//
	// 每一笔消费都是一笔【借贷相等】的交易：
	//
	//	借 人群池[pi]     Gross = net·(1+t)
	//	贷 建筑[卖方]     net        （按各商品购买量占比分摊）
	//	贷 政府           tax
	//
	// 这是"居民真的有资产负债表"的落地处：钱从居民池实际减少，
	// 卖方与政府同时入账，总量不变。
	// 【统一记账簿】消费走 book.Consume——借贷两侧在同一处构造，
	// 卖方净额按占比分摊、残差归最后一家保证逐位等额。
	//
	// 逐池收集 legs：每个池的可消费额由 cohort.Spend 按"现金是否够"裁剪，
	// 它只做可行性判断、不改动余额——扣款由 book.Consume 一次性完成。
	var consumerLegs []book.ConsumerLeg
	for pi := range s.Houses.Pools {
		net := outcome.Pools[pi].SpendNet
		if net <= 0 {
			continue
		}
		paid, _, ok := s.Houses.Spend(pi, net, s.Params.TaxRate)
		if !ok {
			continue
		}
		consumerLegs = append(consumerLegs, book.ConsumerLeg{
			Pool: s.Houses.Account(pi),
			Net:  paid,
		})
	}
	sellerAcc, sellerShare := s.consumerSellerShares(outcome)
	consumerNet, consumerTax, consumerCredited := s.Bk().Consume(consumerLegs, s.Params.TaxRate, sellerAcc, sellerShare)
	s.Recon.ConsumerRevenue = consumerNet
	// 逐建筑登记【记账簿实际贷记的金额】——不在此处重算分摊，
	// 否则两处残差归集规则不一致会产生假的逐建筑残差（实测 213 万，见 book.Consume 注释）。
	for idx := range s.Buildings {
		if amt := consumerCredited[ledger.Building(idx)]; amt != 0 {
			s.recordRecon(idx, func(r *BuildingRecon) { r.ConsumerIn += amt })
		}
	}
	// 【税收口径】book.Consume 已把税额真实打进政府账（那是交易的一半），
	// 但它不维护 Gov.TaxCollected——那是 sim 的统计口径。
	// 若此处不同步，报告里的"税收"就会漏掉消费税（实测漏掉 201,290/39422）。
	s.Gov.TaxCollected += consumerTax

	// ⑤ 中间投入交易（全过程交易税，§4.5.3 修订）
	//
	// 投入品从卖方建筑划到买方建筑，买方支付含税额，税额入政府。
	// 买方的现金池因此实际减少——这是"上游恒亏"能被真实约束住的前提。
	inputValue := produce.InputValue(specs, levels, hire, prices, plan.AllocRatio)
	var intermediateLegs []book.IntermediateLeg
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsFinance || inputValue[i] <= 0 {
			continue
		}
		intermediateLegs = append(intermediateLegs, book.IntermediateLeg{
			Buyer: i,
			Need:  inputValue[i],
		})
	}
	// sellerOf 返回该买方的投入品生产者（§3.3 里每种商品只有一类建筑生产）。
	// 建造力不是任何建筑的投入品，故只可能落在其他商品上。
	sellerOf := func(buyer int) (int, bool) {
		for good := range s.Buildings[buyer].Spec.Recipe.Inputs {
			for j := range s.Buildings {
				if s.Buildings[j].Spec.IsFinance {
					continue
				}
				if s.Buildings[j].Spec.Recipe.Output == good {
					return j, true
				}
			}
		}
		return -1, false
	}
	inputNet, inputTax, inputPaid, inputIncome := s.Bk().PayIntermediate(intermediateLegs, s.Params.TaxRate, sellerOf)
	s.Recon.IntermediateIn = inputNet
	s.Recon.IntermediateOut = inputNet + inputTax
	// 逐建筑登记【记账簿实际借记/贷记的金额】，不重算：
	//   InputOut 必须是该买方实际支付的【含税总额】（买方现金不足时会被缩减），
	//   而不是理论申报额 inputValue——两者在现金不足时会分叉。
	for idx, amt := range inputPaid {
		if amt != 0 {
			s.recordRecon(idx, func(r *BuildingRecon) { r.InputOut += amt })
		}
	}
	for idx, amt := range inputIncome {
		if amt != 0 {
			s.recordRecon(idx, func(r *BuildingRecon) { r.InputIn += amt })
		}
	}
	// 同 ④b：中间投入的税也要同步进 Gov.TaxCollected。
	s.Gov.TaxCollected += inputTax

	// ⑥ 收入结算与利润分账（消除重复记账，§4.5.1 修订）
	//
	// 【必须遵守的不变量】一笔利润只在三个归属之间【划分】，三份额之和恒等于利润：
	//
	//	政府份额 = 利润 × govShare          → 政府现金池
	//	建筑留存 = 利润 × (1−govShare) × RetainRatio → 建筑现金池（供自身扩建）
	//	资本份额 = 利润 × (1−govShare) × (1−RetainRatio) → 资本现金池
	//
	// 修订前实现为 s.BuildingCash(i).Add(b.LastProfit)（建筑拿全额）＋政府/资本池再各拿一份，
	// 实测 Δ货币/Σ利润 = 2.00，即单笔利润产生双倍记账（docs/AUDIT-1.0.md §4）。
	powerIdx := fiscal.PowerGoodIndex
	salesVolume := make([]float64, model.Goods)
	for i := range plan.UsedInputs {
		salesVolume[i] = plan.UsedInputs[i]
	}
	for i := range outcome.Bought {
		salesVolume[i] += outcome.Bought[i]
	}
	// 建造力是唯一由政府采购而非消费者购买的商品，其销量由政府实际采购量补上。
	// 注意用【实际采购量】而非潜在产出：政府买不起的部分不算收入，
	// 否则建造部门会拿到一笔无人支付的"收入"，凭空造币。
	salesVolume[powerIdx] += s.Gov.PowerPurchased

	margins := make([]float64, len(specs))
	profits := make([]float64, len(specs))
	var capitalIncome, govOperating float64

	for i := range s.Buildings {
		b := &s.Buildings[i]
		if b.Spec.IsFinance {
			continue
		}
		saleValue := salesVolume[b.Spec.Recipe.Output] * prices[b.Spec.Recipe.Output]
		b.LastRevenue = saleValue
		b.LastProfit = saleValue - inputValue[i] - wages[i]
		profits[i] = b.LastProfit

		// margin 用【满编】口径：利润与收入同比例于 hireRate，故二者相除消去 hireRate。
		h := hire[i]
		if h <= 1e-9 {
			h = 1e-9
		}
		costFull := inputValue[i]/h + levels[i]*b.Spec.LaborPerLevel*model.AverageWage()
		revFull := b.LastRevenue / h
		if costFull > 1e-9 {
			margins[i] = (revFull - costFull) / costFull
		}

		// 利润划分：【统一记账簿】负责三份额的计算与过账，
		// 借贷两侧在同一处构造，故"三份额之和 ≠ 利润"在结构上不可能发生。
		share := s.govShare(i)
		gov, capital, retain := s.Bk().ProfitSplit(i, b.LastProfit, share, s.Params.RetainRatio)
		govOperating += gov
		capitalIncome += capital
		s.Recon.RetainedProfit += retain
		leg := retain - b.LastProfit // 现金池实际净腿 = −(政府份额 + 资本份额)
		s.Recon.ProfitLegTotal += leg
		s.recordRecon(i, func(r *BuildingRecon) {
			r.Retained += retain
			r.ProfitLeg += leg
		})
	}
	// 金融区自身：收入 = 资本纯利，成本 = 金融区工资。
	//
	// 金融区不在上面的循环里（它没有商品产出），但它确实持有资本纯利这笔钱，
	// 并把它转给资本池。用【统一记账簿】的利润划分表达这笔转移：
	//
	//	借 建筑[金融区]   capitalIncome
	//	贷 资本池         capitalIncome      （govShare = 0、retainRatio = 0）
	//
	// 加上 step③ 已从金融区划走的工资，金融区净额 = capitalIncome − 工资 = finProfit，
	// 与下面的 LastProfit 一致。
	{
		fin := &s.Buildings[model.FinanceIndex]
		finProfit := capitalIncome - wages[model.FinanceIndex]
		fin.LastProfit = finProfit
		if wages[model.FinanceIndex] > 1e-9 {
			margins[model.FinanceIndex] = finProfit / wages[model.FinanceIndex]
		}
		if capitalIncome != 0 {
			// govShare=0 且 retainRatio=0 ⇒ 全部划给资本池
			s.Bk().ProfitSplit(model.FinanceIndex, capitalIncome, 0, 0)
		}
		s.Recon.FinanceProfit = finProfit
		// 【单一口径】金融区净额必须计入 RetainedProfit 汇总，
		// 否则汇总对账会少算它整笔（逐建筑对账则会正确，两者不一致）。
		s.Recon.RetainedProfit += finProfit
		// 金融区的利润划分腿：ProfitSplit(FinanceIndex, capitalIncome, 0, 0)
		// 的净腿 = 留存 − 利润 = 0 − capitalIncome = −capitalIncome。
		// 它的工资已在 payWages 登记，故此处只记利润腿，避免与工资项重复。
		s.Recon.ProfitLegTotal += -capitalIncome
		s.recordRecon(model.FinanceIndex, func(r *BuildingRecon) {
			r.ProfitLeg += -capitalIncome
		})
		s.Cap.OperatingProfit = capitalIncome
		s.Cap.WageBill = wages[model.FinanceIndex]
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
	// 政府份额已经由上面的 s.Bk().ProfitSplit(...) 作为贷方入账——
	// 那是"利润划分"这笔交易的一半。若此处再 s.Gov.Cash.Add(govOperating)，
	// 政府份额就被记了两遍，正是历史上"Δ货币/Σ利润 = 2.00"那类重复记账的复发。
	//
	// 这里只登记资金流分解所需的分项，不再动任何余额。
	s.flowGovOperatingDelta = govOperating

	// ⑦ 私有化（§4.5.1 修订）
	//
	// 契约语义："对于盈利建筑，市场可以私有化"。
	//
	// 两个层次的开关：
	//   - 总开关 Params.PrivatizeEnabled：关闭时整个机制不运行；
	//   - 逐建筑开关 Building.AllowPrivatize：单个种类可独立配置，
	//     例如谷物农场/住房保持政府控制，竞争性行业允许私有化。
	//
	// 三个约束共同决定实际转让量：
	//   ① 该建筑盈利（marginEMA > PrivatizeMargin）；
	//   ② 政府持股仍 > 0；
	//   ③ 资本池付得起对价。
	//
	// 记账：借 资本池、贷 政府（book.Privatize），股票与现金同时易手，
	// 货币总量不变——只是持有主体从政府换成资本。
	s.privatizeUnits, s.privatizePaid = 0, 0
	s.privatizePriceSeen, s.privatizePriceTick = 0, 0
	if s.Params.PrivatizeEnabled {
		for i := range s.Buildings {
			b := &s.Buildings[i]
			if !b.Spec.AllowPrivatize || b.Spec.IsFinance {
				continue
			}
			if b.GovLevel <= 1e-9 {
				continue
			}
			if b.MarginEMA <= s.Params.PrivatizeMargin {
				continue
			}
			// 本 tick 拟转让的等级数 = 政府持股 × 步长
			want := b.GovLevel * s.Params.PrivatizeStep
			if want > b.GovLevel {
				want = b.GovLevel
			}
			// 每级对价 = 建造成本（建造力）× 建造力当期价格 × 估值倍数。
			//
			// 契约定案：私有化成本就是建造成本。买下 N 级必须支付
			// 相当于把这 N 级重新建一遍的建造力价值，故对价随建造力
			// 市价浮动——这使私有化在建造力昂贵时自然放缓。
			//
			// 【可达性】该口径下私有化速度由 资本池 / 重置成本 决定，
			// 而资本池由资本收入累积。故 startupCapFraction 与
			// PrivateRetainRatio 是调节私有化速度的两个旋钮。
			unitPrice := b.Spec.BuildCost * prices[fiscal.PowerGoodIndex] * s.Params.PrivatizePriceMult
			// 【口径】want 是拟转让的【级数】，unitPrice 是【每级净对价】。
			// book.Privatize 内部再乘 (1+t) 得含税总额。
			// 若把已含税的金额当净额传进去，税会被算两遍——
			// 实测实付 18,639,847.90 而净额应为 16,683,800（差 1.567% = 税）。
			res := s.Bk().Privatize(want, unitPrice, s.Params.TaxRate)
			// 记录本 tick 私有化的对价口径，供测试与报告对账
			// （避免从日志文本反解，那既脆弱又容易读错）。
			s.privatizePriceSeen = unitPrice
			s.privatizePriceTick = prices[fiscal.PowerGoodIndex]
			if res.Units <= 0 {
				continue
			}
			// 股权腿：与现金腿在同一处变更，二者必须成对
			moved := res.Units
			if moved > b.GovLevel {
				moved = b.GovLevel
			}
			b.GovLevel -= moved
			b.PrivLevel += moved
			s.privatizeUnits += moved
			s.privatizePaid += res.Paid
		}
	}

	// ⑧ 扩建意向（§8-10，用 margin EMA）。
	//
	// 必须先于政府采购计算：采购要为 ⑨ 中政府项目的付款预留资金，
	// 而预留额取决于本 tick 的意向量。
	marginEMA := make([]float64, len(specs))
	for i := range s.Buildings {
		w := s.Params.MarginEMA
		if w <= 0 {
			w = 12
		}
		s.Buildings[i].MarginEMA = s.Buildings[i].MarginEMA*(1-1/float64(w)) + margins[i]/float64(w)
		marginEMA[i] = s.Buildings[i].MarginEMA
		s.Buildings[i].LastMargin = margins[i]
	}
	govLevels := make([]float64, len(specs))
	privLevels := make([]float64, len(specs))
	for i := range s.Buildings {
		govLevels[i] = s.Buildings[i].GovLevel
		privLevels[i] = s.Buildings[i].PrivLevel
	}
	powerDemand := build.PowerDemand(specs, s.pendingIntents(levels, marginEMA, govLevels, privLevels))
	intents := build.Plan(
		specs, levels, marginEMA, govLevels, privLevels,
		plan.ActualOutput[fiscal.PowerGoodIndex], powerDemand,
		s.Params, true, 3.0,
	)

	// ⑨ 政府采购建造力（G2）
	//
	// 契约与用户方案都没有指定政府的采购量。若只按"当期需要的扩建量"采购，
	// 建造部门会有大量产出卖不出去（它是唯一没有最终消费者的商品），
	// 从而收入不足、利润率跌到 −100% 并被 §4.4 缩编归零——
	// 这是本工程 v1/v2 崩解的直接原因。
	//
	// 因此政府【整批买下】建造部门的当期产出，未售出的部分留作公共储备，
	// 再在 ⑨ 卖给扩建方。这同时满足 G2 的字面要求（建造力必须在市场内购买，
	// 买家是政府），也让建造力维持一条完整的"生产→购买→出售"链。
	//
	// 资金约束不再是"现金池不得为负"，而是契约 §4.5 的债务上限（G7）：
	// 现金池可以为负（欠债），但债务不得超过固定资产的两倍。
	// 采购量由 PurchasePower 依据 AvailableCash（余额或剩余举债空间）自行裁剪。
	// 【统一记账簿】采购量先按 AvailableCash 裁剪（那是资金约束，不是记账），
	// 再过账为一笔借贷相等的交易：
	//
	//	借 政府        Gross
	//	贷 建筑[建造力] Net
	//	贷 政府        Tax
	//
	// 贷方那笔 Tax 是"政府自己收自己"（资金不离开政府池），
	// 故政府净支出恰为 Net。旧实现把这一步拆成"fiscal 扣 Gross + 回冲 Tax"
	// 与"sim 补记 Net 给建造部门"两处，正是分两处写导致漏记的典型。
	powerPrice := prices[fiscal.PowerGoodIndex]
	s.powerPriceNow = powerPrice
	s.Gov.PowerOutput = plan.ActualOutput[fiscal.PowerGoodIndex]

	// 可采购量：受债务上限约束的可用资金除以价格
	govCashBeforePurchase := s.balGov()
	wantQty := s.Gov.PowerOutput
	if avail := s.Gov.AvailableCash(powerPrice); avail > 0 && powerPrice > 0 {
		if afford := avail / powerPrice; wantQty > afford {
			wantQty = afford
		}
	} else {
		wantQty = 0
	}
	pr := s.Bk().PurchasePower(wantQty, powerPrice, s.Params.TaxRate)
	bought := pr.Qty
	s.Gov.PowerPurchased += pr.Qty
	s.Gov.PowerInventory += pr.Qty
	s.Gov.TaxRate = s.Params.TaxRate
	var powerPurchaseTax float64
	if bought > 0 {
		s.Recon.PowerRevenue = pr.Net
		s.recordRecon(fiscal.PowerGoodIndex, func(r *BuildingRecon) { r.PowerNet += pr.Net })
		s.Gov.TaxCollected += pr.Tax
		powerPurchaseTax = pr.Tax
	}
	s.powerSpendActual = govCashBeforePurchase - s.balGov()
	s.flowGovPurchaseSelfTax = powerPurchaseTax

	// ⑩ 出售建造力给扩建方（G2/G6）
	//
	// 【统一记账簿】成交额先在这里算清（资金约束不是记账），再过账。
	// 两类付款方的交易形态不同：
	//
	//	外部付款方（企业/资本）：
	//	    借 付款方  Gross = Paid·(1+t)
	//	    贷 政府    Gross
	//
	//	政府自建项目：
	//	    借 政府        Gross
	//	    贷 建造力卖方  Net
	//	    贷 政府        Tax      （自己收自己，故政府净支出 = Net）
	salePrice := powerPrice * (1 + s.Params.GovPowerMarkup)
	taxMul := 1 + s.Params.TaxRate
	// balanceOf 返回付款方【可动用】资金（含税口径），而不是账面余额。
	//
	// 对政府必须返回 AvailableCash：账面余额可以是负数（债务，§4.5.4），
	// 若直接把它当可动用资金，政府项目会完全停摆；反之若返回全额正余额，
	// 又会突破债务上限。正确做法是让付款能力 = 剩余举债空间（或现金余额）。
	balanceOf := func(payer string, idx int) float64 {
		switch payer {
		case "gov":
			return s.Gov.AvailableCash(powerPrice) / taxMul
		case "capital":
			return s.balCap() / taxMul
		default:
			return s.bal(idx) * (1 - s.govShare(idx)) / taxMul
		}
	}

	limit := s.Gov.PowerInventory
	var powerSaleTax, govBuildoutNet, govBuildoutTax, externalGross float64
	for _, it := range intents {
		if limit <= 1e-9 {
			break
		}
		need := specs[it.BuildingIndex].BuildCost * it.Units
		want := need
		if want > s.Params.SitePowerLimit {
			want = s.Params.SitePowerLimit // §4.2：每工地每 tick 最多投入 30 建造力
		}
		if want > limit {
			want = limit
		}
		avail := balanceOf(it.Payer, it.BuildingIndex)
		if avail < 0 {
			avail = 0
		}
		grant := want
		if afford := avail / salePrice; grant > afford {
			grant = afford
		}
		if grant <= 1e-9 {
			continue
		}
		paid := grant * salePrice

		if it.Payer == "gov" {
			// 政府自建：受债务上限约束。
			//
			// 【口径】paid 是净额（grant × salePrice），GovOwnBuildout 内部
			// 再乘 (1+t) 得含税总额。若把含税总额当净额传进去，税会被算两遍
			// ——实测残差恰等于自建税额（2217.22 = 22172.20×10%）。
			gross := paid * taxMul
			if !s.Gov.CanAfford(gross, powerPrice) {
				s.Gov.DebtCapBoundTicks++
				continue
			}
			n, g := s.Bk().GovOwnBuildout(paid, 1.0, s.Params.TaxRate, 0)
			if n <= 0 {
				continue
			}
			tax := g - n
			s.Gov.TaxCollected += tax
			s.Gov.BuildoutPaid += g
			s.Gov.OwnBuildoutPaid += g
			// 【政府池的实际净流出是 n，不是 g】
			//
			// GovOwnBuildout 的交易是：借 政府 Gross、贷 卖方 Net、贷 政府 Tax。
			// 贷方那笔 Tax 又回到政府池，故政府对这笔交易的净支出恰为 Net。
			// 若把 Gross 当作流出量，分解式会多算一份税额，
			// 表现为残差恰等于自建税额（实测 2217.22 = 22172.20×10%）。
			govBuildoutNet += n
			govBuildoutTax += tax
			powerSaleTax += tax
			s.Recon.PowerRevenue += n
			s.recordRecon(fiscal.PowerGoodIndex, func(r *BuildingRecon) { r.PowerNet += n })
		} else {
			grantedNet, tax := s.Bk().SellPowerExternal(
				it.Payer == "capital", it.BuildingIndex, paid, 1.0, s.Params.TaxRate)
			if grantedNet <= 0 {
				continue
			}
			s.Gov.TaxCollected += tax
			s.Gov.PowerRevenue += grantedNet
			powerSaleTax += tax
			externalGross += grantedNet * taxMul
			if it.Payer == "capital" {
				s.Cap.BuildoutPaid += grantedNet * taxMul
			}
		}
		limit -= grant
		s.Gov.PowerSold += grant
		s.Orders = append(s.Orders, Order{
			BuildingIndex: it.BuildingIndex,
			Units:         grant / specs[it.BuildingIndex].BuildCost,
			Progress:      grant,
			Payer:         it.Payer,
		})
	}
	s.Gov.PowerInventory = limit
	s.flowGovBuildoutDelta = govBuildoutNet
	s.flowGovBuildoutSelfTax = govBuildoutTax
	s.flowGovSaleDelta = externalGross
	// 掌控上限（G5）
	var otherLevels float64
	for i := range s.Buildings {
		if !s.Buildings[i].Spec.IsFinance {
			otherLevels += s.Buildings[i].Level
		}
	}
	s.Cap.UpdateControl(s.Buildings[model.FinanceIndex].Level, otherLevels, s.Params.ControlPerFinance)

	// ⑪ 完工
	s.completeOrders()

	// ⑫ 缩编
	for i := range s.Buildings {
		b := &s.Buildings[i]
		b.IdleTicks = build.UpdateIdle(b.IdleTicks, b.HireRate, s.Params)
		b.Level = build.Decay(b.Level, b.IdleTicks, b.MarginEMA, s.Params)
	}
	s.applyArableCap()

	// ⑬ 人口与雇佣调整
	satNec := outcome.NecessarySatisfaction()
	growth := consume.PopulationGrowth(satNec, s.Params)
	if s.Tick > 0 && int(s.Tick)%s.Params.TicksPerYear == 0 {
		s.Population *= 1 + growth
	}
	for i := range s.Buildings {
		b := &s.Buildings[i]
		b.HireRate = build.AdjustHire(b.HireRate, b.MarginEMA, s.Params)
	}

	s.Tick++
	tickRecon := &TickRecon{
		GovDelta:   s.balGov() - govDeltaBefore,
		CapDelta:   s.balCap() - capDeltaBefore,
		HouseDelta: s.Houses.TotalCash() - houseDeltaBefore,
	}

	// 政府池的资金流分解：每一路都取【账户实际变动】，不用公式推算。
	//
	// 这样"政府池 Δ = 各路之和"按构造成立，不会因含税/不含税口径差异
	// 留下假残差；若仍有残差，那就是真正未知的资金流——这正是我们要抓的。
	// 政府池的资金流分解全部由 fillFlow 统一构造（它持有自收税等分项），
	// 此处不再单独赋值，避免被随后的 fillFlow 覆盖。

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

	// 政府与资本的账目基本不变量（§4.5）：建造力库存不得为负、售出不得超过采购。
	// 这两条若被违反，说明存在"卖了没有买到的建造力"这类无对手方的记账，
	// 它会同时表现为货币守恒缺口。
	if err := s.Gov.Validate(s.powerPriceNow); err != nil {
		s.InvariantErr = err
	}
	s.fillFlow(flowInput{
		govOperating:    govOperating,
		capitalIncome:   capitalIncome,
		wageBill:        totalWage,
		spendNet:        outcome.SpendNet,
		powerOut:        plan.ActualOutput[fiscal.PowerGoodIndex],
		margins:         margins,
		consumerTax:     consumerTax,
		inputNet:        inputNet,
		inputTax:        inputTax,
		powerPurchaseTax: powerPurchaseTax,
		powerSaleTax:    powerSaleTax,
		govBuildout:     s.Gov.OwnBuildoutPaid,
		sat:             outcome.Sat,
	})
	return s.snapshot(plan, margins, totalWage), nil
}

// payWages 把工资从建筑现金池实际划转到各阶级的人群现金池（§5.1 修订）。
//
// 记账：
//
//	建筑现金池 −工资
//	人群池     +工资（按阶级拆分）
//
// 两者严格等额，故货币守恒。建筑现金池允许透支（§4.3 的"资金不足时停工"
// 只约束建造力支出，不约束工资发放），但透支会被记入 OverdraftTicks 供诊断。
func (s *State) payWages(levels, hire, wages []float64) {
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if wages[i] <= 0 {
			continue
		}
		popShare, wageShare := cohort.WageShares(b.Spec.LaborPerLevel)
		eff := levels[i] * hire[i]
		var paid float64
		// 【唯一记账入口】工资是一笔借贷相等的交易：
		//
		//	借 建筑[i]       工资总额
		//	贷 人群[i][c]    各阶级份额
		//
		// 先按阶级收集份额，再用一笔 Txn 同时登记两侧——
		// "建筑池扣了多少、人群池收了多少"由同一笔交易保证相等。
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
			s.Recon.WagePaid += paid
			s.recordRecon(i, func(r *BuildingRecon) { r.Wage += paid })
		}
	}
}

// consumerSellerShares 返回消费者货款的卖方账户列表与各自占比。
//
// 占比按"该建筑卖出商品的购买量 × 当期价格"计算；顺序固定（建筑下标升序），
// 保证确定性（架构 §7.1）。占比之和可能因浮点而不精确等于 1，
// 故调用方必须把残差归给最后一家。
func (s *State) consumerSellerShares(outcome consume.PooledOutcome) ([]ledger.Account, []float64) {
	prices := s.Market.Prices()
	accs := make([]ledger.Account, 0, len(s.Buildings))
	vals := make([]float64, 0, len(s.Buildings))
	var total float64
	for i := range s.Buildings {
		b := &s.Buildings[i]
		if b.Spec.IsFinance {
			continue
		}
		v := outcome.Bought[b.Spec.Recipe.Output] * prices[b.Spec.Recipe.Output]
		if v <= 0 {
			continue
		}
		accs = append(accs, b.Acc)
		vals = append(vals, v)
		total += v
	}
	shares := make([]float64, len(vals))
	if total > 0 {
		for i := range vals {
			shares[i] = vals[i] / total
		}
	}
	return accs, shares
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

// debitPayer 从指定付款方账户扣款。
//
// 付款方语义（与 SellPower 的 Payer 一致）：
//
//	"gov"     → 政府现金池
//	"capital" → 金融区（资本）现金池
//	"firm"    → 该建筑现金池中属于私有的部分
//
// 用于交易税的落地扣款：SellPower 只扣货款，税额必须另行真实扣除，
// 否则政府会收到一笔无人支付的税（凭空造币）。
func (s *State) debitPayer(payer string, idx int, amount float64) {
	if amount <= 0 {
		return
	}
	switch payer {
	case "gov":
		s.Gov.Cash.Add(-amount)
	case "capital":
		s.Cap.Cash.Add(-amount)
		s.Cap.BuildoutPaid += amount
	default:
		if idx >= 0 && idx < len(s.Buildings) {
			s.BuildingCash(idx).Add(-amount)
		}
	}
}

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
// 旧实现把借贷两侧【分两处写】（买方在 717 行扣、卖方在 738 行入），
// 虽然两处都在，但绕过 Post 校验，因此无法保证 Σ借 == Σ贷。
// 现在 step 的 ⑤ 步直接调用 book.PayIntermediate，本函数保留仅为兼容签名。
//
// Deprecated: 使用 book.PayIntermediate。
func (s *State) payIntermediate(buyer int, need float64) (net, tax float64) {
	sellerOf := func(b int) (int, bool) {
		for good := range s.Buildings[b].Spec.Recipe.Inputs {
			for j := range s.Buildings {
				if s.Buildings[j].Spec.IsFinance {
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

// pendingIntents 估算建造力需求时用的临时意向（不含建造部门优先规则）。
func (s *State) pendingIntents(levels, marginEMA, govLevels, privLevels []float64) []build.Intent {
	var out []build.Intent
	for i := range s.Buildings {
		units := build.ExpansionUnits(marginEMA[i], levels[i], s.Params)
		if units <= 0 {
			continue
		}
		payer := "firm"
		if s.Buildings[i].Spec.IsFinance {
			payer = "capital"
		} else if govLevels[i] >= privLevels[i] {
			payer = "gov"
		}
		out = append(out, build.Intent{BuildingIndex: i, Units: units, Payer: payer})
	}
	return out
}

// completeOrders 处理达到成本门槛的订单（§8-6）。
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
		b := &s.Buildings[bi]
		need := b.Spec.BuildCost * o.Units
		if o.Progress < need-1e-9 {
			kept = append(kept, o)
			continue
		}
		addGov, addPriv, added := build.ApplyCompletion(
			&b.Spec, o.Units, s.govShare(bi),
			s.Params.ArableCap, arableUsed, b.Level,
		)
		if added <= 0 {
			s.BlockedBuilds++
			continue
		}
		b.GovLevel += addGov
		b.PrivLevel += addPriv
		b.Level += added
		// §4.3：新建筑的营运本金。这是系统里【唯一】允许的货币创造，
		// 故必须单独计量（NewCapital），否则货币守恒审计会把它误判为漏出。
		injected := added * 5000
		s.BuildingCash(bi).Add(injected)
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

func (s *State) powerTradeValue() float64 {
	// 建造力交易额 = 采购量 × 成交价（政府买入） + 售出量 × 售价（企业买入）。
	// 注意采购与售出是同一批货物的两次易手，故两段都要计税。
	pp := s.Market.Markets[fiscal.PowerGoodIndex].Price
	return s.Gov.PowerPurchased*pp + s.Gov.PowerSold*pp*(1+s.Params.GovPowerMarkup)
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

// demandScale 是联合标定系数。为避免在 State 里冗余存储，这里用字段惰性缓存。
func (s *State) govShare(i int) float64 {
	if s.Buildings[i].Spec.IsFinance {
		return 0
	}
	if s.Buildings[i].Level <= 0 {
		return 0.70
	}
	return s.Buildings[i].GovLevel / s.Buildings[i].Level
}

// flowInput 汇总 fillFlow 的入参，避免长参数列表错位。
type flowInput struct {
	govOperating     float64
	capitalIncome    float64
	wageBill         float64
	spendNet         float64
	powerOut         float64
	margins          []float64
	consumerTax      float64
	inputNet         float64
	inputTax         float64
	powerPurchaseTax float64
	powerSaleTax     float64
	govBuildout      float64
	sat              [4]float64
}

// fillFlow 记录本期资金流分解。
//
// 这一步是诊断的核心：政府现金池的变化必须能由
//
//	税收 + 经营净额 + 售力收入 − 采购支出 − 政府自有项目付款
//
// 完全解释，否则说明有未记账的资金漏出。
func (s *State) fillFlow(in flowInput) {
	// 【税收口径】Gov.TaxCollected 现在由各交易处【全部同步累加】
	// （消费税 ④b、中间投入税 ⑤、采购税 ⑨、售力税 ⑩、自建税 ⑩），
	// 故这里直接使用它，不再另行汇总——否则会与同步累加重复计数。
	//
	// 自收自支的税（采购与自建）仍单列为 GovSelfTax：
	// 它们计入 GovTax，却不是政府池的净流入，分解式必须扣除。
	externalTax := in.consumerTax + in.inputTax
	selfTax := s.flowGovPurchaseSelfTax + s.flowGovBuildoutSelfTax
	_ = externalTax

	f := FlowDiag{
		GovOperating: s.flowGovOperatingDelta,
		GovTax:       s.Gov.TaxCollected,
		GovSelfTax:   selfTax,
		// GovPowerSpend 是政府现金池为采购建造力【实际流出】的金额。
		// 用账户前后差计量，而不是 PowerPurchased×price——后者在政府资金不足、
		// 只买下部分产出时会与实际扣款不符，留下 Net·t 量级的残差。
		GovPowerSpend:    s.powerSpendActual,
		GovPowerRevenue:  s.flowGovSaleDelta,
		CapitalProfit:    in.capitalIncome,
		WageTotal:        in.wageBill,
		SpendNet:         in.spendNet,
		PowerOut:         in.powerOut,
		PowerBought:      s.Gov.PowerPurchased,
		PowerSold:        s.Gov.PowerSold,
		ConsumerTax:      in.consumerTax,
		InputNet:         in.inputNet,
		InputTax:         in.inputTax,
		PowerPurchaseTax: in.powerPurchaseTax,
		PowerSaleTax:     in.powerSaleTax,
		GovBuildoutPaid:  s.flowGovBuildoutDelta,
		Sat:              in.sat,
	}
	if s.Houses != nil {
		f.HouseCash = s.Houses.TotalCash()
	}
	f.WorstSector, f.WorstMargin = -1, 0
	f.BestSector, f.BestMargin = -1, 0
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsFinance || i >= len(in.margins) {
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

// govShareWeighted 返回按级数加权的政府持股比例（私有化诊断用）。
//
// 口径：Σ GovLevel / Σ Level，金融区不参与（它本来就是私有的）。
func govShareWeighted(s *State) float64 {
	var govLv, total float64
	for i := range s.Buildings {
		if s.Buildings[i].Spec.IsFinance {
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

func (s *State) snapshot(plan *produce.Plan, margins []float64, wageBill float64) *Snapshot {
	prices := s.Market.Prices()
	// 现金池总额（契约 §7 修订口径）：
	//   建筑现金池 + max(0, 政府现金池) + 资本现金池 + 人群现金池
	//
	// 政府现金池为负时是债务，不计入（否则一笔举债会被扣两次，使 GDP 必然为负）。
	// 人群现金池是 §5.1 修订新增的持有主体，属于居民部门，必须计入。
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
	cashTotal := buildingCash + govCashPositive + s.balCap() + houseCash

	snap := &Snapshot{
		Tick:           s.Tick,
		Prices:         prices,
		Supply:         plan.ActualOutput,
		Demand:         make([]float64, len(prices)),
		Margins:        margins,
		Levels:         s.levels(),
		GovCash:        s.balGov(),
		CapitalCash:    s.balCap(),
		HouseCash:      houseCash,
		Tax:            s.Gov.TaxCollected,
		PowerPurchased: s.Gov.PowerPurchased,
		PowerSold:      s.Gov.PowerSold,
		Population:     s.Population,
		Sat:            s.Last.Sat,
		SpendNet:       s.Last.SpendNet,
		WageBill:       wageBill,
		CashTotal:      cashTotal,
		TotalMoney:     s.TotalMoney(),
		NewCapital:     s.tickNewCapital,
		GovDebt:        s.Gov.Debt(),
		GovDebtCap:     s.Gov.DebtCap(s.powerPriceNow),
		GovPowerOutput: s.Gov.PowerOutput,
		// 私有化诊断（§4.5.1 修订）
		PrivatizeUnits: s.privatizeUnits,
		PrivatizePaid:  s.privatizePaid,
		GovShareAfter:  govShareWeighted(s),
		Flow:           s.Flow,
	}
	snap.PriceRatio = make([]float64, len(prices))
	for i := range prices {
		snap.PriceRatio[i] = prices[i] / s.Goods[i].Pcost
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
