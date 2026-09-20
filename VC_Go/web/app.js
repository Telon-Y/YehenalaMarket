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
let speed = 1;               // 0 = 暂停
let acc = 0, lastTs = 0;     // 帧与 tick 解耦用的累计器
const hidden = new Set();    // 被取消显示的商品名
let frozenOn = true;

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
  if (speed > 0 && N > 0) {
    acc += dt * speed;                                // 契约 §五：1 tick/s × 档位
    let steps = Math.floor(acc);
    if (steps > 0) {
      acc -= steps;
      cur = Math.min(N - 1, cur + steps);
      renderTick();
      if (cur >= N - 1) { speed = 0; syncSpeed(); }   // 到末尾自动暂停
    }
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
    '按<b>固定阶级</b>切（本导出未含逐池人口，故按 1.0 §5 的<b>劳动结构</b>推算：' +
    '城镇 75/20/5、农业 75/20/5（农民代工程师））。' +
    '⚠ <code>WealthTier</code> 用的是固定工资带 {5,10,20}，与农业结构不一致，图注须注明。';

  $('#s-assess').textContent = `${pass} / ${A.length}`;
  $('#s-assess').className = 'v ' + (pass >= A.length ? 'up' : 'warn');

  drawGDP(); drawPie(); drawGov(); drawReal();
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
function drawPie() {
  // 阶级比例：由各期 population × 劳动结构权重推算（导出未含逐池人口）
  const last = ROWS[N - 1];
  CHART.pie($('#c-pie'), [
    { n: '劳工（劳工档）', v: 0.75, color: getCSS('--accent') },
    { n: '工程师/农民档', v: 0.20, color: getCSS('--up') },
    { n: '资本家档', v: 0.05, color: getCSS('--neutral') },
  ]);
  void last;
}
function getCSS(v) { return getComputedStyle(document.documentElement).getPropertyValue(v).trim(); }

// ---------------------------------------------------------------- 交互
function syncSpeed() {
  $$('.speeds button').forEach(b => b.setAttribute('aria-pressed', String(+b.dataset.sp === speed)));
  $('#t-frames').textContent = speed === 0 ? '已暂停'
    : `1 tick / ${(60 / speed).toFixed(0)} 帧`;
}
function showPage(name) {
  $$('.nav button').forEach(x => x.setAttribute('aria-selected', String(x.dataset.page === name)));
  $$('.page').forEach(p => p.classList.toggle('on', p.id === 'p-' + name));
}
// 深链：#build / #queue / #other（与 web/mockup.html 同一约定）
function hashPage() {
  const h = (location.hash || '').replace('#', '');
  if (h && ['market', 'build', 'queue', 'other'].includes(h)) { showPage(h); redrawAll(); }
}
function wire() {
  $$('.nav button').forEach(b => b.addEventListener('click', () => { showPage(b.dataset.page); redrawAll(); }));
  $$('.speeds button').forEach(b => b.addEventListener('click', () => { speed = +b.dataset.sp; acc = 0; syncSpeed(); }));
  $('#scrub').addEventListener('input', e => { cur = +e.target.value; renderTick(); });
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
    if (e.key === ' ') { e.preventDefault(); speed = speed === 0 ? 1 : 0; syncSpeed(); }
    if (e.key === 'ArrowRight') { if (q) { qpage++; renderQueue(ROWS[cur]); } else { cur = Math.min(N - 1, cur + 1); renderTick(); } }
    if (e.key === 'ArrowLeft')  { if (q) { qpage = Math.max(0, qpage - 1); renderQueue(ROWS[cur]); } else { cur = Math.max(0, cur - 1); renderTick(); } }
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
  renderOtherShell();
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
    renderOtherShell();
    cur = 0;
    renderTick();
    updateFreezeNote();
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
