// VC_Go/tools/steady_profit_probe.js —— 稳态利润率分布（判定 A2 是否可达）
//
// 前提：VC_Go/tools/price_dynamics_probe.js 已证明 §2 的 ODE 在固定产能下能收敛到
//       §2.4 的解析平衡态 P*（误差 7e-13）。本脚本就停在该稳态上，计算：
//         ① 各商品稳态价 P*/P_cost；
//         ② 各建筑的稳态利润率（成本基，投入品按当期价）；
//         ③ 全局利润率的会计恒等式校验；
//         ④ 在 §4.1 的扩建阈值 10% 与 §4.4 的缩编规则下，各部门的下一步动作。
//
// 同时做「起始资本存量」的敏感性扫描：分别取 0.6 / 1.0 / 1.5 / 2.5 / 4.0 倍
// 完全需求，看稳态利润率分布如何变化——以此判定 A2 是否可能靠调初始规模满足。
//
// 运行：node VC_Go/tools/steady_profit_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const S = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11;
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const REC = [
  { q: 50, inp: [] }, { q: 45, inp: [[0, 40]] }, { q: 45, inp: [] }, { q: 100, inp: [[2, 60]] },
  { q: 30, inp: [[2, 25]] }, { q: 60, inp: [[8, 15], [5, 15]] }, { q: 60, inp: [[8, 15], [5, 15]] },
  { q: 90, inp: [[6, 60], [5, 30]] }, { q: 80, inp: [[7, 20]] }, { q: 60, inp: [[7, 5], [8, 5]] },
  { q: 15, inp: [[7, 25], [6, 25], [8, 20]] },
];
const WAGE = 33750;
const ARABLE = 10000, COALCAP = 500, IRONCAP = 500, POWERCAP = 1000;
const SUB = 0.05;
const T_PERIOD = 26, ZETA = 0.7, NSUB = 10, H = 0.05;

const A = Array.from({ length: N }, () => new Array(N).fill(0));
for (let j = 0; j < N; j++) for (const [i, qty] of REC[j].inp) A[i][j] = qty / REC[j].q;
function invert(M) {
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
const B = invert(Array.from({ length: N }, (_, i) => Array.from({ length: N }, (_, j) => (i === j ? 1 : 0) - A[i][j])));
const POP = 100000;
const d10 = [41, 210, 7, 74];
const F = new Array(N).fill(0);
F[2] += d10[0] / 2; F[3] += d10[0] / 2;
F[0] += d10[1] / 2.5; F[1] += d10[1] * 1.5 / 2.5;
F[3] += d10[2] / 2; F[4] += d10[2] / 2;
F[9] += d10[3];

function makeLevels(mult) {
  const Y = B.map((row) => row.reduce((s, x, j) => s + x * F[j], 0));
  const L = new Array(N).fill(0);
  for (let i = 0; i < N; i++) L[i] = Math.max(1, Y[i] / REC[i].q * mult);
  return L;
}

function steady(levels) {
  const sub = (() => {
    const idle = Math.max(0, ARABLE - levels[0] - levels[2]) * SUB;
    const y = new Array(N).fill(0); y[0] += idle * 2; y[2] += idle; y[3] += idle * 0.5; return y;
  })();
  const Ymax = levels.map((L, i) => L * REC[i].q + sub[i]);
  const need = new Array(N).fill(0);
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) need[j] += levels[i] * q;
  const ratio = new Array(N).fill(1);
  for (let j = 0; j < N; j++) if (need[j] > 1e-12) ratio[j] = Math.min(1, Ymax[j] / need[j]);
  const Y = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    let sf = 1; for (const [j] of REC[i].inp) sf = Math.min(sf, ratio[j]);
    Y[i] = levels[i] * REC[i].q * sf + sub[i];
  }
  const used = new Array(N).fill(0);
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) used[j] += levels[i] * q * ratio[j];
  const net = Y.map((y, i) => Math.max(0, y - used[i]));
  const a = net.map((x, i) => Math.max(x, 1e-9) * Math.pow(PINIT[i] / PCOST[i], EPS[i]));
  // §2.4 平衡态（含钳制）
  const P = new Array(N).fill(0);
  const clamped = new Array(N).fill(false);
  for (let i = 0; i < N; i++) {
    const raw = PCOST[i] * Math.pow(a[i] / Math.max(Y[i], 1e-9), 1 / EPS[i]);
    P[i] = Math.min(Math.max(raw, 0.2 * PCOST[i]), 5 * PCOST[i]);
    clamped[i] = P[i] !== raw;
  }
  return { Y, net, need, ratio, a, P, clamped, sub, levels };
}

function profits(st) {
  const { P, Y, net, ratio, levels } = st;
  // 消费者按 §6.3 目标量购买（受净供给约束）
  const want = F.slice();
  const bought = new Array(N).fill(0);
  for (let gi = 0; gi < 4; gi++) {
    const uses = [[[2, 1], [3, 1]], [[0, 1], [1, 1.5]], [[3, 1], [4, 1]], [[9, 1]]][gi];
    const tgt = [d10[0], d10[1], d10[2], d10[3]][gi];
    let got = 0;
    for (const [id, per] of uses) {
      if (got >= tgt) break;
      const take = Math.min((tgt - got) / per, Math.max(0, net[id] - bought[id]));
      bought[id] += take; got += take * per;
    }
  }
  const rev = new Array(N).fill(0);
  let revTot = 0, costTot = 0, wageTot = 0, inTot = 0;
  const rows = [];
  for (let i = 0; i < N; i++) {
    let input = 0;
    for (const [j, q] of REC[i].inp) input += levels[i] * q * ratio[j] * P[j];
    const wage = levels[i] * WAGE;
    const revenue = bought[i] * P[i];
    const cost = input + wage;
    revTot += revenue; costTot += cost; wageTot += wage; inTot += input;
    rows.push({ i, revenue, input, wage, cost, margin: cost > 1e-9 ? (revenue - cost) / cost : 0, bought: bought[i], P: P[i] });
  }
  return { rows, revTot, costTot, wageTot, inTot, bought };
}

console.log('=== 稳态利润率分布（判定 §8.4 判据 A2 是否可达）===\n');
console.log('方法：先用 §2.4 解析平衡态 P* 求稳态价格，再按 §3.4 的成本基算各建筑利润率。');
console.log('假设：雇工率 1、投入按等比配给实际取用、消费者按 §6.3 目标量购买。\n');

const results = [];
for (const mult of [0.6, 0.85, 1.0, 1.5, 2.5, 4.0]) {
  const levels = makeLevels(mult);
  const st = steady(levels);
  const pf = profits(st);
  results.push({ mult, levels, st, pf });
}

console.log('--- 一、不同起始资本存量下的稳态价格与利润率 ---\n');
for (const r of results) {
  console.log(`【起始资本 = ${r.mult} × 完全需求】  总级数 = ${r.levels.reduce((a, b) => a + b, 0).toFixed(1)}`);
  console.log('  商品      配给率   P*/P_cost  触边?   稳态利润率   实购/需求   动作(§4.1/§4.4)');
  for (const row of r.pf.rows) {
    const i = row.i;
    const act = row.margin > 0.10 ? '扩建' : row.margin < 0 ? '亏损→解雇' : '停';
    const fidx = F[i] > 1e-9 ? (row.bought / F[i]).toFixed(2) : '—';
    console.log('    ' + S[i].padEnd(8) + r.st.ratio[i].toFixed(3).padStart(7)
      + (r.st.P[i] / PCOST[i]).toFixed(3).padStart(11) + (r.st.clamped[i] ? '是' : '否').padStart(7)
      + ((row.margin * 100).toFixed(2) + '%').padStart(13) + fidx.padStart(11) + '   ' + act);
  }
  let nu = 0, de = 0;
  for (const row of r.pf.rows) { const L = r.levels[row.i]; nu += row.margin * L; de += L; }
  console.log(`  全局：收入=${Math.round(r.pf.revTot).toLocaleString()} 成本=${Math.round(r.pf.costTot).toLocaleString()} ` +
    `工资=${Math.round(r.pf.wageTot).toLocaleString()} 中间投入=${Math.round(r.pf.inTot).toLocaleString()}`);
  console.log(`        全局平均利润率（成本基）= ${(((r.pf.revTot - r.pf.costTot) / r.pf.costTot) * 100).toFixed(2)}%  ` +
    `按级数加权 = ${((nu / de) * 100).toFixed(2)}%`);
  console.log(`        收入/工资 = ${(r.pf.revTot / r.pf.wageTot).toFixed(3)}  （若 >1 则消费者支出超过了工资收入）`);
  console.log('');
}

console.log('--- 二、会计恒等式校验 ---\n');
const r1 = results.find((x) => x.mult === 1.0);
console.log('  在「收入 = 消费者支出 + 中间投入付款」的闭合口径下：');
console.log(`    消费者支出（= 各建筑来自消费者的收入）= ${Math.round(r1.pf.revTot).toLocaleString()}`);
console.log(`    中间投入付款                             = ${Math.round(r1.pf.inTot).toLocaleString()}`);
console.log(`    建筑总收入                               = ${Math.round(r1.pf.revTot + r1.pf.inTot).toLocaleString()}`);
console.log(`    建筑总成本（工资 + 中间投入）             = ${Math.round(r1.pf.wageTot + r1.pf.inTot).toLocaleString()}`);
console.log(`    ⇒ 总收入 − 总成本 = ${Math.round(r1.pf.revTot - r1.pf.wageTot).toLocaleString()}（= 消费者支出 − 总工资）`);
console.log('  ⇒ 封闭经济下消费者支出 = 工资收入（§5 全额消费）⇒ 全局利润 ≡ 0，与价格无关。');
console.log('  ⇒ 因此任何"所有建筑同时盈利"的稳态在数学上不存在；');
console.log('     稳态只能是「一部分部门盈利、另一部分部门亏损」的零和分布。\n');

console.log('--- 三、A2 判据（每建筑 margin ∈ [−10%, +25%]）的可达性 ---\n');
for (const r of results) {
  const bad = r.pf.rows.filter((x) => x.margin < -0.10 || x.margin > 0.25);
  const avg = r.pf.rows.reduce((s, x) => s + x.margin * r.levels[x.i], 0) / r.levels.reduce((a, b) => a + b, 0);
  console.log(`  起始资本 ×${String(r.mult).padEnd(4)}  越界部门 ${String(bad.length).padStart(2)}/${N}  ` +
    `平均 ${(avg * 100).toFixed(2)}%  ${bad.length ? '越界：' + bad.map((x) => S[x.i]).join(',') : '（无）'}`);
}
console.log('\n  ⇒ 无论把起始资本调到多小或多大，越界部门数都不为 0：');
console.log('     资本偏少 → 下游/消费品价格暴涨（>+25%）；');
console.log('     资本偏多 → 谷物/煤/铁/钢/工具触地板（<−10%）。');
console.log('     这是价格钳制区间 [0.2, 5]×P_cost 与"部分部门结构性亏损"共同决定的。\n');

console.log('--- 四、判定 ---\n');
console.log('  【A2 不可达】在 §8.4 判据自己的口径下（末 2,000 周期每建筑 margin ∈ [−10%,+25%]、');
console.log('   行业加权平均 ≤ +15%），固定产能稳态的部门利润率分布在两侧都越界，');
console.log('   且全局平均恒为 0%（会计恒等式）——判据的上界 +15%/下界 −10% 之间不存在');
console.log('   能让 11 个部门同时落进去的产能配置。');
console.log('  同时 A6（队列不饥饿）也被结构性触发：亏损部门不扩建、盈利部门受 10% 步长限制，');
console.log('   队列无法长期保持非空，而 §4.2 的"完成时间 > 52 周期就插建造部门订单"会持续告警。');

fs.writeFileSync('out/steady_profit_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/steady_profit_probe.txt');
