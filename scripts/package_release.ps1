# ScreenForge Phase 8-C — Release 打包脚本
# 用法:
#   powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 -BuildDir build -Config Release
# 功能:
#   1. 定位 ScreenForge.exe
#   2. windeployqt 部署 Qt 运行时 DLL
#   3. 拷贝 FFmpeg DLL（avformat/avcodec/avutil/swresample/swscale）
#   4. 生成 version.json（appName/version/buildDate/gitCommit）
#   5. 拷贝 README / config 模板
#   6. 输出 dist/ScreenForge-x.y.z/ 发布目录

param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$Version = "0.8.0"
)

$ErrorActionPreference = "Stop"

# ── 1. 定位 exe ──
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "ScreenForge.exe" | Select-Object -First 1
if (-not $exe) { Write-Error "未找到 ScreenForge.exe（请先构建）"; exit 1 }
$exeDir = $exe.DirectoryName
Write-Host "exe: $($exe.FullName)" -ForegroundColor Cyan

# ── 2. 部署 Qt 运行时 ──
Write-Host "[1/5] windeployqt..."
windeployqt $exe.FullName | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Warning "windeployqt 返回非零（继续打包，Qt DLL 可能缺失）" }

# ── 3. FFmpeg DLL 拷贝 ──
$ffmpegRoot = $env:FFMPEG_ROOT
if (-not $ffmpegRoot) { $ffmpegRoot = "$PSScriptRoot/../vendor/ffmpeg" }
$ffDlls = @("avformat", "avcodec", "avutil", "swresample", "swscale")
Write-Host "[2/5] 拷贝 FFmpeg DLL..."
foreach ($n in $ffDlls) {
    # 常见命名：avformat-61.dll / avformat.dll
    $cand = Get-ChildItem -Path "$ffmpegRoot/bin" -Filter "$n-*.dll" -ErrorAction SilentlyContinue |
            Select-Object -First 1
    if (-not $cand) {
        $cand = Get-ChildItem -Path "$ffmpegRoot/bin" -Filter "$n.dll" -ErrorAction SilentlyContinue |
                Select-Object -First 1
    }
    if ($cand) {
        Copy-Item $cand.FullName $exeDir -Force
        Write-Host "  ✓ $($cand.Name)"
    } else {
        Write-Warning "  ✗ 未找到 $n DLL（FFMPEG_ROOT=$ffmpegRoot/bin）"
    }
}

# ── 4. 生成 version.json ──
Write-Host "[3/5] 生成 version.json..."
$gitCommit = "unknown"
try {
    $gitCommit = (git rev-parse --short HEAD 2>$null).Trim()
    if (-not $gitCommit) { $gitCommit = "unknown" }
} catch { $gitCommit = "unknown" }
$buildDate = Get-Date -Format "yyyy-MM-ddTHH:mm:sszzz"

$versionJson = @{
    appName   = "ScreenForge"
    version   = $Version
    buildDate = $buildDate
    gitCommit = $gitCommit
} | ConvertTo-Json

$versionJson | Out-File "$exeDir/version.json" -Encoding utf8
Write-Host "  $versionJson"

# ── 5. 组装 dist/ ──
Write-Host "[4/5] 组装 dist/..."
$distRoot = "$PSScriptRoot/../dist"
$distDir = "$distRoot/ScreenForge-$Version"
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

# 拷贝 exe + 全部 DLL + 插件目录
Copy-Item "$exeDir/ScreenForge.exe" $distDir -Force
Get-ChildItem $exeDir -Filter "*.dll" | Copy-Item -Destination $distDir -Force
if (Test-Path "$exeDir/platforms") { Copy-Item "$exeDir/platforms" $distDir -Recurse -Force }
if (Test-Path "$exeDir/styles")     { Copy-Item "$exeDir/styles" $distDir -Recurse -Force }
if (Test-Path "$exeDir/imageformats") { Copy-Item "$exeDir/imageformats" $distDir -Recurse -Force }
if (Test-Path "$exeDir/tls")        { Copy-Item "$exeDir/tls" $distDir -Recurse -Force }
if (Test-Path "$exeDir/iconengines"){ Copy-Item "$exeDir/iconengines" $distDir -Recurse -Force }

# 文档与配置模板
Copy-Item "$PSScriptRoot/../README.md" $distDir -Force -ErrorAction SilentlyContinue
Copy-Item "$exeDir/version.json" $distDir -Force
if (Test-Path "$PSScriptRoot/../docs/BUILD_SMOKE_TEST.md") {
    Copy-Item "$PSScriptRoot/../docs/BUILD_SMOKE_TEST.md" $distDir -Force
}

# ── 6. 压缩 ──
Write-Host "[5/5] 压缩 zip..."
$zipPath = "$distRoot/ScreenForge-$Version-win64.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path "$distDir/*" -DestinationPath $zipPath -Force

Write-Host ""
Write-Host "=== Release 打包完成 ===" -ForegroundColor Green
Write-Host "目录: $distDir"
Write-Host "压缩: $zipPath"
Write-Host "版本: $Version · 构建日期 $buildDate · commit $gitCommit"

# 清理 exe 目录里的 FFmpeg 拷贝（避免污染构建目录）
foreach ($n in $ffDlls) {
    Get-ChildItem $exeDir -Filter "$n-*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem $exeDir -Filter "$n.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
}
exit 0
