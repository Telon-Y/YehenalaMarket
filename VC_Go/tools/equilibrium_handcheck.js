// 手工推演：按契约 §6.3 的**离散整数表**（结算口径）复算 10m 人口的平衡等级。
// 独立于 Go 代码，不读任何源文件，只把表值抄进来做定点迭代。
// 运行：node VC_Go/tools/equilibrium_handcheck.js

const pop = 10_000_000;
const shares = [0.75, 0.20, 0.05];
const wages = [5, 10, 20];

// §6.3 离散表（每 10 万人），列序 = [简朴衣物, 基础食物, 标准衣物, 住宅]
const TABLE = [
  [39, 94, 0, 20],    // 5
  [39, 113, 1, 52],   // 6
  [40, 135, 3, 80],   // 7
  [40, 159, 4, 105],  // 8
  [41, 184, 6, 128],  // 9
  [41, 210, 7, 148],  // 10
  [37, 236, 19, 166], // 11
  [33, 261, 30, 182], // 12
  [29, 285, 42, 196], // 13
  [25, 307, 53, 209], // 14
  [21, 326, 65, 220], // 15
  [16, 343, 76, 230], // 16
  [12, 358, 88, 239], // 17
  [8, 370, 99, 247],  // 18
  [4, 380, 111, 254], // 19
  [0, 388, 122, 260], // 20
];
const GROUPS = [
  { name: '简朴衣物', uses: { 2: 1, 3: 1 } },
  { name: '基础食物', uses: { 0: 1, 1: 1.5 } },
  { name: '标准衣物', uses: { 3: 1, 4: 1 } },
  { name: '住宅', uses: { 9: 1 } },
];
const NAMES = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const RECIPE = [
  { q: 50, in: {} },                        // 0 谷物农场
  { q: 45, in: { 0: 40 } },                 // 1 加工食品厂
  { q: 45, in: {} },                        // 2 棉花种植园
  { q: 100, in: { 2: 60 } },                // 3 服装厂
  { q: 30, in: { 2: 25 } },                 // 4 高档服装厂
  { q: 60, in: { 8: 15, 5: 15 } },          // 5 煤矿
  { q: 60, in: { 8: 15, 5: 15 } },          // 6 铁矿
  { q: 90, in: { 6: 60, 5: 30 } },          // 7 炼钢厂
  { q: 80, in: { 7: 20 } },                 // 8 工具厂
  { q: 60, in: { 7: 5, 8: 5 } },            // 9 住房
  { q: 15, in: { 7: 25, 6: 25, 8: 20 } },   // 10 建造部门
];

const K = 1.04; // 三表联合标定系数（标定程序解出）

// 配方投入的键在对象里必然是字符串，统一转成数字下标再查价格表——
// 否则 PCOST["0"] === undefined，会把 RHS 算成 NaN。
const inputs = (i) => Object.entries(RECIPE[i].in).map(([g, q]) => [Number(g), q]);

// ---- A 最终需求 ----
const groupTotal = [0, 0, 0, 0];
for (let c = 0; c < 3; c++) {
  const row = TABLE[wages[c] - 5];
  const sub = pop * shares[c];
  for (let g = 0; g < 4; g++) groupTotal[g] += (row[g] * sub) / 100000 * K;
}
const FD = new Array(11).fill(0);
for (let g = 0; g < 4; g++) {
  const u = GROUPS[g].uses;
  const tot = Object.values(u).reduce((a, b) => a + b, 0);
  for (const [idx, w] of Object.entries(u)) FD[idx] += (groupTotal[g] * w) / tot;
}

console.log(`=== A 最终需求（离散表口径, k=${K}）===`);
console.log(`四组目标量: ${groupTotal.map((v) => v.toFixed(1)).join('  ')}`);
let fdVal = 0;
for (let i = 0; i < 11; i++) {
  fdVal += FD[i] * PCOST[i];
  console.log(`  ${NAMES[i].padEnd(8)} ${FD[i].toFixed(1).padStart(12)} × ${String(PCOST[i]).padEnd(6)} = ${(FD[i] * PCOST[i]).toFixed(0).padStart(14)}`);
}
console.log(`  最终需求价值(P_cost) = ${fdVal.toFixed(2)}`);

// ---- B 定点迭代 ----
let L = FD.map((f, i) => f / RECIPE[i].q);
const iters = [];
for (let it = 0; it < 200; it++) {
  const used = new Array(11).fill(0);
  for (let j = 0; j < 11; j++) for (const [g, q] of inputs(j)) used[g] += q * L[j];
  let maxd = 0;
  const next = L.slice();
  for (let i = 0; i < 11; i++) {
    const want = (FD[i] + used[i]) / RECIPE[i].q;
    maxd = Math.max(maxd, Math.abs(want - L[i]));
    next[i] = want;
  }
  L = next;
  if (it < 5 || maxd < 1e-11) iters.push([it + 1, maxd, L.slice()]);
  if (maxd < 1e-11) break;
}
console.log('\n=== B 定点迭代（前 5 轮 + 收敛轮）===');
for (const [n, d, v] of iters) {
  console.log(`  第${String(n).padStart(2)}轮 max|Δ|=${d.toExponential(2).padEnd(10)} ${v.map((x) => x.toFixed(2)).join(' ')}`);
}
const total = L.reduce((a, b) => a + b, 0);
console.log('\n=== 平衡等级 L* ===');
console.log(`${'建筑'.padEnd(10)} ${'起始等级'.padStart(10)} ${'平衡等级L*'.padStart(12)} ${'判定'.padStart(10)}`);
const N0 = 5;
let bad = 0;
for (let i = 0; i < 11; i++) {
  const ok = L[i] > N0;
  if (i < 10 && !ok) bad++;
  console.log(`${NAMES[i].padEnd(10)} ${(i === 10 ? 20 : N0).toFixed(1).padStart(10)} ${L[i].toFixed(2).padStart(12)} ${(ok ? 'OK' : '★未达标').padStart(10)}`);
}
console.log(`  合计 = ${total.toFixed(2)} 级`);

// ---- C 零利润价自校验 ----
// 配方给的是【每级】投入量，单级产出是 q。逐级写"收入 = 成本"：
//     q × P_cost = 33750（每级工资） + Σ 每级投入量 × P_cost
//     ⇒   P_cost = 33750/q + Σ 每级投入量 × P_cost / q
// 注意最后要除以 q —— 我第一版漏了这一步，把每级投入量当成了每单位产出投入量。
console.log('\n=== C 零利润价自校验：P_cost =? 33750/q + (Σ 每级投入量×P_cost)/q ===');
let maxRel = 0;
for (let i = 0; i < 11; i++) {
  const q = RECIPE[i].q;
  let inCost = 0;
  for (const [g, qty] of inputs(i)) inCost += qty * PCOST[g];
  const rhs = 33750 / q + inCost / q;
  const rel = Math.abs(rhs - PCOST[i]) / PCOST[i];
  maxRel = Math.max(maxRel, rel);
  console.log(`  ${NAMES[i].padEnd(8)} 表值 ${String(PCOST[i]).padStart(6)}   复算 ${rhs.toFixed(2).padStart(10)}   相差 ${(rhs - PCOST[i]).toFixed(2).padStart(8)}  (${(rel * 100).toFixed(4)}%)`);
}
console.log(`  最大相对偏差 = ${(maxRel * 100).toFixed(4)}%（P_cost 取整所致）`);

// ---- D 预算闭合 ----
const wageBill = total * 5000 * 6.75;
console.log('\n=== D 预算闭合 ===');
console.log(`  平衡等级合计        = ${total.toFixed(2).padStart(14)}`);
console.log(`  工资总额            = ${wageBill.toFixed(2).padStart(14)}`);
console.log(`  最终需求价值(P_cost) = ${fdVal.toFixed(2).padStart(14)}`);
console.log(`  工资/需求           = ${(wageBill / fdVal).toFixed(6).padStart(14)}`);
const t = 0.10;
console.log(`  税后可购价值(÷1.1)   = ${(wageBill / (1 + t)).toFixed(2)}`);
console.log(`  税后覆盖率          = ${(wageBill / (1 + t) / fdVal).toFixed(6)}  ⇒ 缺口 ${((1 - wageBill / (1 + t) / fdVal) * 100).toFixed(4)}%`);

console.log(`\n结论：起始等级 5 严格小于全部 10 种生产建筑的 L* —— ${bad === 0 ? '成立' : '不成立'}`);

