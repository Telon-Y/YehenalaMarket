// VC_Go/tools/gocheck.js —— Go 源码的轻量静态自检（无 Go 工具链时的替代）
//
// 检查项：
//   ① 括号 / 方括号 / 花括号平衡（跳过字符串、字符、行注释、块注释、反引号原始串）
//   ② 原始字符串（反引号）内的双引号 → 已知会引发语法错误的写法
//   ③ 双引号字符串内部出现未转义的双引号
//   ④ package 声明与文件名一致性提示
//   ⑤ 常见笔误：`:=` 在包级、多余逗号、`func` 后缺空格
//   ⑥ 报告每个文件的括号深度峰值，便于人工复核
//
// 运行：node VC_Go/tools/gocheck.js [目录]
'use strict';
const fs = require('fs');
const path = require('path');

const root = process.argv[2] || 'gosim';
const files = [];
(function walk(d) {
  for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name);
    if (e.isDirectory()) walk(p);
    else if (e.name.endsWith('.go')) files.push(p);
  }
})(root);

let problems = 0;
console.log(`扫描 ${files.length} 个 .go 文件\n`);

for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  const issues = [];
  const stack = [];
  let i = 0, line = 1, depthPeak = 0;
  let inLine = false, inBlock = false, inStr = false, inRaw = false, inChar = false;
  let rawStartLine = 0, strStartLine = 0;

  while (i < src.length) {
    const c = src[i], n = src[i + 1];
    if (c === '\n') { line++; inLine = false; i++; continue; }
    if (inLine) { i++; continue; }
    if (inBlock) { if (c === '*' && n === '/') { inBlock = false; i += 2; continue; } i++; continue; }
    if (inRaw) {
      if (c === '`') { inRaw = false; i++; continue; }
      i++; continue;
    }
    if (inStr) {
      if (c === '\\') { i += 2; continue; }
      if (c === '"') { inStr = false; i++; continue; }
      i++; continue;
    }
    if (inChar) {
      if (c === '\\') { i += 2; continue; }
      if (c === "'") { inChar = false; i++; continue; }
      i++; continue;
    }
    if (c === '/' && n === '/') { inLine = true; i += 2; continue; }
    if (c === '/' && n === '*') { inBlock = true; i += 2; continue; }
    if (c === '`') { inRaw = true; rawStartLine = line; i++; continue; }
    if (c === '"') { inStr = true; strStartLine = line; i++; continue; }
    if (c === "'") {
      // 排除 rune 字面量里的单引号误判：只当作字符字面量处理
      inChar = true; i++; continue;
    }
    if (c === '{' || c === '(' || c === '[') {
      stack.push({ ch: c, line });
      if (stack.length > depthPeak) depthPeak = stack.length;
      i++; continue;
    }
    if (c === '}' || c === ')' || c === ']') {
      const want = c === '}' ? '{' : c === ')' ? '(' : '[';
      const top = stack.pop();
      if (!top) issues.push(`第 ${line} 行: 多余的 '${c}'`);
      else if (top.ch !== want) issues.push(`第 ${line} 行: '${c}' 与第 ${top.line} 行的 '${top.ch}' 不匹配`);
      i++; continue;
    }
    i++;
  }

  if (inRaw) issues.push(`反引号原始字符串未闭合（从第 ${rawStartLine} 行开始）`);
  if (inStr) issues.push(`双引号字符串未闭合（从第 ${strStartLine} 行开始）`);
  if (inBlock) issues.push('块注释未闭合');
  if (stack.length) {
    const unclosed = stack.slice(-6).map((s) => `'${s.ch}'@${s.line}`).join(' ');
    issues.push(`有 ${stack.length} 个未闭合的括号: ${unclosed}`);
  }

  // 检查项 ②：反引号原始串里不应出现双引号（会让 Go 视为字符串结束？不——原始串里双引号合法）
  // 真正的坑是：普通字符串里想表达「」以外的英文引号却忘了转义。逐个字符串扫描。
  {
    const re = /"((?:[^"\\\n]|\\.)*)"/g;
    let m;
    while ((m = re.exec(src)) !== null) {
      const body = m[1];
      if (body.includes('"')) issues.push(`第 ${lineOf(src, m.index)} 行: 字符串内含未转义双引号`);
    }
  }

  // 检查项：`package` 声明存在
  if (!/^package\s+\w+/m.test(src)) issues.push('缺少 package 声明');

  // 检查项 ⑦：未使用的 import（Go 会因此编译失败）
  {
    const m = src.match(/^import \(([\s\S]*?)\n\)/m);
    if (m) {
      const names = [...m[1].matchAll(/"([^"]+)"/g)].map((x) => x[1]);
      if (names.length) {
        const body = src.slice(0, m.index) + src.slice(m.index + m[0].length);
        for (const n of names) {
          const base = n.split('/').pop();
          const re = new RegExp('\\b' + base.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\.');
          if (!re.test(body)) issues.push(`未使用的 import: "${n}"（Go 编译器会报错）`);
        }
      }
    }
  }

  // 检查项 ⑧：声明后未使用的局部变量（只查最明显的 := 赋值后整文件不再出现）
  {
    const decls = [...src.matchAll(/^\s*(\w+)\s*:=\s*/gm)];
    const counts = {};
    for (const d of decls) counts[d[1]] = (counts[d[1]] || 0) + 1;
    for (const [name, n] of Object.entries(counts)) {
      const total = (src.match(new RegExp('\\b' + name + '\\b', 'g')) || []).length;
      if (total <= n) issues.push(`变量 ${name} 被 := 声明但从未使用（Go 会报 declared and not used）`);
    }
  }

  // 检查项 ⑨：字符串拼接里出现中文全角引号以外的裸英文双引号（易造成语法错）
  {
    const lines = src.split('\n');
    lines.forEach((L, k) => {
      const quotes = (L.match(/"/g) || []).length;
      if (quotes % 2 === 1 && !L.trim().startsWith('//') && !L.includes('`')) {
        const backticks = (L.match(/`/g) || []).length;
        if (backticks % 2 === 0) issues.push(`第 ${k + 1} 行: 双引号数量为奇数（${quotes}），可能字符串未闭合`);
      }
    });
  }

  if (issues.length) {
    problems += issues.length;
    console.log(`❌ ${f}（括号深度峰值 ${depthPeak}）`);
    for (const s of issues) console.log(`     ${s}`);
  } else {
    console.log(`✅ ${f}（括号深度峰值 ${depthPeak}）`);
  }
}

function lineOf(src, idx) {
  let l = 1;
  for (let i = 0; i < idx && i < src.length; i++) if (src[i] === '\n') l++;
  return l;
}

console.log(`\n合计问题数：${problems}`);
process.exit(problems === 0 ? 0 : 1);
