# rerun-part2.ps1 -- remaining evidence: first-zero bisection, determinism,
#                     diag_govcash, audit tests, C++/JS verifiers.
# Split out of rerun-all.ps1 so each phase can be timed and re-run on its own.
$ErrorActionPreference = 'Continue'
$root = 'D:\Code\YehenalaMarket'
$gosim = Join-Path $root 'gosim'
$outv = Join-Path $root 'out\verdict'
$bind = Join-Path $root 'out\bin'
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Set-Text($path, $text) { [System.IO.File]::WriteAllText($path, $text, $utf8) }
function HR($t) { return ("`r`n" + ('=' * 78) + "`r`n" + $t + "`r`n" + ('=' * 78)) }

$env:GOROOT = 'D:\Code\Go'; $env:GOCACHE = Join-Path $root 'out\gocache'
$env:GOPATH = Join-Path $root 'out\gopath'; $env:GOTMPDIR = Join-Path $root 'out\gotmp'
$env:PATH = "D:\Code\Go\bin;$env:PATH"
$sim = Join-Path $bind 'market-sim.exe'

function Parse-Run($o) {
  $res = @()
  foreach ($ln in ($o -split "`n")) {
    $t = @($ln.Trim() -split '\s+' | Where-Object { $_ -ne '' })
    if ($t.Count -ne 9) { continue }
    $ok = $true
    foreach ($tok in $t) { $d = 0.0; if (-not [double]::TryParse($tok, [ref]$d)) { $ok = $false; break } }
    if (-not $ok) { continue }
    $res += New-Object psobject -Property @{ Tick = [int]$t[0]; Pop = [double]$t[1]; Levels = [double]$t[2]; GovCash = [double]$t[4] }
  }
  return $res
}
function Last-Row($a) { $r = Parse-Run (& $sim @a 2>&1 | Out-String); return $r[$r.Count - 1] }

# -- A. first-zero bisection
$sw = [Diagnostics.Stopwatch]::StartNew()
$bz = "# first zero tick, found by bisection (predicate: total levels at tick N == 0)`r`n"
$bz += "# method: last row of the time-series table of 'market-sim -ticks N' is tick N;`r`n"
$bz += "#         levels are monotone non-increasing after the collapse starts.`r`n"
foreach ($case in @( @{ n = 'default layout (uniform 5 levels), t=0.10'; a = @() },
                     @{ n = 'default layout (uniform 5 levels), t=0.00'; a = @('-tax','0.00') },
                     @{ n = 'default layout (uniform 5 levels), t=2.00'; a = @('-tax','2.00') },
                     @{ n = 'material-balance layout (-init-level 0), t=0.10'; a = @('-init-level','0') },
                     @{ n = 'material-balance + privatize, t=0.10'; a = @('-init-level','0','-privatize') } )) {
  $hi = 4096; $lo = 1; $runs = 0
  $top = Last-Row (@('-ticks', "$hi") + $case.a); $runs++
  if ($top.Levels -gt 0) { $bz += "`r`n$($case.n): levels at tick $hi = $($top.Levels) > 0 -> no zero crossing`r`n"; continue }
  $mid = 0
  # NOTE: [int] in PowerShell ROUNDS (banker's rounding), so [int](893.5) = 894 and the
  # loop would stop making progress. Use [math]::Floor explicitly.
  while ($lo -lt $hi -and $runs -lt 40) {
    $mid = [int][math]::Floor(($lo + $hi) / 2)
    $lv = (Last-Row (@('-ticks', "$mid") + $case.a)).Levels; $runs++
    if ($lv -le 0) { $hi = $mid } else { $lo = $mid + 1 }
    Write-Host "  [$($case.n)] lo=$lo hi=$hi (levels@$mid=$lv)"
  }
  $z = $lo
  $before = (Last-Row (@('-ticks', "$($z-1)") + $case.a)).Levels; $runs++
  $bz += "`r`n$($case.n)`r`n  first zero tick = $z   (levels at tick $($z-1) = $before)   [sim runs = $runs]`r`n"
}
Set-Text (Join-Path $outv 'first-zero-bisect.txt') $bz
Write-Host ("A. first-zero bisection done in {0:N1}s" -f $sw.Elapsed.TotalSeconds)

# -- B. determinism
$sw.Restart()
$det = "# bit-reproducibility: same command executed 5 times (-ticks 52, defaults, pop 10m, t=0.10)`r`n"
$vals = @()
for ($i = 1; $i -le 5; $i++) {
  $r = Last-Row @('-ticks','52')
  $vals += $r.GovCash
  $det += "  run $i : tick 52  levels = $($r.Levels)  pop = $($r.Pop)  gov_cash = $($r.GovCash)`r`n"
}
$mn = ($vals | Measure-Object -Minimum).Minimum
$mx = ($vals | Measure-Object -Maximum).Maximum
$rel = if ($mx -ne 0) { [math]::Round(100 * ($mx - $mn) / [math]::Abs($mx), 4) } else { 0 }
$det += "`r`n  gov_cash min    = $mn`r`n  gov_cash max    = $mx`r`n  spread          = $($mx - $mn)`r`n  relative spread = $rel %`r`n"
$det += "`r`n  discrete quantities (levels/pop) across runs: " + (($vals | ForEach-Object { $_ }) -join ', ') + "`r`n"
$det += "  (ACTIVE.md fact 5 claims ~0.01%~0.04% jitter on money with identical discrete quantities)`r`n"
Set-Text (Join-Path $outv 'determinism.txt') $det
Write-Host ("B. determinism done in {0:N1}s" -f $sw.Elapsed.TotalSeconds)

# -- C. diag_govcash
$sw.Restart()
$o = (& (Join-Path $bind 'diag_govcash.exe') 2>&1 | Out-String)
Set-Text (Join-Path $outv 'diag_govcash.txt') ((HR '# out/bin/diag_govcash.exe') + "`r`n" + $o)
Write-Host ("C. diag_govcash done in {0:N1}s" -f $sw.Elapsed.TotalSeconds)

# -- D. audit tests (raw)
$sw.Restart()
Push-Location $gosim
$env:DSH_AUDIT = '1'
foreach ($tc in @(
  @{ t = 'TestAuditGovDecomp'; f = 'audit-govdecomp.txt' },
  @{ t = 'TestAuditSurvival';  f = 'audit-survival-600.txt' },
  @{ t = 'TestAuditLayout';    f = 'audit-layout.txt' },
  @{ t = 'TestAuditStartDiag'; f = 'audit-start-diag.txt' } )) {
  $o = (& go test ./internal/sim/ -run $tc.t -v -count=1 2>&1 | Out-String)
  Set-Text (Join-Path $outv $tc.f) ((HR "# DSH_AUDIT=1 go test ./internal/sim/ -run $($tc.t) -v -count=1") + "`r`n" + $o)
  Write-Host "  $($tc.t) done"
}
$o  = (& go test ./... -count=1 2>&1 | Out-String)
$o2 = (& go test ./internal/sim/ -v -count=1 2>&1 | Out-String)
Set-Text (Join-Path $outv 'audit-suite.txt') ((HR '# DSH_AUDIT=1 go test ./... -count=1') + "`r`n" + $o + "`r`n" + (HR '# DSH_AUDIT=1 go test ./internal/sim/ -v -count=1') + "`r`n" + $o2)
$env:DSH_AUDIT = ''
Pop-Location
Write-Host ("D. audit tests done in {0:N1}s" -f $sw.Elapsed.TotalSeconds)

# -- E. C++ verifier + JS claims verifier
$sw.Restart()
& 'D:\Code\mingw64\bin\g++.exe' -O2 -std=c++17 -Wall -o (Join-Path $bind 'contract_check.exe') (Join-Path $root 'tools\cpp\contract_check.cpp') 2>&1 | Out-Null
$cpp = (& (Join-Path $bind 'contract_check.exe') 2>&1 | Out-String)
$cppExit = $LASTEXITCODE
Set-Text (Join-Path $outv 'contract-check-cpp.txt') ((HR "# g++ -O2 -std=c++17 -Wall -o out/bin/contract_check.exe tools/cpp/contract_check.cpp`r`n# exit=$cppExit") + "`r`n" + $cpp)
$env:ELECTRON_RUN_AS_NODE = '1'
$js = (& 'D:\DSH\DSH Desktop\DSH Desktop.exe' (Join-Path $root 'tools\verify_claims.js') 2>&1 | Out-String)
Write-Host ("E. C++/JS verifiers done in {0:N1}s (cpp exit=$cppExit)" -f $sw.Elapsed.TotalSeconds)

"--- products ---"
Get-ChildItem $outv | Sort-Object Name | ForEach-Object { "  {0,-36} {1,10:N0} B" -f $_.Name, $_.Length }
