# YehenalaMarket 发布清单

> 本文件是发布包的内容说明模板，由 `tools/release.ps1` 附加版本信息后写入发布包。
> 该脚本**刻意保持 ASCII-only**（Windows PowerShell 5.1 会把无 BOM 的 `.ps1` 按 ANSI 读取，
> 非 ASCII 会破坏解析——同 `tools/render_all.ps1` 的既有约定），故中文说明放在本文件里。

## 入包内容

| 路径 | 内容 |
|------|------|
| `README.md` | 一页索引：文档表 / 复现命令 / 包结构 → 契约条文映射 |
| `docs/` | 契约 `1.0 生产与市场模拟.md` · 状态入口 `ACTIVE.md` · `1.1 图形化界面.md` · `1.2 货币与金融.md` · 渲染产物 `.html` · `docs/archive/` 历史证据 |
| `docs/verdict/` | **三份验收判决书**：`1.0 验收判决书.md`（未通过）· `1.1 验收判决书.md`（不具备验收条件）· `1.2 验收判决书.md`（机制层通过 / 经济效果层未通过） |
| `gosim/` | Go 内核（模块 `yehenala/market`）：`internal/*` 13 个包 + `cmd/` 两个入口 |
| `tools/` | 契约渲染与结构校验（`md2html.js` / `html_audit.js` / `render_all.ps1`）、独立复算探针、`release.ps1` |
| `MingChinese.ttf` | 1.1 界面所需中文字体（24.4 MB） |

## 未入包（内部工作产物）

| 路径 | 原因 |
|------|------|
| `out/` | 证据与构建缓存（约 1.25 GB，其中**证据约 6 MB**）。证据目录 `out/verdict`、`out/verify`、`out/review` 保留在工作区，**不随包发布** |
| `upload/` | **已陈旧的手工副本**（根 `gosim/` 90 个 `.go`，`upload/gosim/` 仅 55 个）。**已由本脚本取代**，不应再手工同步 |
| 任何 `*.exe` | 构建产物，由使用者自行 `go build` |

## 校验命令

```powershell
Set-Location gosim
go build ./... ; go vet ./...            # 均应 exit=0
go test ./... -count=1                    # 全绿
$env:DSH_AUDIT="1"; go test ./... -v -count=1   # 156 PASS / 0 FAIL / 0 SKIP

go run ./cmd/market-sim -ticks 10000              # 5/9，见 docs/verdict/1.0 验收判决书.md
go run ./cmd/market-sim -calibrate-only           # k = 0.943161（契约记 1.0400，见判决书 §三）
```

## 重新渲染文档

```powershell
powershell -ExecutionPolicy Bypass -File tools\render_all.ps1
```

（脚本要求本机无 `node`，改用 DSH Desktop 的 Electron 作 node；若本机有 node 也没问题。）
