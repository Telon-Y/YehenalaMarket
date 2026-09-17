// VC_Go/tools/md2html.js —— 把 `VC_Go/docs/1.0 生产与市场模拟.md` 直接编译为自包含 HTML
//
// 为什么不走 md → tex → html：
//   原链路是 `VC_Go/docs/1.0 生产与市场模拟.tex` 由 `VC_Go/tools/tex2html.js` 转成 HTML，
//   而 tex 需要手工与 md 同步——多一层中间产物就多一处漂移点。实际已发生：
//   md 更新到 18:31，tex/html 停在 17:26，两者内容不一致。
//   本工具直接以 md 为唯一源，去掉中间层，因此：
//     · **`VC_Go/docs/*.tex` 已不再是本项目的产物**，无需维护，也无需重新生成；
//     · `VC_Go/tools/tex2html.js` 的角色降为"数学渲染库"，仅被本文件 require 两个函数。
//   若日后确实需要 tex（例如要排 PDF），应写 md2tex 由 md 生成，而不是手工维护。
//
// 数学渲染：复用 tex2html.js 的 `renderMath` / `renderInline`
//   （自研 TeX 子集渲染器，输出纯 HTML+CSS，无需 MathJax/KaTeX，离线可用）。
//   样式表也与 tex2html.js 保持一致，因此新旧产物的视觉相同。
//
// 用法：
//   node VC_Go/tools/md2html.js "VC_Go/docs/1.0 生产与市场模拟.md" "VC_Go/docs/1.0 生产与市场模拟.html"
//   node VC_Go/tools/md2html.js "VC_Go/docs/1.0 生产与市场模拟.md"        # 试运行：打印目录与统计
//   node VC_Go/tools/html_audit.js "VC_Go/docs/1.0 生产与市场模拟.html"   # 生成后做结构校验
'use strict';
const fs = require('fs');
const path = require('path');

// 数学渲染：使用自研的 mathrender.js
//
// 不再借用 tex2html.js 的 renderMath —— 那是为整份手写 LaTeX 文档设计的解析器，
// 把上下标内容平铺、\frac 参数被括号截断、\top 与 cases 环境不支持，
// 导致 Markdown 里的 $...$ 大面积显示异常。详见 VC_Go/tools/mathrender.js 头部说明。
const { render: renderMath } = require('./mathrender.js');

const esc = (s) => String(s)
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;');

// =============================================================================
// 1. 行内解析
// =============================================================================
// 处理顺序很关键：先把公式挖成占位符，避免其中的 _{...}、\ 等被后续规则破坏。
function inline(text) {
  const stash = [];
  const keep = (html) => {
    stash.push(html);
    return `\u0000${stash.length - 1}\u0000`;
  };

  let s = String(text);

  // ① 行间公式 $$...$$
  s = s.replace(/\$\$([\s\S]+?)\$\$/g, (_, tex) => {
    const r = renderMath(tex.trim());
    return keep(r.ok ? r.html : `<span class="math-error">${esc(tex)}</span>`);
  });

  // ② 行内公式 $...$
  s = s.replace(/(?<!\$)\$([^$\n]+?)\$(?!\$)/g, (_, tex) => {
    const r = renderMath(tex.trim());
    return keep(r.ok
      ? `<span class="math math-inline">${r.html}</span>`
      : `<span class="math-error">${esc(tex)}</span>`);
  });

  // ③ 行内代码 `...`
  s = s.replace(/`([^`\n]+)`/g, (_, code) => keep(`<code>${esc(code)}</code>`));

  // ④ 转义剩余文本
  s = esc(s);

  // ⑤ 粗体、斜体
  s = s.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
  s = s.replace(/(^|[^*])\*([^*\n]+)\*(?!\*)/g, '$1<em>$2</em>');

  // ⑥ 形如 [文字](链接) 的链接
  s = s.replace(/\[([^\]\n]+)\]\(([^)\s]+)\)/g, '<a href="$2">$1</a>');

  // ⑦ 还原占位符
  s = s.replace(/\u0000(\d+)\u0000/g, (_, i) => stash[Number(i)]);
  return s;
}

// =============================================================================
// 2. 表格
// =============================================================================
function isTableSep(line) {
  return /^\s*\|[\s:|-]+\|\s*$/.test(line);
}

function splitRow(line) {
  let t = line.trim();
  if (t.startsWith('|')) t = t.slice(1);
  if (t.endsWith('|')) t = t.slice(0, -1);
  return t.split('|').map((c) => c.trim());
}

// 解析对齐标记行，返回每列的对齐方式
function parseAlign(sep) {
  return splitRow(sep).map((c) => {
    const l = c.startsWith(':');
    const r = c.endsWith(':');
    if (l && r) return 'center';
    if (r) return 'right';
    if (l) return 'left';
    return null;
  });
}

function renderTable(rows) {
  const head = splitRow(rows[0]);
  const align = rows.length > 1 && isTableSep(rows[1]) ? parseAlign(rows[1]) : [];
  const bodyRows = rows.slice(rows.length > 1 && isTableSep(rows[1]) ? 2 : 1);

  const cls = (i) => (align[i] ? ` class="${align[i]}"` : '');
  let out = '<div class="table-wrap">\n<table>\n';
  out += '<thead>\n<tr>';
  head.forEach((c, i) => { out += `<th${cls(i)}>${inline(c)}</th>`; });
  out += '</tr>\n</thead>\n';
  if (bodyRows.length) {
    out += '<tbody>\n';
    for (const r of bodyRows) {
      const cells = splitRow(r);
      out += '<tr>';
      // 以表头列数为准，缺列补空、多列丢弃，保证表格结构合法
      for (let i = 0; i < head.length; i++) {
        out += `<td${cls(i)}>${inline(cells[i] === undefined ? '' : cells[i])}</td>`;
      }
      out += '</tr>\n';
    }
    out += '</tbody>\n';
  }
  out += '</table>\n</div>';
  return out;
}

// =============================================================================
// 3. 块级解析
// =============================================================================
let headingSeq = 0;

function slug(text) {
  const plain = String(text)
    .replace(/\*\*|`|\$[^$]*\$/g, '')
    .replace(/[^\w\u4e00-\u9fff]+/g, '-')
    .replace(/^-+|-+$/g, '');
  return 'h' + (++headingSeq) + '-' + (plain || 'sec');
}

function parseBlocks(md) {
  const lines = md.split(/\r?\n/);
  const out = [];
  const toc = [];
  let i = 0;

  const blank = (s) => s.trim() === '';

  while (i < lines.length) {
    const line = lines[i];
    const t = line.trim();

    // 空行
    if (blank(line)) { i++; continue; }

    // 代码栅栏
    if (/^```/.test(t)) {
      const lang = t.slice(3).trim();
      const buf = [];
      i++;
      while (i < lines.length && !/^```/.test(lines[i].trim())) { buf.push(lines[i]); i++; }
      i++; // 跳过结束栅栏
      out.push(`<pre class="code-block"><code${lang ? ` data-lang="${esc(lang)}"` : ''}>${esc(buf.join('\n'))}</code></pre>`);
      continue;
    }

    // 水平线（放在标题之前判断，避免 --- 被当成 setext）
    if (/^-{3,}$/.test(t)) { out.push('<hr>'); i++; continue; }

    // 块级公式：独占一段的 $$...$$（可跨行）→ 居中卡片样式
    // 必须放在标题/表格/列表之前判断，且要早于行内处理，
    // 否则会被当成普通段落里的行内公式，失去居中版式。
    if (/^\$\$/.test(t)) {
      const buf = [line];
      // 单行闭合
      if (t.length > 4 && /\$\$\s*$/.test(t) && (t.match(/\$\$/g) || []).length >= 2) {
        const tex = t.replace(/^\$\$/, '').replace(/\$\$\s*$/, '').trim();
        const r = renderMath(tex);
        out.push(`<div class="equation"><span class="math">${r.ok ? r.html : `<span class="math-error">${esc(tex)}</span>`}</span></div>`);
        i++;
        continue;
      }
      i++;
      while (i < lines.length && !/\$\$\s*$/.test(lines[i].trim())) { buf.push(lines[i]); i++; }
      if (i < lines.length) { buf.push(lines[i]); i++; }
      const tex = buf.join('\n').replace(/^\s*\$\$/, '').replace(/\$\$\s*$/, '').trim();
      const r = renderMath(tex);
      out.push(`<div class="equation"><span class="math">${r.ok ? r.html : `<span class="math-error">${esc(tex)}</span>`}</span></div>`);
      continue;
    }

    // 标题
    const hm = t.match(/^(#{1,6})\s+(.*)$/);
    if (hm) {
      const level = hm[1].length;
      const text = hm[2].trim();
      const id = slug(text);
      // 保留原文里的中文序号（如"四、扩建与拆除系统"），不再另加编号
      const html = inline(text);
      out.push(`<h${level} id="${id}">${html}</h${level}>`);
      if (level === 2 || level === 3) {
        toc.push({ level, id, text: text.replace(/\*\*|`/g, '') });
      } else if (level === 4) {
        toc.push({ level, id, text: text.replace(/\*\*|`/g, '') });
      }
      i++;
      continue;
    }

    // 引用块（含其中的列表与嵌套引用）
    if (/^>/.test(t)) {
      const buf = [];
      while (i < lines.length && /^\s*>/.test(lines[i])) {
        buf.push(lines[i].replace(/^\s*>\s?/, ''));
        i++;
      }
      out.push(renderQuote(buf));
      continue;
    }

    // 表格
    if (/^\|/.test(t)) {
      const buf = [];
      while (i < lines.length && /^\s*\|/.test(lines[i])) { buf.push(lines[i]); i++; }
      out.push(renderTable(buf));
      continue;
    }

    // 列表（含嵌套）
    if (/^([-*]|\d+\.)\s/.test(t)) {
      const buf = [];
      while (i < lines.length && (/^(\s*)([-*]|\d+\.)\s/.test(lines[i]) || (buf.length && /^\s+\S/.test(lines[i]) && !blank(lines[i])))) {
        buf.push(lines[i]);
        i++;
      }
      out.push(renderList(buf));
      continue;
    }

    // 普通段落：连续非空行合并
    {
      const buf = [line];
      i++;
      while (i < lines.length && !blank(lines[i]) &&
             !/^(#{1,6}\s|>|\||```|-{3,}$)/.test(lines[i].trim()) &&
             !/^([-*]|\d+\.)\s/.test(lines[i].trim())) {
        buf.push(lines[i]);
        i++;
      }
      out.push(`<p>${inline(buf.join(' '))}</p>`);
    }
  }

  return { html: out.join('\n'), toc };
}

// 引用块：内部按行判断是列表还是段落
function renderQuote(buf) {
  const parts = [];
  let i = 0;
  while (i < buf.length) {
    const t = buf[i].trim();
    if (t === '') { i++; continue; }
    if (/^([-*]|\d+\.)\s/.test(t)) {
      const lb = [];
      while (i < buf.length) {
        const s = buf[i];
        const st = s.trim();
        if (st === '') {
          // 引用块内的空行：若下一行仍是列表项则继续
          if (i + 1 < buf.length && /^([-*]|\d+\.)\s/.test(buf[i + 1].trim())) { i++; continue; }
          break;
        }
        if (!/^\s*([-*]|\d+\.)\s/.test(s) && !/^\s+\S/.test(s)) break;
        lb.push(s);
        i++;
      }
      parts.push(renderList(lb));
      continue;
    }
    // 连续非列表行合成段落
    const pb = [];
    while (i < buf.length) {
      const st = buf[i].trim();
      if (st === '' || /^([-*]|\d+\.)\s/.test(st)) break;
      pb.push(buf[i]);
      i++;
    }
    if (pb.length) parts.push(`<p>${inline(pb.join(' '))}</p>`);
  }
  return `<blockquote>\n${parts.join('\n')}\n</blockquote>`;
}

// 列表：按缩进层级构造嵌套。
//
// 关键约束：每个 <li> 必须严格配对自己的 </li>，且 </li> 前要先关掉它的子列表。
// 早期版本用"看下一个元素的缩进决定是否闭合"的写法，导致末项与同级切换处漏闭合。
function renderList(buf) {
  const items = [];
  for (const raw of buf) {
    const m = raw.match(/^(\s*)([-*]|\d+\.)\s+(.*)$/);
    if (m) {
      items.push({
        indent: m[1].replace(/\t/g, '  ').length,
        ordered: /\d/.test(m[2]),
        text: m[3],
      });
    } else if (items.length) {
      items[items.length - 1].text += ' ' + raw.trim(); // 续行并入上一项
    }
  }
  if (!items.length) return '';

  let html = '';
  const stack = []; // { indent, ordered }
  let liOpen = false;

  const closeLi = () => { if (liOpen) { html += '</li>'; liOpen = false; } };
  const closeList = () => {
    const top = stack.pop();
    html += top.ordered ? '</ol>' : '</ul>';
  };

  for (const it of items) {
    if (!stack.length) {
      // 首个元素：开列表
      html += it.ordered ? '<ol>' : '<ul>';
      stack.push({ indent: it.indent, ordered: it.ordered });
    } else {
      const top = stack[stack.length - 1];
      if (it.indent > top.indent) {
        // 更深一层：作为嵌套列表开在【当前未闭合的 li 内】
        html += it.ordered ? '<ol>' : '<ul>';
        stack.push({ indent: it.indent, ordered: it.ordered });
      } else if (it.indent < top.indent) {
        // 回到上层：逐层关闭
        while (stack.length > 1 && it.indent < stack[stack.length - 1].indent) {
          closeLi();
          closeList();
        }
      } else if (it.ordered !== top.ordered) {
        // 同层但列表类型变了：换一个列表
        closeLi();
        closeList();
        html += it.ordered ? '<ol>' : '<ul>';
        stack.push({ indent: it.indent, ordered: it.ordered });
      }
    }
    closeLi(); // 新项开始前，先闭合上一项
    html += `<li>${inline(it.text)}`;
    liOpen = true;
  }

  closeLi();
  while (stack.length) closeList();
  return html;
}

// =============================================================================
// 4. 目录
// =============================================================================
// 用显式的 flush 逻辑生成嵌套列表，保证 <li>/<ol> 严格配对。
//
// 早期版本试图用正则替换（out.replace(/<\/li>\s*$/, ...)）在追加时补闭合标签，
// 结果侧边栏中间若干项漏掉 </li>，使 <ol> 与 <li> 开闭数量不符。
// 目录结构固定为 h2 → h3/h4 两层，因此用"遇到新的 h2 就 flush 上一组"即可。
function buildToc(toc) {
  const out = [];
  let sub = null; // 当前 h2 下的子列表缓冲

  const flushSub = () => {
    if (sub && sub.length) {
      out.push('<ol class="sub">');
      for (const e of sub) out.push(`<li><a href="#${e.id}">${esc(e.text)}</a></li>`);
      out.push('</ol>');
    }
    sub = null;
  };
  const flushH2 = () => {
    if (sub !== null) {
      flushSub();
      out.push('</li>');
    }
  };

  for (const e of toc) {
    if (e.level === 2) {
      flushH2();
      out.push(`<li><a href="#${e.id}">${esc(e.text)}</a>`);
      sub = [];
    } else {
      if (sub === null) sub = []; // 容错：文档以 h3 开头
      sub.push(e);
    }
  }
  flushH2();

  return `<h4>目录</h4>\n<ol>\n${out.join('\n')}\n</ol>`;
}

// =============================================================================
// 5. 样式（与 tex2html.js 保持一致，视觉相同）
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
p{margin:.7em 0}
ul,ol{margin:.6em 0;padding-left:1.7em}
li{margin:.3em 0}
/* 引用块：文档头的"修订记录"与正文注释都用 > 书写，数量多且常整段出现，
   故采用低调的浅灰底 + 深灰左边框；若用高饱和蓝色会整页发蓝、喧宾夺主。 */
blockquote{margin:1.1em 0;padding:.7em 1.05em;background:#f7f8fa;
  border-left:3px solid #c6ccd4;border-radius:0 6px 6px 0;color:#3a4149}
blockquote p{margin:.3em 0}
blockquote ul,blockquote ol{margin:.4em 0}
blockquote strong{color:var(--ink)}
blockquote ul,blockquote ol{margin:.4em 0}
code{background:rgba(175,184,193,.2);padding:.15em .38em;border-radius:4px;
  font-family:Consolas,"Cascadia Mono",Menlo,monospace;font-size:.9em}
pre.code-block{background:var(--panel);border:1px solid var(--line);border-radius:8px;
  padding:12px 14px;overflow-x:auto}
pre.code-block code{background:none;padding:0;font-size:13.5px;line-height:1.6}
.equation{margin:1.25em 0;padding:.9em 1em;text-align:center;overflow-x:auto;
  background:linear-gradient(180deg,#fbfcfd,#f6f8fa);border:1px solid var(--line);border-radius:8px}
.table-wrap{overflow-x:auto;margin:1.2em 0}
table{border-collapse:collapse;width:100%;font-size:14.5px;line-height:1.65}
th,td{border-bottom:1px solid var(--line);padding:7px 12px;vertical-align:top}
thead th{border-bottom:2px solid var(--ink);background:var(--panel);font-weight:600;white-space:nowrap}
tbody tr:hover{background:#fafbfc}
.left{text-align:left}.right{text-align:right}.center{text-align:center}
tbody td.right{font-variant-numeric:tabular-nums}
/* 数学公式：行内公式必须能在容器内换行，否则 nowrap 会把整段文字撑出视口、
   右侧内容被硬截断（这是"公式显示异常"的主因，截图可见大批行尾被切掉）。
   方案：inline-block 让公式整体可换行 + max-width 允许超长公式在其内部横向滚动。 */
/* ===== 数学排版（配合 VC_Go/tools/mathrender.js 的输出）=====
   设计要点：
     · 上下标用标准 <sub>/<sup>，由浏览器原生渲染，不再平铺；
     · 分式用纵向 flex + 上边框作分数线，可嵌套；
     · 行内公式允许在容器内换行，避免 nowrap 把整段文字撑出视口。 */
.math{font-family:"Cambria Math","Latin Modern Math","STIX Two Math",Cambria,"Times New Roman",Georgia,serif;
  font-size:1.06em;color:#111;line-height:1.5}
.math-inline{display:inline;white-space:normal}
.mi{font-style:italic}
.mn{font-style:normal}
.mbf{font-weight:700}
.mbb{font-family:"Cambria Math",Cambria,serif;font-weight:600}
.mtext{font-family:inherit;font-style:normal}
.mopname{font-style:normal;font-family:inherit;padding-right:.12em}
.mo{padding:0 .04em}
.mscript{white-space:nowrap}
.msub{font-size:.72em;vertical-align:-.25em;line-height:0}
.msup{font-size:.72em;vertical-align:.45em;line-height:0}
/* 分式：纵向堆叠，分子下边框作分数线 */
.mfrac{display:inline-flex;flex-direction:column;vertical-align:-.45em;text-align:center;
  margin:0 .18em;line-height:1.15;font-size:.97em}
.mfrac-d{font-size:1em}
.mfrac-num{border-bottom:1.1px solid currentColor;padding:0 .3em .06em}
.mfrac-den{padding:.06em .3em 0}
/* 根号 */
.msqrt{white-space:nowrap}
.msqrt-sym{padding-right:.04em}
.msqrt-body{border-top:1.1px solid currentColor;padding:.08em .18em 0 .06em}
.msqrt-deg{font-size:.62em;vertical-align:.7em;margin-right:-.35em}
/* 自适应括号 */
.mdelim{font-size:1.15em;line-height:1}
.mdelim-sm{font-size:1.02em}
/* cases 环境：左侧表达式、右侧条件。
   必须 width:auto + 仅按内容占宽，否则 inline-table 会撑满可用宽度，
   把条件列推到很远、中间留出大片空白（目视即"公式错位"）。 */
.mcases{display:inline-table;width:auto;vertical-align:middle;border-collapse:collapse;margin:0 .2em}
.mcases td{border:none;padding:.05em 0 .05em 0;text-align:left;white-space:nowrap}
.mcases td + td{padding-left:1.6em}
.mcases tr:first-child td{padding-top:0}
.mcases tr:last-child td{padding-bottom:0}
.munknown{background:#fff1f0;border:1px solid #ffa39e;border-radius:3px;padding:0 .2em;color:#a8071a}
.math-error{background:#fff1f0;border:1px solid #ffa39e;border-radius:4px;padding:0 .3em;color:#a8071a}
hr{border:none;border-top:1px solid var(--line);margin:2.2em 0}
a{color:var(--accent)}
.doc-note{background:#fffbe6;border:1px solid #ffe58f;border-radius:8px;padding:.7em 1em;margin:1.1em 0}
@media(max-width:1000px){.wrap{flex-direction:column}.sidebar{position:static;width:auto;flex:none;border-right:none;border-bottom:1px solid var(--line)}main{padding:24px 18px 60px}}
`;

// =============================================================================
// 6. 编译
// =============================================================================
function compile(md, opts = {}) {
  headingSeq = 0;
  // 去掉一级标题（模板里已单独渲染为 h1），保留其余
  const lines = md.split(/\r?\n/);
  const bodyLines = [];
  let skippedH1 = false;
  let title = opts.title || '';
  for (const L of lines) {
    const m = L.match(/^#\s+(.*)$/);
    if (m && !skippedH1) {
      if (!title) title = m[1].trim();
      skippedH1 = true;
      continue;
    }
    bodyLines.push(L);
  }

  const body = parseBlocks(bodyLines.join('\n'));
  const stats = {
    // 公式计数：块级（.equation 内的 .math）+ 行内（.math-inline）
    equations: (body.html.match(/class="equation"/g) || []).length
             + (body.html.match(/class="math math-inline"/g) || []).length
             + (body.html.match(/class="math"/g) || []).length,
    tables: (body.html.match(/<table>/g) || []).length,
    headings: (body.html.match(/<h[234] /g) || []).length,
    mathErrors: (body.html.match(/math-error/g) || []).length
              + (body.html.match(/munknown/g) || []).length,
    leftovers: (body.html.match(/\\[a-zA-Z]{2,}/g) || []).length,
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
<nav class="sidebar">${buildToc(body.toc)}</nav>
<main>
<h1>${esc(title)}</h1>
<p class="doc-meta">叶赫那拉市场 · 由 <code>${esc(opts.source || '1.0 生产与市场模拟.md')}</code> 经 <code>VC_Go/tools/md2html.js</code> 离线编译生成 · 完全自包含，无需联网</p>
${body.html}
</main>
</div>
</body>
</html>
`;
  return { html, stats, toc: body.toc };
}

// =============================================================================
// 7. CLI
// =============================================================================
function main() {
  const args = process.argv.slice(2);
  const inFile = args[0] || '1.0 生产与市场模拟.md';
  const outFile = args[1];

  const md = fs.readFileSync(inFile, 'utf8');
  const { html, stats, toc } = compile(md, { source: path.basename(inFile) });

  if (!outFile) {
    console.log(`标题层级（${toc.length} 条，用于检查目录完整性）：`);
    for (const e of toc) console.log(`  ${'  '.repeat(e.level - 2)}h${e.level}  ${e.text}`);
    console.log(`\n统计：公式 ${stats.equations} · 表格 ${stats.tables} · 标题 ${stats.headings} · ` +
                `渲染失败 ${stats.mathErrors} · LaTeX 残留 ${stats.leftovers}`);
    console.log('\n（未指定输出文件，仅做试运行。加第二个参数可写出 HTML。）');
    if (stats.mathErrors) process.exit(1);
    return;
  }

  fs.mkdirSync(path.dirname(path.resolve(outFile)), { recursive: true });
  fs.writeFileSync(outFile, html, 'utf8');
  console.log(`已生成 ${outFile}`);
  console.log(`  公式 ${stats.equations} · 表格 ${stats.tables} · 标题 ${stats.headings} · ` +
              `渲染失败 ${stats.mathErrors} · LaTeX 残留 ${stats.leftovers}`);
  if (stats.mathErrors) process.exit(1);
}

if (require.main === module) main();

module.exports = { compile, inline, parseBlocks, renderTable };
