# ScreenForge Phase 0 — 构建说明

本包包含 Phase 0 全部 11 个 C++ 源文件 + GitHub Actions 工作流 + 一键构建脚本。

## 方式一：GitHub Actions 云端构建（推荐，无需本地安装）
1. 把本包全部文件推送到仓库（务必包含 .github/workflows/build.yml）
2. 打开仓库 Actions 页面，等待「Build ScreenForge」运行完成
3. 进入该次运行 → Artifacts → 下载 ScreenForge-win64.zip
4. 解压运行 ScreenForge.exe

## 方式二：本地构建
前置：Visual Studio 2022（MSVC v143 + Windows 11 SDK）、Qt 6.5+（msvc2019_64 套件）
在「Developer PowerShell for VS 2022」中执行：

    powershell -ExecutionPolicy Bypass -File build.ps1

或手动执行：

    cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.5.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    windeployqt build/bin/Release/ScreenForge.exe
    build\bin\Release\ScreenForge.exe

## 通过标准
- 编译退出码 0，生成 ScreenForge.exe 与 screenforge_core.lib
- 窗口显示五张检测卡片：D3D11 / GPU 型号 / 显存 / NVENC / 驱动版本
- logs/app.log 存在且持续增长
- 无 NVIDIA 显卡的机器：NVENC 显示 ✕，但程序正常启动不崩溃
