# tools/verify_scale_freeze.ps1 -- prove that hiding series does NOT change the axis scale.
# ASCII-ONLY on purpose (PowerShell 5.1 reads .ps1 as ANSI without a BOM).
#
# Method: the mockup writes the axis scale into the canvas dataset
#   data-ymin / data-ymax / data-ticks
# so it can be read from --dump-dom. We dump the DOM for several ?hide= counts and
# compare those attributes. Frozen scale  =>  identical attributes for every case.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\verify_scale_freeze.ps1
$ErrorActionPreference = 'Stop'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$base = 'file:///D:/Code/YehenalaMarket/web/mockup.html'
$common = @('--headless=new','--disable-gpu','--virtual-time-budget=8000','--no-first-run','--no-default-browser-check')
$tmp = Join-Path $env:TEMP 'scalefreeze'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# percent-encoded good names, in the order they appear in the mockup
$G = @(
  '%E8%B0%B7%E7%89%A9',                                                          # grain
  '%E5%8A%A0%E5%B7%A5%E9%A3%9F%E5%93%81',                                        # processed food
  '%E7%BB%87%E7%89%A9',                                                          # fabric
  '%E6%9C%8D%E8%A3%85',                                                          # clothes
  '%E9%AB%98%E6%A1%A3%E6%9C%8D%E8%A3%85',                                        # luxury clothes
  '%E7%85%A4',                                                                   # coal
  '%E9%93%81',                                                                   # iron
  '%E9%92%A2',                                                                   # steel
  '%E5%B7%A5%E5%85%B7',                                                          # tools
  '%E4%BD%8F%E6%88%BF',                                                          # housing
  '%E5%BB%BA%E9%80%A0%E5%8A%9B',                                                 # construction power
  '%E9%87%91%E7%9F%BF',                                                          # gold mine
  '%E4%B8%AD%E5%A4%AE%E9%93%B6%E8%A1%8C',                                        # central bank
  '%E4%BB%93%E5%BA%93'                                                           # warehouse
)

$cases = @(
  @{ name='hide-0';  n=0 },
  @{ name='hide-1';  n=1 },
  @{ name='hide-5';  n=5 },
  @{ name='hide-9';  n=9 },
  @{ name='hide-13'; n=13 }
)

$results = @()
foreach ($c in $cases) {
    $url = $base
    if ($c.n -gt 0) { $url = $base + '?hide=' + (($G[0..($c.n-1)]) -join ',') }
    $file = Join-Path $tmp ($c.name + '.html')
    Remove-Item $file -Force -ErrorAction SilentlyContinue
    & cmd /c "`"$edge`" $($common -join ' ') --dump-dom `"$url`" > `"$file`" 2>nul"
    # DSH Desktop / Edge are GUI-subsystem binaries: cmd returns before the handle is
    # released, so wait for the file to be readable (same trap documented in the repo).
    $d = $null
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        try { $d = [System.IO.File]::ReadAllText($file, [System.Text.Encoding]::UTF8); break }
        catch { }
    }
    if (-not $d) { throw "could not read $file (still locked)" }
    $canvas = [regex]::Match($d, '<canvas id="c-all"[^>]*>').Value
    $ymin = [regex]::Match($canvas, 'data-ymin="([^"]*)"').Groups[1].Value
    $ymax = [regex]::Match($canvas, 'data-ymax="([^"]*)"').Groups[1].Value
    $tick = [regex]::Match($canvas, 'data-ticks="([^"]*)"').Groups[1].Value
    $shown = ([regex]::Matches($d, 'class="on" data-lg')).Count
    $results += [pscustomobject]@{ case=$c.name; shownLegend=$shown; ymin=$ymin; ymax=$ymax; ticks=$tick }
}

Write-Host 'case       legend-on   ymin            ymax            ticks'
foreach ($r in $results) {
    Write-Host ("{0,-10} {1,-11} {2,-15} {3,-15} {4}" -f $r.case, $r.shownLegend, $r.ymin, $r.ymax, $r.ticks)
}
Write-Host ''
$distinct = ($results | ForEach-Object { "$($_.ymin)/$($_.ymax)/$($_.ticks)" } | Sort-Object -Unique)
Write-Host ("distinct scales = {0}   (1 = FROZEN, hide does not move the axis)" -f $distinct.Count)
if ($distinct.Count -eq 1) { Write-Host 'RESULT: PASS' -ForegroundColor Green }
else { Write-Host 'RESULT: FAIL - the axis moves when series are hidden' -ForegroundColor Red }
