// VC_Go/tools/latex_coverage.js —— 检查文档用到的 LaTeX 构造是否被 renderMath 支持
//
// 做法：从 md 抽出所有公式 → 提取其中每个 \command → 逐个单独渲染并检查输出质量，
// 一次性列出"未支持/渲染可疑"的清单，避免逐个碰到再修。
//
// 运行：node VC_Go/tools/latex_coverage.js "VC_Go/1.0 生产与市场模拟.md"
'use strict';
const fs = require('fs');
const { renderMath } = require('./tex2html.js');

const file = process.argv[2] || '1.0 生产与市场模拟.md';
const md = fs.readFileSync(file, 'utf8');

// 抽公式
const exprs = new Set();
let m;
const b = /\$\$([\s\S]+?)\$\$/g;
while ((m = b.exec(md))) exprs.add(m[1].trim());
const i = /(?<!\$)\$([^$\n]+?)\$(?!\$)/g;
while ((m = i.exec(md))) exprs.add(m[1].trim());

// 统计每个 \command 的使用次数
const cmdCount = new Map();
for (const e of exprs) {
  for (const c of e.matchAll(/\\([a-zA-Z]+)/g)) {
    cmdCount.set(c[1], (cmdCount.get(c[1]) || 0) + 1);
  }
}
// 同时统计环境
const envCount = new Map();
for (const e of exprs) {
  for (const c of e.matchAll(/\\begin\{([a-zA-Z*]+)\}/g)) {
    envCount.set(c[1], (envCount.get(c[1]) || 0) + 1);
  }
}

console.log(`=== LaTeX 构造覆盖率检查 ===\n`);
console.log(`公式总数 ${exprs.size}，用到 ${cmdCount.size} 个命令、${envCount.size} 个环境\n`);

// 单个命令的渲染测试：构造最小可用上下文
function probeCommand(name) {
  const tests = {
    frac: `\\frac{a}{b}`, dfrac: `\\dfrac{a}{b}`, tfrac: `\\tfrac{a}{b}`,
    sqrt: `\\sqrt{a}`, text: `\\text{ab}`, mathrm: `\\mathrm{ab}`,
    operatorname: `\\operatorname{ab}`, mathbf: `\\mathbf{a}`, boldsymbol: `\\boldsymbol{a}`,
    left: `\\left(a\\right)`, right: `\\right)`, big: `\\big(a\\big)`,
    begin: `\\begin{cases}a&b\\end{cases}`,
  };
  if (tests[name]) return tests[name];
  return `x ${'\\' + name} y`;
}

const issues = [];
for (const [name, cnt] of [...cmdCount].sort((a, b) => b[1] - a[1])) {
  const tex = probeCommand(name);
  const r = renderMath(tex);
  const h = r.html || '';
  let bad = null;
  if (!r.ok) bad = '渲染失败';
  else if (/\\[a-zA-Z]{2,}/.test(h)) bad = 'LaTeX 残留';
  else if (/[{}]/.test(h)) bad = '裸花括号';
  else if (bad === null && /^(top|begin|end|qquad|quad|cdot|cdots|ldots|ldots)$/.test(name)) bad = null;
  if (bad) issues.push({ name, cnt, tex, bad, h });
}

console.log('命令使用频次（前 30）:');
for (const [name, cnt] of [...cmdCount].sort((a, b) => b[1] - a[1]).slice(0, 30)) {
  console.log(`   ${String(cnt).padStart(4)}  \\${name}`);
}

if (envCount.size) {
  console.log('\n环境使用:');
  for (const [name, cnt] of envCount) console.log(`   ${cnt}  \\begin{${name}}`);
}

console.log(`\n=== 有问题的构造（${issues.length} 个）===`);
for (const it of issues) {
  console.log(`\n\\${it.name}  (用 ${it.cnt} 次) — ${it.bad}`);
  console.log(`  探针: ${it.tex}`);
  console.log(`  输出: ${it.h.slice(0, 200)}`);
}

// 关键：直接找出经典命令在"嵌套"场景下是否正常
console.log('\n=== 嵌套场景回归测试 ===');
const nested = [
  ['\\frac{420}{1 + e^{-0.25(w-10)}}', '分式分母含上标'],
  ['\\frac{K(P)T^2}{4\\pi^2}', '分式两侧含上标'],
  ['\\frac{a}{\\frac{b}{c}}', '分式嵌套分式'],
  ['A^{\\top}', '转置符号'],
  ['\\sqrt{\\frac{a}{b}}', '根号内含分式'],
  ['\\left(\\frac{a}{b}\\right)', '自适应括号含分式'],
  ['\\min(B,\\ \\text{上限})', 'min 带中文 text'],
  ['e^{-b(w-5)}', '指数含括号'],
  ['\\frac{\\partial \\ln P^*}{\\partial \\ln S} = -\\frac{1}{\\varepsilon}', '偏导分式'],
];
for (const [tex, label] of nested) {
  const r = renderMath(tex);
  const h = r.html || '';
  const clean = !/[{}]/.test(h) && !/\\[a-zA-Z]{2,}/.test(h) && r.ok;
  console.log(`   ${clean ? '✅' : '❌'} ${label}`);
  console.log(`        ${tex}`);
  if (!clean) console.log(`        → ${h.slice(0, 180)}`);
}

console.log(`\n合计需处理：${issues.length} 个命令 + ${envCount.size} 个环境`);
