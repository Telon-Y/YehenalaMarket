// VC_Go/tools/goparse.js —— Go 源码语法自检器（无工具链时的替代方案）
//
// 背景：沙箱禁止管道 stdio，而 Go 工具链内部用管道在 go 与 compile/vet 之间通信，
// 因此 go build / go vet / go test 在本环境内无法运行（任何受限沙箱模式都如此）。
// 本脚本用纯 JS 实现一个够用的 Go 词法+结构分析器，覆盖手写代码最可能犯的错误：
//
//   ① 词法层：字符串/字符/行注释/块注释/原始串的边界；未闭合字面量
//   ② 括号层：() [] {} 的类型匹配与平衡（不是简单计数，是按类型配对）
//   ③ 结构层：func/if/for/switch/select 的大括号配对；case 是否在 switch 内
//   ④ 声明层：import 是否被使用；:= 声明的局部变量是否被使用（Go 会因此编译失败）
//   ⑤ 类型层（启发式）：同一文件内 struct 字段名重复；同名函数重复定义
//   ⑥ 引用层（跨文件）：本模块内被引用的包级标识符是否存在定义
//
// 它不能替代编译器（不查类型、不查接口满足），但能覆盖语法与拼写类错误。
//
// 运行：node VC_Go/tools/goparse.js gosim
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

// ---------------------------------------------------------------- 词法层
const GO_KEYWORDS = new Set([
  'break', 'case', 'chan', 'const', 'continue', 'default', 'defer', 'else',
  'fallthrough', 'for', 'func', 'go', 'goto', 'if', 'import', 'interface',
  'map', 'package', 'range', 'return', 'select', 'struct', 'switch', 'type', 'var',
]);

function tokenize(src) {
  const toks = [];
  let i = 0, line = 1;
  const push = (kind, val, ln) => toks.push({ kind, val, line: ln });
  while (i < src.length) {
    const c = src[i], n = src[i + 1];
    if (c === '\n') { line++; i++; continue; }
    if (c === ' ' || c === '\t' || c === '\r') { i++; continue; }
    if (c === '/' && n === '/') { while (i < src.length && src[i] !== '\n') i++; continue; }
    if (c === '/' && n === '*') {
      const start = line; i += 2;
      let closed = false;
      while (i < src.length) {
        if (src[i] === '\n') line++;
        if (src[i] === '*' && src[i + 1] === '/') { i += 2; closed = true; break; }
        i++;
      }
      if (!closed) toks.push({ kind: 'ERR', val: '块注释未闭合', line: start });
      continue;
    }
    if (c === '`') {
      const start = line; i++;
      let closed = false;
      while (i < src.length) {
        if (src[i] === '\n') line++;
        if (src[i] === '`') { i++; closed = true; break; }
        i++;
      }
      push('rawstr', '', start);
      if (!closed) toks.push({ kind: 'ERR', val: '原始字符串未闭合', line: start });
      continue;
    }
    if (c === '"') {
      const start = line; i++;
      let closed = false;
      while (i < src.length) {
        if (src[i] === '\\') { i += 2; continue; }
        if (src[i] === '\n') break;
        if (src[i] === '"') { i++; closed = true; break; }
        i++;
      }
      push('str', '', start);
      if (!closed) toks.push({ kind: 'ERR', val: '字符串未闭合', line: start });
      continue;
    }
    if (c === "'") {
      const start = line; i++;
      let closed = false;
      while (i < src.length) {
        if (src[i] === '\\') { i += 2; continue; }
        if (src[i] === '\n') break;
        if (src[i] === "'") { i++; closed = true; break; }
        i++;
      }
      push('rune', '', start);
      if (!closed) toks.push({ kind: 'ERR', val: '字符字面量未闭合', line: start });
      continue;
    }
    if (/[A-Za-z_\u4e00-\u9fff]/.test(c)) {
      let j = i;
      while (j < src.length && /[A-Za-z0-9_\u4e00-\u9fff]/.test(src[j])) j++;
      const w = src.slice(i, j);
      push(GO_KEYWORDS.has(w) ? 'kw' : 'ident', w, line);
      i = j; continue;
    }
    if (/[0-9]/.test(c)) {
      let j = i;
      while (j < src.length && /[0-9a-fA-FxXoObB._]/.test(src[j])) j++;
      push('num', src.slice(i, j), line);
      i = j; continue;
    }
    // 多字符运算符
    const three = src.substr(i, 3);
    const two = src.substr(i, 2);
    if (['...', '<<=', '>>=', '&^='].includes(three)) { push('op', three, line); i += 3; continue; }
    if ([':=', '==', '!=', '<=', '>=', '&&', '||', '++', '--', '+=', '-=', '*=', '/=', '%=',
         '<<', '>>', '&^', '<-'].includes(two)) { push('op', two, line); i += 2; continue; }
    push('op', c, line);
    i++;
  }
  return toks;
}

// ---------------------------------------------------------------- 结构层
function checkStructure(file, toks) {
  const issues = [];
  // 按类型配对的括号栈
  const pairs = { '(': ')', '[': ']', '{': '}' };
  const stack = [];
  const blockStack = []; // 记录 { 的语义上下文

  for (let k = 0; k < toks.length; k++) {
    const t = toks[k];
    if (t.kind === 'ERR') { issues.push(`第 ${t.line} 行: ${t.val}`); continue; }
    if (t.kind !== 'op') continue;
    if (pairs[t.val]) {
      // 判断这个 { 属于什么语句
      let ctx = 'block';
      for (let b = k - 1; b >= 0 && b > k - 12; b--) {
        const p = toks[b];
        if (p.kind === 'kw') {
          if (p.val === 'func' || p.val === 'if' || p.val === 'for' ||
              p.val === 'switch' || p.val === 'select' || p.val === 'else') { ctx = p.val; break; }
          if (p.val === 'struct' || p.val === 'interface' || p.val === 'map') { ctx = p.val; break; }
        }
        if (p.kind === 'op' && (p.val === ')' || p.val === '}')) break;
      }
      stack.push({ ch: t.val, line: t.line, ctx });
      blockStack.push(ctx);
    } else if ([')', ']', '}'].includes(t.val)) {
      const top = stack.pop();
      blockStack.pop();
      if (!top) { issues.push(`第 ${t.line} 行: 多余的 '${t.val}'`); continue; }
      if (pairs[top.ch] !== t.val) {
        issues.push(`第 ${t.line} 行: '${t.val}' 与第 ${top.line} 行的 '${top.ch}' 类型不匹配`);
      }
    } else if (t.val === 'case' || t.val === 'default') {
      // case 必须在 switch 内
      if (!blockStack.includes('switch') && !blockStack.includes('select')) {
        issues.push(`第 ${t.line} 行: '${t.val}' 出现在 switch/select 之外`);
      }
    }
  }
  if (stack.length) {
    const rest = stack.slice(-8).map((s) => `${s.ch}@${s.line}(${s.ctx})`).join(' ');
    issues.push(`未闭合的括号 ${stack.length} 个: ${rest}`);
  }

  // else 必须紧跟在 } 之后（同行或下一行开头），否则 Go 报语法错误
  for (let k = 0; k < toks.length - 1; k++) {
    if (toks[k].kind === 'kw' && toks[k].val === 'else') {
      const prev = toks[k - 1];
      if (!prev || !(prev.kind === 'op' && prev.val === '}')) {
        issues.push(`第 ${toks[k].line} 行: 'else' 未紧跟在 '}' 之后（Go 要求同一行）`);
      }
    }
  }
  return issues;
}

// ---------------------------------------------------------------- 声明层
function checkDecls(file, src, toks) {
  const issues = [];

  // import 使用检查
  const importRe = /^import\s*\(([\s\S]*?)\n\)/m;
  const importSingle = /^import\s+(?:\w+\s+)?"([^"]+)"/m;
  let importBlock = null;
  const m = src.match(importRe);
  if (m) importBlock = { body: m[1], index: m.index, len: m[0].length };
  else {
    const s = src.match(importSingle);
    if (s) importBlock = { body: `"${s[1]}"`, index: s.index, len: s[0].length };
  }
  if (importBlock) {
    const names = [...importBlock.body.matchAll(/(?:(\w+)\s+)?"([^"]+)"/g)].map((x) => ({
      alias: x[1] || null,
      path: x[2],
      base: x[1] || x[2].split('/').pop(),
    }));
    const rest = src.slice(0, importBlock.index) + src.slice(importBlock.index + importBlock.len);
    for (const nm of names) {
      const re = new RegExp('\\b' + nm.base.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\s*\\.');
      if (!re.test(rest)) {
        issues.push(`未使用的 import "${nm.path}"（Go 编译器会报 declared and not used）`);
      }
    }
  }

  // 包级重复声明（func / type / var / const 名）
  const decls = new Map();
  const funcRe = /^func\s+(?:\([^)]*\)\s*)?(\w+)\s*\(/gm;
  let fm;
  while ((fm = funcRe.exec(src)) !== null) {
    const name = fm[1];
    const ln = src.slice(0, fm.index).split('\n').length;
    if (decls.has(name)) {
      // 允许方法同名（接收者不同），只在无接收者时报告
      const isMethod = /^func\s*\(/.test(fm[0]);
      if (!isMethod) issues.push(`第 ${ln} 行: 函数 ${name} 重复定义（首次在第 ${decls.get(name)} 行）`);
    } else decls.set(name, ln);
  }

  // struct 字段重复：用花括号深度扫描界定 struct 体，而不是非贪婪正则
  // （非贪婪正则会越过 struct 的 } 把后续函数体一起吃进来，造成大量误报）
  {
    const src2 = src;
    const typeStructRe = /\btype\s+(\w+)\s+struct\s*\{/g;
    let tm;
    while ((tm = typeStructRe.exec(src2)) !== null) {
      const typeName = tm[1];
      let i = tm.index + tm[0].length;
      let depth = 1;
      const bodyStart = i;
      // 跳过字符串与注释，找到匹配的 }
      while (i < src2.length && depth > 0) {
        const c = src2[i];
        if (c === '"' || c === '`' || c === "'") {
          const q = c; i++;
          while (i < src2.length && src2[i] !== q) { if (src2[i] === '\\') i++; i++; }
          i++; continue;
        }
        if (c === '/' && src2[i + 1] === '/') { while (i < src2.length && src2[i] !== '\n') i++; continue; }
        if (c === '/' && src2[i + 1] === '*') { i += 2; while (i < src2.length && !(src2[i] === '*' && src2[i + 1] === '/')) i++; i += 2; continue; }
        if (c === '{') depth++;
        else if (c === '}') depth--;
        i++;
      }
      const body = src2.slice(bodyStart, i - 1);
      // 只取深度为 0 的顶层字段行（忽略嵌套的 struct 字面量 / 接口）
      const fieldNames = [];
      let d2 = 0;
      for (const rawLine of body.split('\n')) {
        const t = rawLine.trim();
        if (d2 === 0 && t && !t.startsWith('//')) {
          const fm2 = t.match(/^([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s+[\[\*\w]/);
          if (fm2) for (const nm of fm2[1].split(',')) fieldNames.push(nm.trim());
        }
        for (const ch of rawLine) {
          if (ch === '{' || ch === '(' || ch === '[') d2++;
          else if (ch === '}' || ch === ')' || ch === ']') d2--;
        }
      }
      const seen = new Map();
      for (const fn of fieldNames) {
        if (seen.has(fn)) issues.push(`struct ${typeName}: 字段 ${fn} 重复声明`);
        seen.set(fn, true);
      }
    }
  }

  // := 声明的变量是否被使用（Go 会报 declared and not used）
  const lines = src.split('\n');
  lines.forEach((L, idx) => {
    const t = L.trim();
    if (t.startsWith('//')) return;
    const mm = t.match(/^(\w+)(?:\s*,\s*(\w+))?\s*:=\s*/);
    if (!mm) return;
    for (const name of [mm[1], mm[2]].filter(Boolean)) {
      if (name === '_') continue;
      const total = (src.match(new RegExp('\\b' + name + '\\b', 'g')) || []).length;
      if (total <= 1) issues.push(`第 ${idx + 1} 行: 变量 ${name} 声明后未使用`);
    }
  });

  return issues;
}

// ---------------------------------------------------------------- 跨文件引用
function collectDefs(files) {
  const defs = new Map(); // 包名 -> Set(导出标识符)
  for (const f of files) {
    const src = fs.readFileSync(f, 'utf8');
    const pkgM = src.match(/^package\s+(\w+)/m);
    if (!pkgM) continue;
    const base = path.basename(path.dirname(f));
    const pkg = base === path.basename(root) ? 'main' : base;
    if (!defs.has(pkg)) defs.set(pkg, new Set());
    const S = defs.get(pkg);
    for (const re of [
      /^func\s+(?:\([^)]*\)\s*)?([A-Z]\w*)\s*\(/gm,
      /^type\s+([A-Z]\w*)\s/gm,
      /^var\s+([A-Z]\w*)\s/gm,
      /^const\s+([A-Z]\w*)\s/gm,
      /^\t([A-Z]\w*)\s+[\w\[\]\*]/gm, // struct 的导出字段
    ]) {
      let mm;
      while ((mm = re.exec(src)) !== null) S.add(mm[1]);
    }
    // 顶层 const/var 块内的导出名
    const blockRe = /^(?:const|var)\s*\(([\s\S]*?)\n\)/gm;
    let bm;
    while ((bm = blockRe.exec(src)) !== null) {
      for (const line of bm[1].split('\n')) {
        const nm = line.trim().match(/^([A-Z]\w*)/);
        if (nm) S.add(nm[1]);
      }
    }
  }
  return defs;
}

function checkCrossRefs(files, defs) {
  const issues = [];
  for (const f of files) {
    const src = fs.readFileSync(f, 'utf8');
    const pkgM = src.match(/^package\s+(\w+)/m);
    if (!pkgM) continue;
    // 收集 import 别名 → 包名
    const aliasToPkg = new Map();
    const importRe = /^import\s*\(([\s\S]*?)\n\)/m;
    const m = src.match(importRe);
    const body = m ? m[1] : (src.match(/^import\s+(?:\w+\s+)?"([^"]+)"/m) || [, ''])[1];
    for (const mm of body.matchAll(/(?:(\w+)\s+)?"([^"]+)"/g)) {
      const pkgName = mm[2].split('/').pop();
      aliasToPkg.set(mm[1] || pkgName, pkgName);
    }
    for (const [alias, pkg] of aliasToPkg) {
      const S = defs.get(pkg);
      if (!S) continue; // 标准库
      const re = new RegExp('\\b' + alias + '\\.([A-Z]\\w*)', 'g');
      let mm;
      const reported = new Set();
      while ((mm = re.exec(src)) !== null) {
        const ident = mm[1];
        if (reported.has(ident)) continue;
        reported.add(ident);
        if (!S.has(ident)) {
          const ln = src.slice(0, mm.index).split('\n').length;
          issues.push(`第 ${ln} 行: ${alias}.${ident} —— 包 ${pkg} 中未找到导出标识符 ${ident}`);
        }
      }
    }
  }
  return issues;
}

// ---------------------------------------------------------------- 主流程
const allDefs = collectDefs(files);
let total = 0;
console.log(`Go 语法自检：${files.length} 个文件\n`);

for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  const toks = tokenize(src);
  const issues = [
    ...checkStructure(f, toks),
    ...checkDecls(f, src, toks),
  ];
  if (issues.length) {
    total += issues.length;
    console.log(`❌ ${f}`);
    for (const s of issues) console.log(`     ${s}`);
  } else {
    console.log(`✅ ${f}`);
  }
}

const cross = checkCrossRefs(files, allDefs);
if (cross.length) {
  total += cross.length;
  console.log(`\n❌ 跨文件引用检查`);
  for (const s of cross) console.log(`     ${s}`);
} else {
  console.log(`\n✅ 跨文件引用检查`);
}

console.log(`\n合计问题数：${total}`);
console.log('注意：本检查器不替代编译器——它不查类型、不查接口满足、不查方法集。');
process.exit(total === 0 ? 0 : 1);
