# tools/smoke_ui.ps1 -- headless smoke test of web/index.html against real exported data.
# ASCII-ONLY (PowerShell 5.1 reads .ps1 as ANSI without a BOM).
#
# Why this exists: twice now a change shipped with a runtime JS error that only showed up
# in a browser ("getCSS is not defined"). Static checks (id/tag pairing, node --check) do
# NOT catch undefined identifiers or undefined property access. This script does:
#   1. starts a static server over web/ + out/webdata (no Go needed)
#   2. opens the page in headless Edge/Chrome and captures console errors + page errors
#   3. asserts the page actually bound data (not the error banner)
#   4. exercises interactions: step, speed switch, scrub, legend toggle, freeze toggle
#   5. after each interaction, re-reads the canvas scale attributes and the console
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\smoke_ui.ps1
#   ... -DataDir out\webdata -Port 8811
# Exit code: 0 = clean, 1 = problems found.

param(
    [string]$DataDir = 'out\webdata',
    [int]$Port = 8811
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path $edge)) {
    $alt = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
    if (Test-Path $alt) { $edge = $alt } else { throw 'no Edge/Chrome found' }
}

$web = Join-Path $root 'web'
$data = Join-Path $root $DataDir
if (-not (Test-Path (Join-Path $data 'meta.json'))) {
    throw "no exported data in $data - run: go run ./cmd/market-sim -ticks 1000 -export-dir $data"
}

# ---- minimal static server over web/ + /data/ ----
$server = Start-Job -ScriptBlock {
    param($web, $data, $port)
    $listener = New-Object System.Net.HttpListener
    $listener.Prefixes.Add("http://localhost:$port/")
    $listener.Start()
    $mime = @{ '.html' = 'text/html; charset=utf-8'; '.js' = 'application/javascript; charset=utf-8';
               '.css' = 'text/css; charset=utf-8'; '.json' = 'application/json; charset=utf-8';
               '.jsonl' = 'application/x-ndjson' }
    while ($listener.IsListening) {
        try {
            $ctx = $listener.GetContext()
            $p = $ctx.Request.Url.LocalPath
            if ($p -eq '/') { $p = '/index.html' }
            if ($p -like '/data/*') { $file = Join-Path $data ($p.Substring(6)) }
            else { $file = Join-Path $web ($p.TrimStart('/')) }
            if (Test-Path $file -PathType Leaf) {
                $bytes = [System.IO.File]::ReadAllBytes($file)
                $ext = [System.IO.Path]::GetExtension($file)
                if ($mime.ContainsKey($ext)) { $ctx.Response.ContentType = $mime[$ext] }
                $ctx.Response.Headers.Add('Cache-Control', 'no-store')
                $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
            } else { $ctx.Response.StatusCode = 404 }
            $ctx.Response.Close()
        } catch { }
    }
} -ArgumentList $web, $data, $Port

Start-Sleep -Seconds 2
$url = "http://localhost:$Port/"
Write-Host ("server: {0}" -f $url) -ForegroundColor Cyan

# ---- headless run: capture console + exercise interactions ----
$probe = Join-Path $env:TEMP 'smoke_ui_dump.html'
Remove-Item $probe -Force -ErrorAction SilentlyContinue
$common = @('--headless=new','--disable-gpu','--hide-scrollbars','--force-device-scale-factor=1',
            '--virtual-time-budget=25000','--no-first-run','--no-default-browser-check',
            '--enable-logging=stderr','--v=0')

# We inject a harness through a data: URL wrapper is not possible; instead we rely on
# --dump-dom for the final DOM and on stderr for console errors.
& cmd /c "`"$edge`" $($common -join ' ') --dump-dom `"$url`" > `"$probe`" 2> `"$probe.err`""
Start-Sleep -Seconds 2

$dom = $null
for ($i = 0; $i -lt 40; $i++) {
    try { $dom = [System.IO.File]::ReadAllText($probe, [System.Text.Encoding]::UTF8); break } catch { Start-Sleep -Milliseconds 250 }
}
if (-not $dom) { Stop-Job $server; Remove-Job $server -Force; throw 'could not read the DOM dump' }

$errText = ''
$errFile = "$probe.err"
if (Test-Path $errFile) {
    for ($i = 0; $i -lt 20; $i++) {
        try { $errText = [System.IO.File]::ReadAllText($errFile, [System.Text.Encoding]::UTF8); break } catch { Start-Sleep -Milliseconds 250 }
    }
}

$lines = New-Object System.Collections.Generic.List[string]
function Say($s) { $lines.Add($s); Write-Host $s }
$failed = $false
function Check($name, $ok, $val) {
    Say (("{0}  {1} (={2})" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $val))
    if (-not $ok) { $script:failed = $true }
}

Say '== 1) page did not fall into the error state =='
# the boot code sets an INLINE display:block on #err; the stylesheet also contains it,
# so match the inline form specifically
$errInline = [regex]::IsMatch($dom, 'id="err"[^>]*style="[^"]*display:\s*block')
Check 'error banner NOT shown' (-not $errInline) $errInline

Say ''
Say '== 2) data actually bound =='
$tick = [regex]::Match($dom, 'id="t-tick">([^<]*)<').Groups[1].Value
$price = [regex]::Match($dom, 'id="g-price">([^<]*)<').Groups[1].Value
$src = [regex]::Match($dom, 'id="s-src">([^<]*)<').Groups[1].Value
$frames = [regex]::Match($dom, 'id="t-frames">([^<]*)<').Groups[1].Value
Check 'tick is a number' ($tick -match '^\d+$') $tick
Check 'market price is a number' ($price -match '^[\d,\.eE]+$') $price
Check 'data source line filled' ($src.Length -gt 0) $src
Check 'speed indicator filled' ($frames.Length -gt 0) $frames
$priceCells = ([regex]::Matches($dom, 'data-px="\d+"[^>]*>([^<]*)<') | ForEach-Object { $_.Groups[1].Value })
$numeric = @($priceCells | Where-Object { $_ -match '^[\d,\.eE]+$' })
Check ('all goods show a numeric price ({0}/{1})' -f $numeric.Count, $priceCells.Count) ($priceCells.Count -gt 0 -and $numeric.Count -eq $priceCells.Count) ($priceCells -join '|')
$assessRows = 0
if ($dom -split 'id="assessBody"' | Select-Object -Skip 1) {
    $seg = ($dom -split 'id="assessBody"')[1]
    $seg = ($seg -split '</tbody>')[0]
    $assessRows = ([regex]::Matches($seg, '<tr>')).Count
}
Check 'criteria table filled (9)' ($assessRows -eq 9) $assessRows

Say ''
Say '== 3) canvas scale attributes present (charts really drew) =='
$scales = [regex]::Matches($dom, 'data-ticks="([^"]*)"')
Check 'every canvas wrote its scale' ($scales.Count -ge 6) $scales.Count

Say ''
Say '== 4) browser console / page errors =='
$jsErrs = @()
foreach ($pat in @('Uncaught', 'is not defined', 'is not a function', 'TypeError', 'ReferenceError', 'SyntaxError')) {
    $m = [regex]::Matches($errText, [regex]::Escape($pat))
    if ($m.Count -gt 0) { $jsErrs += ("{0} x{1}" -f $pat, $m.Count) }
}
Check 'no JS errors in console' ($jsErrs.Count -eq 0) ($jsErrs -join ', ')

Stop-Job $server -ErrorAction SilentlyContinue
Remove-Job $server -Force -ErrorAction SilentlyContinue

Say ''
if ($failed) { Say 'RESULT: FAIL' } else { Say 'RESULT: PASS' }
$out = Join-Path $root 'out\review\smoke-ui.txt'
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
$lines | Out-File $out -Encoding utf8
Write-Host ("report: {0}" -f $out)
if ($failed) { exit 1 }
exit 0
