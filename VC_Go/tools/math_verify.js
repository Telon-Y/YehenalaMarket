// VC_Go/tools/_verify_math.js —— 全量公式渲染验证（含质量检查）
'use strict';
const fs = require('fs');
const { render } = require('./mathrender.js');

const md = fs.readFileSync('1.0 生产与市场模拟.md', 'utf8');
const exprs = new Set();
let m;
const b = /\$\$([\s\S]+?)\$\$/g;
while ((m = b.exec(md))) exprs.add(m[1].trim());
const i = /(?<!\$)\$([^$\n]+?)\$(?!\$)/g;
while ((m = i.exec(md))) exprs.add(m[1].trim());

let ok = 0, empty = 0;
const fails = [];
for (const e of exprs) {
  let r;
  try { r = render(e); } catch (err) { fails.push({ e, why: err.message }); continue; }
  const h = r.html;
  if (r.errors.length) { fails.push({ e, why: '未支持: ' + r.errors.join(',') }); continue; }
  if (/[{}]/.test(h)) { fails.push({ e, why: '输出含裸花括号' }); continue; }
  if (h.trim() === '' && e.trim() !== '') { fails.push({ e, why: '输出为空' }); empty++; continue; }
  if (/munknown/.test(h)) { fails.push({ e, why: '含未知命令标记' }); continue; }
  ok++;
}

console.log(`=== 全量公式验证 ===`);
console.log(`总计 ${exprs.size}，通过 ${ok}，异常 ${fails.length}\n`);
for (const f of fails.slice(0, 15)) {
  console.log(`❌ [${f.why}]  ${f.e.slice(0, 120)}`);
}

// 关键构造抽查
console.log('\n=== 关键构造抽查 ===');
const spot = [
  ['下标', 'P_{cost}', (h) => /<sub class="msub">/.test(h)],
  ['上标', 'A^{\\top}', (h) => /<sup class="msup">/.test(h)],
  ['分式', '\\frac{a}{b}', (h) => /mfrac-num/.test(h) && /mfrac-den/.test(h)],
  ['嵌套分式', '\\frac{420}{1+e^{-0.25(w-10)}}', (h) => (h.match(/mfrac-num/g) || []).length === 1 && /<sup class="msup">/.test(h)],
  ['根号次数', '\\sqrt[3]{x}', (h) => /msqrt-deg/.test(h)],
  ['cases', '\\begin{cases} a & b \\\\ c & d \\end{cases}', (h) => /mcases/.test(h)],
  ['表格脚注', '^{†}', (h) => /<sup/.test(h)],
  ['文本', '\\text{上限}', (h) => /mtext/.test(h)],
];
let spotBad = 0;
for (const [label, tex, check] of spot) {
  const r = render(tex);
  const pass = check(r.html);
  if (!pass) spotBad++;
  console.log(`   ${pass ? '✅' : '❌'} ${label}: ${tex}`);
  if (!pass) console.log(`        → ${r.html.slice(0, 160)}`);
}

console.log(`\n结论：公式 ${ok}/${exprs.size} 正常，抽查 ${spot.length - spotBad}/${spot.length} 通过`);
process.exit(fails.length || spotBad ? 1 : 0);
