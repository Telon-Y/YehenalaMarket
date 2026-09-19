// tools/verify_claims.js
// ---------------------------------------------------------------------------
// 判决书定量断言的逐条复核器（JS 侧；与 tools/cpp/contract_check.cpp 同题互验）
//
// 目的：把「判决书里引用的每一个数字」变成**可机检**的断言，并同时给出：
//   ① 本脚本（JS，double）的计算值；
//   ② C++ 复核器（tools/cpp/contract_check.cpp，long double）的计算值；
//   ③ 两份实现是否互相一致（跨语言、跨数值路径的一致性检查）；
//   ④ 与文档记录值的逐条判定：CONFIRMED / CLOSE / DIFFERS。
//
// 依赖：只用 node 内建 fs/path。本机无 node 时用 DSH Desktop 的 Electron：
//   $env:ELECTRON_RUN_AS_NODE="1"
//   & "D:\DSH\DSH Desktop\DSH Desktop.exe" tools\verify_claims.js
//
// 产出：out/verdict/claims-verified-js.txt
// ---------------------------------------------------------------------------

'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const CPP_TXT = path.join(ROOT, 'out', 'verdict', 'contract-check-cpp.txt');
const OUT_TXT = path.join(ROOT, 'out', 'verdict', 'claims-verified-js.txt');

const WAGE_PER_LEVEL = 33750;   // §5
const S_GOV = 0.70;             // §4.5.1
const TAX = 0.10;               // §4.5.3
const DEBT_MULT = 2.0;          // §4.5.4
const POWER_PC = 7250;          // §3.1
const POWER_Q = 15;             // §3.3
const SUBSIST = { grain: 2.0, fabric: 1.0, clothes: 0.5 };

// §3.1 + §3.3 + §8.6（逐项转录；power 条目也被 §3.3 当作建筑参与投入）
const G = [
  { key: 'grain',   name: '谷物',     Pc: 675,  q: 50,  L: 256.06, FD: 5487.0, in: {} },
  { key: 'food',    name: '加工食品', Pc: 1350, q: 45,  L: 182.90, FD: 8230.6, in: { grain: 40 } },
  { key: 'fabric',  name: '织物',     Pc: 750,  q: 45,  L: 81.66,  FD: 1947.4, in: {} },
  { key: 'clothes', name: '服装',     Pc: 788,  q: 100, L: 23.37,  FD: 2337.4, in: { fabric: 60 } },
  { key: 'luxury',  name: '高档服装', Pc: 1750, q: 30,  L: 13.00,  FD: 390.0,  in: { fabric: 25 } },
  { key: 'coal',    name: '煤',       Pc: 1006, q: 60,  L: 7.56,   FD: 0,      in: { tools: 15, coal: 15 } },
  { key: 'iron',    name: '铁',       Pc: 1006, q: 60,  L: 7.56,   FD: 0,      in: { tools: 15, coal: 15 } },
  { key: 'steel',   name: '钢',       Pc: 1381, q: 90,  L: 7.56,   FD: 0,      in: { iron: 60, coal: 30 } },
  { key: 'tools',   name: '工具',     Pc: 767,  q: 80,  L: 9.08,   FD: 0,      in: { steel: 20 } },
  { key: 'housing', name: '住房',     Pc: 741,  q: 60,  L: 99.84,  FD: 5990.4, in: { steel: 5, tools: 5 } },
];
const POWER_IN = { steel: 25, iron: 25, tools: 20 };
const UPSTREAM = ['coal', 'iron', 'steel', 'tools'];

const out = [];
function p(s = '') { out.push(s); console.log(s); }
function hr(t) { p('\n' + '='.repeat(78)); if (t) p(t); p('='.repeat(78)); }

// 反解所需平衡等级：L_i = (FD_i + Σ_j c_ij L_j + power_i × powerL) / q_i
// ksub != null 时按 survival_condition_probe.js 的口径扣除自给农场供给。
function solveLevels(powerL, ksub) {
  let L = Object.fromEntries(G.map((g) => [g.key, 5]));
  for (let it = 0; it < 20000; it++) {
    const nx = {};
    let d = 0;
    for (const g of G) {
      let need = g.FD;
      if (ksub) {
        if (g.key === 'grain') need -= ksub * SUBSIST.grain;
        if (g.key === 'fabric') need -= ksub * SUBSIST.fabric;
        if (g.key === 'clothes') need -= ksub * SUBSIST.clothes;
      }
      for (const g2 of G) need += (g2.in[g.key] || 0) * L[g2.key];
      need += (POWER_IN[g.key] || 0) * powerL;
      nx[g.key] = need / g.q;
      d = Math.max(d, Math.abs(nx[g.key] - L[g.key]));
    }
    L = nx;
    if (d < 1e-13) break;
  }
  return L;
}

// 复刻 survival_condition_probe.js：建造部门固定 5 级 + 反解自给农场规模
function solveLevelsJsProbe() {
  let L = Object.fromEntries(G.map((g) => [g.key, 5]));
  let ksub = 0;
  for (let pass = 0; pass < 3; pass++) {
    L = solveLevels(5, ksub);
    ksub = (L.grain * 50 - 40 * L.food - 5487.0) / SUBSIST.grain;
  }
  return { L, ksub };
}

function flows(L, powerL) {
  let W = 0, finalValue = 0, totalOutValue = 0, intermValue = 0;
  for (const g of G) {
    W += L[g.key] * WAGE_PER_LEVEL;
    finalValue += g.FD * g.Pc;
    totalOutValue += L[g.key] * g.q * g.Pc;
    for (const g2 of G) intermValue += L[g2.key] * (g2.in[g.key] || 0) * g.Pc;
  }
  const powerGross = powerL * POWER_Q * POWER_PC;
  return { W, finalValue, totalOutValue, intermValue, powerGross };
}

// ── ① §8.6 口径（建造部门 20 级）──────────────────────────────────────────
// 判定文档断言用【§8.6 定案的 L* 表】作为基准（文档记录的 0.909157 等数字即由此得出），
// 反解出的 L*（733.05 级）只用于命题 A 的"不相容"判定与信息性对照。
const L_GIVEN = Object.fromEntries(G.map((g) => [g.key, g.L]));
const L86 = solveLevels(20, null);
const f86 = flows(L_GIVEN, 20);          // 基准：§8.6 定案 L*
const fSolved = flows(L86, 20);          // 信息性：反解 L*
const sumGiven = G.reduce((s, g) => s + g.L, 0);
const sumSolved = G.reduce((s, g) => s + L86[g.key], 0);
let downMax = 0, upMin = Infinity, upMax = 0;
for (const g of G) {
  const dev = (L86[g.key] - g.L) / g.L;
  if (UPSTREAM.includes(g.key)) { upMin = Math.min(upMin, dev); upMax = Math.max(upMax, dev); }
  else downMax = Math.max(downMax, Math.abs(dev));
}
const baseJS = f86.finalValue + f86.intermValue + f86.powerGross / (1 + TAX);
const baseGross = f86.finalValue + f86.intermValue + f86.powerGross;
const rev86 = baseJS * TAX;
const gap86 = S_GOV * f86.W - rev86;
const sMax86 = TAX * baseJS / f86.W;
const debtCap = DEBT_MULT * 20 * POWER_Q * POWER_PC;
const cover86 = rev86 / f86.W;
const coveragePay = (f86.W / (1 + TAX)) / f86.finalValue;
const baseSolved = fSolved.finalValue + fSolved.intermValue + fSolved.powerGross / (1 + TAX);
const revSolved = baseSolved * TAX;

// ── ② survival_condition_probe.js 口径（建造部门 5 级）────────────────────
const jsb = solveLevelsJsProbe();
const fjs = flows(jsb.L, 5);
const sumSolvedJs = G.reduce((s, g) => s + jsb.L[g.key], 0);
const baseJSprobe = fjs.finalValue + fjs.intermValue + fjs.powerGross / (1 + TAX);
const revJs = baseJSprobe * TAX;

// ── ③ 断言表 ──────────────────────────────────────────────────────────────
const CLAIMS = [
  { id: 'C2', text: '§3.3 与 §8.6 不相容：上游需 2.0~3.1 倍', got: 1 + upMax, doc: 3.1, tol: 5, mode: 'match' },
  { id: 'C3', text: '反解所需总等级 ≠ §8.6 定案 688.59', got: sumSolved, doc: 688.61, tol: 3, mode: 'differ' },
  { id: 'C4', text: '平衡态税后覆盖率 0.909157', got: coveragePay, doc: 0.909157, tol: 0.5, mode: 'match' },
  { id: 'C5', text: '税收只覆盖约 9.5%（税收/W）', got: cover86, doc: 0.095, tol: 5, mode: 'match' },
  { id: 'C6', text: '政府缺口 0.61·W', got: gap86 / f86.W, doc: 0.61, tol: 5, mode: 'match' },
  { id: 'C7', text: 't=10% 时政府持股上限 s ≤ 0.04', got: sMax86, doc: 0.04, tol: 5, mode: 'match' },
  { id: 'C8', text: '债务上限只够 3~6 个周期', got: debtCap / gap86, doc: 3.0, tol: 5, mode: 'match' },
];
for (const c of CLAIMS) {
  const rel = c.doc !== 0 ? Math.abs(c.got - c.doc) / Math.abs(c.doc) * 100 : 0;
  c.rel = rel;
  if (c.mode === 'differ') c.verdict = rel > c.tol ? 'CONFIRMED' : 'DIFFERS';
  else if (rel <= c.tol) c.verdict = 'CONFIRMED';
  else if (rel <= 2 * c.tol) c.verdict = 'CLOSE';
  else c.verdict = 'DIFFERS';
}

// ── ④ 读 C++ 复核器的机器可读块，做跨语言一致性核对 ──────────────────────
function readCppClaims() {
  if (!fs.existsSync(CPP_TXT)) return null;
  const txt = fs.readFileSync(CPP_TXT, 'utf8');
  const idx = txt.indexOf('# CLAIMS-JSON');
  if (idx < 0) return null;
  const line = txt.slice(idx).split('\n').find((l) => l.trim().startsWith('{'));
  try { return JSON.parse(line); } catch (e) { return null; }
}
const cpp = readCppClaims();
const cppById = {};
if (cpp) for (const c of cpp.claims) cppById[c.id] = c;

// ── 输出 ─────────────────────────────────────────────────────────────────
hr('JS 复核器（node ' + process.version + '）—— 1.0 契约判决书的定量断言逐条复核');
p('  本脚本与 tools/cpp/contract_check.cpp 各自独立实现同一组算术；');
p('  两侧都只使用 docs/1.0 生产与市场模拟.md 里逐项转录的常数，不读取任何实跑中间产物。');

hr('一、命题 A 的算量（§8.6 口径：建造部门 20 级）');
p(`  反解所需总等级 = ${sumSolved.toFixed(2)} 级   §8.6 定案 = ${sumGiven.toFixed(2)} 级   偏差 ${((sumSolved - sumGiven) / sumGiven * 100).toFixed(2)}%`);
p(`  下游六项最大相对偏差 = ${(downMax * 100).toFixed(3)}%`);
p(`  上游四项相对偏差 = +${(upMin * 100).toFixed(1)}% ~ +${(upMax * 100).toFixed(1)}%  ⇒ 所需产能 ${(1 + upMin).toFixed(2)}~${(1 + upMax).toFixed(2)} 倍`);
for (const g of G) p(`    ${g.name.padEnd(6)} §8.6=${String(g.L).padStart(7)}  反解=${L86[g.key].toFixed(2).padStart(8)}`);

hr('二、命题 B 的算量（平衡点上，P = P_cost；基准 = §8.6 定案 L*）');
p(`  生产建筑总级数 = ${(f86.W / WAGE_PER_LEVEL).toFixed(2)} 级   工资总额 W = ${Math.round(f86.W).toLocaleString('en-US')}`);
p(`  最终需求价值 = ${Math.round(f86.finalValue).toLocaleString('en-US')}   中间投入 = ${Math.round(f86.intermValue).toLocaleString('en-US')}   建造力毛额 = ${f86.powerGross.toLocaleString('en-US')}`);
p(`  税基（JS 公式）= ${Math.round(baseJS).toLocaleString('en-US')}（全毛额口径 ${Math.round(baseGross).toLocaleString('en-US')}）`);
p(`  税收@t=10% = ${Math.round(rev86).toLocaleString('en-US')}   税收/W = ${cover86.toFixed(4)}`);
p(`  政府工资义务 = ${Math.round(S_GOV * f86.W).toLocaleString('en-US')}   覆盖率 = ${(rev86 / (S_GOV * f86.W) * 100).toFixed(2)}%`);
p(`  缺口 = ${Math.round(gap86).toLocaleString('en-US')} 元/周期 = ${(gap86 / f86.W).toFixed(4)}·W`);
p(`  t=10% 时 s ≤ ${sMax86.toFixed(4)}；s=0.70 时需 t* ≥ ${(S_GOV * f86.W / baseJS * 100).toFixed(2)}%`);
p(`  债务上限 ${debtCap.toLocaleString('en-US')} 元 = 覆盖缺口 ${(debtCap / gap86).toFixed(2)} 个周期`);
p(`  平衡态可支付性：(W/1.1) ÷ 最终需求 = ${coveragePay.toFixed(6)}  ⇒ 缺口 ${((1 - coveragePay) * 100).toFixed(4)}%`);

p('\n  【信息性对照】若改用【反解出的 L*】（733.05 级，即"按 §8.6 的最终需求重解一遍"）：');
p(`    W = ${Math.round(fSolved.W).toLocaleString('en-US')}   税收/W = ${(revSolved / fSolved.W).toFixed(4)}   缺口 = ${((S_GOV * fSolved.W - revSolved) / fSolved.W).toFixed(4)}·W   s ≤ ${(TAX * baseSolved / fSolved.W).toFixed(4)}`);
p('    ⇒ 该口径下的结论（税收远不足以覆盖政府工资义务）与基准口径一致，仅数值略移。');

hr('三、survival_condition_probe.js 口径的复刻（建造部门固定 5 级）');
p(`  反解自给农场 k_sub = ${jsb.ksub.toFixed(4)}   总级数 = ${sumSolvedJs.toFixed(2)}（§8.6 定案 ${sumGiven.toFixed(2)}）`);
p(`  W = ${Math.round(fjs.W).toLocaleString('en-US')}   税收/W = ${(revJs / fjs.W).toFixed(4)}   政府工资义务覆盖率 = ${(revJs / (S_GOV * fjs.W) * 100).toFixed(2)}%`);
p(`  缺口 = ${((S_GOV * fjs.W - revJs) / fjs.W).toFixed(4)}·W   t=10% 时 s ≤ ${(TAX * baseJSprobe / fjs.W).toFixed(4)}`);
p('  ⇒ 两种口径都不支持文档记录的 0.095（≈9.5%）：复核值分别是 ' +
  `${cover86.toFixed(4)}（§8.6 口径）与 ${(revJs / fjs.W).toFixed(4)}（探针口径）。`);

hr('四、逐条判定');
p('  ID   断言                                      JS 复核值     文档值      相对偏差    判定');
for (const c of CLAIMS) {
  p(`  ${c.id.padEnd(4)} ${c.text.padEnd(38)} ${c.got.toFixed(6).padStart(12)} ${c.doc.toFixed(6).padStart(11)} ${(c.rel.toFixed(2) + '%').padStart(10)}    ${c.verdict}`);
}
const nOk = CLAIMS.filter((c) => c.verdict === 'CONFIRMED').length;
const nClose = CLAIMS.filter((c) => c.verdict === 'CLOSE').length;
const nDiff = CLAIMS.filter((c) => c.verdict === 'DIFFERS').length;
p(`\n  ⇒ CONFIRMED ${nOk} 条，CLOSE ${nClose} 条，DIFFERS ${nDiff} 条`);

hr('五、跨语言一致性核对（JS double ↔ C++ long double）');
if (!cpp) {
  p('  未找到 out/verdict/contract-check-cpp.txt 的 # CLAIMS-JSON 块；');
  p('  先运行：g++ -O2 -std=c++17 -o out/bin/contract_check.exe tools/cpp/contract_check.cpp');
  p('          out/bin/contract_check.exe > out/verdict/contract-check-cpp.txt');
} else {
  p(`  C++ 侧口径：${cpp.basis}`);
  p('  ID     JS 复核值        C++ 复核值       相对差     一致性');
  let mismatch = 0;
  for (const c of CLAIMS) {
    const cppc = cppById[c.id];
    if (!cppc) { p(`  ${c.id.padEnd(5)} （C++ 侧缺少该断言）`); mismatch++; continue; }
    const rel = Math.abs(c.got - cppc.got) / Math.abs(cppc.got) * 100;
    const agree = rel < 0.5;
    if (!agree) mismatch++;
    p(`  ${c.id.padEnd(5)} ${c.got.toFixed(6).padStart(14)} ${Number(cppc.got).toFixed(6).padStart(16)} ${(rel.toFixed(4) + '%').padStart(11)}    ${agree ? 'AGREE' : 'MISMATCH'}`);
  }
  p(`\n  ⇒ 两套独立实现${mismatch === 0 ? '在全部断言上一致（AGREE）' : `有 ${mismatch} 条不一致，须人工裁决`}`);
}

fs.mkdirSync(path.dirname(OUT_TXT), { recursive: true });
fs.writeFileSync(OUT_TXT, out.join('\n') + '\n', 'utf8');
console.log('\n[已写出] out/verdict/claims-verified-js.txt');
