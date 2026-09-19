// VC_Go/tools/food_s_curve_probe.js —— 把 §6.3 的「基础食物」改为 S 形曲线的候选参数核算（rev 2）
//
// 需求：财富水平对食物需求构成 S 形增加，且在财富 10 处增长最大（拐点）。
//
// 模型：logistic  D(w) = L / (1 + exp(-k(w - x0)))
//   x0 = 10（用户指定：拐点处增长最大）
//   D(10) = 210 ⇒ L / (1 + exp(0)) = L/2 = 210 ⇒ L = 420
//
// 与契约原表的冲突（必须裁决）：
//   契约 §6.3 原表：财富 5 → 210、10 → 210、20 → 210（恒定）
//   S 形要求单调递增 ⇒ 财富 5 必须低于 210、财富 20 必须高于 210
//   本脚本保留 D(10) = 210 这个关键锚点，改动 5 与 20 两档 —— 属修契约。
//
// 运行：node VC_Go/tools/food_s_curve_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const X0 = 10;      // 拐点
const ANCHOR = 210; // 财富 10 处的取值（契约锚点，保留）
const L = 2 * ANCHOR;

const D = (w, k) => L / (1 + Math.exp(-k * (w - X0)));
const S = (w, k) => { const e = Math.exp(-k * (w - X0)); return (L * k * e) / Math.pow(1 + e, 2); };

/** 用严格定点迭代校验：单调性、拐点位置、端点比值。 */
function audit(k) {
  let mono = true, prev = -Infinity, maxAt = 5, maxS = -Infinity;
  for (let w = 5; w <= 20; w++) {
    const d = D(w, k);
    if (d <= prev) mono = false;
    prev = d;
    const s = S(w, k);
    if (s > maxS) { maxS = s; maxAt = w; }
  }
  return { mono, maxAt, maxS, ratio: D(20, k) / D(5, k), d5: D(5, k), d20: D(20, k) };
}

console.log('=== 「基础食物」S 形曲线核算（rev 2）===\n');
console.log(`模型：D(w) = ${L} / (1 + exp(-k(w - 10)))`);
console.log('约束：拐点固定在财富 10，D(10) = 210（保留契约锚点）\n');

console.log('--- k 的取值对形状的影响 ---\n');
console.log('   k      D(5)     D(10)    D(20)   D(20)/D(5)  拐点斜率  单调  拐点位置');
for (const k of [0.10, 0.15, 0.20, 0.25, 0.35, 0.45, 0.55]) {
  const a = audit(k);
  console.log(
    k.toFixed(2).padStart(6) + a.d5.toFixed(1).padStart(9) + ANCHOR.toFixed(1).padStart(9) +
    a.d20.toFixed(1).padStart(9) + a.ratio.toFixed(2).padStart(12) +
    a.maxS.toFixed(2).padStart(10) + (a.mono ? '   ✅' : '   ❌').padStart(6) +
    ('  财富' + a.maxAt).padStart(11),
  );
}

console.log('\n  说明：k 越大 → 财富 10 附近变化越剧烈、两端越平。');
console.log('        选定值需权衡两点：');
console.log('          · D(5) 过低 ⇒ 低财富人口的实物食物配给被大幅削减，是实质性改动；');
console.log('          · k 过小 ⇒ 曲线在 5–20 区间内接近直线，失去 S 形的意义。\n');

// 给出两个候选的完整表
for (const k of [0.15, 0.25]) {
  const a = audit(k);
  console.log(`--- 候选 k = ${k} 的完整档位表 ---\n`);
  console.log('财富档   S形取值    取整   与原表(210)之差   该档斜率');
  const rows = [];
  for (let w = 5; w <= 20; w++) {
    const v = D(w, k), r = Math.round(v);
    rows.push(r);
    console.log(
      String(w).padStart(5) + v.toFixed(2).padStart(11) + String(r).padStart(7) +
      ((r - ANCHOR >= 0 ? '+' : '') + (r - ANCHOR)).padStart(17) +
      S(w, k).toFixed(2).padStart(12),
    );
  }
  console.log(`  校验：单调=${a.mono ? '✅' : '❌'}  斜率最大处=财富 ${a.maxAt}${a.maxAt === 10 ? ' ✅' : ' ❌'}  D(20)/D(5)=${a.ratio.toFixed(2)}×`);
  console.log(`  取整后序列：${rows.join(', ')}\n`);
}

console.log('--- 与其余三个消费组的对照（本次只改基础食物）---\n');
console.log('  消费组      财富5    财富10   财富20   原形状');
console.log('  简朴衣物     39.0     41.0      0.0    先平后降');
console.log('  基础食物     ↓       210       ↑     本次改为 S 形（拐点 10）');
console.log('  标准衣物      0.0      7.0    122.0    递增');
console.log('  住宅         20.0     74.0    130.0    递增');

console.log('\n--- 改动的影响面 ---\n');
console.log('  ① §6.2 把"基础食物"组映射到谷物(1)与加工食品(1.5)，');
console.log('     食物需求上升会同时拉动这两种商品，而它们是最主要的农产品，');
console.log('     因此该改动会直接影响 §4.2 的耕地约束与谷物/加工食品的价格。');
console.log('  ② §6.5 的人口增长只由"简朴衣物 + 基础食物"的满足度决定，');
console.log('     食物需求曲线变化会直接改变人口动态：');
console.log('       D(5) 下调 ⇒ 低财富下满足度上升 ⇒ 人口增长更快；');
console.log('       D(20) 上调 ⇒ 高财富下满足度下降 ⇒ 人口增长受抑。');
console.log('  ③ 原表下食物需求完全刚性（恒 210），福利金"提高财富档"对食物毫无拉动；');
console.log('     改为 S 形后这一渠道才成立 —— 这是本次修改的核心收益。');

console.log('\n=== 待裁决项（本脚本不落表）===\n');
console.log('  ① k 取值：本脚本建议 0.15（温和）或 0.25（明显）。');
console.log('     k=0.15 → D(5)=134.7、D(20)=343.4（2.55×），D(5) 仍保留原值的 64%；');
console.log('     k=0.25 → D(5)= 93.5、D(20)=388.1（4.15×），D(5) 降至原值的 45%。');
console.log('  ② 是否保留 D(10) = 210？本方案保留（L=420 由此反解）。');
console.log('  ③ 取值是否取整到整数？（契约原表都是整数）');
console.log('  ④ 是否同步调整 §6.2 的使用价值（加工食品 1 单位 = 1.5 单位基础食物）？');
console.log('     只改需求量表而不动使用价值表，加工食品的派生需求会被同比放大。');
console.log('  ⑤ 是否把同一 S 形思路推广到住宅（当前 20→130 是线性递增）？');

fs.writeFileSync('out/food_s_curve_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/food_s_curve_probe.txt');
