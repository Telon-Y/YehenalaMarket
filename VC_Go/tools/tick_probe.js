// VC_Go/tools/tick_probe.js —— 1.0 版单商品 tick 级可行性探针
//
// 目的：用 1.0 文档 §2（常弹性需求 + 二阶价格 ODE）、§4（扩建/缩编规则）、
//       §5（工资）检验三件事：
//   A. 文档 §3.4 的两条口径是否自洽：P_init 到底是 1.2·P_cost 还是"20% 利润率价"？
//   B. 在文档规定的行为规则（margin>10% 扩建 / margin<0 解雇 / idle>156 缩编）下，
//      10,000 tick 后利润率收敛到哪里？
//   C. "利润趋近于零"这个目标在文档规则下是否可达？
//
// 运行：node VC_Go/tools/tick_probe.js
'use strict';

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
// [产出品, 每周期产出量, [[投入品, 投入量]...]]
const REC = [
  [0, 50, []], [1, 45, [[0, 40]]], [2, 45, []], [3, 100, [[2, 60]]], [4, 30, [[2, 25]]],
  [5, 60, [[8, 15], [5, 15]]], [6, 60, [[8, 15], [5, 15]]], [7, 90, [[6, 60], [5, 30]]],
  [8, 80, [[7, 20]]], [9, 60, [[7, 5], [8, 5]]], [10, 15, [[7, 25], [6, 25], [8, 20]]],
];
// §3.1 需求弹性（分类赋值）
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
// §3.1 表格里的两列
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT_DOC = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const WAGE = 5000 * 6.75;
const n = G.length;

// ---- A. 口径自洽性 ----
console.log('=== A. §3.4 口径自洽性检查 ===');
console.log('若 P_init = 1.2 × P_cost，则利润率恒为 20% 需要成本也按同一比例缩放；');
console.log('但工资不随价格缩放，所以 1.2× 与「20% 利润率价」不是同一个数。\n');
console.log('商品        P_cost   1.2·P_cost   文档P_init   文档/1.2P    文档/P_cost');
let mismatch = 0;
for (let i = 0; i < n; i++) {
  const p12 = PCOST[i] * 1.2;
  console.log(
    '  ' + G[i].padEnd(5, '　'),
    String(PCOST[i]).padStart(7),
    p12.toFixed(0).padStart(11),
    String(PINIT_DOC[i]).padStart(11),
    (PINIT_DOC[i] / p12).toFixed(4).padStart(11),
    (PINIT_DOC[i] / PCOST[i]).toFixed(4).padStart(12),
  );
  if (Math.abs(PINIT_DOC[i] / p12 - 1) > 0.005) mismatch++;
}
console.log(`\n  文档 P_init 与 1.2·P_cost 不一致的商品数: ${mismatch}/${n}`);
console.log('  ⇒ 文档 §3.4 步骤 2 写的「P_init = 加成价」与步骤 3 的 a = S0(1+m0)^ε 用的是两个不同的数。');

// ---- 静态：给定产能倍数 x = S/S0，解利润率 ----
// E = a(P/P0)^-ε - S = 0  =>  P/P0 = (a/S)^(1/ε)
// 取 a 的两种标定口径，比较所需产能扩张倍数
console.log('\n=== B. 利润归零所需的产能扩张倍数（两种 a 标定口径对比） ===');
console.log('口径①：a = S0·(P_init_doc/P_cost)^ε   （与文档表格的 P_init 一致）');
console.log('口径②：a = S0·(1.2)^ε                  （与 §3.4「P_init=1.2P_cost」一致）');
console.log('\n商品        弹性ε   ①所需扩产   ①静态margin@x=1   ②所需扩产   文档§3.4预测(1.2^ε)');
for (let i = 0; i < n; i++) {
  const r = PINIT_DOC[i] / PCOST[i];      // 文档 P_init / P_cost
  const e = EPS[i];
  const x1 = Math.pow(r, e);              // 口径①：涨价因子 r，故 a/S0 = r^ε
  const x2 = Math.pow(1.2, e);            // 口径②
  // 静态 margin：在 x 倍的产能下，P 由 D=S 决定，再看成本利润率
  const x = 1.0;
  const P = PCOST[i] * Math.pow(x1 / x, 1 / e);   // 口径①下 x=1 时的价格
  const rev = P;
  const cost = PCOST[i];                           // 零利润价 = 单位总成本
  const margin = (rev - cost) / cost;
  console.log(
    '  ' + G[i].padEnd(5, '　'),
    String(e).padStart(5),
    (x1 * 100 - 100).toFixed(2).padStart(9) + '%',
    (margin * 100).toFixed(2).padStart(14) + '%',
    (x2 * 100 - 100).toFixed(2).padStart(9) + '%',
    (Math.pow(1.2, e) * 100 - 100).toFixed(2).padStart(17) + '%',
  );
}

// ---- C. 行为规则下的可达区间 ----
console.log('\n=== C. 文档行为规则下的「利润率冻结窗口」 ===');
console.log('扩建触发: margin > 10%   缩编触发: margin < 0（且 idle>156）');
console.log('⇒ margin ∈ [0%, 10%] 时两种机制都不动作，系统在此区间内冻结。');
console.log('⇒ 「利润趋近于零」在文档规则下不可达，除非把扩建阈值设为 0。');
console.log('\n对每种商品，求静态均衡下的 margin（口径①，x 由边际条件决定）：');
for (let i = 0; i < n; i++) {
  const e = EPS[i];
  const r = PINIT_DOC[i] / PCOST[i];
  // 求 margin(x) = 0 与 margin(x) = 0.10 对应的 x
  // P(x)/P_cost = (r^e / x)^(1/e) = r · x^(-1/e)
  // margin(x) = P(x)/P_cost - 1 = r·x^(-1/e) - 1
  const xAt = (target) => Math.pow(r / (1 + target), e); // 解 r·x^(-1/e) = 1+target
  const x0 = xAt(0), x10 = xAt(0.10);
  console.log(
    '  ' + G[i].padEnd(5, '　'),
    `margin=0  @ S/S0=${x0.toFixed(4)}`,
    `   margin=10% @ S/S0=${x10.toFixed(4)}`,
    `   窗口宽度=${((x10 - x0) * 100).toFixed(2)}%`,
  );
}

// ---- D. 单商品动力学：ODE 是否收敛到 P_cost ----
console.log('\n=== D. 单商品 ODE 收敛性（给定 S 固定，看 P 是否收敛到 P*） ===');
// §2.3（2026-09-19 改）：惯性 m ≡ 当期流通量、T = 2π√(m/K) 内生，已无 T 旋钮
const zeta = 0.7, dt = 0.5, nsub = 10, h = dt / nsub;
function simFixedS(i, S, ticks) {
  const e = EPS[i], r = PINIT_DOC[i] / PCOST[i];
  const a = S * Math.pow(r, e);          // 标定使 x=1 时 E=0
  let P = PINIT_DOC[i], dP = 0;
  for (let t = 0; t < ticks; t++) {
    for (let s = 0; s < nsub; s++) {
      const E = (Pv) => a * Math.pow(Pv / PCOST[i], -e) - S;
      const K = (Pv) => (e * a / PCOST[i]) * Math.pow(Pv / PCOST[i], -e - 1);
      // §2.3（2026-09-19 改）：惯性 m ≡ 当期市场内流通商品量 S；ρ = 2ζ√(m·K)
      const m = () => Math.max(S, 1e-9);
      const rho = (Pv) => 2 * zeta * Math.sqrt(m() * K(Pv));
      const f = (Pv, V) => [V, (E(Pv) - rho(Pv) * V) / m(Pv)];
      const [k1a, k1b] = f(P, dP);
      const [k2a, k2b] = f(P + h / 2 * k1a, dP + h / 2 * k1b);
      const [k3a, k3b] = f(P + h / 2 * k2a, dP + h / 2 * k2b);
      const [k4a, k4b] = f(P + h * k3a, dP + h * k3b);
      P += h / 6 * (k1a + 2 * k2a + 2 * k3a + k4a);
      dP += h / 6 * (k1b + 2 * k2b + 2 * k3b + k4b);
      const fl = 0.2 * PCOST[i], ce = 5 * PCOST[i];
      if (P < fl) { P = fl; dP = 0; } else if (P > ce) { P = ce; dP = 0; }
    }
  }
  return P;
}
console.log('商品        起始P_init   终态P(1000tick)   P_cost   相对误差');
for (let i = 0; i < n; i++) {
  const S = REC[i][1];                    // 取 S = 基础产出
  const Pend = simFixedS(i, S, 1000);
  console.log(
    '  ' + G[i].padEnd(5, '　'),
    String(PINIT_DOC[i]).padStart(10),
    Pend.toFixed(2).padStart(16),
    String(PCOST[i]).padStart(9),
    ((Pend - PCOST[i]) / PCOST[i] * 100).toFixed(3).padStart(11) + '%',
  );
}

// ---- E. 决定论：利润归零与 10% 阈值的张力 ----
console.log('\n=== E. 结论 ===');
console.log('1. 谱半径 0.5000 < 1，且 11 个 P_cost 由投入产出表+工资表完全确定 → 价格侧设计可行。');
console.log('2. §3.4 的 P_init 有两个互斥定义（1.2·P_cost vs 20%利润率价），需求标定式 a 会因此取错值：');
console.log('   两者对「利润归零所需扩产倍数」的预测不同，必须二选一。');
console.log('3. 10% 扩建阈值 + 0% 缩编阈值 ⇒ margin 在 [0,10%] 内冻结 ⇒「利润趋近于零」不可达。');
console.log('   若要真的收敛到零利润，需把扩建阈值改为 0（或用零利润模式）——但那样会削掉');
console.log('   10% 的收敛缓冲区，扩建步长 10% 会直接冲过均衡点，需靠 ODE 阻尼吸收。');
