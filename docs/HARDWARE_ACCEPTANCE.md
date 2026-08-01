# ScreenForge 硬件验收指南

Phase 8-B · 目标：在真实 Windows + NVIDIA 机器上验证录屏链路真实可用。
规则：所有未执行项目标记 NOT_TESTED，禁止填写假数据。

## 测试机器信息（真机填写）

| 项目 | 值 |
|---|---|
| CPU | — |
| GPU | — |
| GPU Driver | — |
| RAM | — |
| Windows 版本 | — |
| Qt 版本 | — |
| FFmpeg 版本 | — |
| NVENC 版本 | — |

---

## Test 1 — Smoke Test

```powershell
build/bin/Release/ScreenForge.exe --smoke-test --json smoke_report.json
```

检查项（smoke_report.json 逐项 pass/fail）：
- D3D11 Device 创建
- DXGI GPU 枚举
- FFmpeg 初始化
- WASAPI 音频端点
- NVENC 编码器能力（Hardware 构建下应 PASS）

预期：`SMOKE TEST: PASS`（return 0）。

## Test 2 — Simulation Recording

```powershell
build/bin/Release/ScreenForge.exe --record-test --seconds 60 --out recording.mp4 --json recording_session.json
```

检查：
- 生成 recording.mp4（沙盒视频链路 + MP4 封装）
- ffprobe 验证：
```powershell
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,avg_frame_rate -show_entries format=duration -of default=noprint_wrappers=1 recording.mp4
```
预期：codec=h264 · 1920x1080 · 60fps · duration≈60s

## Test 3 — Hardware Recording

```powershell
build/bin/Release/ScreenForge.exe --hardware-record-test --seconds 60 --out recording_hw.mp4 --json hardware_report.json
```

要求：
- 必须真实 NVENC（hardware_report.json 中 realNvencHardware=true；若为 false 则记录失败原因并停止后续测试）
- 禁止 Simulator 伪装

输出 hardware_report.json 必须包含：
- gpuName
- nvencVersion
- encodedFrames
- failedFrames
- bitrateKbps
- realNvencHardware

## Test 4 — GUI 实际录制（人工操作）

1. 打开 ScreenForge（GUI）
2. 选择显示器目标
3. 点击「开始录制」
4. 运行 5 分钟（桌面正常操作）
5. 点击「停止录制」

人工检查（如实记录）：
- 视频是否正常（播放无花屏/绿屏）
- 声音是否正常（如启用音频）
- 是否卡顿（UI/录制帧率）

## Test 5 — 稳定性测试

```powershell
build/bin/Release/ScreenForge.exe --stability-test --hours 0.5 --out stability_30min.mp4 --json stability_report.json
```

1080p60 · 30 分钟。验收：
- FPS 稳定（≥59）
- 无崩溃（errors 为空）
- 无明显音画偏移
- memoryGrowth < 100MB（参考值）

---

## 结果记录

每项测试结果填入 `REAL_HARDWARE_ACCEPTANCE_REPORT.md`：
- PASS / FAIL / NOT_TESTED
- 失败原因
- 关键输出（ffprobe / 报告文件内容节选）
