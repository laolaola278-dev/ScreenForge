# ScreenForge 稳定性测试报告

Phase RC-4 · 2 小时稳定性验证（真实硬件链路）

> **状态：NOT_TESTED** —— 需真机执行后回填。

## 执行命令

```powershell
# 2 小时稳定性测试（需真实 NVIDIA NVENC；Simulation 构建会明确报错退出）
build/bin/Release/ScreenForge.exe --stability-test --hours 2 `
  --out stability_2h.mp4 --json stability_report.json
```

## 验收标准（Phase 8-B 定义）

- 1080p60 连续 2 小时
- FPS 稳定（≥59）
- 无崩溃（errors 为空）
- 无明显音画偏移
- 内存增长 < 100MB

## 测试环境（真机回填）

| 项目 | 值 |
|---|---|
| GPU | — |
| 驱动 | — |
| 分辨率/帧率 | 1920x1080 @ 60 |
| 录制时长 | 2 小时 |
| 输出文件 | stability_2h.mp4 |

## 结果记录（真机回填）

| 检查项 | 结果 | 备注 |
|---|---|---|
| 是否崩溃 | NOT_TESTED | |
| 内存增长（MB） | NOT_TESTED | 验收 <100MB |
| GPU 显存增长（MB） | NOT_TESTED | |
| 丢帧数 | NOT_TESTED | 验收 0 |
| 音画同步 | NOT_TESTED | |
| FPS 稳定性 | NOT_TESTED | ≥59 |
| 编码失败帧 | NOT_TESTED | 验收 0 |
| 异常列表（stability_report.json errors） | NOT_TESTED | |

## 结论

- [ ] 2 小时稳定运行 —— **待真机回填**

## 失败记录（真机回填）

（崩溃转储、错误输出、修复措施；冻结期仅接受 Bug/Crash/Build Fix）
