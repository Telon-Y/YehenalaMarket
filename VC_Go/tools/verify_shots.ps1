# tools/verify_shots.ps1 -- ACCEPTANCE HARNESS: run the page at unlimited speed and archive
# one frame per N ticks, then save the window image. Bounded: it always exits.
#
# Design (why not headless / not CDP):
#   * headless never advances the replay (virtual time does not drive rAF)
#   * CDP is unavailable on this machine (headless refuses extra targets; non-headless
#     refuses the debugging port)
#   so: a REAL window + the page archives itself. The page, opened with
#       ?rate=instant&shots=N
#   advances one tick per frame and stores a canvas snapshot into #filmstrip every N ticks.
#   This script only has to open the window, wait, and copy the window bitmap.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\verify_shots.ps1
#   ... -Shots 2 -WaitSec 20 -OutDir out\review\auto -Port 8787
#
# ASCII-ONLY. Exit code 0 = image saved, 1 = failed.

param(
    [int]$Shots = 2,
    [int]$WaitSec = 18,
    [string]$OutDir = 'out\review\auto',
    [int]$Port = 8787
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class VW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L,T,Rt,B; }
}
"@

$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
$log = Join-Path $out 'verify-shots.txt'
$L = New-Object System.Collections.Generic.List[string]
function Say($s) { $L.Add($s); Write-Host $s }

function ServeAlive() {
    try { $r = Invoke-WebRequest "http://localhost:$Port/" -UseBasicParsing -TimeoutSec 4; return ($r.StatusCode -eq 200) } catch { return $false }
}

# ---- 1) server must be alive (start one if needed) ----
$srv = $null
if ((ServeAlive)) { Say ("server: alive on {0}" -f $Port) }
else {
    Say 'server: not alive -> starting one'
    $go = 'D:\Code\Go\bin\go.exe'
    if (-not (Test-Path $go)) { throw 'go.exe not found' }
    $data = Join-Path $root 'out\webdata'
    $srv = Start-Process -FilePath $go `
        -ArgumentList @('run','./cmd/market-sim','-serve-only','-serve',":$Port",'-export-dir',$data) `
        -WorkingDirectory (Join-Path $root 'gosim') -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $out 'serve.log') -RedirectStandardError (Join-Path $out 'serve.err')
    $ok = $false
    for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 500; if ((ServeAlive)) { $ok = $true; break } }
    if (-not $ok) {
        Say 'server: FAILED to come up'
        $L | Out-File $log -Encoding utf8
        throw 'server did not come up'
    }
    Say 'server: started'
}

# ---- 2) open a real window at instant speed with self-archiving ----
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path $edge)) { $edge = 'C:\Program Files\Google\Chrome\Application\chrome.exe' }
$url = "http://localhost:$Port/?rate=instant&shots=$Shots"
Say ("open: {0}" -f $url)
$br = Start-Process -FilePath $edge -ArgumentList @(
    '--new-window','--no-first-run','--no-default-browser-check',
    '--window-size=1500,940','--window-position=0,0',$url) -PassThru

# wait for a window to appear (bounded)
$w = $null
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 500
    $w = Get-Process msedge,chrome -ErrorAction SilentlyContinue |
         Where-Object { $_.MainWindowHandle -ne 0 } |
         Sort-Object { $r = New-Object VW+R; [VW]::GetWindowRect($_.MainWindowHandle,[ref]$r) | Out-Null; ($r.Rt-$r.L)*($r.B-$r.T) } -Descending |
         Select-Object -First 1
    if ($w) { break }
}
if (-not $w) {
    Say 'window: NONE appeared within 20s -> aborting (nothing captured)'
    $L | Out-File $log -Encoding utf8
    if ($srv) { Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue }
    exit 1
}
[VW]::ShowWindow($w.MainWindowHandle, 9) | Out-Null
[VW]::SetForegroundWindow($w.MainWindowHandle) | Out-Null
$rr = New-Object VW+R; [VW]::GetWindowRect($w.MainWindowHandle, [ref]$rr) | Out-Null
Say ("window: pid={0} title='{1}' {2}x{3}" -f $w.Id, $w.MainWindowTitle, ($rr.Rt-$rr.L), ($rr.B-$rr.T))

# ---- 3) let it run, then capture (bounded) ----
Say ("waiting {0}s for the replay to advance..." -f $WaitSec)
Start-Sleep -Seconds $WaitSec

$f = Join-Path $out 'window.png'
Remove-Item $f -Force -ErrorAction SilentlyContinue
$rr = New-Object VW+R; [VW]::GetWindowRect($w.MainWindowHandle, [ref]$rr) | Out-Null
$wd = $rr.Rt - $rr.L; $ht = $rr.B - $rr.T
if ($wd -le 0 -or $ht -le 0) { Say 'window: bad rect'; $L | Out-File $log -Encoding utf8; exit 1 }
$bm = New-Object System.Drawing.Bitmap $wd, $ht
$g = [System.Drawing.Graphics]::FromImage($bm)
$dc = $g.GetHdc()
$okpw = [VW]::PrintWindow($w.MainWindowHandle, $dc, 2)
$g.ReleaseHdc($dc); $g.Dispose()
$bm.Save($f, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()
Say ("capture: {0}  ({1} bytes, PrintWindow={2})" -f $f, (Get-Item $f).Length, $okpw)

$L | Out-File $log -Encoding utf8
Write-Host ("log: {0}" -f $log)
Write-Host ("NOTE: browser window and server are LEFT RUNNING. Server pid={0}" -f $(if ($srv) { $srv.Id } else { 'reused' }))
exit 0
