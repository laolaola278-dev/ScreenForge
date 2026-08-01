# ScreenForge 构建与 Smoke Test 指南

Phase 8-A · 目标：在真实 Windows 环境从源码构建并完成冒烟测试。

## 1. 环境信息记录模板

在首次构建前填写本表（每次构建环境变更时更新）：

| 项目 | 值 |
|---|---|
| Windows 版本 | 如 Windows 11 23H2 (build 22631) |
| MSVC 版本 | 如 VS2022 17.8 · MSVC 19.38 |
| Qt 版本 | 6.5.3 (msvc2019_64) |
| CMake 版本 | 如 3.28.1 |
| NVIDIA Video Codec SDK | 12.1（Hardware 构建必需） |
| FFmpeg dev | 如 6.1（BtbN builds） |
| GPU | 如 NVIDIA GeForce RTX 4070 |
| 驱动版本 | 如 552.44 |

环境变量：
- `NVENC_SDK_ROOT` → NVIDIA Video Codec SDK 根目录（Hardware 构建）
- `FFMPEG_ROOT` → FFmpeg dev 包根目录（必需）
- `QT_ROOT` → Qt 根目录（可选，默认 C:/Qt/6.5.0/msvc2019_64）

## 2. 构建（一键脚本）

```powershell
# Simulation 构建（无 NVENC SDK 也可）
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Simulation -RunSmoke

# Hardware 构建（需 NVENC SDK）
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware -RunSmoke
```

或手动：
```powershell
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.5.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
windeployqt build/bin/Release/ScreenForge.exe
```

## 3. Smoke Test

```powershell
build/bin/Release/ScreenForge.exe --smoke-test --json smoke_report.json
```

检查项（全部真实 API 调用）：
1. D3D11 Device 创建（D3D11CreateDevice）
2. DXGI GPU 枚举（枚举适配器 + 驱动版本）
3. FFmpeg 初始化（avformat/avcodec/avutil 版本查询）
4. WASAPI 音频设备枚举（渲染/捕获端点计数）
5. 编码器能力检测（NVENC 会话 + H264 能力；Simulation 构建标注不可用）

输出：`smoke_report.json`（每项 pass/fail + 信息 + 环境信息）

## 4. 运行测试入口

| 命令 | 说明 |
|---|---|
| `--smoke-test` | 环境冒烟检查（本阶段） |
| `--simulate-encode --frames 1000` | 模拟链路验证（simulation_report.json） |
| `--mux-bench --packets 1000` | MP4 封装基准（mux_bench.mp4） |
| `--record-test --seconds 10` | 沙盒录制测试（recording.mp4） |
| `--hardware-record-test --seconds 10` | 真实 NVENC 录制（hardware_report.json；无硬件自动回退） |
| `--stability-test --hours 1` | 稳定性测试（需真实硬件） |

## 5. 验收清单

- [ ] cmake configure 无错误（Simulation / Hardware 两种模式）
- [ ] cmake build 100% 完成
- [ ] smoke_report.json 生成且 5 项检查通过
- [ ] 若任何检查失败：记录于 REAL_BUILD_REPORT.md 失败原因
