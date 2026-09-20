# rerun-probes.ps1 -- regenerate the arithmetic-probe evidence that belongs to the
#                     2026-09-18 "equilibrium" round (all outputs land in out/).
#
# The original out/archive/2026-09-18-equilibrium/ tree was never committed and is
# unrecoverable; this script rebuilds everything that is reproducible from in-repo
# sources. Sim-heavy probes (multigood_probe.js, core_sim_probe.js) are NOT run here
# by default: see -IncludeSlow.
param([switch]$IncludeSlow)

$ErrorActionPreference = 'Continue'
$root = 'D:\Code\YehenalaMarket'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$env:ELECTRON_RUN_AS_NODE = '1'
$node = 'D:\DSH\DSH Desktop\DSH Desktop.exe'
Set-Location $root

# probe, expected product, what it establishes
$fast = @(
  @{ f = 'equilibrium_handcheck.js';        out = '';                                     what = 'independent re-derivation of the equilibrium (stdout only)' },
  @{ f = 'calibration_probe.js';            out = 'out/calibration_probe.txt';            what = 'three-table joint calibration (k)' },
  @{ f = 'margin_base_probe.js';            out = 'out/margin_base_probe.txt';            what = 'opening margin under two cost bases' },
  @{ f = 'structure_probe.js';              out = 'out/structure_probe.txt';              what = 'industrial-structure feasibility (Leontief / gap LP)' },
  @{ f = 'construction_sink_probe.js';      out = 'out/construction_sink_probe.txt';      what = 'missing payer for construction power (5.1e4x gap)' },
  @{ f = 'accounting_probe.js';             out = 'out/accounting_probe.txt';             what = 'static money-loop accounting' },
  @{ f = 'gov_capital_accounting.js';       out = 'out/gov_capital_accounting.txt';       what = 'money loop with government + capital' },
  @{ f = 'debt_cap_probe.js';               out = 'out/debt_cap_probe.txt';               what = 'G7 debt-cap sizing' },
  @{ f = 'tick_probe.js';                   out = '';                                     what = 'single-good tick-level feasibility (stdout only)' },
  @{ f = 'price_dynamics_probe.js';         out = 'out/price_dynamics_probe.txt';         what = 'isolated test of the sec.2 price ODE' },
  @{ f = 'steady_profit_probe.js';          out = 'out/steady_profit_probe.txt';          what = 'steady-state margin distribution (A2 reachability)' },
  @{ f = 'expansion_probe.js';              out = 'out/expansion_probe.txt';              what = 'expansion-rule convergence' },
  @{ f = 'leontief_probe.js';               out = '';                                     what = 'Leontief inverse sanity (stdout only)' },
  @{ f = 'spring_price_probe.js';           out = '';                                     what = 'price-ODE spring analogy / T and zeta' },
  @{ f = 'spring_price_scale.js';           out = '';                                     what = 'dimensional diagnosis for the spring analogy' },
  @{ f = 'food_s_curve_probe.js';           out = 'out/food_s_curve_probe.txt';           what = 'S-curve candidate for basic-food demand' },
  @{ f = 'housing_curve_probe.js';          out = 'out/housing_curve_probe.txt';          what = 'exponential candidate for housing demand' },
  @{ f = 'demand_table_final.js';           out = 'out/demand_table_final.txt';           what = 'discrete completion of the sec.6.3 demand table' },
  @{ f = 'welfare_table_probe.js';          out = 'out/welfare_table_probe.txt';          what = 'wealth-tier table and welfare feasibility' }
)
$slow = @(
  @{ f = 'multigood_probe.js';  out = 'out/multigood_probe.txt';  what = 'multi-good joint sim + sec.8.4 verdicts' },
  @{ f = 'core_sim_probe.js';   out = 'out/core_sim_probe.txt';   what = 'full multi-good sim + sec.8.4 verdicts' }
)

$log = "# RERUN-PROBES -- arithmetic probes of the 2026-09-18 equilibrium round, REGENERATED`r`n"
$log += "# generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')`r`n"
$log += "# runner   : powershell -ExecutionPolicy Bypass -File out/archive/2026-09-18-equilibrium/rerun-probes.ps1`r`n"
$log += "# node     : DSH Desktop Electron in ELECTRON_RUN_AS_NODE mode (no standalone node/npm on this machine)`r`n"
$log += "# NOTE     : regenerated files, NOT the originals; original archive is unrecoverable.`r`n`r`n"

$set = if ($IncludeSlow) { $fast + $slow } else { $fast }
foreach ($p in $set) {
  $t0 = Get-Date
  $o = (& $node (Join-Path $root "tools\$($p.f)") 2>&1 | Out-String)
  $code = $LASTEXITCODE
  $ms = [int]((Get-Date) - $t0).TotalMilliseconds
  $exists = if ($p.out) { Test-Path (Join-Path $root $p.out) } else { $null }
  $log += ("{0,-28} exit={1,-4} {2,6} ms  lines={3,-5} product={4,-38} {5}`r`n" -f `
    $p.f, $code, $ms, ($o -split "`n").Count, $(if ($p.out) { "$($p.out):$(if ($exists) { 'ok' } else { 'MISSING' })" } else { '(stdout only)' }), $p.what)
  if ($code -ne 0) {
    $tail = (($o -split "`n") | Where-Object { $_.Trim() -ne '' } | Select-Object -Last 3) -join ' | '
    $log += "    !! non-zero exit; last lines: $tail`r`n"
  }
}
$log += "`r`n# slow probes (multigood_probe.js / core_sim_probe.js): $(if ($IncludeSlow) { 'RUN in this pass' } else { 'skipped -- pass -IncludeSlow to run them' })`r`n"
$outDir = Join-Path $root 'out\archive\2026-09-18-equilibrium'
New-Item -ItemType Directory -Force $outDir | Out-Null
[System.IO.File]::WriteAllText((Join-Path $outDir 'RERUN-PROBES.txt'), $log, $utf8)

# contract-document integrity check (repo discipline #5 in docs/ACTIVE.md)
# NOTE: the HTML file name contains CJK characters; this script is deliberately ASCII-only
# (PowerShell 5.1 reads BOM-less scripts as ANSI), so the file is located by globbing.
$htmlItem = Get-ChildItem (Join-Path $root 'docs') -Filter '*.html' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($htmlItem) {
  $o = (& $node (Join-Path $root 'tools\html_audit.js') $htmlItem.FullName 2>&1 | Out-String)
  [System.IO.File]::WriteAllText((Join-Path $outDir 'RERUN-HTML-AUDIT.txt'),
    ("# node tools/html_audit.js `"$($htmlItem.Name)`"   (regenerated)`r`n" + $o), $utf8)
  Write-Host "html audit written for $($htmlItem.Name)"
} else {
  [System.IO.File]::WriteAllText((Join-Path $outDir 'RERUN-HTML-AUDIT.txt'),
    "# no *.html found under docs/`r`n", $utf8)
}

Get-Content (Join-Path $outDir 'RERUN-PROBES.txt')
