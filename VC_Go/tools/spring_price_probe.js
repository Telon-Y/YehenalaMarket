// 弹簧(二阶)价格 ODE 可行性数值探针
// 目的：验证 1.0 文档 §2.2 的方程形式是否良定、等效参数是多少，以及修正版是否可行。
// 运行：node VC_Go/tools/spring_price_probe.js

const out = [];
const log = (s = '') => out.push(s);
const fmt = (x, n = 2) => (Number.isFinite(x) ? x.toFixed(n) : String(x));

// ---------- 通用 RK4（状态 y = [P, Pdot]）----------
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

function integrate(rhs, y0, ticks, dt, nSub) {
  let y = y0.slice();
  const h = dt / nSub;
  const traj = [[0, y[0], y[1]]];
  for (let t = 1; t <= ticks; t++) {
    for (let s = 0; s < nSub; s++) y = rk4(rhs, y, h);
    traj.push([t, y[0], y[1]]);
  }
  return traj;
}

// 主导衰减率：解 m·λ² + ρ·λ + b_eff = 0，取实部绝对值较小（衰减最慢）的根。
// ζ≪1 时 λ₊,₋ = −ζω ± iω√(1−ζ²) ⇒ 4/|Re λ| ≈ 4/(ζω)
// ζ≫1 时 λ_slow ≈ −ω/(2ζ)          ⇒ 4/|Re λ| ≈ 8ζ/ω  ← 与 4/(ζω) 差 ζ² 倍
function settleTime(m, rho, bEff) {
  const disc = rho * rho - 4 * m * bEff;
  if (disc < 0) {
    const re = -rho / (2 * m);
    return { lambda: re, T: 4 / Math.abs(re), kind: 'underdamped' };
  }
  const s = Math.sqrt(disc);
  const l1 = (-rho + s) / (2 * m); // 较慢（靠近 0）
  const l2 = (-rho - s) / (2 * m); // 较快
  return { lambda: l1, lambda2: l2, T: 4 / Math.abs(l1), kind: 'overdamped' };
}

// ================= 基准参数（取自文档 §2.4 / §3.1 谷物）=================
const base = {
  a: 2.4e8, // 标准需求量 a = α·Pop
  // ⚠ 注意：这个 a 的数值口径其实是"成交额"而非"数量"（见 spring_price_scale.js 的量纲诊断）。
  //   本脚本刻意沿用文档原值，用来暴露"文档量级不自洽"这一事实。
  b: 0.01,  // 价格抑制系数（文档：需求对价格的敏感度）
  d: 0.01,  // 价格供应系数（文档 §2.1 的 d；§2.3 把供给改成阶梯常数）
  c: 2.4e7, // 标准供应量 / 阶梯供给 S_fixed
  P0: 2400, // 初始价
  Pref: 2400,
  k: 1e-6,  // 惯性系数 m = k·G
  G: 1e11,  // 市场总规模（总成交额）
  rho: 1e4, // 阻尼系数
  dt: 0.5,
  nSub: 10,
};
base.m = base.k * base.G; // = 1e5

log('=== 弹簧价格 ODE 可行性探针 ===');
log(`基准：m = k·G = ${base.m.toExponential(3)}，b = ${base.b}，d = ${base.d}，ρ = ${base.rho}`);
log('');

// ---------- 实验 1：文档字面式，合并同类项看等效刚度 ----------
// m·P̈ + ρ·Ṗ + b(P − P_ref) = E,  E = (a − bP) − (c + dP)
// ⇒ m·P̈ + ρ·Ṗ + [b + b + d]·P = a − c + b·P_ref
log('--- 实验 1：文档字面式的等效刚度（P 的系数会不会相消？）---');
{
  const bEff = base.b + (base.b + base.d); // = 2b + d
  const Pstar = (base.a - base.c + base.b * base.Pref) / bEff;
  log(`  合并同类项后 P 的系数 = b + (b + d) = ${bEff}`);
  log(`  ⇒ 不会相消，反而叠加：文档把 b 当"单一弹性旋钮"，实际生效的是 2b + d。`);
  log(`  等效平衡价 P* = (a − c + b·P_ref)/(2b + d) = ${fmt(Pstar, 1)} 元`);
  log('  ⇒ 与初始价 2400 相差若干数量级 ⇒ 根因不是形式而是量级，见实验 5 与');
  log('     VC_Go/tools/spring_price_scale.js 的量纲诊断。');
  log('');
}

// ---------- 实验 2：§2.3 的真实设定：S 为阶梯常数，只有 D 随 P 变 ----------
// m·P̈ + ρ·Ṗ + b(P − P_ref) = (a − bP) − S_fixed
log('--- 实验 2：§2.3 阶梯供给 S_fixed，只有需求随价格变 ---');
{
  const bEff = base.b + base.b; // = 2b
  const Pstar = (base.a - base.c + base.b * base.Pref) / bEff;
  const rhs = ([P, V]) => {
    const E = base.a - base.b * P - base.c;
    return [V, (E - base.rho * V - base.b * (P - base.Pref)) / base.m];
  };
  const zeta = base.rho / (2 * Math.sqrt(base.m * bEff));
  const wn = Math.sqrt(bEff / base.m);
  const traj = integrate(rhs, [base.P0, 0], 400, base.dt, base.nSub);
  log(`  等效刚度 b_eff = 2b = ${bEff}`);
  log(`  解析 P* = ${fmt(Pstar, 1)} 元，ζ = ${fmt(zeta, 4)}，ω_n = ${fmt(wn, 4)}，T = ${fmt((2 * Math.PI) / wn, 1)} tick`);
  log('   t      P              Ṗ');
  for (const [t, P, V] of traj.filter((r) => [1, 10, 50, 100, 200, 400].includes(r[0])))
    log(`  ${String(t).padStart(3)}  ${fmt(P, 1).padStart(13)}  ${fmt(V, 1).padStart(13)}`);
  const P400 = traj[400][0];
  log(`  |P(400) − P*| = ${fmt(Math.abs(P400 - Pstar), 1)}（${fmt((Math.abs(P400 - Pstar) / Pstar) * 100, 3)}% of P*）`);
  log(`  |P(400) − P*| 仍是 100% ⇒ 400 tick 内完全没接近平衡：这是"量级错配"导致的慢漂移，`);
  log(`  不是 ODE 的缺陷。该参数组下 ζ=${fmt(zeta, 1)} 属严重过阻尼，`);
  log('    价格只是被恒定缺口推着缓慢爬升，不能用"振荡收敛"的直觉去读它。');
  log('  ⇒ 形式可行，但必须先把 a / c / 初始价 三个量级摆平（见实验 5 与');
  log('     VC_Go/tools/spring_price_scale.js）；在那之前谈 T 与 ζ 都没有意义。');
  log('');
}

// ---------- 实验 3：框架级诊断——供给完全不动时会不会退化 ----------
log('--- 实验 3：供给对价格完全不敏感（d=0）时的退化边界 ---');
{
  for (const d of [0, 0.005, 0.01, 0.05]) {
    const bEff = base.b + (base.b + d);
    const Pstar = (base.a - base.c + base.b * base.Pref) / bEff;
    const zeta = base.rho / (2 * Math.sqrt(base.m * bEff));
    log(
      `  d=${fmt(d, 3)} → b_eff=${fmt(bEff, 3)}, P*=${fmt(Pstar, 0).padStart(8)} 元, ζ=${fmt(zeta, 4)}`,
    );
  }
  log('  ⇒ d=0 时 b_eff=2b 仍有回复力；只有当"E 的斜率"与"回复力斜率"人为配成');
  log('     d = −2b（供给随价格反向变动）时才会真正相消 ⇒ 纯漂移（双积分器）。');
  log('     结论：文档形式不至于退化，但 b 的语义被三重使用，调参必然困惑。');
  log('');
}

// ---------- 实验 4：修正版（去掉重复的回复力项）----------
// m·P̈ + ρ·Ṗ = a − b·P − S_fixed + F_ext   ⇒ 刚度恰为 b
log('--- 实验 4：修正版 m·P̈ + ρ·Ṗ = E(P)，无独立回复力项 ---');
{
  const bEff = base.b; // 恰好等于文档的 b
  const Pstar = (base.a - base.c) / base.b; // 无 P_ref 项
  const rhs = ([P, V]) => {
    const E = base.a - base.b * P - base.c;
    return [V, (E - base.rho * V) / base.m];
  };
  const zeta = base.rho / (2 * Math.sqrt(base.m * bEff));
  const wn = Math.sqrt(bEff / base.m);
  log(`  刚度 = b = ${bEff}（ζ = ρ/(2√(mb)) 这个文档定义在修正版下才真正成立）`);
  log(`  P* = (a − c)/b = ${fmt(Pstar, 1)} 元，ζ = ${fmt(zeta, 4)}，T = ${fmt((2 * Math.PI) / wn, 1)} tick`);
  log('  ⇒ 修正版把 b 还原为单一旋钮，且保留 Walras 调整的标准形式。');
  log('  ⇒ P_ref 改由影子成本反推（架构文档 T4），作为 F_ext 的锚定项而非刚度项。');
  log('');
}

// ---------- 实验 5：P_ref 标定 —— 要让稳态价落在初始价上 ----------
log('--- 实验 5：参数自洽性检查：稳态价能否落在文档初始价 2400？---');
{
  const bEff = base.b + base.b;
  const PrefNeeded = (bEff * base.P0 - base.a + base.c) / base.b;
  log(`  文档式：要 P* = 2400，需要 P_ref = ${PrefNeeded.toExponential(4)} 元（负值）⇒ 不可行`);
  const bNeeded = (base.a - base.c) / base.P0;
  log(`  修正版：要 P* = 2400，需要 b = (a − c)/P0 = ${bNeeded.toExponential(2)}`);
  log(`         即需求价格敏感度须是文档值 ${base.b} 的 ${fmt(bNeeded / base.b, 0)} 倍。`);
  log('  根因：a=2.4e8 而供给 c=2.4e7 时，2400 的价格根本压不住这个需求缺口。');
  log('  ⇒ 这就是架构文档 §10.3 (R8) 的规模杠杆问题：ODE 形式没错，喂进去的量级错了。');
  log('');
}

// ---------- 实验 6：用经济时间尺度反推 k 与 ρ ----------
log('--- 实验 6：参数标定——用可解释的时间尺度反推 k、ρ（推荐做法）---');
{
  const bEff = base.b; // 修正版刚度
  const Pstar = (base.a - base.c) / base.b;
  const cases = [
    { name: '文档现值 k=1e-6, ρ=1e4', k: 1e-6, rho: 1e4 },
    { name: '小惯性 k=1e-9, ζ=0.7', k: 1e-9, rho: 2 * 0.7 * Math.sqrt(1e-9 * base.G * bEff) },
    { name: '文档惯性 k=1e-6, ζ=0.7', k: 1e-6, rho: 2 * 0.7 * Math.sqrt(1e-6 * base.G * bEff) },
    { name: '大惯性 k=1e-4, ζ=0.7', k: 1e-4, rho: 2 * 0.7 * Math.sqrt(1e-4 * base.G * bEff) },
    { name: '大惯性 k=1e-4, ζ=0.1', k: 1e-4, rho: 2 * 0.1 * Math.sqrt(1e-4 * base.G * bEff) },
  ];
  log('  方案                         m         ρ          ζ      T(tick)  4/|λ|(tick)  数值±2%(tick)');
  for (const cs of cases) {
    const m = cs.k * base.G;
    const wn = Math.sqrt(bEff / m);
    const zeta = cs.rho / (2 * Math.sqrt(m * bEff));
    const T = (2 * Math.PI) / wn;
    const st = settleTime(m, cs.rho, bEff);
    // 数值积分（修正版）
    const rhs = ([P, V]) => {
      const E = base.a - base.b * P - base.c;
      return [V, (E - cs.rho * V) / m];
    };
    let y = [base.P0, 0];
    const h = base.dt / base.nSub;
    const hist = [];
    const nTicks = 20000;
    for (let t = 0; t < nTicks; t++) {
      for (let s = 0; s < base.nSub; s++) y = rk4(rhs, y, h);
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
    log(
      `  ${cs.name.padEnd(26)} ${m.toExponential(1).padStart(8)}  ${cs.rho.toExponential(1).padStart(
        9,
      )}  ${fmt(zeta, 3).padStart(5)}  ${fmt(T, 1).padStart(7)}  ${fmt(st.T, 0).padStart(
        11,
      )}  ${String(settleTick ?? '>' + nTicks).padStart(13)}`,
    );
  }
  log('  （P* = (a − c)/b = 2.16e10，初值 2400 ⇒ 极端失衡下的最坏情形）');
  log('  ⇒ 关键修正：稳定时间必须用主导特征根的实部，不能一律用 4/(ζω)。');
  log('     ζ≫1（过阻尼）时 λ_slow ≈ ω/(2ζ)，真实稳定时间比 4/(ζω) 慢 ζ² 倍：');
  log('     文档参数 (k=1e-6, ρ=1e4) 的 ζ=158，实际需 ~10⁶ tick 量级才收敛。');
  log('  ⇒ T 与 ζ 才是该调的旋钮；k、ρ 由 m = b/ω² 与 ρ = 2ζ√(mb) 反推。');
  log('');
}

// ---------- 实验 7：积分器稳定性（架构文档选 RK4）----------
log('--- 实验 7：显式积分稳定性边界（看 ω·dt，不是看 dt）---');
{
  const bEff = base.b;
  const m = base.m;
  const wn = Math.sqrt(bEff / m);
  log(`  ω_n = ${fmt(wn, 4)} /tick（k=1e-6）`);
  log('  积分器        dt     ω·dt      200 tick 后');
  for (const [label, dt, nSub, euler] of [
    ['Euler(1子步)', 0.5, 1, true],
    ['RK4(1子步)', 0.5, 1, false],
    ['RK4(10子步)', 0.5, 10, false],
    ['RK4(1子步)', 5, 1, false],
    ['RK4(1子步)', 50, 1, false],
    ['RK4(1子步)', 2000, 1, false],
  ]) {
    const h = dt / nSub;
    let y = [base.P0, 0];
    const rhs = ([P, V]) => {
      const E = base.a - base.b * P - base.c;
      return [V, (E - base.rho * V) / m];
    };
    let bad = false;
    for (let t = 0; t < 200 && !bad; t++) {
      for (let s = 0; s < nSub; s++) {
        if (euler) {
          const k = rhs(y);
          y = [y[0] + h * k[0], y[1] + h * k[1]];
        } else {
          y = rk4(rhs, y, h);
        }
      }
      if (!Number.isFinite(y[0]) || Math.abs(y[0]) > 1e18) bad = true;
    }
    log(
      `  ${label.padEnd(13)} ${fmt(dt, 1).padStart(6)}  ${fmt(wn * h, 4).padStart(7)}   ${
        bad ? '❌ 发散' : '✅ 稳定'
      }`,
    );
  }
  log('  ⇒ 稳定性由 ω·dt 决定，而 ω = √(b/m) 会随 k 变小而变大；');
  log('     架构文档把 nSub 参数化 + 用 m ≥ ρ²/4b 兜底是对症的。');
  log('');
}

// ---------- 实验 8：欠阻尼 + 离散扩建阶跃 → 极限环风险 ----------
log('--- 实验 8：ζ 很小时误差不收敛（架构文档 R1 的具体形态）---');
{
  // 每 50 tick 供给阶梯上调 5%，模拟离散扩建
  const rhsOf = (m, rho, cRef) => ([P, V]) => {
    const E = base.a - base.b * P - cRef.c;
    return [V, (E - rho * V) / m];
  };
  for (const zetaTarget of [0.05, 0.7]) {
    const m = base.m;
    const rho = 2 * zetaTarget * Math.sqrt(m * base.b);
    const cRef = { c: base.c };
    let y = [base.P0, 0];
    const h = base.dt / base.nSub;
    const samples = [];
    for (let t = 1; t <= 600; t++) {
      if (t % 50 === 0) cRef.c *= 1.05; // 离散扩建阶跃
      for (let s = 0; s < base.nSub; s++) y = rk4(rhsOf(m, rho, cRef), y, h);
      samples.push(y[0]);
    }
    const tail = samples.slice(-60);
    const mean = tail.reduce((s, x) => s + x, 0) / tail.length;
    const sd = Math.sqrt(tail.reduce((s, x) => s + (x - mean) ** 2, 0) / tail.length);
    log(
      `  ζ=${fmt(zetaTarget, 2)}：末 60 tick 均价 ${fmt(mean, 0)}，相对波动 std/mean = ${fmt(
        (sd / mean) * 100,
        2,
      )}%`,
    );
  }
  log('  ⇒ 低 ζ 下阶跃激励出持续振荡，收敛判定（<2%）会失败；');
  log('     这正是架构文档 §7.3 收敛判据与 R1 要盯的地方。');
  log('');
}

log('=== 结论 ===');
log('1) 弹簧（二阶弹性）价格 ODE 作为建模框架：可行。它就是"Walras 价格调整 + 惯性 + 阻尼"的标准写法。');
log('2) 文档 §2.2 的具体写法有语义缺陷：b 同时出现在需求斜率和回复力中，');
log('   实际生效刚度是 2b + d 而非 b，导致 ζ = ρ/(2√(mb)) 这个定义与真实动力学不一致。');
log('3) 推荐修正：写成 m·P̈ + ρ·Ṗ = E(P) + F_ext，刚度恰为 b；P_ref 通过影子成本进入 E，');
log('   不再作为独立刚度项。这样三个旋钮（T、ζ、F_ext）才真正正交。');
log('4) 真正的可行性杀手不是 ODE，而是量级（R8）：a、c、初始价三者不自洽，');
log('   稳态价与初始价差一个数量级，必须先做规模标定。');
log('5) m = k·G 的口径要重新定义：G 是"每 tick 成交额"（流量），不是累计规模；');
log('   否则 m 随时间漂移，等于让 ω 与 ζ 随经济总量缓慢变化。');

console.log(out.join('\n'));
