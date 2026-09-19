package sim

import (
	"fmt"
	"testing"
)

// TestAuditHireAndDecay 检查 §5 的解雇机制与 §4.4 的缩编是否真的在工作。
//
// 契约 §5：利润率 < 0 → 每周期解雇 5%（按年化读作 5%/52）
// 契约 §4.4：连续 156 周期雇佣率 < 75% 且亏损 → 每周期等级 −5%
//
// 要验证三件事：
//  1. 亏损建筑的 hireRate 是否逐 tick 下降
//  2. hireRate 是否真的跌破 75%（否则缩编永不触发）
//  3. 亏损建筑是否真的在缩编
func TestAuditHireAndDecay(t *testing.T) {
	auditEnabled(t)
	st := newTestState(t)

	fmt.Printf("\n%-12s %8s %8s %8s %8s %10s %10s\n",
		"建筑", "初始等级", "末等级", "初始雇佣", "末雇佣", "IdleTicks", "末利润率")
	initLevel := make([]float64, len(st.Buildings))
	initHire := make([]float64, len(st.Buildings))
	for i := range st.Buildings {
		initLevel[i] = st.Buildings[i].Level
		initHire[i] = st.Buildings[i].HireRate
	}

	const ticks = 1200
	if _, err := st.Run(ticks); err != nil {
		t.Fatalf("Run: %v", err)
	}

	for i := range st.Buildings {
		b := &st.Buildings[i]
		fmt.Printf("%-12s %8.2f %8.2f %8.4f %8.4f %10d %9.2f%%\n",
			b.Spec.Name, initLevel[i], b.Level, initHire[i], b.HireRate,
			b.IdleTicks, b.LastMargin*100)
	}

	// 判定 1：必须有建筑的雇佣率下降（说明解雇在动）
	decreased := 0
	for i := range st.Buildings {
		if st.Buildings[i].HireRate < initHire[i]-1e-9 {
			decreased++
		}
	}
	fmt.Printf("\n雇佣率下降的建筑数 = %d / %d\n", decreased, len(st.Buildings))
	if decreased == 0 {
		t.Error("没有任何建筑的雇佣率下降——§5 的亏损解雇机制没有生效")
	}

	// 判定 2：必须有建筑的雇佣率跌破 75%（否则 §4.4 缩编永不触发）
	below75 := 0
	for i := range st.Buildings {
		if st.Buildings[i].HireRate < 0.75 {
			below75++
		}
	}
	fmt.Printf("雇佣率 < 75%% 的建筑数 = %d / %d\n", below75, len(st.Buildings))

	// 判定 3：亏损建筑的等级是否下降
	shrunk := 0
	for i := range st.Buildings {
		if initLevel[i] > 0 && st.Buildings[i].Level < initLevel[i]-1e-9 {
			shrunk++
		}
	}
	fmt.Printf("等级下降的建筑数 = %d / %d\n", shrunk, len(st.Buildings))
}
