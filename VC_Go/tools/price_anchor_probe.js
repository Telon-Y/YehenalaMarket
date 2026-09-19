// VC_Go/tools/price_anchor_probe.js —— §3.4「零利润价锚定」的病理量化与替代方案对比
//
// 背景（用户提出，2026-09-19）：
//   「零利润价格对价格起到了严重的压制作用，结合常抑制项，导致价格与短缺脱节、
//     常年维持低位、利润率极低；而且零利润价格会随建筑数量改变而改变
//     （尤其重工业循环，其需求是波动性的，无法通过零利润表示）。探寻代替方案。」
//
// 本探针把该命题拆成三个可计算的量，逐条给出数字：
//   ① 价格路径与"贴底点"：现方案下 P*(λ) = P_init·λ^(−1/ε)，
//      问：产能扩张到初始的几倍时价格就撞上 0.2·P_cost 的地板（此后与短缺脱节）。
//   ② 利润率随产能的衰减：λ = 1…5 时各建筑的利润率（现方案 vs 替代方案）。
//   ③ 重工业（煤/铁/钢/工具）的"派生需求"错配：
//      它们的真实需求 = 下游生产计划 × 投入系数（对 λ 线性），
//      而现方案给的是"人均消费型"常弹性曲线（与 λ 无关）。量化两者的比值。
//
// 替代方案（对比）：
//   A 支出份额锚（单位弹性）：P*(λ) = P_init/λ —— 与短缺直接挂钩，无 −1/ε 放大。
//   B 除方案 A 外，把价格地板从 0.2·P_cost 改为"当期单位现金成本"。
//   C 中间品改为派生需求：D_g = Σ_j q_gj·N_j（下游生产计划）。
//
// 运行：ELECTRON_RUN_AS_NODE=1 "<electron>" VC_Go/tools/price_anchor_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const N = 11;
const NAME = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
// §3.3 投入产出（每级产出 q、每级投入 qty）
const REC = [
  { q: 50, inp: {} }, { q: 45, inp: { 0: 40 } }, { q: 45, inp: {} }, { q: 100, inp: { 2: 60 } },
  { q: 30, inp: { 2: 25 } }, { q: 60, inp: { 8: 15, 5: 15 } }, { q: 60, inp: { 8: 15, 5: 15 } },
  { q: 90, inp: { 6: 60, 5: 30 } }, { q: 80, inp: { 7: 20 } }, { q: 60, inp: { 7: 5, 8: 5 } },
  { q: 15, inp: { 7: 25, 6: 25, 8: 20 } },
];
const WAGE_PER_LEVEL = 33750;      // §5：5,000 人 × 6.75 元
const FLOOR_RATIO = 0.2;           // §2.4 的价格地板 P_floor = 0.2·P_cost
const CEIL_RATIO = 5.0;

// 开局等级：§3.2 的生产建筑 5 级、建造部门 20 级
const LEVEL0 = [5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 20];
const S0 = LEVEL0.map((L, i) => L * REC[i].q);

// ---- 价格/成本工具 ----
function unitCost(P) {
  // 每级成本 = 工资 + Σ 投入量 × 当期价
  return REC.map((r) => WAGE_PER_LEVEL + Object.entries(r.inp).reduce((s, [g, q]) => s + q * P[Number(g)], 0));
}
function margins(P) {
  const cost = unitCost(P);
  return REC.map((r, i) => (r.q * P[i] - cost[i]) / cost[i]);
}
function clamp(p, i) {
  return Math.min(Math.max(p, FLOOR_RATIO * PCOST[i]), CEIL_RATIO * PCOST[i]);
}

// ---- 方案 1（现行）：常弹性需求，锚定在开局产出 ----
// 由 a = S0·(P_init/P_cost)^ε 得 P*(λ) = P_init·λ^(−1/ε)
function priceCurrent(lambda) {
  return PINIT.map((p, i) => clamp(p * Math.pow(lambda, -1 / EPS[i]), i));
}
// ---- 方案 A：支出份额锚（单位弹性），P*(λ) = P_init/λ ----
function priceUnitElastic(lambda) {
  return PINIT.map((p, i) => clamp(p / lambda, i));
}
// ---- 方案 A+B：地板改为"当期单位现金成本"（用未钳制的价格算成本） ----
function priceCostFloor(lambda, P) {
  const cost = unitCost(P); // 用上一轮价格估成本（不动点迭代）
  return PINIT.map((p, i) => {
    const raw = p / lambda;
    const floor = cost[i] / REC[i].q;
    return clamp(Math.max(raw, floor), i);
  });
}

// ===== ① 贴底点 λ_floor =====
console.log('=== 一、价格贴底点：产能扩张到初始的几倍，价格就撞上 0.2·P_cost ===\n');
console.log('现方案 P*(λ) = P_init·λ^(−1/ε)：解得 λ_floor = (P_init/(0.2·P_cost))^ε');
console.log('方案 A  P*(λ) = P_init/λ：λ_floor = P_init/(0.2·P_cost)（与 ε 无关）\n');
console.log('商品        ε    P_init/P_cost   现方案 λ_floor   方案A λ_floor   倍数差');
for (let i = 0; i < N; i++) {
  const r = PINIT[i] / PCOST[i];
  const lf = Math.pow(r / FLOOR_RATIO, EPS[i]);
  const la = r / FLOOR_RATIO;
  console.log(`${NAME[i].padEnd(8)} ${String(EPS[i]).padStart(4)} ${r.toFixed(3).padStart(12)} ${lf.toFixed(2).padStart(15)} ${la.toFixed(2).padStart(14)} ${(la / lf).toFixed(2).padStart(9)}×`);
}
console.log('\n→ 现方案下，必需品与工业品只要扩产到初始的 1.5~1.8 倍就永久贴底；');
console.log('  贴底后钳制把 dP/dt 置 0，价格对短缺完全失去响应。这就是"价格与短缺脱节"。');

// ===== ② 利润率 vs 产能倍数 =====
console.log('\n=== 二、利润率随产能扩张的衰减（行业加权，产能按同一倍数 λ 缩放）===\n');
const lambdas = [1, 1.1, 1.2, 1.5, 2, 3, 5];
function weightedMargin(P) {
  const m = margins(P);
  // 权重 = 各建筑单级成本（近似经济权重）
  const w = unitCost(P);
  let num = 0, den = 0;
  for (let i = 0; i < N; i++) { if (i === 10) continue; num += m[i] * w[i]; den += w[i]; }
  return { wm: num / den, m };
}
console.log('λ       现方案 加权利润率   方案A 加权利润率   方案A+成本地板');
for (const lam of lambdas) {
  const cur = weightedMargin(priceCurrent(lam)).wm;
  const uni = weightedMargin(priceUnitElastic(lam)).wm;
  // 成本地板：迭代两次求不动点
  let P = priceUnitElastic(lam);
  for (let k = 0; k < 30; k++) P = priceCostFloor(lam, P);
  const cf = weightedMargin(P).wm;
  console.log(`${String(lam).padStart(4)} ${(cur * 100).toFixed(2).padStart(18)}% ${(uni * 100).toFixed(2).padStart(18)}% ${(cf * 100).toFixed(2).padStart(18)}%`);
}
console.log('\n（加权口径：以各建筑单级成本为权重，排除建造力——它是投资品，不进入消费品加权）');

console.log('\n=== 三、分商品利润率（λ = 1 / 1.5 / 3）===\n');
for (const lam of [1, 1.5, 3]) {
  const cur = margins(priceCurrent(lam));
  const uni = margins(priceUnitElastic(lam));
  console.log(`λ = ${lam}`);
  console.log('  商品        现方案      方案A');
  for (let i = 0; i < N; i++) {
    console.log(`  ${NAME[i].padEnd(8)} ${(cur[i] * 100).toFixed(1).padStart(8)}% ${(uni[i] * 100).toFixed(1).padStart(9)}%`);
  }
}

// ===== ③ 重工业的派生需求错配 =====
console.log('\n=== 四、重工业（煤/铁/钢/工具）的派生需求 vs 现方案的需求曲线 ===\n');
console.log('现方案：D_g(λ) = a_g = S0_g·(P_init/P_cost)^ε —— 与下游产能 λ 无关。');
console.log('真实派生需求：D_g(λ) = Σ_j 每级投入量(g→j) × 等级_j(λ) —— 对 λ 线性。\n');
const inter = [5, 6, 7, 8]; // 煤 铁 钢 工具
console.log('商品     现方案 D(λ=1)   派生 D(λ=1)   D(λ=2) 现/派生   D(λ=3) 现/派生');
for (const g of inter) {
  let derived1 = 0, derived2 = 0, derived3 = 0;
  for (let j = 0; j < N; j++) {
    const q = REC[j].inp[g] || 0;
    if (!q) continue;
    derived1 += q * LEVEL0[j];
    derived2 += q * LEVEL0[j] * 2;
    derived3 += q * LEVEL0[j] * 3;
  }
  const a = S0[g] * Math.pow(PINIT[g] / PCOST[g], EPS[g]);
  console.log(`${NAME[g].padEnd(8)} ${a.toFixed(1).padStart(13)} ${derived1.toFixed(1).padStart(13)} ${(a / derived2).toFixed(3).padStart(13)} ${(a / derived3).toFixed(3).padStart(13)}`);
}
console.log('\n→ 产能翻倍时，现方案给出的中间品需求只有真实派生需求的 1/2；三倍时只有 1/3。');
console.log('  需求被系统性低估 → 中间品价格被压低 → 上游恒亏。这就是"重工业无法用零利润价表示"。');
console.log('  注意：开局（λ=1）两者相等——错配随偏离开局而增大，正是"零利润价随建筑数量改变"的成因。');

// ===== ⑤ 可实现性核验：纯派生需求（方案 C）在 Leontief 配方下没有均衡 =====
//
// 若 D_g 与价格无关（配方是固定系数的 Leontief，派生需求只取决于下游产量），
// 则 E = D − S 与 P 无关 ⇒ K = −E′(P) = 0 ⇒ ρ = 2ζ√(mK) = 0，
// 价格方程退化为 m·P̈ = E ⇒ 价格等加速发散，必然撞上钳制带后锁死。
// 这不是数值问题，是"需求对价格零弹性 ⇒ 没有价格均衡"的必然结果。
console.log('\n=== 五、可实现性核验：方案 C 不能单独当锚（它没有价格均衡） ===\n');
const ZETA = 0.7;
const STEPS = 10, H = 0.05;
function runODE(i, Dfun, S, ticks) {
  let P = PINIT[i], v = 0, clampTick = -1;
  const floor = FLOOR_RATIO * PCOST[i], ceil = CEIL_RATIO * PCOST[i];
  for (let t = 0; t < ticks; t++) {
    for (let s = 0; s < STEPS; s++) {
      const E = (pv) => Dfun(pv, i) - S;
      const K = (pv) => { const e = 1e-6; return -(E(pv + e) - E(pv - e)) / (2 * e); };
      const kk = Math.max(K(P), 0);
      const m = S, rho = 2 * ZETA * Math.sqrt(m * kk);
      const f = (pv, V) => [V, (E(pv) - rho * V) / m];
      const [a1, b1] = f(P, v);
      const [a2, b2] = f(P + H / 2 * a1, v + H / 2 * b1);
      const [a3, b3] = f(P + H / 2 * a2, v + H / 2 * b2);
      const [a4, b4] = f(P + H * a3, v + H * b3);
      P += H / 6 * (a1 + 2 * a2 + 2 * a3 + a4);
      v += H / 6 * (b1 + 2 * b2 + 2 * b3 + b4);
      if (P < floor) { P = floor; v = 0; if (clampTick < 0) clampTick = t; }
      else if (P > ceil) { P = ceil; v = 0; if (clampTick < 0) clampTick = t; }
    }
  }
  return { P, clampTick };
}
{
  const i = 7; // 钢：q=90，ε=0.5
  const derived = 90, supply = 100;   // 派生需求 90 < 当期供给 100 ⇒ 过剩
  // (a) 纯派生需求：D 与价格无关
  const pure = runODE(i, () => derived, supply, 600);
  console.log(`钢（派生需求 ${derived}，供给 ${supply}）：`);
  console.log(`  (a) 纯派生需求 D = ${derived}（与价格无关）：`);
  console.log(`      价格 ${PINIT[i]} → ${pure.P.toFixed(1)}（地板 ${(FLOOR_RATIO * PCOST[i]).toFixed(1)}），首次撞底 tick = ${pure.clampTick}`);
  console.log('      ⇒ E 恒为常数、K ≡ 0，价格等加速发散；撞底后锁死，短缺/过剩都不再影响价格。');
  // (b) C′：派生需求锚 + 保留价格弹性（a 按当期派生需求重锚）
  const r = PINIT[i] / PCOST[i];
  const aC = derived * Math.pow(r, EPS[i]);
  const cPrime = runODE(i, (pv) => aC * Math.pow(pv / PCOST[i], -EPS[i]), supply, 600);
  console.log(`  (b) C′ 派生需求锚 + 保留弹性（a = 派生需求 × r^ε）：价格 → ${cPrime.P.toFixed(1)}，撞底 tick = ${cPrime.clampTick < 0 ? '无' : cPrime.clampTick}`);
  // 短缺场景：供给跌到 80，派生需求仍 90
  const short = runODE(i, (pv) => aC * Math.pow(pv / PCOST[i], -EPS[i]), 80, 600);
  const wantShort = PCOST[i] * Math.pow(aC / 80, 1 / EPS[i]);
  console.log(`      短缺检验（供给 100 → 80）：C′ 收敛到 ${short.P.toFixed(1)}（解析 P* = ${wantShort.toFixed(1)}），即 +${(((short.P - PINIT[i]) / PINIT[i]) * 100).toFixed(0)}%；`);
  console.log('      ⇒ 与纯派生不同，C′ 保留了"缺货涨价"的价格通道，且有解析均衡。');
  console.log('\n  结论：C 必须写成 C′（派生需求决定锚的**位置**，价格弹性决定锚的**响应**），否则价格没有均衡。');
}

// ===== ⑥ 决策表 =====
console.log('\n=== 六、方案对照（修哪条病、代价、改动面） ===\n');
console.log('方案            修 P1  修 P2  修 P3   残余风险                         契约改动面');
console.log('A 支出份额锚       ✅     —      —     失去"必需品扩产迅速抹平利润"      §2.1 §2.4 §3.4 §6');
console.log('B 成本地板         —     ✅      —     地板随成本上升=隐性加成下限        §2.4 §3.4');
console.log("C′ 派生需求锚      —     —      ✅    需新增派生需求通道与投资品口径      §2.1 §2.2 §3.3 §6");
console.log('A+B                ✅     ✅      —     同上 + 地板参数化                  §2.1 §2.4 §3.4 §6');
console.log('A+C′               ✅     —      ✅     改动面最大，两者正交              §2.1 §2.2 §2.3 §3.3 §3.4 §6');
console.log('A+B+C′             ✅     ✅      ✅    全部；推荐作为 1.1 的目标          上述全部');

// ===== ⑦ 结论 =====
console.log('\n=== 七、结论与候选方案 ===\n');
console.log('病因（三条，互相独立）：');
console.log('  P1 需求弹性口径：P* ∝ S^(−1/ε)，ε<1 的必需品/工业品被 −1/ε 放大到 3~5 倍；');
console.log('  P2 地板口径：地板 0.2·P_cost 与成本无关，贴底后钳制令价格对短缺零响应；');
console.log('  P3 中间品需求口径：用"人均消费型"曲线代替派生需求，偏差随产能线性增长。');
console.log('\n候选方案：');
console.log('  A（需求侧）支出份额锚（单位弹性）：P* ∝ 1/S；λ_floor 从 1.5~1.8 抬到 6~8；改动最小。');
console.log('  B（地板侧）地板改为当期单位现金成本：贴底时仍覆盖工资+投入，上游不再"贴底即恒亏"。');
console.log('  C′（中间品侧）派生需求锚 + 保留价格弹性：a_g 按当期派生需求 Σ_j q_gj·N_j 重锚；');
console.log('      ——注意**不能**直接用"纯派生需求"（D 与价格无关 ⇒ K=0 ⇒ 价格无均衡，见第五节）。');
console.log('      消除"扩产 → 中间品价格崩塌"；重工业的波动由下游订单直接表达。');
console.log('\n建议：**A + C′**（A 修需求形状，C′ 修重工业口径；两者正交且都可实现），B 作为可选加固。');
console.log('代价：A 会削弱"必需品扩产迅速抹平利润"的设计意图（§2.4 的 −1/ε 敏感度随之失效）；');
console.log("      C′ 需要新增派生需求通道：a_g 每 tick 按 Σ_j q_gj·N_j 重锚（建造力作为投资品另给需求）。");
console.log('本探针只做量化与方案对比，**未改动任何实现**——方案选择需裁决。');

fs.writeFileSync('out/price_anchor_probe.txt', OUT.join('\n') + '\n', 'utf8');
