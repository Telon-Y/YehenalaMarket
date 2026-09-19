# 对照实验：§3.4 价格锚定方案 A / C′（均为实验开关，默认关闭）
# 产物：out/anchor_compare/<name>.txt + 控制台汇总表
$ErrorActionPreference = 'Stop'
$root = 'D:\Code\YehenalaMarket'
$env:GOROOT = 'D:\Code\Go'
$env:GOCACHE = "$root\out\gocache"
$env:GOPATH = "$root\out\gopath"
$env:GOTMPDIR = "$root\out\gotmp"
$dir = "$root\out\anchor_compare"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$exe = "$root\out\bin\market-sim.exe"
Push-Location "$root\gosim"
go build -o $exe ./cmd/market-sim
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "build failed" }

$configs = @(
  @{ name = 'cost';        args = @() },
  @{ name = 'A';           args = @('-anchor-expenditure') },
  @{ name = "C'";          args = @('-anchor-derived') },
  @{ name = "A+C'";        args = @('-anchor-expenditure', '-anchor-derived') }
)

$rows = @()
foreach ($c in $configs) {
  $out = "$dir\$($c.name -replace '\+','_').txt"
  & $exe -ticks 10000 @($c.args) *> $out
  $l = [System.IO.File]::ReadAllLines($out, [System.Text.Encoding]::UTF8)
  $last = ($l | Select-String -Pattern '^\s+\d+\s+\d+\s+\d+\s' | Select-Object -Last 1).Line
  $f = $last -split '\s+' | Where-Object { $_ -ne '' }
  # f: tick 人口 总级数 建造力 政府现金池 税收 经营净额 债务 债务上限
  $pop = [double]$f[1]; $lv = [double]$f[2]; $gov = [double]$f[4]
  $sum = ($l | Select-String -Pattern '汇总：通过' | Select-Object -First 1).Line
  $a2 = ($l | Select-String -Pattern 'min=.*max=' | Select-Object -First 1).Line
  $a1 = ($l | Select-String -Pattern '最大贴边率' | Select-Object -First 1).Line
  # 终态价格贴边统计：P/Pcost == 5.000 或 0.200 的商品数
  $prices = @()
  foreach ($line in $l) {
    if ($line -match '^\S+\s+([\d.]+)\s+([\d.]+)\s+(-?[\d.]+)%\s*$') { $prices += [double]$Matches[2] }
  }
  $ceilN = ($prices | Where-Object { $_ -ge 4.999 }).Count
  $floorN = ($prices | Where-Object { $_ -le 0.201 }).Count
  $rows += [pscustomobject]@{
    方案 = $c.name; 人口 = $pop; 总级数 = $lv; 政府现金池 = $gov
    贴顶商品数 = $ceilN; 贴底商品数 = $floorN
    A1 = ($a1 -replace '.*最大贴边率=','').Trim(); A2 = ($a2 -replace '.*max=','').Trim(); 汇总 = ($sum -replace '汇总：','').Trim()
  }
}
Pop-Location
$rows | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$rows | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$dir\summary.json" -Encoding UTF8
Write-Host "产物目录: $dir"
