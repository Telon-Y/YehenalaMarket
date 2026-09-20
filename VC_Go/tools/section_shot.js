// VC_Go/tools/_section_shot.js —— 抽取指定章节生成独立 HTML，便于放大核验渲染
//
// 用法：在【工作区根】运行（本工具按工作区根解析路径）：
//   ELECTRON_RUN_AS_NODE=1 "<electron>" tools/section_shot.js
// 产物：out/sec_45.html / sec_63.html / sec_23.html / sec_84.html
//（2026-09-19 修正：原实现用相对当前目录的 '1.0 ….md' 与 'out/'，
//  与仓库的 docs/ + out/ 布局不符，任何目录下都会 ENOENT。）
'use strict';
const fs = require('fs');
const path = require('path');
const { compile } = require('./md2html.js');

const ROOT = path.resolve(__dirname, '..');
const md = fs.readFileSync(path.join(ROOT, 'docs', '1.0 生产与市场模拟.md'), 'utf8');
const outDir = path.join(ROOT, 'out');
fs.mkdirSync(outDir, { recursive: true });
const lines = md.split(/\r?\n/);

// 找章节：从 startHeading 到下一个同级或更高级标题
function sliceSection(startPat, stopLevel) {
  const startIdx = lines.findIndex((L) => new RegExp('^#{1,4}\\s+' + startPat).test(L.trim()));
  if (startIdx < 0) return null;
  const lvl = /^(#+)/.exec(lines[startIdx].trim())[1].length;
  let end = lines.length;
  for (let k = startIdx + 1; k < lines.length; k++) {
    const m = /^(#+)\s/.exec(lines[k].trim());
    if (m && m[1].length <= (stopLevel || lvl)) { end = k; break; }
  }
  return lines.slice(startIdx, end).join('\n');
}

const sections = [
  ['4\\.5', 3, 'sec_45'],
  ['6\\.3', 3, 'sec_63'],
  ['2\\.3', 3, 'sec_23'],
  ['8\\.4', 3, 'sec_84'],
];

for (const [pat, lvl, name] of sections) {
  const body = sliceSection(pat, lvl);
  if (!body) { console.log('未找到章节 ' + pat); continue; }
  const { html, stats } = compile(body, { title: '章节 ' + pat });
  fs.writeFileSync(path.join(outDir, `${name}.html`), html, 'utf8');
  console.log(`${name}: 公式 ${stats.equations} · 表格 ${stats.tables} · 失败 ${stats.mathErrors}`);
}
