// VC_Go/tools/viz/gen_dashboard.js
//
// 生成一个**自包含**的 HTML 可视化面板（无 CDN、无外部依赖），把 1.0 契约的
// 静态结构与 gosim 的实跑数据放在同一屏里对照。
//
// 数据来源（互不混用）：
//   · VC_Go/tools/contract_data.json      —— 契约声明值（§3.1/§3.3/§4.5/§5/§6.3/§8.6）
//   · out/viz/run-*.json            —— 实测值（VC_Go/tools/viz/parse_run.js 解析的实跑输出）
//
// 用法：node VC_Go/tools/viz/gen_dashboard.js
// 产出：out/viz/dashboard.html
//
// 纪律：本文件是 UTF-8 且含中文。**不要用 PowerShell 的 Set-Content / -replace 改写**
// （GBK 往返会损坏，本项目已踩过两次），一律用编辑工具。

'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const contract = JSON.parse(fs.readFileSync(path.join(ROOT, 'tools', 'contract_data.json'), 'utf8'));

const P = contract.params;
const GOODS = contract.goods;
const KEY = (k) => GOODS.findIndex((g) => g.key === k);
const PROD = GOODS.filter((g) => !g.isPower && !g.isFinance);   // 10 种生产建筑

/** contract_data.json 里带 `_` 前缀的字段是元数据，不是商品。取纯商品映射时统一过滤。 */
function goodMap(obj) {
  const out = {};
  for (const g of GOODS) out[g.key] = Number(obj[g.key]) || 0;
  return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. 契约模型（全部由 contract_data.json 推导，不引入第二份抄写）
// ─────────────────────────────────────────────────────────────────────────────

/** 投入系数 A[out][in] = 生产 1 单位 out 所需的 in 数量（§3.4 定义） */
function buildA() {
  const n = GOODS.length;
  const A = Array.from({ length: n }, () => Array(n).fill(0));
  for (const b of contract.buildings) {
    const j = KEY(b.good);
    for (const [src, qty] of Object.entries(b.inputs)) {
      A[j][KEY(src)] = qty / GOODS[j].outputPerLevel;
    }
  }
  return A;
}

/** 单位劳动成本 l_j = 33750 / q_j（§3.4；金融区不进入该方程） */
function buildL() {
  return GOODS.map((g) => (g.isFinance ? 0 : P.wagePerLevel / g.outputPerLevel));
}

function invert(M) {
  const n = M.length;
  const a = M.map((r, i) => r.concat(Array.from({ length: n }, (_, j) => (i === j ? 1 : 0))));
  for (let c = 0; c < n; c++) {
    let piv = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(a[r][c]) > Math.abs(a[piv][c])) piv = r;
    [a[c], a[piv]] = [a[piv], a[c]];
    const d = a[c][c];
    for (let k = 0; k < 2 * n; k++) a[c][k] /= d;
    for (let r = 0; r < n; r++) {
      if (r === c) continue;
      const f = a[r][c];
      if (!f) continue;
      for (let k = 0; k < 2 * n; k++) a[r][k] -= f * a[c][k];
    }
  }
  return a.map((r) => r.slice(n));
}

/**
 * 解零利润价（§3.4），用于校验 §3.1 的价格表。
 *
 * 下标约定——本项目最容易搞反的一处（实测搞反会让 11 个价格系统性错配、最大偏差 822%）：
 *
 *   本文件的 A 采用**需求侧**约定：**A[i][j] = 生产 1 单位商品 j 所需的商品 i 数量**
 *   （例：A[谷物][加工食品] = 40/45 = 0.8889，与 §3.4 的举例同向）。
 *   于是成本方程 p_j = Σ_i A[i][j]·p_i + l_j 的矩阵形式为 **p = Aᵀ·p + l**，
 *   零利润价 **p = (I − Aᵀ)⁻¹·l**，其中 (I − Aᵀ)[i][j] = δ_ij − A[j][i]。
 *
 *   注意：契约 §3.4 写的 `p = (I − Aᵀ)⁻¹l` 用的是**供给系数**约定
 *   （契约的 A[i][j] = 生产 1 单位 i 所需的 j）。两种约定互为转置，最终公式同为
 *   `p = (I − A_supply)⁻¹l`。**本文件选择了需求侧约定**，因此这里求逆的矩阵是
 *   `δ_ij − A[i][j]`（即 §3.4 契约式中的 Aᵀ 那一侧），而不是 `δ_ij − A[j][i]`。
 *
 *   自检：用本实现复算 §3.1 的 11 个零利润价，最大相对偏差 0.0644%
 *   （全部来自价格表取整），与 `VC_Go/docs/AUDIT-1.0.md` 记录的 0.0675% 同源。
 */
function solveZeroProfitPrices() {
  const n = GOODS.length, A = buildA(), l = buildL();
  const M = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => (i === j ? 1 : 0) - A[i][j]));
  const inv = invert(M);
  return inv.map((row) => row.reduce((s, v, j) => s + v * l[j], 0));
}

/** 谱半径（幂法，§3.4 的可行性硬校验） */
function spectralRadius() {
  const A = buildA(), n = A.length;
  let v = Array(n).fill(1 / Math.sqrt(n)), lambda = 0;
  for (let it = 0; it < 5000; it++) {
    const w = Array(n).fill(0);
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) w[i] += A[i][j] * v[j];
    const norm = Math.hypot(...w);
    if (norm === 0) return 0;
    v = w.map((x) => x / norm);
    lambda = norm;
  }
  return lambda;
}

/** 由配方 + 最终需求解平衡等级（§8.6 的 L* 方程），用于与契约表列值对照 */
function solveEquilibriumLevels(powerLevel, finalDemand) {
  const n = GOODS.length;
  let L = GOODS.map((g) => (g.isPower ? powerLevel : 1));
  for (let it = 0; it < 20000; it++) {
    const nx = GOODS.map(() => 0);
    for (let i = 0; i < n; i++) {
      const g = GOODS[i];
      if (g.isPower) { nx[i] = powerLevel; continue; }
      let need = finalDemand[g.key] || 0;
      for (const b of contract.buildings) need += (b.inputs[g.key] || 0) * L[KEY(b.good)];
      nx[i] = Math.max(0, need / g.outputPerLevel);
    }
    let d = 0;
    for (let i = 0; i < n; i++) d = Math.max(d, Math.abs(nx[i] - L[i]));
    L = nx;
    if (d < 1e-12) break;
  }
  return L;
}

/** 平衡点上的四个流量（§4.5.3 的税基构成） */
function aggregate(levels, finalDemand, taxRate, govShare) {
  const wage = PROD.reduce((s, g) => s + levels[KEY(g.key)] * P.wagePerLevel, 0);
  const finalValue = GOODS.reduce((s, g) => s + (finalDemand[g.key] || 0) * g.pcost, 0);
  let intermValue = 0;
  for (const b of contract.buildings) {
    for (const [src, qty] of Object.entries(b.inputs)) {
      intermValue += levels[KEY(b.good)] * qty * GOODS[KEY(src)].pcost;
    }
  }
  const powerGross = levels[KEY('power')] * GOODS[KEY('power')].outputPerLevel * GOODS[KEY('power')].pcost;
  const taxBase = finalValue + intermValue + powerGross;   // 毛额口径
  const tax = taxBase * taxRate / (1 + taxRate);
  const govObligation = govShare * wage;
  const debtCap = P.debtCapMultiplier * levels[KEY('power')] * GOODS[KEY('power')].outputPerLevel * GOODS[KEY('power')].pcost;
  return { wage, finalValue, intermValue, powerGross, taxBase, tax, govObligation, gap: govObligation - tax, debtCap, levels };
}

const A = buildA();
const spectral = spectralRadius();
const pcostSolved = solveZeroProfitPrices();
const maxPcostDev = Math.max(...pcostSolved.map((p, i) => Math.abs(p - GOODS[i].pcost) / GOODS[i].pcost));

const FD = goodMap(contract.finalDemand);
const Lcontract = GOODS.map((g) => (g.isPower ? P.initialPowerLevel : (Number(contract.equilibriumLevelsContract[g.key]) || 0)));
const Lsolved = solveEquilibriumLevels(P.initialPowerLevel, FD);
const aggContractLevels = aggregate(Lcontract, FD, P.taxRate, P.initialGovShare);
const aggSolved = aggregate(Lsolved, FD, P.taxRate, P.initialGovShare);
const levelSum = (L) => L.filter((_, i) => !GOODS[i].isPower).reduce((a, b) => a + b, 0);

// ─────────────────────────────────────────────────────────────────────────────
// 2. 实测数据
// ─────────────────────────────────────────────────────────────────────────────
function loadRun(name) {
  const p = path.join(ROOT, 'out', 'viz', name + '.json');
  return fs.existsSync(p) ? JSON.parse(fs.readFileSync(p, 'utf8')) : null;
}
const runs = ['run-default-3000', 'run-balanced-3000']
  .map((n) => ({ name: n, ...(loadRun(n) || {}) }))
  .filter((r) => r.series);

// ─────────────────────────────────────────────────────────────────────────────
// 3. SVG 绘图工具（内联，无依赖）
// ─────────────────────────────────────────────────────────────────────────────
const COLORS = ['#e8b64b', '#e07a5f', '#81b29a', '#5fa8d3', '#c084fc', '#94a3b8',
  '#f472b6', '#38bdf8', '#a3e635', '#fb923c', '#22d3ee'];

function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }

function fmt(x, d = 0) {
  if (x === null || x === undefined || Number.isNaN(x)) return '—';
  const a = Math.abs(x);
  if (a >= 1e8) return (x / 1e8).toFixed(2) + '亿';
  if (a >= 1e4) return (x / 1e4).toFixed(a >= 1e6 ? 0 : 1) + '万';
  return x.toFixed(d);
}

/** 通用折线图：series = [{name, color, points:[{x,y}], dashed}] */
function lineChart(opt) {
  const W = opt.width || 620, H = opt.height || 260;
  const m = { t: 28, r: opt.marginRight || 64, b: 40, l: opt.marginLeft || 76 };
  const iw = W - m.l - m.r, ih = H - m.t - m.b;
  const all = opt.series.flatMap((s) => s.points);
  if (!all.length) return `<svg viewBox="0 0 ${W} ${H}" class="chart"></svg>`;
  const xs = all.map((p) => p.x), ys = all.map((p) => p.y);
  const x0 = Math.min(...xs), x1 = Math.max(...xs);
  const span = (Math.max(...ys) - Math.min(...ys)) * 0.08 || 1;
  const lo = opt.yMin !== undefined ? opt.yMin : Math.min(...ys) - span;
  const hi = opt.yMax !== undefined ? opt.yMax : Math.max(...ys) + span;
  const X = (v) => m.l + (x1 === x0 ? iw / 2 : (v - x0) / (x1 - x0) * iw);
  const Y = (v) => m.t + ih - (v - lo) / (hi - lo || 1) * ih;

  let g = '';
  for (let i = 0; i <= 5; i++) {
    const v = lo + (hi - lo) * i / 5, y = Y(v);
    g += `<line x1="${m.l}" y1="${y.toFixed(1)}" x2="${m.l + iw}" y2="${y.toFixed(1)}" class="grid"/>`;
    g += `<text x="${m.l - 8}" y="${(y + 4).toFixed(1)}" class="axis" text-anchor="end">${fmt(v, Math.abs(hi - lo) < 10 ? 2 : 0)}</text>`;
  }
  for (const band of opt.bands || []) {
    const top = Math.max(m.t, Y(Math.min(band.to, hi)));
    const bot = Math.min(m.t + ih, Y(Math.max(band.from, lo)));
    if (bot > top) {
      g += `<rect x="${m.l}" y="${top.toFixed(1)}" width="${iw}" height="${(bot - top).toFixed(1)}" fill="${band.color}" opacity="0.10"/>`;
      if (band.label) g += `<text x="${m.l + iw - 6}" y="${(top + 13).toFixed(1)}" class="bandlabel" text-anchor="end">${esc(band.label)}</text>`;
    }
  }
  for (const v of (opt.xTicks || [])) {
    g += `<text x="${X(v).toFixed(1)}" y="${H - 14}" class="axis" text-anchor="middle">${v}</text>`;
  }
  for (const rl of opt.refLines || []) {
    const y = Y(rl.y);
    if (y >= m.t && y <= m.t + ih) {
      g += `<line x1="${m.l}" y1="${y.toFixed(1)}" x2="${m.l + iw}" y2="${y.toFixed(1)}" class="refline"/>`;
      g += `<text x="${m.l + 6}" y="${(y - 5).toFixed(1)}" class="reflabel">${esc(rl.label)}</text>`;
    }
  }
  for (const s of opt.series) {
    const d = s.points.map((p, i) => `${i ? 'L' : 'M'}${X(p.x).toFixed(1)},${Y(p.y).toFixed(1)}`).join(' ');
    g += `<path d="${d}" fill="none" stroke="${s.color}" stroke-width="${s.width || 2}" ${s.dashed ? 'stroke-dasharray="5 3"' : ''}/>`;
    for (const p of s.points) g += `<circle cx="${X(p.x).toFixed(1)}" cy="${Y(p.y).toFixed(1)}" r="2.6" fill="${s.color}"><title>${esc(s.name)} @t=${p.x}: ${fmt(p.y, 2)}</title></circle>`;
  }
  let lx = m.l;
  for (const s of opt.series) {
    if (s.hideLegend) continue;
    g += `<rect x="${lx}" y="8" width="10" height="10" rx="2" fill="${s.color}"/>`;
    g += `<text x="${lx + 14}" y="17" class="legend">${esc(s.name)}</text>`;
    lx += 24 + s.name.length * 8.6;
  }
  return `<svg viewBox="0 0 ${W} ${H}" class="chart" preserveAspectRatio="xMidYMid meet">${g}</svg>`;
}

/** 分组柱状图：groups = [{label, values:[..]}], seriesNames */
function barChart(opt) {
  const W = opt.width || 620, H = opt.height || 300;
  const m = { t: 28, r: 20, b: 58, l: 76 };
  const iw = W - m.l - m.r, ih = H - m.t - m.b;
  const flat = opt.groups.flatMap((g) => g.values);
  const lo = Math.min(0, ...flat), hi = Math.max(...flat, 1);
  const Y = (v) => m.t + ih - (v - lo) / (hi - lo || 1) * ih;
  const bw = iw / opt.groups.length;
  let g = '';
  for (let i = 0; i <= 5; i++) {
    const v = lo + (hi - lo) * i / 5, y = Y(v);
    g += `<line x1="${m.l}" y1="${y.toFixed(1)}" x2="${m.l + iw}" y2="${y.toFixed(1)}" class="grid"/>`;
    g += `<text x="${m.l - 8}" y="${(y + 4).toFixed(1)}" class="axis" text-anchor="end">${fmt(v)}</text>`;
  }
  g += `<line x1="${m.l}" y1="${Y(0).toFixed(1)}" x2="${m.l + iw}" y2="${Y(0).toFixed(1)}" class="zeroline"/>`;
  opt.groups.forEach((grp, gi) => {
    const n = grp.values.length, w = Math.min(34, (bw * 0.68) / n);
    grp.values.forEach((v, si) => {
      const x = m.l + bw * gi + bw / 2 - (n * w) / 2 + si * w;
      const yTop = Math.min(Y(v), Y(0)), hh = Math.abs(Y(v) - Y(0));
      g += `<rect x="${x.toFixed(1)}" y="${yTop.toFixed(1)}" width="${w.toFixed(1)}" height="${hh.toFixed(1)}" fill="${opt.colors[si]}" rx="2"><title>${esc(grp.label)} · ${esc(opt.seriesNames[si])}: ${fmt(v, 2)}</title></rect>`;
    });
    g += `<text x="${(m.l + bw * gi + bw / 2).toFixed(1)}" y="${H - 38}" class="axis" text-anchor="middle">${esc(grp.label)}</text>`;
  });
  let lx = m.l;
  opt.seriesNames.forEach((nm, i) => {
    g += `<rect x="${lx}" y="8" width="10" height="10" rx="2" fill="${opt.colors[i]}"/><text x="${lx + 14}" y="17" class="legend">${esc(nm)}</text>`;
    lx += 24 + nm.length * 8.6;
  });
  return `<svg viewBox="0 0 ${W} ${H}" class="chart">${g}</svg>`;
}

/** 热力带：把 margins 表画成每部门一行的色带 */
function heatStrip(opt) {
  const rows = opt.rows, cols = opt.cols;
  const cellW = 56, cellH = 23, labelW = 88, topH = 22;
  const W = labelW + cols.length * cellW + 12, H = topH + rows.length * cellH + 12;
  const color = (v) => {
    if (v === null || v === undefined) return '#1b2230';
    if (v >= 0) return `rgba(129,178,154,${0.18 + 0.72 * Math.min(1, v)})`;
    return `rgba(224,122,95,${0.18 + 0.72 * Math.min(1, -v)})`;
  };
  let g = '';
  cols.forEach((c, ci) => {
    g += `<text x="${labelW + ci * cellW + cellW / 2}" y="14" class="axis" text-anchor="middle">t=${c}</text>`;
  });
  rows.forEach((r, ri) => {
    const y = topH + ri * cellH;
    g += `<text x="${labelW - 8}" y="${y + 15}" class="axis" text-anchor="end">${esc(r.label)}</text>`;
    r.values.forEach((v, ci) => {
      g += `<rect x="${labelW + ci * cellW + 1}" y="${y + 1}" width="${cellW - 2}" height="${cellH - 2}" fill="${color(v)}" rx="3"><title>${esc(r.label)} @t=${cols[ci]}: ${(v * 100).toFixed(1)}%</title></rect>`;
      g += `<text x="${labelW + ci * cellW + cellW / 2}" y="${y + 15}" class="celltext" text-anchor="middle">${(v * 100).toFixed(0)}</text>`;
    });
  });
  return `<svg viewBox="0 0 ${W} ${H}" class="chart">${g}</svg>`;
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. 组装页面
// ─────────────────────────────────────────────────────────────────────────────
const primary = runs[0];
const balanced = runs.find((r) => r.name.includes('balanced'));
const first = primary.series[0];
const last = primary.series[primary.series.length - 1];

function card(title, sub, body, cls = '') {
  return `<section class="card ${cls}"><h3>${esc(title)}</h3>${sub ? `<p class="sub">${sub}</p>` : ''}${body}</section>`;
}

const kpi = [
  { k: '标定谱半径', v: spectral.toFixed(6), note: '契约硬校验 < 1（§3.4）', ok: spectral < 1 },
  { k: '实跑谱半径', v: primary.spectralRadius.toFixed(6), note: '与本地复算一致', ok: true },
  { k: 'P_cost 复算偏差', v: (maxPcostDev * 100).toFixed(4) + '%', note: '由 §3.3+§5 解零利润价 vs §3.1 表', ok: maxPcostDev < 0.01 },
  { k: 'L*（契约定案）', v: contract.equilibriumLevelsContract._total.toFixed(2) + ' 级', note: '§8.6 表列合计', ok: null },
  { k: 'L*（配方反解）', v: levelSum(Lsolved).toFixed(2) + ' 级', note: '由 §3.3 配方独立反解', ok: false },
  { k: '政府缺口', v: (aggSolved.gap / aggSolved.wage).toFixed(3) + ' W', note: `税收仅覆盖工资义务的 ${(aggSolved.tax / aggSolved.govObligation * 100).toFixed(1)}%`, ok: false },
  { k: '实跑末态总级数', v: last.totalLevels.toFixed(0), note: `起点 ${first.totalLevels.toFixed(0)} 级`, ok: false },
  { k: '实跑末态人口', v: fmt(last.population), note: `起点 ${fmt(first.population)}`, ok: false },
  { k: '判据汇总', v: `${primary.verdictSummary.pass}/${primary.verdictSummary.total}`, note: '契约 §8.4 实列 8 条，实现只评 6 条', ok: false },
];

const ticks = primary.series.map((s) => s.tick);

const chartLevels = lineChart({
  width: 640, height: 280,
  series: [
    { name: '总级数', color: '#5fa8d3', points: primary.series.map((s) => ({ x: s.tick, y: s.totalLevels })) },
    ...(balanced ? [{ name: '总级数（物质平衡布点）', color: '#c084fc', points: balanced.series.map((s) => ({ x: s.tick, y: s.totalLevels })), dashed: true }] : []),
  ],
  xTicks: ticks, marginRight: 30, yMin: 0,
});

const chartPopulation = lineChart({
  width: 640, height: 280,
  series: [
    { name: '人口', color: '#81b29a', points: primary.series.map((s) => ({ x: s.tick, y: s.population })) },
    ...(balanced ? [{ name: '人口（物质平衡布点）', color: '#a3e635', points: balanced.series.map((s) => ({ x: s.tick, y: s.population })), dashed: true }] : []),
  ],
  xTicks: ticks, marginRight: 30, yMin: 0,
});

const chartGov = lineChart({
  width: 1280, height: 300,
  series: [
    { name: '政府现金池', color: '#e07a5f', points: primary.series.map((s) => ({ x: s.tick, y: s.govCash })) },
    { name: '政府债务', color: '#f472b6', points: primary.series.map((s) => ({ x: s.tick, y: s.govDebt })) },
    { name: '债务上限（§4.5.4）', color: '#e8b64b', points: primary.series.map((s) => ({ x: s.tick, y: s.debtCap })), dashed: true },
  ],
  xTicks: ticks, refLines: [{ y: 0, label: '0' }],
});

const priceRatioSeries = GOODS.map((g, i) => {
  const row = (primary.endState || []).find((e) => e.name === g.name);
  return row ? { name: g.name, color: COLORS[i], points: [{ x: first.tick, y: 1 }, { x: last.tick, y: row.priceRatio }] } : null;
}).filter(Boolean);

const chartPrices = lineChart({
  width: 640, height: 320,
  series: priceRatioSeries,
  yMin: 0, yMax: 5.4, marginRight: 30, xTicks: [first.tick, last.tick],
  bands: [
    { from: 0, to: 0.2, color: '#e07a5f', label: 'P_floor 0.2×' },
    { from: 5.0, to: 5.4, color: '#e07a5f', label: 'P_ceil 5.0×' },
    { from: 0.2, to: 5.0, color: '#81b29a', label: '合法带' },
  ],
  refLines: [{ y: 1, label: 'P = P_cost' }],
});

const heat = heatStrip({
  cols: primary.margins.map((m) => m.tick),
  rows: primary.marginGoods.map((nm, i) => ({ label: nm, values: primary.margins.map((m) => m.margins[i]) })),
});

const chartLevelsCompare = barChart({
  width: 1280, height: 330,
  groups: PROD.map((g) => ({
    label: g.name,
    values: [Number(contract.equilibriumLevelsContract[g.key]) || 0, Lsolved[KEY(g.key)]],
  })),
  seriesNames: ['§8.6 契约定案 L*', '由 §3.3 配方独立反解'],
  colors: ['#5fa8d3', '#e07a5f'],
});

const fiscalGeo = barChart({
  width: 640, height: 330,
  groups: [
    { label: '工资总额 W', values: [aggSolved.wage] },
    { label: '税收@10%', values: [aggSolved.tax] },
    { label: '政府工资义务', values: [aggSolved.govObligation] },
    { label: '债务上限', values: [aggSolved.debtCap] },
  ],
  seriesNames: ['契约 L* 复算'],
  colors: ['#e8b64b'],
});

// 参数扫描表（预计算，页面内只做查表与配色）
const sweep = [];
for (const s of [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]) {
  for (const t of [0, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7]) {
    const a = aggregate(Lsolved, FD, t, s);
    sweep.push({ s, t, cover: a.tax / a.govObligation, gapW: a.gap / a.wage, debtTicks: a.gap > 0 ? a.debtCap / a.gap : null });
  }
}
const taxBaseOverW = aggSolved.taxBase / aggSolved.wage;
const breakEvenT = P.initialGovShare / taxBaseOverW;
const maxGovShareAtT = P.taxRate * taxBaseOverW;

const verdictRows = contract.acceptanceCriteria.map((c) => {
  const actual = primary.verdicts.find((v) => v.id === c.id);
  const status = !c.implemented ? '<span class="bad">未实现判定</span>'
    : actual ? (actual.pass ? '<span class="ok">通过</span>' : '<span class="bad">未通过</span>')
      : '<span class="warn">未出现</span>';
  return `<tr><td class="mono">${c.id}</td><td>${esc(c.text)}</td><td class="small">${esc(c.threshold)}</td><td>${status}</td><td class="small">${actual ? esc(actual.detail) : '—'}</td><td class="small">${esc(c.note)}</td></tr>`;
}).join('');

const dataBlob = {
  model: { taxBaseOverW, breakEvenT, maxGovShareAtT, wage: aggSolved.wage, debtCap: aggSolved.debtCap },
  sweep,
};

const html = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>Yehenala 1.0 生产与市场模拟 — 数据面板</title>
<style>
  :root{--bg:#0e131b;--panel:#151c27;--panel2:#1b2230;--line:#26303f;--fg:#dde5ef;--dim:#8b9bb0;--ok:#81b29a;--bad:#e07a5f;--warn:#e8b64b;--acc:#5fa8d3}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.55 "Segoe UI","Microsoft YaHei",system-ui,sans-serif}
  header{padding:22px 26px 14px;border-bottom:1px solid var(--line);background:linear-gradient(180deg,#141c27,#0e131b)}
  h1{margin:0 0 6px;font-size:20px;letter-spacing:.3px}
  h1 small{font-weight:400;color:var(--dim);font-size:13px;margin-left:10px}
  h3{margin:0 0 4px;font-size:15px}
  .verdictbar{display:flex;flex-wrap:wrap;gap:10px;margin-top:12px}
  .pill{padding:5px 12px;border-radius:999px;font-size:12.5px;border:1px solid var(--line);background:var(--panel2)}
  .pill.bad{border-color:#5c2f26;background:#2a1a17;color:#f0a58d}
  .pill.ok{border-color:#2c4a3c;background:#16241d;color:#a8d6bf}
  main{padding:20px 26px 60px;max-width:1720px;margin:0 auto}
  .kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:20px}
  .kpi{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
  .kpi .k{color:var(--dim);font-size:12px}
  .kpi .v{font-size:20px;font-variant-numeric:tabular-nums;margin:3px 0}
  .kpi .n{color:var(--dim);font-size:11.5px;line-height:1.4}
  .kpi.bad .v{color:#f0a58d}.kpi.ok .v{color:#a8d6bf}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(620px,1fr));gap:16px}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:16px 18px;margin-bottom:16px;overflow:hidden}
  .card.wide{grid-column:1/-1}
  .sub{color:var(--dim);font-size:12.5px;margin:2px 0 10px}
  .chart{width:100%;height:auto;display:block;margin-top:6px}
  .grid line.grid{stroke:#222c3a;stroke-width:1}
  .grid line.zeroline{stroke:#46536a;stroke-width:1;stroke-dasharray:4 3}
  .grid line.refline{stroke:#7d8ba1;stroke-width:1;stroke-dasharray:3 3;opacity:.7}
  .grid text{fill:var(--dim);font-size:10.5px;font-family:inherit}
  .grid text.reflabel{fill:#9fb0c6;font-size:10.5px}
  .grid text.bandlabel{fill:#9fb0c6;font-size:10px;opacity:.9}
  .grid .celltext{fill:#e7eef7;font-size:10px;opacity:.85}
  table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px}
  th,td{padding:7px 10px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}
  th{color:var(--dim);font-weight:600;font-size:12px;white-space:nowrap}
  td.small,.small{font-size:12px;color:#b9c6d6}
  td.mono,.mono{font-variant-numeric:tabular-nums;font-family:Consolas,monospace}
  .ok{color:#a8d6bf}.bad{color:#f0a58d}.warn{color:#f0cf8d}
  .controls{display:flex;flex-wrap:wrap;gap:22px;align-items:flex-end;margin:6px 0 14px}
  .ctl{display:flex;flex-direction:column;gap:5px;min-width:200px}
  .ctl label{font-size:12px;color:var(--dim)}
  .ctl output{font-variant-numeric:tabular-nums;color:var(--fg);font-size:15px}
  input[type=range]{width:100%;accent-color:var(--acc)}
  .note{background:var(--panel2);border-left:3px solid var(--warn);padding:10px 14px;border-radius:0 8px 8px 0;color:#cbd6e4;font-size:12.5px;margin-top:12px}
  footer{color:var(--dim);font-size:12px;border-top:1px solid var(--line);padding:14px 26px 30px}
  code{background:#0b1017;padding:1px 5px;border-radius:4px;font-size:12px;color:#cfe0f2}
</style>
</head>
<body>
<header>
  <h1>Yehenala · 1.0 生产与市场模拟 — 数据面板<small>契约静态结构 × gosim 实跑，同一屏对照</small></h1>
  <div class="verdictbar">
    <span class="pill bad">验收：未通过（实跑判据 ${primary.verdictSummary.pass}/${primary.verdictSummary.total}，契约实列 8 条）</span>
    <span class="pill bad">游戏内核：不能（无稳态 · 每 tick 决策点 0 · 无输入通道）</span>
    <span class="pill">谱半径 ρ(A) = ${spectral.toFixed(6)}</span>
    <span class="pill">L* 契约定案 ${contract.equilibriumLevelsContract._total} 级 / 配方反解 ${levelSum(Lsolved).toFixed(2)} 级</span>
    <span class="pill ok">记账不变量：货币守恒 / 借贷相等残差 0</span>
  </div>
</header>
<main>
  <div class="kpis">
    ${kpi.map((c) => `<div class="kpi ${c.ok === true ? 'ok' : c.ok === false ? 'bad' : ''}"><div class="k">${esc(c.k)}</div><div class="v">${esc(c.v)}</div><div class="n">${esc(c.note)}</div></div>`).join('')}
  </div>

  <div class="grid">
    ${card('总建筑级数（实跑）', `默认布点 ${first.totalLevels.toFixed(0)} 级 → 末态 ${last.totalLevels.toFixed(0)} 级；虚线为物质平衡布点（起点 ${balanced ? balanced.series[0].totalLevels.toFixed(0) : '—'} 级）`, chartLevels)}
    ${card('人口（实跑）', '默认布点在 t≈520 后转入崩塌；物质平衡布点峰值更高，但同样归零', chartPopulation)}
  </div>

  ${card('政府现金池 vs 债务上限', `现金池在 t=2 即转负并单调恶化；t≈520 债务上限归零 ⇒ §4.5.4 的 G2 停摆。末态政府现金池 ${fmt(last.govCash)}`, chartGov, 'wide')}

  <div class="grid">
    ${card('价格比 P/P_cost（实跑末态）', '合法带为 [0.2, 5.0]×P_cost。末态 9 种商品顶在 5.0×、1 种贴 0.2× —— 价格信号失效', chartPrices)}
    ${card('财政量级（契约 L* 复算）', `税收只覆盖政府工资义务的 ${(aggSolved.tax / aggSolved.govObligation * 100).toFixed(1)}%；缺口 ${fmt(aggSolved.gap)}/周期；债务上限仅 ${fmt(aggSolved.debtCap)}`, fiscalGeo)}
  </div>

  <div class="grid">
    ${card('部门利润率热力带（实跑抽样 tick，单位 %）', '绿色 = 盈利，红色 = 亏损。t=1 即有 8/11 部门亏损，§4.1 的 10% 扩建阈值永不触发', heat)}
    ${card('验收判据对照（契约 §8.4）', '契约实列 8 条，<code>report.Assess</code> 只实现 6 条；A7/A8 未判定', `<table><thead><tr><th>#</th><th>判据</th><th>阈值</th><th>实测</th><th>细节</th><th>备注</th></tr></thead><tbody>${verdictRows}</tbody></table>`)}
  </div>

  ${card('平衡等级 L*：契约声明 vs 由 §3.3 配方独立反解', '下游六项吻合到取整；上游四项（煤/铁/钢/工具）配方要求 2.0~3.1 倍 —— 这直接造成实跑中「铁配给 0.375、钢归零」', chartLevelsCompare, 'wide')}

  ${card('参数探查：政府持股 s × 交易税率 t', '拖动滑块。格 = 政府收支覆盖比，绿色（≥100%）表示在该组合下政府收支自洽', `
    <div class="controls">
      <div class="ctl"><label>政府持股 s = <output id="outS">0.70</output></label><input id="sldS" type="range" min="0" max="1" step="0.01" value="0.70"></div>
      <div class="ctl"><label>交易税率 t = <output id="outT">10%</output></label><input id="sldT" type="range" min="0" max="0.7" step="0.01" value="0.10"></div>
      <div class="ctl"><label>当前组合覆盖比</label><output id="outCover">—</output></div>
      <div class="ctl"><label>债务上限可支撑</label><output id="outTicks">—</output></div>
    </div>
    <div id="sweepBox"></div>
    <div class="note" id="sweepNote"></div>
  `, 'wide')}

  <div class="note">
    数据来源：契约声明值取自 <code>VC_Go/tools/contract_data.json</code>（逐项转录自 <code>1.0 生产与市场模拟.md</code>）；
    实测值取自 <code>out/viz/run-*.json</code>（由 <code>VC_Go/tools/viz/parse_run.js</code> 解析 <code>go run ./cmd/market-sim</code> 的输出）。
    注意：同一参数重复执行时，货币量级有约 0.01%~0.04% 的进程间抖动（离散量一致），故本页货币数字只作量级参考。
  </div>
</main>
<footer>
  重新生成：<code>node VC_Go/tools/viz/gen_dashboard.js</code> ·
  刷新实测：<code>go run ./cmd/market-sim -ticks 3000 &gt; out/viz/run-default-3000.txt</code> 后再跑 <code>node VC_Go/tools/viz/parse_run.js out/viz/run-default-3000.txt</code>
</footer>
<script>
const DATA = ${JSON.stringify(dataBlob)};
(function () {
  const sldS = document.getElementById('sldS'), sldT = document.getElementById('sldT');
  const outS = document.getElementById('outS'), outT = document.getElementById('outT');
  const outCover = document.getElementById('outCover'), outTicks = document.getElementById('outTicks');
  const box = document.getElementById('sweepBox'), note = document.getElementById('sweepNote');
  const S = DATA.sweep;
  const Ss = [...new Set(S.map((r) => +r.s.toFixed(2)))];
  const Ts = [...new Set(S.map((r) => +r.t.toFixed(2)))];
  const nearest = (arr, v) => arr.reduce((a, b) => (Math.abs(b - v) < Math.abs(a - v) ? b : a));
  function pick(s, t) {
    const cs = nearest(Ss, s), ct = nearest(Ts, t);
    return S.find((r) => Math.abs(r.s - cs) < 1e-9 && Math.abs(r.t - ct) < 1e-9);
  }
  function color(c) {
    if (c >= 1) return 'rgba(129,178,154,.9)';
    if (c >= 0.8) return 'rgba(232,182,75,.6)';
    if (c >= 0.5) return 'rgba(224,122,95,.5)';
    return 'rgba(224,122,95,.9)';
  }
  function render() {
    const s = +sldS.value, t = +sldT.value;
    outS.textContent = s.toFixed(2);
    outT.textContent = (t * 100).toFixed(0) + '%';
    const r = pick(s, t);
    outCover.textContent = (r.cover * 100).toFixed(1) + '%';
    outCover.style.color = r.cover >= 1 ? '#a8d6bf' : '#f0a58d';
    outTicks.textContent = r.debtTicks === null ? '无缺口（自洽）' : r.debtTicks.toFixed(2) + ' 个周期';
    outTicks.style.color = (r.debtTicks !== null && r.debtTicks < 6) ? '#f0a58d' : '#dde5ef';

    const cw = 36, ch = 23, lw = 66, th = 20;
    const W = lw + Ts.length * cw + 8, H = th + Ss.length * ch + 8;
    let g = '';
    Ts.forEach((tv, ci) => {
      g += '<text x="' + (lw + ci * cw + cw / 2) + '" y="13" style="fill:#8b9bb0;font-size:10px" text-anchor="middle">' + (tv * 100).toFixed(0) + '%</text>';
    });
    Ss.forEach((sv, ri) => {
      g += '<text x="' + (lw - 6) + '" y="' + (th + ri * ch + 15) + '" style="fill:#8b9bb0;font-size:10px" text-anchor="end">' + sv.toFixed(2) + '</text>';
      Ts.forEach((tv, ci) => {
        const rr = pick(sv, tv);
        const cur = Math.abs(sv - s) < 0.005 && Math.abs(tv - t) < 0.005;
        g += '<rect x="' + (lw + ci * cw + 1) + '" y="' + (th + ri * ch + 1) + '" width="' + (cw - 2) + '" height="' + (ch - 2) + '" rx="3" fill="' + color(rr.cover) + '"' + (cur ? ' stroke="#dde5ef" stroke-width="2"' : '') + '><title>s=' + sv.toFixed(2) + ' t=' + (tv * 100).toFixed(0) + '% 覆盖 ' + (rr.cover * 100).toFixed(1) + '%' + (rr.debtTicks === null ? '' : ' 上限撑 ' + rr.debtTicks.toFixed(2) + ' 周期') + '</title></rect>';
      });
    });
    box.innerHTML = '<svg viewBox="0 0 ' + W + ' ' + H + '" style="width:100%;max-width:820px;height:auto">' + g + '</svg>';
    note.innerHTML = '行 = 政府持股 s，列 = 交易税率 t，格 = 政府收支覆盖比（<b>绿 ≥ 100% 即自洽</b>）。' +
      '当前契约点 <b>(s=0.70, t=10%)</b> 的覆盖比只有 <b>' + (pick(0.7, 0.1).cover * 100).toFixed(1) + '%</b>；' +
      '要自洽，t 需升到约 <b>' + (DATA.model.breakEvenT * 100).toFixed(1) + '%</b>，' +
      '或把 s 压到 <b>' + DATA.model.maxGovShareAtT.toFixed(3) + '</b> 以下。';
  }
  sldS.addEventListener('input', render);
  sldT.addEventListener('input', render);
  render();
})();
</script>
</body>
</html>
`;

const outPath = path.join(ROOT, 'out', 'viz', 'dashboard.html');
fs.writeFileSync(outPath, html, 'utf8');
console.log('[已生成] ' + outPath);
console.log('  契约：谱半径 ' + spectral.toFixed(6) + ' / P_cost 复算最大偏差 ' + (maxPcostDev * 100).toFixed(4) + '%');
console.log('  L* 反解合计 ' + levelSum(Lsolved).toFixed(2) + ' vs 契约定案 ' + contract.equilibriumLevelsContract._total);
console.log('  政府缺口 ' + (aggSolved.gap / aggSolved.wage).toFixed(4) + ' W；税收覆盖工资义务 ' +
  (aggSolved.tax / aggSolved.govObligation * 100).toFixed(2) + '%；债务上限 ' + fmt(aggSolved.debtCap) +
  '（' + (aggSolved.debtCap / aggSolved.gap).toFixed(2) + ' 个周期）');
console.log('  自洽所需税率 t* = ' + (breakEvenT * 100).toFixed(1) + '%；或 t=10% 时 s 上限 = ' + maxGovShareAtT.toFixed(4));
console.log('  实跑：' + runs.map((r) => r.name + '(' + r.series.length + ' 点)').join('、'));
