# ScreenForge 构建依赖审计报告

审计日期：Phase 7-C
审计对象：ScreenForge/ 真实 C++ 工程（src/ 全部 11 个模块）
审计方式：静态源码审查（CMakeLists 全量读取 + 接口头文件核对）
结论：**依赖链完整，add_subdirectory 顺序合法，PUBLIC/PRIVATE 划分正确；NVENC 可选化已生效（Simulation / Hardware 双构建模式成立）。**
⚠️ 编译验证：本环境无法执行 MSVC/CMake 生成器，以下为静态审计结论，实际 configure/build 需真机执行。

---

## 1. add_subdirectory 顺序审计

`src/CMakeLists.txt` 顺序：

```
1  app          ← 可执行目标（依赖全部库目标，CMake 允许目标在依赖之前声明）
2  graphics     ← 基础库（无内部依赖）
3  capture      ← 依赖 graphics
4  pipeline     ← 依赖 capture + graphics
5  encoder      ← 依赖 graphics + pipeline（可选，SDK 缺失 return()）
6  encoder/NvEncoderHardware ← 依赖 graphics + capture（可选，SDK 缺失 return()）
7  simulation   ← 依赖 pipeline + graphics
8  muxer        ← 依赖 FFmpeg（独立）
9  recorder     ← 依赖 simulation + muxer + pipeline + graphics（条件链接 encoder*）
10 audio        ← 依赖 muxer + simulation + recorder（条件链接 encoder）
11 ui           ← 仅 Qt6::Widgets
```

**结论**：CMake 中 `target_link_libraries` 允许引用尚未 add_subdirectory 的目标（生成器阶段解析），
顺序合法；库间依赖为 DAG，无循环依赖。`encoder`/`encoder/NvEncoderHardware` 的
`return()` 早退在 `recorder`/`audio`/`app` 的条件 `if(TARGET ...)` 中正确兜底。

## 2. 模块依赖明细

| 模块 | 依赖（链接） | 头文件（include 目录） | 编译宏 |
|---|---|---|---|
| **app** (exe) | graphics, capture, pipeline, simulation, muxer, recorder, audio, ui, Qt6::Widgets；条件: encoder_hw, encoder | 各库 PUBLIC 传播 + Qt | NOMINMAX；条件: SF_HAVE_NVENC_HW |
| **ui** | Qt6::Widgets（PUBLIC） | 自身目录 | NOMINMAX |
| **capture** | graphics, d3d11, dxgi（PUBLIC）；windowsapp（PRIVATE） | 自身目录 | NOMINMAX(PUBLIC)；WIN32_LEAN_AND_MEAN(PRIVATE) |
| **pipeline** | capture, graphics, d3d11, dxgi（PUBLIC） | 自身目录 | NOMINMAX |
| **graphics** | d3d11, dxgi（PUBLIC） | 自身目录 | NOMINMAX |
| **encoder**（可选） | graphics, pipeline, d3d11, dxgi, d3dcompiler（PUBLIC） | NVENC_SDK + 自身 | NOMINMAX, SF_HAVE_NVENC_HW |
| **encoder_hw**（可选） | graphics, capture, d3d11, dxgi, d3dcompiler（PUBLIC） | NVENC_SDK + 自身 | NOMINMAX, SF_HAVE_NVENC_HW |
| **simulation** | pipeline, graphics（PUBLIC） | 自身 + src/（供 encoder/IEncoder.h） | NOMINMAX |
| **muxer** | avformat, avcodec, avutil（PUBLIC） | FFMPEG + 自身 + src/（供 audio/AudioFrame.h） | NOMINMAX |
| **recorder** | simulation, muxer, pipeline, graphics, d3d11, dxgi, d3dcompiler（PUBLIC）；条件: encoder, encoder_hw | 自身 + src/encoder（NvCapabilities.h） | NOMINMAX；条件: SF_HAVE_NVENC_HW |
| **audio** | muxer, simulation, recorder, ole32（PUBLIC）；条件: encoder | 自身目录 | NOMINMAX |

## 3. PUBLIC / PRIVATE 审计

- ✅ `windowsapp`（WinRT 互操作）仅 capture 内部使用 → **PRIVATE 正确**（不泄漏给下游）
- ✅ `WIN32_LEAN_AND_MEAN` 仅 capture 编译需要 → **PRIVATE 正确**
- ✅ d3d11/dxgi 被 capture/graphics 下游依赖（Frame.h 用 DXGI_FORMAT）→ **PUBLIC 正确**
- ✅ d3dcompiler 由 recorder 显式链接（原依赖 encoder_hw 传递，7-B 已修复无 SDK 构建缺口）→ **PUBLIC 正确**
- ✅ FFmpeg / NVENC include 路径经 PUBLIC 传播，消费方（app/audio）无需重复声明

## 4. 构建隔离检查

| 模式 | 条件 | 行为 |
|---|---|---|
| Simulation build | 无 NVENC_SDK_ROOT | encoder/encoder_hw `return()` 跳过；SF_HAVE_NVENC_HW 未定义；recorder/audio/app 条件链接为空；UI/模拟器/MP4 全量编译 → **预期成功** |
| Hardware build | NVENC_SDK_ROOT 已设 | 两硬件库编译 + SF_HAVE_NVENC_HW=1；全链路 → **预期成功** |
| 强制 | `-DSF_REQUIRE_NVENC=ON` + 无 SDK | cmake FATAL_ERROR（含明确指引） |

**潜在关注点（低严重度）**：
- `muxer/CMakeLists.txt` 的 FFmpeg 为**强制依赖**（FATAL_ERROR）。Simulation 模式下仍需 FFmpeg dev 包才能构建——这是设计使然（MP4 封装为产品核心），但若需"零依赖模拟构建"应增加 `SF_REQUIRE_FFMPEG` 选项（未实现，记录在案）。
- `src/app/MainWindow.cpp`、`src/app/VerifyRun.*` 保留于磁盘但**未加入 app 目标编译**（7-B 移除），属历史代码冻结，无构建影响。
