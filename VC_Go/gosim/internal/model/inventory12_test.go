package model

import "testing"

// Test12FeatureInventory 是 1.2 的**完成度清单**：把全部 1.2 特性开关集中在一个地方，
// 断言它们**存在**且**默认值保持 1.0 语义**。
//
// 【为什么需要它】
// 到 R97 为止 1.2 的实现散布在 9 个开关上（跨 4 个包）。每个开关都有自己的审计测试，
// 但**没有任何一处**能回答"1.2 到底实现了哪些特性、默认值是否仍然安全"。
// 本测试就是那一处：**改坏任何一个默认值，这里立刻变红**。
//
// 【默认值的纪律】1.2 的全部特性都必须在**关闭**时逐位复现 1.0。
// 故除 `PrivatizeEnabled`（1.0 自带、默认开）外，其余一律默认 **false**；
// 两个数值型开关（`InvestAIEnabled` 默认 **true**、`InvestManorShare` 默认 **−1**）
// 的默认值同样表达"AI 托管 / 未设定"。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/model/ -run Test12FeatureInventory -v
func Test12FeatureInventory(t *testing.T) {
	p := DefaultParams()

	// ① 全部开关必须存在且默认安全。
	//
	// 每一项 = {名称, 实际值, 期望默认, 含义}。
	// "期望默认"一律是"1.0 语义"，不是"1.2 语义"——这是本项目的核心纪律。
	switches := []struct {
		name string
		got  bool
		want bool
		why  string
	}{
		{"M8   BankEnabled", p.BankEnabled, false,
			"借贷台账/储蓄银行；关闭 ⇒ 储蓄仍直达投资池"},
		{"R71  LoanFromBalance", p.LoanFromBalance, false,
			"放贷额锚到银行余额；关闭 ⇒ 固定 50 万/笔"},
		{"M4.2 OwnershipRestructure", p.OwnershipRestructure, false,
			"私人份额拆 资本30%/劳动力70%；关闭 ⇒ 全额归资本"},
		{"M1   SavingsStockTrack", p.SavingsStockTrack, false,
			"储蓄存量口径；关闭 ⇒ 两个诊断账恒 0"},
		{"§1.2-5 CentralBankEnabled", p.CentralBankEnabled, false,
			"央行+金矿（追加两类建筑、场地数 15→17）"},
		{"M3/M6 InvestAIEnabled", p.InvestAIEnabled, true,
			"**默认 true** = AI 托管投资预算（1.0 的需求比例口径）"},
		{"M5①  GovDebtInterestEnabled", p.GovDebtInterestEnabled, false,
			"政府债务计息；关闭 ⇒ 1.0 §4.5.4『不计息』"},
		{"R97  InflationDeflation", p.InflationDeflation, false,
			"工资平减；关闭 ⇒ 消费预算用名义金额"},
		{"M7   WageBidEnabled", p.WageBidEnabled, false,
			"工资竞标；关闭 ⇒ 工资固定"},
		{"1.0  PrivatizeEnabled", p.PrivatizeEnabled, true,
			"**1.0 自带**（不是 1.2 特性）：默认开，1.2 沿用"},
	}
	for _, s := range switches {
		if s.got != s.want {
			t.Errorf("%s 默认 = %v，应为 %v —— %s", s.name, s.got, s.want, s.why)
		}
	}

	// ② 数值型开关的默认值也必须表达正确语义
	if p.InvestManorShare != -1 {
		t.Errorf("InvestManorShare 默认 = %v，应为 −1（**未设定** ⇒ 交回 AI）；"+
			"注意 0 是**合法的玩家选择**（全给金融），故不能用 0 当哨兵", p.InvestManorShare)
	}
	if p.GovDebtInterestRate != 0.05 {
		t.Errorf("GovDebtInterestRate 默认 = %v，应为 0.05（M8.5 的统一 5%%）", p.GovDebtInterestRate)
	}
	if p.LoanAnnualRate != 0.05 {
		t.Errorf("LoanAnnualRate 默认 = %v，应为 0.05", p.LoanAnnualRate)
	}
	// ③ §1.2-5 的条款数值对上原文
	if p.GoldPrice != 10_000 || p.GoldPerMint != 20 || p.MoneyPerMint != 400_000 {
		t.Errorf("§1.2-5 条款默认 = (%.0f, %.0f, %.0f)，应为 (10000, 20, 400000)",
			p.GoldPrice, p.GoldPerMint, p.MoneyPerMint)
	}
	if p.MintWageFraction <= 0 {
		t.Errorf("MintWageFraction 默认 = %v，应 > 0（R87 裁决 0.015：金矿扩张优先）",
			p.MintWageFraction)
	} else if p.MintWageFraction != 0.015 {
		t.Logf("提示：MintWageFraction = %v（R87 裁决值为 0.015）", p.MintWageFraction)
	}

	t.Logf("1.2 特性清单：%d 个布尔开关 + 3 个数值条款，默认值全部保持 1.0 语义", len(switches))
}
