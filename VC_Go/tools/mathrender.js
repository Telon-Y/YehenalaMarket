// VC_Go/tools/mathrender.js —— 自包含的 LaTeX 子集 → HTML 渲染器
//
// 为什么自己写：
//   原先 md2html.js 借用 tex2html.js 的 renderMath，而那是为**整份手写 LaTeX 文档**
//   设计的解析器：它把上下标内容平铺（P_{cost} → P_cost）、\frac 的参数被括号截断
//   （\frac{420}{1+e^{-0.25(w-10)}} 分子分母错位）、\top 与 \begin{cases} 完全不支持。
//   拿它当 Markdown 的数学后端，等于让 Markdown 里最标准的 $...$ 走一条为别的输入
//   设计的路径——公式大面积显示异常正源于此。
//   本文件按**本文档实际用到的记号集**（VC_Go/tools/latex_inventory.js 统计得出）从头实现，
//   输出标准 HTML（<sub>/<sup>/<table>），正确性优先。
//
// 用法：const { render } = require('./mathrender.js'); render('\\frac{a}{b}')
'use strict';

// ---------------------------------------------------------------- 符号表
const SYMBOLS = {
  // 小写希腊
  alpha: 'α', beta: 'β', gamma: 'γ', delta: 'δ', epsilon: 'ε', varepsilon: 'ε',
  zeta: 'ζ', eta: 'η', theta: 'θ', iota: 'ι', kappa: 'κ', lambda: 'λ', mu: 'μ',
  nu: 'ν', xi: 'ξ', pi: 'π', rho: 'ρ', sigma: 'σ', tau: 'τ', upsilon: 'υ',
  phi: 'φ', varphi: 'φ', chi: 'χ', psi: 'ψ', omega: 'ω',
  // 大写希腊
  Gamma: 'Γ', Delta: 'Δ', Theta: 'Θ', Lambda: 'Λ', Xi: 'Ξ', Pi: 'Π',
  Sigma: 'Σ', Upsilon: 'Υ', Phi: 'Φ', Psi: 'Ψ', Omega: 'Ω',
  // 运算与关系
  times: '×', div: '÷', cdot: '·', pm: '±', mp: '∓', ast: '∗',
  le: '≤', leq: '≤', ge: '≥', geq: '≥', ne: '≠', neq: '≠', equiv: '≡',
  approx: '≈', sim: '∼', simeq: '≃', propto: '∝', ll: '≪', gg: '≫',
  in: '∈', notin: '∉', subset: '⊂', supset: '⊃', cup: '∪', cap: '∩',
  forall: '∀', exists: '∃', neg: '¬', land: '∧', lor: '∨',
  to: '→', rightarrow: '→', leftarrow: '←', leftrightarrow: '↔',
  Rightarrow: '⇒', Leftarrow: '⇐', Leftrightarrow: '⇔',
  mapsto: '↦', uparrow: '↑', downarrow: '↓',
  // 大算符与杂项
  sum: '∑', prod: '∏', int: '∫', oint: '∮',
  nabla: '∇', partial: '∂', infty: '∞', emptyset: '∅', varnothing: '∅',
  angle: '∠', perp: '⊥', parallel: '∥', therefore: '∴', because: '∵',
  top: 'ᵀ', bot: '⊥', quad: '\u2003', qquad: '\u2003\u2003',
  ldots: '…', cdots: '⋯', dots: '…', vdots: '⋮', ddots: '⋱',
  lbrace: '{', rbrace: '}', langle: '⟨', rangle: '⟩',
  lvert: '|', rvert: '|', vert: '|', Vert: '‖',
  '%': '%', '&': '&', '#': '#', '_': '_', '{': '{', '}': '}',
  '\\': '\n',
};

// 函数名（正体显示，前面留空隙）
const OPNAMES = new Set([
  'sin', 'cos', 'tan', 'cot', 'sec', 'csc', 'arcsin', 'arccos', 'arctan',
  'sinh', 'cosh', 'tanh', 'log', 'ln', 'lg', 'exp', 'det', 'dim',
  'lim', 'max', 'min', 'sup', 'inf', 'arg', 'ker', 'deg', 'gcd', 'Pr',
]);

// 需要固定间距的记号（避免 "a b" 粘连）
const THIN = new Set(['\\,', '\\;', '\\:', '\\!', '\\ ']);
const MEDIUM = new Set(['\\quad', '\\qquad']);

const esc = (s) => String(s)
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// ---------------------------------------------------------------- 解析器
class Parser {
  constructor(src) { this.s = src; this.i = 0; this.errors = []; }
  eof() { return this.i >= this.s.length; }
  peek(o = 0) { return this.s[this.i + o]; }
  skipWs() { while (!this.eof() && /\s/.test(this.peek())) this.i++; }

  // 读取一个 {...} 组的原始文本（不做解析），用于 \text 等
  readRaw() {
    this.skipWs();
    if (this.peek() !== '{') {
      // 无花括号：只取一个字符
      const c = this.peek();
      this.i++;
      return c || '';
    }
    this.i++;
    let depth = 1;
    const start = this.i;
    while (this.i < this.s.length) {
      const c = this.s[this.i];
      if (c === '{') depth++;
      else if (c === '}') { depth--; if (depth === 0) break; }
      this.i++;
    }
    const out = this.s.slice(start, this.i);
    if (this.peek() === '}') this.i++;
    return out;
  }

  // 读取一个"参数"：{...} 或单个记号，返回其 HTML
  readArg() {
    this.skipWs();
    if (this.peek() === '{') {
      this.i++;
      // 关键：必须在此声明停止集，否则 parseAtom 会吃掉收尾的 '}'。
      // 历史 bug：^{†} 中，readArg 内层 parseAtom 处理完 † 后又消费了 '}'，
      // 导致上标内容缺失、<sup> 整段丢失。
      const html = this.parseSeq(() => this.peek() === '}');
      if (this.peek() === '}') this.i++;
      return html;
    }
    return this.parseAtom();
  }

  // 解析一段序列，直到 stop() 为真
  parseSeq(stop = () => false) {
    const out = [];
    let guard = 0;
    while (!this.eof() && !stop()) {
      if (++guard > 100000) throw new Error('mathrender: 解析未收敛（疑似死循环）');
      const before = this.i;
      const h = this.parseAtom();
      if (h !== null && h !== '') out.push(h);
      // 防止 parseAtom 未推进位置（例如遇到空组）导致死循环
      if (this.i === before) this.i++;
      // 元素取完后再次检查停止条件：parseAtom 可能已越过边界字符
      if (!this.eof() && stop()) break;
    }
    return out.join('');
  }

  // 解析一个原子，并处理其后紧跟的上下标
  parseAtom() {
    this.skipWs();
    if (this.eof()) return '';
    const c = this.peek();

    // 分组
    if (c === '{') {
      this.i++;
      const html = this.parseSeq(() => this.peek() === '}');
      if (this.peek() === '}') this.i++;
      return this.attachScripts(html);
    }

    // 命令
    if (c === '\\') return this.attachScripts(this.parseCommand());

    // 普通字符
    if (c === '^' || c === '_') {
      // 孤立上下标：没有基底（例如表格脚注标记 $^{†}$）。
      //
      // 必须在此【消费该字符并直接读参数】。历史 bug：曾写成
      // `this.i++; return this.attachScripts('')` —— 而 attachScripts 会自己
      // 再去寻找 '^'/'_'，但那个字符刚被消费掉，于是什么都没解析，返回空串，
      // 表现为 ^{†} 渲染成裸的 †（上标丢失）。
      this.i++;
      const arg = this.readArg();
      return c === '^'
        ? `<sup class="msup">${arg}</sup>`
        : `<sub class="msub">${arg}</sub>`;
    }
    this.i++;

    let html;
    if (/[0-9.]/.test(c)) html = `<span class="mn">${esc(c)}</span>`;
    else if (/[a-zA-Z]/.test(c)) html = `<i class="mi">${esc(c)}</i>`;
    else if (c === '-') html = '<span class="mo">−</span>';
    else if (c === '*') html = '<span class="mo">∗</span>';
    else if (c === ' ') html = '';
    else html = `<span class="mo">${esc(c)}</span>`;   // 含 † 等非 ASCII 记号
    return this.attachScripts(html);
  }

  // 若后面紧跟 _ 或 ^，把当前原子作为基底。
  // 用 undefined 表示"没有上下标"，以区分"上下标存在但内容为空"的情形。
  attachScripts(base) {
    let sub, sup;
    for (;;) {
      this.skipWs();
      const c = this.peek();
      if (c === '_' && sub === undefined) { this.i++; sub = this.readArg(); continue; }
      if (c === '^' && sup === undefined) { this.i++; sup = this.readArg(); continue; }
      break;
    }
    if (sub === undefined && sup === undefined) return base;
    let out = base;
    if (sub !== undefined) out += `<sub class="msub">${sub}</sub>`;
    if (sup !== undefined) out += `<sup class="msup">${sup}</sup>`;
    return out === '' ? '' : out;
  }

  parseCommand() {
    this.i++; // 反斜杠
    const m = /^([a-zA-Z]+)/.exec(this.s.slice(this.i));
    let name;
    if (m) { name = m[1]; this.i += name.length; }
    else { name = this.s[this.i] || ''; this.i++; }

    // 间距
    if (name === ',' || name === ':' || name === ';' || name === '!') {
      const w = name === '!' ? -0.17 : 0.17;
      return `<span style="display:inline-block;width:${w}em"></span>`;
    }
    if (name === ' ') return '<span style="display:inline-block;width:.3em"></span>';
    if (name === '\\') return '<br>';
    if (name === 'quad' || name === 'qquad') {
      return `<span style="display:inline-block;width:${name === 'quad' ? 1 : 2}em"></span>`;
    }

    // 文本类
    if (name === 'text' || name === 'textrm' || name === 'mathrm' ||
        name === 'operatorname' || name === 'mbox') {
      const raw = this.readRaw();
      // \text 内部可能含 $...$（少见），这里按纯文本处理并保留中文
      return `<span class="mtext">${esc(raw)}</span>`;
    }
    if (name === 'mathbf' || name === 'textbf' || name === 'boldsymbol') {
      return `<b class="mbf">${this.readArg()}</b>`;
    }
    if (name === 'mathit') return `<i class="mi">${this.readArg()}</i>`;
    if (name === 'mathbb') {
      const raw = this.readRaw();
      return `<span class="mbb">${esc(raw)}</span>`;
    }

    // 分式
    if (name === 'frac' || name === 'dfrac' || name === 'tfrac') {
      const num = this.readArg();
      const den = this.readArg();
      const cls = name === 'dfrac' ? 'mfrac mfrac-d' : 'mfrac';
      return `<span class="${cls}"><span class="mfrac-num">${num}</span><span class="mfrac-den">${den}</span></span>`;
    }

    // 根号（可带次数）
    if (name === 'sqrt') {
      this.skipWs();
      let deg = null;
      if (this.peek() === '[') {
        this.i++;
        deg = this.parseSeq(() => this.peek() === ']');
        if (this.peek() === ']') this.i++;
      }
      const body = this.readArg();
      const d = deg ? `<sup class="msqrt-deg">${deg}</sup>` : '';
      return `<span class="msqrt">${d}<span class="msqrt-sym">√</span><span class="msqrt-body">${body}</span></span>`;
    }

    // 自适应括号
    if (name === 'left' || name === 'right' || name === 'middle') {
      return this.parseDelim();
    }
    if (/^(big|Big|bigg|Bigg)$/.test(name)) {
      return this.parseDelim(true);
    }

    // 方程组等环境
    if (name === 'begin') return this.parseEnv();
    if (name === 'end') { this.readRaw(); return ''; }

    // 忽略的排版命令
    if (name === 'displaystyle' || name === 'textstyle' || name === 'scriptstyle' ||
        name === 'limits' || name === 'nolimits' || name === 'notag' ||
        name === 'nonumber' || name === 'label' || name === 'tag') {
      if (name === 'label' || name === 'tag') this.readRaw();
      return '';
    }

    // 函数名
    if (OPNAMES.has(name)) return `<span class="mopname">${name}</span>`;

    // 符号
    if (SYMBOLS[name] !== undefined) {
      const v = SYMBOLS[name];
      const cls = /^[a-zA-Z]$/.test(v) ? 'mi' : 'mo';
      // 希腊字母等按斜体变量显示
      if (cls === 'mi') return `<i class="mi">${v}</i>`;
      return `<span class="mo">${esc(v)}</span>`;
    }

    // 未知命令：原样显示，便于发现遗漏
    this.errors.push(name);
    return `<span class="munknown">\\${esc(name)}</span>`;
  }

  parseDelim(collapsed = false) {
    this.skipWs();
    let d = this.peek();
    if (d === '\\') {
      const m = /^\\([a-zA-Z]+|.)/.exec(this.s.slice(this.i));
      if (m) { this.i += m[0].length; d = SYMBOLS[m[1]] !== undefined ? SYMBOLS[m[1]] : m[1]; }
      else { this.i++; d = ''; }
    } else {
      this.i++;
      if (d === '.') d = '';
    }
    if (d === '') return '';
    const cls = collapsed ? 'mdelim mdelim-sm' : 'mdelim';
    return `<span class="${cls}">${esc(d)}</span>`;
  }

  // \begin{cases} a & b \\ c & d \end{cases}
  parseEnv() {
    const env = this.readRaw().trim();
    if (env !== 'cases' && env !== 'array' && env !== 'aligned') {
      this.errors.push('env:' + env);
      return `<span class="munknown">[${esc(env)}]</span>`;
    }
    // 取到 \end{...} 之前的原文
    const rest = this.s.slice(this.i);
    const endIdx = rest.search(/\\end\s*\{/);
    const bodyRaw = endIdx >= 0 ? rest.slice(0, endIdx) : rest;
    this.i += endIdx >= 0 ? endIdx : rest.length;
    // 跳过 \end{...}
    const em = /^\\end\s*\{[^}]*\}/.exec(this.s.slice(this.i));
    if (em) this.i += em[0].length;

    // 按 \\ 分行，按 & 分列
    const rows = bodyRaw.split(/\\\\/).map((r) => r.trim()).filter((r) => r !== '');
    let out = '<table class="mcases">';
    for (const r of rows) {
      const cells = r.split('&').map((c) => c.trim());
      out += '<tr>';
      for (const c of cells) {
        const sub = new Parser(c);
        out += `<td>${sub.parseSeq()}</td>`;
      }
      out += '</tr>';
    }
    out += '</table>';
    return out;
  }
}

// ---------------------------------------------------------------- 对外接口
function render(src) {
  const p = new Parser(String(src));
  const html = p.parseSeq();
  return { ok: p.errors.length === 0, html, errors: p.errors };
}

// 判断是否含"分式/根号/方程组"等需要块级版式的结构
function needsBlock(src) {
  return /\\(frac|dfrac|tfrac|sqrt|begin)\b/.test(String(src));
}

module.exports = { render, needsBlock, SYMBOLS, OPNAMES };
