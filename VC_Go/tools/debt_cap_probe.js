// VC_Go/tools/debt_cap_probe.js —— 政府债务上限的核算
//
// 用户方案：政府现金池允许为负（欠债），债务上限 = 全国固定资产的两倍，
//           其中「全国固定资产 = 建造力 × 建造力价格」。
//
// 本脚本核算该定义的约束力：不同建造部门规模下，上限是多少、能撑多久。
// 目的是在写入契约前把数量级摆清楚——因为上限的大小直接决定 G2 采购能否持续。
//
// 运行：node VC_Go/tools/debt_cap_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

// 契约参数（不得改动）
const POWER_QTY_PER_LEVEL = 15;      // §3.3 建造部门单级产出
const POWER_BUILD_COST = 100;        // §3.2 建造部门建造成本（建造力）
const POWER_CAP = 1000;              // §4.2 建造部门等级上限
const POWER_PCOST = 7250;            // §3.1 建造力零利润价
const POWER_PINIT = 11917;           // §3.1 建造力开局价

console.log('=== 政府债务上限核算 ===\n');
console.log('定义：债务上限 = 2 × (建造力产出 × 建造力价格)');
console.log(`参数：建造部门单级产出 ${POWER_QTY_PER_LEVEL}、单价 P_cost = ${POWER_PCOST}、等级上限 ${POWER_CAP}\n`);

console.log('--- 一、「建造力」取【当期产出】的口径 ---\n');
console.log('建造部门等级   当期产出   资产基数(P_cost)   资产基数(P_init)   债务上限(P_cost)   债务上限(P_init)');
for (const lv of [1, 5, 20, 50, 100, 300, 500, 1000]) {
  const out = lv * POWER_QTY_PER_LEVEL;
  const aC = out * POWER_PCOST;
  const aI = out * POWER_PINIT;
  console.log(
    String(lv).padStart(12) + String(out).padStart(11) +
    Math.round(aC).toLocaleString().padStart(19) + Math.round(aI).toLocaleString().padStart(19) +
    Math.round(2 * aC).toLocaleString().padStart(19) + Math.round(2 * aI).toLocaleString().padStart(19),
  );
}
console.log('\n  说明：初始 20 级建造部门 → 当期产出 300 建造力 → 资产基数 217.5 万 → 上限 435 万。');
console.log('       若建造部门扩到上限 1000 级 → 上限 2.175 亿。');

console.log('\n--- 二、「建造力」取【建造成本】的口径（对照）---\n');
console.log('建造部门等级   建造成本合计   资产基数(P_cost)   债务上限(P_cost)');
for (const lv of [20, 100, 1000]) {
  const costQty = lv * POWER_BUILD_COST;
  const aC = costQty * POWER_PCOST;
  console.log(
    String(lv).padStart(12) + String(costQty).padStart(14) +
    Math.round(aC).toLocaleString().padStart(19) + Math.round(2 * aC).toLocaleString().padStart(17),
  );
}
console.log('\n  说明：该口径把"每级的建造成本"当作建造力存量，量级比上一口径大 6.67 倍');
console.log(`       （因单级建造成本 ${POWER_BUILD_COST} ÷ 单级产出 ${POWER_QTY_PER_LEVEL} = ${(POWER_BUILD_COST / POWER_QTY_PER_LEVEL).toFixed(2)}）。`);

console.log('\n--- 三、上限能撑多久（以实跑观测到的政府净现金流为准）---\n');
// 实跑数据（out/gosim_fixed.txt）：
//   tick 1：税收 2,705,165，政府经营净额 −3,114,187 → 净额 −409,022/tick
const taxPerTick = 2705165;
const govOperPerTick = -3114187;
const netPerTick = taxPerTick + govOperPerTick;
console.log(`  实测 tick 1：税收 ${taxPerTick.toLocaleString()} + 经营净额 ${govOperPerTick.toLocaleString()} = 净额 ${netPerTick.toLocaleString()} 元/tick`);
console.log('  政府净现金流为负时，债务以该速度累积：\n');
console.log('  建造部门等级   债务上限(P_cost)    净额为负时可撑 tick 数');
for (const lv of [20, 50, 100, 300, 1000]) {
  const cap = 2 * lv * POWER_QTY_PER_LEVEL * POWER_PCOST;
  const ticks = netPerTick < 0 ? cap / -netPerTick : Infinity;
  console.log(
    String(lv).padStart(12) + Math.round(cap).toLocaleString().padStart(19) +
    (isFinite(ticks) ? Math.round(ticks).toLocaleString() : '∞').padStart(24),
  );
}
console.log('\n  ⇒ 关键结论：上限与建造部门规模成正比，而政府净亏损与整个经济规模成正比。');
console.log('     由于建造部门只占经济的一小部分，靠债务上限"续命"的 tick 数有限，');
console.log('     但相比"现金池不得为负"（可撑 0 tick、采购立即归零），它足以让 G2 在');
console.log('     政府暂时亏损时仍能运转——这正是本方案的用意。');

console.log('\n--- 四、债务利息问题（本方案未定义）---\n');
console.log('  契约 §1.3 明确把"完整货币、银行、利率"列为 1.2 版本的非目标。');
console.log('  故本版债务【不计息】，只设上限。若日后要计息，需在 1.2 的 Bank 接口上做。');

console.log('\n--- 五、破产处置（本方案未定义）---\n');
console.log('  债务触及上限后怎么办，方案未指定。本工程采用【硬约束】语义：');
console.log('  达到上限时政府无法再增加债务，即采购建造力被压缩到 0，');
console.log('  同时政府项目的付款也被阻止（否则会继续推高债务）。');
console.log('  这是一个保守选择——它让"触及上限"表现为 G2 停摆而非债务违约。');

console.log('\n=== 写入契约时需明确的四项 ===\n');
console.log('  ① 资产基数的口径：建造力【当期产出】还是【建造成本存量】？');
console.log('     本脚本默认前者（"建造力 × 建造力价格"的字面读法）。两者相差 6.67 倍。');
console.log('  ② 建造力价格取 P_cost（7250）还是当期市价？');
console.log('     本脚本默认 P_cost（稳定、可预期）；若取当期市价，债务上限会随价格波动。');
console.log('  ③ 上限是"当前时点"的还是"历史峰值"的？本脚本按当前时点。');
console.log('  ④ 触及上限后的行为：停摆（本工程选择）还是违约/重组？');

fs.writeFileSync('out/debt_cap_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/debt_cap_probe.txt');
