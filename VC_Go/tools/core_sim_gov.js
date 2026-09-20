// VC_Go/tools/core_sim_gov.js —— 政府 + 资本模型的完整多商品联合模拟（v4，货币闭环修正）
//
// 用户方案（G1–G6）：
//   G1 政府抽取全部交易额的税率 t 作为税收
//   G2 建造力必须在市场内购买，最终购买方是政府；政府现金池支付 P_power
//   G3 建筑分所有权：政府 / 私有
//   G4 私有建筑的运营纯利划归金融区（资本收入）
//   G5 金融区是建筑：每级雇 1,000 人（阶层 75/20/5，均薪 6.75）；每级掌控 5 级其余建筑
//   G6 私有扩建的资金从建筑现金池划拨给政府现金池（政府代建）
//
// v4 的关键修正（v3 崩解的真实原因）：
//   C1 【货币闭环】v3 让政府把税收【全额】用于采购建造力，等于每 tick 从流通中
//      抽走与税收等量的货币，消费支出随之下降 → 通缩螺旋。
//      修正：建造力是唯一【没有最终消费者】的商品，其唯一买家是投资需求。
//      政府用税收采购建造力，并把建造力【卖给要扩建的企业】——企业付钱给政府，
//      政府用这笔钱继续采购。这样：税收 → 政府 → 建造部门（工资/投入）→ 回到流通，
//      同时企业用现金池买建造力 → 政府。总流通货币不变，且扩建有了真实资金约束。
//   C2 建造力的成交价 P_power 由市场决定，但政府采购价 = P_power（§3.1 的商品价）。
//   C3 企业向政府购买建造力的价格 = P_power × (1 + 政府加价率 μ)，μ 默认 0（平价代建）。
//   C4 扩建订单由"谁出钱"决定：政府建筑由政府出资，私有建筑由建筑/金融区现金池出资。
//
// 运行：node VC_Go/tools/core_sim_gov.js 10000
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const arg = (k, d) => { const a = process.argv.find((x) => x.startsWith('--' + k + '=')); return a ? Number(a.split('=')[1]) : d; };
const TICKS = Number(process.argv[2] || 10000);

const OVER = {
  tax: arg('tax', 0.10),
  govShare: arg('govshare', 0.70),
  ctrl: arg('ctrl', 5),
  pop: arg('pop', 5000000),
  k: arg('k', 0.936068),
  markup: arg('markup', 0.0),        // 政府向企业收取的建造力加价率
  buildFirst: arg('buildfirst', 1),
  powerTarget: arg('powertarget', 3),
  tier: 10, sub: 0.05,
};

// §2.3（2026-09-19 改）：惯性 m ≡ 当期流通量、T = 2π√(m/K) 内生，已无 T_PERIOD 旋钮
const ZETA = 0.7, NSUB = 10, H = 0.05, TPY = 52;
const WAGE_PER_LEVEL = 5000 * 6.75;
const FIN_WAGE_LEVEL = 1000 * (0.75 * 5 + 0.20 * 10 + 0.05 * 20);
const DECAY_WINDOW = 156, DECAY_RATE = 0.05, IDLE_HR = 0.75;
const EXPAND_TH = 0.10, HIRE_ANNUAL = 0.05, SITE_POWER = 30, EMA_TICKS = 12;

const S = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11, FIN = 11, NT = 12;
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const REC = [
  { q: 50, inp: [] }, { q: 45, inp: [[0, 40]] }, { q: 45, inp: [] }, { q: 100, inp: [[2, 60]] },
  { q: 30, inp: [[2, 25]] }, { q: 60, inp: [[8, 15], [5, 15]] }, { q: 60, inp: [[8, 15], [5, 15]] },
  { q: 90, inp: [[6, 60], [5, 30]] }, { q: 80, inp: [[7, 20]] }, { q: 60, inp: [[7, 5], [8, 5]] },
  { q: 15, inp: [[7, 25], [6, 25], [8, 20]] },
];
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100, 400];
const ARABLE = 10000, COALCAP = 500, IRONCAP = 500, POWERCAP = 1000, FINCAP = 1000;
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
  const d = interp(DEMAND_TABLE, tier).map((x) => x * pop / 100000 * OVER.k);
  const f = new Array(N).fill(0);
  for (let gi = 0; gi < GROUPS.length; gi++) {
    if (gi === 3) { f[9] += d[3]; continue; }
    const uses = GROUPS[gi].uses, s = uses.reduce((a, u) => a + u[1], 0);
    for (const [i, v] of uses) f[i] += d[gi] * v / s;
  }
  return f;
}
function capOf(i) {
  if (i === 0 || i === 2) return ARABLE;
  if (i === 5) return COALCAP; if (i === 6) return IRONCAP;
  if (i === 10) return POWERCAP; if (i === FIN) return FINCAP;
  return Infinity;
}

function createModel() {
  const M = {
    tick: 0, pop: OVER.pop,
    P: PINIT.slice(), dP: new Array(N).fill(0), Pcost: PCOST.slice(), a: new Array(N).fill(0),
    levels: new Array(NT).fill(0), govShare: new Array(NT).fill(0),
    hire: new Array(NT).fill(1), idle: new Array(NT).fill(0), marginEma: new Array(NT).fill(0.2),
    cash: new Array(NT).fill(0),
    govCash: 0, finCash: 0,
    queue: [], hist: [], clampTicks: new Array(N).fill(0),
    govTax: 0, govOperating: 0, govPowerSale: 0, govPowerBuy: 0,
    capitalIncome: 0, powerNeed: 0, overdraftTicks: 0,
    blockedBuilds: 0,
  };
  const f = finalDemand(M.pop, OVER.tier);
  const Y = B.map((row) => row.reduce((s, x, j) => s + x * f[j], 0));
  for (let i = 0; i < N; i++) M.levels[i] = Math.max(1, Math.min(Math.ceil(Y[i] / REC[i].q), capOf(i)));
  for (let pass = 0; pass < 80; pass++) {
    const Yc = new Array(N).fill(0);
    for (let i = 0; i < N; i++) Yc[i] = M.levels[i] * REC[i].q;
    let ch = false;
    for (let i = 0; i < N; i++) {
      let interm = 0; for (let j = 0; j < N; j++) interm += A[i][j] * Yc[j];
      const want = Math.min(Math.ceil((interm + f[i]) / 0.88 / REC[i].q), capOf(i));
      if (want > M.levels[i]) { M.levels[i] = want; ch = true; }
    }
    if (!ch) break;
  }
  {
    const tot = M.levels[0] + M.levels[2];
    if (tot > ARABLE) { const kk = ARABLE / tot; M.levels[0] *= kk; M.levels[2] *= kk; }
  }
  const other = M.levels.slice(0, N).reduce((a, b) => a + b, 0);
  M.levels[FIN] = Math.max(1, Math.ceil(other / OVER.ctrl));
  for (let i = 0; i < NT; i++) M.govShare[i] = i === FIN ? 0 : OVER.govShare;
  M.levelGov = M.levels.map((L, i) => L * M.govShare[i]);
  M.levelPriv = M.levels.map((L, i) => L * (1 - M.govShare[i]));
  const fv = f.reduce((s, x, i) => s + x * PCOST[i], 0);
  const estTax = fv * OVER.tax;
  M.govCash = Math.max(3 * estTax, 5000 * M.levelGov.reduce((a, b) => a + b, 0));
  M.finCash = Math.max(3 * estTax, 5000 * M.levelPriv[FIN]);
  for (let i = 0; i < N; i++) M.cash[i] = Math.max(5000 * M.levelPriv[i], f[i] * PCOST[i] * 0.5);
  const S0 = outputOf(M);
  const netS0 = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    let interm = 0; for (let j = 0; j < N; j++) interm += A[i][j] * S0[j];
    netS0[i] = Math.max(S0[i] - interm, 1e-9);
  }
  for (let i = 0; i < N; i++) M.a[i] = netS0[i] * Math.pow(PINIT[i] / PCOST[i], EPS[i]);
  return M;
}

function subsistence(M) {
  const idle = Math.max(0, ARABLE - M.levels[0] - M.levels[2]) * OVER.sub;
  const y = new Array(N).fill(0);
  for (const [i, c] of SUB_OUT) y[i] += idle * c;
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
  const P = M.P, Pc = M.Pcost, t = OVER.tax;

  // ===== 生产与配给 =====
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
    let sf = 1; for (const [j] of REC[i].inp) sf = Math.min(sf, allocRatio[j]);
    Y[i] = M.levels[i] * REC[i].q * M.hire[i] * sf + sub[i];
  }
  const usedInp = new Array(N).fill(0);
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) usedInp[j] += M.levels[i] * M.hire[i] * q * allocRatio[j];
  const netPool = Y.map((y, i) => Math.max(0, y - usedInp[i]));

  // ===== 价格 ODE =====
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

  // ===== 工资 =====
  const wageOrd = new Array(NT).fill(0);
  for (let i = 0; i < N; i++) wageOrd[i] = M.levels[i] * M.hire[i] * WAGE_PER_LEVEL;
  const wageFin = M.levels[FIN] * FIN_WAGE_LEVEL;
  let totalWage = wageFin; for (let i = 0; i < N; i++) totalWage += wageOrd[i];

  // ===== 消费 =====
  const gq = interp(DEMAND_TABLE, OVER.tier).map((x) => x * M.pop / 100000 * OVER.k);
  const stock = netPool.slice();
  const bought = new Array(N).fill(0);
  const sat = new Array(GROUPS.length).fill(0);
  const dispBudget = totalWage * (1 - t);
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
  let spendScale = 1;
  if (spendWant > dispBudget && spendWant > 1e-9) {
    spendScale = dispBudget / spendWant;
    for (let i = 0; i < N; i++) bought[i] *= spendScale;
    for (let gi = 0; gi < GROUPS.length; gi++) sat[gi] *= spendScale;
  }
  let spendNet = 0; for (let i = 0; i < N; i++) spendNet += bought[i] * P[i];
  const taxConsumer = spendNet * t;

  // ===== 收入与利润 =====
  const saleValue = new Array(N).fill(0);
  for (let i = 0; i < N; i++) saleValue[i] = (bought[i] + usedInp[i]) * P[i];
  const inputValue = new Array(N).fill(0);
  for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) inputValue[i] += M.levels[i] * M.hire[i] * q * allocRatio[j] * P[j];
  const taxIntermediate = inputValue.reduce((a, b) => a + b, 0) * t;
  let taxTotal = taxConsumer + taxIntermediate;

  const margin = new Array(NT).fill(0), profit = new Array(NT).fill(0);
  let capitalIncome = 0, govOperating = 0;
  for (let i = 0; i < N; i++) {
    const revenue = saleValue[i] / (1 + t);
    const costFull = inputValue[i] / Math.max(M.hire[i], 1e-9) + M.levels[i] * WAGE_PER_LEVEL;
    const revenueFull = revenue / Math.max(M.hire[i], 1e-9);
    margin[i] = costFull > 1e-9 ? (revenueFull - costFull) / costFull : 0;
    profit[i] = revenue - inputValue[i] - wageOrd[i];
    capitalIncome += profit[i] * (1 - M.govShare[i]);
    govOperating += profit[i] * M.govShare[i];
    M.cash[i] += profit[i];
  }
  {
    const revenue = capitalIncome;
    margin[FIN] = wageFin > 1e-9 ? (revenue - wageFin) / wageFin : 0;
    profit[FIN] = revenue - wageFin;
    M.finCash += profit[FIN];
  }
  M.capitalIncome = capitalIncome;
  M.govOperating = govOperating;

  // ===== 掌控上限 =====
  const otherLevels = M.levels.slice(0, N).reduce((a, b) => a + b, 0);
  const ctrlCap = M.levels[FIN] * OVER.ctrl;
  let ctrlUsed = otherLevels;

  // ===== 扩建意向 =====
  const powerPrice = P[10], powerOut = Y[10];
  const intent = [];
  let powerDemand = 0;
  for (let i = 0; i <= FIN; i++) {
    if (M.marginEma[i] <= EXPAND_TH) continue;
    const nExtra = Math.floor((M.marginEma[i] - EXPAND_TH) / 0.05) + 1;
    const lv = Math.max(M.levels[i], 1);
    const units = Math.max(1, Math.round(Math.min(nExtra * 0.10 * lv, 0.10 * lv)));
    powerDemand += BUILDCOST[i] * units;
    intent.push({ i, units, gov: i !== FIN && M.levelGov[i] >= M.levelPriv[i] });
  }
  M.powerNeed = powerDemand;
  // 建造力不足 → 政府优先投建造部门
  if (OVER.buildFirst && powerOut * OVER.powerTarget < powerDemand) {
    const have = intent.find((g) => g.i === 10);
    const units = Math.max(1, Math.ceil(Math.max(M.levels[10], 1) * 0.5));
    if (have) { have.units += units; have.gov = true; }
    else intent.unshift({ i: 10, units, gov: true });
  }
  intent.sort((x, y) => (x.i === 10 ? -1 : y.i === 10 ? 1 : 0));

  // ===== C1 货币闭环：政府采购建造力 → 转卖给扩建方 =====
  // 政府先用现金池向建造部门采购建造力（政府是市场买家）
  const buyBudget = Math.max(0, M.govCash);
  const powerBuy = Math.min(powerOut, buyBudget / Math.max(powerPrice, 1e-9));
  M.govCash -= powerBuy * powerPrice;
  M.govPowerBuy = powerBuy;
  // 建造部门收到货款（进入其现金池）
  M.cash[10] += powerBuy * powerPrice * (1 - t) / (1 + t);
  taxTotal += powerBuy * powerPrice * t / (1 + t);
  // 政府把建造力卖给扩建方：政府建筑用财政资金，私有建筑用现金池
  const salePrice = powerPrice * (1 + OVER.markup);
  let powerLeft = powerBuy;
  let sold = 0;
  for (const g of intent) {
    if (powerLeft <= 1e-9) break;
    if (!g.gov && g.i !== FIN && ctrlUsed + g.units > ctrlCap) continue;
    const need = BUILDCOST[g.i] * g.units;
    const want = Math.min(SITE_POWER, powerLeft, need);
    // 付款方
    const payer = g.gov ? 'gov' : (g.i === FIN ? 'fin' : 'firm');
    const avail = payer === 'gov' ? Math.max(0, M.govCash) : (payer === 'fin' ? Math.max(0, M.finCash) : Math.max(0, M.cash[g.i] * (1 - M.govShare[g.i])));
    const afford = avail / Math.max(salePrice, 1e-9);
    const grant = Math.min(want, afford);
    if (grant <= 1e-9) continue;
    const pay = grant * salePrice;
    if (payer === 'gov') M.govCash -= pay;
    else if (payer === 'fin') M.finCash -= pay;
    else M.cash[g.i] -= pay;
    M.govCash += pay;                                   // 政府收取建造力售价（含加价）
    sold += grant;
    M.queue.push({ type: g.i, units: g.units, progress: grant, owner: payer });
    powerLeft -= grant;
    if (!g.gov) ctrlUsed += g.units;
  }
  M.govPowerSale = sold;
  // 未售出的建造力由政府吸收（作为公共储备，留在政府账上）
  const unsold = powerLeft;
  if (unsold > 1e-9) M.govCash -= 0;                    // 已支付，未售出部分记为政府库存
  M.govCash += taxTotal;                                // 税收入账
  M.govCash += govOperating;                            // 政府建筑经营净额

  // ===== 完工 =====
  const keep = [];
  for (const o of M.queue) {
    if (o.progress >= BUILDCOST[o.type] * o.units - 1e-9) {
      let add = o.units;
      if (o.type === 0 || o.type === 2) add = Math.max(0, Math.min(add, ARABLE - M.levels[0] - M.levels[2]));
      else if (o.type === 5) add = Math.max(0, Math.min(add, COALCAP - M.levels[5]));
      else if (o.type === 6) add = Math.max(0, Math.min(add, IRONCAP - M.levels[6]));
      else if (o.type === 10) add = Math.max(0, Math.min(add, POWERCAP - M.levels[10]));
      else if (o.type === FIN) add = Math.max(0, Math.min(add, FINCAP - M.levels[FIN]));
      const gs = M.govShare[o.type];
      M.levelGov[o.type] += add * gs;
      M.levelPriv[o.type] += add * (1 - gs);
      M.levels[o.type] += add;
      M.cash[o.type] += add * 5000;
      if (o.type === FIN) M.finCash += add * 5000;
      if (add < o.units) M.blockedBuilds++;
    } else keep.push(o);
  }
  M.queue = keep;

  // ===== 缩编 =====
  for (let i = 0; i < NT; i++) {
    M.marginEma[i] = M.marginEma[i] * (1 - 1 / EMA_TICKS) + margin[i] / EMA_TICKS;
    if (M.hire[i] < IDLE_HR) M.idle[i]++; else M.idle[i] = 0;
    if (M.idle[i] > DECAY_WINDOW && M.marginEma[i] < 0 && M.levels[i] > 0) {
      const nl = M.levels[i] * (1 - DECAY_RATE);
      M.levels[i] = nl >= 0.5 ? nl : 0;
    }
  }

  // ===== 人口 =====
  const satNec = (sat[0] + sat[1]) / 2;
  const growth = satNec >= 0.75 ? 0.05 * (satNec - 0.75) / 0.25 : -0.20 * (0.75 - satNec) / 0.75;
  if (M.tick > 0 && M.tick % TPY === 0) M.pop *= (1 + growth);

  // ===== 雇佣调整 =====
  const step = HIRE_ANNUAL / TPY;
  for (let i = 0; i < NT; i++) {
    if (M.marginEma[i] < 0) M.hire[i] = Math.max(0, M.hire[i] - step);
    else if (M.marginEma[i] > 0) M.hire[i] = Math.min(1, M.hire[i] + step);
  }

  // ===== GDP =====
  let cashTotal = M.govCash + M.finCash;
  for (let i = 0; i < NT; i++) cashTotal += M.cash[i];
  const overdraft = M.govCash < -1e6 || M.finCash < -1e6;
  if (overdraft) M.overdraftTicks++;
  const gdpDoc = spendNet * (1 + t) + cashTotal;

  M.tick++;
  let nu = 0, de = 0;
  for (let i = 0; i < NT; i++) { const L = M.levels[i]; nu += margin[i] * L; de += L; }
  M.hist.push({
    tick: M.tick, P: P.slice(), S: Y.slice(), D: D.slice(), margin: margin.slice(), sat: sat.slice(),
    pop: M.pop, levels: M.levels.slice(), govCash: M.govCash, finCash: M.finCash, cashTotal, gdpDoc,
    spendNet, tax: taxTotal, wage: totalWage, queueLen: M.queue.length,
    capitalIncome, govOperating, powerBuy, powerOut, powerDemand, powerSold: sold, ctrlCap, ctrlUsed,
    allocMin: Math.min(...allocRatio), marginWavg: de > 0 ? nu / de : 0, spendScale,
    finLevels: M.levels[FIN], overdraft,
  });
}

function assess(M) {
  const h = M.hist;
  if (h.length < 200) return null;
  const tail = h.slice(-2000), T = tail.length, res = {};
  const clampShare = new Array(N).fill(0), maxLog = new Array(N).fill(0);
  for (const r of tail) for (let i = 0; i < N; i++) {
    if (r.P[i] <= 0.2 * PCOST[i] * 1.001 || r.P[i] >= 5 * PCOST[i] * 0.999) clampShare[i]++;
    maxLog[i] = Math.max(maxLog[i], Math.abs(Math.log(r.P[i] / PCOST[i])));
  }
  res.A1 = { pass: clampShare.every((x) => x / T < 0.05), clampShare: clampShare.map((x) => x / T), maxLog };
  let mMin = 1e9, mMax = -1e9, mFin = 0;
  for (const r of tail) for (let i = 0; i < NT; i++) {
    if (i < N) { mMin = Math.min(mMin, r.margin[i]); mMax = Math.max(mMax, r.margin[i]); } else mFin = r.margin[i];
  }
  let nu = 0, de = 0;
  for (const r of tail) for (let i = 0; i < NT; i++) { const L = r.levels[i]; nu += r.margin[i] * L; de += L; }
  const wAvg = de > 0 ? nu / de : 0;
  res.A2 = { pass: mMin >= -0.10 - 1e-9 && mMax <= 0.25 + 1e-9 && wAvg <= 0.15 + 1e-9, mMin, mMax, wAvg, mFin };
  const l5 = h.slice(-500);
  let dPR = 0, dSa = 0, nrm = 0;
  for (let k = 1; k < l5.length; k++) for (let i = 0; i < N; i++) {
    dPR += Math.abs(l5[k].P[i] / PCOST[i] - l5[k - 1].P[i] / PCOST[i]);
    dSa += Math.abs(l5[k].S[i] / Math.max(M.a[i], 1e-12) - l5[k - 1].S[i] / Math.max(M.a[i], 1e-12));
    nrm++;
  }
  res.A3 = { pass: dPR / nrm < 1e-4 && dSa / nrm < 2e-4, dPR: dPR / nrm, dSa: dSa / nrm };
  const allPos = h.every((r) => r.gdpDoc > 0 && isFinite(r.gdpDoc));
  res.A4 = { pass: allPos, allPos, minGdp: Math.min(...h.map((r) => r.gdpDoc)) };
  const popAnnual = Math.pow(M.pop / h[0].pop, TPY / Math.max(1, M.tick - h[0].tick)) - 1;
  const lv0 = h[0].levels.reduce((a, b) => a + b, 0), lvT = h[h.length - 1].levels.reduce((a, b) => a + b, 0);
  res.A5 = { pass: popAnnual >= -1e-9 && popAnnual <= 0.05 + 1e-9 && lvT > 0, popAnnual, lv0, lvT };
  const ne = tail.filter((r) => r.queueLen > 0).length;
  res.A6 = { pass: ne / T < 0.20 || lvT > lv0 * 1.05, share: ne / T };
  return res;
}

function run(label) {
  const M = createModel();
  console.log(`\n${'='.repeat(84)}\n${label}\n${'='.repeat(84)}`);
  console.log(`参数：tick=${TICKS} 税率=${(OVER.tax * 100).toFixed(0)}% 政府所有权=${(OVER.govShare * 100).toFixed(0)}% ` +
    `掌控 1:${OVER.ctrl} k=${OVER.k} 加价=${(OVER.markup * 100).toFixed(0)}% 人口0=${OVER.pop.toLocaleString()}`);
  console.log('开局级数：' + S.map((s, i) => `${s}=${M.levels[i]}`).join(' ') + ` fin=${M.levels[FIN]}`);
  console.log(`开局总级数 ${M.levels.reduce((a, b) => a + b, 0)}  政府池 ${Math.round(M.govCash).toLocaleString()}  金融池 ${Math.round(M.finCash).toLocaleString()}`);
  const t0 = Date.now();
  for (let i = 0; i < TICKS; i++) {
    tick(M);
    if ([0, 51, 259, 519, 999, 2999, 4999, 9999].includes(i) || i === TICKS - 1) {
      const r = M.hist[M.hist.length - 1];
      console.log(`  tick ${String(M.tick).padStart(6, ' ')} pop=${Math.round(M.pop).toLocaleString().padStart(12)} 总级=${r.levels.reduce((a, b) => a + b, 0).toFixed(0).padStart(5)} ` +
        `pw=${r.levels[10].toFixed(1).padStart(7)} 政府池=${Math.round(r.govCash).toLocaleString().padStart(16)} 税=${Math.round(r.tax).toLocaleString().padStart(12)} ` +
        `购力=${r.powerBuy.toFixed(1).padStart(8)} 售出=${r.powerSold.toFixed(1).padStart(8)} 队列=${String(r.queueLen).padStart(4)}`);
    }
  }
  console.log(`  耗时 ${Date.now() - t0} ms`);
  return M;
}

function report(M) {
  const h = M.hist, last = h[h.length - 1];
  console.log('\n--- 终态 ---');
  console.log('类别        P/P_cost   margin      级数   政府级   私有级          现金池');
  for (let i = 0; i < NT; i++) {
    const nm = i === FIN ? 'finance' : S[i];
    console.log('  ' + nm.padEnd(10)
      + (i < N ? (last.P[i] / PCOST[i]).toFixed(3).padStart(8) : '      —'.padStart(8))
      + ((last.margin[i] * 100).toFixed(1) + '%').padStart(10)
      + last.levels[i].toFixed(1).padStart(9) + M.levelGov[i].toFixed(1).padStart(9) + M.levelPriv[i].toFixed(1).padStart(9)
      + Math.round(M.cash[i]).toLocaleString().padStart(16));
  }
  console.log('  满足度：' + last.sat.map((s, i) => `${GROUPS[i].name}=${(s * 100).toFixed(1)}%`).join(' '));
  console.log(`  人口 ${Math.round(M.pop).toLocaleString()}  总级数 ${last.levels.reduce((a, b) => a + b, 0).toFixed(1)}  政府池 ${Math.round(M.govCash).toLocaleString()}  金融池 ${Math.round(M.finCash).toLocaleString()}`);
  console.log(`  掌控上限 ${last.ctrlCap.toFixed(1)} vs 实际 ${last.ctrlUsed.toFixed(1)}（${last.ctrlUsed <= last.ctrlCap ? '未超限' : '超限'}）  透支 tick ${M.overdraftTicks}/${M.tick}`);

  const l5 = h.slice(-500), avg = (k) => l5.reduce((s, r) => s + r[k], 0) / l5.length;
  console.log('\n--- 政府与资本资金流（末 500 周期均值）---');
  console.log(`  政府税收                = ${Math.round(avg('tax')).toLocaleString()} 元/tick`);
  console.log(`  政府采购建造力          = ${avg('powerBuy').toFixed(1)} 单位/tick（${Math.round(avg('powerBuy') * PCOST[10]).toLocaleString()} 元）`);
  console.log(`  政府售出建造力          = ${avg('powerSold').toFixed(1)} 单位/tick`);
  console.log(`  建造力产出 / 申报需求   = ${avg('powerOut').toFixed(1)} / ${avg('powerDemand').toFixed(1)}`);
  console.log(`  政府建筑经营净额        = ${Math.round(avg('govOperating')).toLocaleString()} 元/tick`);
  console.log(`  政府现金池              = ${Math.round(avg('govCash')).toLocaleString()} 元`);
  console.log(`  资本纯利                = ${Math.round(avg('capitalIncome')).toLocaleString()} 元/tick`);
  console.log(`  金融区现金池            = ${Math.round(avg('finCash')).toLocaleString()} 元`);
  console.log(`  居民工资总额            = ${Math.round(avg('wage')).toLocaleString()} 元/tick`);
  console.log(`  消费者实付（含税）      = ${Math.round(avg('spendNet') * (1 + OVER.tax)).toLocaleString()} 元/tick`);
  console.log(`  行业加权平均利润率      = ${(avg('marginWavg') * 100).toFixed(2)}%`);

  const res = assess(M);
  if (!res) return null;
  const yn = (b) => (b ? '通过' : '未通过');
  console.log('\n--- §8.4 验收判据（末 2,000 周期）---');
  console.log(`A1 贴边占比 <5%      ${yn(res.A1.pass)}  最大贴边率=${(Math.max(...res.A1.clampShare) * 100).toFixed(2)}%`);
  console.log(`A2 margin 有界       ${yn(res.A2.pass)}  min=${(res.A2.mMin * 100).toFixed(1)}% max=${(res.A2.mMax * 100).toFixed(1)}% 加权=${(res.A2.wAvg * 100).toFixed(2)}% 金融区=${(res.A2.mFin * 100).toFixed(1)}%`);
  console.log(`A3 均衡点不漂移      ${yn(res.A3.pass)}  d(P/Pc)=${res.A3.dPR.toExponential(2)} d(S/a)=${res.A3.dSa.toExponential(2)}`);
  console.log(`A4 GDP > 0           ${yn(res.A4.pass)}  最低GDP=${Math.round(res.A4.minGdp).toLocaleString()}`);
  console.log(`A5 无坍缩+人口区间   ${yn(res.A5.pass)}  人口年化=${(res.A5.popAnnual * 100).toFixed(2)}% 总级数 ${res.A5.lv0.toFixed(0)}→${res.A5.lvT.toFixed(0)}`);
  console.log(`A6 队列不饥饿        ${yn(res.A6.pass)}  队列非空占比=${(res.A6.share * 100).toFixed(1)}%`);
  return res;
}

const MODES = [];
if (process.argv.includes('--sweep')) {
  for (const tax of [0.05, 0.10, 0.20]) MODES.push({ label: `税率 ${(tax * 100).toFixed(0)}%`, tax });
} else {
  MODES.push({ label: `政府/资本模型 v4（税率 ${(OVER.tax * 100).toFixed(0)}%）`, tax: OVER.tax });
}
const all = {};
for (const m of MODES) {
  OVER.tax = m.tax;
  const M = run(m.label);
  all[m.label] = report(M);
}
console.log('\n=== 汇总 ===');
const ids = ['A1', 'A2', 'A3', 'A4', 'A5', 'A6'];
for (const [k, v] of Object.entries(all)) {
  if (!v) { console.log(`${k}\n  （周期不足）`); continue; }
  console.log(`${k}\n  通过 ${ids.filter((a) => v[a].pass).length}/6：${ids.filter((a) => v[a].pass).join(' ') || '（无）'}   未通过：${ids.filter((a) => !v[a].pass).join(' ') || '（无）'}`);
}
fs.writeFileSync('out/core_sim_gov.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/core_sim_gov.txt');
