# tools/start.ps1 -- one-click launcher for the 1.1 web UI.
#
# What it does:
#   1. checks it is running from the workspace (web/ + gosim/ present)
#   2. locates a Go toolchain (PATH, or D:\Code\Go\bin\go.exe)
#   3. exports tick snapshots if they are missing OR stale for the requested -Ticks
#   4. starts the read-only HTTP server (-serve-only: instant, no re-simulation)
#   5. waits until the server answers, then opens the browser
#   6. runs until interrupted, then stops the server it started
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\start.ps1
#   ... -Ticks 10000 -Port 8790 -NoBrowser
#
# ASCII-ONLY on purpose: Windows PowerShell 5.1 reads .ps1 as ANSI without a BOM,
# so any non-ASCII in this file would corrupt parsing (this bit us twice).
# Exit code: 0 = started and stopped cleanly, 1 = failed.

param(
    [int]$Ticks = 1000,
    [int]$Port  = 8787,
    [int]$ExportEvery = 0,      # 0 = auto (the sim targets ~2000 rows)
    [switch]$NoBrowser,
    [switch]$ForceExport
)

$ErrorActionPreference = 'Stop'

# ---- 0) locate the workspace ----------------------------------------------
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
Set-Location $root

$webDir  = Join-Path $root 'web'
$gosim   = Join-Path $root 'gosim'
$index   = Join-Path $webDir 'index.html'
$dataDir = Join-Path $root 'out\webdata'
$metaJson  = Join-Path $dataDir 'meta.json'
$snapJsonl = Join-Path $dataDir 'snapshots.jsonl'

Write-Host ''
Write-Host '=== YehenalaMarket 1.1 launcher ===' -ForegroundColor Cyan
Write-Host ("workspace : {0}" -f $root)
Write-Host ("ticks     : {0}    port: {1}" -f $Ticks, $Port)
Write-Host ''

function Fail($msg) {
    Write-Host ''
    Write-Host ("FAILED: " + $msg) -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $index)) { Fail 'web/index.html not found (is this the workspace root?)' }
if (-not (Test-Path $gosim)) { Fail 'gosim/ not found (is this the workspace root?)' }

# ---- 1) locate Go ---------------------------------------------------------
$go = $null
$cand = Get-Command go -ErrorAction SilentlyContinue
if ($cand) { $go = $cand.Source }
if (-not $go) {
    foreach ($p in @('D:\Code\Go\bin\go.exe', 'C:\Go\bin\go.exe')) {
        if (Test-Path $p) { $go = $p; break }
    }
}
if (-not $go) { Fail 'Go toolchain not found. Install Go, or put go.exe on PATH.' }
Write-Host ("go        : {0}" -f $go)
$gov = (& $go version) 2>&1
Write-Host ("            {0}" -f $gov)

# ---- 2) free the port if a stale server holds it ---------------------------
$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($busy) {
    $owners = ($busy | Select-Object -ExpandProperty OwningProcess -Unique) -join ','
    $proc = Get-Process -Id ($busy[0].OwningProcess) -ErrorAction SilentlyContinue
    $nm = 'unknown'
    if ($proc) { $nm = $proc.ProcessName }
    Write-Host ("port {0} busy (pid {1}, {2}) - stopping it" -f $Port, $owners, $nm) -ForegroundColor Yellow
    foreach ($procId in ($busy | Select-Object -ExpandProperty OwningProcess -Unique)) {
        Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 800
    if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
        Fail "port $Port is still busy; pass -Port to use another one"
    }
    Write-Host 'port freed' -ForegroundColor Green
}

# ---- 3) export data if missing / stale / forced ---------------------------
$needExport = $true
if ((Test-Path $snapJsonl) -and (Test-Path $metaJson) -and (-not $ForceExport)) {
    $ok = $false
    try {
        $m = Get-Content $metaJson -Raw -Encoding UTF8 | ConvertFrom-Json
        if ([int]$m.ticks -eq $Ticks) { $ok = $true }
        else { Write-Host ("existing export is for {0} ticks, want {1} - re-exporting" -f $m.ticks, $Ticks) -ForegroundColor Yellow }
    } catch {
        Write-Host 'meta.json unreadable - re-exporting' -ForegroundColor Yellow
    }
    if ($ok) { $needExport = $false }
}

if ($needExport) {
    Write-Host ''
    Write-Host ("-- exporting {0} ticks (first run takes a moment) --" -f $Ticks) -ForegroundColor Cyan
    if (-not $env:GOROOT) { $env:GOROOT = Split-Path -Parent (Split-Path -Parent $go) }
    $goArgs = @('run', './cmd/market-sim', '-ticks', "$Ticks", '-export-dir', $dataDir)
    if ($ExportEvery -gt 0) { $goArgs += @('-export-every', "$ExportEvery") }
    Push-Location $gosim
    try {
        & $go @goArgs | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "export failed (exit $LASTEXITCODE)" }
    } finally { Pop-Location }
    if (-not (Test-Path $snapJsonl)) { Fail "export reported success but snapshots.jsonl is missing" }
    $kb = [math]::Round((Get-Item $snapJsonl).Length / 1KB, 0)
    Write-Host ("exported: {0} KB" -f $kb) -ForegroundColor Green
} else {
    Write-Host 'existing export is up to date - skipping simulation' -ForegroundColor Green
}

# ---- 4) start the read-only server (-serve-only = instant) -----------------
Write-Host ''
Write-Host '-- starting read-only server --' -ForegroundColor Cyan
$log    = Join-Path $dataDir 'serve.log'
$errLog = Join-Path $dataDir 'serve.err.log'
$srv = Start-Process -FilePath $go `
    -ArgumentList @('run', './cmd/market-sim', '-serve-only', '-serve', ":$Port", '-export-dir', $dataDir) `
    -WorkingDirectory $gosim -PassThru -NoNewWindow `
    -RedirectStandardOutput $log -RedirectStandardError $errLog

$url = "http://localhost:$Port/"
$ready = $false
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    if ($srv.HasExited) { break }
    try {
        $r = Invoke-WebRequest ($url + 'data/meta.json') -UseBasicParsing -TimeoutSec 3
        if ($r.StatusCode -eq 200) { $ready = $true; break }
    } catch { }
}

if (-not $ready) {
    $errText = ''
    if (Test-Path $errLog) { $errText += (Get-Content $errLog -Raw) }
    if (Test-Path $log)    { $errText += (Get-Content $log -Raw) }
    Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
    Write-Host $errText
    Fail "server did not become ready on $url"
}
Write-Host ("server ready: {0}" -f $url) -ForegroundColor Green

# ---- 5) open the browser --------------------------------------------------
if (-not $NoBrowser) {
    Write-Host 'opening browser...' -ForegroundColor Cyan
    Start-Process $url | Out-Null
}

# ---- 6) run until interrupted, then clean up ------------------------------
Write-Host ''
Write-Host '=========================================================' -ForegroundColor Cyan
Write-Host ("  UI is ready:  {0}" -f $url) -ForegroundColor Green
Write-Host '  Press Ctrl+C to stop (this also stops the server)' -ForegroundColor Cyan
Write-Host '=========================================================' -ForegroundColor Cyan
Write-Host ''
try {
    while (-not $srv.HasExited) { Start-Sleep -Milliseconds 500 }
    Write-Host 'server exited on its own'
} finally {
    if (-not $srv.HasExited) {
        Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
        Write-Host 'server stopped' -ForegroundColor Yellow
    }
}
exit 0
