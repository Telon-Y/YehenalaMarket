// VC_Go/tools/core_sim_probe.js —— 1.0 契约的完整多商品联合模拟 + §8.4 验收判据实测
//
// 为什么需要本脚本：
//   FEASIBILITY-1.0.md §5.1 自认"多商品联合模拟仍需实现后才能给出 1.0 的最终可行性结论"。
//   仓库内原有的 expansion_probe.js 只是单商品自洽近似（投入品价格按本商品价格同比例缩放）。
//   本脚本按契约原文实现 11 商品 x 11 建筑的联合 tick 级模拟，并直接计算 §8.4 的 A1-A6。
//
// 契约条文对应：
//   §2.1 D = a(P/P0)^-eps；§2.2 S = Σ hireRate·q·N·shortageFactor；E = D − S
//   §2.3 m P'' + rho P' = E；§2.4 RK4 dt=0.5 nSub=10，P ∈ [0.2,5]P_cost，每 tick 重估 K(P)/m/rho
//   §3.3 配方 + 原料短缺惩罚（等比配给）；§3.4 P_cost/P_init 与 a = S0(P_init/P_cost)^eps 标定
//   §4.1 利润率>10% 扩建（每超 5pp 多扩 1 个，单次 ≤ 总数 10%）
//   §4.2 队列/每工地 30 建造力/产能上限/未利用耕地自动生成自给农场
//   §4.3 每建筑独立现金池（初始 5,000/级），资金不足停工
//   §4.4 雇佣率<75% 连续 156 周期 → 每周期 -5%
//   §5   5,000 人/级（0.75/0.20/0.05 阶层，均薪 6.75），工资全额消费
//   §6   消费组/使用价值/权重 S/(2P)/满足度；§6.5 人口每 52 周期结算
//   §7   GDP = 消费者实际支出 + 各建筑现金池期末总额
//   §8   主循环 12 步
//
// 运行：
//   node VC_Go/tools/core_sim_probe.js 10000
//   node VC_Go/tools/core_sim_probe.js 10000 --sweep
//   node VC_Go/tools/core_sim_probe.js 10000 --budget=0
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const arg = (k, d) => { const a = process.argv.find((x) => x.startsWith('--' + k + '=')); return a ? Number(a.split('=')[1]) : d; };
const TICKS = Number(process.argv[2] || 10000);
const POP0 = arg('pop', 100000);

// 运行期可覆盖参数（供 --sweep 使用）
const OVER = {
  red: arg('redundancy', 1.15),      // 开局冗余系数
  sub: arg('subsist', 0.05),         // 自给农场规模（每单位未利用耕地）
  tier: arg('tier', 7),              // §6.3 需求量表所取财富档
  budget: arg('budget', 1),          // 1 = 施加「工资是消费唯一资金来源」预算约束
  cash: arg('cashmult', 1),          // 现金池初始倍数（契约 §4.3 = 1 → 5,000/级）
};

// §2.3（2026-09-19 改）：惯性 m ≡ 当期流通量、T = 2π√(m/K) 内生，已无 T_PERIOD 旋钮
const ZETA = 0.7, DT = 0.5, NSUB = 10, H = DT / NSUB;
const TPY = 52;
const WAGE_PER_LEVEL = 5000 * 6.75;   // §5 = 33,750 元/级/周期
const DECAY_WINDOW = 156, DECAY_RATE = 0.05, IDLE_HR = 0.75;
const EXPAND_TH = 0.10, HIRE_ANNUAL = 0.05;
const SITE_POWER = 30, QUEUE_WARN = 52;

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
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
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100];
const ARABLE = 10000, COALCAP = 500, IRONCAP = 500, POWERCAP = 1000;
const SUB_OUT = [[0, 2], [2, 1], [3, 0.5]];

const GROUPS = [
  { name: '简朴衣物', uses: [[2, 1], [3, 1]] },
  { name: '基础食物', uses: [[0, 1], [1, 1.5]] },
  { name: '标准衣物', uses: [[3, 1], [4, 1]] },
  { name: '住宅', uses: [[9, 1]] },
];
const DEMAND_TABLE = [{ w: 5, d: [39, 210, 0, 20] }, { w: 10, d: [41, 210, 7, 74] }, { w: 20, d: [0, 210, 122, 130] }];

function interp(table, w) {
  w = Math.min(20, Math.max(5, w));
  let lo = table[0], hi = table[table.length - 1];
  for (let i = 0; i + 1 < table.length; i++) if (w >= table[i].w && w <= table[i + 1].w) { lo = table[i]; hi = table[i + 1]; break; }
  const t = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  return lo.d.map((x, k) => x + (hi.d[k] - x) * t);
}

// ---------- Leontief 完全需求，用于推导开局级数 ----------
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

function finalDemand(pop, tier) {
  const d = interp(DEMAND_TABLE, tier).map((x) => x * pop / 100000);
  const f = new Array(N).fill(0);
  for (let gi = 0; gi < GROUPS.length; gi++) {
    if (gi === 3) { f[9] += d[3]; continue; }
    const uses = GROUPS[gi].uses, s = uses.reduce((a, u) => a + u[1], 0);
    for (const [i, v] of uses) f[i] += d[gi] * v / s;
  }
  return f;
}

function createModel() {
  const f = finalDemand(POP0, OVER.tier);
  const Y = B.map((row) => row.reduce((s, x, j) => s + x * f[j], 0));
  const M = {
    tick: 0, pop: POP0, P: PINIT.slice(), dP: new Array(N).fill(0),
    Pcost: PCOST.slice(), a: new Array(N).fill(0),
    levels: new Array(N).fill(0), hire: new Array(N).fill(1), idle: new Array(N).fill(0),
    cash: new Array(N).fill(0), queue: [], hist: [], clampTicks: new Array(N).fill(0),
    qWarn: 0, orderSeq: 0, powerRevenue: 0, blockedBuilds: 0,
  };
  const capOf = (i) => (i === 0 || i === 2) ? ARABLE : i === 5 ? COALCAP : i === 6 ? IRONCAP : i === 10 ? POWERCAP : Infinity;
  for (let i = 0; i < N; i++) {
    const L = Y[i] / REC[i].q * OVER.red;
    M.levels[i] = Math.max(1, Math.min(Math.ceil(L), capOf(i)));
  }
  // 迭代抬升：保证每个部门的中间投入需求不超过其产出的 85%，避免开局紧平衡
  for (let pass = 0; pass < 60; pass++) {
    const Yc = new Array(N).fill(0);
    for (let i = 0; i < N; i++) Yc[i] = M.levels[i] * REC[i].q;
    let changed = false;
    for (let i = 0; i < N; i++) {
      let interm = 0;
      for (let j = 0; j < N; j++) interm += A[i][j] * Yc[j];
      const want = Math.ceil((interm + f[i]) / 0.85 / REC[i].q);
      if (want > M.levels[i]) { M.levels[i] = Math.min(want, capOf(i)); changed = true; }
    }
    if (!changed) break;
  }
  {
    const tot = M.levels[0] + M.levels[2];
    if (tot > ARABLE) { const k = ARABLE / tot; M.levels[0] *= k; M.levels[2] *= k; }
  }
  for (let i = 0; i < N; i++) M.cash[i] = 5000 * OVER.cash * M.levels[i];
  const S0 = outputOf(M);
  // §3.4 步骤 3 标定：a = S0·(P_init/P_cost)^eps。这里的 S0 必须是【净供给/售出量】，
  // 不是总产出——否则会把中间投入重复算作可售量。用恒等式 netS = Y − A·Y 反算。
  const netS0 = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    let interm = 0;
    for (let j = 0; j < N; j++) interm += A[i][j] * S0[j];
    netS0[i] = Math.max(S0[i] - interm, 1e-9);
  }
  for (let i = 0; i < N; i++) M.a[i] = netS0[i] * Math.pow(PINIT[i] / PCOST[i], EPS[i]);
  M.S0 = S0; M.netS0 = netS0; M.f0 = f;
  return M;
}

function subsistence(M) {
  const used = M.levels[0] + M.levels[2];
  const idle = Math.max(0, ARABLE - used);
  const n = idle * OVER.sub;
  const y = new Array(N).fill(0);
  for (const [i, c] of SUB_OUT) y[i] += n * c;
  return y;
}
function outputOf(M) {
  const y = new Array(N).fill(0);
  for (let i = 0; i < N; i++) y[i] = M.levels[i] * REC[i].q * M.hire[i];
  const sub = subsistence(M);
  for (let i = 0; i < N; i++) y[i] += sub[i];
  return y;
}

function tick(M) {
  const P = M.P, Pc = M.Pcost;

  // ===== §8-2 产能申报 / §3.3 等比配给 / 短缺惩罚 =====
  const Ymax = new Array(N).fill(0);
  for (let i = 0; i < N; i++) Ymax[i] = M.levels[i] * REC[i].q * M.hire[i];
  const sub = subsistence(M);
  for (let i = 0; i < N; i++) Ymax[i] += sub[i];
  const demandInp = new Array(N).fill(0);
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) demandInp[j] += M.levels[i] * M.hire[i] * q;
  const allocRatio = new Array(N).fill(1);
  for (let j = 0; j < N; j++) if (demandInp[j] > 1e-12) allocRatio[j] = Math.min(1, Ymax[j] / demandInp[j]);
  const Y = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    let sf = 1;
    for (const [j] of REC[i].inp) sf = Math.min(sf, allocRatio[j]);
    Y[i] = M.levels[i] * REC[i].q * M.hire[i] * sf + sub[i];
  }
  const usedInp = new Array(N).fill(0);
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) usedInp[j] += M.levels[i] * M.hire[i] * q * allocRatio[j];
  const netPool = Y.map((y, i) => Math.max(0, y - usedInp[i]));   // §6.4 的净供给 S_i

  // ===== §2 价格 ODE =====
  const D = new Array(N).fill(0);
  for (let i = 0; i < N; i++) D[i] = M.a[i] * Math.pow(P[i] / Pc[i], -EPS[i]);
  const E = D.map((d, i) => d - Y[i]);
  for (let i = 0; i < N; i++) {
    const K = (EPS[i] * M.a[i] / Pc[i]) * Math.pow(P[i] / Pc[i], -EPS[i] - 1);
    // §2.3（2026-09-19 改）：惯性 m ≡ 当期市场内流通商品量 S；ρ = 2ζ√(m·K)
    let mm = Y[i];
    if (!isFinite(mm) || mm <= 0) mm = 1e-9;
    let rho = 2 * ZETA * Math.sqrt(mm * K);
    if (!isFinite(rho) || rho < 0) rho = 0;
    let p = P[i], v = M.dP[i];
    const fl = 0.2 * Pc[i], ce = 5 * Pc[i];
    const fn = (pv, vv) => [vv, (E[i] - rho * vv) / mm];
    for (let s = 0; s < NSUB; s++) {
      const [k1a, k1b] = fn(p, v);
      const [k2a, k2b] = fn(p + H / 2 * k1a, v + H / 2 * k1b);
      const [k3a, k3b] = fn(p + H / 2 * k2a, v + H / 2 * k2b);
      const [k4a, k4b] = fn(p + H * k3a, v + H * k3b);
      p += H / 6 * (k1a + 2 * k2a + 2 * k3a + k4a);
      v += H / 6 * (k1b + 2 * k2b + 2 * k3b + k4b);
      if (!isFinite(p)) { p = Pc[i]; v = 0; break; }
      if (p < fl) { p = fl; v = 0; } else if (p > ce) { p = ce; v = 0; }
    }
    P[i] = p; M.dP[i] = v;
    if (p <= fl * 1.001 || p >= ce * 0.999) M.clampTicks[i]++;
  }

  // ===== §5 工资 =====
  const wageBill = new Array(N).fill(0);
  let totalWage = 0;
  for (let i = 0; i < N; i++) { wageBill[i] = M.levels[i] * M.hire[i] * WAGE_PER_LEVEL; totalWage += wageBill[i]; }

  // ===== §6 消费组购买 =====
  const gq = interp(DEMAND_TABLE, OVER.tier).map((x) => x * M.pop / 100000);
  const stock = netPool.slice();
  const bought = new Array(N).fill(0);
  const sat = new Array(GROUPS.length).fill(0);
  for (let gi = 0; gi < GROUPS.length; gi++) {
    const uses = GROUPS[gi].uses;
    const ordered = uses.map(([id]) => ({ id, w: stock[id] / (2 * P[id]) })).sort((x, y) => y.w - x.w);
    const target = gq[gi];
    let got = 0;
    for (const { id } of ordered) {
      if (got >= target - 1e-12) break;
      const perUnit = uses.find((u) => u[0] === id)[1];
      const avail = Math.max(0, stock[id]);
      if (avail <= 1e-12) continue;
      const take = Math.min((target - got) / perUnit, avail);
      stock[id] -= take; bought[id] += take; got += take * perUnit;
    }
    sat[gi] = target > 1e-9 ? Math.min(1, got / target) : 1;
  }
  let spendWant = 0; for (let i = 0; i < N; i++) spendWant += bought[i] * P[i];
  // 预算约束：§5「全部工资用于消费」+「工资是唯一资金来源」⇒ 消费支出 ≤ 当期工资总额。
  // 契约未显式写这条，但不加它货币就不守恒（消费者会凭空支付超出其收入的金额）。
  let spendScale = 1;
  if (OVER.budget && spendWant > totalWage && spendWant > 1e-9) {
    spendScale = totalWage / spendWant;
    for (let i = 0; i < N; i++) bought[i] *= spendScale;
    for (let gi = 0; gi < GROUPS.length; gi++) sat[gi] *= spendScale;
  }
  let spend = 0; for (let i = 0; i < N; i++) spend += bought[i] * P[i];

  // ===== §4.3/§8-3 利润与现金池（含中间投入的货币流）=====
  const saleValue = new Array(N).fill(0);
  let saleValueTotal = 0;
  for (let i = 0; i < N; i++) { saleValue[i] = bought[i] * P[i]; saleValueTotal += saleValue[i]; }
  const inputValue = new Array(N).fill(0);
  let inputValueTotal = 0;
  for (let i = 0; i < N; i++) {
    for (const [j, q] of REC[i].inp) inputValue[i] += M.levels[i] * M.hire[i] * q * allocRatio[j] * P[j];
    inputValueTotal += inputValue[i];
  }
  const MKT = spend + inputValueTotal;
  const cashIn = new Array(N).fill(0);
  if (saleValueTotal > 1e-9) for (let i = 0; i < N; i++) cashIn[i] = MKT * (saleValue[i] / saleValueTotal);
  else for (let i = 0; i < N; i++) cashIn[i] = MKT / N;
  for (let i = 0; i < N; i++) M.cash[i] += cashIn[i] - wageBill[i];

  // ===== §4.1/§8-5.6 建造力分配 =====
  let powerLeft = Y[10];
  const cashPerPower = P[10];
  for (const o of M.queue) {
    if (powerLeft <= 1e-9) break;
    const grant = Math.min(SITE_POWER, powerLeft);
    o.progress += grant; powerLeft -= grant;
    M.cash[o.type] -= grant * cashPerPower;
    M.powerRevenue += grant * cashPerPower;
  }
  M.cash[10] += M.powerRevenue; M.powerRevenue = 0;
  const keep = [];
  for (const o of M.queue) {
    if (o.progress >= BUILDCOST[o.type] * o.units - 1e-9) {
      let add = o.units;
      if (o.type === 0 || o.type === 2) add = Math.max(0, Math.min(add, ARABLE - M.levels[0] - M.levels[2]));
      else if (o.type === 5) add = Math.max(0, Math.min(add, COALCAP - M.levels[5]));
      else if (o.type === 6) add = Math.max(0, Math.min(add, IRONCAP - M.levels[6]));
      else if (o.type === 10) add = Math.max(0, Math.min(add, POWERCAP - M.levels[10]));
      M.levels[o.type] += add;
      if (add < o.units) M.blockedBuilds++;
    } else keep.push(o);
  }
  M.queue = keep;

  // ===== §8-1 利润率（收入 − 投入成本 − 工资，成本基 = 当期市价）=====
  const margin = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    const cost = inputValue[i] + wageBill[i];
    margin[i] = cost > 1e-9 ? (cashIn[i] - cost) / cost : 0;
  }

  // ===== §4.4 缩编 =====
  for (let i = 0; i < N; i++) {
    if (M.hire[i] < IDLE_HR) M.idle[i]++; else M.idle[i] = 0;
    if (M.idle[i] > DECAY_WINDOW && M.levels[i] > 0) {
      const nl = M.levels[i] * (1 - DECAY_RATE);
      M.levels[i] = nl >= 0.5 ? nl : 0;
    }
  }

  // ===== §4.2 队列预警 + §4.1 AI 扩建 =====
  {
    let p = Y[10], eta = 0;
    for (const o of M.queue) {
      const need = BUILDCOST[o.type] * o.units - o.progress;
      if (need > p) { eta += (need - p) / Math.max(p, 1e-9) + 1; p = 0; } else p -= need;
    }
    if (M.queue.length && eta > QUEUE_WARN) {
      M.qWarn++;
      if (!M.queue.length || M.queue[0].type !== 10) M.queue.unshift({ type: 10, units: 1, progress: 0, id: M.orderSeq++ });
    }
  }
  for (let i = 0; i < N; i++) {
    if (margin[i] <= EXPAND_TH) continue;
    const nExtra = Math.floor((margin[i] - EXPAND_TH) / 0.05) + 1;
    const units = Math.max(1, Math.round(Math.min(nExtra * 0.10 * M.levels[i], 0.10 * M.levels[i])));
    M.queue.push({ type: i, units, progress: 0, id: M.orderSeq++ });
  }

  // ===== §6.5 人口（每 52 周期结算）=====
  const satNec = (sat[0] + sat[1]) / 2;
  let growth;
  if (satNec >= 0.75) growth = 0.05 * (satNec - 0.75) / 0.25;
  else growth = -0.20 * (0.75 - satNec) / 0.75;
  if (M.tick > 0 && M.tick % TPY === 0) M.pop *= (1 + growth);

  // ===== §5 雇佣调整（年率折算）=====
  const step = HIRE_ANNUAL / TPY;
  for (let i = 0; i < N; i++) {
    if (margin[i] < 0) M.hire[i] = Math.max(0, M.hire[i] - step);
    else if (margin[i] > 0) M.hire[i] = Math.min(1, M.hire[i] + step);
  }

  // ===== §7 GDP =====
  let cashTotal = 0; for (let i = 0; i < N; i++) cashTotal += M.cash[i];
  const gdpDoc = spend + cashTotal;
  let gdpProd = 0;
  for (let i = 0; i < N; i++) gdpProd += Y[i] * P[i] - inputValue[i];

  M.tick++;
  let nu = 0, de = 0;
  for (let i = 0; i < N; i++) { const L = M.levels[i]; nu += margin[i] * L; de += L; }
  M.hist.push({
    tick: M.tick, P: P.slice(), S: Y.slice(), D: D.slice(), E: E.slice(),
    margin, sat: sat.slice(), pop: M.pop, gdpDoc, gdpFlow: spend, gdpProd,
    levels: M.levels.slice(), cashTotal, spend, wage: totalWage, queueLen: M.queue.length,
    allocMin: Math.min(...allocRatio), bought: bought.slice(),
    costTotal: inputValueTotal + totalWage, inputTotal: inputValueTotal,
    revTotal: cashIn.reduce((a, b) => a + b, 0),
    marginWavg: de > 0 ? nu / de : 0, spendScale,
    hireAvg: M.hire.reduce((a, b) => a + b, 0) / N,
  });
}

// ---------- §8.4 判据 ----------
function assess(M) {
  const h = M.hist;
  if (h.length < 200) return null;
  const res = {};
  const tail = h.slice(-2000);
  const T = tail.length;
  const clampShare = new Array(N).fill(0), maxLog = new Array(N).fill(0);
  for (const r of tail) for (let i = 0; i < N; i++) {
    if (r.P[i] <= 0.2 * PCOST[i] * 1.001 || r.P[i] >= 5 * PCOST[i] * 0.999) clampShare[i]++;
    maxLog[i] = Math.max(maxLog[i], Math.abs(Math.log(r.P[i] / PCOST[i])));
  }
  res.A1 = { pass: maxLog.every((x) => x <= Math.log(5) + 1e-9) && clampShare.every((x) => x / T < 0.05), maxLog, clampShare: clampShare.map((x) => x / T) };
  let mMin = 1e9, mMax = -1e9;
  for (const r of tail) for (let i = 0; i < N; i++) { mMin = Math.min(mMin, r.margin[i]); mMax = Math.max(mMax, r.margin[i]); }
  let num = 0, den = 0;
  for (const r of tail) for (let i = 0; i < N; i++) { const L = r.levels[i]; num += r.margin[i] * L; den += L; }
  const wAvg = den > 0 ? num / den : 0;
  res.A2 = { pass: mMin >= -0.10 - 1e-9 && mMax <= 0.25 + 1e-9 && wAvg <= 0.15 + 1e-9, mMin, mMax, wAvg };
  const l5 = h.slice(-500);
  let dPR = 0, dSa = 0, nrm = 0;
  for (let k = 1; k < l5.length; k++) for (let i = 0; i < N; i++) {
    dPR += Math.abs(l5[k].P[i] / PCOST[i] - l5[k - 1].P[i] / PCOST[i]);
    dSa += Math.abs(l5[k].S[i] / Math.max(M.a[i], 1e-12) - l5[k - 1].S[i] / Math.max(M.a[i], 1e-12));
    nrm++;
  }
  dPR /= nrm; dSa /= nrm;
  res.A3 = { pass: dPR < 1e-4 && dSa < 2e-4, dPR, dSa };
  const allPos = h.every((r) => r.gdpDoc > 0 && isFinite(r.gdpDoc));
  const xs = l5.map((r) => r.tick), ys = l5.map((r) => r.gdpDoc);
  const mx = xs.reduce((a, b) => a + b, 0) / xs.length, my = ys.reduce((a, b) => a + b, 0) / ys.length;
  let sxy = 0, sxx = 0;
  for (let k = 0; k < xs.length; k++) { sxy += (xs[k] - mx) * (ys[k] - my); sxx += (xs[k] - mx) ** 2; }
  const slope = sxx > 0 ? sxy / sxx : 0;
  res.A4 = { pass: allPos && slope >= 0, allPos, slope, minGdp: Math.min(...h.map((r) => r.gdpDoc)) };
  const popAnnual = Math.pow(M.pop / h[0].pop, TPY / Math.max(1, M.tick - h[0].tick)) - 1;
  const lv0 = h[0].levels.reduce((a, b) => a + b, 0), lvT = h[h.length - 1].levels.reduce((a, b) => a + b, 0);
  res.A5 = { pass: popAnnual >= -1e-9 && popAnnual <= 0.05 + 1e-9 && lvT > 0, popAnnual, lv0, lvT };
  const ne = tail.filter((r) => r.queueLen > 0).length;
  res.A6 = { pass: ne / T < 0.20 && M.qWarn === 0, share: ne / T, qWarn: M.qWarn };
  return res;
}

function run(label) {
  const M = createModel();
  console.log(`\n${'='.repeat(76)}\n${label}\n${'='.repeat(76)}`);
  console.log(`参数：ticks=${TICKS} 开局冗余=${OVER.red} 自给农场=${OVER.sub} 需求档=${OVER.tier} 人口0=${POP0} 预算约束=${OVER.budget ? '开' : '关'} 现金池倍数=${OVER.cash}`);
  console.log('开局级数：' + M.levels.map((l, i) => `${S[i]}=${l.toFixed(0)}`).join(' '));
  console.log('标定：a = netS0·(P_init/P_cost)^eps ⇒ t=0 时 E=0（§3.4 步骤 3）');

  // ---- 开局会计体检 ----
  {
    const Y = new Array(N).fill(0);
    for (let i = 0; i < N; i++) Y[i] = M.levels[i] * REC[i].q;
    let wage = 0; for (let i = 0; i < N; i++) wage += M.levels[i] * WAGE_PER_LEVEL;
    let inputVal = 0, rev = 0;
    const f = finalDemand(M.pop, OVER.tier);
    console.log('\n  开局体检（价格 = P_init，雇工率 = 1）：');
    console.log('  商品        总产出      中间投入     净供给     §6.3最终需求   净供给/需求');
    for (let i = 0; i < N; i++) {
      let interm = 0; for (let j = 0; j < N; j++) interm += A[i][j] * Y[j];
      const net = Y[i] - interm;
      inputVal += interm * M.P[i];
      rev += net * M.P[i];
      const r = f[i] > 1e-9 ? (net / f[i]).toFixed(2) : 'INF';
      console.log('    ' + S[i].padEnd(9) + Y[i].toFixed(1).padStart(9) + interm.toFixed(1).padStart(12)
        + net.toFixed(1).padStart(11) + f[i].toFixed(2).padStart(15) + r.padStart(15));
    }
    const cost = inputVal + wage;
    console.log(`  总工资（消费者唯一收入来源） = ${Math.round(wage).toLocaleString()} 元/tick`);
    console.log(`  净供给按 P_init 全额售出的收入 = ${Math.round(rev).toLocaleString()} 元/tick`);
    console.log(`  中间投入成本                 = ${Math.round(inputVal).toLocaleString()} 元/tick`);
    console.log(`  总成本（中间投入 + 工资）     = ${Math.round(cost).toLocaleString()} 元/tick`);
    console.log(`  ⇒ 消费者可用的货币只有工资 ${Math.round(wage).toLocaleString()} 元，`);
    console.log(`     买下当期全部净供给需要 ${Math.round(rev).toLocaleString()} 元，缺口 ${Math.round(rev - wage).toLocaleString()} 元/tick`);
    console.log(`  ⇒ 全局平均利润率（成本基）= ${(((rev - cost) / cost) * 100).toFixed(2)}%（契约 §3.4 声称应为 +20%）`);
  }

  const t0 = Date.now();
  for (let t = 0; t < TICKS; t++) {
    tick(M);
    if ([1, 52, 260, 520, 1000, 5000, TICKS - 1].includes(t)) {
      const r = M.hist[M.hist.length - 1];
      const lv = r.levels.reduce((a, b) => a + b, 0);
      console.log(`  tick ${String(M.tick).padStart(6)} pop=${Math.round(M.pop).toLocaleString().padStart(11)} GDPdoc=${Math.round(r.gdpDoc).toLocaleString().padStart(16)} 现金池=${Math.round(r.cashTotal).toLocaleString().padStart(16)} 队列=${String(r.queueLen).padStart(4)} 总级=${lv.toFixed(0).padStart(6)} 最小配给=${r.allocMin.toFixed(3)} 加权利润率=${(r.marginWavg * 100).toFixed(1)}%`);
    }
  }
  console.log(`  耗时 ${Date.now() - t0} ms`);
  return M;
}

function report(M) {
  const h = M.hist, last = h[h.length - 1];
  console.log('\n--- 终态 ---');
  console.log('商品        P/P_cost     S/a       margin    级数        现金池        实购');
  for (let i = 0; i < N; i++) {
    console.log('  ' + S[i].padEnd(8) + (last.P[i] / PCOST[i]).toFixed(3).padStart(9)
      + (last.S[i] / Math.max(M.a[i], 1e-12)).toFixed(3).padStart(10)
      + (last.margin[i] * 100).toFixed(1).padStart(10) + '%'
      + last.levels[i].toFixed(1).padStart(8) + Math.round(M.cash[i]).toLocaleString().padStart(16)
      + last.bought[i].toFixed(1).padStart(11));
  }
  console.log('  消费组满足度：' + last.sat.map((s, i) => `${GROUPS[i].name}=${(s * 100).toFixed(1)}%`).join(' '));
  console.log(`  人口 ${Math.round(M.pop).toLocaleString()}  总级数 ${last.levels.reduce((a, b) => a + b, 0).toFixed(1)}  clamp 累计 ${M.clampTicks.reduce((a, b) => a + b, 0)}  queueWarn ${M.qWarn}`);

  const res = assess(M);
  if (!res) { console.log('  周期不足 200，未做 §8.4 判定'); return null; }
  console.log('\n--- §8.4 验收判据（末 2,000 周期）---');
  const yn = (b) => (b ? '通过' : '未通过');
  console.log(`A1 价格合法带/贴边<5%       ${yn(res.A1.pass)}  max|ln(P/Pc)|=${Math.max(...res.A1.maxLog).toFixed(3)}（限 ${Math.log(5).toFixed(3)}）最大贴边率=${(Math.max(...res.A1.clampShare) * 100).toFixed(2)}%`);
  console.log(`A2 margin∈[-10%,25%] 均≤15% ${yn(res.A2.pass)}  min=${(res.A2.mMin * 100).toFixed(2)}% max=${(res.A2.mMax * 100).toFixed(2)}% 加权=${(res.A2.wAvg * 100).toFixed(2)}%`);
  console.log(`A3 均衡点不漂移             ${yn(res.A3.pass)}  d(P/Pc)=${res.A3.dPR.toExponential(2)}(限1e-4) d(S/a)=${res.A3.dSa.toExponential(2)}(限2e-4)`);
  console.log(`A4 GDP>0 且斜率>=0          ${yn(res.A4.pass)}  全程正=${res.A4.allPos} 最低GDP=${Math.round(res.A4.minGdp).toLocaleString()} 斜率=${res.A4.slope.toExponential(2)}`);
  console.log(`A5 无爆炸坍缩+人口∈[0,5%]    ${yn(res.A5.pass)}  人口年化=${(res.A5.popAnnual * 100).toFixed(2)}% 总级数 ${res.A5.lv0.toFixed(0)}->${res.A5.lvT.toFixed(0)}`);
  console.log(`A6 建造队列不饥饿           ${yn(res.A6.pass)}  队列非空占比=${(res.A6.share * 100).toFixed(1)}% queueWarn=${res.A6.qWarn}`);

  const l5 = h.slice(-500);
  console.log('\n--- 末 500 周期均值 ---');
  for (let i = 0; i < N; i++) {
    const mp = l5.reduce((s, r) => s + r.P[i], 0) / l5.length;
    const ms = l5.reduce((s, r) => s + r.S[i], 0) / l5.length;
    const md = l5.reduce((s, r) => s + r.D[i], 0) / l5.length;
    const mb = l5.reduce((s, r) => s + r.bought[i], 0) / l5.length;
    console.log(`  ${S[i].padEnd(8)} P=${mp.toFixed(1).padStart(9)}(${(mp / PCOST[i]).toFixed(3)}x)  S=${ms.toFixed(1).padStart(9)}  D=${md.toFixed(1).padStart(9)}  S/D=${(ms / Math.max(md, 1e-9)).toFixed(3)}  实购=${mb.toFixed(1).padStart(9)}`);
  }

  console.log('\n--- 会计闭环诊断（末 500 周期均值）---');
  const avg = (k) => l5.reduce((s, r) => s + r[k], 0) / l5.length;
  console.log(`  总工资（消费者唯一收入来源）  = ${Math.round(avg('wage')).toLocaleString()} 元/tick`);
  console.log(`  消费者实际支出               = ${Math.round(avg('spend')).toLocaleString()} 元/tick`);
  console.log(`  中间投入支付总额             = ${Math.round(avg('inputTotal')).toLocaleString()} 元/tick`);
  console.log(`  建筑收入总额                 = ${Math.round(avg('revTotal')).toLocaleString()} 元/tick`);
  console.log(`  总成本（中间投入 + 工资）     = ${Math.round(avg('costTotal')).toLocaleString()} 元/tick`);
  console.log(`  行业加权平均利润率           = ${(avg('marginWavg') * 100).toFixed(2)}%`);
  console.log(`  建筑现金池总额               = ${Math.round(avg('cashTotal')).toLocaleString()} 元`);
  console.log(`  消费预算压缩系数             = ${avg('spendScale').toFixed(4)}（<1 表示工资买不起当期供给）`);
  console.log(`  平均雇工率                   = ${(avg('hireAvg') * 100).toFixed(1)}%`);
  console.log(`  GDP_doc / flow / prod        = ${Math.round(avg('gdpDoc')).toLocaleString()} / ${Math.round(avg('gdpFlow')).toLocaleString()} / ${Math.round(avg('gdpProd')).toLocaleString()}`);
  return res;
}

// ---------- 入口 ----------
const MODES = [];
if (process.argv.includes('--sweep')) {
  MODES.push({ label: '情形 1：无预算约束（消费者可支出超过工资）+ 现金池 x1', red: 1.15, sub: 0.05, tier: 7, budget: 0, cash: 1 });
  MODES.push({ label: '情形 2：有预算约束（工资是唯一资金来源）+ 现金池 x1', red: 1.15, sub: 0.05, tier: 7, budget: 1, cash: 1 });
  MODES.push({ label: '情形 3：有预算约束 + 现金池 x100', red: 1.15, sub: 0.05, tier: 7, budget: 1, cash: 100 });
  MODES.push({ label: '情形 4：有预算约束 + 现金池 x1000', red: 1.15, sub: 0.05, tier: 7, budget: 1, cash: 1000 });
  MODES.push({ label: '情形 5：无预算约束 + 现金池 x100', red: 1.15, sub: 0.05, tier: 7, budget: 0, cash: 100 });
} else {
  MODES.push({ label: '情形：契约默认参数', red: OVER.red, sub: OVER.sub, tier: OVER.tier, budget: OVER.budget, cash: OVER.cash });
}

const all = {};
for (const m of MODES) {
  OVER.red = m.red; OVER.sub = m.sub; OVER.tier = m.tier; OVER.budget = m.budget; OVER.cash = m.cash;
  const M = run(m.label);
  all[m.label] = report(M);
}

console.log('\n=== 汇总 ===');
const ids = ['A1', 'A2', 'A3', 'A4', 'A5', 'A6'];
for (const [k, v] of Object.entries(all)) {
  if (!v) { console.log(`${k}\n  （周期不足，未判定）`); continue; }
  console.log(`${k}\n  通过 ${ids.filter((a) => v[a].pass).length}/6：${ids.filter((a) => v[a].pass).join(' ') || '（无）'}   未通过：${ids.filter((a) => !v[a].pass).join(' ') || '（无）'}`);
}
fs.writeFileSync('out/core_sim_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/core_sim_probe.txt');
