// VC_Go/tools/housing_curve_probe.js —— 「住宅」需求改为指数型曲线的候选核算
//
// 需求（用户指定）：
//   ① 住宅使用指数型，与现实相符；
//   ② 财富 10 时为两倍 —— 契约原表住宅在财富 10 处为 74，故目标值 148。
//
// 模型：D(w) = a · exp(b·(w − 10))，a = D(10) = 148
//
// 核心矛盾（本脚本存在的理由）：
//   指数曲线在 5–20 的跨度上增长极快。若额外要求保留契约原表的 D(5) = 20，
//   则 b = ln(148/20)/5 ≈ 0.399，外推到财富 20 得 D(20) ≈ 8,060 —— 是原表 130 的 62 倍。
//   因此必须放弃一个约束：
//     (A) 保留 D(5) = 20  → D(20) 失控；
//     (B) 保留 D(20) = 130（贴合原表右端）→ D(5) 被抬高；
//     (C) 折中：以 D(10) = 148 与 D(20) 取一个合理上限，反解 b。
//
// 运行：node VC_Go/tools/housing_curve_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const ANCHOR10 = 148;   // 财富 10 处的目标（契约原表 74 的两倍）
const ORIG5 = 20;       // 契约原表财富 5
const ORIG20 = 130;     // 契约原表财富 20

/** 由 D(10)=ANCHOR10 与 D(w1)=v1 反解指数参数。 */
function fitByPoint(w1, v1) {
  // a·exp(b(w1−10)) = v1  ⇒  b = ln(v1/a)/(w1−10)
  const b = Math.log(v1 / ANCHOR10) / (w1 - 10);
  return { a: ANCHOR10, b, at: (w) => ANCHOR10 * Math.exp(b * (w - 10)) };
}

console.log('=== 「住宅」指数型曲线候选核算 ===\n');
console.log(`模型：D(w) = 148 · exp(b(w − 10))     （财富 10 处恒为 148 = 契约原表 74 的两倍）\n`);

console.log('--- 方案 A：保留契约原表 D(5) = 20 ---');
{
  const c = fitByPoint(5, ORIG5);
  console.log(`  反解 b = ${c.b.toFixed(4)}`);
  console.log('  财富档  D(w)       取整     与原表(住宅)之差');
  for (let w = 5; w <= 20; w++) {
    const v = c.at(w);
    const orig = w <= 10 ? 20 + (74 - 20) * (w - 5) / 5 : 74 + (130 - 74) * (w - 10) / 10;
    console.log(String(w).padStart(6) + v.toFixed(1).padStart(11) + String(Math.round(v)).padStart(9) +
      ((Math.round(v) - Math.round(orig) >= 0 ? '+' : '') + (Math.round(v) - Math.round(orig))).padStart(17));
  }
  console.log(`  ⇒ D(20)/D(5) = ${(c.at(20) / c.at(5)).toFixed(1)} 倍，D(20) = ${Math.round(c.at(20))}`);
  console.log('  ❌ 失控：财富 20 处达 8,060，是原表 130 的 62 倍，不可能作为消费需求使用。\n');
}

console.log('--- 方案 B：保留契约原表 D(20) = 130 ---');
{
  const c = fitByPoint(20, ORIG20);
  console.log(`  反解 b = ${c.b.toFixed(4)}（负值，曲线随财富递减）`);
  console.log(`  ⇒ D(5) = ${c.at(5).toFixed(1)}，D(10) = ${c.at(10).toFixed(1)}，D(20) = ${c.at(20).toFixed(1)}`);
  console.log('  ❌ 与"财富 10 为两倍"冲突：要 D(10)=148 > D(20)=130，指数只能递减，方向反了。\n');
}

console.log('--- 方案 C：固定 D(10)=148，以 D(20) 的合理上限反解 b ---\n');
console.log('  目标 D(20)   b        D(5)      D(10)     D(20)     D(20)/D(5)');
const candidates = [];
for (const target20 of [200, 260, 320, 400, 500, 700, 1000]) {
  const c = fitByPoint(20, target20);
  candidates.push({ target20, c });
  console.log(
    String(target20).padStart(11) + c.b.toFixed(4).padStart(9) +
    c.at(5).toFixed(1).padStart(11) + c.at(10).toFixed(1).padStart(10) +
    c.at(20).toFixed(1).padStart(10) + (target20 / c.at(5)).toFixed(2).padStart(13),
  );
}

console.log('\n--- 选定候选的完整档位表 ---\n');
for (const target20 of [260, 400]) {
  const c = fitByPoint(20, target20);
  console.log(`  【D(20) = ${target20}，b = ${c.b.toFixed(4)}】`);
  console.log('  财富档  D(w)      取整   斜率(每档增量)');
  let prev = null;
  const rows = [];
  for (let w = 5; w <= 20; w++) {
    const v = c.at(w);
    rows.push(Math.round(v));
    const slope = prev === null ? '—' : (v - prev).toFixed(1);
    console.log(String(w).padStart(6) + v.toFixed(1).padStart(10) + String(Math.round(v)).padStart(8) + String(slope).padStart(14));
    prev = v;
  }
  console.log(`  取整后序列：${rows.join(', ')}\n`);
}

console.log('--- 与"符合现实"的对照 ---');
console.log('  现实中住房支出的特征：');
console.log('    · 收入上升时住房支出占比上升，但在高收入段趋于饱和（恩格尔定律的住房版）；');
console.log('    · 因此常见的拟合形式是「指数上升 + 饱和」，而非纯指数。');
console.log('  纯指数在 5–20 跨度上必然甩尾；若确要用纯指数，须接受 D(5) 被抬高。');
console.log('  若希望"指数感 + 可控上界"，可用 D(w) = C − (C − D(5))·exp(−b(w−5))，');
console.log('  它是"向饱和值 C 逼近"的指数型，形状与住房支出的现实特征一致。\n');
{
  // 给出饱和型指数的一个候选：D(5)=20，D(10)=148，反解 C 与 b
  // D(w) = C - (C-20)exp(-b(w-5))
  // D(10)=148 ⇒ C - (C-20)exp(-5b) = 148
  // 取 D(20)=260 作为第二个条件 ⇒ 两式联立数值解
  console.log('  【饱和型指数候选：D(5)=20、D(10)=148、D(20)=260】');
  let best = null;
  for (let C = 150; C <= 4000; C += 0.5) {
    // 由 D(10)=148 解 b
    const r5 = (C - 148) / (C - 20);
    if (r5 <= 0 || r5 >= 1) continue;
    const b = -Math.log(r5) / 5;
    const d20 = C - (C - 20) * Math.exp(-b * 15);
    const err = Math.abs(d20 - 260);
    if (best === null || err < best.err) best = { C, b, d20, err };
  }
  if (best) {
    const D = (w) => best.C - (best.C - 20) * Math.exp(-best.b * (w - 5));
    console.log(`  反解：饱和值 C = ${best.C.toFixed(1)}，b = ${best.b.toFixed(4)}（拟合残差 ${best.err.toFixed(3)}）`);
    console.log('  财富档  D(w)      取整   每档增量');
    let prev = null;
    const rows = [];
    for (let w = 5; w <= 20; w++) {
      const v = D(w);
      rows.push(Math.round(v));
      const slope = prev === null ? '—' : (v - prev).toFixed(1);
      console.log(String(w).padStart(6) + v.toFixed(1).padStart(10) + String(Math.round(v)).padStart(8) + String(slope).padStart(12));
      prev = v;
    }
    console.log(`  取整后序列：${rows.join(', ')}`);
    console.log('  ✅ 单调递增、D(10)=148（两倍）、有饱和上界，且 D(5)=20 与原表一致。');
  }
}

console.log('\n=== 待裁决项（本脚本不落表）===');
console.log('  ① 选纯指数还是饱和型指数？');
console.log('     纯指数：严格符合"指数型"字面，但 D(5) 会被抬高（约 56–84），且上界不可控；');
console.log('     饱和型：D(5)=20 与原表一致、D(10)=148 满足两倍、有现实意义上的饱和上界。');
console.log('  ② 若选纯指数，D(20) 取多少？（200 / 260 / 320 / 400 / 500 见上表）');
console.log('  ③ 食物（k=0.25）与住宅两条曲线同时改动后，§6.5 的人口动态会显著变化：');
console.log('     食物 D(5) 从 210 降到 94、住宅 D(10) 从 74 升到 148，');
console.log('     必需品满足度（只含简朴衣物 + 基础食物）会因此上升，人口增长加快。');

fs.writeFileSync('out/housing_curve_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/housing_curve_probe.txt');
