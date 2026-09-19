# 1.0 资金流量表与唯一记账入口

> 本文档是 `VC_Go/gosim/internal/ledger/` 包的设计契约，也是 §4.5.3「总流通货币不变」的
> 可执行定义。它与代码一一对应：表里每一行都对应一个交易构造函数，
> 每个构造函数都保证**借贷相等**。

---

## 一、为什么需要这张表

修订前，"钱从哪个池扣、记到哪个池"散落在 `sim` / `fiscal` / `consume` / `cohort`
四个包的十几个 `Cash.Add` 调用点上，各写各的。后果是货币守恒只能靠**事后审计**
发现，而审计每修一处、残差就跑到另一处——实测连续暴露了四个不同层级的缺陷：

| # | 缺陷 | 表现 |
|---|------|------|
| ① | 工资只作为成本扣减，钱从未真正付出 | 居民没有账户；工资 1,712,219 而消费仅 486,255 |
| ② | 利润被建筑现金池与政府池各记一次 | Δ货币/Σ利润 = **2.00**，连续 6 个 tick 稳定 |
| ③ | 对中间投入计征的税，政府收了但无人被扣 | 隐含税率 0.176，正确值应为 0.0909 |
| ④ | 政府自建项目付款只累加计数、不动任何余额 | 工程白得；残差恰等于该笔付款 |

**收敛方案**：把记账收敛为一条规则——**每一笔交易都必须借贷相等**。
`ledger.Auditor.Post` 校验 Σ借 == Σ贷（逐位相等），不相等直接报错。
于是"货币守恒"不再需要追查，而是**构造性事实**：任何一笔交易的净效应都为零。

---

## 二、账户清单（15 类）

| 账户 | 数量 | 说明 |
|------|------|------|
| 政府现金池 `Gov` | 1 | 允许为负（§4.5.4 的债务） |
| 资本现金池 `Capital` | 1 | 金融区持有（G4/G5） |
| 建筑现金池 `Building[i]` | 12 | i = 0..11，含金融区自身 |
| 人群现金池 `Household[场地][阶级]` | 12 × 3 = 36 | §5.1 修订 |

账户标识由 `ledger.Account{Kind, Index}` 给出，构造器：
`Gov()` / `Capital()` / `Building(i)` / `Household(worksite, class, classes)`。

余额**只有一份**，存放在 `ledger.Auditor` 里。各业务结构（`fiscal.Government`、
`fiscal.Capital`、`cohort.Pool`、`sim.BuildingState`）只持有账户标识——
"同一笔钱被两个池各记一次"在结构上不可能发生。

---

## 三、资金流量表

借 = 余额减少，贷 = 余额增加。每行都满足 Σ借 = Σ贷。

| # | 交易 | 借 | 贷 | 构造函数 |
|---|------|----|----|----------|
| ① | 工资 | 建筑[i] 工资总额 | 人群[i][c] 各阶级份额 | `Wage(site, classAmounts)` |
| ② | 消费 | 人群[池] Gross | 建筑[卖方] Net<br>政府 Tax | `ConsumerPurchase(hh, seller, net, t)` |
| ③ | 中间投入 | 建筑[买方] Gross | 建筑[卖方] Net<br>政府 Tax | `Intermediate(buyer, seller, net, t)` |
| ④ | 政府采购建造力 | 政府 Gross | 建筑[建造力] Net<br>政府 Tax | `PowerPurchase(powerIdx, net, t)` |
| ⑤ | 政府售力给外部 | 付款方 Gross | 政府 Gross | `PowerSale(payer, net, t)` |
| ⑥ | 政府自建付款 | 政府 Gross | 建筑[建造力] Net<br>政府 Tax | `GovOwnBuildout(powerIdx, net, t)` |
| ⑦ | 利润划分 | 建筑[i] 利润 | 政府 gov份额<br>资本 capital份额<br>建筑[i] 留存 | `ProfitSplit(i, gov, capital, retain)` |
| ⑧ | 新建营运本金 | （无） | 建筑[i] 金额 | `NewCapital(i, amount)` → `PostInjection` |

其中 `Gross = Net × (1 + t)`。

### 3.1 ④⑤⑥ 里的"自己收自己"

这三类的贷方有一笔是**政府收政府的税**：资金不离开政府池，故政府对这笔交易的
净支出恰好等于 Net。

**必须显式写出这笔（而不是省掉），否则借贷不相等。** 此前反复出错正是省掉它的结果：

- 省掉 ④ 的 `贷 政府 Tax` → 政府净减 Gross 而建造部门只收 Net，差 `Net·t`
  （实测 15,071.84 = 150,718.44 × 10%）
- 省掉 ⑥ 的 `贷 政府 Tax` → 工程白得，审计看到一笔无人认领的差额

### 3.2 ⑧ 是唯一允许借贷不等的交易

它是系统里**唯一合法的货币注入**（§4.3 新建筑的营运本金）。为此它必须走
`PostInjection` 而不是 `Post`——这样"凭空造币"这件事在**类型层面就是显式的**，
任何调用点都可被审查。

---

## 四、实现纪律

1. **所有资金流动必须走 `Post`**，且必须经上表的构造函数构造，不许在业务代码里
   手工拼 `Debit`/`Credit`——那样又会回到"各写各的"的老问题。
2. **直接调用 `Ledger.Add` 是禁止的**，它只用于开局注资与调试。
   运行期出现 `Add` 即视为待收敛的技术债。
3. **新增交易类型必须先在本文档登记**，再在 `rules.go` 里新增构造函数与自测。
4. 借贷不等**不被静默修正**：`Post` 返回 error 并记入 `Violations()`，
   由守门测试断言为空。

---

## 五、守门测试（`ledger_test.go`，7 项全绿）

| 测试 | 断言 |
|------|------|
| `TestAllTxnTypesBalance` | 上表 8 类交易的借贷都相等 |
| `TestPostRejectsUnbalanced` | 借贷不等的交易被拒绝，且不改动任何余额 |
| `TestMoneyIsConservedByConstruction` | 11 笔连续交易后货币存量逐位不变 |
| `TestInjectionIsOnlySourceOfGrowth` | 只有 `PostInjection` 能改变货币存量 |
| `TestPowerPurchaseNetCostEqualsNet` | ④⑥ 的政府净支出恰为 Net |
| `TestProfitSplitSumsToProfit` | ⑦ 的三份额之和恒等于利润 |
| `TestAccountTotalMatchesSumOfPools` | 货币总量 = 各类账户之和 |

第 3 项是关键：它**不依赖任何业务逻辑**，只依赖"每笔交易借贷相等"，
因此天然覆盖全部交易类型，包括未来新增的类型。

---

## 六、接入状态（已完成）

| 包 | 状态 |
|----|------|
| `internal/ledger` | ✅ 机制层，7 项测试全绿 |
| `internal/book` | ✅ **统一记账簿**，6 项测试全绿 |
| `internal/fiscal` | ✅ 已挂接审计账本 |
| `internal/cohort` | ✅ 已挂接，人口登记与记账分离 |
| `internal/sim` | ✅ **全部资金流动已经过 book** |

### 6.1 实跑结论（`go run ./cmd/diag_govcash`）

```
货币守恒检验（§4.5.3）：初值 37,762,461 → 末值 37,763,410，净变动 949
（唯一允许的来源是新建营运本金，累计 949）
借贷校验：全部交易借贷相等（0 条违规）
债务触限 tick 数 = 0 / 60
总级数 = 737.9
```

- **货币净变动 949 = 累计营运本金注入 949**：除唯一合法注入外，货币逐位不变。
- **借贷违规 0 条**：每一笔交易都满足 Σ借 = Σ贷。
- **债务触限 0/60**：修复 `Gov.PowerOutput` 未初始化后，政府不再被 0 上限锁死（原为 59/60）。
- **总级数稳定在 737.9**：经济不再坍缩。

### 6.2 迁移过程中修掉的四个真实缺陷

迁移到 `book` 的过程中，原先被"分两处写"掩盖的缺陷逐个暴露并被修掉：

| # | 缺陷 | 症状 | 修复 |
|---|------|------|------|
| 1 | 政府份额被记两遍 | `ProfitSplit` 已贷记政府，下面又 `Gov.Cash.Add(govOperating)` | 删除重复入账 |
| 2 | 自建付款把含税总额当净额传入 | 税被算两遍，残差 = 自建税额 | 传净额，让 book 内部乘 (1+t) |
| 3 | 外部税未汇总 | `book` 把税打进政府池但不维护 `Gov.TaxCollected`，分解式凭空丢税 | 在 `fillFlow` 按来源统一汇总 |
| 4 | 自建付款的流出量记成 Gross | 政府池实际只走 Net（Tax 回流自己） | 分解式改用 Net |

### 6.3 尚未消除的一处：tick 1 的分解残差

`tick 1` 的政府池分解仍报出 **−268,395.45** 的残差，`tick 2` 起为 0。

这是**分解口径在首 tick 的不一致**（收入入账与采购预算的时序），
**不是货币守恒问题**——守恒已由 §6.1 的实跑证明（净变动 = 注入额）。

已定位到唯一入口：`New()` 里 `Gov.PowerOutput` 现在被正确初始化，
但首 tick 的 `flowGovOperatingDelta` 与 `ProfitSplit` 的实际入账在
"金融区净额"上仍差一笔。修掉后分解式将在首 tick 也恒为 0。

---

## 七、顺带修复的真实 bug

`New()` 从未初始化 `Gov.PowerOutput`，而它只在 `step` 的采购步骤里被设置。
结果是开局到第一个 tick 之间它为 0，导致：

```
资产基数 = 0  →  债务上限 = 0  →  可动用资金 = 0
```

政府的**任何主动支出都被拒绝**（实测"债务触限 tick 数 = 59/60"）。
修复后同一输出为：

```
建造力产出(PowerOutput)= 300.00
资产基数(P_cost=7250) = 2175000
债务上限              = 4350000
可动用资金            = 4350000
```

---

## 八、复现命令

```powershell
$env:GOROOT  = "D:\DSH Desktop\Code\YehenalaMarket\Go"
$env:PATH    = "$env:GOROOT\bin;$env:PATH"
$env:GOCACHE = "D:\DSH Desktop\Code\YehenalaMarket\out\gocache"
$env:GOPATH  = "D:\DSH Desktop\Code\YehenalaMarket\out\gopath"
$env:GOTMPDIR= "D:\DSH Desktop\Code\YehenalaMarket\out\gotmp"
Set-Location "D:\DSH Desktop\Code\YehenalaMarket\gosim"

go build ./...                          # exit=0
go test ./internal/ledger/ -v           # 7 项全绿
go test ./internal/fiscal/ -v           # 全绿
go run ./cmd/diag_govcash               # 借贷校验、货币守恒检验
```
