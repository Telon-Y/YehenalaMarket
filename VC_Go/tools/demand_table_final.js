// VC_Go/tools/demand_table_final.js —— §6.3 劳动力购买表的最终离散补全（四组）
//
// 本次修订（相对契约原表）：
//   ① 基础食物：恒为 210 → S 形 logistic，k = 0.25，拐点在财富 10
//                 D(w) = 420 / (1 + exp(-0.25(w-10)))，D(10) = 210（保留锚点）
//   ② 住宅    ：线性 20→130 → 饱和型指数（走法二）
//                 D(w) = C - (C - 20)·exp(-b(w-5))
//                 以 D(5)=20、D(10)=148、D(20)=260 反解 C、b
//   ③ 简朴衣物 / 标准衣物：本次不动，沿用契约线性插值
//
// 运行：node VC_Go/tools/demand_table_final.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

// ---------- ① 简朴衣物 / 标准衣物：沿用契约线性插值 ----------
const ANCHORS = [
  { w: 5, plain: 39, std: 0 },
  { w: 10, plain: 41, std: 7 },
  { w: 20, plain: 0, std: 122 },
];
function linear(w, key) {
  let lo = ANCHORS[0], hi = ANCHORS[ANCHORS.length - 1];
  for (let i = 0; i + 1 < ANCHORS.length; i++) {
    if (w >= ANCHORS[i].w && w <= ANCHORS[i + 1].w) { lo = ANCHORS[i]; hi = ANCHORS[i + 1]; break; }
  }
  const t = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  return lo[key] + (hi[key] - lo[key]) * t;
}

// ---------- ② 基础食物：logistic，k = 0.25 ----------
const FOOD_K = 0.25, FOOD_X0 = 10, FOOD_L = 420;
const food = (w) => FOOD_L / (1 + Math.exp(-FOOD_K * (w - FOOD_X0)));

// ---------- ③ 住宅：饱和型指数 ----------
// D(w) = C - (C-20)exp(-b(w-5))
// 联立 D(10)=148、D(20)=260 数值解 C、b
let HOUSING = null;
{
  let best = null;
  for (let C = 150; C <= 4000; C += 0.01) {
    const r = (C - 148) / (C - 20);
    if (r <= 0 || r >= 1) continue;
    const b = -Math.log(r) / 5;
    const d20 = C - (C - 20) * Math.exp(-b * 15);
    const err = Math.abs(d20 - 260);
    if (best === null || err < best.err) best = { C, b, err };
  }
  HOUSING = best;
}
const housing = (w) => HOUSING.C - (HOUSING.C - 20) * Math.exp(-HOUSING.b * (w - 5));

console.log('=== §6.3 劳动力购买表：最终离散补全（四组）===\n');
console.log('单位：每 100k 人口的标准消费组单位数\n');
console.log('基础食物：D(w) = 420 / (1 + exp(-0.25(w-10)))，拐点 = 财富 10');
console.log(`住宅    ：D(w) = ${HOUSING.C.toFixed(2)} - (${HOUSING.C.toFixed(2)} - 20)·exp(-${HOUSING.b.toFixed(6)}(w-5))`);
console.log(`          反解自 D(5)=20、D(10)=148、D(20)=260（拟合残差 ${HOUSING.err.toExponential(2)}）`);
console.log('简朴衣物 / 标准衣物：沿用契约的档间线性插值\n');

const rows = [];
console.log('财富档  简朴衣物  基础食物  标准衣物   住宅   ┃ 契约原表(简/食/标/住)');
for (let w = 5; w <= 20; w++) {
  const plain = linear(w, 'plain');
  const f = food(w);
  const std = linear(w, 'std');
  const h = housing(w);
  const r = {
    w,
    plain: Math.round(plain),
    food: Math.round(f),
    std: Math.round(std),
    housing: Math.round(h),
    exact: { plain, food: f, std, housing: h },
  };
  rows.push(r);
  const oPlain = linear(w, 'plain'), oFood = 210, oStd = linear(w, 'std');
  const oHous = w <= 10 ? 20 + (74 - 20) * (w - 5) / 5 : 74 + (130 - 74) * (w - 10) / 10;
  console.log(
    String(w).padStart(5) +
    String(r.plain).padStart(10) + String(r.food).padStart(10) +
    String(r.std).padStart(10) + String(r.housing).padStart(7) +
    '   ┃ ' + [oPlain, oFood, oStd, oHous].map((x) => x.toFixed(0).padStart(5)).join(' '),
  );
}

console.log('\n--- 校验 ---');
{
  // 基础食物拐点与单调性
  let mono = true, maxAt = 5, maxS = -Infinity;
  for (let w = 6; w <= 20; w++) if (food(w) <= food(w - 1)) mono = false;
  for (let w = 5; w <= 20; w++) {
    const e = Math.exp(-FOOD_K * (w - FOOD_X0));
    const s = FOOD_L * FOOD_K * e / Math.pow(1 + e, 2);
    if (s > maxS) { maxS = s; maxAt = w; }
  }
  console.log(`  基础食物：单调=${mono ? '✅' : '❌'}  拐点=财富 ${maxAt}${maxAt === 10 ? ' ✅' : ' ❌'}  D(10)=${food(10).toFixed(1)}${Math.abs(food(10) - 210) < 1e-9 ? ' ✅（=210 锚点）' : ' ❌'}`);

  // 住宅单调性与两倍点
  let mono2 = true;
  for (let w = 6; w <= 20; w++) if (housing(w) <= housing(w - 1)) mono2 = false;
  console.log(`  住宅    ：单调=${mono2 ? '✅' : '❌'}  D(5)=${housing(5).toFixed(2)}  D(10)=${housing(10).toFixed(2)}${Math.abs(housing(10) - 148) < 0.1 ? ' ✅（=74×2）' : ' ❌'}  D(20)=${housing(20).toFixed(2)}${Math.abs(housing(20) - 260) < 0.1 ? ' ✅' : ' ❌'}`);
  console.log(`            饱和值 C = ${HOUSING.C.toFixed(2)}（曲线永不超过它）`);

  // 每档增量
  console.log('\n  住宅每档增量（验证"先增后减"的饱和形态）：');
  const inc = [];
  for (let w = 6; w <= 20; w++) inc.push((housing(w) - housing(w - 1)).toFixed(1));
  console.log('    ' + inc.join(', '));
  const incN = inc.map(Number);
  const peak = incN.indexOf(Math.max(...incN)) + 6;
  console.log(`    增量峰值出现在财富 ${peak}；峰值 ${Math.max(...incN).toFixed(1)} → 末档 ${incN[incN.length - 1].toFixed(1)}`);

  console.log('\n  基础食物每档增量：');
  const incF = [];
  for (let w = 6; w <= 20; w++) incF.push((food(w) - food(w - 1)).toFixed(1));
  console.log('    ' + incF.join(', '));
}

console.log('\n--- 汇总：四组需求随财富档的形态 ---');
console.log('  消费组      财富5    财富10   财富20    形态');
console.log(`  简朴衣物    ${linear(5, 'plain').toFixed(0).padStart(6)}  ${linear(10, 'plain').toFixed(0).padStart(7)}  ${linear(20, 'plain').toFixed(0).padStart(7)}    先平后降（契约原样）`);
console.log(`  基础食物    ${food(5).toFixed(0).padStart(6)}  ${food(10).toFixed(0).padStart(7)}  ${food(20).toFixed(0).padStart(7)}    S 形 logistic，拐点 10`);
console.log(`  标准衣物    ${linear(5, 'std').toFixed(0).padStart(6)}  ${linear(10, 'std').toFixed(0).padStart(7)}  ${linear(20, 'std').toFixed(0).padStart(7)}    递增（契约原样）`);
console.log(`  住宅        ${housing(5).toFixed(0).padStart(6)}  ${housing(10).toFixed(0).padStart(7)}  ${housing(20).toFixed(0).padStart(7)}    饱和型指数`);

console.log('\n--- 相对契约原表的变化幅度 ---');
{
  const sum = (r) => r.plain + r.food + r.std + r.housing;
  console.log('  财富档   原表合计   新表合计    变化');
  for (const r of rows) {
    const oPlain = linear(r.w, 'plain'), oFood = 210, oStd = linear(r.w, 'std');
    const oHous = r.w <= 10 ? 20 + (74 - 20) * (r.w - 5) / 5 : 74 + (130 - 74) * (r.w - 10) / 10;
    const oSum = oPlain + oFood + oStd + oHous;
    const nSum = sum(r);
    console.log(
      String(r.w).padStart(7) + oSum.toFixed(0).padStart(11) + nSum.toFixed(0).padStart(11) +
      (((nSum - oSum) / oSum) * 100).toFixed(1).padStart(9) + '%',
    );
  }
}

console.log('\n--- 对 §6.5 人口动态的影响（必需品 = 简朴衣物 + 基础食物）---');
{
  const nec = (w, useNew) => {
    const p = linear(w, 'plain');
    const f = useNew ? food(w) : 210;
    return p + f;
  };
  console.log('  财富档   必需品(原)  必需品(新)   变化');
  for (const w of [5, 7, 10, 15, 20]) {
    const o = nec(w, false), n = nec(w, true);
    console.log(String(w).padStart(7) + o.toFixed(0).padStart(12) + n.toFixed(0).padStart(12) +
      (((n - o) / o) * 100).toFixed(1).padStart(9) + '%');
  }
  console.log('  ⇒ 低财富档必需品需求下降（财富 5：249 → 133，−47%），');
  console.log('     满足度因此上升、人口增长加快；高财富档必需品上升，增长受抑。');
}

fs.writeFileSync('out/demand_table_final.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/demand_table_final.txt');
