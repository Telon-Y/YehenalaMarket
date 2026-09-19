// VC_Go/tools/_render_test.js —— 生成公式/表格渲染测试页，用于目视核验
'use strict';
const fs = require('fs');
const { render } = require('./mathrender.js');
const { inline } = require('./md2html.js');

const samples = [
  ['行内下标', '$P_{cost}$ 与 $P_{init}$ 的关系'],
  ['希腊字母', '$\\varepsilon$、$\\zeta$、$\\rho$、$\\omega$、$\\Delta$、$\\pi$'],
  ['分式', '$D = a\\left(\\frac{P}{P_0}\\right)^{-\\varepsilon}$'],
  ['分式嵌套上标', '$D_{\\text{基础食物}}(w) = \\frac{420}{1 + e^{-0.25\\,(w - 10)}}$'],
  ['复杂分式', '$\\rho = 2\\zeta\\sqrt{m\\,K(P)},\\qquad T = 2\\pi\\sqrt{\\frac{m}{K(P)}}$'],
  ['带括号的指数', '$D(w) = C - (C - 20)\\,e^{-b\\,(w - 5)}$'],
  ['转置', '$p = A^{\\top}p + l$'],
  ['根号', '$\\sqrt{mK}$ 与 $\\sqrt[3]{x}$'],
  ['求和', '$\\sum_{i} a_i$'],
  ['cases 环境', '$\\text{可动用} = \\begin{cases} \\min(B,\\ \\text{上限}) & B > 0 \\\\ \\text{上限} & B = 0 \\end{cases}$'],
  ['中文 text', '$\\text{消费者最终购买商品的总支出} + \\text{各建筑现金池期末总额}$'],
  ['表格脚注', '$^{†}$ 与 $^{††}$'],
  ['长行内公式', '价格不是手填的设计输入，而是由生产侧反推的**零利润价**；需求函数的归一化点取该价格，开局报价在其上加 20％ 作为扩建种子。**注意 $P_{cost}$ 只是需求曲线的归一化锚点**——在自由进入下利润率会被扩建压到阈值附近。'],
  ['嵌套分式', '$p = \\left(I - A^{\\top}\\right)^{-1} l$'],
  ['偏导', '$\\frac{\\partial \\ln P^*}{\\partial \\ln S} = -\\frac{1}{\\varepsilon}$'],
];

const rows = samples.map(([label, md]) => {
  return `<tr><td style="white-space:nowrap;color:#57606a;width:110px">${label}</td><td>${inline(md)}</td></tr>`;
}).join('\n');

const html = `<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8"><title>渲染测试</title>
<style>
body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;font-size:16px;line-height:1.9;margin:24px;color:#1f2328}
table{border-collapse:collapse;width:100%}
td{border-bottom:1px solid #d8dee4;padding:10px 12px;vertical-align:top}
.math{font-family:"Cambria Math","Latin Modern Math","STIX Two Math",Cambria,"Times New Roman",Georgia,serif;font-size:1.06em;line-height:1.5}
.math-inline{display:inline;white-space:normal}
.mi{font-style:italic}.mn{font-style:normal}
.mtext{font-family:inherit;font-style:normal}
.mopname{font-style:normal;font-family:inherit;padding-right:.12em}
.mo{padding:0 .04em}
.mscript{white-space:nowrap}
.msub{font-size:.72em;vertical-align:-.25em;line-height:0}
.msup{font-size:.72em;vertical-align:.45em;line-height:0}
.mfrac{display:inline-flex;flex-direction:column;vertical-align:-.45em;text-align:center;margin:0 .18em;line-height:1.15;font-size:.97em}
.mfrac-num{border-bottom:1.1px solid currentColor;padding:0 .3em .06em}
.mfrac-den{padding:.06em .3em 0}
.msqrt{white-space:nowrap}.msqrt-sym{padding-right:.04em}
.msqrt-body{border-top:1.1px solid currentColor;padding:.08em .18em 0 .06em}
.msqrt-deg{font-size:.62em;vertical-align:.7em;margin-right:-.35em}
.mdelim{font-size:1.15em;line-height:1}
.mcases{display:inline-table;vertical-align:middle;border-collapse:collapse;margin:0 .2em}
.mcases td{border:none;padding:.05em .55em .05em 0;text-align:left;white-space:nowrap}
.munknown{background:#fff1f0;border:1px solid #ffa39e;padding:0 .2em;color:#a8071a}
.math-error{background:#fff1f0;border:1px solid #ffa39e;padding:0 .3em;color:#a8071a}
h2{font-size:18px}
</style></head><body>
<h2>公式渲染核验（新渲染器 mathrender.js）</h2>
<table>${rows}</table>
</body></html>`;

fs.writeFileSync('out/render_test.html', html, 'utf8');
console.log('已写出 out/render_test.html');

// 同时打印原始 HTML 便于对照
console.log('\n=== 逐项原始输出 ===');
for (const [label, md] of samples) {
  console.log(`\n【${label}】${md.slice(0, 60)}`);
  console.log(renderInline(md).slice(0, 400));
}
