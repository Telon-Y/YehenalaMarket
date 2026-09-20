// 追加验证：把需求函数的量纲摆正后，弹簧 ODE 的动力学是否落在合理区间
// 运行：node VC_Go/tools/spring_price_scale.js

const out = [];
const log = (s = '') => out.push(s);
const fmt = (x, n = 2) => (Number.isFinite(x) ? x.toFixed(n) : String(x));

function rk4(rhs, y, h) {
  const k1 = rhs(y);
  const k2 = rhs([y[0] + 0.5 * h * k1[0], y[1] + 0.5 * h * k1[1]]);
  const k3 = rhs([y[0] + 0.5 * h * k2[0], y[1] + 0.5 * h * k2[1]]);
  const k4 = rhs([y[0] + h * k3[0], y[1] + h * k3[1]]);
  return [
    y[0] + (h / 6) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]),
    y[1] + (h / 6) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]),
  ];
}

// ------------------------------------------------------------------
// 量纲诊断：D = a − b·P 中 a 与 b·P 必须同量纲
// a 是"价格为零时的需求量(数量/tick)"，不是"成交额"
// 100k 人口、谷物人均消费 1 单位/tick ⇒ a = 1.0e5 单位/tick
// P0 = 2400 元；若 P0 处消费量降到 a 的一半，则 b·P0 = a/2
// ------------------------------------------------------------------
const Pop = 1e5;
const a = 1.0 * Pop;        // 1.0e5 单位/tick（数量）
const P0 = 2400;            // 元
const bHalf = a / (2 * P0); // 20.83 单位/元 —— P0 处消耗掉一半需求
// 供给取 70% 的标准需求 ⇒ 冲击后均衡价 P* = (a − S)/b = 0.3a/b = 0.6·P0 = 1440
// （刻意让初值 2400 ≠ P*，否则初值即稳态、什么都看不到）
// 即：供给砍掉 30% ⇒ 价格从 2400 下行到 1440（−40%）—— 一个真实的价格冲击场景
const S_fixed = 0.7 * a;

log('=== 量纲诊断：a 到底该取多少 ===');
log(`  若 a = 数量 = ${a.toExponential(3)} 单位/tick（100k 人 × 1 单位/人）`);
log(`  自洽的 b：P0=${P0} 元处需求降到一半 ⇒ b = a/(2·P0) = ${fmt(bHalf, 4)} 单位/元`);
log(`  文档给的 b = 0.01 ⇒ 需求对价格几乎无弹性（涨 2400 元只减 24 单位需求）`);
log(`  文档写法 a = α·Pop = 2.4e8 的隐含口径是"成交额" ⇒ 与 b·P 的量纲不一致`);
log(`  量纲错配倍数 ≈ ${fmt((2.4e8 / a) * (bHalf / 0.01), 0)} 倍 ⇒ 平衡价被推到 1e10 量级`);
log('');

// 主导衰减率：解 λ² + (ρ/m)λ + b/m = 0，取实部绝对值较小（衰减最慢）的根。
// ζ≪1：4/|Re λ| ≈ 4/(ζω)；ζ≫1：|Re λ| ≈ ω/(2ζ) ⇒ 真实稳定时间远比 4/(ζω) 慢。
function dominantRoot(m, rho, bEff) {
  const disc = (rho / m) ** 2 - (4 * bEff) / m;
  if (disc < 0) return -rho / (2 * m); // 欠阻尼：实部
  return (-rho / m + Math.sqrt(disc)) / 2; // 过阻尼：较慢的实根
}

// ------------------------------------------------------------------
// 动力学：给定想要的 T 与 ζ，反推 m 与 ρ，再数值验证
// 修正版：m·P̈ + ρ·Ṗ = a − b·P − S_fixed
//   ω = √(b/m)，T = 2π/ω，ζ = ρ/(2√(m·b))，稳定时间 ≈ 4/|Re λ_slow|
// ------------------------------------------------------------------
log('=== 动力学标定：用 T 与 ζ 反推 m、ρ ===');
log('  目标          b         m          ρ        ζ      T(tick)  4/|λ|(tick)  数值±2%(tick)  过冲');

const targets = [
  { name: 'T=26, ζ=0.7', T: 26, zeta: 0.7 },
  { name: 'T=26, ζ=1.0', T: 26, zeta: 1.0 },
  { name: 'T=52, ζ=0.7', T: 52, zeta: 0.7 },
  { name: 'T=52, ζ=0.2', T: 52, zeta: 0.2 },
  { name: 'T=104, ζ=0.7', T: 104, zeta: 0.7 },
];

for (const tg of targets) {
  const b = bHalf;
  const wn = (2 * Math.PI) / tg.T;
  const m = b / (wn * wn);
  const rho = 2 * tg.zeta * Math.sqrt(m * b);
  const Pstar = (a - S_fixed) / b;

  const rhs = ([P, V]) => {
    const E = a - b * P - S_fixed;
    return [V, (E - rho * V) / m];
  };
  let y = [P0, 0];
  const h = 0.5 / 10;
  const hist = [];
  for (let t = 1; t <= 2000; t++) {
    for (let s = 0; s < 10; s++) y = rk4(rhs, y, h);
    hist.push(y[0]);
  }
  let settleTick = null;
  for (let i = 0; i < hist.length; i++) {
    if (Math.abs(hist[i] - Pstar) / Pstar > 0.02) continue;
    let ok = true;
    for (let j = i; j < hist.length; j++)
      if (Math.abs(hist[j] - Pstar) / Pstar > 0.02) { ok = false; break; }
    if (ok) { settleTick = i + 1; break; }
  }
  const over = tg.zeta < 1 ? Math.exp((-Math.PI * tg.zeta) / Math.sqrt(1 - tg.zeta * tg.zeta)) : 1;
  const overPct = tg.zeta < 1 ? (1 - over) * 100 : 0; // 标准二阶系统过冲百分比
  log(
    `  ${tg.name.padEnd(12)} ${fmt(b, 3).padStart(7)} ${m.toExponential(2).padStart(10)} ${rho
      .toExponential(2)
      .padStart(10)} ${fmt(tg.zeta, 2).padStart(5)} ${fmt(tg.T, 1).padStart(8)} ${fmt(
      4 / Math.abs(dominantRoot(m, rho, b)),
      1,
    ).padStart(12)} ${String(settleTick ?? '>2000').padStart(14)} ${fmt(overPct, 1).padStart(7)}%`,
  );
}
log(`  （P* = (a − S_fixed)/b = ${fmt((a - S_fixed) / bHalf, 0)} 元，初值 ${P0} 元 ⇒ 供给砍 30%、价格 −40% 的冲击）`);
log('  ⇒ T、ζ 一旦定死，m 与 ρ 就被唯一确定，数值 settling 与 4/|λ_slow| 同量级，模型可控。');
log('  ⚠ 务必用主导特征根而不是 4/(ζω)：过阻尼时两者可差 ζ² 倍（这是最容易踩的坑）。');
log('');

// ------------------------------------------------------------------
// 关键实操问题：b 随人口变，而 m 若按 k·G 定，则 ω 与 ζ 会漂移
// ------------------------------------------------------------------
log('=== m = k·G 的口径问题：G 变化会不会让 ζ 漂移？===');
{
  const b = bHalf;
  const T = 26;
  const wn = (2 * Math.PI) / T;
  const m0 = b / (wn * wn);
  const k = m0 / 1e11; // 用初始 G=1e11 标定 k
  const rho = 2 * 0.7 * Math.sqrt(m0 * b);
  log(`  初始标定：G=1e11 ⇒ k=${k.toExponential(3)}, m=${m0.toExponential(3)}, ρ=${rho.toExponential(3)}`);
  log('  经济规模 G     m=k·G       ω         ζ      T(tick)');
  for (const G of [1e10, 1e11, 1e12]) {
    const m = k * G;
    const w = Math.sqrt(b / m);
    const z = rho / (2 * Math.sqrt(m * b));
    log(
      `  ${G.toExponential(0).padStart(10)}  ${m.toExponential(3).padStart(10)}  ${fmt(w, 5).padStart(8)}  ${fmt(
        z,
        3,
      ).padStart(6)}  ${fmt((2 * Math.PI) / w, 1).padStart(8)}`,
    );
  }
  log('  ⇒ G 涨 10 倍 ⇒ ζ 掉到 1/√10、T 涨 √10 倍：同一个市场会"越富越震荡"。');
  log('     若不想让规模改变动力学，m 应与 b 同比缩放（m ∝ b），而不是 ∝ 成交额。');
  log('');
}

// ------------------------------------------------------------------
// 附录：真正的退化情形（纯漂移/双积分器）长什么样，作为反例存档
// ------------------------------------------------------------------
log('=== 反例存档：什么写法会退化成"没有弹簧" ===');
{
  // 把 E 的斜率与回复力斜率人为配平：E = a − c − (b+d)P 且回复力系数取 b+d 之外
  // 使 P 的总系数为零 —— 这里直接构造：m·P̈ + ρ·Ṗ = 常数
  const b = bHalf;
  const m = 1e5;
  const rho = 1e3;
  const constE = (a - S_fixed) * 0.5; // 与 P 无关的恒定过剩需求
  const rhs = ([, V]) => [V, (constE - rho * V) / m];
  let y = [P0, 0];
  const h = 0.05;
  for (let t = 0; t < 200; t++) for (let s = 0; s < 10; s++) y = rk4(rhs, y, h);
  log(`  恒定过剩需求（E 与 P 无关）：200 tick 后 P = ${fmt(y[0], 0)}，Ṗ = ${fmt(y[1], 1)}`);
  log(`  ⇒ 阻尼把 Ṗ 拉到 0 后停在 ${fmt(y[0], 0)}，永不回到任何 P*（没有回复力）。`);
  log('     这就是"没有弹簧"的市场：价格只被过剩需求推着走，推完就停。');
  log('     文档 §2.3 的阶梯供给恰好避免了这一点（S_fixed 不随 P 变，b·P 留在方程里）。');
}

console.log(out.join('\n'));
