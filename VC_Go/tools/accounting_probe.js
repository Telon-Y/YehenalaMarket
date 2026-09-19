// VC_Go/tools/accounting_probe.js —— 1.0 契约的货币闭环判定（纯静态核算，不依赖任何模拟）
//
// 问题：契约 §3.4 声称"开局时各建筑在实际成本基下的利润率恰好为 20%"，
//       同时 §5 规定"全部工资用于消费"、§7 规定 GDP = 消费者支出 + 现金池。
//       工资是消费者唯一的资金来源。问：这两个要求能否同时成立？
//
// 方法：在 §3.1 的 P_init 价格向量下，直接做封闭经济的资金流核算。
//       无需模拟、无需初始资本结构、无需任何动力学。
//
// 运行：node VC_Go/tools/accounting_probe.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const G = ['谷物', '加工食品', '织物', '服装', '高档服装', '煤', '铁', '钢', '工具', '住房', '建造力'];
const S = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11;
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const PINIT = [810, 1764, 900, 1053, 2250, 1465, 1465, 2208, 1169, 1013, 11917];
const REC = [
  { q: 50, inp: [] }, { q: 45, inp: [[0, 40]] }, { q: 45, inp: [] }, { q: 100, inp: [[2, 60]] },
  { q: 30, inp: [[2, 25]] }, { q: 60, inp: [[8, 15], [5, 15]] }, { q: 60, inp: [[8, 15], [5, 15]] },
  { q: 90, inp: [[6, 60], [5, 30]] }, { q: 80, inp: [[7, 20]] }, { q: 60, inp: [[7, 5], [8, 5]] },
  { q: 15, inp: [[7, 25], [6, 25], [8, 20]] },
];
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100];
const WAGE = 5000 * 6.75;

console.log('=== 1.0 契约的货币闭环判定 ===\n');
console.log('契约条款：');
console.log('  §5  每级建筑雇佣 5,000 人、平均工资 6.75 元 → 每级工资成本 33,750 元/周期');
console.log('  §5  「全部工资用于消费」');
console.log('  §7  GDP = 消费者最终购买商品的总支出 + 各建筑现金池期末总额');
console.log('  §3.4 开局价 P_init 使各建筑在「实际成本基」下利润率恰好为 20%');
console.log('  §4.3 每种建筑每级独立现金池，初始 5,000 元\n');

console.log('--- 一、单级资金流（价格 = §3.1 的 P_init）---\n');
console.log('建筑            单级收入     工资成本    中间投入成本   总成本     利润率(成本基)');
let revSum = 0, wageSum = 0, inputSum = 0;
for (let i = 0; i < N; i++) {
  const rev = REC[i].q * PINIT[i];
  let input = 0;
  for (const [j, qty] of REC[i].inp) input += qty * PINIT[j];
  const cost = WAGE + input;
  const margin = (rev - cost) / cost;
  revSum += rev; wageSum += WAGE; inputSum += input;
  console.log('  ' + S[i].padEnd(10) + rev.toFixed(0).padStart(12) + WAGE.toFixed(0).padStart(13)
    + input.toFixed(0).padStart(15) + cost.toFixed(0).padStart(12) + ((margin * 100).toFixed(2) + '%').padStart(15));
}
console.log('  ' + '合计（每 1 级各类建筑）'.padEnd(10) + revSum.toFixed(0).padStart(12) + wageSum.toFixed(0).padStart(13)
  + inputSum.toFixed(0).padStart(15) + (wageSum + inputSum).toFixed(0).padStart(12)
  + ((((revSum - wageSum - inputSum) / (wageSum + inputSum)) * 100).toFixed(2) + '%').padStart(15));
console.log('\n  ⇒ 11 种商品各自利润率都恰为 20%（契约 §3.4 的声明成立）。');

console.log('\n--- 二、封闭经济的资金流守恒 ---\n');
console.log('  消费者收入 = 总工资 = ' + wageSum.toFixed(0) + ' 元（每 1 级各类建筑）');
console.log('  消费者支出 ≤ 消费者收入（无外部注资、无信贷、§5 规定工资全额消费）');
console.log('  ⇒ 全社会总收入（= 各建筑销售额）= 消费者支出 ≤ 总工资 = ' + wageSum.toFixed(0));
console.log('  而 §3.4 要求：总收入 = 1.20 × 总成本 = 1.20 × ' + (wageSum + inputSum).toFixed(0) + ' = ' + (1.2 * (wageSum + inputSum)).toFixed(0));
console.log('\n  两者之比 = ' + (1.2 * (wageSum + inputSum) / wageSum).toFixed(4) + ' 倍');
console.log('  ⇒ 要求总收入是工资的 ' + (1.2 * (wageSum + inputSum) / wageSum).toFixed(2) + ' 倍，但消费者只有工资可花。');
console.log('  ⇒ 除非中间投入的支出（' + inputSum.toFixed(0) + ' 元）能作为收入回流并被再次支出，');
console.log('     否则 20% 的全局利润率与货币守恒不可能同时成立。\n');

console.log('--- 三、把中间投入的货币流也计入后，是否闭合？---\n');
console.log('  假设中间投入的付款也由建筑现金池支付，并回流为生产者收入：');
console.log('    建筑总收入 = 消费者支出 + 中间投入付款 = ' + wageSum.toFixed(0) + ' + ' + inputSum.toFixed(0) + ' = ' + (wageSum + inputSum).toFixed(0));
console.log('    建筑总成本 = 工资 + 中间投入 = ' + wageSum.toFixed(0) + ' + ' + inputSum.toFixed(0) + ' = ' + (wageSum + inputSum).toFixed(0));
console.log('  ⇒ 总收入 ≡ 总成本，全局总利润 ≡ 0，全局平均利润率 ≡ 0%。');
console.log('  ⇒ 这是会计恒等式（收入 = 支出），与价格、产量、参数全都无关。');
console.log('  ⇒ 因此「所有建筑同时获得正利润」在封闭经济中是不可能的：');
console.log('     正利润只能来自"某些建筑的支出少于其收入"，即必然是零和的。\n');

console.log('--- 四、推论：§3.4 的"统一 20% 利润率"隐含的外部资金需求 ---\n');
const need = 1.2 * (wageSum + inputSum) - wageSum;
console.log('  要使全局平均利润率 = +20%，需要额外注入的资金 =');
console.log('    1.2 × 总成本 − 总工资 = ' + need.toFixed(0) + ' 元/周期（每 1 级各类建筑）');
console.log('  这笔钱的合法来源在 1.0 契约中不存在：');
console.log('    · §5 工资是居民唯一收入；');
console.log('    · §4.3 现金池初始仅 5,000 元/级（每 1 级各类建筑合计仅 ' + (5000 * N) + ' 元），');
console.log('      而每周期的缺口就是 ' + need.toFixed(0) + ' 元 → 现金池在第一个周期即被耗尽；');
console.log('    · 1.0 无银行、无信贷、无政府支出、无出口（§1.3 明确列为非目标）。');
console.log('  ⇒ 结论：契约缺少一个"利润来源"环节。这不是参数问题，是会计结构问题。\n');

console.log('--- 五、与 §8 主循环的对照 ---\n');
console.log('  §8 步 3「利润计入对应建筑现金池」、步 4「消费者基于工资与消费组规则购买」。');
console.log('  步 3 只写了利润入池，没有定义"谁付出了这笔利润"。');
console.log('  ARCHITECTURE §6 的 M2 修正补上了工资池（wagePool），但工资池的来源是');
console.log('  "建筑现金池扣减工资"，其金额等于工资总额——仍不足以支撑 1.2 倍成本的收入。');
console.log('  ⇒ 主循环的资金闭环缺口 = 20% 的总成本，规模与 §3.4 的利润率声明同阶。\n');

console.log('--- 六、判定 ---\n');
console.log('  【不通过】§3.4「开局各建筑利润率恰好 20%」与 §5/§7 的封闭货币循环互斥。');
console.log('  可选的修复方向（任一即可，但都必须改契约）：');
console.log('    (a) 引入外部需求/出口，或让资本家把利润作为消费/投资重新支出（1.0 需新增机制）；');
console.log('    (b) 承认全局利润恒为 0，把 §3.4 的"20% 利润率"改为"相对价格加成"（即 §3.1 的');
console.log('        P_init 只作为相对价格结构，不再声称它是各建筑的实际利润率）；');
console.log('    (c) 给出初始货币存量并明确"利润来自存量货币的再循环"，同时把 §8.4 判据 A2');
console.log('        的下界从 −10% 改到能容纳结构性亏损的水平。');
console.log('  说明：本判定与 AI 扩建阈值、ODE 参数、人口规则、产能上限均无关，');
console.log('        是纯会计层面的矛盾，因此不可能靠调参绕过。');

fs.writeFileSync('out/accounting_probe.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/accounting_probe.txt');
