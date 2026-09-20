/* =============================================================================
   app.js —— 1.1 界面（只读回放）
   · 数据来自 market-sim -export-dir 的 snapshots.jsonl + meta.json
   · 帧与 tick 解耦（契约 §五）：渲染 rAF 恒定；tick 按档位由累计时间推进
   · 不写任何引擎状态（契约 §0.2-13 同一条纪律）
   ============================================================================= */
(() => {
'use strict';

const $  = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

// 分类色：与 mockup 同一组，同一商品在所有图中恒为同一色
const COLORS = ['#0969da','#1a7f37','#8250df','#bf3989','#9a6700','#57606a','#cf222e','#953800',
                '#116329','#0550ae','#6e7781','#d4a72c','#6639ba','#1b7c83'];

let META = null, ROWS = [], N = 0;
let cur = 0;                 // 当前行下标（不是 tick）
// 播放速度用"每 tick 多少帧"表达（契约 §五：N = 60 ÷ tick/秒）。
// 新增 1/8×：为"慢速逐步模拟"准备——每 8 个 tick 才推进 1 个，肉眼可以逐步看。
const SPEEDS = [
  { key: '0',   label: '⏸', framesPerTick: 0,    rate: 0 },
  { key: '1/8', label: '×1/8', framesPerTick: 480, rate: 60 / 480 },
  { key: '1',   label: '×1',   framesPerTick: 60,  rate: 1 },
  { key: '2',   label: '×2',   framesPerTick: 30,  rate: 2 },
  { key: '5',   label: '×5',   framesPerTick: 12,  rate: 5 },
];
let speedKey = '1';
let frameAcc = 0, lastTs = 0;   // 帧与 tick 解耦：帧数恒定，tick 按 framesPerTick 推进
const hidden = new Set();    // 被取消显示的商品名
let frozenOn = true;

const speedOf = k => SPEEDS.find(s => s.key === k) || SPEEDS[2];
const isPlaying = () => speedOf(speedKey).framesPerTick > 0;

const fmt = CHART.fmt;
const num = v => (typeof v === 'number' && isFinite(v)) ? fmt(v) : '—';
const pct = v => (typeof v === 'number' && isFinite(v)) ? (v * 100).toFixed(2) + '%' : '—';

// ---------------------------------------------------------------- 载入
function dataURL(file) {
  // 首选 Go 只读服务 /data/；没有服务时退回同目录（file:// 或静态托管）
  return (location.protocol === 'http:' || location.protocol === 'https:')
    ? 'data/' + file
    : file;
}
async function load() {
  const mr = await fetch(dataURL('meta.json'), { cache: 'no-store' });
  if (!mr.ok) throw new Error('取 meta.json 失败（HTTP ' + mr.status + '）');
  META = await mr.json();

  const sr = await fetch(dataURL('snapshots.jsonl'), { cache: 'no-store' });
  if (!sr.ok) throw new Error('取 snapshots.jsonl 失败（HTTP ' + sr.status + '）');
  const text = await sr.text();
  ROWS = text.split('\n').filter(l => l.trim().length).map(l => {
    try { return JSON.parse(l); } catch (e) { return null; }
  }).filter(Boolean);

  N = ROWS.length;
  if (!N) throw new Error('snapshots.jsonl 里没有有效行');
}

// ---------------------------------------------------------------- 序列
let hist = null;   // 缓存：逐商品价格史、GDP 史等
function buildHistory() {
  const G = META.goods.length;
  hist = {
    price: Array.from({ length: G }, () => []),
    ratio: Array.from({ length: G }, () => []),
    margin: Array.from({ length: G }, () => []),
    gdpNom: [], gdpReal: [], govCash: [], govCap: [], pop: [], index: [],
    indextick: [],
    level: [], orders: [],
  };
  for (const r of ROWS) {
    for (let i = 0; i < G; i++) {
      hist.price[i].push(r.price ? r.price[i] : NaN);
      hist.ratio[i].push(r.ratio ? r.ratio[i] : NaN);
      hist.margin[i].push(r.margin ? r.margin[i] : NaN);
    }
    hist.gdpNom.push(r.spendNet + r.cashTotal);
    hist.gdpReal.push(r.productAdded);
    hist.govCash.push(r.govCash);
    hist.govCap.push(r.govDebtCap);
    hist.pop.push(r.population);
    hist.index.push(r.productIndex);
    hist.indextick.push(r.tick);
  }
}

// ---------------------------------------------------------------- 回放循环
function frame(ts) {
  if (!lastTs) lastTs = ts;
  const dt = Math.min(0.25, (ts - lastTs) / 1000);   // 秒；夹住以避免切标签页后跳变
  lastTs = ts;
  const sp = speedOf(speedKey);
  if (sp.framesPerTick > 0 && N > 0) {
    // 关键：tick 的推进量**与帧率无关**（1.0 要求"同二进制同参数逐位一致"；
    // 让帧直接驱动 tick 会打破它）。这里按"每 tick 帧数"换算，而不是数帧。
    frameAcc += dt * 60;                          // 本帧折算成 60FPS 的帧数
    const ticks = Math.floor(frameAcc / sp.framesPerTick);
    if (ticks > 0) {
      frameAcc -= ticks * sp.framesPerTick;
      cur = Math.min(N - 1, cur + ticks);
      renderTick();
      if (cur >= N - 1) { speedKey = '0'; syncSpeed(); }   // 到末尾自动暂停
    }
  } else {
    frameAcc = 0;
  }
  requestAnimationFrame(frame);
}

function renderTick() {
  const r = ROWS[cur];
  if (!r) return;
  $('#t-tick').textContent = String(r.tick).padStart(4, '0');
  $('#scrub').value = String(cur);
  renderMarketTick(r);
  renderBuild(r);
  renderQueue(r);
  renderStatus(r);
  renderOtherTick(r);     // 逐 tick 变化的图（GDP/实际/债务/饼图）
}

// ---------------------------------------------------------------- 市场页
function renderMarketShell() {
  const G = META.goods.length;
  $('#goodlist').innerHTML = META.goods.map((n, i) => `
    <div class="row" data-g="${i}">
      <input type="checkbox" ${hidden.has(n) ? '' : 'checked'} data-hide="${i}">
      <span class="swatch" style="background:${COLORS[i % COLORS.length]}"></span>
      <span>${n}</span>
      <span class="num ink2" style="margin-left:auto" data-px="${i}">—</span>
    </div>`).join('');
  $('#legend').innerHTML = META.goods.map((n, i) =>
    `<span class="${hidden.has(n) ? 'off' : 'on'}" data-lg="${i}">
       <i style="background:${COLORS[i % COLORS.length]}"></i>${n}</span>`).join('');
  $('#m-sub').textContent = `（${G} 个商品）`;
}

let selGood = 0;
function renderMarketTick(r) {
  // 左侧价格
  for (let i = 0; i < META.goods.length; i++) {
    const el = document.querySelector(`[data-px="${i}"]`);
    if (el) el.textContent = num(r.price ? r.price[i] : NaN);
  }
  const i = selGood;
  const g = META.goods[i];
  $('#g-name').textContent = g;
  $('#g-price').textContent = num(r.price[i]);
  const m = r.margin[i];
  const gm = $('#g-margin');
  gm.textContent = (m > 0 ? '+' : '') + pct(m);
  gm.className = m < 0 ? 'down' : (m > 0 ? 'up' : 'ink2');
  $('#g-r0').textContent = (r.ratio[i] || 0).toFixed(3);
  const pi = META.goodPinit[i];
  $('#g-r1').textContent = pi > 0 ? (r.price[i] / pi).toFixed(3) : '—';
  $('#g-sup').textContent = num(r.supply[i]);
  $('#g-dem').textContent = num(r.demand[i]);
  $('#g-pz').textContent = num(r.pzero ? r.pzero[i] : NaN);
  $('#g-wage').textContent = num(r.wageBill);
  $('#g-spend').textContent = num(r.spendNet);
  $('#g-save').textContent = num(r.savingInvest);

  drawRecent(r);
  drawFull();
  drawAll(r);
}

function drawRecent() {
  const i = selGood, half = 30;
  const lo = Math.max(0, cur - 59), hi = cur;
  const seg = arr => arr.slice(lo, hi + 1);
  const cv = $('#c-recent');
  CHART.lines(cv, {
    n: hi - lo + 1,
    log: true, base: 1,
    nodesc: ROWS[hi] ? ROWS[hi].tick : hi,
    cursor: hi - lo,
    lines: [
      { data: seg(hist.price[i]), color: COLORS[i % COLORS.length], width: 1.8 },
      { data: seg(hist.price[(i + 1) % META.goods.length]), color: COLORS[(i + 1) % COLORS.length], width: 1.2 },
    ],
  });
}
function drawFull() {
  const i = selGood;
  const base0 = hist.price[i][0] || 1;
  CHART.lines($('#c-full'), {
    n: N, log: true, base: 1,
    cursor: cur,
    lines: [{ data: hist.price[i].map(v => v / base0), color: COLORS[i % COLORS.length], width: 1.8 }],
  });
}
function drawAll(r) {
  const lines = META.goods.map((n, i) => ({
    data: hist.price[i], color: COLORS[i % COLORS.length], off: hidden.has(n), width: 1.3,
  }));
  CHART.lines($('#c-all'), { n: N, log: true, lines, cursor: cur, freeze: frozenOn });
}

// ---------------------------------------------------------------- 建筑页
function renderBuildShell() {
  $('#b-sub').textContent = `（${META.buildings.length} 类）`;
  $('#build-note').innerHTML =
    '数值来自本次运行的<b>末态</b>：<code>snapshots.jsonl</code> 里只有末行带逐建筑 ' +
    '<code>hire</code> 与 <code>cash</code>（1.0 的快照结构未包含这两个向量）；' +
    '回放到末 tick 前显示「—」。<br>' +
    '<b>现金池允许为负</b>（1.0 实测政府池可到 −1.85e12），负值带 ▼ 且不得显示为 0。';
}
function renderBuild(r) {
  const atLast = cur === N - 1;
  const lv = r.level || [];
  const rows = META.buildings.map((name, j) => {
    const level = lv[j];
    const hire = atLast && r.hire ? r.hire[j] : null;
    const cash = atLast && r.cash ? r.cash[j] : null;
    const mg = r.margin && j < r.margin.length ? r.margin[j] : null;
    const neg = cash !== null && cash < 0;
    const rateBad = hire !== null && hire < 0.05;
    let st = '<span class="tag ok">—</span>';
    if (hire !== null) {
      st = hire < 0.05 ? '<span class="tag bad">停产</span>'
         : (hire < 0.75 ? '<span class="tag warn">低雇</span>' : '<span class="tag ok">正常</span>');
    }
    const canDemo = META.buildExplicit[j];
    return `<tr>
      <td>${name}</td>
      <td class="r num">${num(level)}</td>
      <td class="r num ${rateBad ? 'down' : ''}">${hire === null ? '—' : hire.toFixed(3)}</td>
      <td class="r num ${neg ? 'down' : ''}">${cash === null ? '—' : (neg ? '▼' : '') + fmt(cash)}</td>
      <td class="r num ${mg < 0 ? 'down' : (mg > 0 ? 'up' : 'ink2')}">${mg === null ? '—' : (mg > 0 ? '+' : '') + pct(mg)}</td>
      <td class="r">${st}</td>
      <td class="r">${canDemo ? '<span class="tag warn">可拆</span>' : '<span class="tag">不可拆</span>'}</td>
    </tr>`;
  }).join('');
  $('#buildbody').innerHTML = rows;
}

// ---------------------------------------------------------------- 队列页
// 注意：1.0 的快照不含 Orders 向量，故队列只能**由相邻两期的等级差重建**：
// 等级正在增长的建筑 = 有在建订单。这是诚实标注的近似（见页面 note）。
let qpage = 0; const PER = 20;
function deriveOrders(r) {
  const prev = ROWS[cur - 1];
  if (!prev || !r.level || !prev.level) return [];
  const out = [];
  for (let j = 0; j < r.level.length; j++) {
    const d = (r.level[j] || 0) - (prev.level[j] || 0);
    if (d > 1e-9) out.push({ b: META.buildings[j], units: d, level: r.level[j] });
  }
  return out;
}
function renderQueue(r) {
  const ords = deriveOrders(r);
  const pages = Math.max(1, Math.ceil(ords.length / PER));
  if (qpage >= pages) qpage = pages - 1;
  const seg = ords.slice(qpage * PER, (qpage + 1) * PER);
  $('#queuebody').innerHTML = seg.length ? seg.map((o, k) => `
    <tr>
      <td class="r num ink2">${qpage * PER + k + 1}</td>
      <td>${o.b}</td>
      <td class="r num">${o.level.toFixed(1)}</td>
      <td class="r num up">+${o.units.toFixed(3)}</td>
      <td class="r num ink2">—</td>
      <td>本期在建</td>
      <td class="r"><span class="tag">扩建中</span></td>
    </tr>`).join('')
    : '<tr><td colspan="7" class="ink2">本期没有等级增长的建筑（无在建订单）</td></tr>';
  $('#q-page').textContent = qpage + 1;
  $('#q-pages').textContent = pages;
  $('#queue-note').innerHTML =
    '⚠ <b>本表的来源是近似</b>：1.0 的快照结构里没有 <code>Orders</code> 向量，' +
    '所以队列只能<b>由相邻两期的等级差重建</b>（等级增长 = 在建）。' +
    '要做成契约 §4.3 要求的「已建部分 ÷ 总建造力 / 剩余时间 / 来源（扩建·收购·公共工程）」，' +
    '需要在导出侧增加 Orders 向量 —— 这是 1.1 实现阶段的一项明确待办。';
}

// ---------------------------------------------------------------- 状态栏
function renderStatus(r) {
  // ΔM 与 NewCapital+Infusion 的逐行残差（A8 的口径，前端独立复算一次）
  const prev = ROWS[cur - 1];
  if (prev) {
    const dM = r.totalMoney - prev.totalMoney;
    const exp = r.newCapital + (r.infusion - prev.infusion);
    const res = dM - exp;
    const el = $('#s-money');
    el.textContent = fmt(res);
    el.className = 'v ' + (Math.abs(res) < 1e-3 ? 'up' : 'down');
  }
  const ratio = r.govDebtCap > 0 ? r.govDebt / r.govDebtCap : (r.govDebt > 0 ? Infinity : 0);
  const de = $('#s-debt');
  de.textContent = isFinite(ratio) ? ratio.toFixed(3) : '+Inf';
  de.className = 'v ' + (ratio <= 1 ? 'up' : 'down');
  $('#s-index').textContent = num(r.productIndex);
  const dot = $('#t-dot');
  const bad = !isFinite(ratio) || ratio > 1;
  dot.className = 'dot' + (bad ? ' bad' : '');
  $('#t-health').textContent = bad ? '债务越界' : '运行正常';
}

// ---------------------------------------------------------------- 其他页
function renderOtherShell() {
  // 4 张指标卡（末态）
  const last = ROWS[N - 1];
  const gdpAnnual = (last.spendNet + last.cashTotal) * 52;
  $('#other-cards').innerHTML = `
    <div class="card metric"><div class="label">名义 GDP（52 周折算）</div>
      <div class="value num">${num(gdpAnnual)}</div><div class="sub">§7.1 口径（含各池存量）</div></div>
    <div class="card metric"><div class="label">名义 GDP（本周）</div>
      <div class="value num">${num(last.spendNet + last.cashTotal)}</div>
      <div class="sub">消费者支出 ${num(last.spendNet)} + 池存量 ${num(last.cashTotal)}</div></div>
    <div class="card metric"><div class="label">实际工农总产值</div>
      <div class="value num">${num(last.grossProduct)}</div>
      <div class="sub ink2">固定 P_ref（§7.2）· 中间投入 ${num(last.productInput)}</div></div>
    <div class="card metric"><div class="label">实际工农增加值</div>
      <div class="value num ${last.productAdded < ROWS[0].productAdded ? 'down' : 'up'}">${num(last.productAdded)}</div>
      <div class="sub">首期 ${num(ROWS[0].productAdded)} · 指数 ${num(last.productIndex)}</div></div>`;

  $('#marketRow').innerHTML = `<tr><td>本地市场 #1</td>
    <td class="r num">${num(last.spendNet + last.cashTotal)}</td>
    <td class="r num">${num(gdpAnnual)}</td>
    <td class="r num">${num(last.grossProduct)}</td>
    <td class="r num ${last.productInput > last.grossProduct ? 'down' : ''}">${num(last.productInput)}</td>
    <td class="r num">${num(last.population)}</td>
    <td class="r num">${(last.happiness || 0).toFixed(4)}</td>
    <td class="r num">${weightedPrice(last)}</td></tr>`;

  // 判据表
  const A = META.assess || [];
  const pass = A.filter(a => a.pass).length;
  $('#assess-sum').textContent = `通过 ${pass} / ${A.length}`;
  $('#assessBody').innerHTML = A.map(a => `<tr>
      <td class="num">${a.id}</td><td>${a.text}</td>
      <td>${a.pass ? '<span class="tag ok">通过</span>' : '<span class="tag bad">未通过</span>'}</td>
      <td class="ink2" style="white-space:normal">${a.detail}</td></tr>`).join('');
  $('#assess-note').innerHTML =
    '判据由 <code>report.Assess</code> 在本次运行的<b>末态窗口</b>给出，前端不重算。' +
    '⚠ 依 1.0 的实测校准：A8 构造性恒真、A6 靠增长豁免短路、A4 在平态由浮点符号决定 ⇒ ' +
    '<b>“通过数”不是健康度读数</b>，真正的判别量是 A1 贴边率 / A2 亏损占比 / A7 债务÷上限 / A9 实际增加值斜率。';

  $('#pie-note').innerHTML =
    '阶级占比取自本次导出的<b>逐建筑劳动结构</b>，不写死 75/20/5（见下方图注更新）。';

  $('#s-assess').textContent = `${pass} / ${A.length}`;
  $('#s-assess').className = 'v ' + (pass >= A.length ? 'up' : 'warn');
}
// 逐 tick 变化的部分（其他页里会随回放动的图 + 饼图）
function renderOtherTick(r) {
  drawGDP(); drawPie(r); drawGov(); drawReal();
}
function weightedPrice(r) {
  if (!r.price || !r.supply) return '—';
  let n = 0, d = 0;
  for (let i = 0; i < r.price.length; i++) {
    const q = r.supply[i];
    if (isFinite(q) && q > 0) { n += q * r.price[i]; d += q; }
  }
  return d > 0 ? fmt(n / d) : '—';
}
function drawGDP() {
  CHART.lines($('#c-gdp'), {
    n: N, log: true, cursor: cur,
    lines: [
      { data: hist.gdpNom, color: getCSS('--accent'), width: 1.8 },
      { data: hist.gdpReal, color: getCSS('--up'), width: 1.8 },
    ],
  });
}
function drawReal() {
  CHART.lines($('#c-real'), {
    n: N, cursor: cur,
    lines: [
      { data: hist.gdpReal, color: getCSS('--up'), width: 2 },
      { data: hist.index.map(v => v * (ROWS[0].productAdded || 1)), color: getCSS('--neutral'), width: 1.4 },
    ],
  });
}
function drawGov() {
  CHART.lines($('#c-gov'), {
    n: N, cursor: cur,
    lines: [
      { data: hist.govCash, color: getCSS('--down'), width: 1.8 },
      { data: hist.govCap, color: getCSS('--warn'), width: 1.6 },
    ],
  });
}
function drawPie(r) {
  // 阶级比例：用**本次导出里的真实劳动结构**聚合（见 classShares），不写死 75/20/5
  const cs = classShares(r || ROWS[cur]);
  const lab = classLabels();
  const parts = [
    { n: lab[0], v: cs ? cs.p[0] : 0, color: getCSS('--accent') },
    { n: lab[1], v: cs ? cs.p[1] : 0, color: getCSS('--up') },
    { n: lab[2], v: cs ? cs.p[2] : 0, color: getCSS('--neutral') },
  ];
  CHART.pie($('#c-pie'), parts);
  const head = cs ? num(cs.total) : '—';
  const LS = META.laborStructures || [];
  const structs = Array.from(new Set(LS.map(s => s.name))).filter(Boolean).join(' · ');
  $('#pie-note').innerHTML =
    `本期就业人口（等级 × 每级人数 × 阶级占比）≈ <b>${head}</b> 人。<br>` +
    `占比取自本次导出的<b>逐建筑劳动结构</b>（${structs}）—— <b>不写死 75/20/5</b>：` +
    `农业与庄园的中档是<b>农民 7 元</b>，不是工程师 10 元。<br>` +
    `⚠ 这是"模型派生的就业结构"，与 <code>WealthTier</code> 的固定工资带 {5,10,20} 口径不同，` +
    `不能与"财富档"混读。`;
}
// 阶级占比：**按各建筑自己的劳动结构**聚合（§5 第 20 轮），不得写死 75/20/5。
// 口径：每建筑 headcount = 等级 × 每级雇佣人数；再按该建筑的 Shares 分到三档。
// 三档是"共同池下标"：0 恒为最低档（劳工 5 元），1 为中档（农业是农民 7 元、
// 城镇是工程师 10 元），2 为最高档（城镇资本家 20 元、农业/庄园为工程师 10 元）。
function classShares(r) {
  const out = [0, 0, 0];
  const LS = META.laborStructures || [];
  const per = META.laborPerLevel || [];
  const lv = r.level || [];
  for (let j = 0; j < lv.length; j++) {
    const st = LS[j]; if (!st) continue;
    const head = (lv[j] || 0) * (per[j] || 0);
    if (!(head > 0)) continue;
    for (let c = 0; c < 3; c++) out[c] += head * st.shares[c];
  }
  const tot = out[0] + out[1] + out[2];
  if (!(tot > 0)) return null;
  return { n: out, p: out.map(v => v / tot), total: tot };
}
// 标签由**工资**推断（同一档在不同结构里叫法不同）
function classLabels() {
  const LS = META.laborStructures || [];
  const midName = LS.some(s => s.wages[1] === 7) ? '工程师/农民' : '工程师';
  const topName = LS.some(s => s.wages[2] === 20) ? '资本家' : '工程师';
  return ['劳工', midName, topName];
}

// ---------------------------------------------------------------- 交互
function syncSpeed() {
  $$('.speeds button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.sp === speedKey)));
  const sp = speedOf(speedKey);
  $('#t-frames').textContent = sp.framesPerTick === 0
    ? (cur >= N - 1 ? '已到末尾' : '已暂停')
    : `1 tick / ${sp.framesPerTick} 帧`;
}
// 单步：±1 tick（慢速逐步模拟的核心操作）
function step(d) {
  speedKey = '0'; frameAcc = 0;
  cur = Math.max(0, Math.min(N - 1, cur + d));
  renderTick(); syncSpeed();
}
function showPage(name) {
  $$('.nav button').forEach(x => x.setAttribute('aria-selected', String(x.dataset.page === name)));
  $$('.page').forEach(p => p.classList.toggle('on', p.id === 'p-' + name));
}
// 深链：#build / #queue / #other（与 web/mockup.html 同一约定）
// 启动参数：?play=0 打开即暂停在 tick 1（供"慢速逐步模拟"入口使用）
function hashPage() {
  const h = (location.hash || '').replace('#', '');
  if (h && ['market', 'build', 'queue', 'other'].includes(h)) { showPage(h); redrawAll(); }
}
function queryStart() {
  try {
    const p = new URLSearchParams(location.search).get('play');
    if (p === '0') { speedKey = '0'; }
    const sp = new URLSearchParams(location.search).get('speed');
    if (sp && SPEEDS.some(s => s.key === sp)) { speedKey = sp; }
  } catch (e) { }
}
function wire() {
  $$('.nav button').forEach(b => b.addEventListener('click', () => { showPage(b.dataset.page); redrawAll(); }));
  $$('.speeds button').forEach(b => b.addEventListener('click', () => { speedKey = b.dataset.sp; frameAcc = 0; syncSpeed(); }));
  const stepF = $('#step-f'); const stepB = $('#step-b');
  if (stepF) stepF.addEventListener('click', () => step(1));
  if (stepB) stepB.addEventListener('click', () => step(-1));
  $('#scrub').addEventListener('input', e => { speedKey = '0'; cur = +e.target.value; renderTick(); syncSpeed(); });
  $('#goodlist').addEventListener('click', e => {
    const row = e.target.closest('.row'); if (!row) return;
    const idx = +row.dataset.g;
    if (e.target.dataset.hide !== undefined) {
      const n = META.goods[idx];
      hidden.has(n) ? hidden.delete(n) : hidden.add(n);
      renderMarketShell(); redrawAll(); return;
    }
    selGood = idx; renderMarketTick(ROWS[cur]);
  });
  $('#legend').addEventListener('click', e => {
    const s = e.target.closest('[data-lg]'); if (!s) return;
    const n = META.goods[+s.dataset.lg];
    hidden.has(n) ? hidden.delete(n) : hidden.add(n);
    renderMarketShell(); redrawAll();
  });
  $('#freeze').addEventListener('change', e => {
    frozenOn = e.target.checked;
    CHART.setRangeMode(frozenOn ? 'freeze' : 'auto');
    // 切换模式必须清掉缓存，否则旧范围会粘住
    $$('canvas').forEach(c => { delete c.dataset.ymin; c.dataset.ymax; c.dataset.ticks; });
    redrawAll(); updateFreezeNote();
  });
  $('#q-prev').addEventListener('click', () => { qpage = Math.max(0, qpage - 1); renderQueue(ROWS[cur]); });
  $('#q-next').addEventListener('click', () => { qpage++; renderQueue(ROWS[cur]); });
  document.addEventListener('keydown', e => {
    const q = $('#p-queue').classList.contains('on');
    if (e.key === ' ') { e.preventDefault(); speedKey = speedOf(speedKey).framesPerTick > 0 ? '0' : '1'; syncSpeed(); }
    if (e.key === '.') { e.preventDefault(); step(1); }      // 单步前进
    if (e.key === ',') { e.preventDefault(); step(-1); }     // 单步后退
    if (e.key === 'ArrowRight') { if (q) { qpage++; renderQueue(ROWS[cur]); } else step(1); }
    if (e.key === 'ArrowLeft')  { if (q) { qpage = Math.max(0, qpage - 1); renderQueue(ROWS[cur]); } else step(-1); }
  });
  window.addEventListener('resize', () => { clearTimeout(window.__rz); window.__rz = setTimeout(redrawAll, 140); });
  window.addEventListener('hashchange', hashPage);
}
function updateFreezeNote() {
  $('#freeze-note').innerHTML = frozenOn
    ? '✅ <b>比例尺已冻结</b>：轴范围由<b>全量数据</b>算一次，隐藏某个商品<b>只移除它的线</b>，' +
      '不会改变网格与刻度 —— 否则「隐藏一条线」会被读成「剩下的线变陡了」。' +
      '可用 <code>tools/verify_scale_freeze.ps1</code> 复验（比对不同隐藏数下的 ' +
      '<code>data-ymin/ymax/ticks</code>，应为 1 种）。'
    : '⚠ <b>已切到「按可见序列拟合」</b>（旧行为，仅作对照）：此时取消/开启商品会改变轴范围，' +
      '同样的数据会被映射成不同斜率 —— 这正是契约 §0.2d 登记的比例尺缺陷。';
}

// ---------------------------------------------------------------- 重绘
function redrawAll() {
  if (!N) return;
  renderMarketTick(ROWS[cur]);
  renderOtherTick(ROWS[cur]);   // 只重画会动的图；静态表不重建（契约 §六：节点复用）
  renderStatus(ROWS[cur]);
}

// ---------------------------------------------------------------- 启动
(async () => {
  try {
    $('#t-health').textContent = '载入中…';
    await load();
    buildHistory();
    renderMarketShell();
    renderBuildShell();
    $('#b-sub').textContent = `（${META.buildings.length} 类）`;
    $('#scrub').max = String(N - 1);
    $('#s-src').textContent = `${N} 行 / every=${META.every}`;
    wire();
    renderOtherShell();      // 静态部分（指标卡/对照表/判据表）只在启动时建一次
    cur = 0;
    renderTick();            // 逐 tick 部分（含 renderOtherTick）
    updateFreezeNote();
    queryStart();            // ?play=0 / ?speed=1/8
    syncSpeed();
    hashPage();          // 深链：最后执行，确保数据已就绪
    requestAnimationFrame(frame);
  } catch (e) {
    const box = $('#err');
    box.style.display = 'block';
    box.innerHTML = '<b>无法载入数据</b>：' + e.message +
      '<br><br>请先导出数据并启动只读服务（在 <code>gosim/</code> 目录下）：' +
      '<br><code>go run ./cmd/market-sim -ticks 1000 -export-dir ../out/webdata -serve :8787</code>' +
      '<br>然后打开 <code>http://localhost:8787/</code>。';
    $('#t-health').textContent = '无数据';
    $('#t-dot').className = 'dot bad';
  }
})();
})();
