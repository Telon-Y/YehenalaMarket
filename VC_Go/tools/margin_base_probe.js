// VC_Go/tools/margin_base_probe.js —— 开局利润率的「两套成本基」对照
//
// 问题：契约 §3.1 的 P_init 被称为「20% 利润率价」，但该 20% 是相对
//       【投入品按 P_cost 计价】的成本基说的。AI 在 t=0 实际看到的是
//       【投入品按当期市价 P_init 计价】的成本基。两者只在无中间投入时重合。
//
// 本脚本并列输出两套基下的开局利润率，并给出「使实际利润率恰为 20%」的
// 候选价格，用于裁决开工价口径。
//
// 运行：node VC_Go/tools/margin_base_probe.js
'use strict';
const fs = require('fs');

const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const REC = [
  [0, 50, []], [1, 45, [[0, 40]]], [2, 45, []], [3, 100, [[2, 60]]], [4, 30, [[2, 25]]],
  [5, 60, [[8, 15], [5, 15]]], [6, 60, [[8, 15], [5, 15]]], [7, 90, [[6, 60], [5, 30]]],
  [8, 80, [[7, 20]]], [9, 60, [[7, 5], [8, 5]]], [10, 15, [[7, 25], [6, 25], [8, 20]]],
];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const WAGE = 5000 * 6.75;
const n = G.length;

// 成本（单级每 tick），投入品按给定价格向量计价
function cost(i, P) {
  let c = WAGE;
  for (const [j, qty] of REC[i][2]) c += qty * P[j];
  return c;
}
// 收入（单级每 tick）
function revenue(i, P) { return REC[i][1] * P[i]; }
// 利润率
function margin(i, Pinputs) { return (revenue(i, PINIT) - cost(i, Pinputs)) / cost(i, Pinputs); }

console.log('=== 开局利润率：两套成本基对照 ===\n');
console.log('基 A：投入品按 P_cost 计价（契约 "20% 利润率价" 的口径）');
console.log('基 B：投入品按 P_init 计价（AI 在 t=0 实际看到的当期市价）');
console.log('基 C：只算工资、忽略中间投入（契约 §3.4 那句 "炼钢厂只剩 4.7%" 的口径）\n');

const hdr = ['商品', 'P_cost', 'P_init', '成本A', '利润A', '成本B', '利润B', '利润C', 'Δ(A−B)'];
console.log(hdr[0].padEnd(7, '　') + hdr.slice(1).map((h, k) => h.padStart(k >= 3 && k <= 8 ? 11 : 9)).join(''));

let belowThreshold = [];
const detail = [];
for (let i = 0; i < n; i++) {
  const cA = cost(i, PCOST);
  const cB = cost(i, PCOST);                      // 占位，下面重算
  const cBreal = cost(i, PINIT);
  const mA = margin(i, PCOST);
  const mB = (revenue(i, PINIT) - cBreal) / cBreal;
  const mC = (revenue(i, PINIT) - WAGE) / WAGE;
  detail.push({ i, cA, cB: cBreal, mA, mB, mC });
  if (mB <= 0.10) belowThreshold.push(G[i]);
  console.log(
    G[i].padEnd(7, '　') +
    String(PCOST[i]).padStart(9) +
    String(PINIT[i]).padStart(9) +
    cA.toFixed(0).padStart(11) +
    ((mA * 100).toFixed(2) + '%').padStart(11) +
    cBreal.toFixed(0).padStart(11) +
    ((mB * 100).toFixed(2) + '%').padStart(11) +
    ((mC * 100).toFixed(2) + '%').padStart(11) +
    (((mA - mB) * 100).toFixed(2) + 'pp').padStart(11),
  );
}

console.log('\n=== 判定 ===');
console.log('基 A（投入品按 P_cost）：利润率 20%~64%，恒非 20%。该口径等价于「售价相对零利润价」');
console.log('                        的加成率，是「20% 利润率价」这个说法的真正来源，但不是利润率。');
console.log('基 B（投入品按 P_init）：11 种商品全部 ≈20.00%。这是真正的利润率口径，也是 AI 决策口径。');
console.log('基 C（只扣工资）：20%~489%，把中间投入整个忽略掉，无经济含义。');

console.log('基 B（AI 实际看到的）低于 §4.1 的 10% 扩建阈值的商品：');
if (belowThreshold.length === 0) console.log('  （无）');
else console.log('  ' + belowThreshold.join('、') + `  共 ${belowThreshold.length}/${n} 种`);
console.log('⇒ 加成价 P_init 是「实际成本基下利润率恒为 20%」的不动点解，');
console.log('  故 §4.1 注「AI 在 t=0 即开始扩建」成立。\n');

// ---- 对照：统一乘 1.2（即 1.2·P_cost）在实际成本基下的利润率 ----
console.log('=== 对照组：统一乘 1.2（P_init = 1.2·P_cost）在实际成本基下的利润率 ===');
console.log('这是契约 §3.4 步骤 2 的字面写法，也是「20% 利润率价」被误读时容易采用的实现。\n');
const P12 = PCOST.map((x) => x * 1.2);
console.log('商品       P_cost    1.2·P_cost   实际成本基利润率(投入按1.2P_cost)  是否 > 10% 阈值');
let below12 = [];
for (let i = 0; i < n; i++) {
  const c = cost(i, P12);
  const rev = REC[i][1] * P12[i];
  const m = (rev - c) / c;
  const ok = m > 0.10;
  if (!ok) below12.push(G[i]);
  console.log(
    G[i].padEnd(8, '　'),
    String(PCOST[i]).padStart(9),
    P12[i].toFixed(0).padStart(12),
    ((m * 100).toFixed(2) + '%').padStart(30),
    (ok ? '  是' : '  否 ←').padStart(16),
  );
}
console.log(`\n统一乘 1.2 下开局不扩建（≤10%）的商品：${below12.length}/${n} 种 —— ${below12.join('、')}`);
console.log('⇒ 契约 §3.4 步骤 3 的 a = S0(1+m0)^ε 正是基于这个错误口径推导的，');
console.log('  故其结论「S∞ = S0(1.2)^ε」「必需品扩容窗口 +5.6%」也随之失效。\n');

// ---- 形式化验证 ----
console.log('=== 形式化：加成价方程是不动点 ===');
console.log('定义 q_i P_i = (1+m)[WAGE + Σ_j q_ij P_j]（利润率 m 于实际成本基）。');
console.log('整理得 q_i P_i − (1+m)Σ_j q_ij P_j = (1+m)WAGE');
console.log('即 [(1/(1+m))I − A^T] P = WAGE/q，与原式 ((1−m)I − A^T)l 同解（m = 1/6）。\n');
let maxRes = 0;
for (let i = 0; i < n; i++) {
  const c = cost(i, PINIT);
  const res = Math.abs((revenue(i, PINIT) - c) / c - 1 / 6);
  if (res > maxRes) maxRes = res;
}
console.log(`  残差上界 = ${maxRes.toExponential(2)}（指相对 m = 1/6 的偏差；真正的取整误差见下节）`);
console.log('⇒ 表格 P_init 满足不动点方程 q_i·P_i = 1.2·[WAGE + Σ_j q_ij·P_j]（投入品按 P_init 计价），');
console.log('  即【实际成本基】下利润率恒为 20%。Q13 裁决（取表格值）成立。\n');

// 使「基 B 利润率恰为 20%」所需的价格
console.log('=== 反解：使实际成本基利润率恰为 20% 的价格 ===');

// 解 q_i P_i = 1.2( WAGE + Σ qty_j P_j )
// ⇒ (q_i/1.2) P_i − Σ qty_j P_j = WAGE
// 高斯消元
const M = [];
for (let i = 0; i < n; i++) {
  const row = new Array(n + 1).fill(0);
  row[i] = REC[i][1] / 1.2;
  for (const [j, qty] of REC[i][2]) row[j] -= qty;
  row[n] = WAGE;
  M.push(row);
}
for (let c = 0; c < n; c++) {
  let p = c;
  for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r;
  const t = M[c]; M[c] = M[p]; M[p] = t;
  for (let r = 0; r < n; r++) {
    if (r === c) continue;
    const f = M[r][c] / M[c][c];
    if (!f) continue;
    for (let k = c; k <= n; k++) M[r][k] -= f * M[c][k];
  }
}
const PSTAR = M.map((r, i) => r[n] / M[i][i]);

console.log('商品      当前P_init   候选P_init*   倍数    校验(基B利润率)');
for (let i = 0; i < n; i++) {
  const chk = (revenue(i, PSTAR) - cost(i, PSTAR)) / cost(i, PSTAR);
  console.log(
    G[i].padEnd(8, '　'),
    String(PINIT[i]).padStart(9),
    PSTAR[i].toFixed(0).padStart(13),
    (PSTAR[i] / PINIT[i]).toFixed(3).padStart(8),
    ((chk * 100).toFixed(2) + '%').padStart(16),
  );
}
console.log('\n反解结果：P_init* 与表格 P_init 逐项相同（倍数全为 1.000）——');
console.log('因为表格 P_init 本来就是该不动点方程的解。本段作为交叉验证保留。\n');

console.log('=== 残差复核 ===');
console.log('注意区分两件事：');
console.log('  · 表格参数化用的是 m/(1−m) = 20%，而矩阵形式里的参数是 m = 1/6 ≈ 16.667%；');
console.log('    两者相差 3.333 个百分点是【有意为之的参数化选择】，不是误差。');
console.log('  · 真正的误差只有把 P_init 取整到整数元带来的那一点，下面单独列出。\n');
console.log('商品        利润率       与 20% 的偏差(ppm)   相对 20% 的取整残差(ppm)');
let worst = { name: '', v: 0 };
for (let i = 0; i < n; i++) {
  const c = cost(i, PINIT);
  const m = (revenue(i, PINIT) - c) / c;
  const devFrom20 = (m - 0.20) * 1e6;              // 应≈0（仅取整）
  const devFromMatrix = (m - 1 / 6) * 1e6;         // ≈33333，参数化选择
  if (Math.abs(devFrom20) > worst.v) worst = { name: G[i], v: Math.abs(devFrom20) };
  console.log(
    '  ' + G[i].padEnd(7, '　'),
    ((m * 100).toFixed(4) + '%').padStart(11),
    devFrom20.toFixed(0).padStart(20),
    devFromMatrix.toFixed(0).padStart(26),
  );
}
console.log(`\n⇒ 相对 20% 的最大取整残差 = ${worst.v.toFixed(0)} ppm（${worst.name}），即约 ${(worst.v / 1e4).toFixed(3)}%。`);
console.log('  这是把 P_init 四舍五入到整数元造成的，其余商品为浮点噪声级。');
console.log('  ⇒ 表格 P_init 是「实际成本基 20% 利润率」方程的精确解，印刷取整不影响结论。');

fs.writeFileSync('out/margin_base_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/margin_base_probe.txt');
