# ScreenForge 真实硬件验收报告

Phase 8-B · 真实硬件验收

## 状态总览

| 测试 | 状态 |
|---|---|
| Test 1 — Smoke Test | **NOT_TESTED** |
| Test 2 — Simulation Recording | **NOT_TESTED** |
| Test 3 — Hardware Recording | **NOT_TESTED** |
| Test 4 — GUI 实际录制（人工） | **NOT_TESTED** |
| Test 5 — 稳定性测试（30 分钟） | **NOT_TESTED** |

> **诚实声明：** 当前生成环境为浏览器沙盒，无法执行 Windows API / MSVC / FFmpeg / NVENC / GUI。
> 本报告不填写任何模拟数据，全部项目标记 NOT_TESTED，待真机执行后回填。

## 测试机器（待回填）

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

## Test 1 — Smoke Test

- 命令：`ScreenForge.exe --smoke-test --json smoke_report.json`
- 结果：**NOT_TESTED**
- smoke_report.json 内容（待回填）：
  - D3D11 / DXGI / FFmpeg / WASAPI / NVENC 五项 pass/fail

## Test 2 — Simulation Recording

- 命令：`ScreenForge.exe --record-test --seconds 60`
- 结果：**NOT_TESTED**
- ffprobe 输出（待回填）：codec / resolution / fps / duration

## Test 3 — Hardware Recording

- 命令：`ScreenForge.exe --hardware-record-test --seconds 60`
- 结果：**NOT_TESTED**
- 要求：realNvencHardware=true（禁止 Simulator 伪装）
- hardware_report.json（待回填）：
  - gpuName
  - nvencVersion
  - encodedFrames
  - failedFrames
  - bitrateKbps

## Test 4 — GUI 实际录制（人工）

- 操作：打开 GUI → 选显示器 → 开始 → 5 分钟 → 停止
- 结果：**NOT_TESTED**
- 人工检查（待回填）：视频正常 / 声音正常 / 无卡顿

## Test 5 — 稳定性测试

- 命令：`ScreenForge.exe --stability-test --hours 0.5`
- 结果：**NOT_TESTED**
- 验收（待回填）：FPS 稳定 / 无崩溃 / 无明显音画偏移

## 失败原因（待回填）

（真机执行出现失败时在此记录）

---

## 执行方法（真机）

```powershell
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware -RunSmoke
# 或手动运行各测试命令（见 docs/HARDWARE_ACCEPTANCE.md）
```
