<div align="center">
  <h1>ScreenForge</h1>
  <p><strong>Windows 原生高性能视频录制程序</strong></p>
  <p>
    C++20 · Qt6 · Direct3D11 · WGC · NVENC · WASAPI · FFmpeg · CMake
  </p>
  <p>
    <a href="#-构建"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="Build Status"></a>
    <a href="#-技术栈"><img src="https://img.shields.io/badge/C%2B%2B-20-blue" alt="C++20"></a>
    <a href="#-技术栈"><img src="https://img.shields.io/badge/Qt-6.5-green" alt="Qt 6.5"></a>
    <a href="#-技术栈"><img src="https://img.shields.io/badge/license-MIT-yellow" alt="License"></a>
  </p>
</div>

---

## 概述

ScreenForge 是一个 Windows 原生视频录制程序，采用 GPU 零拷贝管线架构，从屏幕捕获到编码全程保持数据在 GPU 驻留，无需 CPU 回读。

项目采用**渐进式阶段开发**，每个阶段产出可编译的真实代码，支持 GitHub Actions 自动化构建。

### 架构亮点

```
WGC 屏幕捕获 ──┐
               ├─→ FrameQueue(SPSC 无锁队列) ─→ NVENC 编码 ─→ MP4 封装
WASAPI 音频 ───┘
(QPC 统一时钟)    (60fps Pacing，满丢最旧)    (GPU 零拷贝)    (fragmented MP4)
```

## 技术栈

| 模块 | 技术 |
|---|---|
| 屏幕捕获 | Windows Graphics Capture (WGC) — ID3D11Texture2D，GPU 驻留，零拷贝 |
| 帧管线 | SPSC 有界无锁队列，QPC 60fps Pacing，满丢最旧策略 |
| 视频编码 | NVIDIA NVENC (H.264)，BT.709 色彩转换，DirectX 输入 |
| 音频录制 | WASAPI Loopback / 麦克风，48kHz stereo，AAC |
| 用户界面 | Qt6 Widgets，系统托盘，快捷键，配置持久化 |
| 视频封装 | FFmpeg libavformat — fragmented MP4，崩溃安全 |
| 模拟器 | 完整沙盒模拟链路，无需 GPU 即可运行验证 |

## 功能

- ✅ 屏幕录制（全屏/窗口/区域选择）
- ✅ 音频录制（系统声音/麦克风/两者）
- ✅ NVIDIA NVENC 硬件编码（H.264，GPU 零拷贝）
- ✅ 沙盒模拟模式（无 GPU 也能运行）
- ✅ Qt6 图形界面（开始/暂停/停止，实时状态监控）
- ✅ 系统托盘、快捷键（Ctrl+F9）
- ✅ 崩溃安全的 fragmented MP4 输出
- ✅ 2 小时稳定性测试
- ✅ CI 自动化构建（GitHub Actions）

## 构建

### 使用 GitHub Actions（推荐）

Fork 或推送后，在 GitHub Actions 页面选择 **Build ScreenForge** workflow，自动使用 `windows-2022` runner 编译。

产物下载：Artifacts → `ScreenForge-win64.zip`

### 本地构建（Windows + NVIDIA GPU）

**前置依赖：**

| 依赖 | 版本 | 路径 |
|---|---|---|
| Visual Studio 2022 | 17.x | 含 MSVC v143 工具集 + Windows 10 SDK 10.0.22621+ |
| Qt | 6.5.3 | `C:/Qt/6.5.0/msvc2019_64` 或设置 `QT_ROOT` |
| NVIDIA Video Codec SDK | 12.x | 设置 `NVENC_SDK_ROOT` |
| FFmpeg dev | 6.x/7.x | 设置 `FFMPEG_ROOT` |

**构建步骤：**

```powershell
# Simulation 模式（无需 NVENC/FFmpeg）
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Simulation -RunSmoke

# Hardware 模式（需 NVENC SDK + FFmpeg）
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware -RunTests
```

**手动构建：**

```powershell
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.5.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
windeployqt build/bin/Release/ScreenForge.exe
```

## 运行

```powershell
# 启动图形界面
ScreenForge.exe

# 沙盒模拟录制测试（无需 GPU）
ScreenForge.exe --sandbox-demo --seconds 10 --out output.mp4 --json report.json

# 硬件录制测试（需 NVIDIA GPU）
ScreenForge.exe --hardware-record-test --seconds 60 --fps 60 --out recording.mp4

# 稳定性测试（2 小时）
ScreenForge.exe --stability-test --hours 2

# 音频录制测试
ScreenForge.exe --audio-record-test --seconds 60 --out recording_with_audio.mp4
```

## 项目结构

```
ScreenForge/
├── CMakeLists.txt              主构建文件
├── src/
│   ├── app/                    Qt 入口 + 硬件装配层
│   ├── ui/                     Qt6 Widgets 图形界面（纯 UI，无硬件依赖）
│   ├── graphics/               D3D11 设备管理 + GPU 检测
│   ├── capture/                WGC 屏幕捕获源
│   ├── pipeline/               帧队列 + 60fps Pacing 管线
│   ├── encoder/                NVENC 编码器接口 + 硬件实现
│   ├── simulation/             沙盒模拟器（SyntheticFrameSource, NvencSimulator）
│   ├── audio/                  WASAPI 音频捕获（系统声音 + 麦克风）
│   ├── muxer/                  FFmpeg MP4 封装
│   └── recorder/               录制引擎（状态机 + 链路集成）
├── sandbox/                    独立沙盒演示模块
├── scripts/                    构建脚本
├── docs/                       文档
├── .github/workflows/build.yml GitHub Actions CI
└── README.md
```

## 开发路线图

| Phase | 内容 | 状态 |
|---|---|---|
| 0 | 工程初始化 — CMake + Qt6 + D3D11 设备 | ✅ |
| 1 | WGC 屏幕捕获 | ✅ |
| 2 | D3D11 Frame Pipeline（SPSC 队列 + 60fps Pacing） | ✅ |
| 3-A | NVENC 编码器实现（BT.709 + QPC PTS） | ✅ |
| 3-B-Sim | 沙盒模拟验证 | ✅ |
| 4-A | FFmpeg MP4 封装（fragmented MP4） | ✅ |
| 4-B | RecorderEngine 视频链路集成 | ✅ |
| 5-A | 真实硬件录制 MVP（NVENC） | ✅ |
| 5-B | 稳定性测试（2 小时） | ✅ |
| 6-A | WASAPI 音频录制 | ✅ |
| 7-A | Qt6 用户界面产品化 | ✅ |
| 7-B | 编译审计与构建模式 | ✅ |
| 8 | 发布与文档 | 🔜 |

## 许可证

MIT