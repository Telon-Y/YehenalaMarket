# 2026-09-18「平衡」轮归档 —— 索引（重建版）

> ## ⚠️ 重建声明
>
> 原 `out/archive/2026-09-18-equilibrium/` 目录**从未入库**（`out/` 在 `.gitignore` 里），
> 连同它的 README 索引一起**不可恢复**。本文件是 2026-09-19 依据
> `docs/ACTIVE.md`、`docs/CHANGES-1.0.md`、`docs/AUDIT-1.0.md`、`docs/ARCHITECTURE.md` 的转述
> **重建的索引**，并把该轮**可以复算**的证据全部重跑了一遍。
>
> - 重跑清单与逐项退出码：`RERUN-PROBES.txt`
> - 契约 HTML 结构校验：`RERUN-HTML-AUDIT.txt`
> - 重跑驱动脚本：`rerun-probes.ps1`（`-IncludeSlow` 可加跑两个模拟型探针）
> - 判决书与其证据：`../verdict/`（入口 `../verdict/VERDICT-1.0-acceptance.md`）

---

## 一、这一轮做了什么（据 `docs/` 转述，非本次推断）

| 产出 | 位置 |
|------|------|
| §8.6 平衡价格与平衡等级（人口 10m） | `docs/1.0 生产与市场模拟.md` §8.6；定案 688.61 级 |
| 三表联合标定 `k = 1.0400` | `docs/CHANGES-1.0.md`；实跑复核见 `sim-10000pop10m.txt` 首段 |
| 记账分层（`ledger` 机制 + `book` 语义）与 G1–G7 | `docs/LEDGER-1.0.md`、`gosim/internal/fiscal` |
| 架构决策 D1–D9、风险 R1–R3 | `docs/ARCHITECTURE.md` |
| 三轮改动表 | `docs/CHANGES-1.0.md` |
| 验收判决与全部证据 | **原归档已丢失**；重建版见 `../verdict/` |

## 二、重跑结果（19 个算术探针，全部 exit=0）

`RERUN-PROBES.txt` 记录每条的退出码、耗时与产物是否落盘。

| 探针 | 产物（`out/` 下） | 证明什么 |
|------|-------------------|----------|
| `equilibrium_handcheck.js` | （仅 stdout） | 平衡点的独立复算 |
| `calibration_probe.js` | `calibration_probe.txt` | 三表联合标定与 k |
| `margin_base_probe.js` | `margin_base_probe.txt` | 开局利润率的两套成本基 |
| `structure_probe.js` | `structure_probe.txt` | 产业结构可行性（Leontief 逆 / 缺口 LP） |
| `construction_sink_probe.js` | `construction_sink_probe.txt` | §4.3"建造力无人付款"的 5.1e4 倍缺口 |
| `accounting_probe.js` | `accounting_probe.txt` | 货币闭环静态核算 |
| `gov_capital_accounting.js` | `gov_capital_accounting.txt` | 含政府与资本的闭环校验 |
| `debt_cap_probe.js` | `debt_cap_probe.txt` | G7 债务上限核算 |
| `tick_probe.js` | （仅 stdout） | 单商品 tick 级可行性 |
| `price_dynamics_probe.js` | `price_dynamics_probe.txt` | §2 价格 ODE 隔离测试 |
| `steady_profit_probe.js` | `steady_profit_probe.txt` | 稳态利润率分布（A2 可达性） |
| `expansion_probe.js` | `expansion_probe.txt` | 扩建规则收敛性 |
| `leontief_probe.js` | （仅 stdout） | Leontief 逆的量级检查 |
| `spring_price_probe.js` / `spring_price_scale.js` | （仅 stdout） | 价格 ODE 的弹簧类比与量纲诊断 |
| `food_s_curve_probe.js` | `food_s_curve_probe.txt` | §6.3 基础食物的 S 形曲线候选 |
| `housing_curve_probe.js` | `housing_curve_probe.txt` | 住宅需求的指数型候选 |
| `demand_table_final.js` | `demand_table_final.txt` | §6.3 需求表的离散补全 |
| `welfare_table_probe.js` | `welfare_table_probe.txt` | 财富档表与福利金可行性 |

**未重跑**（模拟型，耗时长；用 `-IncludeSlow` 可跑）：`multigood_probe.js`、`core_sim_probe.js`。
**未重跑**（需要本机没有的 node/npm 之外还依赖 DSH 之外的工具）：无。

## 三、文档渲染链路校验

`RERUN-HTML-AUDIT.txt`：对 `docs/1.0 生产与市场模拟.html` 跑 `tools/html_audit.js`，
**全部检查通过**（标签配对、数学容器、表格等 6 类检查）。
⇒ 契约正文与渲染产物在本机一致（这是 `docs/ACTIVE.md` §五·5 要求的准入检查）。

## 四、可视化（`out/viz/`）

| 产物 | 说明 |
|------|------|
| `run-default-3000.txt` | `market-sim -ticks 3000` 的原始 dump（UTF-8） |
| `run-default-3000.json` | `tools/viz/parse_run.js` 解析结果（6 点序列 + 判据 4/6） |
| `dashboard.html` | `tools/viz/gen_dashboard.js` 生成的单文件看板 |
| `RERUN-VIZ.txt` | 本次重跑的完整日志 |

⚠️ **重要**：`gen_dashboard.js` 会**自己**从契约 §8.6 的 L\* 独立复算财政量，它打印的是

```
L* 反解合计 733.05 vs 契约定案 688.61
政府缺口 0.5631 W；税收覆盖工资义务 19.55%；债务上限 435万（0.31 个周期）
自洽所需税率 t* = 46.5%；或 t=10% 时 s 上限 = 0.1506
```

也就是说：**仓库里本来就有一个独立实现给出的"修正后数字"**，与 `docs/ACTIVE.md` 记的
「税收约 9.5% / s ≤ 0.04 / 债务上限 3~6 个周期」不符，而与本次 C++/JS 复核一致。
引用旧数字前请先读 `../verdict/EVIDENCE-CAVEATS.md` §三。

## 五、永久不可恢复项（**不要**试图引用它们的原始内容）

| 项 | 引用它的地方 | 影响 |
|----|--------------|------|
| `out/gosim_contract.txt` | `tools/welfare_table_probe.js:135` 硬编码「tick 3000 税收 166,894,483」 | 该探针的税收基线不可复核 |
| `out/gosim_fixed.txt` | `tools/debt_cap_probe.js:56` 注释 | 债务上限探针的实跑对照不可复核 |
| `out/sim_notax.log` | `docs/AUDIT-1.0.md` §九 | "零税率 + 20 万人口同样冻结"缺证据 |
| v3 版 `core_sim_gov.js` 的产物 | `README.md`「反例（已实测崩解）」 | 现脚本头部自述已是 **v4 闭环版**，重跑得到的是 v4 结果，**不能**用它证明 v3 的沉淀式崩解 |
| 原始 `VERDICT-1.0-acceptance.md` 与原始归档 | `docs/ACTIVE.md` §判决 | 见 `../verdict/` 重建版与其声明 |

## 六、复现命令

```powershell
# 本轮全部算术探针（含产物落盘）
powershell -ExecutionPolicy Bypass -File D:\Code\YehenalaMarket\out\archive\2026-09-18-equilibrium\rerun-probes.ps1

# 加跑两个模拟型探针
powershell -ExecutionPolicy Bypass -File ...\rerun-probes.ps1 -IncludeSlow

# 可视化
D:\Code\YehenalaMarket\out\bin\market-sim.exe -ticks 3000 > out\viz\run-default-3000.txt
$env:ELECTRON_RUN_AS_NODE="1"
& "D:\DSH\DSH Desktop\DSH Desktop.exe" tools\viz\parse_run.js out\viz\run-default-3000.txt default-3000
& "D:\DSH\DSH Desktop\DSH Desktop.exe" tools\viz\gen_dashboard.js
```

> 本机没有 node/npm：所有 `node xxx.js` 一律用 DSH Desktop 的 Electron 以
> `ELECTRON_RUN_AS_NODE=1` 运行（node v24.18.1）。Go 在 `D:\Code\Go`（1.26.5，已在 PATH 上）。
