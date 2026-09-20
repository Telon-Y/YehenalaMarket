@echo off
REM ============================================================================
REM  1.1 UI - normal launcher (double-clickable).
REM  Exports 1000 ticks by default, starts the read-only server, opens the browser.
REM
REM  Extra args are forwarded, e.g.:   start.cmd -Ticks 10000 -Port 8790
REM  This file is ASCII-only: cmd.exe reads .cmd as OEM/ANSI, non-ASCII breaks it.
REM ============================================================================
setlocal
cd /d "%~dp0.."

set PS=powershell
where pwsh >nul 2>nul && set PS=pwsh

echo Launching 1.1 UI (1000 ticks, slow-mode entry is start-slow.cmd) ...
%PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" -Ticks 1000 %*

if errorlevel 1 (
  echo.
  echo The launcher reported an error. Press any key to close.
  pause >nul
)
endlocal
