// VC_Go/tools/structure_probe.js —— 1.0 契约产业结构可行性判定（rev 2，修正 Leontief 逆与缺口 LP）
//
// 目的：抛开价格与动力学，只回答一个静态问题——
//   在契约 §4.2 的产能上限下，§3.3 投入产出表 + §4.3 资金 + §6.3 消费需求
//   是否存在互相自洽的规模结构？最大可持续人口是多少？
//
// 方法：
//   标准 Leontief 完全消耗矩阵 A[i][j] = 投入量/产出量（列 = 被生产的商品 j）
//   完全需求矩阵 B = (I − A)⁻¹（列 = 每单位最终需求 j 对全部商品的总产出需求）
//   给定人口 Pop，最终需求向量 f = b·Pop（§6.2/§6.3 唯一确定）
//   物质平衡要求总产出 Y ≥ A·Y + f  ⇒  Y ≥ B·f
//   再逐商品检查级数 = Y_i/q_i 是否超过 §4.2 的产能上限
//
// 运行：node VC_Go/tools/structure_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const n = 11;
const REC = [
  { out: 0, q: 50, inp: {} },
  { out: 1, q: 45, inp: { 0: 40 } },
  { out: 2, q: 45, inp: {} },
  { out: 3, q: 100, inp: { 2: 60 } },
  { out: 4, q: 30, inp: { 2: 25 } },
  { out: 5, q: 60, inp: { 8: 15, 5: 15 } },
  { out: 6, q: 60, inp: { 8: 15, 5: 15 } },
  { out: 7, q: 90, inp: { 6: 60, 5: 30 } },
  { out: 8, q: 80, inp: { 7: 20 } },
  { out: 9, q: 60, inp: { 7: 5, 8: 5 } },
  { out: 10, q: 15, inp: { 7: 25, 6: 25, 8: 20 } },
];
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100];
const ARABLE = 10000, COALCAP = 500, IRONCAP = 500, POWERCAP = 1000;
const CAPS = { 5: COALCAP, 6: IRONCAP, 10: POWERCAP };

// ---------- 矩阵工具 ----------
const A = Array.from({ length: n }, () => new Array(n).fill(0));
for (let j = 0; j < n; j++) for (const [i, qty] of Object.entries(REC[j].inp)) A[i][j] = qty / REC[j].q;

function invert(M) {
  const m = M.length;
  const a = M.map((r, i) => r.concat(Array.from({ length: m }, (_, k) => (i === k ? 1 : 0))));
  for (let c = 0; c < m; c++) {
    let p = c;
    for (let r = c + 1; r < m; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
    const t = a[c]; a[c] = a[p]; a[p] = t;
    if (Math.abs(a[c][c]) < 1e-14) throw new Error('奇异矩阵');
    const pv = a[c][c];
    for (let k = 0; k < 2 * m; k++) a[c][k] /= pv;
    for (let r = 0; r < m; r++) {
      if (r === c) continue;
      const f = a[r][c];
      if (!f) continue;
      for (let k = 0; k < 2 * m; k++) a[r][k] -= f * a[c][k];
    }
  }
  return a.map((r) => r.slice(m));
}
const ImA = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => (i === j ? 1 : 0) - A[i][j]));
const Binv = invert(ImA);                       // B[i][j] = 每单位最终需求 j 对商品 i 的总产出需求
function matVec(M, v) { return M.map((r) => r.reduce((s, x, j) => s + x * v[j], 0)); }
const Bcol = (j) => Binv.map((r) => r[j]);

// ---------- §6.2/§6.3 最终需求（每 1 人每 tick） ----------
function perCapitaFinal(tier) {
  // §6.3 三档表 + 档间线性插值（§6.3 只定义了"财富等级由平均工资插值确定"，档内需求按此插值）
  const table = [ { w: 5, d: [39, 210, 0, 20] }, { w: 10, d: [41, 210, 7, 74] }, { w: 20, d: [0, 210, 122, 130] } ];
  const w = Math.min(20, Math.max(5, tier));
  let lo = table[0], hi = table[table.length - 1];
  for (let i = 0; i + 1 < table.length; i++) if (w >= table[i].w && w <= table[i + 1].w) { lo = table[i]; hi = table[i + 1]; break; }
  const t = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  const d = lo.d.map((x, k) => x + (hi.d[k] - x) * t);
  const gq = d.map((x) => x / 100000);
  const f = new Array(n).fill(0);
  // §6.2 使用价值表决定组内分摊：简朴衣物组 {织物:1, 服装:1}；基础食物组 {谷物:1, 加工食品:1.5}
  // 标准衣物组 {服装:1, 高档服装:1}；住宅组 {住房:1}
  f[2] += gq[0] * 1 / 2; f[3] += gq[0] * 1 / 2;
  f[0] += gq[1] * 1 / 2.5; f[1] += gq[1] * 1.5 / 2.5;
  f[3] += gq[2] * 1 / 2; f[4] += gq[2] * 1 / 2;
  f[9] += gq[3];
  return f;
}

console.log('=== 1.0 契约产业结构可行性判定 ===\n');
console.log('方法：标准 Leontief 完全消耗矩阵 B = (I − A)⁻¹');
console.log('      物质平衡 Y ≥ B·f，f = §6.2/§6.3 的最终消费需求（每单位人口）');
console.log('      再逐商品检查级数是否越过 §4.2 的产能上限\n');

// ---------- 一、完全消耗系数：每单位最终需求的全部直接+间接拉动 ----------
console.log('=== 一、完全消耗系数 B（每 1 单位最终需求对每种商品的总产出需求）===\n');
const names = G.map((g, i) => g);
const key = [9, 7, 8, 5, 6, 10, 0, 2, 3, 1, 4];
console.log('        ' + key.map((j) => G[j].padEnd(6, '　')).join(''));
for (const i of key) {
  console.log('  ' + G[i].padEnd(6, '　') + key.map((j) => Binv[i][j].toFixed(4).padStart(8)).join(''));
}
console.log('\n读法（例）：生产 1 单位住房，需要完全消耗');
for (const j of [9]) for (const i of [5, 6, 7, 8]) console.log(`  ${G[i]} = ${Binv[i][j].toFixed(4)}`);

// ---------- 二、每单位最终需求对瓶颈商品的拉动 ----------
console.log('\n=== 二、关键链条：每 1 单位住房/服装/建造力的全部间接消耗 ===\n');
console.log('商品        住房    高档服装   服装    加工食品   建造力');
for (const i of [0, 2, 5, 6, 7, 8, 10]) {
  console.log('  ' + G[i].padEnd(6, '　') + [9, 4, 3, 1, 10].map((j) => Binv[i][j].toFixed(4).padStart(8)).join(''));
}

// ---------- 三、给定人口下的级数需求（含间接） ----------
function levelsForPop(Pop, f) {
  const fv = f.map((x) => x * Pop);
  const Y = matVec(Binv, fv);
  return Y.map((y, i) => y / REC[i].q);
}
function reportPop(Pop, tier, label) {
  const f = perCapitaFinal(tier);
  const L = levelsForPop(Pop, f);
  console.log(`\n--- ${label}：人口 ${Pop.toLocaleString()}，财富档 ${tier} ---`);
  console.log('商品         完全需求级数     契约上限     越界?');
  let over = [];
  for (let i = 0; i < n; i++) {
    const cap = (i === 0 || i === 2) ? ARABLE : (CAPS[i] || Infinity);
    const ok = L[i] <= cap + 1e-9;
    if (!ok) over.push(`${G[i]}(×${(L[i] / cap).toFixed(1)})`);
    console.log('  ' + G[i].padEnd(6, '　') + L[i].toFixed(2).padStart(14) + (isFinite(cap) ? cap.toLocaleString().padStart(12) : '       —'.padStart(12)) + (ok ? '    ok' : '    ❌ 越界'));
  }
  console.log('  越界商品：' + (over.length ? over.join(' ') : '（无）'));
  return { L, over };
}
console.log('\n=== 三、给定人口下的完全需求级数 vs 契约产能上限 ===\n');
reportPop(100000, 20, '财富 20 档');
reportPop(100000, 7, '财富 7 档（平均工资 6.75 的插值结果）');

// ---------- 四、最大可持续人口（逐商品单约束上界） ----------
console.log('\n=== 四、每一条产能上限所允许的人口上限（单约束口径）===\n');
console.log('商品          上限级数   每级产出   完全需求/人/tick      人口上限');
for (let i = 0; i < n; i++) {
  const cap = (i === 0 || i === 2) ? ARABLE : CAPS[i];
  if (!isFinite(cap)) continue;
  // 求 f_i 对应的"投入系数"：把完全需求折算为每人对该商品的总产出需求
  const f = perCapitaFinal(20);
  const fv = f.map((x) => x * 1);
  const unit = matVec(Binv, fv)[i];               // 每人每 tick 对该商品的总产出需求
  const capOut = cap * REC[i].q;
  console.log('  ' + G[i].padEnd(6, '　') + String(cap).padStart(9) + String(REC[i].q).padStart(11) + unit.toExponential(4).padStart(22) + (capOut / unit).toExponential(3).padStart(16));
}
console.log('  （谷物/棉花按耕地合计 10,000 级、财富 20 档计算）');

// 联合约束：耕地是谷物+棉花合计
{
  const f = perCapitaFinal(20);
  const fv = f.map((x) => x * 1);
  const Y = matVec(Binv, fv);
  const arablePerCapita = Y[0] / REC[0].q + Y[2] / REC[2].q;
  const coalP = (Y[5] / REC[5].q) / COALCAP;
  const ironP = (Y[6] / REC[6].q) / IRONCAP;
  const powerP = (Y[10] / REC[10].q) / POWERCAP;
  console.log(`\n  耕地（谷物+棉花）每人每 tick 占用 ${arablePerCapita.toExponential(4)} 级 → 人口上限 ${(ARABLE / arablePerCapita).toExponential(3)}`);
  console.log(`  煤矿：每人每 tick 占用 ${(Y[5] / REC[5].q).toExponential(4)} 级（上限 500）→ 人口上限 ${(1 / coalP).toExponential(3)}`);
  console.log(`  铁矿：每人每 tick 占用 ${(Y[6] / REC[6].q).toExponential(4)} 级（上限 500）→ 人口上限 ${(1 / ironP).toExponential(3)}`);
  console.log(`  建造部门：每人每 tick 占用 ${(Y[10] / REC[10].q).toExponential(4)} 级（上限 1000）→ 人口上限 ${(1 / powerP).toExponential(3)}`);
}

// ---------- 五、R8 的同型检查：单级收入 vs 单级工资 ----------
console.log('\n=== 五、对照：单级就业与产值量级（检验"规模杠杆"是否已真正消除）===\n');
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const WAGELEVEL = 33750;
console.log('建筑         单级工资     单级收入(按P_cost)   收入/工资     单级固定资本(建造力)   资本/年工资');
for (let i = 0; i < n; i++) {
  const rev = REC[i].q * PCOST[i];
  console.log('  ' + G[i].padEnd(6, '　') + String(WAGELEVEL).padStart(10) + rev.toFixed(0).padStart(18) + (rev / WAGELEVEL).toFixed(3).padStart(14) + String(BUILDCOST[i]).padStart(20) + (BUILDCOST[i] / WAGELEVEL).toFixed(4).padStart(15));
}
console.log('  注：建造力单价按 P_cost = 7,250 计；"资本/年工资"= 建造成本 ÷ (单级工资 × 52)');

// ---------- 六、动态含义：扩建 1 级所需的建造力与时间 ----------
console.log('\n=== 六、扩建吞吐：建 1 级要多久 ===\n');
console.log('建筑            建造成本(建造力)   单工地 30/tick 所需 tick   建造部门级数(每级15建造力/tick)');
for (let i = 0; i < n; i++) {
  console.log('  ' + G[i].padEnd(6, '　') + String(BUILDCOST[i]).padStart(16) + (BUILDCOST[i] / 30).toFixed(2).padStart(24) + (BUILDCOST[i] / 30 / 15).toFixed(3).padStart(28));
}
console.log('  注：契约 §4.1 规定单次扩建 ≤ 当前总数的 10%，且 §4.2 规定每工地 ≤ 30 建造力/tick。');

// ---------- 七、结论 ----------
console.log('\n=== 七、判定 ===\n');
{
  const f20 = perCapitaFinal(20);
  const Y20 = matVec(Binv, f20);
  const needCoal = Y20[5] / REC[5].q, needIron = Y20[6] / REC[6].q;
  const ratC = needCoal / COALCAP, ratI = needIron / IRONCAP;
  console.log(`  按 §6.3 财富 20 档，每 1 人每 tick 需要：`);
  console.log(`    煤的完全产出 ${Y20[5].toExponential(4)}（= ${needCoal.toExponential(4)} 级煤矿，占上限的 ${ratC.toExponential(3)}）`);
  console.log(`    铁的完全产出 ${Y20[6].toExponential(4)}（= ${needIron.toExponential(4)} 级铁矿，占上限的 ${ratI.toExponential(3)}）`);
  console.log(`  ⇒ 煤/铁上限 500 级所允许的人口上限：煤 ${(COALCAP / needCoal).toExponential(3)} 人、铁 ${(IRONCAP / needIron).toExponential(3)} 人`);
  console.log(`  ⇒ 也就是说，只要人口超过约 ${Math.round(Math.min(COALCAP / needCoal, IRONCAP / needIron)).toLocaleString()} 人，`);
  console.log(`     煤或铁的完全需求就必然越界，无论 AI 如何扩建都补不上（因为上限是契约硬约束）。`);
}

fs.writeFileSync('out/structure_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/structure_probe.txt');
