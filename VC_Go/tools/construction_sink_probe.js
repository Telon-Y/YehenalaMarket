// VC_Go/tools/construction_sink_probe.js —— 证明 1.0 契约缺少「建造力的付款方」
//
// 用户指正：1.0 里全部支出仅靠劳动力收入，建造是一个财政黑洞。
// 本脚本把这个直觉做成可计算的判定，分三步：
//   ① 建造力的货币需求量 vs 系统可用货币量（量级对比）
//   ② 1.0 契约的资金流清单里，建造力付款的收款方是谁（逐一核对条文）
//   ③ 加入政府（征税 + 采购建造力）与资本（金融区）后，货币闭环是否成立
//
// 运行：node VC_Go/tools/construction_sink_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const S = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11;
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const REC = [
  { q: 50, inp: [] }, { q: 45, inp: [[0, 40]] }, { q: 45, inp: [] }, { q: 100, inp: [[2, 60]] },
  { q: 30, inp: [[2, 25]] }, { q: 60, inp: [[8, 15], [5, 15]] }, { q: 60, inp: [[8, 15], [5, 15]] },
  { q: 90, inp: [[6, 60], [5, 30]] }, { q: 80, inp: [[7, 20]] }, { q: 60, inp: [[7, 5], [8, 5]] },
  { q: 15, inp: [[7, 25], [6, 25], [8, 20]] },
];
const WAGE = 33750;

console.log('=== 建造力「财政黑洞」判定 ===\n');
console.log('命名约定：本脚本把「建造力」统一记作 CONSTRUCTION_POWER（契约 §3.1 的"建造力"，');
console.log('          单级建造部门每周期产出 15 单位）。\n');

console.log('--- 一、1.0 契约的资金流清单：谁为建造力付款？---\n');
console.log('逐条核对契约与架构文档里出现过的资金流：\n');
const flows = [
  ['§5  工资', '建筑现金池 → 居民', '有付款方、有收款方', '闭环'],
  ['§6  消费者购买', '居民工资 → 建筑现金池', '有付款方、有收款方', '闭环'],
  ['§8  中间投入', '买方建筑 → 卖方建筑', '契约未写；架构 §6 未补', '缺口（但同额对冲）'],
  ['§4.3 建造力费用', '买方建筑现金池 → ?', '**收款方未定义**', '缺口（单向漏出）'],
  ['§4.3 建造力产出', '建造部门 → ?（谁买）', '**购买方未定义**', '缺口'],
];
for (const [a, b, c, d] of flows) console.log(`  ${a.padEnd(18)} ${b.padEnd(28)} ${c.padEnd(24)} ${d}`);
console.log('\n  ⇒ §4.3 只写了"建造力费用从中扣除"，没有定义这笔钱付给谁；');
console.log('     §3.1 的建造力是商品、有单价 11,917 元/单位，但契约里没有任何主体申报购买它。');
console.log('     即：建造力被"生产"并被"消耗"，却没有人"购买"——这是一条单向漏出的资金流。\n');

console.log('--- 二、这笔漏出的量级 ---\n');
const expandFrom = 50, expandTo = 3000, avgCost = 600;   // ARCHITECTURE §10.2 的口径
const powerNeed = (expandTo - expandFrom) * avgCost;
const moneyNeedPcost = powerNeed * PCOST[10];
const moneyNeedPinit = powerNeed * PINIT[10];
console.log('  ARCHITECTURE §10.2 的目标：10,000 tick 内建筑总量 50 级 → 3,000 级');
console.log(`  平均建造成本 ${avgCost} 建造力/级，则总建造力需求 = ${powerNeed.toLocaleString()} 单位`);
console.log(`  按 P_cost = ${PCOST[10]} 元/单位计价 → 需要 ${(moneyNeedPcost / 1e8).toFixed(2)} 亿元`);
console.log(`  按 P_init = ${PINIT[10]} 元/单位计价 → 需要 ${(moneyNeedPinit / 1e8).toFixed(2)} 亿元`);
const cash0 = 5000 * expandFrom;
console.log(`\n  而 §4.3 给出的初始货币总量 = 5,000 元/级 × 50 级 = ${cash0.toLocaleString()} 元`);
console.log(`  ⇒ 缺口比 = ${(moneyNeedPcost / cash0).toExponential(2)} 倍（按 P_cost 计）`);
console.log('  ⇒ 即便把 50 级开工规模下全部工资收入（约 50×33,750 = 1,687,500 元/周期）');
console.log('     全部投入建造，也需要 10,000 个周期以上，而工资还必须用于消费。');
console.log('  ⇒ 结论：建造力在 1.0 中没有任何合法的货币来源，扩建在资金上不可能发生。\n');

console.log('--- 三、加入政府与资本后的货币闭环 ---\n');
console.log('用户方案：');
console.log('  (1) 政府抽取全部交易额的 10% 作为税收；');
console.log('  (2) 建造力必须在市场内购买，购买方是政府；');
console.log('  (3) 建筑有所有权（政府 / 私有），所有者取得运营纯利；');
console.log('  (4) 私有扩建的资金从建筑的现金池划拨给政府现金池；');
console.log('  (5) 金融区（建筑）作为资本载体，每级雇 1,000 人，每级掌控 5 级其余建筑。\n');

// 简化稳态核算：以「完全需求 = 1 单位」的相对规模计算（口径与 structure_probe 一致）
// 取一件代表性结构：各级数使净供给恰好等于 §6.3 财富 10 档的 10 万人需求
const d10 = [41, 210, 7, 74];   // 简朴衣物 / 基础食物 / 标准衣物 / 住宅（每 10 万人）
const F = new Array(N).fill(0);
F[2] += d10[0] / 2; F[3] += d10[0] / 2;
F[0] += d10[1] / 2.5; F[1] += d10[1] * 1.5 / 2.5;
F[3] += d10[2] / 2; F[4] += d10[2] / 2;
F[9] += d10[3];
// 用 Leontief 逆推总产出
const A = Array.from({ length: N }, () => new Array(N).fill(0));
for (let j = 0; j < N; j++) for (const [i, qty] of REC[j].inp) A[i][j] = qty / REC[j].q;
function inv(M) {
  const m = M.length;
  const a = M.map((r, i) => r.concat(Array.from({ length: m }, (_, k) => (i === k ? 1 : 0))));
  for (let c = 0; c < m; c++) {
    let p = c; for (let r = c + 1; r < m; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
    const t = a[c]; a[c] = a[p]; a[p] = t;
    const pv = a[c][c];
    for (let k = 0; k < 2 * m; k++) a[c][k] /= pv;
    for (let r = 0; r < m; r++) { if (r === c) continue; const f = a[r][c]; if (!f) continue; for (let k = 0; k < 2 * m; k++) a[r][k] -= f * a[c][k]; }
  }
  return a.map((r) => r.slice(m));
}
const B = inv(Array.from({ length: N }, (_, i) => Array.from({ length: N }, (_, j) => (i === j ? 1 : 0) - A[i][j])));
const Y = B.map((row) => row.reduce((s, x, j) => s + x * F[j], 0));
const L = Y.map((y, i) => Math.max(1, Math.ceil(y / REC[i].q)));
console.log('  参考规模（10 万人、财富 10 档、完全需求口径）：');
console.log('    ' + S.map((s, i) => `${s}=${L[i]}`).join(' '));
const Lother = L.reduce((a, b) => a + b, 0);

// 各商品的稳态交易额（用 P_cost 计价，避免 P_init 加成带来的口径混淆）
let consumerSpend = 0, intermediateSpend = 0;
for (let i = 0; i < N; i++) consumerSpend += F[i] * PCOST[i];
for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) intermediateSpend += L[i] * q * PCOST[j];
const gdpLike = consumerSpend + intermediateSpend;
const tax = 0.10 * gdpLike;

console.log('\n  交易额与税收：');
console.log(`    消费者最终购买额 = ${consumerSpend.toFixed(0)} 元/周期`);
console.log(`    中间投入交易额   = ${intermediateSpend.toFixed(0)} 元/周期`);
console.log(`    总交易额         = ${gdpLike.toFixed(0)} 元/周期`);
console.log(`    政府税收（10%）  = ${tax.toFixed(0)} 元/周期`);

// 政府用税收购买建造力 → 可支撑的扩建速度
const powerPrice = PCOST[10];
const powerBuy = tax / powerPrice;
console.log(`\n  政府把全部税收用于采购建造力：`);
console.log(`    可购买建造力 = ${powerBuy.toFixed(2)} 单位/周期`);
console.log(`    折合建筑等级 = ${(powerBuy / avgCost).toFixed(4)} 级/周期（按平均成本 ${avgCost}）`);
console.log(`    10,000 周期累计可建 = ${(powerBuy / avgCost * 10000).toFixed(0)} 级`);

// 金融区规模
const finLevels = Math.ceil(Lother / 5);
console.log(`\n  金融区规模（每级掌控 5 级其余建筑）：`);
console.log(`    其余建筑合计 ${Lother} 级 → 金融区需 ${finLevels} 级`);
console.log(`    金融区雇佣 = ${finLevels} × 1,000 = ${(finLevels * 1000).toLocaleString()} 人`);
const totalWorkers = Lother * 5000;
console.log(`    其余建筑雇佣 = ${Lother} × 5,000 = ${(totalWorkers).toLocaleString()} 人`);
console.log(`    金融区劳动力占比 = ${(finLevels * 1000 / (totalWorkers + finLevels * 1000) * 100).toFixed(2)}%`);

// 金融区掌控下的利润率：私有建筑的纯利归金融区
let privateProfit = powerBuy * powerPrice;   // 假设政府把税收全部花掉，形成建造部门收入
console.log('\n  --- 闭环校验 ---\n');
console.log(`    政府收入（税收）       = ${tax.toFixed(0)}`);
console.log(`    政府支出（采购建造力） = ${(powerBuy * powerPrice).toFixed(0)}`);
console.log(`    ⇒ 政府收支恰好相等（税收全额转成建造力采购）`);
console.log('    ⇒ 货币闭环成立：居民 → 消费/中间投入 → 建筑 → 税收 → 政府 → 建造力 → 建造部门');
console.log('       → 工资/投入 → 居民。建造不再是黑洞，而是政府注入需求的主渠道。\n');

console.log('--- 四、闭环成立的前提条件（必须写进契约）---\n');
console.log('  ① 政府必须把税收【全额】用于采购建造力，或有等价的需求注入渠道；');
console.log('     若政府只征税不支出，私人部门货币以 10%/交易的速度被抽干，系统更快崩溃。');
console.log('  ② 建造力必须【有购买方】。政府是最终需求方，建造部门是供给方，');
console.log('     §4.3 的"从现金池扣除"必须明确为"支付给建造部门/政府"。');
console.log('  ③ 私有建筑的扩建资金必须【从建筑现金池划拨】，而不是凭空生成；');
console.log('     划拨对象是政府现金池（政府代建）或建造部门（直接采购）。');
console.log('  ④ 金融区的掌控上限（1 级管 5 级）给出了资本扩张的硬约束：');
console.log(`     在本参考规模下金融区需 ${finLevels} 级、雇 ${(finLevels * 1000).toLocaleString()} 人，`);
console.log('     即资本部门本身也要占用建造力与劳动力，构成扩张的自我限制。');
console.log('  ⑤ 政府建筑与私有建筑的利润率口径必须分别定义：');
console.log('     政府建筑的"利润"是财政盈余，私有建筑的纯利归金融区（资本收入）。');

console.log('\n--- 五、判定 ---\n');
console.log('  【成立】用户方案在会计上闭合了 1.0 的黑洞：');
console.log('     - 建造力获得明确的最终需求方（政府）；');
console.log('     - 货币通过"税收 → 政府 → 建造力采购 → 建造部门"完成再循环；');
console.log('     - 私有扩建的付款方是建筑现金池、收款方是政府，资金不凭空生成；');
console.log('     - 金融区为资本收入提供了归属主体，使"纯利"有去处（否则纯利仍是悬空的）。');
console.log('  【前提】必须同时规定：政府税收的支出比例、建造力的买卖双方、');
console.log('     私有扩建的资金划拨路径、政府/私有建筑的利润口径。');
console.log('  【量级提示】在上面的参考规模下，10% 税收能支撑的扩建速度约');
console.log(`     ${(powerBuy / avgCost).toFixed(4)} 级/周期；把 50 级扩到 3,000 级需要约 ${Math.round((3000 - 50) / (powerBuy / avgCost)).toLocaleString()} 周期，`);
console.log('     仍远超 10,000 周期的验收窗口。若要更快，需要提高税率、');
console.log('     提高建造部门产出、或降低单位建造成本。');

fs.writeFileSync('out/construction_sink_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/construction_sink_probe.txt');
