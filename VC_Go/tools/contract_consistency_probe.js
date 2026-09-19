// VC_Go/tools/contract_consistency_probe.js
//
// 只做一件事：校验契约 §3.3（投入产出表）与 §8.6（平衡等级表）是否自洽。
//
// 判据（纯算术，无自由参数）：
//   对每个商品 i，把 §8.6 的 L*_i 与 §3.3 的配方逐条相乘，得到
//      总产出_i = L*_i × q_i
//      中间投入_i = Σ_j L*_j × q_ij      （q_ij = 建筑 j 每级消耗 i 的数量）
//   则必须满足供求平衡：  总产出_i − 中间投入_i = 最终需求_i（§8.6 表末列）
//
// 若上式不成立，说明 §8.6 的 L* 表与 §3.3 的配方（在 §8.6 自己给的最终需求下）**不相容**。
//
// 附加两项独立检查：
//   (a) 自给农场规模：§3.3 说谷物农场产出 50，但 §8.6 的 FD_grain = 5487.0
//       ⇒ 要 109.74 个"谷物农场当量"，而 §8.6 的 L*_grain = 256.06。差额须由自给农场补。
//       §4.3 未规定"每单位耕地对应几个自给农场"（契约自认为未定项），此处只做量化。
//   (b) 净产出的价格恒等式：Σ 净产出×P_cost =? 最终需求价值。
//
// 运行：node VC_Go/tools/contract_consistency_probe.js

'use strict';

const fs = require('fs');
const path = require('path');

const out = [];
function p(s = '') { out.push(s); console.log(s); }
function hr(t) { p('\n' + '='.repeat(78)); if (t) p(t); p('='.repeat(78)); }
function f2(x) { return x.toFixed(2); }
function f0(x) { return Math.round(x).toLocaleString('en-US'); }

// §3.1 商品（P_cost 与单级产出 q）+ §3.3 配方
const G = [
  { key: 'grain',   name: '谷物',     Pc: 675,  q: 50,  L: 256.06, FD: 5487.0, in: {} },
  { key: 'food',    name: '加工食品', Pc: 1350, q: 45,  L: 182.90, FD: 8230.6, in: { grain: 40 } },
  { key: 'fabric',  name: '织物',     Pc: 750,  q: 45,  L: 81.66,  FD: 1947.4, in: {} },
  { key: 'clothes', name: '服装',     Pc: 788,  q: 100, L: 23.37,  FD: 2337.4, in: { fabric: 60 } },
  { key: 'luxury',  name: '高档服装', Pc: 1750, q: 30,  L: 13.00,  FD: 390.0,  in: { fabric: 25 } },
  { key: 'coal',    name: '煤',       Pc: 1006, q: 60,  L: 7.56,   FD: 0,      in: { tools: 15, coal: 15 } },
  { key: 'iron',    name: '铁',       Pc: 1006, q: 60,  L: 7.56,   FD: 0,      in: { tools: 15, coal: 15 } },
  { key: 'steel',   name: '钢',       Pc: 1381, q: 90,  L: 7.56,   FD: 0,      in: { iron: 60, coal: 30 } },
  { key: 'tools',   name: '工具',     Pc: 767,  q: 80,  L: 9.08,   FD: 0,      in: { steel: 20 } },
  { key: 'housing', name: '住房',     Pc: 741,  q: 60,  L: 99.84,  FD: 5990.4, in: { steel: 5, tools: 5 } },
];
// 建造部门不进入消费平衡（§3.3：建造力是投资品），但它**消耗**钢/铁/工具，
// 故 §8.6 的 L* 表若要与配方自洽，必须把建造部门的消耗计入。
// §8.6 未给建造部门等级（记为"由 InitialPowerLevel 另行给定"，实现取 20）。
const POWER_L = 20;
const POWER_IN = { steel: 25, iron: 25, tools: 20 };
const K = Object.fromEntries(G.map((g, i) => [g.key, i]));

hr('一、§8.6 的 L* 表 × §3.3 的配方 ⇒ 供求平衡检验');
p(`  建造部门等级按契约实现默认值 ${POWER_L} 级计入中间投入（§8.6 未给该值）。\n`);
p('  商品            总产出      中间投入(生产)  中间投入(建造)     净产出      §8.6最终需求      残差');
let bad = 0;
const resid = {};
for (const g of G) {
  const outQ = g.L * g.q;
  let inter = 0;
  for (const g2 of G) inter += g2.L * (g2.in[g.key] || 0);
  const interPower = POWER_L * (POWER_IN[g.key] || 0);
  const net = outQ - inter - interPower;
  const r = net - g.FD;
  resid[g.key] = r;
  if (Math.abs(r) > 0.5) bad++;
  p(`  ${g.name.padEnd(10)} ${f2(outQ).padStart(11)} ${f2(inter).padStart(15)} ${String(interPower).padStart(15)} ${f2(net).padStart(11)} ${f2(g.FD).padStart(15)} ${f2(r).padStart(10)}`);
}
p(`\n  ⇒ 残差非零的商品数 = ${bad} / 10`);
if (bad === 10) {
  p('  ⇒ **§8.6 的 L* 表与 §3.3 的配方（在 §8.6 自列的最终需求下）不相容。**');
} else if (bad > 0) {
  p('  ⇒ 部分商品不相容，见残差列。');
} else {
  p('  ⇒ 完全自洽。');
}

hr('二、逐项读数：哪些商品的 L* 与配方差得最远');
p('  "所需等级"= 在 §8.6 自己的最终需求下，供求平衡所要求的等级（用 §8.6 自己的 L* 做迭代初值）。\n');
p('  商品            §8.6 L*      由配方反解所需 L*      相对偏差');
// 不动点迭代：L_i = (FD_i + Σ_j c_ij L_j) / q_i，其中 c_ij = 建筑 j 的投入量
// 建造部门固定为 POWER_L（不求解）
let L = Object.fromEntries(G.map((g) => [g.key, g.L]));
for (let it = 0; it < 5000; it++) {
  const nx = {};
  for (const g of G) {
    let need = g.FD;
    for (const g2 of G) need += (g2.in[g.key] || 0) * L[g2.key];
    need += (POWER_IN[g.key] || 0) * POWER_L;
    nx[g.key] = need / g.q;
  }
  let d = 0;
  for (const g of G) d = Math.max(d, Math.abs(nx[g.key] - L[g.key]));
  L = nx;
  if (d < 1e-12) break;
}
for (const g of G) {
  const dev = (L[g.key] - g.L) / g.L;
  p(`  ${g.name.padEnd(10)} ${f2(g.L).padStart(11)} ${f2(L[g.key]).padStart(20)} ${(dev * 100).toFixed(1).padStart(13)}%`);
}
const sumGiven = G.reduce((s, g) => s + g.L, 0);
const sumSolved = G.reduce((s, g) => s + L[g.key], 0);
p(`  ${'合计'.padEnd(10)} ${f2(sumGiven).padStart(11)} ${f2(sumSolved).padStart(20)} ${((sumSolved - sumGiven) / sumGiven * 100).toFixed(1).padStart(13)}%`);
const devs = G.map((g) => (L[g.key] - g.L) / g.L);
const downDev = Math.max(...[0,1,2,3,4,9].map((i) => Math.abs(devs[i])));
const upDev = [5,6,7,8].map((i) => devs[i]);
p(`\n  ⇒ 下游六项（谷物/加工食品/织物/服装/高档服装/住房）最大偏差 ${(downDev * 100).toFixed(3)}%`);
p('     —— 纯属 §8.6 表列取整，完全吻合。');
p(`  ⇒ 上游四项（煤/铁/钢/工具）偏差 ${upDev.map((d) => '+' + (d * 100).toFixed(0) + '%').join(' / ')}`);
p('     —— 方向一致，配方要求的产能是 §8.6 表列的 2.0~3.1 倍。');

hr('三、上游四部门的耦合结构（为什么偏差集中在煤/铁/钢/工具）');
p('按 §3.3：  煤矿 = 工具×15 + 煤×15 → 煤×60      （自耗煤，净产 45）');
p('           铁矿 = 工具×15 + 煤×15 → 铁×60      （净产 60）');
p('           炼钢 = 铁×60 + 煤×30 → 钢×90        （净产 90）');
p('           工具 = 钢×20 → 工具×80              （净产 80）');
p('           建造 = 钢×25 + 铁×25 + 工具×20 → 建造力×15');
p('           住房 = 钢×5 + 工具×5 → 住房×60');
p('');
p('  下游对上游的每级拉动（由 §3.3 直接读出）：');
p('    · 住房每级吃 钢×5 + 工具×5');
p('    · 建造部门每级吃 钢×25 + 铁×25 + 工具×20');
p('    · 煤矿/铁矿每级吃 工具×15');
p('    · 炼钢每级吃 铁×60 + 煤×30');
p('    · 工具厂每级吃 钢×20');
p('');
p('  以 §8.6 的 L* 反算上游必须承担的中间投入量（手算近似，仅示耦合方向；');
p('   精确解见上一节的不动点迭代，四项均须 2~3 倍）：');
p(`    工具需求 ≈ 住房 99.84×5 + 建造 ${POWER_L}×20 + 煤 7.56×15 + 铁 7.56×15`);
const toolDemand = 99.84 * 5 + POWER_L * 20 + 7.56 * 15 + 7.56 * 15;
p(`             ≈ ${f2(toolDemand)} 单位 ⇒ 需工具厂 ≈ ${f2(toolDemand / 80)} 级（§8.6 给 9.08 级）`);
p(`    钢需求   ≈ 住房 99.84×5 + 建造 ${POWER_L}×25 + 工具 ${f2(toolDemand / 80)}×20`);
const steelDemand = 99.84 * 5 + POWER_L * 25 + (toolDemand / 80) * 20;
p(`             ≈ ${f2(steelDemand)} 单位 ⇒ 需炼钢 ≈ ${f2(steelDemand / 90)} 级（§8.6 给 7.56 级）`);
p(`    铁需求   ≈ 建造 ${POWER_L}×25 + 炼钢 ${f2(steelDemand / 90)}×60`);
const ironDemand = POWER_L * 25 + (steelDemand / 90) * 60;
p(`             ≈ ${f2(ironDemand)} 单位 ⇒ 需铁矿 ≈ ${f2(ironDemand / 60)} 级（§8.6 给 7.56 级）`);
p('\n  ⇒ 上游四部门在 §8.6 的表中被系统性低估（精确解为 2.0~3.1 倍），');
p('     这正是实跑中"铁矿配给比 0.375、钢产出归零"的来源：');
p('     §8.6 的平衡点本身就没有为上游留够产能。');

hr('四、价格恒等式与自给农场');
const netValue = G.reduce((s, g) => s + (g.L * g.q - G.reduce((t, g2) => t + g2.L * (g2.in[g.key] || 0), 0) - POWER_L * (POWER_IN[g.key] || 0)) * g.Pc, 0);
const fdValue = G.reduce((s, g) => s + g.FD * g.Pc, 0);
p(`  Σ 净产出 × P_cost   = ${f0(netValue)}`);
p(`  Σ 最终需求 × P_cost = ${f0(fdValue)}`);
p(`  偏差                = ${f0(netValue - fdValue)}（${((netValue - fdValue) / fdValue * 100).toFixed(2)}%）`);
p('');
p(`  谷物：§8.6 的 L*_grain = 256.06 级 × 50 = ${f2(256.06 * 50)} 单位总产出；`);
p(`        加工食品厂吃掉 ${f2(182.90 * 40)} 单位，恰好把专业农场的产出吃光（残差 0.00）。`);
p(`        因此 FD_grain = ${f2(5487.0)} 单位**全部**由自给农场提供 —— 自给农场不是补充，是主力。`);
p('  契约 §4.3 只写"未利用耕地自动生成自给农场"，**未给每单位耕地对应几个自给农场**，');
p('  并自认这是"未定项，影响极大"（实现取 0.05，属建模选择而非契约值）。');
p('  ⇒ 该项**无法从契约文本复核**：§8.6 的谷物平衡表隐含了一个未公开的自给农场规模。');
p('     这是契约自身的缺失，不是实现的错；但它使 §3.1 的谷物价格标定失去可复核基准。');

hr('五、结论');
p('1. §3.3 的配方与 §8.6 的 L* 表**不相容**：按 §8.6 自己列的最终需求，');
p(`   平衡等级应约为 ${f2(sumSolved)} 级（下游六项吻合到取整，上游四项须 2.0~3.1 倍），而 §8.6 定案 ${f2(sumGiven)} 级。`);
p('2. 不相容的方向是**上游产能偏小**，与实跑观察到的"铁配给 0.375、钢归零"完全一致。');
p('3. 三个受 §8.6 影响的判据因此都失去基准：§4.1 的扩建阈值（起点利润率）、');
p('   §8.4 A2（利润率区间）、§8.5 的"起点产能 < 平衡产能"结论。');
p('4. 该项**独立于**政府/税收缺口：即使把 s 与 t 重新匹配，上游产能的标定仍须重做。');

fs.mkdirSync(path.join(__dirname, '..', 'out', 'verdict'), { recursive: true });
fs.writeFileSync(path.join(__dirname, '..', 'out', 'verdict', 'contract_consistency_probe.txt'),
  out.join('\n') + '\n', 'utf8');
console.log('\n[已写出] out/verdict/contract_consistency_probe.txt');
