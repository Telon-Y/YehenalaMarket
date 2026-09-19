package sim

// audit_r41_test.go —— 「税制是参数、不是硬编码」的守门测试（2026-09-19 第 17 轮）。
//
// 背景（用户）："现在征收的是相对抽象的增值税+消费税，之后可能会修改。"
//
// 现行两段税（增值税 ν + 消费税 τ）与仓库加价都是**抽象替身**。它们只从三处进入模型：
//
//	① 买家的实际付款（出库 / 入库的记账腿）；
//	② §3.4 的标定（价格方程 p = w·Aᵀp + l、加成价、§3.1 的价格两列）；
//	③ 决策与判据用的利润率口径（`fullCapacityCosts`）。
//
// ②③ 共用**买家加载系数** w = (1+加价)(1+消费税)。本测试把"换税制"这条路走一遍：
// **把 ν = τ = 加价 = 0**（w = 1），断言模型自动退化为"无税、无加价"的干净口径——
// 价格表回到仓库落地前的值、谱半径回到 0.5、政府商品税为 0、仓库只有工资支出。
// 若哪天有人把税率写死在别处（绕过 w），本测试会失败。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditR41 -v

import (
	"math"
	"testing"

	"yehenala/market/internal/model"
)

// TestAuditR41TaxKnobsAreTheOnlyChannels 校验"税制=参数"：置零即回到无税口径。
func TestAuditR41TaxKnobsAreTheOnlyChannels(t *testing.T) {
	auditEnabled(t)

	zero := 0.0
	st, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
		// 三个旋钮全部置 0：w = 1。
		VATRate:         &zero,
		ConsumeTaxRate:  &zero,
		WarehouseMarkup: &zero,
	})
	if err != nil {
		t.Fatalf("构造: %v", err)
	}

	// ① 买家加载系数 = 1，且**标定**确实用了它（否则价格表还是含税的那份）。
	if w := st.Params.BuyerWedge(); math.Abs(w-1) > 1e-12 {
		t.Fatalf("买家加载系数 = %.6f，税与加价置零后应为 1", w)
	}
	cal := st.calibration
	if math.Abs(cal.BuyerWedge-1) > 1e-12 {
		t.Fatalf("标定用的 w = %.6f，应为 1（说明标定没有跟随税制旋钮）", cal.BuyerWedge)
	}
	if math.Abs(cal.SpectralRadius-0.5) > 1e-6 {
		t.Errorf("ρ(A) = %.6f，应为 0.5", cal.SpectralRadius)
	}
	// w = 1 口径下的零利润价表：这就是"换税制后价格表自动重解"的证据。
	//
	// 【前值 → 后值（2026-09-19 第 20 轮）】农业劳动结构（农民 7 元）把谷物/棉花的
	// 每级工资从 33,750 降到 28,250，故含农产品的价格整体下移：
	//
	//	谷物 675 → 565、织物 750 → 627.778、加工食品 1350 → 1252.222、
	//	服装 788 → 714.167、高档服装 1750 → 1648.148；
	//	煤/铁/钢/工具/住房/建造力不含农产品，历史值不变（1006/1381/767/741/7250）。
	hist := []float64{565, 1252.2222, 627.7778, 714.1667, 1648.1481, 1005.6818, 1005.6818, 1380.6818, 767.0455, 741.4773, 7250}
	for i, want := range hist {
		if math.Abs(cal.Pcost[i]-want) > 0.5 {
			t.Errorf("P_ref[%d] = %.4f，w = 1 时应回到历史值 %.0f——"+
				"说明价格表没有跟随税制（换税种时会与实际付款分叉）", i, cal.Pcost[i], want)
		}
	}

	// ② 运行时：两段税恒为 0，政府**商品税**收入为 0；仓库的收入 = 成本（只有工资支出）。
	for i := 0; i < 20; i++ {
		snap, err := st.Step()
		if err != nil {
			t.Fatalf("Step: %v", err)
		}
		if snap.VAT != 0 || snap.ConsumeTax != 0 {
			t.Fatalf("tick %d：ν/τ 置零后仍收到税（增值税 %.4f / 消费税 %.4f）",
				snap.Tick, snap.VAT, snap.ConsumeTax)
		}
		if st.Gov.TaxCollected != 0 {
			t.Fatalf("tick %d：政府税收 = %.4f，应为 0（商品税是当前唯一的税源）",
				snap.Tick, st.Gov.TaxCollected)
		}
		// 仓库：加价 = 0、增值税 = 0 ⇒ 纯利 = −自身工资（它的唯一支出）。
		if snap.WarehouseLevel > 0 || snap.WarehouseIn > 0 {
			want := -snap.WarehouseWage
			if math.Abs(snap.WarehouseProfit-want) > 1e-6*math.Max(1, math.Abs(want)) {
				t.Errorf("tick %d：仓库纯利 = %.4f，无加价无增值税时应为 −工资 = %.4f",
					snap.Tick, snap.WarehouseProfit, want)
			}
		}
		if r := st.Recon.Residual(); math.Abs(r) > 1e-6 {
			t.Fatalf("tick %d：建筑池对账残差 = %.6f，应为 0", snap.Tick, r)
		}
		if st.InvariantErr != nil {
			t.Fatalf("tick %d：记账不变量被破坏：%v", snap.Tick, st.InvariantErr)
		}
	}

	// ③ 对照：默认税制下 w ≠ 1、价格表也不同——证明这两个旋钮真的在起作用（防空真）。
	def, err := New(Options{
		Population:           10_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("构造默认态: %v", err)
	}
	if math.Abs(def.Params.BuyerWedge()-1.07625) > 1e-9 {
		t.Errorf("默认买家加载系数 = %.6f，应为 1.07625", def.Params.BuyerWedge())
	}
	if math.Abs(def.calibration.Pcost[model.Goods-1]-8130.49) > 0.5 {
		t.Errorf("默认口径建造力 P_ref = %.2f，应为 8130.49（含税口径）",
			def.calibration.Pcost[model.Goods-1])
	}
	t.Logf("税制=参数：w=1 时 ρ(A)=0.500、价格表回到历史值、政府商品税 0、仓库纯利 = −工资；"+
		"默认 w=%.5f、建造力 P_ref=%.2f（自动重解）",
		def.Params.BuyerWedge(), def.calibration.Pcost[model.Goods-1])
}
