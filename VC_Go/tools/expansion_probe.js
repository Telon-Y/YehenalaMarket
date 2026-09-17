// VC_Go/tools/expansion_probe.js —— 扩建规则的收敛性探针
//
// 检验 1.0 文档 §4.1「利润率 > 10% 时自动扩建」与 §8「10,000 周期后各建筑利润率
// 趋近于零」是否相容。
//
// 模型（逐 tick）：
//   - 需求按 §2.1 常弹性 D = a (P/P0)^-ε，a 按 §3.4 步骤 3 标定，使 P_init 处 E = 0
//   - 价格按 §2.4 平衡态 P* = P0 (a/S)^(1/ε) 出清（先剥离 ODE 振荡，单看结构收敛性）
//   - 利润率按 §3.4 口径：收入用当期价、成本用投入品当期价 + 工资
//   - 扩建按 §4.1：margin > threshold 时，n_extra = floor((margin-threshold)/5%)+1，
//     单次扩建量 = min(n_extra × 1 × 10% × level, level × 10%)
//
// 运行：node VC_Go/tools/expansion_probe.js
'use strict';
const fs = require('fs');

// 输出既进控制台，也写文件（避开 shell 重定向）
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const REC = [
  [0, 50, []], [1, 45, [[0, 40]]], [2, 45, []], [3, 100, [[2, 60]]], [4, 30, [[2, 25]]],
  [5, 60, [[8, 15], [5, 15]]], [6, 60, [[8, 15], [5, 15]]], [7, 90, [[6, 60], [5, 30]]],
  [8, 80, [[7, 20]]], [9, 60, [[7, 5], [8, 5]]], [10, 15, [[7, 25], [6, 25], [8, 20]]],
];
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const WAGE = 5000 * 6.75;
const n = G.length;

// a 的两种标定口径，都保证 t=0 时 E = 0
//   'doc'  : 用 §3.1 表格的 P_init（= 20% 利润率价），a = S0·(P_init/P_cost)^ε
//   'p12'  : 用 §3.4 步骤 2 的字面定义 P_init = 1.2·P_cost，a = S0·1.2^ε
function calibA(mode) {
  const a = new Array(n);
  for (let i = 0; i < n; i++) {
    const r = mode === 'doc' ? PINIT[i] / PCOST[i] : 1.2;
    a[i] = Math.pow(r, EPS[i]);   // 以 S0 = 1 为基准单位
  }
  return a;
}
let a = calibA('doc');

// 给定各商品产量 S（以 S0=1 为单位），解出清价
function prices(S) {
  const P = new Array(n);
  for (let i = 0; i < n; i++) {
    P[i] = PCOST[i] * Math.pow(a[i] / S[i], 1 / EPS[i]);
    const fl = 0.2 * PCOST[i];
    if (P[i] < fl) P[i] = fl;
  }
  return P;
}

// 利润率（单级口径，与级数无关）：收入用当期价，成本用投入品当期价 + 工资
function margins(P) {
  const mg = new Array(n);
  for (const [out, q, inp] of REC) {
    const rev = q * P[out];
    const cost = inp.reduce((s, [i, qty]) => s + qty * P[i], 0) + WAGE;
    mg[out] = (rev - cost) / cost;
  }
  return mg;
}

function run(label, threshold, ticks) {
  let S = new Array(n).fill(1.0);
  let last = null;
  for (let t = 0; t < ticks; t++) {
    const P = prices(S);
    const mg = margins(P);
    last = { P, mg, S: S.slice() };
    // 扩建
    const Sn = S.slice();
    for (let i = 0; i < n; i++) {
      if (mg[i] > threshold) {
        const extra = Math.floor((mg[i] - threshold) / 0.05) + 1;
        const add = Math.min(extra * 1 * 0.10 * S[i], 0.10 * S[i]);
        Sn[i] = S[i] + add;
      }
    }
    S = Sn;
  }
  console.log(`\n--- ${label}（threshold = ${(threshold * 100).toFixed(0)}%，${ticks} tick）---`);
  console.log('商品       终态S/S0    终态P      P/P_cost    终态margin');
  let maxAbs = 0, frozen = 0;
  for (let i = 0; i < n; i++) {
    const m = last.mg[i];
    if (Math.abs(m) > maxAbs) maxAbs = Math.abs(m);
    if (Math.abs(m - threshold) < 0.005 && threshold > 0) frozen++;
    console.log(
      '  ' + G[i].padEnd(5, '　'),
      last.S[i].toFixed(4).padStart(10),
      last.P[i].toFixed(1).padStart(10),
      (last.P[i] / PCOST[i]).toFixed(4).padStart(11),
      (m * 100).toFixed(3).padStart(12) + '%',
    );
  }
  console.log(`  max|margin| = ${(maxAbs * 100).toFixed(3)}%`);
  if (threshold > 0) {
    console.log(`  冻结在 threshold 的商品数 = ${frozen}/${n}`);
  }
  return { last, maxAbs };
}

console.log('=== 扩建规则收敛性检验 ===');
console.log('a 按 §3.4 步骤 3 标定（P_init 处 E = 0），价格按 §2.4 平衡态出清。');
console.log('如果 §4.1 的 10% 阈值可行，10,000 tick 后 margin 应趋近 0；');
console.log('如果阈值成为利润下限，margin 会冻结在 10%。');

run('情形 A：文档原规则（P_init 用表格值）', 0.10, 10000);
run('情形 B：零利润模式（阈值 0）', 0.0, 10000);
a = calibA('p12');
run('情形 C：文档原规则，但 P_init = 1.2·P_cost（口径②）', 0.10, 10000);

// ---- 逐 tick 追踪，确认动力学到底怎么走 ----
console.log('\n=== 逐 tick 追踪（谷物 / 情形 A）===');
a = calibA('doc');
{
  let S = new Array(n).fill(1.0);
  console.log('tick      S_grain     P_grain    margin_grain   动作');
  for (let t = 0; t < 12; t++) {
    const P = prices(S);
    const mg = margins(P);
    const act = mg[0] > 0.10 ? `扩建 +${(Math.min((Math.floor((mg[0] - 0.10) / 0.05) + 1) * 0.10 * S[0], 0.10 * S[0])).toFixed(3)}` : '不动';
    console.log(
      String(t).padStart(4),
      S[0].toFixed(4).padStart(11),
      P[0].toFixed(2).padStart(11),
      (mg[0] * 100).toFixed(3).padStart(12) + '%',
      '   ' + act,
    );
    const Sn = S.slice();
    for (let i = 0; i < n; i++) {
      if (mg[i] > 0.10) {
        const extra = Math.floor((mg[i] - 0.10) / 0.05) + 1;
        Sn[i] = S[i] + Math.min(extra * 1 * 0.10 * S[i], 0.10 * S[i]);
      }
    }
    S = Sn;
  }
}

console.log('\n=== margin 随 S/S0 的变化（谷物，全价联动）===');
a = calibA('doc');
console.log(' S/S0      P_grain    margin');
for (const x of [1.0, 1.02, 1.05, 1.08, 1.1, 1.12, 1.15, 1.2, 1.3, 1.4]) {
  const S = new Array(n).fill(1.0); S[0] = x;
  const P = prices(S);
  const mg = margins(P);
  console.log(x.toFixed(3).padStart(6), P[0].toFixed(2).padStart(11), (mg[0] * 100).toFixed(3).padStart(10) + '%');
}

fs.writeFileSync('out/expansion_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('已写入 out/expansion_probe.txt');

// ============================================================
// F. 完整单商品模拟：含 雇佣率 / 解雇 / 缩编 / ODE 价格动态
//    检验 §8「10,000 周期后利润率趋近于零」在文档规则下是否成立
// ============================================================
const T_PERIOD = 26, ZETA = 0.7, DT = 0.5, NSUB = 10, H = DT / NSUB;

function fullSim(i, opts) {
  const e = EPS[i], P0 = PCOST[i], r = PINIT[i] / P0;
  const a = Math.pow(r, e);                       // S0 = 1 基准
  const S0base = REC[i][1];                        // 基础单级产出
  const WAGE1 = WAGE;                              // 单级工资 33,750
  const hireStep = (opts.hireAnnual || 0.05) / 52; // 年率 5% → 每 tick
  const decayRate = (opts.decayAnnual || 0.05) / 52;
  const decayWindow = opts.decayWindow || 156;
  const expandTh = opts.expandTh || 0.10;

  let S = 1.0, hire = 1.0, idle = 0;
  let P = PINIT[i], dP = 0;
  const eps = 1e-9;
  const xOf = (Sv) => Math.max(Sv, eps);           // 总产出（S0=1 单位）
  const priceOf = (Sv) => {
    const p = P0 * Math.pow(a / xOf(Sv), 1 / e);
    return Math.max(p, 0.2 * P0);
  };
  const marginOf = (Sv, Pcur) => {
    const q = S0base;                              // 单级产出
    const rev = q * Pcur;
    let cost = WAGE1;
    for (const [j, qty] of REC[i][2]) cost += qty * Pcur; // 简化：投入品按本商品价格同比例
    return (rev - cost) / cost;
  };

  const hist = { S: [], margin: [], P: [] };
  for (let t = 0; t < (opts.ticks || 10000); t++) {
    // --- 1. 雇佣调整（用上一 tick 的 margin）---
    const mgPrev = hist.margin.length ? hist.margin[hist.margin.length - 1] : 0.2;
    if (mgPrev < 0) hire = Math.max(0, hire - hireStep);
    else if (mgPrev > 0) hire = Math.min(1, hire + hireStep);

    // --- 2. 实际产出 ---
    S = xOf(S) * 1.0;                              // level 倍数保持
    const Y = S * hire;                            // 实际产量（S0=1 单位）
    // --- 3. 价格 ODE（工作点用当期价格）---
    const E = a * Math.pow(P / P0, -e) - Y;
    const K = (e * a / P0) * Math.pow(P / P0, -e - 1);
    const m = (K * T_PERIOD * T_PERIOD) / (4 * Math.PI * Math.PI);
    const rho = (ZETA * K * T_PERIOD) / Math.PI;
    const f = (Pv, V) => [V, (E - rho * V) / m];
    for (let s = 0; s < NSUB; s++) {
      const [k1a, k1b] = f(P, dP);
      const [k2a, k2b] = f(P + H / 2 * k1a, dP + H / 2 * k1b);
      const [k3a, k3b] = f(P + H / 2 * k2a, dP + H / 2 * k2b);
      const [k4a, k4b] = f(P + H * k3a, dP + H * k3b);
      P += H / 6 * (k1a + 2 * k2a + 2 * k3a + k4a);
      dP += H / 6 * (k1b + 2 * k2b + 2 * k3b + k4b);
      const fl = 0.2 * P0, ce = 5 * P0;
      if (P < fl) { P = fl; dP = 0; } else if (P > ce) { P = ce; dP = 0; }
    }
    // --- 4. 利润率结算 ---
    const rev = S0base * S * hire * P;
    let cost = WAGE1 * S * hire;
    for (const [, qty] of REC[i][2]) cost += qty * S * hire * P;
    const margin = (rev - cost) / cost;

    // --- 5. 缩编判定 ---
    if (hire < 0.75) idle++; else idle = 0;
    if (idle > decayWindow) S = Math.max(0, S * (1 - decayRate));

    // --- 6. 扩建 ---
    if (margin > expandTh) {
      const extra = Math.floor((margin - expandTh) / 0.05) + 1;
      S += Math.min(extra * opts.autoScale * S, 0.10 * S);
    }
    hist.S.push(S); hist.margin.push(margin); hist.P.push(P);
  }
  return hist;
}

console.log('\n=== F. 完整单商品模拟（含雇佣/解雇/缩编/ODE），10,000 tick ===');
try {
console.log('判据（§7.3 等价）：末 2000 tick 内 |margin| < 2% 且 价格相对波动 < 2%');
console.log('\n商品       末2000tick max|margin|   max|margin|@末值   价格波动    S/S0 范围        判定');
let pass = 0;
for (let i = 0; i < n; i++) {
  const h = fullSim(i, { ticks: 10000, autoScale: 1.0 });
  const N = h.margin.length, tail = 2000;
  let mx = 0, mxLast = 0, sMin = 1e9, sMax = -1e9, pMin = 1e9, pMax = -1e9;
  for (let t = N - tail; t < N; t++) {
    const am = Math.abs(h.margin[t]);
    if (am > mx) mx = am;
    if (t > N - 100 && am > mxLast) mxLast = am;
    if (h.S[t] < sMin) sMin = h.S[t];
    if (h.S[t] > sMax) sMax = h.S[t];
    if (h.P[t] < pMin) pMin = h.P[t];
    if (h.P[t] > pMax) pMax = h.P[t];
  }
  const pv = (pMax - pMin) / ((pMax + pMin) / 2);
  const ok = mx < 0.02 && pv < 0.02;
  if (ok) pass++;
  console.log(
    '  ' + G[i].padEnd(5, '　'),
    (mx * 100).toFixed(2).padStart(18) + '%',
    (mxLast * 100).toFixed(2).padStart(18) + '%',
    (pv * 100).toFixed(2).padStart(9) + '%',
    `${sMin.toFixed(3)}~${sMax.toFixed(3)}`.padStart(16),
    (ok ? '  收敛' : '  未收敛').padStart(8),
  );
}
console.log(`\n通过收敛判据的商品数 = ${pass}/${n}`);
console.log('注：本模拟为单商品自洽近似（投入品价格按本商品价格同比例变动），');
console.log('    用于判断「扩建步长 vs 均衡步长」这一结构性问题，不替代多商品联合模拟。');
} catch (err) {
  console.log('!! 情形 F 抛错: ' + (err && err.stack ? err.stack : err));
}

fs.writeFileSync('out/expansion_probe.txt', OUT.join('\n') + '\n', 'utf8');
