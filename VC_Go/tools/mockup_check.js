// tools/mockup_check.js -- load web/mockup.html in a real browser engine and assert it rendered.
//
// Why: the repo has no node/npm, so verification runs through DSH Desktop's Electron used as
// node. An exit code from a GUI-subsystem binary is unreliable on Windows, so this script
// writes a RESULT FILE and the caller reads it instead of trusting the exit code.
//
// Usage:
//   $env:ELECTRON_RUN_AS_NODE=''
//   & "D:\DSH\DSH Desktop\DSH Desktop.exe" tools\mockup_check.js web\mockup.html out\review\mockup-check.txt
//
// It opens the page in a hidden BrowserWindow, waits for load, then reports:
//   - any uncaught JS error / console error
//   - element counts (rows / canvases / nav buttons)
//   - whether each canvas actually got a non-empty bitmap (charts really drew)
//   - a PNG screenshot next to the result file (so the layout can be eyeballed)

const { app, BrowserWindow } = require('electron');
const fs = require('fs');
const path = require('path');

const srcRel = process.argv[2] || 'web/mockup.html';
const outRel = process.argv[3] || 'out/review/mockup-check.txt';
const src = path.resolve(srcRel);
const out = path.resolve(outRel);
const png = out.replace(/\.txt$/, '.png');

const lines = [];
let failed = false;
function log(s) { lines.push(s); }

app.disableHardwareAcceleration();

app.whenReady().then(async () => {
  const win = new BrowserWindow({
    width: 1600, height: 1000, show: false,
    webPreferences: { offscreen: false, contextIsolation: false, nodeIntegration: false },
  });

  const errors = [];
  win.webContents.on('console-message', (_e, level, message) => {
    if (level >= 2) errors.push('console: ' + message);
  });
  win.webContents.on('render-process-gone', (_e, d) => errors.push('render-process-gone: ' + JSON.stringify(d)));
  win.webContents.on('did-fail-load', (_e, code, desc) => errors.push('did-fail-load: ' + code + ' ' + desc));

  try {
    await win.loadFile(src);
  } catch (e) {
    log('FAILED to load: ' + e.message);
    finish(1);
    return;
  }

  // give rAF a few frames so redraw() has run
  await new Promise(r => setTimeout(r, 900));

  let report;
  try {
    report = await win.webContents.executeJavaScript(`(() => {
      const canvases = [...document.querySelectorAll('canvas')].map(c => {
        let painted = false;
        try {
          const g = c.getContext('2d');
          const d = g.getImageData(0, 0, Math.min(c.width, 200), Math.min(c.height, 200)).data;
          for (let i = 3; i < d.length; i += 4) { if (d[i] !== 0) { painted = true; break; } }
        } catch (e) { painted = 'err:' + e.message; }
        return { id: c.id, w: c.width, h: c.height, painted };
      });
      return {
        title: document.title,
        navButtons: document.querySelectorAll('.nav button').length,
        pages: document.querySelectorAll('.page').length,
        goodRows: document.querySelectorAll('#goodlist .row').length,
        legendItems: document.querySelectorAll('#legend span').length,
        buildRows: document.querySelectorAll('#buildbody tr').length,
        queueRows: document.querySelectorAll('#queuebody tr').length,
        pageIndicator: (document.querySelector('#q-page')||{}).textContent + '/' + (document.querySelector('#q-pages')||{}).textContent,
        statusChips: document.querySelectorAll('.statusbar .chip').length,
        canvases
      };
    })()`);
  } catch (e) {
    log('FAILED to evaluate: ' + e.message);
    finish(1);
    return;
  }

  log('title            : ' + report.title);
  log('nav buttons      : ' + report.navButtons + '  (契约 §四：四个一级菜单)');
  log('pages            : ' + report.pages);
  log('market 商品行    : ' + report.goodRows + '  (契约 §四-1：14 商品)');
  log('market 图例项    : ' + report.legendItems);
  log('建筑行           : ' + report.buildRows + '  (契约 §四-2：含自给农场 15 类)');
  log('队列行           : ' + report.queueRows + '  (契约 §四-3：每页 20 条)');
  log('队列页码         : ' + report.pageIndicator);
  log('状态栏 chip      : ' + report.statusChips + '  (记账不变量常驻)');
  log('');
  log('canvas 绘制检查：');
  for (const c of report.canvases) {
    log('  ' + c.id.padEnd(10) + ' ' + c.w + 'x' + c.h + '  painted=' + c.painted);
    if (c.painted !== true) failed = true;
  }

  // assertions
  const checks = [
    ['四个一级菜单', report.navButtons === 4],
    ['四个页面', report.pages === 4],
    ['商品行 = 14', report.goodRows === 14],
    ['建筑行 = 15', report.buildRows === 15],
    ['队列行 ≤ 20', report.queueRows > 0 && report.queueRows <= 20],
    ['每个 canvas 都已绘制', report.canvases.every(c => c.painted === true)],
    ['无 JS 错误', errors.length === 0],
  ];
  log('');
  for (const [name, ok] of checks) {
    log((ok ? 'PASS  ' : 'FAIL  ') + name);
    if (!ok) failed = true;
  }
  if (errors.length) { log(''); errors.forEach(e => log('  ' + e)); }

  // screenshot for eyeballing
  try {
    const img = await win.webContents.capturePage();
    fs.mkdirSync(path.dirname(png), { recursive: true });
    fs.writeFileSync(png, img.toPNG());
    log('');
    log('screenshot: ' + png);
  } catch (e) { log('screenshot failed: ' + e.message); }

  finish(failed ? 1 : 0);
});

function finish(code) {
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, lines.join('\n') + '\n', 'utf8');
  app.exit(code);
}
