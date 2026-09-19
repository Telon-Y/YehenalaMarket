// =============================================================================
//  VC_Go/tools/tex2html.js   (rewrite)
//  把 LaTeX 源文件编译为**自包含、离线可用**的 HTML。
//
//  为什么不用 pandoc / MathJax / KaTeX：
//    本机没有 pandoc，也没有 LaTeX 发行版；取 CDN 被网络策略阻断；
//    pnpm 在沙箱下 spawn EPERM；DSH 自带前端里的 KaTeX 是压缩进 vendor
//    bundle 的混淆符号，无法可靠复用。
//    因此这里针对本文档用到的 LaTeX 子集自写编译器：
//      结构层：section / itemize / enumerate / quote / table / tabular(x)
//      数学层：自定义 TeX 数学解析器 -> 纯 HTML+CSS，无需外部库
//
//  用法：
//      node VC_Go/tools/tex2html.js <input.tex> <output.html>
//      node VC_Go/tools/tex2html.js --selftest <original.md>   # 校验数学覆盖率
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

// =============================================================================
// 1. 数学：TeX 子集 -> AST -> HTML
// =============================================================================
const SYMBOLS = {
  alpha: 'α', beta: 'β', gamma: 'γ', delta: 'δ', epsilon: 'ε', varepsilon: 'ε',
  zeta: 'ζ', eta: 'η', theta: 'θ', iota: 'ι', kappa: 'κ', lambda: 'λ', mu: 'μ',
  nu: 'ν', xi: 'ξ', pi: 'π', rho: 'ρ', sigma: 'σ', tau: 'τ', upsilon: 'υ',
  phi: 'φ', varphi: 'φ', chi: 'χ', psi: 'ψ', omega: 'ω',
  Gamma: 'Γ', Delta: 'Δ', Theta: 'Θ', Lambda: 'Λ', Xi: 'Ξ', Pi: 'Π',
  Sigma: 'Σ', Upsilon: 'Υ', Phi: 'Φ', Psi: 'Ψ', Omega: 'Ω',
  times: '×', div: '÷', cdot: '·', pm: '±', mp: '∓', ast: '∗',
  le: '≤', leq: '≤', ge: '≥', geq: '≥', ne: '≠', neq: '≠', equiv: '≡',
  approx: '≈', sim: '∼', simeq: '≃', propto: '∝', ll: '≪', gg: '≫',
  in: '∈', notin: '∉', subset: '⊂', supset: '⊃', cup: '∪', cap: '∩',
  forall: '∀', exists: '∃', neg: '¬', land: '∧', lor: '∨',
  to: '→', rightarrow: '→', leftarrow: '←', leftrightarrow: '↔',
  Rightarrow: '⇒', Leftarrow: '⇐', Leftrightarrow: '⇔',
  mapsto: '↦', uparrow: '↑', downarrow: '↓',
  sum: '∑', prod: '∏', int: '∫', oint: '∮',
  nabla: '∇', partial: '∂', infty: '∞', emptyset: '∅', varnothing: '∅',
  sqrt: '√', angle: '∠', perp: '⊥', parallel: '∥', therefore: '∴', because: '∵',
  ldots: '…', cdots: '⋯', dots: '…', vdots: '⋮', ddots: '⋱',
  quad: '\u2003', qquad: '\u2003\u2003',
};

const OPNAMES = new Set([
  'sin', 'cos', 'tan', 'cot', 'sec', 'csc', 'arcsin', 'arccos', 'arctan',
  'sinh', 'cosh', 'tanh', 'log', 'ln', 'lg', 'exp', 'det', 'dim', 'lim',
  'max', 'min', 'sup', 'inf', 'arg', 'ker', 'deg', 'gcd', 'Pr',
]);

const DELIMS = {
  '(': '(', ')': ')', '[': '[', ']': ']', '|': '|', '.': '',
  lbrace: '{', rbrace: '}', langle: '⟨', rangle: '⟩',
  lvert: '|', rvert: '|', vert: '|', Vert: '‖',
};

const esc = (s) => String(s)
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;');

class MathParser {
  constructor(s) { this.s = s; this.i = 0; }

  eof() { return this.i >= this.s.length; }
  peek(o = 0) { return this.s[this.i + o]; }
  skipWs() { while (!this.eof() && /\s/.test(this.peek())) this.i++; }

  readRawGroup() {
    this.skipWs();
    if (this.peek() !== '{') return '';
    let depth = 0;
    const start = this.i;
    for (; this.i < this.s.length; this.i++) {
      const c = this.s[this.i];
      if (c === '{') depth++;
      else if (c === '}') { depth--; if (depth === 0) { this.i++; return this.s.slice(start + 1, this.i - 1); } }
    }
    return this.s.slice(start + 1);
  }

  readArgs(n) {
    const out = [];
    for (let k = 0; k < n; k++) {
      this.skipWs();
      if (this.peek() === '{') {
        this.i++;                       // 吃掉 '{'
        const body = this.parseUntil(true); // 组内解析，按深度识别组尾
        if (this.peek() === '}') this.i++;  // 吃掉 '}'
        out.push({ type: 'group', body });
      } else {
        const b = this.parseBase();
        out.push(b ? { type: 'group', body: [b] } : { type: 'group', body: [] });
      }
    }
    return out;
  }

  isStop(c) { return c === undefined || c === '}' || c === ']' || c === ')' || c === '&'; }

  // 从 pos 起，判断是否存在与 bracket 配对的闭合括号（跳过嵌套与嵌套组）。
  // 用于回答"这个 ')' 是普通内容还是真正的组尾"。
  hasMatchingClose(pos, open, close) {
    let depth = 0;
    for (let k = pos; k < this.s.length; k++) {
      const c = this.s[k];
      if (c === open) depth++;
      else if (c === close) { if (depth === 0) return true; depth--; }
    }
    return false;
  }

  // parseUntil 解析一串单元，直到遇到【本层的】停止符。
  //
  // 这里是本渲染器最容易错的地方，历史上有两次同类 bug：
  //   ① 早期遇到【任何】'}' 就停 → \frac{420}{1 + e^{-0.25(w-10)}} 的分母被
  //      组内 '}' 截断，分子分母错位、输出混入裸花括号；
  //   ② 修好花括号后，')' ']' 仍被当作无条件停止符 → e^{-0.25(w-10)} 里的
  //      '(w-10)' 让解析在上标中途停止，指数内容只剩 "-0.25(w-10"。
  //
  // 正确规则：三类括号都按【配对】判断——
  //   · '(' '[' 视为普通内容（其内部由后续单元自行消费）
  //   · ')' ']' 仅当【后面还有与之配对的开括号】时才是普通内容，否则是本层组尾
  //   · '}' 只由 inGroup 决定是否为组尾（花括号是显式的组语法）
  //
  // inGroup=true 表示当前在某个 {...} 内部（由 parseBase / readArgs 调用）。
  parseUntil(inGroup = false) {
    const nodes = [];
    while (!this.eof()) {
      const c = this.peek();
      if (c === '{') {
        const n = this.parseUnit();   // 子组整体消费，内部 '}' 不会泄漏到此层
        if (n) nodes.push(n);
        continue;
      }
      if (c === '}') break;           // 花括号始终是本层组尾
      if (c === '&') break;           // 对齐符：交给上层（如 cases 环境）
      if (c === ')' || c === ']') {
        const open = c === ')' ? '(' : '[';
        if (!this.hasMatchingClose(this.i + 1, open, c)) break; // 无配对 → 本层组尾
        // 有配对 → 当作普通符号继续
        this.i++;
        nodes.push(this.atom(c));
        continue;
      }
      const n = this.parseUnit();
      if (n) nodes.push(n);
    }
    return nodes;
  }

  parseBase() {
    this.skipWs();
    const c = this.peek();
    if (c === undefined) return null;
    if (c === '{') { this.i++; const body = this.parseUntil(true); if (this.peek() === '}') this.i++; return { type: 'group', body }; }
    if (c === '\\') return this.parseCommand();
    this.i++;
    return this.atom(c);
  }

  parseUnit() {
    const c = this.peek();
    if (c === '^' || c === '_') {
      // 不应单独出现；作为普通符号处理
      this.i++;
      return { type: 'op', value: c };
    }
    return this.parseBase();
  }

  parseDelim() {
    this.skipWs();
    const c = this.peek();
    if (c === '\\') {
      const m = /^\\([a-zA-Z]+)/.exec(this.s.slice(this.i));
      if (m) { this.i += m[0].length; return DELIMS[m[1]] !== undefined ? DELIMS[m[1]] : m[1]; }
      this.i++;
      return '';
    }
    this.i++;
    return DELIMS[c] !== undefined ? DELIMS[c] : c;
  }

  parseCommand() {
    this.i++; // backslash
    const m = /^([a-zA-Z]+)/.exec(this.s.slice(this.i));
    let name;
    if (m) { name = m[1]; this.i += name.length; }
    else { name = this.s[this.i] || ''; this.i++; }

    if (name === ',' || name === ';' || name === ':' || name === '!' || name === ' ') {
      return { type: 'space', size: name === ' ' ? 0.3 : 0.17 };
    }
    if (name === '\\') return { type: 'break' };
    if (name === 'text' || name === 'mathrm' || name === 'textrm' || name === 'operatorname' || name === 'mathbb') {
      return { type: 'text', value: this.readRawGroup() };
    }
    if (name === 'mathbf' || name === 'textbf' || name === 'boldsymbol') {
      return { type: 'bold', body: new MathParser(this.readRawGroup()).parseUntil() };
    }
    if (name === 'mathit') {
      return { type: 'italicGroup', body: new MathParser(this.readRawGroup()).parseUntil() };
    }
    if (name === 'frac' || name === 'dfrac' || name === 'tfrac') {
      const [num, den] = this.readArgs(2);
      return { type: 'frac', num, den };
    }
    if (name === 'sqrt') {
      const [b] = this.readArgs(1);
      return { type: 'sqrt', body: b.body };
    }
    if (name === 'left' || name === 'right') {
      const d = this.parseDelim();
      return { type: 'delim', value: d };
    }
    if (/^(big|Big|bigg|Bigg)$/.test(name)) {
      this.skipWs();
      const d = this.peek() === '\\' ? this.parseDelim() : (this.i++, this.peek(-1));
      return { type: 'delim', value: DELIMS[d] !== undefined ? DELIMS[d] : d };
    }
    if (name === 'displaystyle' || name === 'textstyle' || name === 'limits' ||
        name === 'nolimits' || name === 'notag' || name === 'nonumber') {
      return null;
    }
    if (OPNAMES.has(name)) return { type: 'opname', value: name };
    if (SYMBOLS[name] !== undefined) return { type: 'sym', value: SYMBOLS[name] };
    return { type: 'sym', value: name };
  }

  atom(c) {
    if (/[0-9]/.test(c)) return { type: 'num', value: c };
    if (c === '-' || c === '+') {
      // 二元/一元都按关系符处理
      return { type: 'op', value: c === '-' ? '−' : '+' };
    }
    if (/[=<>*!,;:|]/.test(c)) return { type: 'op', value: c === '*' ? '∗' : c };
    if (/[a-zA-Z]/.test(c)) return { type: 'var', value: c };
    if (c === '.') return { type: 'op', value: '.' };
    return { type: 'ord', value: c };
  }
}

// 处理上下标：在原子序列上做一遍后处理，绑定 ^ 与 _
function parseMath(src) {
  const p = new MathParser(src);
  const raw = [];
  while (!p.eof()) {
    p.skipWs();
    if (p.eof()) break;
    const c = p.peek();
    if (c === '^' || c === '_') {
      p.i++;
      const arg = p.parseBase();
      raw.push({ type: c === '^' ? 'sup' : 'sub', arg: arg || { type: 'group', body: [] } });
      continue;
    }
    const n = p.parseBase();
    if (n) raw.push(n);
  }
  // 绑定：把 sup/sub 附加到前一个原子
  const out = [];
  for (const node of raw) {
    if (node.type === 'sup' || node.type === 'sub') {
      let base = out.pop();
      if (!base) base = { type: 'ord', value: '' };
      if (base.type === 'script') {
        base[node.type] = node.arg;
        out.push(base);
      } else {
        const sc = { type: 'script', base, sup: undefined, sub: undefined };
        sc[node.type] = node.arg;
        out.push(sc);
      }
    } else {
      out.push(node);
    }
  }
  return out;
}

function renderNodes(nodes) {
  return (nodes || []).map(renderNode).join('');
}

function body(n) {
  if (!n) return [];
  if (n.type === 'group') return n.body;
  if (n.type === 'script') return [n];
  return [n];
}

function renderNode(n) {
  if (!n) return '';
  switch (n.type) {
    case 'num': return `<span class="mn">${esc(n.value)}</span>`;
    case 'op': return `<span class="mo">${esc(n.value)}</span>`;
    case 'sym': return `<span class="mo">${esc(n.value)}</span>`;
    case 'ord': return `<span class="mo">${esc(n.value)}</span>`;
    case 'var': return `<span class="mi">${esc(n.value)}</span>`;
    case 'text': return `<span class="mtext">${esc(n.value)}</span>`;
    case 'opname': return `<span class="mopname">${esc(n.value)}</span>`;
    case 'space': return `<span style="display:inline-block;width:${n.size}em"></span>`;
    case 'break': return '<br>';
    case 'bold': return `<span class="mtext" style="font-weight:700">${renderNodes(n.body)}</span>`;
    case 'italicGroup': return `<span class="mi">${renderNodes(n.body)}</span>`;
    case 'group': return renderNodes(n.body);
    case 'delim': return `<span class="mdelim">${esc(n.value)}</span>`;
    case 'sqrt':
      return `<span class="msqrt"><span class="msqrt-rad">√</span><span class="msqrt-body">${renderNodes(n.body)}</span></span>`;
    case 'frac':
      return `<span class="mfrac"><span class="mfrac-num">${renderNodes(n.num.body)}</span>` +
             `<span class="mfrac-den">${renderNodes(n.den.body)}</span></span>`;
    case 'script': {
      const hasSup = n.sup !== undefined;
      const hasSub = n.sub !== undefined;
      const sup = hasSup ? `<span class="msup">${renderNodes(body(n.sup))}</span>` : '';
      const sub = hasSub ? `<span class="msub">${renderNodes(body(n.sub))}</span>` : '';
      if (hasSup && hasSub) {
        return `<span class="mscripts">${renderNode(n.base)}<span class="mscripts-col">${sup}${sub}</span></span>`;
      }
      return `${renderNode(n.base)}${sup}${sub}`;
    }
    default: return '';
  }
}

function renderMath(src) {
  try {
    const nodes = parseMath(src);
    const html = renderNodes(nodes);
    if (!html.replace(/<[^>]*>/g, '').trim() && !/mfrac|msqrt/.test(html)) {
      return { html: `<span class="math-error">${esc(src)}</span>`, ok: false };
    }
    return { html, ok: true };
  } catch (e) {
    return { html: `<span class="math-error" title="${esc(e.message)}">${esc(src)}</span>`, ok: false };
  }
}

// =============================================================================
// 2. 内联文本（段落 / 表格单元格）
// =============================================================================
function renderInline(text) {
  const out = [];
  const s = String(text);
  let i = 0;
  while (i < s.length) {
    const c = s[i];

    if (c === '\\') {
      const t = /^\\(textbf|emph|textit|texttt)\s*\{/.exec(s.slice(i));
      if (t) {
        const open = i + t[0].length - 1;
        const close = matchBrace(s, open);
        const inner = s.slice(open + 1, close);
        const tag = t[1] === 'textbf' ? 'strong' : t[1] === 'emph' || t[1] === 'textit' ? 'em' : 'code';
        out.push(`<${tag}>${renderInline(inner)}</${tag}>`);
        i = close + 1;
        continue;
      }
      if (s[i + 1] === '\\') { out.push('<br>'); i += 2; continue; }
      if ('&%_#{}'.includes(s[i + 1])) { out.push(esc(s[i + 1])); i += 2; continue; }
      if (s[i + 1] === '$') { out.push('$'); i += 2; continue; }
      const nm = /^\\([a-zA-Z]+)/.exec(s.slice(i));
      if (nm) { out.push(esc(nm[0])); i += nm[0].length; continue; }
      out.push(esc(s[i])); i++; continue;
    }

    if (c === '$') {
      const disp = s[i + 1] === '$';
      const open = disp ? '$$' : '$';
      const close = s.indexOf(open, i + open.length);
      if (close !== -1) {
        const expr = s.slice(i + open.length, close);
        const forceDisp = /\\displaystyle|\\sum|\\frac|\\prod|\\int/.test(expr);
        const r = renderMath(expr);
        const cls = disp || forceDisp ? 'math math-display-inline' : 'math';
        out.push(`<span class="${cls}">${r.html}</span>`);
        i = close + open.length;
        continue;
      }
      out.push(esc(c)); i++; continue;
    }

    let j = i;
    while (j < s.length && s[j] !== '\\' && s[j] !== '$') j++;
    out.push(esc(s.slice(i, j)));
    i = j;
  }
  return out.join('');
}

// =============================================================================
// 3. 工具
// =============================================================================
function matchBrace(s, open) {
  let depth = 0;
  for (let i = open; i < s.length; i++) {
    if (s[i] === '{') depth++;
    else if (s[i] === '}') { depth--; if (depth === 0) return i; }
  }
  return s.length;
}

function matchBracket(s, open) {
  const close = s.indexOf(']', open);
  return close === -1 ? s.length : close;
}

// 按顶层分隔符切分（考虑 {} 与 [] 嵌套）
function splitTop(s, sep) {
  const parts = [];
  let depth = 0;
  let cur = '';
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '{' || c === '[') depth++;
    else if (c === '}' || c === ']') depth--;
    if (depth === 0 && s.startsWith(sep, i)) {
      parts.push(cur);
      cur = '';
      i += sep.length - 1;
      continue;
    }
    cur += c;
  }
  parts.push(cur);
  return parts;
}

// 行首块级命令（段落遇到它必须停下）
const BLOCK_START = /^\\(begin|end|section|subsection|subsubsection|paragraph|item|title|author|date|maketitle|caption|centering|small|footnotesize|normalsize|label|toprule|midrule|bottomrule)\b/;

// 扫描一个环境：从 \begin{env} 之后开始，返回 { body, next }
function scanEnv(src, i, env) {
  const endTag = '\\end{' + env + '}';
  const j = src.indexOf(endTag, i);
  return { body: src.slice(i, j === -1 ? src.length : j), next: j === -1 ? src.length : j + endTag.length };
}

// =============================================================================
// 4. 表格
// =============================================================================
function renderTable(colSpec, content, caption) {
  const cols = [];
  for (const ch of colSpec) {
    if (ch === 'l' || ch === 'X' || ch === 'Y') cols.push('left');
    else if (ch === 'r') cols.push('right');
    else if (ch === 'c') cols.push('center');
  }
  const strip = (t) => t.replace(/\\(toprule|midrule|bottomrule|hline)\b/g, '').trim();
  const rowsAll = splitTop(content, '\\\\')
    .map((r) => r.trim())
    .filter((r) => r && !/^\\(toprule|midrule|bottomrule|hline)\s*$/.test(r));
  if (!rowsAll.length) return '';

  const toCells = (row) => splitTop(row, '&').map((c) => strip(c));
  const header = toCells(rowsAll[0]);
  const rows = rowsAll.slice(1).map(toCells);
  const ncol = Math.max(cols.length, header.length, ...rows.map((r) => r.length), 1);
  const align = (k) => cols[k] || 'left';

  let html = '<div class="table-wrap"><table>';
  if (caption) html += `<caption>${renderInline(caption)}</caption>`;
  html += '<thead><tr>';
  for (let k = 0; k < ncol; k++) html += `<th class="${align(k)}">${renderInline(header[k] || '')}</th>`;
  html += '</tr></thead><tbody>';
  for (const r of rows) {
    html += '<tr>';
    for (let k = 0; k < ncol; k++) html += `<td class="${align(k)}">${renderInline(r[k] || '')}</td>`;
    html += '</tr>';
  }
  return html + '</tbody></table></div>';
}

// =============================================================================
// 5. 块级解析（单趟递归下降）
// =============================================================================
function parseBlocks(src, opts = {}) {
  const clean = String(src).replace(/\r\n/g, '\n');
  const out = [];
  const counters = { sec: 0, sub: 0 };
  const parentSec = opts._sec || 0;
  counters.sec = parentSec;

  let i = 0;
  const n = clean.length;

  while (i < n) {
    const c = clean[i];
    if (globalThis.__TRACE) {
      globalThis.__TRACE.push('ITER i=' + i + ' c=' + JSON.stringify(c) + ' next=' + JSON.stringify(clean.slice(i, i + 12)));
    }

    // 空白
    if (/\s/.test(c)) { i++; continue; }

    // 注释
    if (c === '%') {
      const nl = clean.indexOf('\n', i);
      i = nl === -1 ? n : nl + 1;
      continue;
    }

    // ---- 环境 ----
    if (c === '\\') {
      const envM = /^\\begin\{([a-zA-Z*]+)\}/.exec(clean.slice(i));
      if (envM) {
        const env = envM[1];
        if (env === 'document') {
          const r = scanEnv(clean, i + envM[0].length, 'document');
          out.push(...parseBlocks(r.body, { _sec: counters.sec }));
          i = r.next;
          continue;
        }
        if (env === 'itemize' || env === 'enumerate' || env === 'description') {
          const r = scanEnv(clean, i + envM[0].length, env);
          const items = splitTop(r.body, '\\item').slice(1);
          const tag = env === 'enumerate' ? 'ol' : 'ul';
          let list = `<${tag}>`;
          for (const it of items) {
            const lbl = /^\s*\[([^\]]*)\]/.exec(it);
            const itemSrc = lbl ? it.slice(lbl[0].length) : it;
            const inner = parseBlocks(itemSrc, { skipItemParsing: true, _sec: counters.sec }).join('');
            list += '<li>' +
              (lbl ? `<span class="item-label">${renderInline(lbl[1])}</span>` : '') +
              (inner || `<p>${renderInline(itemSrc.trim())}</p>`) +
              '</li>';
          }
          list += `</${tag}>`;
          out.push(list);
          i = r.next;
          continue;
        }
        if (env === 'quote' || env === 'quotation') {
          const r = scanEnv(clean, i + envM[0].length, env);
          out.push(`<blockquote>${parseBlocks(r.body, { _sec: counters.sec }).join('')}</blockquote>`);
          i = r.next;
          continue;
        }
        if (env === 'table' || env === 'table*') {
          const r = scanEnv(clean, i + envM[0].length, env);
          const capM = /\\caption\{/.exec(r.body);
          let caption = null;
          if (capM) {
            const ob = r.body.indexOf('{', capM.index);
            caption = r.body.slice(ob + 1, matchBrace(r.body, ob));
          }
          const tabM = /\\begin\{(tabularx|tabular|longtable)\}/.exec(r.body);
          if (tabM) {
            const inner = scanEnv(r.body, tabM.index + tabM[0].length, tabM[1]);
            // 依次消费：可选 [..]、宽度宏（\textwidth 等）、列格式 {@{}l L L@{}}
            let rest = inner.body.replace(/^\s+/, '');
            let consumed = inner.body.length - rest.length;
            const take = (re) => {
              const m = re.exec(rest);
              if (!m) return false;
              consumed += m[0].length;
              rest = rest.slice(m[0].length);
              return true;
            };
            take(/^\[[^\]]*\]\s*/);
            take(/^\{\s*\\[a-zA-Z]+\s*\}\s*/);          // 宽度：{\textwidth}
            let colSpec = '';
            if (rest[0] === '{') {
              const cb = matchBrace(rest, 0);
              colSpec = rest.slice(1, cb).trim();        // 内容含嵌套 {}，必须用 matchBrace
              consumed += cb + 1;
            }
            out.push(renderTable(colSpec || 'l', inner.body.slice(consumed), caption));
          }
          i = r.next;
          continue;
        }
        if (env === 'equation' || env === 'equation*' || env === 'align' || env === 'align*' ||
            env === 'displaymath' || env === 'gather' || env === 'gather*') {
          const r = scanEnv(clean, i + envM[0].length, env);
          const cleaned = r.body.replace(/&/g, '').replace(/\\label\{[^}]*\}/g, '');
          for (const ch of cleaned.split('\\\\')) {
            if (!ch.trim()) continue;
            out.push(`<div class="equation">${renderMath(ch).html}</div>`);
          }
          i = r.next;
          continue;
        }
        if (env === 'center' || env === 'flushleft' || env === 'flushright') {
          const r = scanEnv(clean, i + envM[0].length, env);
          out.push(`<div class="${env}">${parseBlocks(r.body, { _sec: counters.sec }).join('')}</div>`);
          i = r.next;
          continue;
        }
        // 未知环境：继续解析内部
        i += envM[0].length;
        continue;
      }

      // ---- 章节 ----
      const secM = /^\\(section|subsection|subsubsection|paragraph)\*?\{/.exec(clean.slice(i));
      if (secM) {
        const ob = clean.indexOf('{', i);
        const cb = matchBrace(clean, ob);
        const title = clean.slice(ob + 1, cb);
        const lvl = secM[1];
        let num;
        if (lvl === 'section') { counters.sec++; counters.sub = 0; num = String(counters.sec); }
        else if (lvl === 'subsection') { counters.sub++; num = `${counters.sec}.${counters.sub}`; }
        else { num = `${counters.sec}.${counters.sub}`; }
        const tag = lvl === 'section' ? 'h2' : lvl === 'subsection' ? 'h3' : 'h4';
        const id = `sec-${num.replace(/\./g, '-')}`;
        out.push(`<${tag} id="${id}"><span class="sec-num">${num}</span>${renderInline(title)}</${tag}>`);
        i = cb + 1;
        continue;
      }

      // ---- 丢弃型命令 ----
      // 注意：命令名后面必须是 { [ 空白 或行尾，否则它只是别的宏的前缀。
      //   例如 \textbf 的前 6 个字符与 \title 完全相同，宽松匹配会把
      //   \textbf{...} 当成 \title 吃掉（这条曾导致大段粗体文字丢失）。
      const dropM = /^\\(maketitle|title|author|date|centering|small|footnotesize|normalsize|noindent|bigskip|medskip|smallskip|tableofcontents|newpage|clearpage|label|vspace|hspace|documentclass|usepackage|geometry|hypersetup|renewcommand|newcolumntype|setlist|setCJK|CTEXsetup|arraystretch)(?![a-zA-Z])(?=[\s{\[\\]|$)/.exec(clean.slice(i));
      if (dropM) {
        let k = i + dropM[0].length;
        // 吃掉紧随其后的 [..] 与 {..} 参数
        for (;;) {
          const w = /^\s*(\[[^\]]*\]|\{[^{}]*\})/.exec(clean.slice(k));
          if (!w) break;
          k += w[0].length;
        }
        // 这些命令多为"整行声明"，把该行剩余部分一并跳过
        if (/^(centering|small|footnotesize|normalsize|noindent|bigskip|medskip|smallskip|tableofcontents|newpage|clearpage)$/.test(dropM[1])) {
          const nl = clean.indexOf('\n', k);
          k = nl === -1 ? n : nl + 1;
        }
        i = k;
        continue;
      }

      // ---- @{} 之类的列间距声明：整体吃掉 ----
      const atM = /^@\{[^{}]*\}/.exec(clean.slice(i));
      if (atM) { i += atM[0].length; continue; }

      // ---- 顶层裸 \item（容错） ----
      if (/^\\item\b/.test(clean.slice(i))) { i += 5; continue; }

      // ---- 其它反斜杠命令（\textbf 等内联宏）----
      //   绝不能在这里消费命令名或反斜杠：\textbf{...} 之类的内联宏必须原样
      //   留在文本里交给 renderInline。这里什么都不做，控制流自然落到下方
      //   段落分支，由它把整段（含反斜杠）收进 chunk。
      if (globalThis.__TRACE) globalThis.__TRACE.push('  INLINE-PASS i=' + i);
    }

    // ---- 列表项内遇到裸 \item：交回外层 ----
    if (opts.skipItemParsing && /^\\item\b/.test(clean.slice(i, i + 5))) break;

    // ---- 段落 ----
    let j = i;
    let chunk = '';
    let consumed = i;
    if (globalThis.__DUMP2 && clean.includes('标准需求量')) {
      globalThis.__DUMP2.push('ENTER-PARA i=' + i + ' cleanJSON=' + JSON.stringify(clean.slice(0, 60)));
    }
    while (j < n) {
      const nl = clean.indexOf('\n', j);
      const line = nl === -1 ? clean.slice(j) : clean.slice(j, nl);
      const trimmed = line.trim();
      const next = nl === -1 ? n : nl + 1;
      if (trimmed === '') { consumed = next; break; }
      if (BLOCK_START.test(trimmed)) {
        // 该行开启块级结构，段落到此结束。
        //   游标必须停在**该行的行首**（跳过行首空白，但不能跳过整行）：
        //   \begin{equation} 这类命令后面还有自己的环境体，若跳过整行就会
        //   把环境体当成普通段落。
        if (opts.skipItemParsing && /^\\item\b/.test(trimmed)) { consumed = n; break; }
        consumed = j;
        while (consumed < n && (clean[consumed] === ' ' || clean[consumed] === '\t')) consumed++;
        break;
      }
      chunk = chunk ? chunk + ' ' + trimmed : trimmed;
      if (globalThis.__DUMP2 && clean.includes('标准需求量')) {
        globalThis.__DUMP2.push('  LINE j=' + j + ' trimmed=' + JSON.stringify(trimmed) + ' chunk=' + JSON.stringify(chunk));
      }
      consumed = next;
      if (nl === -1) break;
      j = nl + 1;
    }
    if (consumed <= i) consumed = i + 1;
    i = consumed;
    if (chunk) {
      if (globalThis.__DUMP) {
        globalThis.__DUMP.push({
          chunk,
          inline: renderInline(chunk),
        });
      }
      out.push(`<p>${renderInline(chunk)}</p>`);
    }
  }

  return out;
}

// =============================================================================
// 6. 样式与模板
// =============================================================================
const CSS = `
:root{--ink:#1f2328;--ink-2:#57606a;--line:#d8dee4;--bg:#fff;--accent:#0969da;--panel:#f6f8fa}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-size:16px;line-height:1.85;
  font-family:"Segoe UI","PingFang SC","Microsoft YaHei","Hiragino Sans GB",system-ui,sans-serif}
.wrap{display:flex;max-width:1400px;margin:0 auto;align-items:flex-start}
.sidebar{position:sticky;top:0;width:270px;flex:0 0 270px;max-height:100vh;overflow:auto;
  padding:32px 18px 48px 24px;border-right:1px solid var(--line);font-size:13.5px}
.sidebar h4{margin:0 0 10px;font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--ink-2)}
.sidebar ol{list-style:none;margin:0;padding:0}
.sidebar a{color:var(--ink-2);text-decoration:none;display:block;padding:3px 8px;border-radius:5px;line-height:1.5}
.sidebar a:hover{background:var(--panel);color:var(--accent)}
.sidebar .sub{padding-left:14px;font-size:13px}
main{flex:1 1 auto;min-width:0;padding:40px 48px 90px}
h1{font-size:30px;line-height:1.3;margin:0 0 6px}
.doc-meta{color:var(--ink-2);font-size:14px;margin:0 0 34px;padding-bottom:18px;border-bottom:2px solid var(--ink)}
h2{font-size:22px;margin:44px 0 14px;padding-bottom:7px;border-bottom:1px solid var(--line)}
h3{font-size:17.5px;margin:30px 0 10px}
h4{font-size:16px;margin:24px 0 8px}
.sec-num{color:var(--accent);margin-right:.5em;font-weight:600}
p{margin:.7em 0}
ul,ol{margin:.6em 0;padding-left:1.7em}
li{margin:.3em 0}
.item-label{font-weight:600;margin-right:.4em}
blockquote{margin:1.1em 0;padding:.75em 1.1em;background:var(--panel);
  border-left:4px solid var(--accent);border-radius:0 6px 6px 0}
blockquote p{margin:.25em 0}
code{background:rgba(175,184,193,.2);padding:.15em .38em;border-radius:4px;
  font-family:Consolas,"Cascadia Mono",Menlo,monospace;font-size:.9em}
.equation{margin:1.25em 0;padding:.9em 1em;text-align:center;overflow-x:auto;
  background:linear-gradient(180deg,#fbfcfd,#f6f8fa);border:1px solid var(--line);border-radius:8px}
.table-wrap{overflow-x:auto;margin:1.2em 0}
table{border-collapse:collapse;width:100%;font-size:14.5px;line-height:1.65}
caption{caption-side:top;text-align:left;color:var(--ink-2);font-size:13.5px;padding-bottom:6px}
th,td{border-bottom:1px solid var(--line);padding:7px 12px;vertical-align:top}
thead th{border-bottom:2px solid var(--ink);background:var(--panel);font-weight:600;white-space:nowrap}
tbody tr:hover{background:#fafbfc}
.left{text-align:left}.right{text-align:right}.center{text-align:center}
tbody td.right{font-variant-numeric:tabular-nums}
.math{font-family:"Cambria Math","Latin Modern Math","STIX Two Math",Cambria,"Times New Roman",Georgia,serif;
  font-size:1.06em;white-space:nowrap;color:#111}
.math-display-inline{display:inline-block}
.mi{font-style:italic}
.mtext,.mopname{font-style:normal;font-family:inherit;font-size:.94em}
.mopname{padding-right:.16em}
.mo{padding:0 .06em}
.mfrac{display:inline-flex;flex-direction:column;vertical-align:middle;text-align:center;margin:0 .22em;line-height:1.18}
.mfrac-num{border-bottom:1.1px solid currentColor;padding:0 .28em}
.mfrac-den{padding:0 .28em}
.msup{font-size:.72em;vertical-align:super;line-height:0;margin-left:.02em}
.msub{font-size:.72em;vertical-align:sub;line-height:0;margin-left:.02em}
.mscripts{display:inline-flex;align-items:center}
.mscripts-col{display:inline-flex;flex-direction:column;font-size:.72em;line-height:1.05;margin-left:.06em}
.mscripts-col .msup,.mscripts-col .msub{vertical-align:baseline;font-size:1em;margin:0}
.msqrt-body{border-top:1.1px solid currentColor;padding:.08em .18em 0 .06em}
.msqrt-rad{transform:scaleY(1.15)}
.mdelim{padding:0 .05em}
.math-error{color:#b42318;background:#fff1f0;border-bottom:1px dashed #b42318;padding:0 .2em}
@media (max-width:1000px){
  .wrap{display:block}
  .sidebar{position:static;width:auto;max-height:none;border-right:0;border-bottom:1px solid var(--line)}
  main{padding:24px 20px 70px}
}
@media print{.sidebar{display:none}main{padding:0}.equation,table{break-inside:avoid}}
`;

function buildToc(bodyHtml) {
  const items = [];
  const re = /<h([23]) id="([^"]+)">([\s\S]*?)<\/h\1>/g;
  let m;
  while ((m = re.exec(bodyHtml))) {
    items.push({ lvl: +m[1], id: m[2], text: m[3].replace(/<[^>]*>/g, '').trim() });
  }
  let html = '<h4>目录</h4><ol>';
  for (const it of items) {
    html += it.lvl === 2
      ? `<li><a href="#${it.id}">${esc(it.text)}</a></li>`
      : `<li class="sub"><a href="#${it.id}">${esc(it.text)}</a></li>`;
  }
  return html + '</ol>';
}

function compile(tex, opts = {}) {
  const m = /\\begin\{document\}/.exec(tex);
  let content = m ? tex.slice(m.index + m[0].length) : tex;
  const end = content.lastIndexOf('\\end{document}');
  if (end !== -1) content = content.slice(0, end);

  const blocks = parseBlocks(content);
  const body = blocks.join('\n');
  const title = opts.title || '1.0 生产与市场模拟（最终版）';

  const stats = {
    equations: (body.match(/class="equation"/g) || []).length,
    tables: (body.match(/<table>/g) || []).length,
    headings: (body.match(/<h[23] /g) || []).length,
    mathErrors: (body.match(/math-error/g) || []).length,
    leftovers: (body.match(/\\[a-zA-Z]+|\\begin|\\end/g) || []).length,
  };

  const html = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(title)}</title>
<style>${CSS}</style>
</head>
<body>
<div class="wrap">
<nav class="sidebar">${buildToc(body)}</nav>
<main>
<h1>${esc(title)}</h1>
<p class="doc-meta">叶赫那拉市场 · 由 <code>VC_Go/docs/1.0 生产与市场模拟.tex</code> 经 <code>VC_Go/tools/tex2html.js</code> 离线编译生成${opts.sourceNote ? ' · ' + esc(opts.sourceNote) : ''}</p>
${body}
</main>
</div>
</body>
</html>
`;
  return { html, stats };
}

// =============================================================================
// 7. 自检：原文所有公式是否都能渲染
// =============================================================================
function selftest(mdPath) {
  const md = fs.readFileSync(mdPath, 'utf8');
  const exprs = [];
  let m;
  const blockRe = /\$\$([\s\S]+?)\$\$/g;
  while ((m = blockRe.exec(md))) exprs.push(m[1].trim());
  const inlineRe = /(?<!\$)\$([^$\n]+?)\$(?!\$)/g;
  while ((m = inlineRe.exec(md))) exprs.push(m[1].trim());
  const seen = new Set();
  const fails = [];
  let ok = 0;
  for (const e of exprs) {
    if (!e || seen.has(e)) continue;
    seen.add(e);
    const r = renderMath(e);
    if (r.ok) ok++; else fails.push(e);
  }
  return { total: seen.size, ok, fails };
}

// =============================================================================
// 8. CLI
// =============================================================================
function main() {
  const args = process.argv.slice(2);
  if (args[0] === '--selftest') {
    const r = selftest(args[1] || '1.0 生产与市场模拟.md');
    console.log(`数学表达式自检：${r.ok}/${r.total} 渲染成功`);
    for (const f of r.fails) console.log('  失败: ' + f);
    process.exit(r.fails.length ? 1 : 0);
  }
  const [inFile, outFile] = args;
  if (!inFile || !outFile) {
    console.error('用法: node VC_Go/tools/tex2html.js <input.tex> <output.html>');
    console.error('      node VC_Go/tools/tex2html.js --selftest [original.md]');
    process.exit(2);
  }
  const tex = fs.readFileSync(inFile, 'utf8');
  const { html, stats } = compile(tex, { sourceNote: '完全自包含，无需联网' });
  fs.mkdirSync(path.dirname(path.resolve(outFile)), { recursive: true });
  fs.writeFileSync(outFile, html, 'utf8');
  console.log(`已生成 ${outFile}`);
  console.log(`  公式块 ${stats.equations} · 表格 ${stats.tables} · 标题 ${stats.headings} · ` +
              `渲染失败 ${stats.mathErrors} · LaTeX 残留 ${stats.leftovers}`);
  if (stats.mathErrors) process.exit(1);
}

if (require.main === module) main();

module.exports = { compile, renderMath, renderInline, parseBlocks, selftest };
