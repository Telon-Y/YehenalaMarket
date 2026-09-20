# EVIDENCE-CAVEATS —— 本目录证据的来源、口径与已知缺陷

> # ⛔ 部分已失效（2026-09-20）
>
> **本文件的口径说明仍有效**（"本目录证据都是 2026-09-19 重跑产物、非原始证据"这一条依然成立），
> 但其中对**确定性**与 **`diag_govcash` 残差成因**的叙述**已被代码推翻**：
>
> | 本文旧叙述 | 现状 |
> |---|---|
> | 模拟非逐位可复现（货币抖动 0.061%） | **已修复**（R72 `ledger.sortedAccounts()`）；本次 6 次复跑逐位一致 |
> | `diag_govcash` 残差 = −`GovSelfTax` | 该字段**已不存在**；真因是漏 `−GovWelfare/−GovPublicWorks/−GovWarehouseExpand` 三项 |
>
> **引用本目录证据前，请先读**：`docs/verdict/1.0 验收判决书.md`（2026-09-20 重审版）
> 与 `docs/ACTIVE.md` 末尾的 **「统一矫正 C1」**。

> **一句话**：`out/verdict/` 下的**所有**文件都是 **2026-09-19 在本机重新生成**的，
> 不是原始证据。原始 `out/` 从未入库（`.gitignore` 忽略），已不可恢复。
> 引用这里的数字时必须带「重跑」标注；本文件同时列出**重新生成过程中暴露出来的证据缺陷**。

---

## 一、生成环境（RERUN-ENV.txt 有原始记录）

| 项 | 值 |
|----|----|
| 仓库 | `D:\Code\YehenalaMarket` = GitHub `Telon-Y/YehenalaMarket` 的 `VC_Go/` 目录，commit `58f82e71` |
| Go | `D:\Code\Go\bin\go.exe`，**go1.26.5**（在系统 PATH 上） |
| C++ | `D:\Code\mingw64\bin\g++.exe`，**g++ 16.1.0** |
| JS | 本机**未安装 node/npm**；用 `D:\DSH\DSH Desktop\DSH Desktop.exe` 以 `ELECTRON_RUN_AS_NODE=1` 运行（**node v24.18.1 / Electron 43.3.0**） |
| Shell | Windows PowerShell 5.1（`rerun-all.ps1` 用 `powershell -ExecutionPolicy Bypass -File` 执行） |
| 一键复跑 | `out/verdict/rerun-all.ps1` |

> **文档里的路径是旧的**：`README.md` / `docs/ACTIVE.md` 的复现命令写 `GOROOT = D:\DSH Desktop\Code\YehenalaMarket\Go`、
> `Set-Location ...\gosim`。该目录在本机不存在（工具链在 `D:\Code\Go`），照抄命令必然失败——
> 这是"复现不了"的真实原因，**不是 Go 没装**。

## 二、文件清单与性质

| 文件 | 性质 | 说明 |
|------|------|------|
| `RERUN-ENV.txt` | 元数据 | 生成时间、工具链版本、构建命令 |
| `rerun-all.ps1` | 生成器 | 一键重跑本目录全部证据（幂等，可覆盖重跑） |
| `contract_consistency_probe.txt` | **重跑** | `node tools/contract_consistency_probe.js`（纯算术，无随机性） |
| `survival_condition_probe.txt` | **重跑 + 含缺陷** | `node tools/survival_condition_probe.js`；⚠ 见 §三.1–§三.3 |
| `claims-verified-js.txt` | **新工具** | `node tools/verify_claims.js`：把判决书里的定量断言变成机检项（JS 侧） |
| `contract-check-cpp.txt` | **新工具** | `out/bin/contract_check.exe`（源码 `tools/cpp/contract_check.cpp`）：同题独立复核（C++ / long double） |
| `sim-600-tax-sweep.txt` | **重跑** | 11 组 600 tick 实跑（8 档税率 × 默认布点 + 物质平衡布点 + 私有化开关），含逐组完整 stdout 与解析汇总表 |
| `sim-10000pop10m.txt` | **重跑** | 契约指定的验收长跑：`-ticks 10000`（人口 10m、t=0.10） |
| `sim-3000-init-level-0.txt` | **重跑** | 物质平衡布点对照：`-ticks 3000 -init-level 0` |
| `sim-600-tax-sweep.txt` 之后的三个诊断文件 | **重跑** | `first-zero-bisect.txt`（首次归零 tick 二分）、`determinism.txt`（逐位可复现性）、`diag_govcash.txt` |
| `audit-*.txt`、`audit-suite.txt` | **重跑** | 审计测试原始输出（`DSH_AUDIT=1`） |
| `VERDICT-1.0-acceptance.md` | **重建** | 判决书：结论取自重跑证据 + 文档记录，逐条标注「已复核 / 不能复现」 |
| `../core_sim_gov.txt` | **重跑** | `node tools/core_sim_gov.js 10000`（v4 闭环版；**不是** v3 沉淀式崩解版） |

**不可恢复**：原始 `out/verdict/*`、`out/archive/2026-09-18-equilibrium/*`、`out/sim_notax.log`、
`out/gosim_contract.txt`、v3 版本的 `core_sim_gov.js` 产物（现脚本已是 v4，见其头部注释）。
这些只出现在 `docs/AUDIT-1.0.md`、`docs/ACTIVE.md` 的引用里，没有任何副本入库。

## 三、重新生成时暴露出的证据缺陷（**引用前必读**）

### 1. `tools/survival_condition_probe.js` 的结论句是硬编码的，与它自己算出的数不符

该脚本把"计算值"和"结论文字"写在同一行，**结论文字不随计算变化**：

- `:117` 无条件打印「**§3.3 配方与 §8.6 的 L\* 表自洽**（偏差来自 §8.6 表列的取整）」，
  而同一段上方它自己算出的最大逐项偏差是 **53.33%**、合计 **699.72 级 vs 定案 688.61 级**。
- `:142` 在算出 1.117830 后仍打印「（契约 §8.5 定案 0.909157 / 9.0843%；本复算 …, 一致）」。
- `:149` 打印「税收 / 工资 = 0.1389（契约 §8.5 记为"约 9.5%"…）」——**括号里的"约 9.5%"是文本，不是算出来的**。
- `:154` 打印「缺口 = 0.5611·W（契约 §8.5 定案 0.61W，本复算 0.5611W，**吻合**）」。
- `:172–196` 的 10 组实跑对照表（`ARMS`）是**转录常数**，脚本本身不跑模拟。

> 已独立核对：`ARMS` 表与本次实跑一致（起始 84 级、600 tick 末 10~11 级；物质平衡布点起始 1436 级、末 147 级），
> 所以**那张表可信**；但**它旁边的结论句不可作为计算结果引用**。

### 2. 同一脚本的"覆盖率"方向反了

`:141` 计算 `覆盖率 = W ÷ 税后可购价值`（= W ÷ (最终需求/1.1)），得到 **1.1178**，随后打印「缺口 −11.7830%」。
`docs/ACTIVE.md` §二-4 记的 0.909157 是**反方向**的比值（税后可购 ÷ 名义需求）。
C++ 复核：`(W/1.1) ÷ 最终需求 = 0.909133`（与 0.909157 相差 0.0027%，**该断言成立**）。方向不能混用。

### 3. 两个探针的 L\* 口径互不相同

| 脚本 | 建造部门等级 | 自给农场 | 解出的总等级 |
|------|--------------|----------|--------------|
| `contract_consistency_probe.js` | 20 级（= 实现默认值） | 不建模 | 反解 **733.05** vs 定案 688.59 |
| `survival_condition_probe.js` | **固定 5 级**（注释未说明） | 反解 `k_sub`（本次解出 **0.00**） | **699.72** vs 定案 688.61 |

两者的 `W`（工资总额）因此不同（23,239,912 vs 23,615,533），进而所有税基/缺口数字都不同。
**引用任何一个数字都必须写明是哪套口径。**

### 4. 由此，`ACTIVE.md` / `README.md` 的四个定量断言不能复现

见 `claims-verified-js.txt` 与 `contract-check-cpp.txt` 的逐条判定（JS 与 C++ 两套独立实现在全部断言上一致）：

| 断言 | 文档值 | 复核值（§8.6 口径） | 判定 |
|------|--------|---------------------|------|
| §3.3 与 §8.6 不相容，上游需 2.0~3.1 倍 | 3.1 | 3.1316（上游四项 +102.9%~+213.2%） | **CONFIRMED** |
| 反解所需总等级 ≠ 定案 688.59 | 688.61 | 733.05（下游六项 ≤0.017%） | **CONFIRMED** |
| 平衡态税后覆盖率 0.909157 | 0.909157 | 0.909133 | **CONFIRMED** |
| 税收只覆盖约 9.5%（税收/W） | 0.095 | **0.1457** | **DIFFERS** |
| 政府缺口 0.61·W | 0.61 | 0.5543 | CLOSE（9.1%） |
| t=10% ⇒ 政府持股 s ≤ 0.04 | 0.04 | **0.1457** | **DIFFERS** |
| 债务上限只够 3~6 个周期 | 3.0 | **0.34** | **DIFFERS**（实际比记录的更糟） |

**第三处独立复算（仓库自带）**：`tools/viz/gen_dashboard.js`（原会话自己写的看板生成器）
从契约 §8.6 的 L\* 独立复算并打印 `政府缺口 0.5631 W；税收覆盖工资义务 19.55%；
债务上限 435万（0.31 个周期）；自洽所需税率 t* = 46.5%；或 t=10% 时 s 上限 = 0.1506`
（重跑日志 `../viz/RERUN-VIZ.txt`）。它与本次 C++/JS 复核一致，**同样不支持** 9.5% / s≤0.04 /
3~6 周期 —— 即修正后的数字在仓库里早已有一个独立实现给出，只是没被写回 `ACTIVE.md`。

⇒ 定性结论（**政府 70% 工资义务远非 10% 交易税所能覆盖；供给侧上游产能被系统性低估**）成立且稳健；
**定量数字须按上表更新**，不能继续引用 9.5% / s≤0.04 / 3~6 周期。

### 5. 货币量级不是逐位可复现的

`determinism.txt`：同一命令重复执行，离散量（级数、人口、配给比）完全一致，
货币量级有约 0.01%~0.04% 抖动。**引用货币绝对值必须带量级口径**，单次实跑的精确值不能作为验收证据。

### 6. `diag_govcash` 的 tick 1 残差不是记账漏洞

`diag_govcash.txt` 与 `audit-govdecomp.txt` 对照可确认：CLI 的分解式**漏了 `GovSelfTax`（政府收自己的税）**这一项，
而 `internal/sim/audit_govdecomp_test.go:26` 的分解式含该项（残差 0.00）。
`ACTIVE.md` 把成因写成"主函数求和用未取负的字段、打印时取负"——本次复核**不支持该描述**，
真实成因是**漏项**。这是工具口径缺陷，不是记账漏洞。

## 四、纪律（沿用 `docs/ACTIVE.md` §五）

1. 本目录文件**不得**作为"原始证据"引用；一律标注重跑与工具链版本。
2. 新增/修改数据表一律走 `internal/sim/scenario_test.go` 的 `Scenario` 注入并打印「字段 前值 → 后值 + 理由」。
3. `write` / `edit` 一律用绝对路径（相对路径会被解析到工作区根，历史上覆盖过仓库根 README）。
4. 记账口径只有一处（`internal/ledger` + `internal/book`），诊断代码不得重算分摊。
