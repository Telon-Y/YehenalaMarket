package sim

// 【R81 诊断】M1 的"投资池支出端 → 居民购买资产"**是否已经在运行**？
//
// 关键线索（`state.processAcquisitions` 的 ③）：
//
//	出资方：默认"**投资池优先，其次资本池**"（第 28 轮用户裁决）。
//	`Params.AcquireFromInvestment = true`（默认）⇒ 先走
//	`book.PrivatizeFromInvestment` —— 而这正是"**居民储蓄（投资池）
//	买下政府持股**"= M1 所说的"居民以储蓄购买资产"。
//
// 若它在默认参数下**确实成交**，则 M1 的第二条（投资池支出端接到个人投资）
// 在**功能上已经存在**，缺的不是机制而是**口径**（谁的收益权、怎么分红）。
//
// 运行：$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestM1AcquireProbe -v

import (
	"fmt"
	"testing"
)

func TestM1AcquireProbe(t *testing.T) {
	auditEnabled(t)
	st, err := New(Options{
		Population:           5_000_000,
		WealthTier:           10,
		FinanceLaborPerLevel: 1000,
		GovStartupFraction:   0.5,
		ProductionInitLevel:  -1,
	})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	var cumUnits, cumPaid, cumOrderUnits float64
	var firstTick int64 = -1
	for i := 0; i < 1200; i++ {
		if _, err := st.Step(); err != nil {
			t.Fatalf("Step %d: %v", i, err)
		}
		cumUnits += st.privatizeUnits
		cumPaid += st.privatizePaid
		if st.privatizeUnits > 1e-9 && firstTick < 0 {
			firstTick = st.Tick
		}
	}
	// 末期政府持股与投资池余额
	var govLevel, privLevel float64
	for i := range st.Buildings {
		govLevel += st.Buildings[i].GovLevel
		privLevel += st.Buildings[i].PrivLevel
	}
	for _, o := range st.Orders {
		if o.Acquire {
			cumOrderUnits += o.Remaining(st.Buildings)
		}
	}
	fmt.Printf("1200 tick：累计私有化 %.4f 级、累计对价 %.2f、首成交 tick %d\n",
		cumUnits, cumPaid, firstTick)
	fmt.Printf("末期：政府级合计 %.4f、私有级合计 %.4f（私有占比 %.2f%%）\n",
		govLevel, privLevel, 100*privLevel/(govLevel+privLevel))
	fmt.Printf("末期投资池 %.2f、资本池 %.2f、政府池 %.2f\n",
		st.InvestmentPool(), st.balCap(), st.balGov())
	fmt.Printf("未成交的收购单剩余对价 %.2f；AcquireFromInvestment=%v\n",
		cumOrderUnits, st.Params.AcquireFromInvestment)
}
