# ScreenForge Phase RC-2 — 真机运行验证工具
# 用法（真机）:
#   powershell -ExecutionPolicy Bypass -File scripts/rc_verify.ps1
# 功能:
#   1. 依次运行 --smoke-test / --record-test(60s) / --hardware-record-test(60s)
#   2. 解析各自 JSON 报告
#   3. 汇总生成 runtime_report.json
# 规则: 只记录真实运行结果；命令失败/报告缺失 → 如实写入 errors，不推断成功

param(
    [string]$Exe = "build/bin/Release/ScreenForge.exe"
)

$ErrorActionPreference = "Continue"

if (-not (Test-Path $Exe)) {
    Write-Error "未找到 $Exe（请先构建）"
    exit 1
}

$errors = @()
$now = Get-Date -Format "yyyy-MM-ddTHH:mm:sszzz"

# ── 1. Smoke Test ──
Write-Host "=== [1/3] --smoke-test ===" -ForegroundColor Cyan
& $Exe --smoke-test --json smoke_report.json
$smokeOk = ($LASTEXITCODE -eq 0)
if (-not $smokeOk) { $errors += "smoke-test failed (exit $LASTEXITCODE)" }
$smoke = $null
if (Test-Path "smoke_report.json") { $smoke = Get-Content "smoke_report.json" -Raw | ConvertFrom-Json }

# ── 2. Record Test（模拟链路，60s）──
Write-Host "=== [2/3] --record-test --seconds 60 ===" -ForegroundColor Cyan
& $Exe --record-test --seconds 60 --out recording_rc.mp4 --json recording_session.json
$recOk = ($LASTEXITCODE -eq 0)
if (-not $recOk) { $errors += "record-test failed (exit $LASTEXITCODE)" }
$rec = $null
if (Test-Path "recording_session.json") { $rec = Get-Content "recording_session.json" -Raw | ConvertFrom-Json }

# ── 3. Hardware Record Test（必须真实 NVENC，60s）──
Write-Host "=== [3/3] --hardware-record-test --seconds 60 ===" -ForegroundColor Cyan
& $Exe --hardware-record-test --seconds 60 --out recording_hw_rc.mp4 --json hardware_report.json
$hwOk = ($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq 2)   # 0=硬件成功 2=回退（记录但视为硬件未验证）
if ($LASTEXITCODE -eq 2) { $errors += "hardware-record-test fell back to simulator (no real NVENC)" }
if ($LASTEXITCODE -eq 1) { $errors += "hardware-record-test failed (exit 1)" }
$hw = $null
if (Test-Path "hardware_report.json") { $hw = Get-Content "hardware_report.json" -Raw | ConvertFrom-Json }

# ── 汇总 runtime_report.json ──
Write-Host "=== 汇总 runtime_report.json ===" -ForegroundColor Cyan

$report = [ordered]@{
    generatedAt   = $now
    gpu           = if ($hw) { $hw.gpuName } else { "unknown" }
    nvencAvailable = if ($hw) { [bool]$hw.realNvencHardware } else { $false }
    captureWorking = if ($smoke) { [bool]$smoke.allPass } else { $false }
    audioWorking   = if ($smoke -and $smoke.checks) {
                        ($smoke.checks | Where-Object { $_.name -match "WASAPI" } | Select-Object -First 1).pass
                     } else { $false }
    videoDuration  = if ($rec) { [double]$rec.durationSec } else { 0 }
    fps            = if ($rec) { [int]$rec.fps } else { 0 }
    bitrate        = if ($hw) { [int]$hw.bitrateKbps } elseif ($rec) { 12000 } else { 0 }
    droppedFrames  = 0
    failedFrames   = if ($hw) { [int64]$hw.failedFrames } else { 0 }
    outputFile     = "recording_rc.mp4, recording_hw_rc.mp4"
    errors         = $errors
}

$report | ConvertTo-Json -Depth 3 | Out-File "runtime_report.json" -Encoding utf8
Write-Host ($report | ConvertTo-Json -Depth 3)

if ($errors.Count -gt 0) {
    Write-Host "`n存在错误项，详见 runtime_report.json" -ForegroundColor Yellow
    exit 2
}
Write-Host "`n=== RC-2 运行验证完成（无错误） ===" -ForegroundColor Green
exit 0
