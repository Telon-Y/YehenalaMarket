# 1.0 内核改动记录

> 本文档记录本工程对 1.0 契约的【逐条显式改动】：位置、原文、改后、理由、验证方式。
>
> **契约文档 `VC_Go/docs/1.0 生产与市场模拟.md` 已同步**（本次完成）。同步范围见第七节。

---

## 零、改动总览

| # | 改动 | 涉及文件 | 状态 |
|---|------|----------|------|
| 1 | 新建人群与阶级现金池（3 阶级 × 12 场地 = 36 池） | `internal/cohort/cohort.go` | ✅ 已完成并验证 |
| 2 | 工资从建筑池**实际划转**到人群池 | `internal/sim/step.go` | ✅ 已完成并验证 |
| 3 | 消费改为**逐池结算**：每池用自己的现金池、自己的财富档 | `internal/consume/consume.go`、`internal/sim/step.go` | ✅ 已完成并验证 |
| 4 | 满足度改为**按人口加权**汇总 | `internal/consume/consume.go` | ✅ 已完成 |
| 5 | 消费交易税**真实扣款**并真实入政府账 | `internal/sim/step.go` | ✅ 已完成 |
| 6 | 中间投入交易税**真实双边记账** | `internal/sim/step.go` | ✅ 已完成并验证 |
| 7 | 建造力买卖全程计税（采购、售力、政府自建） | `internal/fiscal/fiscal.go`、`internal/sim/step.go` | ✅ 已完成并验证 |
| 8 | 政府自建项目付款补上**真实扣款与卖方入账** | `internal/fiscal/fiscal.go` | ✅ 已完成并验证 |
| 9 | 利润分账由**复制**改为**划分**（消除 2× 重复入账） | `internal/fiscal/fiscal.go`、`internal/sim/step.go` | ✅ 已完成并验证 |
| 10 | 初始货币存量由**流量**导出（原为任意值） | `internal/sim/state.go` | ✅ 已完成并验证 |
| 11 | 开局布点算法重写 | `internal/sim/state.go` | ✅ 已完成，**后被统一起始等级取代**（见 #15） |
| 12 | 新增货币守恒不变量与四池对账 | `internal/sim/state.go`、`state_test.go` | ✅ 已完成并验证 |
| 13 | 建筑池对账**完全对平**（逐建筑残差 = 0） | `internal/sim/step.go`、`state.go` | ✅ 已完成并验证 |
| 14 | **统一记账簿**：一个文件收全部离散记账 | `internal/ledger/`、`internal/book/` | ✅ 已完成并验证 |
| 15 | **起始等级统一为每种生产建筑 5 级** | `internal/model/domain.go`、`internal/sim/state.go` | ✅ 已完成并验证 |
| 16 | **私有化机制**（总开关 + 逐建筑开关） | `internal/book/flow.go`、`internal/model/domain.go`、`internal/sim/step.go` | ✅ 已完成并验证 |
| 17 | **显式场景注入**（严禁不明示修改数据） | `internal/sim/scenario_test.go` | ✅ 已完成 |
| 18 | 修复 `Gov.TaxCollected` 漏记消费税与中间投入税 | `internal/sim/step.go` | ✅ 已完成并验证 |
| 19 | **契约文档同步** | `VC_Go/docs/1.0 生产与市场模拟.md`、`VC_Go/docs/1.0 生产与市场模拟.html` | ✅ 已完成 |

**当前状态**：`go build ./...` exit=0，`go vet ./...` exit=0，7 个包全绿，16 个审计测试全绿。

---

## 一、改动 1：人群与阶级现金池（新建文件）

**文件**：`VC_Go/gosim/internal/cohort/cohort.go`（新建，约 240 行）

**改动内容**：新建 `cohort` 包，把"居民"从一句口号变成有资产负债表的实体。

```
每个劳动场地（每类建筑）× 每个阶级 = 一个独立现金池
3 个阶级（劳工 / 工程师 / 资本家）× 12 类建筑 = 36 个池
```

**关键结构**：

```go
type Pool struct {
    Worksite   int      // 劳动场地（建筑类别下标）
    Class      int      // 阶级下标（0 劳工 / 1 工程师 / 2 资本家）
    Population float64  // 该池人口 = LaborPerLevel × 阶级比例 × 雇佣率 × 级数
    Cash       float64  // 该池自己的现金池
}
```

**财富档口径（改动 3 的前提）**：

原契约 §6.3 说"财富等级由平均工资插值确定"。若对全场地取平均工资，
三个阶级会落到同一个档（6.75 → 约档 7），阶级差异被平均掉。

本改动改为**逐池按阶级工资定档**：

| 阶级 | 占每级比例 | 工资 | 落档 |
|------|-----------|------|------|
| 劳工 | 75% | 5 元 | 档 5 |
| 工程师 | 20% | 10 元 | 档 10 |
| 资本家 | 5% | 20 元 | 档 20 |

每级工资总额 = 5000 × 6.75 = **33,750 元，与契约 §5 完全一致**（口径未变），
只是把这一笔钱按阶级拆成三份，分别落在三个财富档上。

需求按人口加权汇总：

```
target_g = Σ_pools DemandAt(档_pool)[g] × 人口_pool / 100000 × k
```

这等价于"对全场地的平均后财富等级套用需求表"，但保留阶级间差异。

**验证**：`go test ./internal/sim/ -run TestAuditConsumptionShortfall`
输出显示 36 个池各自的财富档分别为 5.0 / 10.0 / 20.0，人口与现金池独立。

---

## 二、改动 2：工资实际划转

**文件**：`VC_Go/gosim/internal/sim/step.go`，新增函数 `payWages`

**改动前**（`step.go` 原 ③ 步）：

```go
// ③ 工资
wages := produce.WageBill(specs, levels, hire)
totalWage := produce.TotalWage(wages)
```

工资只是算出来一个数，用在利润公式里 `LastProfit = 收入 − 投入 − 工资`，
**没有任何账户收到这笔钱**。居民没有账户，消费预算 `totalWage` 是凭空算出的数。

**改动后**：

```go
// ③ 工资：从建筑现金池【实际划转】到人群现金池（§5.1 修订）
s.Houses.Reset()            // 人口是流量，先清零
s.payWages(levels, hire, wages)

func (s *State) payWages(levels, hire, wages []float64) {
    for i := range s.Buildings {
        popShare, wageShare := cohort.WageShares(b.Spec.LaborPerLevel)
        eff := levels[i] * hire[i]
        for c := 0; c < cohort.ClassCount; c++ {
            amount := eff * wageShare[c]
            s.Houses.Credit(i, c, eff*popShare[c], amount)  // 人群池 +工资
            paid += amount
        }
        b.Cash.Add(-paid)                                    // 建筑池 −工资
    }
}
```

**记账语义**：建筑现金池 −工资，人群池 +工资，两者严格等额，货币守恒。
建筑池允许透支（§4.3 的"资金不足时停工"只约束建造力支出，不约束工资发放）。

**验证**：`TestWagesAreActuallyPaid` 断言首 tick 人群池流入 **恰等于**工资总额。

---

## 三、改动 3：消费改为逐池结算

**文件**：`VC_Go/gosim/internal/consume/consume.go`，新增 `PurchaseByPools`

**改动前**：全体人口当作一个消费单元，用一个 `totalWage` 当预算，
一个 `WealthTier` 查需求表：

```go
targets := consume.Targets(groups, s.Population, s.WealthTier, scale)
outcome := consume.Purchase(groups, targets, plan.NetSupply, prices, totalWage, s.Params.TaxRate)
```

**改动后**：每个池用自己的预算、自己的财富档查表，共享同一份净供给：

```go
outcome := consume.PurchaseByPools(groups, poolSpecs, plan.NetSupply, prices, s.demandScale)
```

**购买顺序**：池按财富档从高到低购买（同档按池下标稳定排序，保证确定性）。
理由：现实中高收入者优先获得稀缺品，且这使"缺货"在各阶层的分布可观察。

**保留 `Purchase`**：单预算单元的旧口径函数保留，供单元测试与单群体场景使用；
§8 主循环一律走 `PurchaseByPools`。

**诊断字段**（新增，用于判定消费受限原因）：

```go
PoolSpend       []float64  // 各池实际税前支出
PoolBudget      []float64  // 各池税前预算
PoolTargetValue []float64  // 各池按目标量的税前支出
```

判定规则：

```
PoolSpend ≈ PoolTargetValue < PoolBudget     ⇒ 供给不足（缺货）
PoolSpend ≈ PoolBudget     < PoolTargetValue ⇒ 预算不足（缺钱）
```

---

## 四、改动 4：全过程真实 10% 交易税

### 4.1 消费环节

**改动前**：只在预算上除以 `(1+t)`

```go
affordable := budget / (1 + taxRate)   // 仅用于压缩，钱并未真的被扣
```

**改动后**（`step.go` ④b）：

```go
for pi := range s.Houses.Pools {
    net := outcome.Pools[pi].SpendNet
    paid, tax, ok := s.Houses.Spend(pi, net, s.Params.TaxRate)
    consumerNet += paid
    consumerTax += tax
}
s.distributeConsumerRevenue(outcome, consumerNet)   // 净额入卖方建筑
```

`cohort.Ledger.Spend` 的记账：

```
池现金   −= net + tax
卖方收入 += net          （由 distributeConsumerRevenue 分摊到各卖方）
政府税收 += tax          （累加进 Gov.TaxCollected）
```

**精确等额的保证**：`distributeConsumerRevenue` 用"累计残差"而不是逐项四舍五入——
最后一家拿到的份额是差额，使 Σ入账 与 `consumerNet` 逐位相等。

### 4.2 中间投入环节

**改动前**：这笔税**从未从任何账户扣除**，政府却全额入账。
实测隐含税率 0.176，而正确值应为 `t/(1+t) = 0.0909`。

**改动后**（`step.go` 新增 `payIntermediate`）：

```go
gross := need * taxMul
if gross > avail { gross = avail }        // 买不起就按可用资金等比缩减
net = gross / taxMul
s.Buildings[buyer].Cash.Add(-gross)        // 买方 −含税
s.Buildings[target].Cash.Add(net)          // 卖方 +净额
s.Gov.Cash.Add(tax); s.Gov.TaxCollected += tax   // 政府 +税额
```

三者之和为零，货币守恒。买方现金不足时按可用资金缩减——
这是"上游恒亏"能真正约束住生产链的机制，而不是靠一条无人支付的账。

### 4.3 建造力买卖

**采购（G2）**：政府池 −含税、建造部门池 +净额、政府税收 +税额。
由于付款方与收款方都在政府账内，**税额部分相抵，资金不离开政府池**，
故只登记税额而不重复计入 `TaxCollected`（否则税收与资金流对不上）。

**售力（G6）**：`SellPower` 原先只扣货款。新增**税额的真实扣款**：

```go
tax := fiscal.TaxOn(sale.Paid, s.Params.TaxRate)
if tax > 0 {
    s.debitPayer(sale.Payer, sale.BuildingIndex, tax)   // 从付款方真实扣除
    s.Gov.Cash.Add(tax)
    s.Gov.TaxCollected += tax
}
```

并把 `balanceOf` 改为按**含税口径**折算可动用资金，否则成交后余额会被税收击穿：

```go
return s.Gov.AvailableCash(powerPrice) / taxMul
```

### 4.4 政府自建项目付款（新发现的一处漏记）

**改动前**（`fiscal.go` 原 440–447 行）：

```go
if req.Payer == "gov" {
    // 内部转账：只做资金来源约束，不再重复进出政府账。
    if !g.CanAfford(paid, salePrice) { ... }
    g.BuildoutPaid += paid
}
```

只累加 `BuildoutPaid`，**不动任何账户余额**——政府项目既不花钱、
也不给建造力卖方收入，工程是白得的。

**改动后**：

```go
gross := paid * (1 + taxRate)
if !g.CanAfford(gross, salePrice) { ... }
g.Cash.Add(-gross)              // 政府真实付款
credit(paid)                    // 建造力卖方真实入账（新增 credit 回调）
g.TaxCollected += paid * taxRate  // 税入库
g.BuildoutPaid += gross
g.OwnBuildoutPaid += gross      // 新增字段：真实走完双边记账的部分
```

**注意**：不能再把 Tax 加回政府现金池——那是政府自己收自己，
加了会让政府现金池净减 `Net` 而税收凭空出现 `Tax`，两本账对不上。

**新增参数**：`SellPower` 增加 `credit func(net float64)` 回调；
`Government` 增加 `TaxRate` 与 `OwnBuildoutPaid` 两个字段。

---

## 五、改动 5：消除重复记账

**文件**：`VC_Go/gosim/internal/fiscal/fiscal.go`

**改动前**（原 `DistributeProfit`）：

```go
func (g *Government) DistributeProfit(profit float64, govShare float64) float64 {
    govPart := profit * govShare
    privPart := profit * (1 - govShare)
    g.Cash.Add(govPart)          // 政府拿一份
    g.OperatingIncome += govPart
    return privPart              // 私有部分由调用方再入资本池
}
```

而调用方 `step.go` 同时又做了 `b.Cash.Add(b.LastProfit)`——建筑池拿**全额**利润。
同一笔利润被记两次。实测 **Δ货币/Σ利润 = 2.00**，连续 6 个 tick 稳定成立。

**改动后**：删除 `DistributeProfit`，改为纯函数 `ShareProfit`：

```go
func ShareProfit(profit, govShare, retainRatio float64) (gov, capital, retain float64) {
    gov = profit * govShare
    priv := profit * (1 - govShare)
    retain = priv * retainRatio
    capital = priv - retain
    return
}
```

**核心不变量**：`gov + capital + retain ≡ profit`（逐位相等）。
这是"划分"与"复制"的分界，也是消除重复记账的充要条件。

`step.go` 的调用方相应改为：

```go
gov, capital, retain := fiscal.ShareProfit(b.LastProfit, share, s.Params.RetainRatio)
govOperating += gov
capitalIncome += capital
b.Cash.Add(retain)              // 建筑池只拿【留存】份额，不再拿全额
```

**新增契约参数**：`Params.RetainRatio`（默认 0.5）——
私人份额中留存于建筑现金池、用于自身扩建的比例。
依据：§4.3 要求"私有建筑的扩建资金从其自身现金池划拨"。

**验证**：`fiscal_test.go` 新增两个测试
- `TestProfitDistributionSplitsByOwnership`：三份额之和恒等于利润
- `TestShareProfitSumsToProfitForAllRatios`：遍历 6×6×3 组边界比例，和恒等于利润

---

## 六、改动 6：初始货币存量由流量导出

**文件**：`VC_Go/gosim/internal/sim/state.go`，`New`

**改动前**：

```go
debtCap := st.Gov.DebtCap(st.Goods[powerGoodIndex].Pcost)
startup := debtCap * opt.GovStartupFraction
st.Gov.Cash.Balance = startup
st.Cap.Cash.Balance = startup / 3
for i := range st.Buildings {
    st.Buildings[i].Cash.Balance = startup / float64(len(st.Buildings))
}
```

初始货币总量 5,075,000，而一个 tick 的工资总额是 24,444,555——
**货币存量只有一周工资的 1/25**。在这样的存量下，"工资实际支付"根本不可能发生。
这正是旧实现把工资做成"只扣成本、不付出"的隐性原因。

**改动后**：按**一周工资总额**给存量，并分配到四个持有主体：

```go
wageBill := st.wageBillNow()                    // 一个 tick 的工资总额
st.Gov.Cash.Balance  = 0.50 * wageBill          // 政府起步流动性
st.Cap.Cash.Balance  = 0.25 * wageBill          // 金融区营运资金
firmStock            = 1.00 * wageBill          // 建筑池（基数 + 按级数分摊）
```

建筑池按"基数 + 按级数"分摊，而不是纯按级数：
建造部门默认只有 20 级，而它要垫付钢/铁/工具的中间投入（每级约 8.8 万元），
纯按级数分到的钱不足以做第一笔采购，会立刻透支并让整条扩建链断掉。

**新增常量**（三个比例是建模选择，不是契约参数，契约改为按流量标定）：

```go
const (
    startupGovFraction  = 0.50
    startupCapFraction  = 0.25
    startupFirmFraction = 1.00
)
```

**守恒约束**：人群池初始现金为 **0**——居民的第一次收入来自第一期工资。
给初始现金等于凭空注入一笔没有来源的货币。

---

## 七、改动 7：开局布点算法重写

**文件**：`VC_Go/gosim/internal/sim/state.go`，`initialLayout`

这是本轮改动中影响最大、也是此前最隐蔽的一处。

### 7.1 旧算法的问题

旧算法取 `Y = (I−A)⁻¹·f` 作为总产出目标，再除以单级产出得级数。
实测它给出的产业链级数是个位数：

| 建筑 | 旧算法级数 | 申报投入 | 实际产出 | 配给比 |
|------|-----------|---------|---------|--------|
| 煤矿 | 0.98 | 52.8 | 9.8 | 0.167 |
| 铁矿 | 1.06 | 544.6 | 10.6 | 0.117 |
| 炼钢厂 | 0.74 | 547.7 | 7.8 | 0.122 |
| 工具厂 | 0.96 | 459.0 | 9.4 | 0.167 |

下游建筑的短缺惩罚被压到 0.12~0.17，净供给塌到需求的一小部分，
**经济从第 0 tick 起就不可能出清**。

**根因**：Leontief 完全需求矩阵是按**价格方程**的单位需求系数构造的
（`A[i][j] = 投入量/产出量`）。这对价格是对的，但用它反推**级数**时，
各种商品的"单级产出量"差异极大（谷物 50、建造力 15），
归一化后的系数会严重低估高投入比部门的实际消耗：

```
建造部门每级：25 钢 + 25 铁 + 20 工具，单级只产 15 建造力
⇒ 20 级建造部门就要吞掉 500 单位铁
⇒ 而按需求反推出的铁总产出只有 63 单位
```

旧算法还**完全漏掉了建造部门**（在旧代码里被显式排除在迭代之外），
而它恰恰是最大的中间需求来源。

### 7.2 新算法

改为**直接按级数做物质平衡**，不经任何归一化：

```
每种商品的级数 L_i 必须满足：q_i·L_i = 最终需求_i + Σ_j c_ij·L_j
```

其中 `c_ij` 是建筑 i 每级对商品 j 的消耗量，由 §3.3 配方直接给出。
用迭代求解该不动点。建造力是投资品（§3.3 配方里没有任何建筑消耗它），
其级数由 `Options.InitialPowerLevel` 给定，但**它对钢/铁/工具的消耗必须完整计入**。

自给农场挤占专业耕地的处理单独迭代一次：自给产出先抵扣谷物的最终需求，
再决定专业谷物农场与棉花种植园的级数，两者合计受耕地上限约束。

### 7.3 修复效果

| 建筑 | 旧级数 | 新级数 | 配给比 |
|------|-------|-------|--------|
| 谷物农场 | 9.63 | 4.66 | 1.0000 |
| 加工食品厂 | 6.47 | 5.82 | 1.0000 |
| 煤矿 | 0.98 | **10.94** | **1.0000** |
| 铁矿 | 1.06 | **16.50** | **1.0000** |
| 炼钢厂 | 0.74 | **8.17** | **1.0000** |
| 工具厂 | 0.96 | **10.47** | **1.0000** |
| 住房 | 5.70 | 5.13 | 1.0000 |

**全部商品的配给比 = 1.0000**，产业链自身匹配。这是本轮最重要的修复。

---

## 八、改动 8：货币守恒守门测试

**文件**：`VC_Go/gosim/internal/sim/state.go`、`state_test.go`

**新增诊断结构 `Recon`（建筑池对账）+ `TickRecon`（四池对账）**：

```go
// 建筑池的 Δ 必须能被下列各项完全解释，残差恒为 0：
//   Δ建筑 = 消费者收入 + 中间投入收入 + 建造力收入 + 经营留存 + 金融区净额
//         + 新资本 − 工资 − 中间投入付款(含税) − 建造力支出
```

**三个易错点在注释中显式标注**：
1. 不要重复计入资本份额（`FinanceProfit` 已含）
2. `IntermediateOut` 已含税，不得再加一次
3. `PowerRevenue` 与 `PowerSpend` 必须成对出现

**新增测试**：

| 测试 | 断言 |
|------|------|
| `TestMoneyIsConserved` | 四类账户之和逐 tick 恒定（仅 `NewCapital` 为合法注入） |
| `TestWagesAreActuallyPaid` | 首 tick 人群池流入恰等于工资总额 |
| `TestGovCashFlowIsFullyExplained` | 政府池 Δ 被完整资金流解释，残差为 0 |
| `TestGovOwnBuildoutDoesFullDoubleEntry` | 政府自建项目三本账合账为零 |

**新增字段**：`Snapshot.TotalMoney`（守恒审计口径，政府池按实际值计入）、
`Snapshot.NewCapital`（唯一合法的货币注入）、`State.InvariantErr`。

---

## 九、当前状态与未完成项

### 已完成且已验证

- 编译通过：`go build ./...` exit=0；`go vet ./...` exit=0
- **三处记账缺陷全部消除**：
  - 利润重复入账（2× 特征值）→ 改为划分，`fiscal` 包两个专项测试通过
  - 工资从未支付 → 真实划转，`TestWagesAreActuallyPaid` 通过
  - 政府自建项目付款无对手方 → 完整双边记账，专项测试通过
- **开局布点修复**：全部商品配给比 = 1.0000（旧算法煤 0.167 / 铁 0.117）
- **建筑现金池逐建筑对账完全对平**：12 类建筑残差全部为 0
- 人群池、工资划转、逐池消费、全过程交易税均已落地

### 未完成 A：建造力买卖的政府池恒等式仍有残差

**现象**：`TestGovCashFlowIsFullyExplained` 在 tick 1 报告残差 −28,773.52。
`TestMoneyIsConserved` 报告总货币每 tick 变动 −2,409,428。

**已定位的部分**：建筑池（12 类建筑逐项）已完全对平，残差不在建筑侧；
政府池的残差随建造力采购/售出的税额规模同阶变化，说明它在
"政府既是买方又是卖方"这条路径上。已修正两处：

1. `GovPowerSpend` 改为按**账户前后差**计量（`powerSpendActual`），
   而不是 `PowerPurchased × price` —— 后者在政府资金不足、只买下部分产出时失真。
2. 采购环节补上对称的税额记账：政府池净流出 = Gross − Tax = Net，
   建造部门 +Net，税收 +Tax。

**尚未解决**：`SellPower` 内部对 `Payer=="gov"` 分支同时做
"扣 Gross"与"记 OwnBuildoutPaid=Gross"，而调用方又在循环里对
非政府付款方补扣税额，两条路径的税基口径仍不完全一致。
需要在契约层面先定案"政府自建项目的税基与税额归属"，再改代码。

### 未完成 B：需求缩放系数 k 的重标定

**现象**：实测工资总额 2,935,597，而消费支出（税前）只有 931,758，
**支出/工资 = 31.7%**。诊断显示全部消费组满足度 = 1.000，
缺货池数 = 0，缺钱池数 = 0 —— 即消费者**已经买满了目标量**。

所以消费低不是"买不起"或"买不到"，而是**需求表本身给的目标量太小**：

```
工资总额        = 2,935,597
消费支出(税前)  =   931,758
当前 k          = 1.0400
应为 k = 工资/支出 = 3.1506
```

**根因**：`calibrate.jointDemandScale` 标定 k 时用的是
`y = LeontiefInv × f` 反推出的**理论级数**，而布点算法（改动 11）改正后，
实际级数包含完整的中间消耗循环，比理论级数大 3.15 倍。
两者不在同一量级，故 k 失效。

**为什么不能简单把 k 调成 3.15**：需求放大 3.15 倍 → 布点级数也放大 →
工资也放大 3.15 倍 → 比值不变。实测 `工资(k)/需求价值(k) ≡ 3.1506` 对 k 恒定，
迭代不收敛。除非布点算法中的非线性约束（最少 0.5 级、耕地上限 10,000、
煤/铁上限 500、建造部门上限 1,000）变为活跃 —— 而当前它们都不活跃。

**结论**：这是**契约层面的结构问题**，与 `VC_Go/docs/AUDIT-1.0.md` §7 记的
"上游恒亏"同源。具体表现为：

> 契约的 §3.3 投入产出表与 §6.3 消费需求表在给定人口下不兼容 ——
> 为了生产出满足 §6.3 的最终消费品，产业链需要的劳动力
> （继而是工资）是最终消费品价值的 3.15 倍。

**需要裁决的方向**（三选一，不能由实现单方面决定）：

1. **调整建造部门配方**：每级 25 钢 + 25 铁 + 20 工具 → 15 建造力，
   投入比过高，是上游膨胀的主要驱动。但改配方会改 §3.4 的零利润价。
2. **调整需求表量级**：把 §6.3 的需求量整体上调（相当于把 k 并入需求表重标定），
   使"最终需求价值 = 工资"成立。改动最小，但要重新验证 §6.3 的锚点。
3. **承认资本有机构成**：接受"工资 > 消费品价值"，并补上一条
   **利润回流**机制（资本家的储蓄转化为投资需求），使货币闭环闭合。
   这是最贴近真实经济的方案，但需要新增契约条文。

### 已完成 D：契约文档同步（本次完成）

`VC_Go/docs/1.0 生产与市场模拟.md` 已同步本次全部改动。逐节：

| 契约位置 | 同步内容 |
|----------|----------|
| 文首修订说明 | 新增本轮修订总览（8 项），并保留上一轮作为"上次修订" |
| §1 版本目标 | 加入人群现金池、私有化、全过程交易税与唯一记账入口；标注三表联合标定的**已知失效** |
| §2.5 参数表 | 补入 $N_0$、$r_{ret}$、$\text{PrivatizeEnabled}$、$\text{AllowPrivatize}_i$、$\text{PrivatizeMargin}$、$\text{PrivatizeStep}$、$\text{PrivatizePriceMult}$ |
| §3.2 | 新增**起始等级** $N_0 = 5$ 条款（含两个例外与理由） |
| §4.3 | 初始货币**由流量标定**（1.75 × 周工资，四主体分配）；**更正**旧注中"工资成本不从现金池实时扣划"的失效表述 |
| §4.5.1 | 利润**划分而非复制**（含三份额公式与留存比例）；记录该处历史两次犯错 |
| **§4.5.1a（新增）** | 私有化机制：两级开关、逐建筑默认值表、触发条件、转让方式、对价公式、记账、可达性实测 |
| §4.5.3 | 全过程交易税的记账口径；"政府自己收自己"的处理；**唯一记账入口**与**记账不变量**声明 |
| §5.1（新增） | 人群与阶级现金池：36 个池、逐池财富档、工资划转与消费结算公式、为何必须逐池定档 |
| §5.2（新增） | 雇佣调整与缩编的年化语义、实测确认（12/12）、方向性提示 |
| §8 主循环 | 12 步 → **17 步**，补入工资划转、逐池消费、私有化，并写明顺序敏感点；新增**记账纪律**段落 |
| §8.4 | 新增判据 **A8 货币守恒**（含实测状态）；说明它为何是第一个结构性判据 |
| §8.5 | 阶段目标纳入 A8；配套指标补入私有化指标与货币守恒指标；"已知未达标项"更新为两条耦合的结构问题 + 三个候选裁决方向 |
| **§8.6（新增）** | 人口 10m 的**平衡价格与平衡等级**推导表；三条不可达性障碍的定量说明 |
| §1 / §3.4 / §4.5.1a / §8.5 | 修正"需求侧量级缺口 3.15 倍"的**误诊**（实为起点产能构成的函数，与 §6.3 表量级无关） |

---

# 第三轮：人口 10m + 平衡推导 + 归因纠正

本轮把人口默认值改为 10,000,000，手工推演了平衡价格与平衡等级，
并在此过程中**纠正了上一轮留下的一处误诊**。

## F1. 人口默认值 5,000,000 → 10,000,000

| 位置 | 改动 |
|------|------|
| `cmd/market-sim/main.go` | `-pop` 默认值 → `10_000_000`；用法注释同步 |
| `cmd/diag_govcash/main.go` | `Population` → `10_000_000` |
| `internal/sim/state_test.go` | `newTestState` 人口 → `10_000_000`（原先 200,000 是"让测试跑得快"的旧值，会使满足度/成交额断言测到另一套经济） |
| `internal/sim/audit_test.go` | `auditState` 人口 → `10_000_000` |

## F2. 平衡价格与平衡等级的推导（新增 §8.6）

两条独立路径互验（Go 的 `TestAuditEquilibrium` 与 `VC_Go/tools/equilibrium_handcheck.js`）：

- **平衡价格** $P^* = P_{cost}$。逐商品回代零利润价方程，**最大相对偏差 0.0675%**
  （建造部门 1.00 元），全部来自 §3.1 价格表取整 ⇒ 价格表自洽。
- **平衡等级**：零利润平衡要求 $E = D - S = 0$，且在 $P^* = P_{cost}$ 处 $D = a$，
  故条件化为 $S^* = a$。因成本含中间投入、中间投入随产能变化，需解
  $$L^*_i = \frac{\text{最终需求}_i + \sum_j c_{ij} L^*_j}{q_i}$$
  41 轮不动点迭代收敛，合计 **688.61 级**。
- **"初始等级 < 平衡等级"成立**：10 种生产建筑全部严格小于 $L^*$，
  最紧的煤/铁/钢 $7.56 > 5$。建造部门例外（建造力是投资品、无最终需求、
  也不被任何配方消耗，按消费需求口径 $L^* = 0$，由 `InitialPowerLevel` 另行给定）。

## F3. 归因纠正：所谓"3.15 倍需求侧量级缺口"是误诊

上一轮把"工资 ÷ 已实现消费支出 = 3.15"记为"§6.3 需求表的量级本身太小"，
并据此把"整体上调需求表"列为候选裁决方向。**复测证明该归因不成立**：

$$\frac{\text{工资}}{\text{最终需求价值}} = \frac{\sum L_i}{\sum L^*_i}$$

需求缩放系数 $k$ 在分子分母各出现一次、恰好抵消（"改 $k$ 无效"的判断是对的），
但由此只能推出"改 $k$ 无效"，推不出"缺口在需求表量级"。实测：
统一 5 级布点 $50.00/688.61 = 0.0726$（倒数 13.77）、物质平衡布点 $1436/716 = 2.01$、
需求匹配布点恒为 $1.0000$。即该比值**只是起点产能构成的函数**。

新增 `TestAuditLevelCompositionGap` 固化这一恒等式并断言实际布点比值
恰等于级数构成比（容差 1e-4，残差 6e-6 来自迭代收敛）。

**处理**：契约 §1、§3.4、§4.5.1a、§8.5 四处同步更正；
从候选裁决方向中**剔除**"调整需求表量级"。

## F4. 本轮新量化的两条独立结构问题

| # | 问题 | 定量 |
|---|------|------|
| 1 | 需求缩放 $k$ 按 $t=0$ 标定，而 §4.5.3 收全过程 10% 交易税 | 需求匹配布点上税后可购价值只覆盖名义需求的 $0.909157$，**缺口 $9.0843\%$** |
| 2 | G7 债务上限资产基数只取建造力产出 | $\text{DebtCap} = 2.739116 \times \text{PowerOutput} \times P_{power}$，只够 $3\sim6$ 个周期；而政府缺口 $\approx 0.61W$ |

**主因判定**：政府持股约 70%、交易税只收到产出的约 $9.5\%$，故
$\text{缺口} \approx 0.70W - 0.095W = 0.61W$。该式不含 $k$、不含起点布点——
`TestAuditSurvival` 四组对照证实：$t = 0$ 与 $t = 0.10$ 轨迹几乎重合，
换物质平衡布点（工资提高 16.8 倍）后死亡时点相同。

## 本轮新增/修改的测试

| 文件 | 用途 |
|------|------|
| `internal/sim/audit_equilibrium_test.go` | 平衡价格与平衡等级推导；平衡态可支付性；级数构成比归因（`TestAuditEquilibrium`、`TestAuditLevelCompositionGap`） |
| `internal/sim/audit_start_diag_test.go` | 起点活动度分解：生产 / 名义需求 / 购买力 / 成交（`TestAuditStartDiag`） |
| `internal/sim/audit_survival_test.go` | 布点 × 税率四组存活对照（`TestAuditSurvival`） |
| `VC_Go/tools/equilibrium_handcheck.js` | 独立于 Go 的手工复算（离散表口径、零利润价自校验、预算闭合） |

## F5. 修复逐建筑对账缺陷（"全局全绿"掩盖"明细口径分叉"）

排查过程中发现逐建筑对账残差 **2,134,022**，而货币守恒、借贷相等、
政府现金流分解**三项同时全绿**。定位到三处独立缺陷：

| # | 缺陷 | 位置 |
|---|------|------|
| 1 | `ConsumerIn` / `InputIn` 两个对账字段从未被填充 | `distributeConsumerRevenue` 是死代码；`InputIn` 无赋值处 |
| 2 | `InputOut` 记税前申报额，而记账簿实际借记含税实付额（且现金不足时缩减） | `step.go` ⑤ |
| 3 | 利润划分被当成"留存份额"，但记账腿是「借全额利润、贷留存份额」，净腿 = `留存 − 利润` | `step.go` ⑦ |

缺陷 3 是主项，误差达 `2×|利润|` 量级。

**修复**：

- `book.Consume` 增加第三个返回值：**逐卖方账户实际收到的净额**；
- `book.PayIntermediate` 增加第四个返回值：**逐卖方实际收到的净额**（`paid` 已有逐买方实付）；
- 新增 `BuildingRecon.ProfitLeg` 与 `Recon.ProfitLegTotal` 记录利润划分净腿；
  `RetainedProfit` 降为报表口径；`Expected()` / `ExpectedDelta()` 改用 `ProfitLeg` / `ProfitLegTotal`；
- **删除死代码 `distributeConsumerRevenue`**——它若被误接回去会把同一笔消费者货款
  贷记两次，直接破坏货币守恒，故连同其重算逻辑一并删除；
- `TestAuditBuildingRecon` 原先只打印不断言（注释却写"必须被完全解释"），
  补上汇总与逐建筑两级残差断言。

**修复后**：汇总残差 `0.00`，逐建筑残差全部 `0.00`。

> **口径教训（已写入契约 §8 记账纪律的精神）**：全局不变量能证明"钱没丢也没多"，
> 但不能证明"每一笔都归对了户"——前者是全账户求和后的标量，后者是逐账户的向量。
> 因此逐账户对账必须有独立断言，且口径直接取自记账处，不在诊断侧重算。


## 复现命令（本轮）

```powershell
$env:DSH_AUDIT="1"
go test ./internal/sim/ -run 'TestAuditEquilibrium|TestAuditLevelCompositionGap' -v
go test ./internal/sim/ -run TestAuditStartDiag -v
go test ./internal/sim/ -run TestAuditSurvival -v
go run ./cmd/market-sim -ticks 3000                 # 基线（统一 5 级）
go run ./cmd/market-sim -ticks 800 -init-level 0    # 物质平衡布点对照
node VC_Go/tools/equilibrium_handcheck.js
```


配套产物 `VC_Go/docs/1.0 生产与市场模拟.html` 已用 `VC_Go/tools/md2html.js` 重新生成并通过 `VC_Go/tools/html_audit.js` 结构校验：

```
公式 457 · 表格 14 · 标题 37 · 渲染失败 0 · LaTeX 残留 0
37 个锚点全部有对应标题 · 全部标签配对 · 全部检查通过
```

### 未完成 E：需求侧量级缺口（需契约层裁决）

同"未完成 B"，仍是开放项。契约已**如实记录**在 §8.4 末，并列出三个候选裁决方向，
未擅自选定。

---

## 十一、复现命令

```powershell
$env:GOROOT  = "D:\DSH Desktop\Code\YehenalaMarket\Go"
$env:PATH    = "$env:GOROOT\bin;$env:PATH"
$env:GOCACHE = "D:\DSH Desktop\Code\YehenalaMarket\out\gocache"
$env:GOPATH  = "D:\DSH Desktop\Code\YehenalaMarket\out\gopath"
$env:GOTMPDIR= "D:\DSH Desktop\Code\YehenalaMarket\out\gotmp"
Set-Location "D:\DSH Desktop\Code\YehenalaMarket\gosim"

go build ./...                          # exit=0
go vet ./...                            # exit=0
go test ./... -count=1                  # 7 个包全绿

# 全部审计测试（16 项；含三项任务、记账、布点、消费、量级缺口）
$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAudit -v

# 三项任务专项
$env:DSH_AUDIT="1"; go test ./internal/sim/ -run TestAuditTask -v

# 货币守恒与借贷校验
go run ./cmd/diag_govcash

# 契约文档重新渲染（md 为唯一源）
Set-Location ..
node VC_Go/tools/md2html.js "VC_Go/docs/1.0 生产与市场模拟.md" "VC_Go/docs/1.0 生产与市场模拟.html"
node VC_Go/tools/html_audit.js "VC_Go/docs/1.0 生产与市场模拟.html"
```
