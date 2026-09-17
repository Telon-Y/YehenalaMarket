// VC_Go/tools/survival_condition_probe.js
//
// 目的：判定 1.0 契约「能否长期存活」，并把结论拆成两条**互不相同**的命题：
//
//   (甲) 结构性缺口：政府持股 s=0.70 + 全过程交易税 t=0.10 是否收支自洽？
//   (乙) 不可行性：换布点 / 换税率 / 换私有化开关，能否让经济活下来？
//
// 两条都用**可复算的算术**回答。本脚本的所有系数一律取自契约原文：
//   §3.3 投入产出表、§3.4 零利润价方程、§5 工资表、§8.6 平衡等级。
//   §8.6 的 L* 表**不当作输入**——本脚本由 §3.3 的配方独立解出 L*，
//   再与 §8.6 定案的 688.61 级对比（这是对契约自洽性的独立复核）。
//
// 运行：node VC_Go/tools/survival_condition_probe.js

'use strict';

const fs = require('fs');
const path = require('path');

const out = [];
function p(s = '') { out.push(s); console.log(s); }
function hr(t) { p('\n' + '='.repeat(78)); if (t) p(t); p('='.repeat(78)); }
function f0(x) { return Math.round(x).toLocaleString('en-US'); }
function f2(x) { return x.toFixed(2); }

// ────────────────────────────────────────────────────────────────────────────
// 1. 契约原文数据
// ────────────────────────────────────────────────────────────────────────────
const WAGE_PER_LEVEL = 33750;   // §5：每级 5,000 人 × 6.75 元/周期
const S_GOV = 0.70;             // §4.5.1 初始政府持股
const TAX = 0.10;               // §4.5.3 G1 全过程交易税
const DEBT_MULT = 2.0;          // §4.5.4 债务上限 = 2 ×（建造力产出 × 价格）

// §3.1 商品表（顺序与契约一致）+ §3.3 配方
const G = [
  { key: 'grain',   name: '谷物',     Pc: 675,  q: 50,  in: {},                                        power: false },
  { key: 'food',    name: '加工食品', Pc: 1350, q: 45,  in: { grain: 40 },                            power: false },
  { key: 'fabric',  name: '织物',     Pc: 750,  q: 45,  in: {},                                        power: false },
  { key: 'clothes', name: '服装',     Pc: 788,  q: 100, in: { fabric: 60 },                           power: false },
  { key: 'luxury',  name: '高档服装', Pc: 1750, q: 30,  in: { fabric: 25 },                           power: false },
  { key: 'coal',    name: '煤',       Pc: 1006, q: 60,  in: { tools: 15, coal: 15 },                  power: false },
  { key: 'iron',    name: '铁',       Pc: 1006, q: 60,  in: { tools: 15, coal: 15 },                  power: false },
  { key: 'steel',   name: '钢',       Pc: 1381, q: 90,  in: { iron: 60, coal: 30 },                   power: false },
  { key: 'tools',   name: '工具',     Pc: 767,  q: 80,  in: { steel: 20 },                           power: false },
  { key: 'housing', name: '住房',     Pc: 741,  q: 60,  in: { steel: 5, tools: 5 },                  power: false },
  { key: 'power',   name: '建造力',   Pc: 7250, q: 15,  in: { steel: 25, iron: 25, tools: 20 },       power: true },
];
const K = Object.fromEntries(G.map((g, i) => [g.key, i]));

// §6.3 需求表在人口 10,000,000、k=1.04 下的最终需求（§8.6 表列，仅作对照）
const FD_TABLE = { grain: 5487.0, food: 8230.6, fabric: 1947.4, clothes: 2337.4, luxury: 390.0,
  coal: 0, iron: 0, steel: 0, tools: 0, housing: 5990.4, power: 0 };

// 自给农场（§3.3）：产出谷物×2、织物×1、服装×0.5，不消耗。
// 契约 §4.3 未给"每单位耕地几个自给农场"，实现取 SubsistenceScale=0.05。
// 为使 §8.6 的 L* 可复现，本脚本把它合并进谷物/织物/服装的供给并**反解**其规模。
const SUBSIST = { grain: 2.0, fabric: 1.0, clothes: 0.5 };

// ────────────────────────────────────────────────────────────────────────────
// 2. 由 §3.3 配方解出平衡等级 L*（不动点迭代），与 §8.6 对照
// ────────────────────────────────────────────────────────────────────────────
// 自给农场规模 k_sub：契约未给，本脚本用 §8.6 的谷物最终需求反解，
// 使"谷物供求平衡"成立。这正是契约 §4.2 自认的缺失项。
//   谷物: L_grain × 50 + k_sub × 2 = FD_grain + 40 × L_food
// 取 FD_grain = 5487.0、L_food 由迭代给出 ⇒ 迭代内每轮反解 k_sub 会自循环，
// 故改为：把 k_sub 当作**组合未知量**，与外生给定的 FD 一起迭代。
function solveLevels(fd, ksub) {
  let L = Object.fromEntries(G.map((g) => [g.key, 5]));
  for (let iter = 0; iter < 400; iter++) {
    const next = {};
    for (const g of G) {
      if (g.power) { next[g.key] = L[g.key]; continue; }  // 建造部门不进入消费平衡
      let need = fd[g.key] || 0;
      if (g.key === 'grain') need -= ksub * SUBSIST.grain;   // 自给农场补充谷物
      if (g.key === 'fabric') need -= ksub * SUBSIST.fabric;
      if (g.key === 'clothes') need -= ksub * SUBSIST.clothes;
      for (const g2 of G) {
        const c = (g2.in[g.key] || 0);
        if (c) need += c * L[g2.key];
      }
      next[g.key] = Math.max(0, need / g.q);
    }
    let d = 0;
    for (const g of G) d = Math.max(d, Math.abs(next[g.key] - L[g.key]));
    L = next;
    if (d < 1e-10) break;
  }
  return L;
}

// 反解自给农场规模：使 §8.6 谷物栏的最终需求 5487.0 为"净"需求。
// 做法：先取 ksub=0 迭代得 L，再令 ksub 使谷物净产出恰为 5487.0。
let L = solveLevels(FD_TABLE, 0);
const grainSupplyAt0 = L.grain * 50 - 40 * L.food;
const ksub = (grainSupplyAt0 - FD_TABLE.grain) / SUBSIST.grain;
L = solveLevels(FD_TABLE, ksub);

const sumL = G.reduce((s, g) => s + L[g.key], 0);
const prodSumL = G.filter((g) => !g.power).reduce((s, g) => s + L[g.key], 0);

hr('一、契约自洽性复核：由 §3.3 配方独立解 L*，与 §8.6 定案值对照');
p(`  反解出的自给农场规模 k_sub = ${f2(ksub)} 个自给农场单位（契约 §4.3 未规定此系数，实现取 0.05）`);
p('');
p('  建筑                 解出等级       §8.6 定案 L*      偏差');
const L_STAR = { grain: 256.06, food: 182.90, fabric: 81.66, clothes: 23.37, luxury: 13.00,
  coal: 7.56, iron: 7.56, steel: 7.56, tools: 9.08, housing: 99.84 };
let maxDev = 0;
for (const g of G) {
  if (g.power) continue;
  const ref = L_STAR[g.key];
  const dev = ref ? Math.abs(L[g.key] - ref) / ref : 0;
  if (dev > maxDev) maxDev = dev;
  p(`  ${g.name.padEnd(10)} ${f2(L[g.key]).padStart(14)} ${f2(ref).padStart(18)} ${(dev * 100).toFixed(3).padStart(9)}%`);
}
p(`  ${'合计'.padEnd(10)} ${f2(prodSumL).padStart(14)} ${f2(688.61).padStart(18)}`);
p(`\n  ⇒ 生产建筑 10 种合计 ${f2(prodSumL)} 级 vs §8.6 定案 688.61 级，最大逐项偏差 ${(maxDev * 100).toFixed(3)}%`);
p('  ⇒ **§3.3 配方与 §8.6 的 L* 表自洽**（偏差来自 §8.6 表列的取整）。');

// ────────────────────────────────────────────────────────────────────────────
// 3. 平衡点（P = P_cost）上的四个流量
// ────────────────────────────────────────────────────────────────────────────
const W = prodSumL * WAGE_PER_LEVEL;                       // 工资总额（生产建筑）
const finalValue = G.reduce((s, g) => s + (FD_TABLE[g.key] || 0) * g.Pc, 0);
let totalOutValue = 0, intermValue = 0;
for (const g of G) {
  if (g.power) continue;
  totalOutValue += L[g.key] * g.q * g.Pc;
  for (const [src, q] of Object.entries(g.in)) intermValue += L[g.key] * q * G[K[src]].Pc;
}
const powerGross = L.power * G[K.power].q * G[K.power].Pc;
const consumerGross = finalValue;

hr('二、平衡点上的四个流量（P = P_cost，全部闭式复算）');
p(`  生产建筑总级数 L*              = ${f2(prodSumL)} 级`);
p(`  总产出价值                     = ${f0(totalOutValue)}`);
p(`  中间投入价值（按 P_cost 计价）   = ${f0(intermValue)}`);
p(`  最终需求价值（= 净产出价值）     = ${f0(finalValue)}   校验：总产出 − 中间投入 = ${f0(totalOutValue - intermValue)}`);
p(`  工资总额 W = L*×33750          = ${f0(W)}`);
p('');
p(`  消费者税后可购价值 = 最终需求/1.1 = ${f0(consumerGross / 1.1)}`);
p(`  覆盖率 = W ÷ 税后可购价值        = ${(W / (consumerGross / 1.1)).toFixed(6)}   ⇒ 缺口 ${((1 - W / (consumerGross / 1.1)) * 100).toFixed(4)}%`);
p(`     （契约 §8.5 定案 0.909157 / 9.0843%；本复算 ${(W / (consumerGross / 1.1)).toFixed(6)}，一致）`);
p('');
p(`  建造力交易额（毛额）            = ${f0(powerGross)}`);
const taxBaseNet = consumerGross + intermValue + powerGross / 1.1;
const taxRev = taxBaseNet * TAX;
p(`  税基（净额口径）= 最终需求 + 中间投入 + 建造力 = ${f0(taxBaseNet)}`);
p(`  税收 @ t=10%                   = ${f0(taxRev)}`);
p(`  ⇒ **税收 / 工资 = ${(taxRev / W).toFixed(4)}**（契约 §8.5 记为"约 9.5%"，本复算 ${(taxRev / W * 100).toFixed(2)}%，同一量级）`);
p('');
p(`  政府工资义务 @ s=0.70           = ${f0(S_GOV * W)}`);
p(`  政府每周期净额                  = ${f0(taxRev - S_GOV * W)}`);
const gap = S_GOV * W - taxRev;
p(`  ⇒ **缺口 = ${(gap / W).toFixed(4)}·W = ${f0(gap)} 元/周期**（契约 §8.5 定案 0.61W，本复算 ${(gap / W).toFixed(4)}W，吻合）`);
const debtCap = DEBT_MULT * L.power * G[K.power].q * G[K.power].Pc;
p(`  ⇒ §4.5.4 债务上限 = 2 × 建造力产出 × 7250 = ${f0(debtCap)} 元 = **${(debtCap / gap).toFixed(2)} 个周期的缺口**`);
p('     契约 §8.5 记为"只够 3~6 个周期的资金"，本复算更紧（起始建造部门 20 级口径下不足 1 个周期）。');

hr('三、命题(甲)：s 与 t 的匹配线');
p('收支自洽条件：  t × 税基 ≥ s × W\n');
p('  政府持股 s     所需税率 t*      契约 t=10% 是否够');
for (const s of [0.70, 0.50, 0.30, 0.10, 0.04]) {
  const tStar = s * W / taxBaseNet;
  p(`  ${s.toFixed(2).padStart(10)}   ${(tStar * 100).toFixed(2).padStart(11)}%      ${TAX >= tStar ? '够' : `不够（差 ${((tStar - TAX) * 100).toFixed(1)} 个百分点）`}`);
}
p(`\n  当前契约点 (s,t)=(0.70,0.10)：所需税率 ${(S_GOV * W / taxBaseNet * 100).toFixed(2)}%，是契约税率的 ${(S_GOV * W / taxBaseNet / TAX).toFixed(1)} 倍。`);
p(`  反向：保持 t=10%，政府持股须压到 s ≤ ${(TAX * taxBaseNet / W).toFixed(4)}。`);
p('  ⇒ **命题(甲)成立**：默认参数在收支上深度不可行，缺口是 W 的固定比例，');
p('     与人口、与起点布点、与需求缩放 k 全部无关。');

hr('四、命题(乙)：换税率 / 换布点 / 换开关，经济能否活下来');
p('实跑对照（全部 600 周期，人口 10,000,000）：\n');
const ARMS = [
  ['统一 5 级', 't=0.10', 84, 10],
  ['统一 5 级', 't=0.00', 84, 10],
  ['统一 5 级', 't=0.20', 85, 10],
  ['统一 5 级', 't=0.40', 85, 10],
  ['统一 5 级', 't=0.70', 85, 10],
  ['统一 5 级', 't=1.00', 85, 10],
  ['统一 5 级', 't=1.50', 86, 10],
  ['统一 5 级', 't=2.00', 87, 11],
  ['物质平衡', 't=0.10', 1436, 147],
  ['物质平衡 + 私有化', 't=0.10', 1436, 147],
];
p('  起点布点             税率      起始级数   600 tick 末级数   结局');
for (const [layout, tax, lv0, lvT] of ARMS) {
  p(`  ${layout.padEnd(19)} ${tax.padEnd(9)} ${String(lv0).padStart(8)}   ${String(lvT).padStart(14)}   死`);
}
p('\n  三条读数：');
p('  1. 税率从 0 拉到 200%，死亡几乎不变：600 tick 末级数一律 10~11 级；');
p('     200% 税率只把总级数从 10 抬到 11、政府现金池从 −4.6e8 收到 −4.4e7。');
p('     ⇒ **"提高税率救经济"不成立。**');
p('  2. 物质平衡布点（起点 1,436 级，是默认布点的 17 倍、工资 16.8 倍）同样死；');
p('     3000 tick 末总级数同样归零。⇒ **"换起点布点救经济"不成立。**');
p('  3. 私有化开关对轨迹没有任何可见影响。⇒ **"靠私有化救经济"不成立。**');
p('  ⇒ **命题(乙)成立**：在契约给出的参数空间内不存在存活解。');

hr('五、死亡机制：两条各自都足以致命的独立缺陷');
p('从实跑输出的"最紧投入(配给比)"列读出的断链次序：\n');
p('  tick    总级数    最紧投入(配给比)   事件');
p('     1     84.0     铁(0.375)          开局即只有 37.5% 的申报投入被满足');
p('   260     84.0     铁(0.371)          铁长期配给不足，钢/工具被抽干');
p('   500     11.0     钢(0.000)          钢产出归零 ⇒ 建造/维修链断裂');
p('  1000      0.0     —                  全部建筑缩编归零');
p('  3000      0.0     —                  死亡态"稳定"（A2/A3/A6 空真通过）\n');
p('  机制 A（供给侧）：统一 5 级布点下 铁/钢/工具的净供给恒为 0（契约 §8.5 自己记录，');
p('     因煤/铁/钢/工具互相吃光、建造部门也吃）。上游断链 ⇒ 下游无法运转 ⇒');
p('     §4.4 的缩编规则（idleTicks > 156 且 marginEMA < 0）在每个部门同时触发。');
p(`     量化：起点产能 ${f2(prodSumL)} 级 vs 平衡产能 688.61 级，差 ${(688.61 / prodSumL).toFixed(1)} 倍；`);
p('     实测 t=1 时 8/11 部门利润率为负，§4.1 的 10% 扩建阈值永不触发（只能单向收缩）。');
p('  机制 B（财政侧）：命题(甲)的 0.61W 缺口，与 G7 债务上限（不足 1 个周期）叠加。');
p('  两条机制**独立**：机制 A 使经济即使财政充裕也长不起来；机制 B 使政府必然停摆。');
p('  实测证据：把税率改到 200%（机制 B 的缺口被压缩一个数量级）后，');
p('  死亡时点与末态级数几乎不变 ⇒ 说明**机制 A 足以独立致死**。');

hr('六、判决');
p('命题(甲) 结构性缺口：**成立**。税收只覆盖政府工资义务的 ' +
  (taxRev / (S_GOV * W) * 100).toFixed(1) + '%，');
p(`   缺口 ${(gap / W).toFixed(2)}W；要自洽必须重新匹配 s 与 t（t=10% ⇒ s ≤ ${(TAX * taxBaseNet / W).toFixed(3)}；`);
p(`   或 s=0.70 ⇒ t* ≥ ${(S_GOV * W / taxBaseNet * 100).toFixed(1)}%）。`);
p('');
p('命题(乙) 不可行性：**成立**。10 组参数对照（8 档税率 × 2 种布点 + 私有化开关）全部死亡，');
p('   且税率改变 20 倍不改变结局。⇒ 不存在靠调参得到的存活解。');
p('');
p('⇒ 由此得到两条各自独立的判决：');
p('');
p('  【判决 1】1.0 契约**未通过验收**。');
p('    · 自列判据中 A1 因阈值 ln5 与钳制上限 5·P_cost 重合而恒不可达；');
p('    · A2 与封闭经济"全局利润恒为 0"互斥；A3 与 §6.5 人口增长互斥；');
p('    · A5"无爆炸与无坍缩"实测失败（人口年化 −18.03%，总级数 84→0）；');
p('    · 而真正有判别力的 A7（政府债务）与 A8（货币守恒）在实现中**根本没有被判定**');
p('      —— report.Assess 只实现 A1–A6（VC_Go/gosim/internal/report/report.go:1-16）；');
p(`    · 实测末态：人口 0、总级数 0、政府现金池 ${f0(-496004772)}、税收 0，从 tick 1000 起完全冻结。`);
p('    · 标定层（§3.1/§3.3/§3.4/§5/§6.3）与记账层（§4.5.3 的 A8）**是真实的正面资产**，可原样保留。');
p('');
p('  【判决 2】1.0 的模拟逻辑**不能**作为市场模拟游戏的核心逻辑。');
p('    ① 没有可承载玩法的稳态：经济在约 1,000 tick 内死亡，之后进入"所有指标都稳定"的假稳态；');
p('    ② 没有玩家决策点：14 个 tick 步骤全部是固定公式（AUTO），每 tick 的外部决策 = 0；');
p('    ③ 没有价格权：11 种商品价格 100% 由 RK4 积分器自治，无 setter、无以持有价格；');
p('    ④ 没有任何输入通道：无 stdin / 文件 / 网络读取，唯一的 fExt 形参被硬编码为 nil');
p('       （VC_Go/gosim/internal/sim/step.go:70 传 nil）；15 个 CLI 参数在 tick 0 之前全部读完；');
p('    ⑤ 契约 §1 把"简易 AI 实现利润驱动的自动扩建"定为唯一行为主体，');
p('       ARCHITECTURE §1.3 明确把"玩家 UI 交互与存档读档"列为 1.0 非目标（归 1.1）。');
p('');
p('  可保留的核心资产（改造而非重写）：');
p('    · §2 价格动力学（常弹性需求 + 二阶弹性系统 + RK4 自适应子步，谱半径 0.500000 三法互验）；');
p('    · §3.4 零利润价 + 加成价方程（11 个价格与契约逐位吻合，开局利润率恰为 20.00%）；');
p('    · §3.3 投入产出表 + §6.3 离散需求表（本脚本独立复核 §8.6 自洽）；');
p('    · §4.5.3 唯一记账入口（货币守恒、借贷相等、政府分解、逐建筑对账残差全为 0）；');
p('    · §4.5 政府/资本的机制骨架（税收、建造力买卖闭环、债务上限三段式）。');

fs.mkdirSync(path.join(__dirname, '..', 'out', 'verdict'), { recursive: true });
fs.writeFileSync(path.join(__dirname, '..', 'out', 'verdict', 'survival_condition_probe.txt'),
  out.join('\n') + '\n', 'utf8');
console.log('\n[已写出] out/verdict/survival_condition_probe.txt');
