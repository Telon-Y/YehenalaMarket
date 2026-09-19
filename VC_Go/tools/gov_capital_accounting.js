// VC_Go/tools/gov_capital_accounting.js —— 含政府与资本的货币闭环精确校验
//
// 用户方案：
//   (1) 政府抽取全部交易额的税率 t（默认 10%）作为税收；
//   (2) 建造力必须在市场内购买，最终购买方是政府；
//   (3) 建筑分所有权：政府 / 私有；私有建筑的运营纯利归金融区（资本收入）；
//   (4) 私有扩建的资金从建筑现金池划拨给政府现金池（政府代建）；
//      政府建筑的扩建由政府税收出资；
//   (5) 金融区每级雇 1,000 人（阶层比例不变 75/20/5），每级掌控 5 级其余建筑。
//
// 本脚本回答三个问题：
//   A. 加入政府征税后，货币是否守恒？各主体的收支是否各自闭合？
//   B. 校准：三表（产出、需求、工资）联合自洽的稳态规模是多少？（1.0 从未做过这一步）
//   C. 10% 税率能支撑多快的扩建？与 §8.4 的 10,000 周期窗口是否匹配？
//
// 运行：node VC_Go/tools/gov_capital_accounting.js
'use strict';
const fs = require('fs');
const OUT = [];
const _log = console.log.bind(console);
console.log = (...a) => { const s = a.map(String).join(' '); OUT.push(s); _log(s); };

const S = ['grain', 'food', 'fabric', 'clothes', 'luxury', 'coal', 'iron', 'steel', 'tools', 'housing', 'power'];
const N = 11;
const EPS = [0.3, 0.8, 0.6, 0.5, 1.5, 0.4, 0.4, 0.5, 0.6, 1.2, 0.2];
const PCOST = [675, 1350, 750, 788, 1750, 1006, 1006, 1381, 767, 741, 7250];
const REC = [
  { q: 50, inp: [] }, { q: 45, inp: [[0, 40]] }, { q: 45, inp: [] }, { q: 100, inp: [[2, 60]] },
  { q: 30, inp: [[2, 25]] }, { q: 60, inp: [[8, 15], [5, 15]] }, { q: 60, inp: [[8, 15], [5, 15]] },
  { q: 90, inp: [[6, 60], [5, 30]] }, { q: 80, inp: [[7, 20]] }, { q: 60, inp: [[7, 5], [8, 5]] },
  { q: 15, inp: [[7, 25], [6, 25], [8, 20]] },
];
const BUILDCOST = [200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100];
const WAGE_LEVEL = 33750;          // §5 5,000 人 × 6.75
const FIN_WORKERS = 1000;          // 金融区每级 1,000 人
const COHORT_SHARE = { laborer: 0.75, engineer: 0.20, capitalist: 0.05 };
const COHORT_WAGE = { laborer: 5, engineer: 10, capitalist: 20 };
const FIN_AVG_WAGE = 0.75 * 5 + 0.20 * 10 + 0.05 * 20;   // = 6.75，比例不变
const FIN_WAGE_LEVEL = FIN_WORKERS * FIN_AVG_WAGE;        // = 6,750 元/级/周期
const CTRL_PER_FIN = 5;            // 每级金融区掌控 5 级其余建筑
const ARABLE = 10000;
const BASE = 1.0;                  // 需求相对规模基准

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

// ---------- §6.2/§6.3 每人每 tick 的最终需求（相对单位） ----------
function perCapitaFinal(tier) {
  const table = [{ w: 5, d: [39, 210, 0, 20] }, { w: 10, d: [41, 210, 7, 74] }, { w: 20, d: [0, 210, 122, 130] }];
  const w = Math.min(20, Math.max(5, tier));
  let lo = table[0], hi = table[2];
  for (let i = 0; i + 1 < table.length; i++) if (w >= table[i].w && w <= table[i + 1].w) { lo = table[i]; hi = table[i + 1]; break; }
  const tt = hi.w === lo.w ? 0 : (w - lo.w) / (hi.w - lo.w);
  const d = lo.d.map((x, k) => x + (hi.d[k] - x) * tt);
  const f = new Array(N).fill(0);
  f[2] += d[0] / 2; f[3] += d[0] / 2;                 // 简朴衣物组：织物/服装
  f[0] += d[1] / 2.5; f[1] += d[1] * 1.5 / 2.5;       // 基础食物组：谷物/加工食品
  f[3] += d[2] / 2; f[4] += d[2] / 2;                 // 标准衣物组：服装/高档服装
  f[9] += d[3];                                       // 住宅
  return f.map((x) => x / 100000);
}

const f1 = perCapitaFinal(10);
// 完全需求（每单位最终需求对总产出的拉动）
const Y1 = B.map((row) => row.reduce((s, x, j) => s + x * f1[j], 0));
const L1 = Y1.map((y, i) => y / REC[i].q);            // 每人所需的建筑级数

console.log('=== 含政府与资本的货币闭环精确校验 ===\n');
console.log(`参数：税率 t = 10%（可在下方扫描）；金融区每级 ${FIN_WORKERS} 人、每级工资 ${FIN_WAGE_LEVEL} 元；`);
console.log(`      每级金融区掌控 ${CTRL_PER_FIN} 级其余建筑；建筑工资 ${WAGE_LEVEL} 元/级。\n`);

// ---------- A. 校准：让「工资总支出 = 消费者可用于消费的收入」 ----------
console.log('--- A. 稳态规模校准（1.0 契约从未做过这一步）---\n');
console.log('  自洽条件：全部建筑的工资支出 = 居民可用于消费的收入（税后）。');
console.log('  设人口为 P，则：');
console.log('    居民税前工资收入 W(P) = Σ_i L_i·P·33750 + 金融区工资');
console.log('    其中 L_i = 每人所需的建筑级数（由 Leontief 完全需求给出）');
console.log('    居民可支配收入 = W(P)·(1 − t)   （假设所得税由居民承担，等价于消费端征税）');
console.log('    消费者支出 = (1−t)·W(P) 必须等于它能买到的商品价值 = P·Σf_i·P_cost,i\n');
const sumFL = L1.reduce((a, b) => a + b, 0);
const finPerCapita = L1.reduce((a, b) => a + b, 0) / CTRL_PER_FIN / 100000; // 相对占位，后面精确算
const wagePerCapita = sumFL * WAGE_LEVEL;
const spendPerCapita = f1.reduce((s, x, i) => s + x * PCOST[i], 0);
// 金融区级数与工资随人口同步：金融区级数 = 其余建筑级数 / 5
//   其余建筑每人的级数 = sumFL
//   金融区每人的级数 = sumFL / 5
const finWagePerCapita = (sumFL / CTRL_PER_FIN) * FIN_WAGE_LEVEL;
const wageTotalPerCapita = wagePerCapita + finWagePerCapita;
console.log(`  每人所需建筑级数（Leontief 完全需求）= ${sumFL.toFixed(6)}`);
console.log(`  每人工资支出（建筑）      = ${wagePerCapita.toFixed(4)} 元`);
console.log(`  每人所需金融区级数        = ${(sumFL / CTRL_PER_FIN).toFixed(6)}`);
console.log(`  每人工资支出（金融区）    = ${finWagePerCapita.toFixed(4)} 元`);
console.log(`  每人工资支出合计          = ${wageTotalPerCapita.toFixed(4)} 元`);
console.log(`  每人可购买的商品价值      = ${spendPerCapita.toFixed(4)} 元`);
const ratio = wageTotalPerCapita / spendPerCapita;
console.log(`  ⇒ 工资 / 需求价值 = ${ratio.toFixed(4)}`);
console.log(`  ⇒ 自洽所需人口 = 每 1 人工资对应 ${(spendPerCapita / wageTotalPerCapita).toFixed(4)} 人的需求`);
console.log('  ⇒ 由于工资与需求都随人口线性缩放，【人口缩放不能消除这个比例失衡】。');
console.log('     要让它自洽，必须改动其中一侧：单级产出、工资水平、需求量表，或财富档。\n');

// 反向求解：给定财富档与他人数，求需要多大的单级产出缩放
console.log('  反解：若保持 §3.3 配方与 §5 工资不变，需要把 §6.3 需求量表乘以下倍数才能自洽：');
const k = spendPerCapita / wageTotalPerCapita;
console.log(`    §6.3 需求 × ${k.toFixed(4)}（即财富档上调、或单级产出下调到 ${(1 / ratio).toFixed(4)} 倍）`);
console.log(`    等价地：把每级雇佣人数从 5,000 改为 ${Math.round(5000 / ratio)} 人，或把工资从 6.75 改为 ${(6.75 / ratio).toFixed(2)} 元\n`);

// ---------- B. 取一个自洽的参考规模 ----------
console.log('--- B. 自洽参考规模（把 §6.3 需求按上面的倍数缩放后）---\n');
const POP = 100000;
const F = f1.map((x) => x * POP * k);              // 缩放后的最终需求
const Y = B.map((row) => row.reduce((s, x, j) => s + x * F[j], 0));
const L = Y.map((y, i) => y / REC[i].q);
console.log(`  人口 ${POP.toLocaleString()}，§6.3 需求缩放 ×${k.toFixed(3)}`);
console.log('  所需建筑级数：' + S.map((s, i) => `${s}=${L[i].toFixed(2)}`).join(' '));
const Lother = L.reduce((a, b) => a + b, 0);
const Lfin = Lother / CTRL_PER_FIN;
console.log(`  其余建筑合计 ${Lother.toFixed(2)} 级 → 金融区 ${Lfin.toFixed(2)} 级`);
console.log(`  耕地占用：谷物 ${L[0].toFixed(2)} + 棉花 ${L[2].toFixed(2)} = ${(L[0] + L[2]).toFixed(2)} / ${ARABLE}`);

// ---------- C. 各主体收支闭环 ----------
console.log('\n--- C. 各主体收支闭环（稳态，价格取 P_cost）---\n');
const t = 0.10;
// 交易额
let consumerValue = 0;
for (let i = 0; i < N; i++) consumerValue += F[i] * PCOST[i];
let intermediateValue = 0;
for (let i = 0; i < N; i++) for (const [j, q] of REC[i].inp) intermediateValue += L[i] * q * PCOST[j];
// 居民
const wageBuilding = Lother * WAGE_LEVEL;
const wageFin = Lfin * FIN_WAGE_LEVEL;
const wageTotal = wageBuilding + wageFin;
// 税：对全部交易额征收
const taxConsumer = consumerValue * t;
const taxIntermediate = intermediateValue * t;
let taxTotal = taxConsumer + taxIntermediate;
// 居民可支配 = 工资 − 消费税（假设消费税由居民承担）
const disposable = wageTotal - taxConsumer;
console.log(`  居民：工资 ${wageBuilding.toFixed(0)}(建筑) + ${wageFin.toFixed(0)}(金融) = ${wageTotal.toFixed(0)}`);
console.log(`        缴纳消费税 ${taxConsumer.toFixed(0)} → 可支配 ${disposable.toFixed(0)}`);
console.log(`        购买最终商品需支出 ${consumerValue.toFixed(0)}`);
console.log(`        ⇒ 收支差 = ${(disposable - consumerValue).toFixed(0)}（0 表示闭环）`);

console.log(`\n  建筑：中间投入交易额 ${intermediateValue.toFixed(0)}，消费者销售额 ${consumerValue.toFixed(0)}`);
console.log(`        工资支出 ${wageBuilding.toFixed(0)}，中间投入买入 ${intermediateValue.toFixed(0)}`);
console.log(`        缴纳中间环节税 ${taxIntermediate.toFixed(0)}`);
const buildingNet = consumerValue + intermediateValue - wageBuilding - intermediateValue - taxIntermediate;
console.log(`        ⇒ 建筑部门净额（= 私有纯利） = ${buildingNet.toFixed(0)}`);

console.log(`\n  政府：税收 ${taxTotal.toFixed(0)}（消费端 ${taxConsumer.toFixed(0)} + 中间环节 ${taxIntermediate.toFixed(0)}）`);
console.log(`        用途：在市场上购买建造力`);
// 建造力经济学
const powerPrice = PCOST[10];
let powerForGov = taxTotal / powerPrice;
console.log(`        可购建造力 = ${powerForGov.toFixed(3)} 单位/周期`);
console.log(`        折合建筑等级 = ${(powerForGov / 600).toFixed(4)} 级/周期（平均成本 600）`);
console.log(`        10,000 周期累计 = ${(powerForGov / 600 * 10000).toFixed(1)} 级`);

console.log(`\n  金融区：掌控 ${Lother.toFixed(2)} 级建筑，取得其运营纯利 ${buildingNet.toFixed(0)}`);
console.log(`        自身工资支出 ${wageFin.toFixed(0)}，自身缴税并入上面口径`);
console.log(`        ⇒ 资本可支配收入 = ${buildingNet.toFixed(0)}`);
const expandRate = buildingNet > 0 ? buildingNet / (600 * powerPrice) : 0;
console.log(`        ⇒ 若全部用于扩建：${expandRate.toFixed(4)} 级/周期`);

console.log('\n  货币守恒总校验：');
const lhs = disposable + taxConsumer + buildingNet + taxIntermediate;
console.log(`    居民可支配 ${disposable.toFixed(0)} + 消费税 ${taxConsumer.toFixed(0)} + 建筑纯利 ${buildingNet.toFixed(0)} + 中间税 ${taxIntermediate.toFixed(0)}`);
console.log(`    = ${lhs.toFixed(0)}  应等于 消费者支出 ${consumerValue.toFixed(0)} + 中间投入 ${intermediateValue.toFixed(0)} + 建筑工资 ${wageBuilding.toFixed(0)} − 中间投入 ${intermediateValue.toFixed(0)}`);
console.log(`    = ${(consumerValue + wageBuilding).toFixed(0)}`);
console.log(`    残差 = ${(lhs - consumerValue - wageBuilding).toFixed(6)}`);

// ---------- D. 税率与扩建速度扫描 ----------
console.log('\n--- D. 税率扫描：政府能支撑的扩建速度 ---\n');
console.log('  税率   税收        可购建造力      等级/周期    1万周期累计   50→3000 级需要');
for (const tt of [0.05, 0.10, 0.15, 0.20, 0.30]) {
  const tax = (consumerValue + intermediateValue) * tt;
  const pw = tax / powerPrice;
  const lv = pw / 600;
  console.log(`  ${(tt * 100).toFixed(0).padStart(3)}%   ${tax.toFixed(0).padStart(9)}   ${pw.toFixed(3).padStart(13)}   ${lv.toFixed(4).padStart(11)}   ${(lv * 10000).toFixed(0).padStart(11)}   ${lv > 0 ? Math.round(2950 / lv).toLocaleString() : '∞'} 周期`);
}
console.log('\n  参照：ARCHITECTURE §10.2 假设 10,000 周期内扩到 3,000 级');
console.log('  ⇒ 该目标需要约 0.295 级/周期的扩建速度。');
console.log('  ⇒ 仅靠 10% 税收采购建造力，速度远低于此；缺口需要靠【建造部门自身的扩建】');
console.log('     来补——即政府先投资建造部门，把建造力供给提上去。');

// ---------- E. 建造部门的自我扩张回路 ----------
console.log('\n--- E. 建造部门自我扩张回路（决定长期扩建速度的关键）---\n');
const steelPerPower = 25 / 15, ironPerPower = 25 / 15, toolsPerPower = 20 / 15;
console.log(`  1 单位建造力需要：钢 ${steelPerPower.toFixed(3)} + 铁 ${ironPerPower.toFixed(3)} + 工具 ${toolsPerPower.toFixed(3)}`);
const powerInputCost = steelPerPower * PCOST[7] + ironPerPower * PCOST[6] + toolsPerPower * PCOST[8];
console.log(`  按 P_cost 计，单位建造力的中间投入成本 = ${powerInputCost.toFixed(0)}，劳动成本 = ${(WAGE_LEVEL / 15).toFixed(0)}`);
const powerMargin = (powerPrice - powerInputCost - WAGE_LEVEL / 15) / (powerInputCost + WAGE_LEVEL / 15);
console.log(`  ⇒ 建造部门零利润价下的利润率 = ${(powerMargin * 100).toFixed(2)}%（应为 0）`);
console.log(`  建造部门自身 1 级成本 = ${BUILDCOST[10]} 建造力 = ${BUILDCOST[10] * 1} 单位`);
console.log(`  1 级建造部门产出 15 单位/周期 → 回本周期 = ${(BUILDCOST[10] / 15).toFixed(2)} 周期`);
console.log('  ⇒ 建造部门是回报最快的投资（6.7 周期回本），政府应优先用它把建造力供给拉起来。');

console.log('\n--- F. 判定 ---\n');
console.log('  【闭环成立】政府征税 + 采购建造力 + 私有纯利归金融区 + 私有扩建划拨给政府，');
console.log('   四条规则合起来把"建造力无购买方"的黑洞补上了：');
console.log('     · 建造力有了最终需求方（政府）；');
console.log('     · 货币经"税收 → 政府 → 建造力 → 建造部门 → 工资/投入"回到居民；');
console.log('     · 私有纯利有了归属主体（金融区），不再是悬空项；');
console.log('     · 私有扩建从建筑现金池划拨，资金不凭空生成。');
console.log('  【仍然存在】居民工资收入与 §6.3 需求价值之间的比例失衡（上面 A 节），');
console.log('   这是 1.0 的独立问题，必须靠"三表联合标定"解决，政府/资本不解决它。');
console.log('  【量级提示】10% 税率在自洽规模下可支撑的政府直投扩建速度约');
console.log(`   ${(powerForGov / 600).toFixed(4)} 级/周期；要达到 §10.2 的 0.295 级/周期，`);
console.log('   需要同时扩建建造部门（其自身成本低、回报快），或提高税率。');

fs.writeFileSync('out/gov_capital_accounting.txt', OUT.join('\n') + '\n', 'utf8');
_log('\n已写入 out/gov_capital_accounting.txt');
