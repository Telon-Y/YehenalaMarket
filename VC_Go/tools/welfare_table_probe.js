// VC_Go/tools/welfare_table_probe.js —— §6.3 财富档表的离散补全与福利金可行性核算
//
// 契约 §6.3 只给了 3 个锚点（财富 5 / 10 / 20）：
//
//	财富等级   简朴衣物   基础食物   标准衣物   住宅
//	   5         39        210         0        20
//	  10         41        210         7        74
//	  20          0        210       122       130
//
// 且 §6.3 规定"财富等级由平均工资插值确定"，档间线性插值。
// 要让政府按档位精准发放福利金，必须把 5–20 全部整数档补全成离散表。
//
// 本脚本做两件事：
//   ① 按契约的线性插值规则补全 5–20 共 16 个整数档位；
//   ② 核算"发放福利金 → 提升财富档 → 刺激消费"的财政可行性。
//
// 运行：node VC_Go/tools/welfare_table_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

// 契约 §6.3 的原表（3 个锚点）
const ANCHORS = [
  { w: 5, d: [39, 210, 0, 20] },
  { w: 10, d: [41, 210, 7, 74] },
  { w: 20, d: [0, 210, 122, 130] },
];
const GROUPS = ['简朴衣物', '基础食物', '标准衣物', '住宅'];

function interpTable(w) {
  let lo = ANCHORS[0], hi = ANCHORS[ANCHORS.length - 1];
  for (let i = 0; i + 1 < ANCHORS.length; i++) {
    if (w >= ANCHORS[i].w && w <= ANCHORS[i + 1].w) { lo = ANCHORS[i]; hi = ANCHORS[i + 1]; break; }
  }
  const t = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  return {
    w,
    d: lo.d.map((x, k) => x + (hi.d[k] - x) * t),
    exact: lo.w === w || hi.w === w,
  };
}

console.log('=== §6.3 财富档表的离散补全（5–20）===\n');
console.log('补全规则：契约 §6.3 的"财富等级由平均工资插值确定"+ 档间线性插值。');
console.log('标注 ● 的是契约原表直接给出的锚点，其余为按同一规则补全的档位。\n');
console.log('财富档   ' + GROUPS.map((g) => g.padStart(9)).join('') + '   合计');
const table = [];
for (let w = 5; w <= 20; w++) {
  const r = interpTable(w);
  table.push(r);
  const sum = r.d.reduce((a, b) => a + b, 0);
  console.log(
    String(w).padStart(5) + (r.exact ? ' ●' : '  ') +
    r.d.map((x) => x.toFixed(1).padStart(9)).join('') +
    sum.toFixed(1).padStart(8),
  );
}

console.log('\n--- 校验：补全表在锚点处必须与契约原表逐项相等 ---');
let ok = true;
for (const a of ANCHORS) {
  const r = interpTable(a.w);
  for (let k = 0; k < 4; k++) {
    if (Math.abs(r.d[k] - a.d[k]) > 1e-9) {
      console.log(`  ❌ 财富 ${a.w} 的 ${GROUPS[k]}: 补全值 ${r.d[k]} ≠ 契约值 ${a.d[k]}`);
      ok = false;
    }
  }
}
console.log(ok ? '  ✅ 3 个锚点全部吻合' : '  ❌ 存在偏差');

console.log('\n--- 单调性检查（福利金把人口推向更高财富档时，各组需求如何变化）---');
for (let k = 0; k < 4; k++) {
  let inc = 0, dec = 0, flat = 0;
  for (let w = 6; w <= 20; w++) {
    const a = interpTable(w - 1).d[k], b = interpTable(w).d[k];
    if (b > a + 1e-9) inc++; else if (b < a - 1e-9) dec++; else flat++;
  }
  const dir = dec === 0 ? '单调递增' : inc === 0 ? '单调递减' : '非单调';
  console.log(`  ${GROUPS[k].padEnd(8)} 递增 ${String(inc).padStart(2)} 档 / 递减 ${String(dec).padStart(2)} 档 / 持平 ${String(flat).padStart(2)} 档   → ${dir}`);
}
console.log('  合计需求随财富档的变化：');
for (let w = 5; w <= 20; w++) {
  const s = interpTable(w).d.reduce((a, b) => a + b, 0);
  console.log(`    财富 ${String(w).padStart(2)} → ${s.toFixed(1)}`);
}

console.log('\n=== 福利金刺激消费的财政可行性核算 ===\n');
console.log('机制：政府按就业人口发放福利金 → 居民收入上升 → 平均工资上升');
console.log('      → §6.3 的财富等级上升 → 各消费组需求量上升 → 消费支出上升。\n');

// 用契约的核心参数做核算（不改任何契约值，只做推导）
const WAGE_PER_LEVEL = 5000 * 6.75;      // §5 = 33,750
const TAX_RATE = 0.10;                   // G1
const GOV_POWER_PRICE = 7250;            // §3.1 建造力零利润价

// 假设：政府把福利金定为"每级就业人口每 tick 发放 x 元"
// 则平均工资变为 6.75 + x/5000 元
console.log('  设福利金为每级就业人口 x 元/tick（每级 5,000 人）。');
console.log('  平均工资 = 6.75 + x/5000，财富档 = clamp(平均工资, 5, 20)。\n');
console.log('  x(元/级)  平均工资   财富档   人均需求合计   相对基线');
const base = interpTable(6.75).d.reduce((a, b) => a + b, 0);
for (const x of [0, 3375, 6750, 13500, 33750, 67500]) {
  const wage = 6.75 + x / 5000;
  const tier = Math.min(20, Math.max(5, wage));
  const sum = interpTable(tier).d.reduce((a, b) => a + b, 0);
  console.log(
    String(x).padStart(10) + wage.toFixed(2).padStart(10) + tier.toFixed(2).padStart(9) +
    sum.toFixed(1).padStart(15) + ((sum / base - 1) * 100).toFixed(2).padStart(11) + '%',
  );
}
console.log(`  （基线：x=0 时平均工资 6.75 → 财富档 ${6.75}，人均需求合计 ${base.toFixed(1)}）`);

console.log('\n  关键观察：');
console.log('    · 基础食物恒为 210（三个锚点都相同），不随财富档变化；');
console.log('    · 简朴衣物从 41 降到 0，标准衣物从 7 升到 122 —— 是【替代】而非净增；');
console.log('    · 只有住宅（20 → 130）是净增；');
console.log('    · 因此"提高财富档"对总需求的拉动有限，且集中在住宅。\n');
{
  const s5 = interpTable(5).d.reduce((a, b) => a + b, 0);
  const s20 = interpTable(20).d.reduce((a, b) => a + b, 0);
  console.log(`    量化：财富档 5 → 20（跨满量程），人均需求合计仅从 ${s5.toFixed(1)} 变到 ${s20.toFixed(1)}，`);
  console.log(`          即 ${((s20 / s5 - 1) * 100).toFixed(2)}%。`);
}

console.log('\n--- 财政侧：福利金规模 vs 政府收入 ---');
console.log('  以 500 万人口、每级 5,000 人计：就业级数 L ≈ 人口/5000 = 1,000 级。');
console.log('  （这里的 L 是"就业总量"口径，用于估算福利金总额。）\n');
console.log('  x(元/级)  年化福利金总额      政府税收(估)     福利/税收');
// 税收粗估：以契约规模下的交易额为基数（见 gov_capital_accounting 的口径）
const POP = 5_000_000;
const L = POP / 5000;
const estTaxPerTick = 166_894_483;   // Go 版实跑在 tick 3000 的税收（out/gosim_contract.txt）
for (const x of [3375, 6750, 13500, 33750]) {
  const perTick = L * x;
  const perYear = perTick * 52;
  console.log(
    String(x).padStart(10) + Math.round(perYear).toLocaleString().padStart(18) +
    Math.round(estTaxPerTick * 52).toLocaleString().padStart(18) +
    (perYear / (estTaxPerTick * 52)).toFixed(2).padStart(12) + ' 倍',
  );
}
console.log('  （政府税收基线取 Go 版实跑 tick 3000 的值 166,894,483 元/tick）');

console.log('\n=== 判定 ===\n');
console.log('  ① 表可以离散补全：按契约 §6.3 的线性插值规则，5–20 共 16 档全部可算，');
console.log('     且 3 个锚点自动吻合，无需新增参数。');
console.log('  ② 福利金的刺激效果集中在住宅：基础食物恒为 210 不随财富档变化；');
console.log('     简朴衣物（41→0）与标准衣物（7→122）是替代关系而非净增；');
console.log('     只有住宅（20→130）是真净增。由于 §6.2 把"简朴衣物"组同时映射到');
console.log('     织物与服装、"标准衣物"组映射到服装与高档服装，服装需求存在重叠，');
console.log('     若按纯替代口径，实际净增主要来自住宅。');
{
  const s675 = interpTable(6.75).d.reduce((a, b) => a + b, 0);
  const s135 = interpTable(13.5).d.reduce((a, b) => a + b, 0);
  const s20 = interpTable(20).d.reduce((a, b) => a + b, 0);
  console.log(`  ③ 刺激幅度可量化：`);
  console.log(`       福利金使平均工资翻倍（6.75 → 13.50，财富档同步）→ 需求合计 ${s675.toFixed(1)} → ${s135.toFixed(1)}，+${((s135 / s675 - 1) * 100).toFixed(1)}%`);
  console.log(`       即便把财富档推到满量程 20（工资需 20 元/人）→ 需求合计 ${s20.toFixed(1)}，+${((s20 / s675 - 1) * 100).toFixed(1)}%`);
}
console.log('  ④ 财政侧可行：以 500 万人口、就业约 1,000 级计，');
console.log('       福利金 x = 33,750 元/级（相当于员工资翻倍）时，年化福利金为');
console.log('       17.55 亿元，仅为政府税收估算（86.79 亿元/年）的 0.20 倍。');
console.log('       ⇒ 财政不是约束；真正的约束是 §6.3 表本身的刺激弹性偏小。');
console.log('  ⑤ 结论：本表应作为【离散补全的数据结构】先落地；');
console.log('       福利金的发放规则（发放对象、单位、是否与就业挂钩）需另行裁决，');
console.log('       实现中福利金默认必须为 0，不得擅自设定。');

fs.writeFileSync('out/welfare_table_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/welfare_table_probe.txt');
