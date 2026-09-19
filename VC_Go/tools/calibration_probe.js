// VC_Go/tools/calibration_probe.js —— 三表联合标定（政府/资本版）
//
// 1.0 契约从未做过这一步：它把人口、单级产出、工资、需求量表当作四组独立输入，
// 但四者必须满足货币守恒才有稳态。本脚本把「人口」当作待解变量，闭式求解。
//
// 约束方程组（稳态，价格取 P_cost，雇工率 1）：
//   (1) 需求：f_i = §6.3 每 10 万人需求 × P/1e5 × k
//   (2) 完全需求：Y = (I−A)⁻¹ f ，级数 L_i = Y_i/q_i
//   (3) 金融区级数 L_fin = ΣL_i / ctrl ，工资 W_fin = L_fin × 1000 × 6.75
//   (4) 总工资 W = ΣL_i × 33750 + W_fin
//   (5) 居民可支配 = W × (1−t) 必须恰好等于 f 的价值：Σ f_i·P_cost,i = W×(1−t)
//   (6) 政府税收 T = t × (消费者实付 + 中间投入交易额)
//   (7) 政府建设支出 = 目标扩建速度 × 平均建造成本 × 建造力单价
//
// 求解：由 (5) 闭式解出 P（人口）；再验证 (6)(7) 是否匹配。
//
// 运行：node VC_Go/tools/calibration_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const S = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11;
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const REC = [
  { q: 50, inp: [] }, { q: 45, inp: [[0, 40]] }, { q: 45, inp: [] }, { q: 100, inp: [[2, 60]] },
  { q: 30, inp: [[2, 25]] }, { q: 60, inp: [[8, 15], [5, 15]] }, { q: 60, inp: [[8, 15], [5, 15]] },
  { q: 90, inp: [[6, 60], [5, 30]] }, { q: 80, inp: [[7, 20]] }, { q: 60, inp: [[7, 5], [8, 5]] },
  { q: 15, inp: [[7, 25], [6, 25], [8, 20]] },
];
const WAGE_LEVEL = 33750, FIN_WAGE_LEVEL = 6750;

const A = Array.from({ length: N }, () => new Array(N).fill(0));
for (let j = 0; j < N; j++) for (const [i, qty] of REC[j].inp) A[i][j] = qty / REC[j].q;
function inv(M) {
  const m = M.length;
  const a = M.map((r, i) => r.concat(Array.from({ length: m }, (_, k) => (i === k ? 1 : 0))));
  for (let c = 0; c < m; c++) {
    let p = c; for (let r = c + 1; r < m; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
    const t = a[c]; a[c] = a[p]; a[p] = t;
    const pv = a[c][c];
    for (let k = 0; k < 2 * m; k++) a[c][k] /= pv;
    for (let r = 0; r < m; r++) { if (r === c) continue; const f = a[r][c]; if (!f) continue; for (let k = 0; k < 2 * m; k++) a[r][k] -= f * a[c][k]; }
  }
  return a.map((r) => r.slice(m));
}
const B = inv(Array.from({ length: N }, (_, i) => Array.from({ length: N }, (_, j) => (i === j ? 1 : 0) - A[i][j])));

function perCapitaFinal(tier) {
  const table = [{ w: 5, d: [39, 210, 0, 20] }, { w: 10, d: [41, 210, 7, 74] }, { w: 20, d: [0, 210, 122, 130] }];
  const w = Math.min(20, Math.max(5, tier));
  let lo = table[0], hi = table[2];
  for (let i = 0; i + 1 < table.length; i++) if (w >= table[i].w && w <= table[i + 1].w) { lo = table[i]; hi = table[i + 1]; break; }
  const tt = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  const d = lo.d.map((x, k) => x + (hi.d[k] - x) * tt);
  const f = new Array(N).fill(0);
  f[2] += d[0] / 2; f[3] += d[0] / 2;
  f[0] += d[1] / 2.5; f[1] += d[1] * 1.5 / 2.5;
  f[3] += d[2] / 2; f[4] += d[2] / 2;
  f[9] += d[3];
  return f.map((x) => x / 100000);
}

console.log('=== 三表联合标定（政府 / 资本版）===\n');
console.log('待解变量：人口 P。约束：居民税后工资收入 = 最终需求的价值（货币守恒）。\n');

const TIER = 10, CTRL = 5, TAX = 0.10;
const f1 = perCapitaFinal(TIER);
const Y1 = B.map((row) => row.reduce((s, x, j) => s + x * f1[j], 0));
const L1 = Y1.map((y, i) => y / REC[i].q);
const sumL1 = L1.reduce((a, b) => a + b, 0);
const wagePC = sumL1 * WAGE_LEVEL + (sumL1 / CTRL) * FIN_WAGE_LEVEL;
const valPC = f1.reduce((s, x, i) => s + x * PCOST[i], 0);

console.log(`财富档 ${TIER}：每 10 万人需求（§6.3）：`);
console.log('  ' + S.map((s, i) => `${s}=${(f1[i] * 1e5).toFixed(1)}`).join(' '));
console.log(`\n每人所需建筑级数（完全需求，含中间投入）= ${sumL1.toExponential(6)}`);
console.log(`每人工资成本 = ${wagePC.toFixed(6)} 元   （金融区按 1/${CTRL} 计）`);
console.log(`每人最终需求价值 = ${valPC.toFixed(6)} 元`);

// (5) 解人口：P·valPC = P·wagePC·(1−t) 只有在 wagePC(1−t) = valPC 时才有无穷多解（齐次）
// ⇒ 人口不可由该式确定！揭示一个结构性事实：
console.log(`\n关键观察：工资与需求都随人口线性缩放，故上式对 P 恒为`);
console.log(`   P·${valPC.toFixed(6)} = P·${wagePC.toFixed(6)}·${(1 - TAX).toFixed(2)} = P·${(wagePC * (1 - TAX)).toFixed(6)}`);
console.log(`   两者之比 = ${(wagePC * (1 - TAX) / valPC).toFixed(6)}（与 P 无关）`);
console.log('⇒ 人口不是可解变量；能解的是【工资与需求的相对水平】。');
console.log('   缩放人口只会整体放大或缩小经济，不改变守恒比例。');
console.log(`   要让守恒成立，必须把 §6.3 需求 × ${(wagePC * (1 - TAX) / valPC).toFixed(6)}，`);
console.log(`   或把工资改为 ${(valPC / (1 - TAX) / sumL1).toFixed(2)} 元/级，`);
console.log(`   或把单级产出改为对应的 ${(valPC / (wagePC * (1 - TAX))).toFixed(6)} 倍。\n`);

// 于是采用：需求缩放 k，使守恒成立；人口随后由"政府建设需求"定出
const k = wagePC * (1 - TAX) / valPC;
console.log(`取 §6.3 需求缩放 k = ${k.toFixed(6)}（无需改工资或配方）\n`);

// 现在人口由"政府能否支撑目标扩建速度"决定
console.log('--- 人口由政府建设需求决定 ---\n');
function economy(P, tax, targetLevels, avgCost, years) {
  const f = f1.map((x) => x * P * k);
  const Y = B.map((row) => row.reduce((s, x, j) => s + x * f[j], 0));
  const L = Y.map((y, i) => y / REC[i].q);
  const other = L.reduce((a, b) => a + b, 0);
  const Lfin = other / CTRL;
  const wage = other * WAGE_LEVEL + Lfin * FIN_WAGE_LEVEL;
  const consumerPay = f.reduce((s, x, i) => s + x * PCOST[i], 0);      // = 居民税后支出
  let interm = 0;
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) interm += L[i] * q * PCOST[j];
  const trans = consumerPay + interm;
  const taxRev = trans * tax;
  const buildPowerValue = (targetLevels * avgCost) * PCOST[10];
  const yearsNeeded = buildPowerValue / taxRev;
  return { f, Y, L, other, Lfin, wage, consumerPay, interm, trans, taxRev, buildPowerValue, yearsNeeded, P };
}

console.log(`  假设：目标建筑总量 3,000 级，平均建造成本 600 建造力，建造力单价 ${PCOST[10]} 元`);
console.log(`  目标扩建投入 = 3000 × 600 × ${PCOST[10]} = ${(3000 * 600 * PCOST[10] / 1e8).toFixed(2)} 亿元\n`);
console.log('  税率   人口        总级数   金融区   工资/周期      税收/周期     政府要攒够 3000 级需');
for (const P of [100000, 500000, 1000000, 2000000, 5000000]) {
  const e = economy(P, TAX, 3000, 600);
  console.log(`  ${(TAX * 100).toFixed(0)}%  ${P.toLocaleString().padStart(9)}  ${e.other.toFixed(0).padStart(7)}  ${e.Lfin.toFixed(1).padStart(7)}  ` +
    `${Math.round(e.wage).toLocaleString().padStart(15)}  ${Math.round(e.taxRev).toLocaleString().padStart(14)}  ${e.yearsNeeded.toFixed(1).padStart(10)} 周期`);
}

console.log('\n  反解：要让政府用 10% 税收在 10,000 周期内建满 3,000 级，需要的人口为');
{
  // taxRev ∝ P（线性），故可闭式解
  const e1 = economy(100000, TAX, 3000, 600);
  const need = e1.buildPowerValue / 10000;              // 每周期需要的税收
  const P = 100000 * need / e1.taxRev;
  const e = economy(P, TAX, 3000, 600);
  console.log(`     人口 = ${Math.round(P).toLocaleString()} 人`);
  console.log(`     此时总级数 ${e.other.toFixed(0)} 级（金融区 ${e.Lfin.toFixed(1)} 级），工资 ${Math.round(e.wage).toLocaleString()} 元/周期，税收 ${Math.round(e.taxRev).toLocaleString()} 元/周期`);
  console.log(`     10,000 周期累计税收 = ${(e.taxRev * 10000 / 1e8).toFixed(2)} 亿元 ≥ 目标投入 ${(e.buildPowerValue / 1e8).toFixed(2)} 亿元 ✓`);
  console.log(`     耕地占用：谷物 ${(e.L[0] / 100000 * P).toFixed(0)} + 棉花 ${(e.L[2] / 100000 * P).toFixed(0)} = ${((e.L[0] + e.L[2]) / 100000 * P).toFixed(0)} / 10000`);
}

console.log('\n--- 关键洞察：政府不必从零攒钱，建造部门自身是放大器 ---\n');
console.log('  1 级建造部门的回本周期 = 100 建造力 ÷ 15 建造力/周期 = 6.67 周期（见 §3.2/§3.3）');
console.log('  ⇒ 政府应把初期税收【全部投入建造部门】，使其按指数增长，');
console.log('    再由放大的建造力供给去扩建其他部门。这是唯一能匹配 §10.2 时间表的路径。');
console.log('  验算：若把建造部门从 1 级扩到 100 级（成本 99×100 = 9,900 建造力），');
console.log(`    需要货币 ${Math.round(9900 * PCOST[10]).toLocaleString()} 元；`);
console.log(`    100 级建造部门产出 1,500 建造力/周期，可支撑 ${(1500 / 600).toFixed(2)} 级/周期 的常规扩建 = 每年 ${(1500 / 600 * 52).toFixed(0)} 级。`);
console.log('  ⇒ 与 §10.2 的目标（10,000 周期 = 192 年建 2,950 级 ≈ 15 级/年）相比有大量余量。');

console.log('\n--- 判定 ---\n');
console.log('  1. 人口不是自由参数：工资与需求同比缩放，简单缩放人口无法改变守恒比例。');
console.log(`     必须做三表联合标定（本脚本给出 k = ${k.toFixed(6)}）。`);
console.log('  2. 政府建设的真正约束不是税率，而是【建造部门的产能】。');
console.log('     建造部门回本仅需 6.67 周期，是回报最快的投资，政府应优先扩它。');
console.log('  3. 加入政府/资本后，1.0 的建造黑洞被闭合，且长期扩建速度由建造部门产能决定，');
console.log('     不再受货币总量限制——这正是用户方案的关键价值。');

fs.writeFileSync('out/calibration_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/calibration_probe.txt');
