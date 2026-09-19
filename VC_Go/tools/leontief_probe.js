// 零利润价（Leontief 价格方程）探针
// 目的：用 §3.3 投入产出表 + §5 工资表反推每种商品的零利润价 P_cost，
//       并检查 §3.1 初始价与零利润价的比值是否一致。
// 方程：p = Aᵀp + l  ⇒  p = (I − Aᵀ)⁻¹ l
// 带统一加成 m：p = ((1−m)I − Aᵀ)⁻¹ l  （保证每种建筑利润率恰好为 m）
// 运行：node VC_Go/tools/leontief_probe.js

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const REC = [
  // [产出品下标, 每周期产出量, [[投入品下标, 投入量], ...]]
  [0, 50, []],
  [1, 45, [[0, 40]]],
  [2, 45, []],
  [3, 100, [[2, 60]]],
  [4, 30, [[2, 25]]],
  [5, 60, [[8, 15], [5, 15]]],
  [6, 60, [[8, 15], [5, 15]]],
  [7, 90, [[6, 60], [5, 30]]],
  [8, 80, [[7, 20]]],
  [9, 60, [[7, 5], [8, 5]]],
  [10, 15, [[7, 25], [6, 25], [8, 20]]],
];
const DOC = [2400, 4000, 5000, 12000, 40000, 4000, 4000, 8000, 4000, 1600, 24000];
const WAGE = 5000 * 6.75; // 5,000 人 × 平均工资 6.75 元 = 33,750 元/级/周期
const n = G.length;

const A = Array.from({ length: n }, () => new Array(n).fill(0));
const l = new Array(n).fill(0);
for (const [out, q, inp] of REC) {
  l[out] = WAGE / q;
  for (const [i, qty] of inp) A[i][out] = qty / q;
}

function solve(M, b) {
  const m = b.length;
  const a = M.map((r, i) => r.concat([b[i]]));
  for (let c = 0; c < m; c++) {
    let p = c;
    for (let r = c + 1; r < m; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
    const t = a[c]; a[c] = a[p]; a[p] = t;
    for (let r = 0; r < m; r++) {
      if (r === c) continue;
      const f = a[r][c] / a[c][c];
      if (!f) continue;
      for (let k = c; k <= m; k++) a[r][k] -= f * a[c][k];
    }
  }
  return a.map((r, i) => r[m] / a[i][i]);
}

function priceAt(markup) {
  const M = Array.from({ length: n }, (_, i) =>
    Array.from({ length: n }, (_, j) => (i === j ? 1 - markup : 0) - A[j][i]),
  );
  return solve(M, l);
}

function spectralRadius() {
  let v = new Array(n).fill(1 / Math.sqrt(n)), r = 0;
  for (let it = 0; it < 500; it++) {
    const w = new Array(n).fill(0);
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) w[i] += A[i][j] * v[j];
    r = Math.hypot(...w);
    v = w.map((x) => x / r);
  }
  return r;
}

const pc = priceAt(0);      // 零利润价
const p20 = priceAt(1 / 6); // 利润率恰为 20% 的价格（参数 m 满足 m/(1-m)=0.2）

console.log('A 的谱半径 =', spectralRadius().toFixed(4), '（必须 < 1 才存在正的零利润价）');
console.log('');
console.log('商品          零利润价    20%利润率价   开局价(1.2x)   文档初始价   文档价/零利润价');
for (let i = 0; i < n; i++) {
  console.log(
    '  ' + G[i].padEnd(6, '　'),
    String(Math.round(pc[i])).padStart(8),
    String(Math.round(p20[i])).padStart(12),
    String(Math.round(pc[i] * 1.2)).padStart(13),
    String(DOC[i]).padStart(12),
    (DOC[i] / pc[i]).toFixed(2).padStart(14),
  );
}

console.log('');
console.log('校验：按 20% 利润率价定价时，各建筑实际利润率');
for (const [out, q, inp] of REC) {
  const rev = q * p20[out];
  const cost = inp.reduce((s, [i, qty]) => s + qty * p20[i], 0) + WAGE;
  console.log('  ' + G[out].padEnd(6, '　'), (((rev - cost) / cost) * 100).toFixed(2) + '%');
}
console.log('');
console.log('开局价下各建筑利润率（P_init = 1.2 × P_cost，投入品同样按开局价计）');
{
  const init = pc.map((x) => x * 1.2);
  for (const [out, q, inp] of REC) {
    const rev = q * init[out];
    const cost = inp.reduce((s, [i, qty]) => s + qty * init[i], 0) + WAGE;
    console.log('  ' + G[out].padEnd(6, '　'), (((rev - cost) / cost) * 100).toFixed(2) + '%');
  }
}

console.log('');
console.log('对照：若把文档初始价整体缩放到达零利润（统一乘子）');
const ratios = DOC.map((d, i) => d / pc[i]);
console.log('  比值范围 =', Math.min(...ratios).toFixed(2), '~', Math.max(...ratios).toFixed(2));
console.log('  ⇒ 比值不一致 ⇒ 单靠整体放大产量无法让 11 种商品同时零利润。');
