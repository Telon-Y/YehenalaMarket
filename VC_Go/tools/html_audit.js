// VC_Go/tools/html_audit.js —— 校验生成的 HTML 结构完整性
//
// 检查项：
//   ① 基本结构（DOCTYPE、lang、charset、title、sidebar、main）
//   ② 元素计数（h1-h4 / table / blockquote / ul / ol / li / 公式节点）
//   ③ 表格列数一致性（每行 td 数 = 表头 th 数）
//   ④ 目录锚点与标题 id 是否一一对应
//   ⑤ 是否有未渲染的 LaTeX 残留或 math-error
//   ⑥ 标签配对（blockquote / table / ul / ol / li / p / h* 开闭数量）
//   ⑦ 标签严格嵌套（栈校验：⑥ 只数个数，查不出"</li> 放错位置"这类错误）
//
// 运行：node VC_Go/tools/html_audit.js "VC_Go/docs/1.0 生产与市场模拟.html"
'use strict';
const fs = require('fs');

const file = process.argv[2] || 'VC_Go/docs/1.0 生产与市场模拟.html';
const rawHtml = fs.readFileSync(file, 'utf8');
// 统计前剔除 <style>/<script>：否则 CSS 里的 ".math-error{...}" 之类会被
// 误判成渲染失败，而页面里其实一个失败都没有（历史误报）。
const styleBlocks = (rawHtml.match(/<style>[\s\S]*?<\/style>/g) || []).length;
const html = rawHtml.replace(/<style>[\s\S]*?<\/style>/g, '')
                    .replace(/<script>[\s\S]*?<\/script>/g, '');
const problems = [];
const count = (re) => (html.match(re) || []).length;

console.log(`=== ${file} 结构校验 ===\n`);
console.log(`（已剔除 ${styleBlocks} 个 <style> 块后再统计，避免 CSS 文本造成误报）\n`);

// ① 基本结构（CSS 检查必须用 rawHtml——html 里已把 <style> 剔掉了）
const basics = {
  'DOCTYPE': /^<!DOCTYPE html>/i.test(rawHtml),
  'lang=zh-CN': /<html lang="zh-CN">/.test(rawHtml),
  'charset=utf-8': /<meta charset="utf-8">/.test(rawHtml),
  '<title>': /<title>[^<]+<\/title>/.test(rawHtml),
  'sidebar 目录': /<nav class="sidebar">[\s\S]*?<\/nav>/.test(rawHtml),
  '<main>': /<main>[\s\S]*<\/main>/.test(rawHtml),
  'CSS 内联': /<style>[\s\S]{500,}<\/style>/.test(rawHtml),
};
console.log('① 基本结构：');
for (const [k, v] of Object.entries(basics)) {
  console.log(`   ${v ? '✅' : '❌'} ${k}`);
  if (!v) problems.push(`缺少 ${k}`);
}

// ② 元素计数
console.log('\n② 元素计数：');
const counts = {
  'h1': count(/<h1[ >]/g),
  'h2': count(/<h2[ >]/g),
  'h3': count(/<h3[ >]/g),
  'h4': count(/<h4[ >]/g),
  'table': count(/<table>/g),
  'thead tr': count(/<thead>/g),
  'tbody tr': count(/<tbody>[\s\S]*?<\/tbody>/g) ? null : 0,
  'blockquote': count(/<blockquote>/g),
  'ul': count(/<ul>/g),
  'ol': count(/<ol>/g),
  'li': count(/<li>/g),
  '行间公式 .equation': count(/class="equation"/g),
  '行内公式 .math': count(/class="math /g),
  '分式 .mfrac': count(/class="mfrac"/g),
  '上下标 .msup/.msub': count(/class="m(sup|ub)"/g),
  '根号 .msqrt': count(/class="msqrt"/g),
};
for (const [k, v] of Object.entries(counts)) {
  if (v !== null) console.log(`   ${String(v).padStart(5)}  ${k}`);
}

// ③ 表格列数一致性
console.log('\n③ 表格结构：');
const tables = [...html.matchAll(/<table>([\s\S]*?)<\/table>/g)];
tables.forEach((m, idx) => {
  const body = m[1];
  const thCount = (body.match(/<th[ >]/g) || []).length;
  const rows = [...body.matchAll(/<tr>([\s\S]*?)<\/tr>/g)];
  const bad = [];
  rows.forEach((r, ri) => {
    const td = (r[1].match(/<td[ >]/g) || []).length;
    const th = (r[1].match(/<th[ >]/g) || []).length;
    if (td && td !== thCount) bad.push(`第 ${ri + 1} 行 td=${td} ≠ th=${thCount}`);
  });
  if (bad.length) {
    problems.push(`表格 #${idx + 1} 列数不一致：${bad.join('; ')}`);
    console.log(`   ❌ 表 #${idx + 1}（${thCount} 列）${bad.join('; ')}`);
  } else {
    console.log(`   ✅ 表 #${idx + 1}：${thCount} 列 × ${rows.length - 1} 行`);
  }
});

// ④ 目录锚点 vs 标题 id
console.log('\n④ 目录锚点：');
const anchors = [...html.matchAll(/<a href="#([^"]+)">/g)].map((m) => m[1]);
const ids = new Set([...html.matchAll(/<h[1-4] id="([^"]+)"/g)].map((m) => m[1]));
const missing = anchors.filter((a) => !ids.has(a));
if (missing.length) {
  problems.push(`目录锚点无对应标题：${missing.slice(0, 5).join(', ')}`);
  console.log(`   ❌ ${missing.length} 个锚点无对应标题：${missing.slice(0, 5).join(', ')}`);
} else {
  console.log(`   ✅ ${anchors.length} 个锚点全部有对应标题（标题 id 共 ${ids.size} 个）`);
}

// ⑤ 渲染残留
console.log('\n⑤ 渲染残留：');
const mathErr = count(/math-error/g);
const leftover = [...html.matchAll(/\\[a-zA-Z]{2,}/g)].map((m) => m[0]);
const uniqLeft = [...new Set(leftover)];
console.log(`   math-error: ${mathErr}`);
console.log(`   LaTeX 残留: ${uniqLeft.length ? uniqLeft.slice(0, 10).join(' ') : '0'}`);
if (mathErr) problems.push(`${mathErr} 处公式渲染失败`);
if (uniqLeft.length) problems.push(`LaTeX 残留：${uniqLeft.slice(0, 5).join(' ')}`);

// ⑥ 标签配对
console.log('\n⑥ 标签配对：');
for (const tag of ['blockquote', 'table', 'ul', 'ol', 'li', 'p', 'h2', 'h3', 'h4', 'h5', 'div']) {
  const open = count(new RegExp(`<${tag}[ >]`, 'g'));
  const close = count(new RegExp(`</${tag}>`, 'g'));
  const ok = open === close;
  console.log(`   ${ok ? '✅' : '❌'} <${tag}> ${open} / </${tag}> ${close}`);
  if (!ok) problems.push(`<${tag}> 开闭不配对：${open} vs ${close}`);
}

// ⑦ 标签严格嵌套
//
// ⑥ 只比较开闭【个数】——个数相等并不代表位置正确。反例（2026-09-19 实际出现过）：
//   <ul><li>父项：<ul></li><li>子项</li></ul><li>同级项</li></ul>
// <ul>/<li> 的个数完全相等，⑥ 判定通过；但浏览器会把那个 </li> 读成"父项结束"，
// 于是子项被当成同级项，嵌套缩进整个丢失。这里用栈做严格的成对嵌套校验，
// 并把出错位置换算成行号，便于直接定位到渲染器或源文档。
console.log('\n⑦ 标签严格嵌套：');
{
  const VOID = new Set(['br', 'hr', 'img', 'meta', 'link', 'input', 'col', 'area', 'base', 'source']);
  const bodyStart = html.indexOf('<body>');
  const bodyEnd = html.lastIndexOf('</body>');
  const scope = bodyStart >= 0 && bodyEnd > bodyStart ? html.slice(bodyStart, bodyEnd + 7) : html;
  const lineOf = (pos) => scope.slice(0, pos).split('\n').length;
  const tagRe = /<(\/?)([a-zA-Z][a-zA-Z0-9]*)((?:"[^"]*"|[^>"])*)>/g;
  const stack = [];
  const nesting = [];
  let m;
  while ((m = tagRe.exec(scope))) {
    const closing = m[1] === '/';
    const name = m[2].toLowerCase();
    const attrs = m[3] || '';
    if (VOID.has(name) || /\/\s*$/.test(attrs)) continue;
    if (!closing) { stack.push({ name, pos: m.index }); continue; }
    const top = stack.pop();
    if (!top) {
      nesting.push(`多余的 </${name}>（第 ${lineOf(m.index)} 行）`);
    } else if (top.name !== name) {
      nesting.push(`<${top.name}>（第 ${lineOf(top.pos)} 行）与 </${name}>（第 ${lineOf(m.index)} 行）交叉嵌套`);
      stack.push(top); // 恢复栈顶，避免一处错误引发连锁误报
    }
  }
  for (const s of stack) nesting.push(`<${s.name}> 未闭合（第 ${lineOf(s.pos)} 行）`);
  if (nesting.length) {
    problems.push(`标签嵌套错误 ${nesting.length} 处：${nesting.slice(0, 3).join('；')}`);
    console.log(`   ❌ ${nesting.length} 处：`);
    nesting.slice(0, 10).forEach((n) => console.log('      - ' + n));
  } else {
    console.log('   ✅ 全部标签成对且严格嵌套');
  }
}

console.log('\n' + '='.repeat(50));
if (problems.length === 0) {
  console.log('✅ 全部检查通过');
  process.exit(0);
} else {
  console.log(`❌ 发现 ${problems.length} 个问题：`);
  problems.forEach((p) => console.log('   - ' + p));
  process.exit(1);
}
