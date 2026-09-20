# tools/push-vc-go.ps1 -- publish THIS workspace into the VC_Go/ subdirectory of the
# upstream repository, WITHOUT touching the repository root (the C++ / Raylib project).
#
# Why a dedicated script:
#   Upstream `Telon-Y/YehenalaMarket` is the C++ project. Its repository root holds the
#   C++ sources (local_market.cpp, national_market.cpp, ui_*.cpp, CMakeLists.txt, ...) and
#   the Go kernel lives in the `VC_Go/` subdirectory. This workspace IS `VC_Go/`.
#
#   A normal `git push` would publish this tree as the repository ROOT and DELETE the C++
#   project. So instead this script:
#     1) takes the local commit tree (the Go project),
#     2) wraps it one level under `VC_Go/`,
#     3) grafts it onto the CURRENT remote branch tip as a child commit
#        (the other 120 root entries are copied over unchanged),
#     4) pushes that commit -- a FAST-FORWARD, so no `--force` and nothing else is lost.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\push-vc-go.ps1 -DryRun     # preview only
#   powershell -ExecutionPolicy Bypass -File tools\push-vc-go.ps1             # push
#   ... -Branch main -Message "VC_Go: ..."                                    # customise
#
# Prerequisites: a proxy may be required; if github.com is unreachable configure it with
#   git config --global http.proxy http://127.0.0.1:7897   (see the local setup).
#
# Exit code: 0 = pushed (or dry-run ok), 1 = failed.
#
# NOTE: ASCII-ONLY on purpose (Windows PowerShell 5.1 reads .ps1 as ANSI without a BOM).

param(
    [string]$Remote  = 'origin',
    [string]$Branch  = 'main',
    [string]$Message = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

# helper: run git with stdin redirected from a file (raw bytes; PowerShell pipes add CR)
function GitStdinFile {
    param([string]$GitArgs, [string]$File)
    $o = & cmd /c "git $GitArgs < `"$File`"" 2>&1 | Out-String
    return $o.Trim()
}

# helper: write a file as UTF-8 WITHOUT BOM and with LF only
function WriteLfFile {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host '== push VC_Go/ to upstream ==' -ForegroundColor Cyan
Write-Host ("remote/branch : {0}/{1}" -f $Remote, $Branch)

# ---- 0) sanity: the working tree must be committed ----
$dirty = (git status --porcelain)
if ($dirty) {
    if (-not $DryRun) { throw "working tree is dirty: commit first (git add -A; git commit)." }
    Write-Host 'working tree is dirty (dry-run continues with HEAD only)' -ForegroundColor Yellow
}
$subTree = (git rev-parse 'HEAD^{tree}').Trim()
Write-Host ("local tree   : {0}" -f $subTree)

# ---- 1) fetch the branch tip (graft parent) ----
Write-Host '-- fetch --' -ForegroundColor Cyan
& cmd /c "git fetch --depth=1 $Remote $Branch 2>&1" | Select-Object -Last 3 | ForEach-Object { Write-Host "   $_" }
$baseRef = "$Remote/$Branch"
$base = (git rev-parse $baseRef).Trim()
Write-Host ("parent commit: {0}" -f $base)

# ---- 2) build the new top tree: remote root, with VC_Go replaced ----
$lines = (git ls-tree $base) -split "`r?`n" | Where-Object { $_ -match '\S' }
$newTop = New-Object System.Collections.Generic.List[string]
$found = $false
foreach ($l in $lines) {
    if ($l -match '\bVC_Go$') { $newTop.Add("040000 tree $subTree" + [char]9 + 'VC_Go'); $found = $true }
    else { $newTop.Add($l) }
}
if (-not $found) { $newTop.Add("040000 tree $subTree" + [char]9 + 'VC_Go') }
Write-Host ("root entries : {0} -> {1} (VC_Go {2})" -f $lines.Count, $newTop.Count, $(if ($found) {'replaced'} else {'added'}))
$tmpTree = Join-Path $env:TEMP 'pushvcgo_tree.txt'
WriteLfFile -Path $tmpTree -Text (($newTop -join [char]10) + [char]10)
$newTopTree = GitStdinFile -GitArgs 'mktree' -File $tmpTree
if ($newTopTree -notmatch '^[0-9a-f]{40}$') { throw "git mktree failed: $newTopTree" }
Write-Host ("new root tree: {0}" -f $newTopTree)

# ---- 3) commit as a CHILD of the remote tip (fast-forward) ----
if ([string]::IsNullOrWhiteSpace($Message)) {
    $Message = ("VC_Go: sync Go kernel snapshot ({0})" -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
}
$tmpMsg = Join-Path $env:TEMP 'pushvcgo_msg.txt'
WriteLfFile -Path $tmpMsg -Text ($Message + [char]10)
$commit = GitStdinFile -GitArgs ("commit-tree $newTopTree -p $base -F `"$tmpMsg`"") -File 'NUL'
if ($commit -notmatch '^[0-9a-f]{40}$') { throw "git commit-tree failed: $commit" }
Write-Host ("new commit   : {0}" -f $commit)

# ---- 4) preview: what changes ----
Write-Host '-- changes (root paths other than VC_Go must be EMPTY) --' -ForegroundColor Cyan
$changed = git diff --name-only $base $commit
$outside = $changed | Where-Object { $_ -notmatch '^"?VC_Go/' }
if ($outside) {
    Write-Host 'UNEXPECTED changes outside VC_Go/:' -ForegroundColor Red
    $outside | Select-Object -First 20 | ForEach-Object { Write-Host "   $_" }
    throw 'refusing to push: changes outside VC_Go/'
}
Write-Host ("   {0} files changed, all under VC_Go/" -f ($changed | Measure-Object).Count) -ForegroundColor Green

if ($DryRun) {
    Write-Host ''
    Write-Host 'DRY RUN: nothing pushed.' -ForegroundColor Yellow
    Write-Host ("would push: {0} -> {1} {2}" -f $commit.Substring(0,7), $Remote, $Branch)
    exit 0
}

# ---- 5) push the fast-forward ----
Write-Host '-- push --' -ForegroundColor Cyan
$env:GIT_TERMINAL_PROMPT = '0'
$pushOut = & cmd /c "git push $Remote ${commit}:refs/heads/$Branch 2>&1" | Out-String
Write-Host $pushOut.Trim()
if ($LASTEXITCODE -ne 0) { throw 'push failed' }

Write-Host ''
Write-Host ("OK: pushed {0} to {1}/{2}" -f $commit.Substring(0,7), $Remote, $Branch) -ForegroundColor Green
Write-Host ("view: https://github.com/Telon-Y/YehenalaMarket/tree/{0}/VC_Go" -f $Branch)
exit 0
