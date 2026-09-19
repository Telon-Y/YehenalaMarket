// VC_Go/tools/zeroprofit_cost_probe.js —— 核验"零利润价是否同时包含人工成本与原料成本"
//
// 用户的核验要求（2026-09-19）：
//   「确认先前的零利润算法包括了人工成本和原料成本」
//
// 本探针把 §3.4 的零利润价方程拆开算，逐项给出数字：
//
//   方程：p = Aᵀ·p + l   ⇒   p = (I − Aᵀ)⁻¹·l
//     · l_j = 每级工资总额 ÷ 单级产出 = (5000 × 6.75) / q_j = 33,750 / q_j   ← 人工成本
//     · A[i][j] = 每级投入量 ÷ 单级产出 = q_ij / q_j                          ← 原料/中间投入
//     · p_i 出现在 Aᵀp 里，即【上游商品的价格】= 上游的人工 + 上游的原料（递归）
//
// 逐商品输出：
//   ① 单位人工成本 l_j（直接）
//   ② 单位原料成本 Σ_i A[i][j]·p_i（直接，按零利润价计价）
//   ③ 两者之和 vs 解出的 p_j（恒等式核验）
//   ④ 与契约 §3.1 表格的 P_cost 列对比（表格是取整值）
//   ⑤ 累计人工含量 Σ_k [(I−Aᵀ)⁻¹][k][j]·l_k —— 封闭经济里唯一初级要素是劳动，
//      故它应当**恰好等于 p_j**：即"原料成本"最终也是上游人工的载体。
//
// 运行：ELECTRON_RUN_AS_NODE=1 "<electron>" VC_Go/tools/zeroprofit_cost_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
// 契约 §3.3：每级产出 q、每级投入 [投入品下标, 投入量]
const REC = [
  [50, []],
  [45, [[0, 40]]],
  [45, []],
  [100, [[2, 60]]],
  [30, [[2, 25]]],
  [60, [[8, 15], [5, 15]]],
  [60, [[8, 15], [5, 15]]],
  [90, [[6, 60], [5, 30]]],
  [80, [[7, 20]]],
  [60, [[7, 5], [8, 5]]],
  [15, [[7, 25], [6, 25], [8, 20]]],
];
// 契约 §3.1 的 P_cost 列（取整后的表格值）与 P_init 列
const PCOST_DOC = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT_DOC = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
// 契约 §5 工资表
const CLASSES = [[0.75, 5], [0.20, 10], [0.05, 20]];
const LABOR_PER_LEVEL = 5000;
const n = G.length;

function solve(M, b) {
  const m = b.length;
  const a = M.map((r, i) => r.concat([b[i]]));
  for (let c = 0; c < m; c++) {
    let p = c;
    for (let r = c + 1; r < m; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
    const t = a[c]; a[c] = a[p]; a[p] = t;
    for (let r = 0; r < m; r++) {
      if (r === c) continue;
      const f = a[r][c] / a[c][c];
      if (!f) continue;
      for (let k = c; k <= m; k++) a[r][k] -= f * a[c][k];
    }
  }
  return a.map((r, i) => r[m] / a[i][i]);
}
function invert(M) {
  const m = M.length;
  const a = M.map((r, i) => r.concat(Array.from({ length: m }, (_, k) => (i === k ? 1 : 0))));
  for (let c = 0; c < m; c++) {
    let p = c;
    for (let r = c + 1; r < m; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
    const t = a[c]; a[c] = a[p]; a[p] = t;
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

// ── 构造 A 与 l（与 calibrate.InputMatrix / UnitLaborCost 同式）──
const avgWage = CLASSES.reduce((s, [share, w]) => s + share * w, 0);
const wagePerLevel = LABOR_PER_LEVEL * avgWage;
const A = Array.from({ length: n }, () => new Array(n).fill(0));
const l = new Array(n).fill(0);
for (let j = 0; j < n; j++) {
  const [q, inp] = REC[j];
  l[j] = wagePerLevel / q;
  for (const [i, qty] of inp) A[i][j] = qty / q;
}

console.log('=== 一、工资表与单位人工成本 l_j（契约 §5 → §3.4）===\n');
console.log(`阶层加权平均工资 = ${CLASSES.map(([s, w]) => `${s}×${w}`).join(' + ')} = ${avgWage} 元/人/周期`);
console.log(`每级工资总额 = ${LABOR_PER_LEVEL} 人 × ${avgWage} = ${wagePerLevel} 元/级/周期`);
console.log('（金融区每级 1,000 人 × 6.75 = 6,750 元不进入本方程，见 §3.4 注：它不生产商品）\n');
console.log('商品       单级产出 q    l_j = 33,750/q');
for (let j = 0; j < n; j++) {
  console.log(`${G[j].padEnd(8)} ${String(REC[j][0]).padStart(9)} ${l[j].toFixed(3).padStart(18)}`);
}

// ── 解零利润价与加成价 ──
const M0 = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => (i === j ? 1 : 0) - A[j][i]));
const p = solve(M0, l);
const m0 = 1 / 6;
const Mm = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => (i === j ? 1 - m0 : 0) - A[j][i]));
const pinit = solve(Mm, l);
const Linv = invert(M0);

console.log('\n=== 二、零利润价分解：人工 + 原料 = p_j（恒等式核验）===\n');
console.log('商品        人工 l_j      原料 ΣA[i][j]·p_i        合计          解出 p_j        残差        人工占比');
let maxRes = 0;
for (let j = 0; j < n; j++) {
  let material = 0;
  for (let i = 0; i < n; i++) material += A[i][j] * p[i];
  const sum = l[j] + material;
  const res = Math.abs(sum - p[j]) / p[j];
  maxRes = Math.max(maxRes, res);
  console.log(`${G[j].padEnd(8)} ${l[j].toFixed(3).padStart(11)} ${material.toFixed(3).padStart(20)} ` +
    `${sum.toFixed(3).padStart(14)} ${p[j].toFixed(3).padStart(15)} ${res.toExponential(1).padStart(11)} ` +
    `${((l[j] / p[j]) * 100).toFixed(1).padStart(9)}%`);
}
console.log(`\n最大恒等式残差 = ${maxRes.toExponential(2)}（应为机器精度量级）`);

console.log('\n=== 三、与契约 §3.1 表格的 P_cost 列对比 ===\n');
console.log('商品        探针解 p_j      契约表格 P_cost     相对偏差');
let maxDev = 0;
for (let j = 0; j < n; j++) {
  const dev = Math.abs(p[j] - PCOST_DOC[j]) / PCOST_DOC[j];
  maxDev = Math.max(maxDev, dev);
  console.log(`${G[j].padEnd(8)} ${p[j].toFixed(2).padStart(13)} ${String(PCOST_DOC[j]).padStart(18)} ${(dev * 100).toFixed(4).padStart(13)}%`);
}
console.log(`\n最大相对偏差 = ${(maxDev * 100).toFixed(4)}%（全部来自 §3.1 表格的取整）`);

console.log('\n=== 四、加成价与"实际成本基"利润率（§3.4 步骤 2）===\n');
console.log('商品      加成价 P_init   契约表 P_init   投入也按 P_init 计价的利润率');
let maxMarginErr = 0;
for (let j = 0; j < n; j++) {
  let material = 0;
  for (let i = 0; i < n; i++) material += A[i][j] * pinit[i] * REC[j][0]; // 每级投入成本
  const revenue = REC[j][0] * pinit[j];
  const cost = wagePerLevel + material;
  const margin = revenue / cost - 1;
  maxMarginErr = Math.max(maxMarginErr, Math.abs(margin - m0 / (1 - m0)));
  console.log(`${G[j].padEnd(8)} ${pinit[j].toFixed(1).padStart(12)} ${String(PINIT_DOC[j]).padStart(15)} ${(margin * 100).toFixed(3).padStart(20)}%`);
}
console.log(`\n利润率与 m/(1−m) = ${((m0 / (1 - m0)) * 100).toFixed(2)}% 的最大偏差 = ${(maxMarginErr * 100).toFixed(4)} 个百分点`);

console.log('\n=== 五、原料成本里装的是什么：Neumann 展开（按上游轮次分解人工）===\n');
// p = (I − Aᵀ)⁻¹ l = l + Aᵀl + (Aᵀ)²l + …  （谱半径 0.5，级数收敛）
// 第 r 轮 = 第 r 层上游投入所"携带"的人工成本。这是"原料成本最终是上游人工"的可核验形式。
const rounds = [l.slice()];
for (let r = 1; r <= 6; r++) {
  const prev = rounds[r - 1];
  const out = new Array(n).fill(0);
  // (Aᵀ·prev)_j = Σ_i A[i][j]·prev_i
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) out[j] += A[i][j] * prev[i];
  rounds.push(out);
}
console.log('轮次 r（第 r 层上游）' + G.map((g) => g.padStart(10)).join(''));
for (let r = 0; r <= 6; r++) {
  console.log(`  r = ${r}`.padEnd(16) + rounds[r].map((x) => x.toFixed(1).padStart(10)).join(''));
}
console.log('  ' + '累计'.padEnd(14) + G.map((_, j) => rounds.slice(0, 7).reduce((s, rr) => s + rr[j], 0).toFixed(1).padStart(10)).join(''));
console.log('  ' + 'p_j（解）'.padEnd(14) + G.map((_, j) => p[j].toFixed(1).padStart(10)).join(''));
const tailMax = Math.max(...G.map((_, j) => Math.abs(p[j] - rounds.slice(0, 7).reduce((s, rr) => s + rr[j], 0))));
console.log(`\n前 7 轮的累计与 p_j 的差（= 被截断的更高阶项；谱半径 0.5 ⇒ 几何衰减）最大 = ${tailMax.toFixed(2)} 元（建造力那一列）`);
console.log('读法：谷物的价格 100% 是它自己的人工；建造力的 7,250 元里只有 2,250 元是自己的工资，');
console.log('      其余 5,000 元是钢/铁/工具环节的人工（再往上是煤、工具……）——即"原料成本"装载的是上游人工。');

console.log('\n=== 六、结论 ===\n');
console.log('  1. 零利润价**同时**包含人工成本与原料成本，且是显式两项：');
console.log('     p_j = l_j（人工）+ Σ_i A[i][j]·p_i（原料），l_j = 33,750/q_j 来自 §5 工资表，');
console.log('     A[i][j] = 每级投入量/q_j 来自 §3.3 投入产出表。');
console.log('  2. 原料项里的 p_i 是【上游的零利润价】，因此原料成本本身递归地含上游人工与上游原料。');
console.log('  3. 因此"原料成本"装载的是上游人工：把 p 按 Neumann 级数 l + Aᵀl + (Aᵀ)²l + … 展开（第五节），');
console.log('     第 r 轮就是第 r 层上游的人工；建造力的 7,250 元里只有 2,250 元是自己的工资，');
console.log('     其余是钢/铁/工具（再往上是煤、工具…）环节的人工。');
console.log('     封闭经济里唯一初级要素是劳动（无资本租金、无进口），故这与"全局利润恒为 0"是同一件事。');
console.log('  4. 唯一的排除项是金融区（每级 1,000 人 × 6.75 = 6,750 元），契约 §3.4 明确它不生产商品、');
console.log('     故不进入 l；这不是漏算人工成本。');
console.log('  5. 运行时 AI 决策用的是**实际成本基**（§3.1 的 † 注：投入按当期市价而非 P_cost 计价），');
console.log('     该口径同样含人工与原料两项；本探针第四节验证加成价下 11 种建筑利润率全部恰为 20%。');

fs.writeFileSync('out/zeroprofit_cost_probe.txt', OUT.join('\n') + '\n', 'utf8');
