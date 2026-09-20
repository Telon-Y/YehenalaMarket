# tools/mockup_check.ps1 -- self-check for web/mockup.html (renders + screenshots + asserts).
# ASCII-ONLY on purpose: Windows PowerShell 5.1 reads .ps1 as ANSI without a BOM.
#
# Why not Electron: this machine has no node/npm, and DSH Desktop only behaves as plain
# node with ELECTRON_RUN_AS_NODE=1 -- in which mode `require('electron')` does not resolve
# (so a BrowserWindow check is impossible), while WITHOUT that variable DSH Desktop loads
# its own app.asar and ignores the script argument. So verification goes through the
# system Edge/Chrome in headless mode instead. See docs/1.1 前端设计.md section 10.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\mockup_check.ps1
#   ... -OutDir out\review\mockup
#
# Outputs: the four page screenshots + a machine-readable report file.
# Exit code: 0 = all assertions passed, 1 = something failed.

param(
    [string]$Mockup = 'web\mockup.html',
    [string]$OutDir = 'out\review\mockup'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path $edge)) {
    $alt = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
    if (Test-Path $alt) { $edge = $alt } else { throw 'no Edge/Chrome found for headless checks' }
}

$src = (Resolve-Path $Mockup).Path
$url = 'file:///' + ($src -replace '\\', '/')
$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
$common = @('--headless=new','--disable-gpu','--hide-scrollbars','--force-device-scale-factor=1',
            '--virtual-time-budget=8000','--no-first-run','--no-default-browser-check')

$lines = New-Object System.Collections.Generic.List[string]
function Say($s) { $lines.Add($s); Write-Host $s }
$failed = $false

# ---- 1) dump the DOM and assert the rendered structure ----
$domFile = Join-Path $out 'dom.html'
Remove-Item $domFile -Force -ErrorAction SilentlyContinue
& cmd /c "`"$edge`" $($common -join ' ') --dump-dom `"$url`" > `"$domFile`" 2>nul"
$dom = $null
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    try { $dom = [System.IO.File]::ReadAllText($domFile, [System.Text.Encoding]::UTF8); break } catch { }
}
if (-not $dom) { throw "could not read $domFile (still locked by the browser)" }

function CountOf($pattern) { ([regex]::Matches($dom, $pattern)).Count }
function TbodyRows($id) {
    $m = [regex]::Match($dom, '<tbody id="' + $id + '">(?s)(.*?)</tbody>')
    if (-not $m.Success) { return -1 }
    ([regex]::Matches($m.Groups[1].Value, '<tr>')).Count
}

Say '== 1) rendered structure (from --dump-dom) =='
$navN   = CountOf 'role="tab"'
$pageN  = CountOf 'class="page( on)?"'
$goodN  = CountOf 'data-g="\d+"'
$buildN = TbodyRows 'buildbody'
$queueN = TbodyRows 'queuebody'
$chipN  = CountOf 'class="chip"'
$cvN    = CountOf '<canvas'

$checks = @(
    @{ n='four top-level menus';      ok = ($navN -eq 4);    v=$navN },
    @{ n='four pages';                ok = ($pageN -eq 4);   v=$pageN },
    @{ n='14 goods rows';             ok = ($goodN -eq 14);  v=$goodN },
    @{ n='15 building rows';          ok = ($buildN -eq 15); v=$buildN },
    @{ n='queue rows <= 20 (paged)';  ok = ($queueN -gt 0 -and $queueN -le 20); v=$queueN },
    @{ n='9 status-bar chips';        ok = ($chipN -eq 9);   v=$chipN },
    @{ n='7 canvases';                ok = ($cvN -eq 7);     v=$cvN },
    @{ n='queue page label present';  ok = ($dom -match 'id="q-page">\d+</b>'); v='-' },
    @{ n='negative cash shows arrow'; ok = ($dom -match ([char]0x25BC + '-'));  v='-' }
)
foreach ($c in $checks) {
    Say (("{0}  {1} (={2})" -f $(if ($c.ok) { 'PASS' } else { 'FAIL' }), $c.n, $c.v))
    if (-not $c.ok) { $failed = $true }
}

# ---- 2) screenshots for eyeballing ----
Say ''
Say '== 2) screenshots =='
$pages = @(
    @{ f='A-market'; h=1150; hash='' },
    @{ f='B-build';  h=1150; hash='#build' },
    @{ f='C-queue';  h=1150; hash='#queue' },
    @{ f='D-other';  h=1280; hash='#other' }
)
foreach ($p in $pages) {
    $png = Join-Path $out ($p.f + '.png')
    Remove-Item $png -Force -ErrorAction SilentlyContinue
    & cmd /c "`"$edge`" $($common -join ' ') --window-size=1600,$($p.h) --screenshot=`"$png`" `"$url$($p.hash)`" >nul 2>nul"
    Start-Sleep -Milliseconds 500
    if (Test-Path $png) { Say ("OK   {0}  {1} bytes" -f $p.f, (Get-Item $png).Length) }
    else { Say ("FAIL {0} (no screenshot)" -f $p.f); $failed = $true }
}

# ---- 3) report ----
Say ''
if ($failed) { Say 'RESULT: FAIL' } else { Say 'RESULT: PASS' }
$report = Join-Path $out 'mockup-check.txt'
$lines | Out-File $report -Encoding utf8
Write-Host ''
Write-Host ("report: {0}" -f $report)
if ($failed) { exit 1 }
exit 0
