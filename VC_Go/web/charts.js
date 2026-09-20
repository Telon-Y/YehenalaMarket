/* =============================================================================
   charts.js —— 原生 canvas 绘图（无任何依赖）
   契约 §六 的五条义务在这里落地：
     · 节点复用（图表用一块 canvas，不做 DOM 重建）
     · 帧与 tick 解耦（本文件不碰帧率，只按传入的 tick 画）
     · R-scale：轴范围**不得依赖可见性**（见 setRangeMode / frozen）
     · R-zero ：防零除用相对量，绝不能写 mx = mn + 1
   ============================================================================= */

const CHART = (() => {
  const css = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();

  // 轴范围模式：'auto' = 每次按可见序列拟合；'freeze' = 按全量算一次后冻结
  let rangeMode = 'freeze';
  const frozen = new WeakMap();   // canvas -> {mn,mx}

  function setRangeMode(m) { rangeMode = m; }
  function getRangeMode() { return rangeMode; }

  function setup(cv, h) {
    const dpr = window.devicePixelRatio || 1;
    const w = cv.clientWidth || 600;
    const hh = h || parseInt(cv.getAttribute('height'), 10) || 200;
    cv.width = Math.max(1, Math.round(w * dpr));
    cv.height = Math.max(1, Math.round(hh * dpr));
    const x = cv.getContext('2d');
    x.setTransform(dpr, 0, 0, dpr, 0, 0);
    x.clearRect(0, 0, w, hh);
    return { x, w, h: hh };
  }

  function fmt(v) {
    if (!isFinite(v)) return '—';
    const a = Math.abs(v);
    if (a >= 1e12) return (v / 1e12).toFixed(2) + 'e12';
    if (a >= 1e9)  return (v / 1e9).toFixed(2) + 'e9';
    if (a >= 1e6)  return (v / 1e6).toFixed(2) + 'e6';
    if (a >= 1e4)  return v.toLocaleString('en-US', { maximumFractionDigits: 0 });
    if (a >= 1)    return v.toFixed(2);
    return v.toPrecision(3);
  }

  // 从若干序列算范围（用于 'auto' 与新 canvas 的首次冻结）
  function rangeOf(series, log) {
    let mn = Infinity, mx = -Infinity;
    for (const s of series) {
      for (const v of s) { if (isFinite(v)) { if (v < mn) mn = v; if (v > mx) mx = v; } }
    }
    if (!isFinite(mn)) { mn = 0; mx = 1; }
    if (log) mn = Math.max(mn, 1e-9);
    // R-zero：相对量兜底，绝不 mx = mn + 1
    if (!(mx > mn)) mx = mn + (Math.abs(mn) * 0.01 + 1);
    return [mn, mx];
  }

  /**
   * 画多序列折线。
   * opts: { n, lines:[{data,color,width,label,off}], log, base, yRange, freeze:true|false, xLabel }
   * R-scale 规则：
   *   freeze=true（默认） ⇒ 首次用**全量**数据定范围并缓存；隐藏序列只少画线，轴不动。
   *   freeze=false        ⇒ 'auto' 模式：按可见序列拟合（旧行为，仅作对照）。
   */
  function lines(cv, opts) {
    const { x, w, h } = setup(cv);
    const padL = 60, padR = 12, padT = 10, padB = 24;
    const iw = w - padL - padR, ih = h - padT - padB;

    let mn, mx;
    const freeze = opts.freeze !== false;
    if (opts.yRange) { mn = opts.yRange[0]; mx = opts.yRange[1]; }
    else if (freeze && frozen.has(cv)) { const r = frozen.get(cv); mn = r.mn; mx = r.mx; }
    else {
      // 全量（忽略 off）算范围 —— 这是 R-scale 的关键
      const all = opts.lines.map(l => l.data);
      [mn, mx] = rangeOf(freeze ? all : opts.lines.filter(l => !isOff(l)).map(l => l.data), opts.log);
      if (freeze) frozen.set(cv, { mn, mx });
    }
    if (opts.log) mn = Math.max(mn, 1e-9);
    if (!(mx > mn)) mx = mn + (Math.abs(mn) * 0.01 + 1);

    const isLog = !!opts.log;
    const Y = v => {
      if (!isFinite(v)) return padT + ih;
      if (isLog) {
        const l0 = Math.log10(mn), l1 = Math.log10(mx);
        return padT + ih - (Math.log10(Math.max(v, 1e-9)) - l0) / (l1 - l0) * ih;
      }
      return padT + ih - (v - mn) / (mx - mn) * ih;
    };
    const n = opts.n;
    const X = i => padL + (n <= 1 ? 0 : (i / (n - 1)) * iw);

    // 网格 + 刻度（记下刻度文本，自检要用）
    x.strokeStyle = css('--grid'); x.lineWidth = 1;
    x.fillStyle = css('--ink-2'); x.font = '12px ' + css('--mono');
    const ticks = [];
    for (let k = 0; k <= 4; k++) {
      const v = isLog ? Math.pow(10, Math.log10(mn) + (Math.log10(mx) - Math.log10(mn)) * k / 4)
                      : mn + (mx - mn) * k / 4;
      const y = Math.round(Y(v)) + 0.5;
      x.beginPath(); x.moveTo(padL, y); x.lineTo(w - padR, y); x.stroke();
      x.textAlign = 'right'; x.fillText(fmt(v), padL - 6, y + 4);
      ticks.push(fmt(v));
    }
    if (opts.base !== undefined) {
      const y = Math.round(Y(opts.base)) + 0.5;
      x.strokeStyle = css('--neutral'); x.setLineDash([5, 4]);
      x.beginPath(); x.moveTo(padL, y); x.lineTo(w - padR, y); x.stroke(); x.setLineDash([]);
    }
    // 游标（当前 tick）
    if (opts.cursor !== undefined && n > 1) {
      const cx = Math.round(X(opts.cursor)) + 0.5;
      x.strokeStyle = css('--accent'); x.globalAlpha = .35; x.lineWidth = 1.5;
      x.beginPath(); x.moveTo(cx, padT); x.lineTo(cx, padT + ih); x.stroke();
      x.globalAlpha = 1;
    }
    // 数据线
    for (const l of opts.lines) {
      if (isOff(l)) continue;
      x.strokeStyle = l.color; x.lineWidth = l.width || 1.5; x.beginPath();
      let started = false;
      for (let i = 0; i < l.data.length; i++) {
        const v = l.data[i];
        if (!isFinite(v)) { started = false; continue; }
        const px = X(i), py = Y(v);
        if (!started) { x.moveTo(px, py); started = true; } else x.lineTo(px, py);
      }
      x.stroke();
    }
    x.fillStyle = css('--ink-2'); x.font = '12px ' + css('--mono');
    x.textAlign = 'left';  x.fillText('0', padL, h - 6);
    x.textAlign = 'right'; x.fillText('tick ' + (opts.nodesc || n), w - padR, h - 6);

    // 把比例尺写进 DOM，供 tools/verify_scale_freeze.ps1 自检（canvas 文字读不到）
    cv.dataset.ymin = String(mn);
    cv.dataset.ymax = String(mx);
    cv.dataset.ticks = ticks.join('|');
  }

  function isOff(l) { return l.off === true; }

  function pie(cv, parts) {
    const { x, w, h } = setup(cv);
    const cx = w * 0.30, cy = h / 2, r = Math.max(20, Math.min(h / 2 - 8, w * 0.22));
    const total = parts.reduce((s, p) => s + (p.v > 0 ? p.v : 0), 0) || 1;
    let a0 = -Math.PI / 2;
    for (const p of parts) {
      const frac = (p.v > 0 ? p.v : 0) / total;
      if (frac <= 0) continue;
      const a1 = a0 + frac * Math.PI * 2;
      x.beginPath(); x.moveTo(cx, cy); x.arc(cx, cy, r, a0, a1); x.closePath();
      x.fillStyle = p.color; x.fill();
      x.strokeStyle = css('--bg'); x.lineWidth = 2; x.stroke();
      a0 = a1;
    }
    x.font = '13px ' + css('--font');
    parts.forEach((p, i) => {
      const ly = cy - r + i * 22;
      x.fillStyle = p.color; x.fillRect(w * 0.58, ly - 10, 10, 10);
      x.fillStyle = css('--ink');
      x.fillText(p.n + '  ' + ((p.v > 0 ? p.v : 0) / total * 100).toFixed(1) + '%', w * 0.58 + 16, ly);
    });
  }

  function bars(cv, items) {
    const { x, w, h } = setup(cv);
    const padL = 8, padT = 12, padB = 8;
    const rowH = (h - padT - padB) / Math.max(1, items.length);
    items.forEach((it, i) => {
      const y = padT + i * rowH;
      x.font = '13px ' + css('--font'); x.fillStyle = css('--ink');
      x.fillText(it.n, padL, y + 12);
      x.font = '13px ' + css('--mono'); x.fillStyle = css('--ink-2');
      x.fillText(it.txt, padL, y + 28);
      const bx = padL + 140, bw = Math.max(40, w - padL - 8 - 140), bh = 12, by = y + 8;
      x.fillStyle = css('--grid'); x.fillRect(bx, by, bw, bh);
      const frac = isFinite(it.v) ? Math.max(0, Math.min(1, it.v)) : 0;
      x.fillStyle = it.color; x.fillRect(bx, by, frac * bw, bh);
      if (it.thr !== undefined && isFinite(it.thr)) {
        const tx = bx + Math.max(0, Math.min(1, it.thr)) * bw;
        x.strokeStyle = css('--ink-2'); x.setLineDash([3, 3]);
        x.beginPath(); x.moveTo(tx, by - 3); x.lineTo(tx, by + bh + 3); x.stroke(); x.setLineDash([]);
      }
    });
  }

  return { lines, pie, bars, fmt, setRangeMode, getRangeMode, rangeOf };
})();
