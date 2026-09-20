@echo off
REM ============================================================================
REM  1.1 UI - STEP mode entry (double-clickable).
REM  For watching the simulation advance slowly, tick by tick.
REM
REM  Differences from start.cmd:
REM    - default 300 ticks (short run finishes fast, so stepping stays meaningful)
REM    - single-tick export (no sampling), so every row is a real tick
REM    - the page opens in the middle of the sequence? no: it starts at tick 1
REM      and the UI opens and presses PAUSE for you (see -StartPaused below).
REM
REM  In the page, use:
REM    [space]      play / pause
REM    [.] and [,]  step +1 / -1 tick
REM    x1/8         slow automatic advance (one tick every 8 steps)
REM    the slider   jump anywhere in the recorded range
REM
REM  Extra args forwarded, e.g.:  start-slow.cmd -Ticks 1000 -Port 8790
REM ============================================================================
setlocal
cd /d "%~dp0.."

set PS=powershell
where pwsh >nul 2>nul && set PS=pwsh

echo Launching 1.1 UI in STEP mode (300 ticks, paused) ...
%PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" -Ticks 300 -ExportEvery 1 -StartPaused %*

if errorlevel 1 (
  echo.
  echo The launcher reported an error. Press any key to close.
  pause >nul
)
endlocal
