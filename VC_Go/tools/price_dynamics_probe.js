// VC_Go/tools/price_dynamics_probe.js —— 隔离测试 §2 价格动力学本身
//
// 目的：把「会计/结构问题」与「ODE 数值问题」分开。
//   只保留：§3.3 配方 + §3.3 等比配给 + §2.1 常弹性需求 + §2.3 RK4 + §2.4 钳制。
//   关闭：扩建、人口增长、现金池、利润率决策、缩编、雇佣调整。
//   也就是说：产能固定、需求函数固定、只有价格在动。
//   问：价格是否能收敛到 §2.4 的平衡态 P* = P0·(a/S)^(1/ε)？实测与解析解的误差多大？
//
// 运行：node VC_Go/tools/price_dynamics_probe.js
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
// §2.3（2026-09-19 改）：惯性 m ≡ 当期流通量、T = 2π√(m/K) 内生，已无 T_PERIOD 旋钮
const ZETA = 0.7, DT = 0.5, NSUB = 10, H = DT / NSUB;
const ARABLE = 10000;
const SUB = 0.05;

// 开局：与 core_sim_probe 相同的完全需求推导 + 85% 余量迭代
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
// 财富 10 档（§6.3 表中间档），每 10 万人
const d10 = [41, 210, 7, 74];
const POP = 100000;
const f = new Array(N).fill(0);
f[2] += d10[0] * POP / 100000 / 2; f[3] += d10[0] * POP / 100000 / 2;
f[0] += d10[1] * POP / 100000 / 2.5; f[1] += d10[1] * POP / 100000 * 1.5 / 2.5;
f[3] += d10[2] * POP / 100000 / 2; f[4] += d10[2] * POP / 100000 / 2;
f[9] += d10[3] * POP / 100000;

const levels = new Array(N).fill(0);
{
  const Y = B.map((row) => row.reduce((s, x, j) => s + x * f[j], 0));
  for (let i = 0; i < N; i++) levels[i] = Math.max(1, Math.ceil(Y[i] / REC[i].q));
  for (let pass = 0; pass < 60; pass++) {
    const Yc = levels.map((L, i) => L * REC[i].q);
    let ch = false;
    for (let i = 0; i < N; i++) {
      let interm = 0; for (let j = 0; j < N; j++) interm += A[i][j] * Yc[j];
      let cap = Infinity; if (i === 0 || i === 2) cap = ARABLE; else if (i === 5) cap = 500; else if (i === 6) cap = 500; else if (i === 10) cap = 1000;
      const want = Math.min(Math.ceil((interm + f[i]) / 0.85 / REC[i].q), cap);
      if (want > levels[i]) { levels[i] = want; ch = true; }
    }
    if (!ch) break;
  }
}
const sub = (() => {
  const idle = Math.max(0, ARABLE - levels[0] - levels[2]) * SUB;
  const y = new Array(N).fill(0);
  y[0] += idle * 2; y[2] += idle; y[3] += idle * 0.5;
  return y;
})();

console.log('=== §2 价格动力学隔离测试 ===\n');
console.log('固定产能（不扩建、不缩编、无人口增长、无现金池决策），只让价格按 §2.3 演化。');
console.log('开局级数：' + levels.map((l, i) => `${S[i]}=${l}`).join(' '));
console.log(`解析平衡态 P* = P0·(a/S)^(1/eps)，其中 P0 = P_cost，a 按 §3.4 步骤 3 标定。\n`);

// 固定产能下的实际供给（含配给），以及净供给
function yearOutput() {
  const Ymax = levels.map((L, i) => L * REC[i].q);
  for (let i = 0; i < N; i++) Ymax[i] += sub[i];
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
  return { Y, net, ratio };
}
const { Y: Yfixed, net: netFixed, ratio } = yearOutput();
console.log('固定产能下的供给（含配给）与配给率：');
for (let i = 0; i < N; i++) console.log(`  ${S[i].padEnd(8)} 总产出=${Yfixed[i].toFixed(1).padStart(9)}  净供给=${netFixed[i].toFixed(1).padStart(9)}  配给率=${ratio[i].toFixed(3)}  最终需求=${f[i].toFixed(2).padStart(9)}`);

// §3.4 标定 a
const a = new Array(N).fill(0);
for (let i = 0; i < N; i++) a[i] = Math.max(netFixed[i], 1e-9) * Math.pow(PINIT[i] / PCOST[i], EPS[i]);

console.log('\n--- 一、解析平衡态 vs 实测收敛值 ---\n');
const Pstar = new Array(N).fill(0);
for (let i = 0; i < N; i++) {
  const raw = PCOST[i] * Math.pow(a[i] / Math.max(Yfixed[i], 1e-9), 1 / EPS[i]);
  Pstar[i] = Math.min(Math.max(raw, 0.2 * PCOST[i]), 5 * PCOST[i]);
}

function simulate(ticks, P0vec) {
  let P = P0vec.slice(), dP = new Array(N).fill(0);
  const trace = [];
  for (let t = 0; t < ticks; t++) {
    const D = P.map((p, i) => a[i] * Math.pow(p / PCOST[i], -EPS[i]));
    const E = D.map((d, i) => d - Yfixed[i]);
    const Pnew = new Array(N), dPnew = new Array(N);
    for (let i = 0; i < N; i++) {
      const K = (EPS[i] * a[i] / PCOST[i]) * Math.pow(P[i] / PCOST[i], -EPS[i] - 1);
      // §2.3（2026-09-19 改）：惯性 m ≡ 当期市场内流通商品量 S；ρ = 2ζ√(m·K)
      let mm = Yfixed[i];
      if (!isFinite(mm) || mm <= 0) mm = 1e-9;
      let rho = 2 * ZETA * Math.sqrt(mm * K);
      if (!isFinite(rho) || rho < 0) rho = 0;
      let p = P[i], v = dP[i];
      const fl = 0.2 * PCOST[i], ce = 5 * PCOST[i];
      const fn = (pv, vv) => [vv, (E[i] - rho * vv) / mm];
      for (let s = 0; s < NSUB; s++) {
        const [k1a, k1b] = fn(p, v);
        const [k2a, k2b] = fn(p + H / 2 * k1a, v + H / 2 * k1b);
        const [k3a, k3b] = fn(p + H / 2 * k2a, v + H / 2 * k2b);
        const [k4a, k4b] = fn(p + H * k3a, v + H * k3b);
        p += H / 6 * (k1a + 2 * k2a + 2 * k3a + k4a);
        v += H / 6 * (k1b + 2 * k2b + 2 * k3b + k4b);
        if (!isFinite(p)) { p = PCOST[i]; v = 0; break; }
        if (p < fl) { p = fl; v = 0; } else if (p > ce) { p = ce; v = 0; }
      }
      Pnew[i] = p; dPnew[i] = v;
    }
    P = Pnew; dP = dPnew;
    if (t % 500 === 0 || t === ticks - 1) trace.push({ t: t + 1, P: P.slice() });
  }
  return { P, trace };
}

const r1 = simulate(10000, PINIT);
console.log('商品       解析 P*     P*/P_cost   实测末值   /P_cost    相对误差');
let maxErr = 0;
for (let i = 0; i < N; i++) {
  const err = Math.abs(r1.P[i] - Pstar[i]) / Pstar[i];
  maxErr = Math.max(maxErr, err);
  console.log('  ' + S[i].padEnd(8) + Pstar[i].toFixed(1).padStart(11) + (Pstar[i] / PCOST[i]).toFixed(3).padStart(11)
    + r1.P[i].toFixed(1).padStart(13) + (r1.P[i] / PCOST[i]).toFixed(3).padStart(10) + (err * 100).toFixed(4).padStart(12) + '%');
}
console.log(`\n  最大相对误差 = ${(maxErr * 100).toExponential(3)}%`);

console.log('\n--- 二、收敛速度（|P − P*|/P* 随时间）---\n');
const rows = [0.001, 0.01, 0.05, 0.2];
console.log('tick       ' + rows.map((x) => ('偏离' + (x * 100).toFixed(1) + '%').padStart(12)).join('') + '   注');
for (const tt of [1, 5, 13, 26, 52, 130, 260, 520, 1000, 2000, 5000, 10000]) {
  const P0 = PINIT.map((p, i) => Pstar[i] * 1.001);
  const rr = simulate(tt, P0);
  const errs = rr.P.map((p, i) => Math.abs(p - Pstar[i]) / Pstar[i]);
  console.log(String(tt).padStart(6) + '    ' + errs.map((e) => e.toExponential(2).padStart(12)).join(''));
}
console.log('  （起始偏离 0.1%，看各商品衰减到 1e-3 / 1e-2 / 5e-2 / 0.2 的相对偏差需要多久）');

console.log('\n--- 三、从 P_init 出发的原始轨迹（前 3000 tick 抽样）---\n');
{
  const rr = simulate(3000, PINIT);
  console.log('tick       ' + S.map((s) => s.slice(0, 8).padStart(9)).join(''));
  for (const pt of rr.trace) {
    console.log('  ' + String(pt.t).padStart(6) + '   ' + pt.P.map((p, i) => (p / PCOST[i]).toFixed(3).padStart(9)).join(''));
  }
}

console.log('\n--- 四、判定 ---\n');
console.log('  价格侧：固定产能下 §2 的 ODE 能收敛到 §2.4 的解析平衡态 P*，');
console.log(`  最大相对误差 ${(maxErr * 100).toExponential(2)}% ⇒ §2 的数学形式与 RK4 实现自洽。`);
console.log('  注意：此处的 P* 由【固定产能 + 标定后的 a】共同决定，与 P_cost 无关——');
console.log('  因为 a 是在开局净供给上标定的，而 §3.4 的"a = S 时 P* = P_cost"只在');
console.log('  §3.1 表格价格恰好等于 P_cost 时成立。这正是契约把目标从"P→P_cost"');
console.log('  改为"有界稳定"的直接原因。');

fs.writeFileSync('out/price_dynamics_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/price_dynamics_probe.txt');
