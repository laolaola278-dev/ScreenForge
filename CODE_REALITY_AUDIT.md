# ScreenForge 代码真实性审计报告

Release Candidate 最终审计 · 只读检查（未修改任何代码）
审计方式：全仓库静态扫描 + 调用链逐级核对（grep 证据 + 源码阅读）

---

## 1. 全仓库扫描

### 1.1 文件统计（真实存在）

| 类型 | 数量 | 明细 |
|---|---|---|
| .cpp | 28 | 编译链 22 + 历史冻结 6 |
| .h | 35 | src/ 32 + 历史冻结 3 |
| CMakeLists.txt | 16 | 根 1 + src/ 12 + 旧工程 3 |

**编译链实际引用（src/CMakeLists.txt + 各模块）**：
```
app(main/Logger/UiBackend/SmokeTest) · graphics · capture · pipeline
· encoder(NvEncoder) · encoder/NvEncoderHardware · simulation · muxer
· recorder · audio · ui
```
**历史冻结（保留未编译，7-B 起移出构建）**：`app/`、`core/` 旧 Phase0 工程 4 文件；`src/app/MainWindow.cpp`、`src/app/VerifyRun.cpp`（+对应 .h）。

### 1.2 第三方依赖（CMake 真实链接）

| 依赖 | 类型 | 证据 |
|---|---|---|
| Qt6 Widgets | 链接 | 根 CMakeLists `find_package(Qt6 REQUIRED COMPONENTS Widgets)` |
| FFmpeg avformat/avcodec/avutil | 链接 | muxer/CMakeLists `find_library` + target_link |
| NVIDIA Video Codec SDK | 头文件（可选） | encoder 双 CMake `find_path(nvEncodeAPI.h)`；缺失 → SIMULATION-only |
| Windows SDK d3d11/dxgi/d3dcompiler | 链接 | graphics/capture/pipeline/recorder CMake |
| windowsapp（WinRT） | 链接 | capture/CMakeLists（PRIVATE） |
| ole32 / psapi | 链接 | audio / StabilityTest+UiBackend（#pragma comment） |

**判定：PASS**

## 2. 入口检查（main.cpp 真实调用链）

| 入口 | main.cpp 分支 | 调用函数 | 判定 |
|---|---|---|---|
| --simulate-encode | L48 | `sf::RunSimulationBenchmark` | PASS |
| --mux-bench | L64 | `sf::RunMuxBenchmark` | PASS |
| --record-test | L74 | `sf::RunRecordTest` | PASS |
| --hardware-record-test | L87 | `sf::RunHardwareRecordTest` | PASS |
| --stability-test | L100 | `sf::RunStabilityTest` | PASS |
| --audio-record-test | L117 | `sf::RunAudioRecordTest` | PASS |
| --smoke-test | L132 | `sf::RunSmokeTest` | PASS |
| GUI（默认） | 尾部 | `UiBackend backend; MainWindow w(&backend)` | PASS |

**判定：PASS**（7 命令 + GUI 全部存在真实函数调用，无空分支）

## 3. 核心调用链检查（真实 API 逐级验证）

### 3.1 WGC 捕获链 — PASS
```
WgcCaptureSource::Start()  L194 Direct3D11CaptureFramePool::Create
                           L196 framePool.CreateCaptureSession(item)
                           L204 session.StartCapture()
回调 OnFrameArrived         L69  pool.TryGetNextFrame()
                           L75  access->GetInterface(guid_of<ID3D11Texture2D>)
目标选择                   L180 CreateForWindow / L186 CreateForMonitor
```
WinRT 真实 API 调用，零 CPU 回读路径存在。

### 3.2 FramePipeline 链 — PASS
```
FramePipeline::Start  L42 source.SetFrameCallback(...)   ← 生产端接线
                      L49 std::thread(ConsumerLoop)
OnCaptureFrame        L77 m_queue.Push (SPSC 满丢最旧)
ConsumerLoop          L95-99 QueryPerformanceCounter 高精度等待
                      L105 m_queue.Pop → consumer
```
SPSC FrameQueue 无锁环形队列 + QPC 60fps pacing 真实存在。

### 3.3 NVENC 硬件编码链 — PASS
```
NvEncoderHardware::LoadApi        L133 LoadLibraryA("nvEncodeAPI64.dll")
                                  L139 GetProcAddress("NvEncodeAPICreateInstance")
CreateSession                     L158 nvEncOpenEncodeSessionEx(D3D11)
CreateConfig                       L191 nvEncInitializeEncoder(H264 CBR 12Mbps GOP120)
CreateConvertPipeline              L249 nvEncRegisterResource(NV12)
GetSequenceParams                  L262 nvEncGetSequenceParams
EncodeOneFrame                     L324 nvEncEncodePicture / L326 nvEncEncodeFrame
DrainBitstream                     L343 nvEncLockBitstream → L372 nvEncUnlockBitstream
FlushAndClose                      L397 nvEncFlushEncoder
```
真实 NVENC SDK 动态加载调用，全程 GPU 纹理路径。

### 3.4 MP4 封装链 — PASS
```
Mp4Muxer::Initialize  L51 avformat_alloc_output_context2("mp4")
                      L58 av_opt_set(movflags=frag_keyframe+empty_moov+default_base_moof)
                      L118 avio_open → L124 avformat_write_header
WritePacket           L158 av_interleaved_write_frame
EncodePcmChunk(AAC)   L199 avcodec_send_frame → L204 avcodec_receive_packet
Finalize              L247 flush(null) → L264 av_write_trailer
```
FFmpeg 真实封装 + AAC 编码，崩溃安全 fragment 结构。

### 3.5 音频链 — PASS
```
AudioCapture::CaptureLoop  GetBuffer → AudioFrame{captureQpc} → ReleaseBuffer
（IAudioSource.h 内联 InitCapture：GetDefaultAudioEndpoint + IAudioClient::Initialize
  + AUDCLNT_STREAMFLAGS_LOOPBACK + AUTOCONVERTPCM）
```
真实 WASAPI Loopback 调用。

### 3.6 UI 装配链（UiBackend）— PASS
```
L191/195 SetPacketSink → m_muxer->WritePacket   ← Encoder→Muxer 接线
L214 engine.StartRecording()
L266 音频 GetFrame → m_muxer->WriteAudioFrame
```
RecorderEngine 零修改，仅 API 调用。

### 3.7 稳定性真实系统调用 — PASS
```
GetProcessMemoryInfo（working set）· QueryVideoMemoryInfo（GPU 显存）
GetDeviceRemovedReason（设备移除检测）· GetDiskFreeSpaceExA（磁盘满检测）
```

**调用链判定：PASS（7 条链全部存在真实 API 函数调用，无空壳）**

## 4. 源码质量扫描

| 检查项 | 结果 | 判定 |
|---|---|---|
| TODO / FIXME / XXX / HACK | **0 匹配**（全仓 cpp/h） | PASS |
| 未实现 / 待实现 / 占位 | 0 匹配 | PASS |
| return nullptr | 4 处（HardwareRecordTest.cpp L80-93，均为 `GpuPatternSource::Create` 工厂失败路径） | PASS（合法错误返回） |
| 空实现 | 0 处 stub；`GpuPatternSource::Stop(){}` 为 RAII no-op（合法） | PASS |
| 假 Sleep 模拟 | 见下 | PASS（全部可解释） |

### Sleep 分布审计（全部真实用途，无伪装）

| 位置 | 用途 | 性质 |
|---|---|---|
| FramePipeline / RecorderEngine / PipelineBenchmark / VerifyRun | QPC pacing 剩余时间睡眠 | 真实调度 |
| NvEncoder / NvEncoderHardware `Sleep(1)` | 编码线程轮询 DrainBitstream | 真实 |
| AudioCapture / MicrophoneCapture `Sleep(8)` | WASAPI 包轮询 | 真实 |
| UiBackend `Sleep(16)` / 各测试等待循环 | 音频 drain / 状态轮询 | 真实 |
| Logger `Sleep(100)` | 异步落盘周期 | 真实 |
| **NvencSimulator `Sleep(delayMs)`** | **模拟编码延迟** | ✅ 明确 SIMULATION 模块（输出文件头 `SIMULATION_OUTPUT=true`，报告标注 NOT TESTED），**非伪装** |
| StabilityTest `Sleep(1000)` | 采样周期 | 真实 |

## 5. 总判定

| 审计项 | 判定 |
|---|---|
| 1. 全仓库扫描（28 cpp / 35 h / 16 CMake + 5 组真实依赖） | **PASS** |
| 2. 入口检查（7 命令 + GUI 真实调用链） | **PASS** |
| 3. 调用链（WGC→Pipeline→IEncoder→IMuxer→MP4 + 音频 + 稳定性） | **PASS** |
| 4. 源码质量（0 TODO、0 假实现、Sleep 全部可解释、SIMULATION 明确标注） | **PASS** |
| 5. 结论：真实可编译 C++ 工程（非架构展示/非 HTML 演示） | **PASS** |

**最终结论：CODE REALITY — PASS。**

补充说明（事实，不修改代码）：
- 编译链 22 个 .cpp 全部在 CMake 中注册；6 个历史冻结文件明确未编译（记录在 CODE_FREEZE.md）
- 沙盒无法执行 MSVC/NVENC/FFmpeg 运行验证——编译与运行结论需真机回填（BUILD_RESULT.md / runtime_report.json），本审计仅证明**代码与调用链的真实存在性**。
