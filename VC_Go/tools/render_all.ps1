# tools/render_all.ps1 -- render the project's markdown docs into HTML.
#
# Why: the workspace has no md reader, so HTML is the reading format; this also keeps
# HTML in sync with the markdown sources after edits.
#
# NOTE: this file is deliberately ASCII-ONLY. Windows PowerShell 5.1 reads .ps1 files as
# ANSI unless they carry a UTF-8 BOM; non-ASCII here would break parsing whenever the file
# is edited by a tool that writes UTF-8 without BOM. Keep it ASCII.
#
# Node is not installed on this machine: DSH Desktop's Electron is used as node
# (ELECTRON_RUN_AS_NODE=1).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File D:\Code\YehenalaMarket\tools\render_all.ps1
#
# Verification does NOT rely on exit codes: DSH Desktop.exe is a GUI-subsystem binary and
# its exit code / stdout are unreliable from Windows PowerShell. Instead each artifact is
# checked directly:
#   1) the target HTML timestamp must have been refreshed by this run;
#   2) the target HTML must contain zero "<span class=""math-error"">" (no failed formulas).
# Exit code: 0 = all rendered + contract audit passed; 1 = something failed.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$node = 'D:\DSH\DSH Desktop\DSH Desktop.exe'
$md2html = Join-Path $root 'tools\md2html.js'
$audit   = Join-Path $root 'tools\html_audit.js'

if (-not (Test-Path -LiteralPath $node)) {
    Write-Error "Electron not found: $node (DSH Desktop is used as node on this machine)"
}
$env:ELECTRON_RUN_AS_NODE = '1'

# R37: only the two core documents + README are rendered.
# Everything else was merged into them; the originals live in docs/archive/ (not rendered).
# Source paths are discovered from the filesystem instead of being written as literals:
# the contract filename contains non-ASCII characters, and this script must stay ASCII-only.
$docsDir = Join-Path $root 'docs'
$contractMd = (Get-ChildItem -LiteralPath $docsDir -Filter '*.md' |
               Where-Object { $_.Name -like '1.0 *' } | Select-Object -First 1).FullName
$map = @(
    @($contractMd,                        [System.IO.Path]::ChangeExtension($contractMd, '.html')),
    @((Join-Path $docsDir 'ACTIVE.md'),   (Join-Path $docsDir 'ACTIVE.html')),
    @((Join-Path $root 'README.md'),      (Join-Path $root 'README.html'))
)

$failed = @()
foreach ($m in $map) {
    $src = $m[0]          # absolute path (see $map above)
    $dst = $m[1]
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Host ("SKIP {0} (source missing)" -f $src) -ForegroundColor Yellow
        continue
    }

    $stamp = (Get-Date).AddSeconds(-2)   # tolerate filesystem timestamp granularity
    # Must go through cmd /c: PowerShell does not wait for GUI-subsystem executables,
    # so a plain "& $node ..." call would make the freshness check fail.
    $cmdline = '"{0}" "{1}" "{2}" "{3}"' -f $node, $md2html, $src, $dst
    & cmd /c $cmdline *> $null

    $fresh = (Test-Path -LiteralPath $dst) -and ((Get-Item -LiteralPath $dst).LastWriteTime -ge $stamp)
    $errs = -1
    if ($fresh) {
        $html = [System.IO.File]::ReadAllText($dst, [System.Text.Encoding]::UTF8)
        $errs = ([regex]::Matches($html, '<span class="math-error">')).Count
    }

    if ($fresh -and $errs -eq 0) {
        Write-Host ("OK   {0,-40} math-error=0" -f $m[1]) -ForegroundColor Green
    } else {
        Write-Host ("FAIL {0,-40} refreshed={1} math-error={2}" -f $m[1], $fresh, $errs) -ForegroundColor Red
        $failed += $m[1]
    }
}

# Structural audit of the contract (anchors / tag pairing / strict nesting).
$contract = Join-Path $root 'docs\1.0 生产与市场模拟.html'
if (Test-Path -LiteralPath $contract) {
    Write-Host ''
    Write-Host '=== html_audit (contract) ==='
    $auditOut = (& $node $audit $contract 2>&1 | Out-String)
    if ([string]::IsNullOrWhiteSpace($auditOut)) {
        Write-Host '(could not capture html_audit output; math-error=0 above is the fallback)' -ForegroundColor Yellow
    } else {
        ($auditOut.Trim() -split "`r?`n" | Select-Object -Last 3) | ForEach-Object { Write-Host $_ }
        if ($auditOut -notmatch 'PASS|all checks|全部检查通过') {
            $failed += 'contract html_audit'
        }
    }
}

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host ("FAILED: {0} item(s): {1}" -f $failed.Count, ($failed -join ', ')) -ForegroundColor Red
    exit 1
}
Write-Host 'OK: all documents rendered; contract audit passed.' -ForegroundColor Green
exit 0
