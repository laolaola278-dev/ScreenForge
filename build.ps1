# ScreenForge Phase 0 —— 一键构建脚本（需先安装 VS2022 + Qt 6.5 msvc2019_64）
$ErrorActionPreference = "Stop"
$QtDir = "C:/Qt/6.5.0/msvc2019_64"

cmake -B build -DCMAKE_PREFIX_PATH=$QtDir -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# MSVC 多配置生成器把 exe 输出到 build/bin/Release/，自动递归查找真实路径
$exe = Get-ChildItem -Path build -Recurse -Filter ScreenForge.exe | Select-Object -First 1
if (-not $exe) { Write-Error "未找到 ScreenForge.exe"; exit 1 }
windeployqt $exe.FullName

Write-Host "`n完成 → $($exe.FullName)" -ForegroundColor Green
