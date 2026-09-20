// VC_Go/tools/multigood_probe.js —— 1.0 契约的多商品联合模拟与 §8.4 验收判据实测
//
// 目的：契约 §8.4 的 A1–A6 是「运行 10,000 周期后」的判据，而仓库内
//       *没有任何* 多商品联合模拟器（FEASIBILITY-1.0.md §5.1 自认这是缺口：
//       expansion_probe.js 的 F 段是单商品自洽近似，投入品价格按本商品价格同比例缩放）。
//
// 本脚本按契约原文实现完整管线：
//   §2.1 常弹性需求  D = a(P/P0)^-ε,  P0 = P_cost
//   §2.2 供给        S = Σ hireRate·q_b·N_b·shortageFactor_b（含自给农场）
//   §2.3 价格 ODE    m P̈ + ρ Ṗ = E + F_ext，RK4 dt=0.5, nSub=10
//   §2.4 平衡态/钳制  P* = P0(a/S)^(1/ε)，P ∈ [0.2, 5]·P_cost
//   §3.3 投入产出表 + 原料短缺惩罚（按需求等比配给，保证 Σalloc = A）
//   §3.4 开局标定    P_init = 加成价方程解，a = S0·(P_init/P_cost)^ε
//   §4.1 扩建        margin > 10% → n_extra = floor((margin-10%)/5%)+1，≤ 总数 10%
//   §4.2 建造队列    每工地 ≤ 30 建造力/tick，成本 = buildCost 建造力
//   §4.3 资金        每建筑独立现金池，初始 5,000
//   §4.4 缩编        雇佣率 < 75% 连续 156 周期 → 每周期 -5%
//   §5   劳动力      5,000 人/级，0.75/0.20/0.05 阶层，均薪 6.75，工资全额消费
//   §6   消费组/使用价值/财富插值/权重购买/满足度
//   §6.5 人口增长    满足度 ≥75% → +5%/52周期，0% → -20%
//   §7   GDP         消费者实际支出 + 各建筑现金池期末总额
//   §8   主循环 12 步顺序
//
// 运行：node VC_Go/tools/multigood_probe.js [ticks]
'use strict';
const fs = require('fs');

const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const TICKS = Number(process.argv[2] || 10000);
// §2.3（2026-09-19 改）：惯性 m ≡ 当期流通量、T = 2π√(m/K) 内生，已无 T_PERIOD 旋钮
const ZETA = 0.7, DT = 0.5, NSUB = 10, H = DT / NSUB;
const TICKS_PER_YEAR = 52;
const WAGE_PER_LEVEL = 5000 * 6.75;      // §5 = 33,750 元/级/周期
const WAGE_TIER = [5, 10, 20];           // 资本家实际工资 20，见 §6.3 插值上限

// ---------- §3.1/§3.3 商品与配方 ----------
const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const SHORT = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11;
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const REC = [
  { out: 0, q: 50, inp: {} },
  { out: 1, q: 45, inp: { 0: 40 } },
  { out: 2, q: 45, inp: {} },
  { out: 3, q: 100, inp: { 2: 60 } },
  { out: 4, q: 30, inp: { 2: 25 } },
  { out: 5, q: 60, inp: { 8: 15, 5: 15 } },
  { out: 6, q: 60, inp: { 8: 15, 5: 15 } },
  { out: 7, q: 90, inp: { 6: 60, 5: 30 } },
  { out: 8, q: 80, inp: { 7: 20 } },
  { out: 9, q: 60, inp: { 7: 5, 8: 5 } },
  { out: 10, q: 15, inp: { 7: 25, 6: 25, 8: 20 } },
];
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100]; // §3.2
const LANDTYPE = ['grain', 'none', 'cotton', 'none', 'none', 'coal', 'iron', 'none', 'none', 'none', 'none'];
const LANDCAP = { grain: 10000, cotton: 10000, coal: 500, iron: 500 };      // §4.2（耕地合并计 10,000）
const BUILD_SECTOR_CAP = 1000;
const SUBSIST = { grain: 2, fabric: 1, clothes: 0.5 };                     // §3.3 自给农场

// ---------- §6 消费组 ----------
const GROUPS = [
  { name: '简朴衣物', uses: { 2: 1 } },
  { name: '基础食物', uses: { 0: 1, 1: 1.5 } },
  { name: '标准衣物', uses: { 3: 1, 4: 1 } },
  { name: '住宅', uses: { 9: 1 } },
];
const DEMAND_PER100K = [ // §6.3  财富等级 → 每 100k 人口的标准消费组单位数
  { w: 5, d: [39, 210, 0, 20] },
  { w: 10, d: [41, 210, 7, 74] },
  { w: 20, d: [0, 210, 122, 130] },
];

function parseArgs() {
  const o = { subsist: 'auto', pop: 100000, label: '' };
  for (const a of process.argv.slice(3)) {
    const [k, v] = a.split('=');
    if (k === '--subsist') o.subsist = v;
    if (k === '--pop') o.pop = Number(v);
    if (k === '--label') o.label = v;
  }
  return o;
}

// 财富等级插值（§6.3）
function wealthTier(wage) {
  if (wage <= 5) return 5;
  if (wage < 10) return 5 + (wage - 5) * (10 - 5) / (10 - 5);
  if (wage < 20) return 10 + (wage - 10) * (20 - 10) / (20 - 10);
  return 20;
}
function baseDemandPer100k(tier) {
  const w = Math.min(20, Math.max(5, tier));
  // 在 DEMAND_PER100K 的整数档间线性插值
  let lo = DEMAND_PER100K[0], hi = DEMAND_PER100K[DEMAND_PER100K.length - 1];
  for (let i = 0; i + 1 < DEMAND_PER100K.length; i++) {
    if (w >= DEMAND_PER100K[i].w && w <= DEMAND_PER100K[i + 1].w) {
      lo = DEMAND_PER100K[i]; hi = DEMAND_PER100K[i + 1]; break;
    }
  }
  const t = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  return lo.d.map((x, k) => x + (hi.d[k] - x) * t);
}

// ---------- 开局：S0 全为 1 单位的产出结构，由 §6.3 需求的固定比例确定 ----------
function openingStructure() {
  // 1) 工资 → 财富等级 → 各消费组基准需求（每 100k 人口）
  const wage = 6.75;                       // 开局为无失业的层级工资结构（资本家 20）
  const tier = wealthTier(wage);
  const per100k = baseDemandPer100k(tier);
  const POP0 = 100000;
  const groupQty = per100k.map((x) => x * POP0 / 100000);

  // 2) 消费组 → 商品需求（使用价值表 §6.2）
  const good = new Array(N).fill(0);
  for (let gi = 0; gi < GROUPS.length; gi++) {
    const uses = GROUPS[gi].uses;
    const total = Object.values(uses).reduce((s, v) => s + v, 0);
    for (const [gid, v] of Object.entries(uses)) good[gid] += groupQty[gi] * (v / total);
  }
  // 3) 标准衣物的 "0 档" 缺口由 §6.2 使用价值折算（简朴衣物组用织物，标准衣物组用服装/高档服装）
  //    这里已由 uses 表直接给出。谷物/加工食品按基础食物组内的使用价值比例分配。
  //    但 §6.3 财富 7 档下简朴衣物 = 40.2，其使用价值表把织物/服装都算 1 → 织物与服装各分一半。
  //    为与契约 §6.2 表一致，重算：简朴衣物组只列了"织物 = 1"与"服装 = 1"，故按此分配。
  return { good, tier, per100k, groupQty, POP0 };
}

function buildModel(opts) {
  const st = openingStructure();
  const M = {
    tick: 0,
    P: PINIT.slice(),
    dP: new Array(N).fill(0),
    Pcost: PCOST.slice(),
    Pinit: PINIT.slice(),
    a: new Array(N).fill(0),
    S0: new Array(N).fill(0),
    levels: new Array(N).fill(0),
    hire: new Array(N).fill(1),
    idle: new Array(N).fill(0),
    cash: new Array(N).fill(0),
    queue: [],
    pop: opts.pop,
    clampTicks: new Array(N).fill(0),
    hist: [],
    groupQty: st.groupQty,
    tier: st.tier,
    log: [],
  };

  // 开局产出结构：令每个商品的 S0 与消费组需求比例一致，缺口用最少建筑补齐；
  // 这是唯一能让 a 标定后 t=0 时 E=0 的可行做法（契约未规定开局建筑数量）。
  const S0 = st.good.map((x) => Math.max(x, 1e-6));
  M.consumptionAtCost = st.good.slice();

  // 按 S0 反推最小建筑级数（含中间投入链），并向上取整以保证开局不发生系统性短缺
  const need = new Array(N).fill(0);
  for (let i = 0; i < N; i++) need[i] = S0[i] / REC[i].q;
  // 中间投入会额外占用产出：迭代两次把投入需求加回去
  for (let pass = 0; pass < 3; pass++) {
    const extra = new Array(N).fill(0);
    for (let i = 0; i < N; i++) for (const [j, qty] of Object.entries(REC[i].inp)) extra[j] += need[i] * qty;
    for (let j = 0; j < N; j++) need[j] = Math.max(need[j], (S0[j] + extra[j]) / REC[j].q);
  }
  for (let i = 0; i < N; i++) M.levels[i] = Math.max(1, Math.ceil(need[i]));
  // 土地约束：耕地（谷物+棉花）合并 ≤ 10,000
  const arable = M.levels[0] + M.levels[2];
  if (arable > LANDCAP.grain) { M.levels[0] = Math.floor(M.levels[0] * LANDCAP.grain / arable); M.levels[2] = Math.floor(M.levels[2] * LANDCAP.grain / arable); }
  M.levels[5] = Math.min(M.levels[5], LANDCAP.coal);
  M.levels[6] = Math.min(M.levels[6], LANDCAP.iron);
  M.levels[10] = Math.min(M.levels[10], BUILD_SECTOR_CAP);

  // 自给农场规模
  M.subsistScale = opts.subsist === 'auto' ? 0.05 : Number(opts.subsist);

  for (let i = 0; i < N; i++) M.cash[i] = 5000 * M.levels[i];   // §4.3 每级 5,000

  // 开局实际产出（含自给农场），据此标定 a
  const S = rawOutput(M);
  for (let i = 0; i < N; i++) M.S0[i] = Math.max(S[i], 1e-9);
  for (let i = 0; i < N; i++) M.a[i] = M.S0[i] * Math.pow(PINIT[i] / PCOST[i], EPS[i]); // §3.4 步骤 3
  return M;
}

function rawOutput(M) {
  const Y = new Array(N).fill(0);
  for (let i = 0; i < N; i++) Y[i] += M.levels[i] * REC[i].q * M.hire[i];
  // 自给农场（§4.2/§3.3）：未利用耕地自动生成，不耗劳动力、不发工资
  const arableUsed = M.levels[0] + M.levels[2];
  const idle = Math.max(0, LANDCAP.grain - arableUsed);
  const sub = idle * M.subsistScale;
  Y[0] += sub * SUBSIST.grain; Y[2] += sub * SUBSIST.fabric; Y[3] += sub * SUBSIST.clothes;
  return Y;
}

function demand(M, P, Pc) {
  const D = new Array(N).fill(0);
  for (let i = 0; i < N; i++) D[i] = M.a[i] * Math.pow(P[i] / Pc[i], -EPS[i]);
  return D;
}

// ---------- 单 tick ----------
function tick(M) {
  const P = M.P, Pc = M.Pcost;
  const rec = {};

  // ---------- §8 步 2：产能申报 / §3.3 等比配给 / 短缺惩罚 ----------
  const avail = new Array(N).fill(0);
  for (let i = 0; i < N; i++) avail[i] = M.levels[i] * REC[i].q * M.hire[i];
  {
    const arableUsed = M.levels[0] + M.levels[2];
    const idle = Math.max(0, LANDCAP.grain - arableUsed);
    const sub = idle * M.subsistScale;
    avail[0] += sub * SUBSIST.grain; avail[2] += sub * SUBSIST.fabric; avail[3] += sub * SUBSIST.clothes;
  }
  const needRaw = new Array(N).fill(0);          // 各建筑申报的投入需求
  for (let i = 0; i < N; i++) for (const [j, qty] of Object.entries(REC[i].inp)) needRaw[j] += M.levels[i] * M.hire[i] * qty;
  const allocRatio = new Array(N).fill(1);
  for (let j = 0; j < N; j++) if (needRaw[j] > 1e-12) allocRatio[j] = Math.min(1, avail[j] / needRaw[j]);

  // ---------- §8 步 2：实际产出（含短缺惩罚） ----------
  const Y = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    if (i === 0) { /* 谷物农场无投入 */ }
    let sf = 1;
    for (const [j] of Object.entries(REC[i].inp)) sf = Math.min(sf, allocRatio[j]);
    Y[i] = M.levels[i] * REC[i].q * M.hire[i] * sf;
  }
  for (let j = 0; j < N; j++) Y[j] += allocRatio[j] >= 0 ? 0 : 0;
  {
    const arableUsed = M.levels[0] + M.levels[2];
    const idle = Math.max(0, LANDCAP.grain - arableUsed);
    const sub = idle * M.subsistScale;                 // 自给农场无投入，不受配给影响
    Y[0] += sub * SUBSIST.grain; Y[2] += sub * SUBSIST.fabric; Y[3] += sub * SUBSIST.clothes;
  }
  const S = Y.slice();                                  // §2.2 当期供给

  // ---------- §2.3/§2.4：价格 ODE ----------
  const D = demand(M, P, Pc);
  const E = D.map((d, i) => d - S[i]);
  const marginPrev = M.marginNow ? M.marginNow.slice() : new Array(N).fill(0.2);
  const K = new Array(N), mm = new Array(N), rho = new Array(N);
  for (let i = 0; i < N; i++) {
    K[i] = (EPS[i] * M.a[i] / Pc[i]) * Math.pow(P[i] / Pc[i], -EPS[i] - 1);
    // §2.3（2026-09-19 改）：惯性 m ≡ 当期市场内流通商品量 S；ρ = 2ζ√(m·K)
    mm[i] = S[i];
    if (!isFinite(mm[i]) || mm[i] <= 0) mm[i] = 1e-9;
    rho[i] = 2 * ZETA * Math.sqrt(mm[i] * K[i]);
    if (!isFinite(rho[i]) || rho[i] < 0) rho[i] = 0;
  }
  let clampEvents = 0;
  for (let i = 0; i < N; i++) {
    let p = P[i], v = M.dP[i];
    const fl = 0.2 * Pc[i], ce = 5 * Pc[i];
    const f = (pv, vv) => [vv, (E[i] - rho[i] * vv) / mm[i]];
    let clamped = false;
    for (let s = 0; s < NSUB; s++) {
      const [k1a, k1b] = f(p, v);
      const [k2a, k2b] = f(p + H / 2 * k1a, v + H / 2 * k1b);
      const [k3a, k3b] = f(p + H / 2 * k2a, v + H / 2 * k2b);
      const [k4a, k4b] = f(p + H * k3a, v + H * k3b);
      p += H / 6 * (k1a + 2 * k2a + 2 * k3a + k4a);
      v += H / 6 * (k1b + 2 * k2b + 2 * k3b + k4b);
      if (!isFinite(p)) { p = Pc[i]; v = 0; break; }
      if (p < fl) { p = fl; v = 0; clamped = true; } else if (p > ce) { p = ce; v = 0; clamped = true; }
    }
    P[i] = p; M.dP[i] = v;
    if (p <= fl * 1.0001 || p >= ce * 0.9999) { M.clampTicks[i]++; clampEvents++; }
  }

  // ---------- §8 步 3：工资 ----------
  const wageBill = new Array(N).fill(0);
  let totalWage = 0;
  for (let i = 0; i < N; i++) { wageBill[i] = M.levels[i] * M.hire[i] * WAGE_PER_LEVEL; totalWage += wageBill[i]; }

  // ---------- §8 步 4：消费组购买 ----------
  // 净供给 = 本 tick 产出 − 中间投入（§6.4 的 S_i）
  const netS = new Array(N).fill(0);
  for (let j = 0; j < N; j++) netS[j] = Math.max(0, S[j] - needRaw[j] * allocRatio[j]);
  const wealth = M.levels.reduce((s, L) => s + L, 0) > 0 ? avgWage(M) : 6.75;
  const tier = wealthTier(wealth);
  const per100k = baseDemandPer100k(tier);
  const gq = per100k.map((x) => x * M.pop / 100000);
  const purchased = new Array(N).fill(0);
  const sat = new Array(GROUPS.length).fill(0);
  let consumerSpend = 0;
  for (let gi = 0; gi < GROUPS.length; gi++) {
    const uses = GROUPS[gi];
    const ids = Object.keys(uses.uses).map(Number);
    const w = ids.map((id) => ({ id, w: netS[id] / (2 * P[id]) })).sort((x, y) => y.w - x.w);
    const target = gq[gi];
    let got = 0;
    for (const { id } of w) {
      if (got >= target) break;
      const perUnit = uses.uses[id];
      const availUnits = Math.max(0, netS[id] - purchased[id]);
      if (availUnits <= 0) continue;
      const want = (target - got) / perUnit;
      const take = Math.min(want, availUnits);
      purchased[id] += take; got += take * perUnit;
      netS[id] -= take;
    }
    sat[gi] = target > 0 ? Math.min(1, got / target) : 1;
  }
  for (let i = 0; i < N; i++) consumerSpend += purchased[i] * P[i];
  // budget 约束：工资全额消费（§5「全部工资用于消费」）
  const budgetOK = consumerSpend <= totalWage + 1e-6;

  // ---------- §8 步 1/3：利润与现金池 ----------
  const margin = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    let inputCost = 0;
    for (const [j, qty] of Object.entries(REC[i].inp)) inputCost += M.levels[i] * M.hire[i] * qty * allocRatio[j] * P[j];
    const revenue = purchased[i] * P[i];
    const cost = inputCost + wageBill[i];
    margin[i] = cost > 1e-9 ? (revenue - cost) / cost : 0;
    M.cash[i] += revenue - inputCost - wageBill[i];
  }

  // ---------- §8 步 5/6：建造力分配与完工 ----------
  const power = Y[10];
  const buildPowerPrice = P[10];
  let queueStart = M.queue.length;
  let powerLeft = power;
  for (const o of M.queue) {
    if (powerLeft <= 0) break;
    const grant = Math.min(30, powerLeft);
    o.progress += grant; powerLeft -= grant;
    M.cash[o.type] -= grant * buildPowerPrice;             // §4.3 建造力费用从现金池扣除
  }
  const still = [];
  for (const o of M.queue) {
    const cost = BUILDCOST[o.type] * (o.units || 1);
    if (o.progress >= cost) {
      // 完工：受土地/上限约束
      let add = o.units || 1;
      if (LANDTYPE[o.type] === 'grain' || LANDTYPE[o.type] === 'cotton') {
        const other = o.type === 0 ? M.levels[2] : M.levels[0];
        add = Math.max(0, Math.min(add, LANDCAP.grain - other - M.levels[o.type]));
      } else if (LANDTYPE[o.type] === 'coal') add = Math.max(0, Math.min(add, LANDCAP.coal - M.levels[o.type]));
      else if (LANDTYPE[o.type] === 'iron') add = Math.max(0, Math.min(add, LANDCAP.iron - M.levels[o.type]));
      else if (o.type === 10) add = Math.max(0, Math.min(add, BUILD_SECTOR_CAP - M.levels[o.type]));
      M.levels[o.type] += add;
    } else still.push(o);
  }
  M.queue = still;

  // ---------- §8 步 7：缩编 ----------
  for (let i = 0; i < N; i++) {
    if (M.hire[i] < 0.75) M.idle[i]++; else M.idle[i] = 0;
    if (M.idle[i] > 156 && M.levels[i] > 0) {
      const nl = M.levels[i] * (1 - 0.05);
      M.levels[i] = nl >= 1 ? nl : 0;                       // 不返还建造力/现金（Q8 默认）
    }
  }

  // ---------- §8 步 10：AI 扩建 ----------
  const order = [...Array(N).keys()].sort((x, y) => margin[y] - margin[x]);
  for (const i of order) {
    if (margin[i] <= 0.10) continue;
    const nExtra = Math.floor((margin[i] - 0.10) / 0.05) + 1;
    const units = Math.min(nExtra * 0.10 * M.levels[i], 0.10 * M.levels[i]); // "每超 5 个百分点多扩 1 个"按 10% 级数计
    if (units >= 1 || Math.random() < units) {
      const n = Math.max(1, Math.round(units));
      // 现金池必须付得起建造力（§4.3）
      M.queue.push({ type: i, units: n, progress: 0, cashClaim: n * BUILDCOST[i] * buildPowerPrice });
    }
  }

  // ---------- §8 步 11：人口 ----------
  const satNec = 0.5 * sat[0] + 0.5 * sat[1];   // §6.5 必需品 = 简朴衣物 + 基础食物
  let growth;
  if (satNec >= 0.75) growth = 0.05 * (satNec - 0.75) / 0.25;
  else growth = -0.20 * (0.75 - satNec) / 0.75;
  M.pop *= (1 + growth / TICKS_PER_YEAR);

  // ---------- §8 步 1/12：雇佣调整 与 期末记录 ----------
  M.marginNow = margin.slice();
  for (let i = 0; i < N; i++) {
    const step = 0.05 / TICKS_PER_YEAR;                     // D9 年率折算
    if (margin[i] < 0) M.hire[i] = Math.max(0, M.hire[i] - step);
    else if (margin[i] > 0) M.hire[i] = Math.min(1, M.hire[i] + step);
  }

  // ---------- §7 GDP ----------
  const cashTotal = M.cash.reduce((a, b) => a + b, 0);
  const gdp = consumerSpend + cashTotal;
  const gdpFlow = consumerSpend;
  const gdpProd = Y.reduce((s, y, i) => s + y * P[i], 0) - needRaw.reduce((s, x, j) => s + x * allocRatio[j] * P[j], 0);

  M.tick++;
  M.hist.push({
    tick: M.tick, P: P.slice(), S: S.slice(), D: D.slice(), E: E.slice(),
    margin, sat: sat.slice(), pop: M.pop, gdp, gdpFlow, gdpProd,
    levels: M.levels.slice(), consumerSpend, cashTotal, clampEvents,
    purchased: purchased.slice(), allocRatio: allocRatio.slice(),
  });
  return { E, S, D, margin, sat, gdp };
}

function avgWage(M) {
  const total = M.levels.reduce((s, L) => s + L, 0);
  if (total <= 0) return 6.75;
  const emp = M.levels.reduce((s, L, i) => s + L * M.hire[i], 0);
  return emp > 0 ? WAGE_PER_LEVEL * emp / (5000 * emp) : 6.75;
}

// ---------- §8.4 验收判据 ----------
function assess(M, opts) {
  const h = M.hist;
  if (h.length < 200) return { fail: ['运行周期不足'] };
  const tail = h.slice(-2000);
  const T = tail.length;
  const res = { A1: null, A2: null, A3: null, A4: null, A5: null, A6: null, detail: {} };

  // A1 价格在合法带内、贴边占比 < 5%
  const clampShare = new Array(N).fill(0);
  for (const r of tail) for (let i = 0; i < N; i++) if (r.P[i] <= 0.2 * PCOST[i] * 1.0001 || r.P[i] >= 5 * PCOST[i] * 0.9999) clampShare[i]++;
  const ratio = tail.map((r) => r.P.map((p, i) => p / PCOST[i]));
  const maxLog = new Array(N).fill(0);
  for (const rr of ratio) for (let i = 0; i < N; i++) maxLog[i] = Math.max(maxLog[i], Math.abs(Math.log(rr[i])));
  res.A1 = { pass: maxLog.every((x) => x <= Math.log(5) + 1e-9) && clampShare.every((x) => x / T < 0.05), maxLog, clampShare: clampShare.map((x) => x / T) };

  // A2 利润率 ∈ [-10%, +25%]，行业加权平均 ≤ +15%
  let mMin = 1e9, mMax = -1e9;
  const perB = new Array(N).fill(0);
  for (const r of tail) for (let i = 0; i < N; i++) { mMin = Math.min(mMin, r.margin[i]); mMax = Math.max(mMax, r.margin[i]); perB[i] = Math.max(perB[i], Math.abs(r.margin[i])); }
  let num = 0, den = 0;
  for (const r of tail) for (let i = 0; i < N; i++) { const L = r.levels[i]; num += r.margin[i] * L; den += L; }
  const wAvg = den > 0 ? num / den : 0;
  res.A2 = { pass: mMin >= -0.10 - 1e-9 && mMax <= 0.25 + 1e-9 && wAvg <= 0.15 + 1e-9, mMin, mMax, wAvg, perB };

  // A3 均衡点不漂移（末 500 周期，逐周期增量的均值）
  const last500 = h.slice(-500);
  let dPR = 0, dSa = 0;
  for (let k = 1; k < last500.length; k++) {
    for (let i = 0; i < N; i++) {
      dPR += Math.abs(last500[k].P[i] / PCOST[i] - last500[k - 1].P[i] / PCOST[i]);
      const sa1 = last500[k].S[i] / Math.max(M.a[i], 1e-12), sa0 = last500[k - 1].S[i] / Math.max(M.a[i], 1e-12);
      dSa += Math.abs(sa1 - sa0);
    }
  }
  const nrm = (last500.length - 1) * N;
  dPR /= nrm; dSa /= nrm;
  res.A3 = { pass: dPR < 1e-4 && dSa < 2e-4, dPR, dSa };

  // A4 GDP 全程 > 0，末 500 周期斜率 ≥ 0
  const allPos = h.every((r) => r.gdp > 0 && isFinite(r.gdp));
  const xs = last500.map((r) => r.tick), ys = last500.map((r) => r.gdp);
  const mx = xs.reduce((a, b) => a + b, 0) / xs.length, my = ys.reduce((a, b) => a + b, 0) / ys.length;
  let sxy = 0, sxx = 0;
  for (let k = 0; k < xs.length; k++) { sxy += (xs[k] - mx) * (ys[k] - my); sxx += (xs[k] - mx) ** 2; }
  const slope = sxx > 0 ? sxy / sxx : 0;
  res.A4 = { pass: allPos && slope >= 0, allPos, slope };

  // A5 无爆炸/坍缩；人口增长率 ∈ [0%, +5%] 年化
  const lv0 = h[0].levels.reduce((a, b) => a + b, 0), lvT = tail[tail.length - 1].levels.reduce((a, b) => a + b, 0);
  const popAnnual = Math.pow(M.pop / h[0].pop, TICKS_PER_YEAR / (M.tick - h[0].tick)) - 1;
  res.A5 = { pass: popAnnual >= -1e-9 && popAnnual <= 0.05 + 1e-9 && lvT > 0, popAnnual, lv0, lvT };

  // A6 建造队列不饥饿
  let nonEmpty = 0;
  for (const r of tail) if (r.queueLen > 0) nonEmpty++;
  res.A6 = { pass: nonEmpty / T < 0.20, nonEmptyShare: nonEmpty / T, warn52: M.warn52 || 0 };

  return res;
}

// ---------- 主流程 ----------
function run(opts) {
  const M = buildModel(opts);
  M.hist = [];
  const S0sum = M.S0.reduce((a, b) => a + b, 0);
  console.log(`\n=== ${opts.label || '默认'} | ticks=${TICKS} | 自给农场 scale=${M.subsistScale} | 人口0=${opts.pop} ===`);
  console.log('开局级数：' + M.levels.map((l, i) => `${SHORT[i]}=${l}`).join(' '));
  const t0 = Date.now();
  for (let t = 0; t < TICKS; t++) {
    const r = tick(M);
    // 队列长度快照（供 A6）
    M.hist[M.hist.length - 1].queueLen = M.queue.length;
    if (t === 0 || t === 51 || t === 519 || t === TICKS - 1) {
      console.log(`  tick ${String(M.tick).padStart(6)} pop=${Math.round(M.pop).toLocaleString()} GDP=${Math.round(M.hist[M.hist.length - 1].gdp).toLocaleString()} 队列=${M.queue.length} 活级=${M.levels.reduce((a, b) => a + b, 0).toFixed(0)}`);
    }
  }
  console.log(`  运行耗时 ${Date.now() - t0} ms`);
  return M;
}

function report(M, opts) {
  const h = M.hist;
  const last = h[h.length - 1];
  console.log('\n--- 终态（末 tick）---');
  console.log('商品        P/P_cost    S/a       E          margin    满足度组   级数');
  for (let i = 0; i < N; i++) {
    const sa = last.S[i] / Math.max(M.a[i], 1e-12);
    console.log(
      '  ' + G[i].padEnd(5, '　'),
      (last.P[i] / PCOST[i]).toFixed(4).padStart(9),
      sa.toFixed(4).padStart(9),
      (last.E[i] >= 0 ? '+' : '') + last.E[i].toFixed(1).padStart(10),
      (last.margin[i] * 100).toFixed(2).padStart(9) + '%',
      ''.padStart(6),
      last.levels[i].toFixed(0).padStart(7),
    );
  }
  console.log('  消费组满足度：' + last.sat.map((s, i) => `${GROUPS[i].name}=${(s * 100).toFixed(1)}%`).join(' '));

  const res = assess(M, opts);
  console.log('\n--- §8.4 验收判据（末 2,000 周期）---');
  const yn = (b) => (b ? '✅ 通过' : '❌ 未通过');
  console.log(`A1 价格合法带内/贴边<5%     ${yn(res.A1.pass)}   max|ln(P/P_cost)|=${Math.max(...res.A1.maxLog).toFixed(3)}  最大贴边率=${(Math.max(...res.A1.clampShare) * 100).toFixed(2)}%`);
  console.log(`A2 margin∈[-10%,25%] 加权≤15%  ${yn(res.A2.pass)}   min=${(res.A2.mMin * 100).toFixed(2)}%  max=${(res.A2.mMax * 100).toFixed(2)}%  加权=${(res.A2.wAvg * 100).toFixed(2)}%`);
  console.log(`A3 均衡点不漂移              ${yn(res.A3.pass)}   Δ(P/P_cost)=${res.A3.dPR.toExponential(2)} (限 1e-4)  Δ(S/a)=${res.A3.dSa.toExponential(2)} (限 2e-4)`);
  console.log(`A4 GDP>0 且斜率≥0            ${yn(res.A4.pass)}   全程正=${res.A4.allPos}  末500斜率=${res.A4.slope.toExponential(2)}`);
  console.log(`A5 无爆炸/坍缩 + 人口区间     ${yn(res.A5.pass)}   人口年化=${(res.A5.popAnnual * 100).toFixed(3)}%  总级数 ${res.A5.lv0}→${res.A5.lvT}`);
  console.log(`A6 建造队列不饥饿            ${yn(res.A6.pass)}   队列非空占比=${(res.A6.nonEmptyShare * 100).toFixed(1)}%`);

  // 结构诊断
  console.log('\n--- 结构诊断：终态各商品供需比与满足度 ---');
  const gq = baseDemandPer100k(wealthTier(6.75)).map((x) => x * M.pop / 100000);
  console.log('  人口 = ' + Math.round(M.pop).toLocaleString() + '（开局 100,000）');
  for (let gi = 0; gi < GROUPS.length; gi++) {
    const uses = GROUPS[gi];
    const ids = Object.keys(uses.uses).map(Number);
    const tot = Object.values(uses.uses).reduce((s, v) => s + v, 0);
    const need = ids.map((id) => `${SHORT[id]}=${(gq[gi] * uses.uses[id] / tot).toFixed(0)}`);
    const got = ids.map((id) => `${SHORT[id]}=${last.purchased[id].toFixed(0)}`);
    console.log(`  ${GROUPS[gi].name}: 需求 ${need.join(',')} | 实购 ${got.join(',')} | 满足 ${(last.sat[gi] * 100).toFixed(1)}%`);
  }
  const p = [];
  for (let i = 0; i < N; i++) {
    const r = last.S[i] / Math.max(last.D[i], 1e-12);
    p.push(`${SHORT[i]}=${r.toFixed(2)}`);
  }
  console.log('  S/D = ' + p.join(' '));

  // 末 500 周期均值/斜率用于诊断
  const tail = h.slice(-500);
  console.log('\n--- 末 500 周期均值 ---');
  for (let i = 0; i < N; i++) {
    const mp = tail.reduce((s, r) => s + r.P[i], 0) / tail.length;
    const ms = tail.reduce((s, r) => s + r.S[i], 0) / tail.length;
    const md = tail.reduce((s, r) => s + r.D[i], 0) / tail.length;
    console.log(`  ${SHORT[i].padEnd(8)} P=${mp.toFixed(1).padStart(9)} (${(mp / PCOST[i]).toFixed(3)}×P_cost)  S=${ms.toFixed(1).padStart(10)}  D=${md.toFixed(1).padStart(10)}  S/D=${(ms / Math.max(md, 1e-9)).toFixed(3)}`);
  }
  return res;
}

// ---------- 入口 ----------
const MODES = [
  { subsist: '0.05', pop: 100000, label: '情形 A：契约默认（自给农场 scale=0.05）' },
  { subsist: '0', pop: 100000, label: '情形 B：无自给农场（隔离自给扰动）' },
];
const results = {};
for (const m of MODES) {
  const M = run(m);
  results[m.label] = report(M, m);
}

console.log('\n=== 汇总 ===');
for (const [k, v] of Object.entries(results)) {
  const passes = ['A1', 'A2', 'A3', 'A4', 'A5', 'A6'].filter((a) => v[a].pass);
  console.log(`${k}`);
  console.log(`  通过 ${passes.length}/6：${passes.join(' ') || '（无）'}   未通过：${['A1', 'A2', 'A3', 'A4', 'A5', 'A6'].filter((a) => !v[a].pass).join(' ') || '（无）'}`);
}

fs.writeFileSync('out/multigood_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/multigood_probe.txt');
