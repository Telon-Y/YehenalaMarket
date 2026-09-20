# tools/live_shots.ps1 -- open a REAL browser window, let the replay run, screenshot over time.
#
# Why a separate background script: capturing the live playback needs a real (non-headless)
# window -- headless never advances the replay because virtual time does not drive
# requestAnimationFrame. This script:
#   1. starts the read-only server (or reuses one already listening)
#   2. opens a real Edge/Chrome window at the page
#   3. screenshots the primary screen every -IntervalSec seconds, -Shots times
#   4. writes a small log with the tick read from the DOM is NOT possible here, so instead
#      it records file sizes (a blank/unchanged frame shows up as an identical size)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\live_shots.ps1
#   ... -IntervalSec 10 -Shots 6 -OutDir out\review\live -Speed 5 -Port 8787
#
# ASCII-ONLY (PowerShell 5.1 reads .ps1 as ANSI without a BOM).
# Exit code: 0 = shots taken, 1 = failed.

param(
    [int]$Port = 8787,
    [int]$IntervalSec = 10,
    [int]$Shots = 6,
    [string]$OutDir = 'out\review\live',
    [double]$Speed = 1,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class LiveWin {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path $edge)) {
    $alt = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
    if (Test-Path $alt) { $edge = $alt } else { throw 'no Edge/Chrome found' }
}

$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
$log = Join-Path $out 'live-shots.txt'
$lines = New-Object System.Collections.Generic.List[string]
function Say($s) { $lines.Add($s); Write-Host $s }

# ---- 1) make sure something serves the page ----
$serving = $false
try {
    $r = Invoke-WebRequest "http://localhost:$Port/" -UseBasicParsing -TimeoutSec 4
    if ($r.StatusCode -eq 200) { $serving = $true; Say ("server: reusing existing on port {0}" -f $Port) }
} catch { }
$srv = $null
if (-not $serving) {
    $go = 'D:\Code\Go\bin\go.exe'
    if (-not (Test-Path $go)) { throw 'go.exe not found; start the server yourself or run tools\start.ps1' }
    $data = Join-Path $root 'out\webdata'
    if (-not (Test-Path (Join-Path $data 'snapshots.jsonl'))) { throw "no data in $data; run tools\start.ps1 once" }
    $srv = Start-Process -FilePath $go `
        -ArgumentList @('run','./cmd/market-sim','-serve-only','-serve',":$Port",'-export-dir',$data) `
        -WorkingDirectory (Join-Path $root 'gosim') -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $out 'serve.log') -RedirectStandardError (Join-Path $out 'serve.err')
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        try { $r = Invoke-WebRequest "http://localhost:$Port/" -UseBasicParsing -TimeoutSec 3; if ($r.StatusCode -eq 200) { $serving = $true; break } } catch { }
    }
    if (-not $serving) { throw "server did not come up on port $Port" }
    Say ("server: started by this script on port {0}" -f $Port)
}

# ---- 2) find the browser window (by title / biggest area), not "any window" ----
$url = "http://localhost:$Port/"
if ($Speed -ne 1) { $url += ("?speed={0}" -f $Speed) }
$br = $null
if (-not $NoLaunch) {
    $br = Start-Process -FilePath $edge -ArgumentList @(
        '--new-window','--no-first-run','--no-default-browser-check',
        '--window-size=1680,900','--window-position=0,0',$url) -PassThru
    Start-Sleep -Seconds 8
}

function FindBrowserWindow() {
    $cands = Get-Process msedge,chrome -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 -and [LiveWin]::IsWindowVisible($_.MainWindowHandle) }
    # prefer a window whose title contains the page name (unicode-escaped, keep file ASCII)
    $hit = $cands | Where-Object { $_.MainWindowTitle -match '\u53f6\u8d6b\u90a3\u62c9|Yehenala' } | Select-Object -First 1
    if (-not $hit) {
        # fallback: take the largest window
        $scored = foreach ($c in $cands) {
            $r = New-Object LiveWin+RECT
            if ([LiveWin]::GetWindowRect($c.MainWindowHandle, [ref]$r)) {
                [pscustomobject]@{ P = $c; Area = ($r.Right - $r.Left) * ($r.Bottom - $r.Top) }
            }
        }
        $hit = ($scored | Sort-Object Area -Descending | Select-Object -First 1).P
    }
    return $hit
}

$w = FindBrowserWindow
if ($w) {
    [LiveWin]::ShowWindow($w.MainWindowHandle, 9) | Out-Null
    [LiveWin]::SetForegroundWindow($w.MainWindowHandle) | Out-Null
    $r = New-Object LiveWin+RECT
    [LiveWin]::GetWindowRect($w.MainWindowHandle, [ref]$r) | Out-Null
    Say ("window: pid={0} title='{1}'  rect={2},{3} {4}x{5}" -f $w.Id, $w.MainWindowTitle,
         $r.Left, $r.Top, ($r.Right - $r.Left), ($r.Bottom - $r.Top))
} else {
    Say 'window: NO browser window found - aborting instead of capturing the wrong image'
    $lines | Out-File $log -Encoding utf8
    exit 1
}

# ---- 3) capture THIS window only ----
Say ("interval={0}s   shots={1}   speed=x{2}" -f $IntervalSec, $Shots, $Speed)
Say ''
function ShotWindow($hwnd, $path) {
    $r = New-Object LiveWin+RECT
    [LiveWin]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $wd = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($wd -le 0 -or $ht -le 0) { throw 'bad window rect' }
    $bm = New-Object System.Drawing.Bitmap $wd, $ht
    $g = [System.Drawing.Graphics]::FromImage($bm)
    $hdc = $g.GetHdc()
    # PrintWindow can grab a BACKGROUND window; fall back to a screen-region copy
    $ok = [LiveWin]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc)
    if (-not $ok) {
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $wd, $ht))
    }
    $g.Dispose()
    $bm.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bm.Dispose()
}

$prevSize = -1
for ($i = 0; $i -lt $Shots; $i++) {
    if ($i -gt 0) { Start-Sleep -Seconds $IntervalSec }
    $name = ("shot-{0:D2}-t{1,4}s.png" -f $i, ($i * $IntervalSec))
    $path = Join-Path $out $name
    Remove-Item $path -Force -ErrorAction SilentlyContinue
    # re-find the window each time in case the handle went stale
    $w2 = FindBrowserWindow
    if ($w2) { $hwnd2 = $w2.MainWindowHandle } else { $hwnd2 = $w.MainWindowHandle }
    ShotWindow $hwnd2 $path
    $sz = (Get-Item $path).Length
    $same = ($sz -eq $prevSize)
    Say ("[{0,2}] t={1,4}s  {2}  {3,9} bytes{4}" -f $i, ($i * $IntervalSec), $name, $sz,
         $(if ($same) { '   <-- identical size (frame may not be updating)' } else { '' }))
    $prevSize = $sz
}

Say ''
Say ("shots dir: {0}" -f $out)
$lines | Out-File $log -Encoding utf8
Write-Host ("log: {0}" -f $log)
if ($srv) { Write-Host ("server pid {0} left running; stop it with: Stop-Process -Id {0}" -f $srv.Id) }
exit 0
