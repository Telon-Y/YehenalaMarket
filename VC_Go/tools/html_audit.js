// VC_Go/tools/html_audit.js —— 校验生成的 HTML 结构完整性
//
// 检查项：
//   ① 基本结构（DOCTYPE、lang、charset、title、sidebar、main）
//   ② 元素计数（h1-h4 / table / blockquote / ul / ol / li / 公式节点）
//   ③ 表格列数一致性（每行 td 数 = 表头 th 数）
//   ④ 目录锚点与标题 id 是否一一对应
//   ⑤ 是否有未渲染的 LaTeX 残留或 math-error
//   ⑥ 标签配对（blockquote / table / ul / ol / li / p / h* 开闭数量）
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
for (const tag of ['blockquote', 'table', 'ul', 'ol', 'li', 'p', 'h2', 'h3', 'h4', 'div']) {
  const open = count(new RegExp(`<${tag}[ >]`, 'g'));
  const close = count(new RegExp(`</${tag}>`, 'g'));
  const ok = open === close;
  console.log(`   ${ok ? '✅' : '❌'} <${tag}> ${open} / </${tag}> ${close}`);
  if (!ok) problems.push(`<${tag}> 开闭不配对：${open} vs ${close}`);
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
