// VC_Go/tools/latex_inventory.js —— 列出文档用到的全部 LaTeX 记号，作为自研渲染器的需求清单
'use strict';
const fs = require('fs');
const md = fs.readFileSync(process.argv[2] || '1.0 生产与市场模拟.md', 'utf8');

const exprs = new Set();
let m;
const b = /\$\$([\s\S]+?)\$\$/g;
while ((m = b.exec(md))) exprs.add(m[1].trim());
const i = /(?<!\$)\$([^$\n]+?)\$(?!\$)/g;
while ((m = i.exec(md))) exprs.add(m[1].trim());

const cmds = new Map(), envs = new Map(), syms = new Map();
for (const e of exprs) {
  for (const c of e.matchAll(/\\([a-zA-Z]+|.)/g)) cmds.set(c[1], (cmds.get(c[1]) || 0) + 1);
  for (const c of e.matchAll(/\\begin\{([a-zA-Z*]+)\}/g)) envs.set(c[1], (envs.get(c[1]) || 0) + 1);
  // 裸符号：非字母数字非命令的可见字符
  for (const c of e.replace(/\\[a-zA-Z]+/g, '').matchAll(/[^\s\w{}]/g)) {
    syms.set(c[0], (syms.get(c[0]) || 0) + 1);
  }
}

const asciiCmd = [...cmds].filter(([k]) => k.length === 1);
const wordCmd = [...cmds].filter(([k]) => k.length > 1).sort((a, b) => b[1] - a[1]);

console.log(`公式 ${exprs.size} 个\n`);
console.log(`=== 单字符命令（转义符）${asciiCmd.length} 个 ===`);
console.log('   ' + asciiCmd.map(([k, v]) => `\\${k}×${v}`).join('  '));
console.log(`\n=== 词命令 ${wordCmd.length} 个 ===`);
console.log('   ' + wordCmd.map(([k, v]) => `\\${k}×${v}`).join('  '));
console.log(`\n=== 环境 ${envs.size} 个 ===`);
console.log('   ' + [...envs].map(([k, v]) => `${k}×${v}`).join('  '));
console.log(`\n=== 裸符号 ${syms.size} 个 ===`);
console.log('   ' + [...syms].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}×${v}`).join('  '));

// 关键结构用法统计
console.log('\n=== 结构用法 ===');
const struct = {
  '上标 ^': (md.match(/\^/g) || []).length,
  '下标 _': (md.match(/_/g) || []).length,
  '分式 \\frac': (md.match(/\\frac/g) || []).length,
  '\\dfrac': (md.match(/\\dfrac/g) || []).length,
  '根号 \\sqrt': (md.match(/\\sqrt/g) || []).length,
  '自适应括号 \\left': (md.match(/\\left/g) || []).length,
  '方程组 cases': (md.match(/\\begin\{cases\}/g) || []).length,
  '文本 \\text': (md.match(/\\text/g) || []).length,
  '多字母下标 _{}': (md.match(/_\{/g) || []).length,
  '多字母上标 ^{}': (md.match(/\^\{/g) || []).length,
};
for (const [k, v] of Object.entries(struct)) console.log(`   ${String(v).padStart(4)}  ${k}`);
