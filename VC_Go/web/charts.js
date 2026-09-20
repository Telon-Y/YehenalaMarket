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
    // 【不要在这里做防零除兜底】历史上这里有一句 mx = mn + (|mn|*0.01+1)，
    // 它在 mn===mx===1（归一化曲线的 tick 1，完全合法）时把范围撑成 [1, 2.01]，
    // 于是下游的最小量程逻辑看到"跨度已够"就跳过 —— 一个兜底把另一个修复屏蔽掉。
    // 兜底只作**最后的安全网**，放在范围决策（expandToMinSpan）之后。
    return [mn, mx];
  }

  // 按**可见窗口**取数据：窗口从 0 起，长度 len。
  // 为什么这么做：以前用全量数据算范围，而图只画到光标处 ⇒ 早期数据被压在底部、
  // 拖动进度条时"图看着变了"。改成窗口口径后，x 轴与 y 轴都只覆盖看得见的部分。
  function windowed(data, len) { return data.slice(0, Math.max(1, Math.min(data.length, len))); }

  /**
   * 最小相对跨度：把范围撑开到至少 span 倍（默认 1，即不撑）。
   *
   * 【为什么需要】"全期价格指数"这类归一化曲线在早期**跨度≈1**（tick 1 时恰好等于 1）。
   * 若按数据自适应，坐标轴会被放大到 ±0.5% 的尺度，于是任何微小数值抖动都铺满整张图，
   * 看起来就是"比例尺异常"。这类比值图必须有一个**有意义的最小量程**。
   *
   * 【往哪撑】用 minLower 指定下界的上限：比值图把 minLower 设为 1/h（h=√span），
   * 使**基期值恰好落在量程顶部**——因为指数只可能往下走，线上方不该留大段空白。
   */
  function expandToMinSpan(mn, mx, log, span, minLower) {
    if (!(span > 1)) return [mn, mx];
    if (!(mx > mn) || (log ? (mx / mn) : (mx - mn)) < span) {
      if (log) {
        let c = Math.sqrt(Math.max(mn, 1e-12) * Math.max(mx, mn));
        if (minLower > 0) c = Math.min(c, minLower);
        const h = Math.sqrt(span);
        return [c / h, c * h];
      }
      const c = (mn + mx) / 2;
      return [c - span / 2, c + span / 2];
    }
    return [mn, mx];
  }

  /**
   * 画多序列折线。
   * opts: { n, lines:[{data,color,width,label,off}], log, base, yRange, freeze, xLabel }
   *
   * R-scale（两条规则，都要守住）：
   *   ① **轴范围不得依赖可见性**：范围由**未隐藏的全体序列**算出（不是只算画出来的那些），
   *      故"隐藏某条线"只移除它的像素，不动网格与刻度。
   *   ② **范围跟随可见窗口**：同一窗口下范围**冻结**（WeakMap 缓存），拖动进度条时保持一致；
   *      窗口长度变化（回放推进）时才重算一次。用 opts.windowKey 标识窗口（一般传当前下标）。
   */
  function lines(cv, opts) {
    const { x, w, h } = setup(cv);
    const padL = 60, padR = 12, padT = 10, padB = 24;
    const iw = w - padL - padR, ih = h - padT - padB;

    let mn, mx;
    const freeze = opts.freeze !== false;
    const key = (opts.windowKey !== undefined) ? String(opts.windowKey) : '';
    if (opts.yRange) { mn = opts.yRange[0]; mx = opts.yRange[1]; }
    else if (freeze && frozen.has(cv) && frozen.get(cv).key === key) {
      const r = frozen.get(cv); mn = r.mn; mx = r.mx;
    } else {
      // 规则①：用**未隐藏**的全体序列（此时 data 已是窗口化的切片）
      const all = opts.lines.filter(l => !isOff(l)).map(l => l.data);
      const src = all.length ? all : opts.lines.map(l => l.data);
      [mn, mx] = rangeOf(src, opts.log);
      if (freeze) frozen.set(cv, { mn, mx, key });
    }
    if (opts.log) mn = Math.max(mn, 1e-9);
    // 先撑到最小量程，再兜底防零除（顺序很重要：先撑开才不会退化成 ±0.5%）
    const span = opts.minSpanRatio || 1;
    const minLower = opts.minLower || 0;
    [mn, mx] = expandToMinSpan(mn, mx, !!opts.log, span, minLower);
    // 最后的安全网：只有在范围仍然退化时才生效（见 rangeOf 的注释）
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
    // 数据线：先画"整条历史"的重影（淡），再画"已播放"的实线。
    // 【为什么】"全期"图在回放早期只有一两个点，看起来像坏图（空图）。
    // 画出完整历史的重影，既让图立刻可读，又能一眼看出播放进度。
    for (const l of opts.lines) {
      if (isOff(l)) continue;
      if (l.ghost && l.ghost.length) {
        x.strokeStyle = l.color; x.globalAlpha = 0.22; x.lineWidth = 1;
        x.beginPath();
        let st = false;
        for (let i = 0; i < l.ghost.length; i++) {
          const v = l.ghost[i];
          if (!isFinite(v)) { st = false; continue; }
          const px = X(i), py = Y(v);
          if (!st) { x.moveTo(px, py); st = true; } else x.lineTo(px, py);
        }
        x.stroke();
        x.globalAlpha = 1;
      }
    }
    for (const l of opts.lines) {
      if (isOff(l)) continue;
      x.strokeStyle = l.color; x.lineWidth = l.width || 1.5; x.beginPath();
      let started = false, lastPx = 0, lastPy = 0, pts = 0;
      for (let i = 0; i < l.data.length; i++) {
        const v = l.data[i];
        if (!isFinite(v)) { started = false; continue; }
        const px = X(i), py = Y(v);
        if (!started) { x.moveTo(px, py); started = true; } else x.lineTo(px, py);
        lastPx = px; lastPy = py; pts++;
      }
      x.stroke();
      // 【单点也要可见】归一化序列在首期只有一个点（比值恰好为 1），
      // 只画 stroke 会什么都看不到、看起来像"图是坏的"。补一个端点标记。
      if (pts === 1) {
        x.fillStyle = l.color;
        x.beginPath(); x.arc(lastPx, lastPy, 3.2, 0, Math.PI * 2); x.fill();
      }
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

  return { lines, pie, bars, fmt, setRangeMode, getRangeMode, rangeOf, windowed };
})();
