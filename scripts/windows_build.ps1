# ScreenForge Phase 8-A — 一键构建脚本（Windows）
# 用法:
#   Simulation 构建（无 NVENC SDK 也可）:
#     powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Simulation -RunSmoke
#   Hardware 构建（需 NVENC SDK）:
#     powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware -RunSmoke
# 参数:
#   -Mode      Simulation | Hardware（默认 Simulation）
#   -BuildDir  构建目录（默认 build）
#   -Config    Release | Debug（默认 Release）
#   -RunSmoke  构建后运行 --smoke-test
#   -RunTests  构建后运行模拟链路/封装/录制测试
#   -SkipDeploy 跳过 windeployqt

param(
    [ValidateSet("Simulation", "Hardware")]
    [string]$Mode = "Simulation",
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$RunSmoke,
    [switch]$RunTests,
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"

# ── 环境探测 ──
$QtDir = $env:QT_ROOT
if (-not $QtDir) { $QtDir = "C:/Qt/6.5.0/msvc2019_64" }
if (-not (Test-Path "$QtDir/bin/qmake6.exe")) {
    Write-Warning "未找到 Qt ($QtDir)，请设置 QT_ROOT 或安装 Qt 6.5+ msvc2019_64 套件"
}

$FFmpeg = $env:FFMPEG_ROOT
if (-not $FFmpeg) { $FFmpeg = "$PSScriptRoot/../vendor/ffmpeg" }

Write-Host "=== ScreenForge Build ($Mode) ===" -ForegroundColor Cyan
Write-Host "Qt      : $QtDir"
Write-Host "FFmpeg  : $FFmpeg"
Write-Host "BuildDir: $BuildDir  Config: $Config"

# ── 1. cmake configure ──
$extraArgs = @()
if ($Mode -eq "Hardware") { $extraArgs += "-DSF_REQUIRE_NVENC=ON" }
else { $extraArgs += "-DSF_REQUIRE_NVENC=OFF" }

Write-Host "`n[1/4] cmake configure..." -ForegroundColor Yellow
cmake -B $BuildDir -DCMAKE_PREFIX_PATH=$QtDir -DCMAKE_BUILD_TYPE=$Config @extraArgs
if ($LASTEXITCODE -ne 0) { Write-Error "cmake configure 失败 (exit $LASTEXITCODE)"; exit 1 }

# ── 2. cmake build ──
Write-Host "`n[2/4] cmake build..." -ForegroundColor Yellow
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { Write-Error "cmake build 失败 (exit $LASTEXITCODE)"; exit 1 }

# ── 3. 定位 exe + 部署 Qt 运行时 ──
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "ScreenForge.exe" | Select-Object -First 1
if (-not $exe) { Write-Error "未找到 ScreenForge.exe"; exit 1 }
Write-Host "`n[3/4] 产物: $($exe.FullName)" -ForegroundColor Green

if (-not $SkipDeploy) {
    Write-Host "部署 Qt 运行时 (windeployqt)..."
    windeployqt $exe.FullName | Out-Null
}

# ── 4. 测试 ──
Write-Host "`n[4/4] 运行测试..." -ForegroundColor Yellow
$failed = $false

if ($RunSmoke) {
    Write-Host "→ --smoke-test"
    & $exe.FullName --smoke-test --json smoke_report.json
    if ($LASTEXITCODE -ne 0) { Write-Host "  ✗ smoke-test FAILED (exit $LASTEXITCODE)" -ForegroundColor Red; $failed = $true }
    else { Write-Host "  ✓ smoke-test PASSED" -ForegroundColor Green }
    if (Test-Path "smoke_report.json") {
        Get-Content "smoke_report.json" | Write-Host
    }
}

if ($RunTests) {
    Write-Host "→ --simulate-encode (1000 帧)"
    & $exe.FullName --simulate-encode --frames 1000 --out simulation_test.h264 --report simulation_report.json
    if ($LASTEXITCODE -ne 0) { Write-Host "  ✗ simulate-encode FAILED" -ForegroundColor Red; $failed = $true }
    else { Write-Host "  ✓ simulate-encode PASSED" -ForegroundColor Green }

    Write-Host "→ --mux-bench (1000 packets)"
    & $exe.FullName --mux-bench --packets 1000 --out mux_bench.mp4
    if ($LASTEXITCODE -ne 0) { Write-Host "  ✗ mux-bench FAILED" -ForegroundColor Red; $failed = $true }
    else { Write-Host "  ✓ mux-bench PASSED" -ForegroundColor Green }

    Write-Host "→ --record-test (10s)"
    & $exe.FullName --record-test --seconds 10 --out recording.mp4 --json recording_session.json
    if ($LASTEXITCODE -ne 0) { Write-Host "  ✗ record-test FAILED" -ForegroundColor Red; $failed = $true }
    else { Write-Host "  ✓ record-test PASSED" -ForegroundColor Green }
}

if ($failed) { Write-Host "`n=== 构建成功，但存在失败的测试 ===" -ForegroundColor Red; exit 2 }
Write-Host "`n=== 构建与测试全部完成 ($Mode) ===" -ForegroundColor Green
exit 0
