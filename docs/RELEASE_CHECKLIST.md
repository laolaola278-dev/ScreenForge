# ScreenForge Release Checklist

Phase 8-C · 候选发布版本验证清单。
每项执行后填写：PASS / FAIL / NOT_TESTED + 备注。
规则：禁止伪造测试结果；未执行项目必须标记 NOT_TESTED。

---

## Build

- [ ] **Simulation Build 成功**
  - 命令：`scripts/windows_build.ps1 -Mode Simulation`
  - 预期：`[100%] Built target ScreenForge`
- [ ] **Hardware Build 成功**
  - 命令：`scripts/windows_build.ps1 -Mode Hardware`
  - 预期：`[100%] Built target ScreenForge`（需 NVENC SDK）

## Runtime

- [ ] **GUI 启动**
  - 命令：`ScreenForge.exe`（默认进入 GUI）
  - 预期：主窗口显示，无崩溃
- [ ] **Smoke Test 通过**
  - 命令：`ScreenForge.exe --smoke-test --json smoke_report.json`
  - 预期：return 0，5 项检查全 PASS
- [ ] **录制 60 秒成功**
  - 命令：`ScreenForge.exe --record-test --seconds 60`
  - 预期：生成 recording.mp4
- [ ] **音频正常**
  - 命令：`ScreenForge.exe --audio-record-test --seconds 30`
  - 预期：recording_with_audio.mp4 含 AAC 音频流，播放有声
- [ ] **MP4 可播放**
  - 播放器（VLC/MPC/WMP）打开录制产物，画面声音正常

## Performance（1080p60）

- [ ] **CPU 占用记录**
  - 录制时任务管理器记录（预期 <10%，参考）
- [ ] **GPU 占用记录**
  - 任务管理器 Video Encode 引擎占用记录
- [ ] **内存增长记录**
  - 30 分钟录制前后 working set 差值（预期 <100MB）

---

## 执行记录（真机回填）

| 检查项 | 结果 | 备注 |
|---|---|---|
| Simulation Build | | |
| Hardware Build | | |
| GUI 启动 | | |
| Smoke Test | | |
| 录制 60 秒 | | |
| 音频正常 | | |
| MP4 可播放 | | |
| CPU 占用 | | |
| GPU 占用 | | |
| 内存增长 | | |
