// VC_Go/tools/viz/parse_run.js
//
// 把 `go run ./cmd/market-sim ...` 的纯文本输出解析成结构化 JSON。
// 输出与契约数据分开存放，避免把『实测』与『契约声明』混为一谈。
//
// 用法：node VC_Go/tools/viz/parse_run.js <dump.txt> [label]

'use strict';

const fs = require('fs');
const path = require('path');

function linesOf(text) {
  return text.split(/\r?\n/).map((l) => l.replace(/\s+$/, ''));
}

// 去掉表头行：以字母/汉字开头且不含小数点数字的行
function isHeaderRow(cells) {
  return cells.some((c) => /[^\d.\-+eE]/.test(c));
}

// 把表格区域的行收集成"逻辑行"：控制台宽度不足时，Go 的制表输出会把最后一列
// 折到下一行（形如只有一列的孤立数字行）。此处按"总列数达到预期宽度"来合并。
function collectTableRows(lines, startIdx, expectedCols) {
  const rows = [];
  let pending = [];
  for (let i = startIdx; i < lines.length; i++) {
    const raw = lines[i];
    if (!raw || !raw.trim()) break;
    const trimmed = raw.trim();
    if (isHeaderRow(trimmed.split(/\s+/))) break;
    const cells = trimmed.split(/\s+/);
    if (!cells.every((c) => /^-?\d+(\.\d+)?([eE][-+]?\d+)?%?$/.test(c))) break;
    pending = pending.concat(cells);
    while (pending.length >= expectedCols) {
      const row = pending.slice(0, expectedCols);
      pending = pending.slice(expectedCols);
      rows.push(row.map((c) => (c.endsWith('%') ? parseFloat(c) / 100 : parseFloat(c))));
    }
  }
  return rows;
}

function parseNumericTable(lines, startIdx) {
  // 从 startIdx（表头行）之后开始，收集连续的纯数字行，按表头列数合并折行
  const header = lines[startIdx].trim().split(/\s+/);
  return { header, rows: collectTableRows(lines, startIdx + 1, header.length) };
}

function findIndex(lines, needle, from = 0) {
  for (let i = from; i < lines.length; i++) if (lines[i].includes(needle)) return i;
  return -1;
}

function parseRun(text, label) {
  const lines = linesOf(text);
  const out = { label, sections: {} };

  // ── §3.4 标定
  const calIdx = findIndex(lines, '标定结果');
  if (calIdx >= 0) {
    out.spectralRadius = parseFloat((/=\s*([\d.]+)/.exec(lines[calIdx + 1] || '') || [])[1]);
    out.demandScale = parseFloat((/=\s*([\d.]+)/.exec(lines[calIdx + 2] || '') || [])[1]);
    const goodsHeader = findIndex(lines, '零利润价', calIdx);
    if (goodsHeader >= 0) {
      const t = parseNumericTable(lines, goodsHeader);
      // 这里首列是商品名，不能用 parseNumericTable；手工解析
      const rows = [];
      for (let i = goodsHeader + 1; i < lines.length; i++) {
        const raw = lines[i];
        if (!raw || !raw.trim()) break;
        const m = /^(\S+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)%$/.exec(raw.trim());
        if (!m) break;
        rows.push({ name: m[1], pcost: parseFloat(m[2]), pinit: parseFloat(m[3]), margin: parseFloat(m[4]) / 100 });
      }
      out.calibrationGoods = rows;
    }
  }

  // ── 开局状态
  const openIdx = findIndex(lines, '开局状态');
  if (openIdx >= 0) {
    const rows = [];
    for (let i = openIdx + 2; i < lines.length; i++) {
      const raw = lines[i];
      if (!raw || !raw.trim()) break;
      const m = /^(\S+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)$/.exec(raw.trim());
      if (!m) break;
      rows.push({ name: m[1], level: parseFloat(m[2]), govLevel: parseFloat(m[3]), privLevel: parseFloat(m[4]) });
    }
    out.opening = rows;
  }

  // ── 时序摘要（核心时间序列）
  const tsIdx = findIndex(lines, '时序摘要');
  if (tsIdx >= 0) {
    const hIdx = findIndex(lines, 'tick', tsIdx + 1);
    const t = parseNumericTable(lines, hIdx);
    out.series = t.rows.map((r) => ({
      tick: r[0], population: r[1], totalLevels: r[2], powerOutput: r[3],
      govCash: r[4], tax: r[5], govOperating: r[6], govDebt: r[7], debtCap: r[8],
    }));
  }

  // ── 资金流分解
  const flIdx = findIndex(lines, '资金流分解');
  if (flIdx >= 0) {
    const hIdx = findIndex(lines, 'tick', flIdx + 1);
    const t = parseNumericTable(lines, hIdx);
    out.flows = t.rows.map((r) => ({
      tick: r[0], wageTotal: r[1], consumerSpend: r[2], privProfit: r[3],
      govOperating: r[4], tax: r[5], powerSpend: r[6], powerRevenue: r[7],
    }));
  }

  // ── 部门利润率
  const mgIdx = findIndex(lines, '部门利润率');
  if (mgIdx >= 0) {
    const hIdx = findIndex(lines, 'tick', mgIdx + 1);
    const header = lines[hIdx].trim().split(/\s+/);
    const names = header.slice(1);
    const rows = [];
    for (let i = hIdx + 1; i < lines.length; i++) {
      const raw = lines[i];
      if (!raw || !raw.trim()) break;
      const cells = raw.trim().split(/\s+/);
      if (cells.length !== header.length) break;
      if (!cells.every((c) => /^-?\d+(\.\d+)?$/.test(c))) break;
      rows.push({ tick: parseInt(cells[0], 10), margins: cells.slice(1).map(Number) });
    }
    out.marginGoods = names;
    out.margins = rows;
  }

  // ── 终态
  const endIdx = findIndex(lines, '终态');
  if (endIdx >= 0) {
    const rows = [];
    for (let i = endIdx + 2; i < lines.length; i++) {
      const raw = lines[i];
      if (!raw || !raw.trim()) break;
      const m = /^(\S+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)%$/.exec(raw.trim());
      if (!m) break;
      rows.push({ name: m[1], price: parseFloat(m[2]), priceRatio: parseFloat(m[3]), margin: parseFloat(m[4]) / 100 });
    }
    out.endState = rows;
    const popLine = lines.find((l, i) => i > endIdx && l.startsWith('人口'));
    if (popLine) {
      const m = /人口\s+(\d+)\s+政府现金池\s+(-?\d+)\s+资本现金池\s+(-?\d+)\s+税收\s+(\d+)/.exec(popLine);
      if (m) out.endTotals = { population: +m[1], govCash: +m[2], capCash: +m[3], tax: +m[4] };
    }
  }

  // ── §8.4 判据
  const verdicts = [];
  for (let i = 0; i < lines.length; i++) {
    const m = /^A(\d)\s+(.+?)\s{2,}(通过|未通过)\s*$/.exec(lines[i]);
    if (m) {
      const detail = (lines[i + 1] || '').trim();
      const noteLine = (lines[i + 2] || '').trim();
      verdicts.push({
        id: 'A' + m[1], text: m[2].trim(), pass: m[3] === '通过', detail,
        selfNote: noteLine.startsWith('判据自身问题：') ? noteLine.replace('判据自身问题：', '') : null,
      });
    }
  }
  out.verdicts = verdicts;
  const summaryLine = lines.find((l) => l.startsWith('汇总：通过'));
  if (summaryLine) {
    const m = /通过\s+(\d+)\/(\d+)/.exec(summaryLine);
    if (m) out.verdictSummary = { pass: +m[1], total: +m[2] };
  }
  return out;
}

function main() {
  const [, , dumpPath, label] = process.argv;
  if (!dumpPath) {
    console.error('用法：node VC_Go/tools/viz/parse_run.js <dump.txt> [label]');
    process.exit(2);
  }
  const text = fs.readFileSync(dumpPath, 'utf8');
  const run = parseRun(text, label || path.basename(dumpPath, '.txt'));
  const outPath = dumpPath.replace(/\.txt$/, '.json');
  fs.writeFileSync(outPath, JSON.stringify(run, null, 2), 'utf8');
  console.log(`${path.basename(outPath)}: series=${(run.series || []).length} flows=${(run.flows || []).length} ` +
    `margins=${(run.margins || []).length} verdicts=${(run.verdicts || []).length} ` +
    `summary=${run.verdictSummary ? run.verdictSummary.pass + '/' + run.verdictSummary.total : '—'}`);
}

if (require.main === module) main();
module.exports = { parseRun };
