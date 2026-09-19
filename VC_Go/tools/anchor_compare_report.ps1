# 解析 out/anchor_compare/*.txt（不重跑仿真），输出对照表
$ErrorActionPreference = 'Stop'
$dir = 'D:\Code\YehenalaMarket\out\anchor_compare'
$rows = @()
foreach ($f in @('cost', 'A', "C'", "A_C'")) {
  $p = Join-Path $dir "$f.txt"
  if (-not (Test-Path $p)) { continue }
  $l = [System.IO.File]::ReadAllLines($p, [System.Text.Encoding]::UTF8)

  # 时序摘要：最后一行（tick=10000）
  $ts = ($l | Select-String -Pattern '^\s+10000\s+' | Select-Object -First 1).Line
  $tf = $ts -split '\s+' | Where-Object { $_ -ne '' }
  $levels = [double]$tf[2]

  # 终态：人口 / 政府现金池
  $popLine = ($l | Select-String -Pattern '^人口 ' | Select-Object -First 1).Line
  $pop = [double](($popLine -split '\s+')[1])
  $gov = [double](($popLine -replace '.*政府现金池\s+', '') -replace '\s+.*', '')

  # 终态价格表：P/Pcost 分布
  $start = ($l | Select-String -Pattern '^--- 终态' | Select-Object -First 1).LineNumber
  $vals = @()
  for ($i = $start; $i -lt $l.Count; $i++) {
    if ($l[$i] -match '^人口 ') { break }
    if ($l[$i] -match '^\S+\s+[\d.]+\s+([\d.]+)\s+(-?[\d.]+)%\s*$') { $vals += [double]$Matches[1] }
  }
  $ceilN = @($vals | Where-Object { $_ -ge 4.999 }).Count
  $floorN = @($vals | Where-Object { $_ -le 0.201 }).Count
  $midN = @($vals | Where-Object { $_ -gt 0.201 -and $_ -lt 4.999 }).Count

  # 判据
  $sum = (($l | Select-String -Pattern '汇总：通过' | Select-Object -First 1).Line -replace '汇总：', '').Trim()
  $a2 = ($l | Select-String -Pattern 'max=.*加权=' | Select-Object -First 1).Line
  $a2max = if ($a2 -match 'max=([^ ]+)') { $Matches[1] } else { '?' }
  $a2w = if ($a2 -match '加权=([^ ]+)') { $Matches[1] } else { '?' }

  $rows += [pscustomobject]@{
    方案 = $f; 末态人口 = $pop; 总级数 = $levels; 政府现金池 = $gov
    贴顶 = $ceilN; 贴底 = $floorN; 带内 = $midN; A2_max = $a2max; A2_加权 = $a2w; 汇总 = $sum
  }
}
$rows | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
