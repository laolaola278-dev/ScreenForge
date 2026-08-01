# ScreenForge Release Ready 判定

Phase RC-5 · 最终发布判定

> **状态：NOT_RELEASED** —— 所有验收条件待真机验证。
> 规则：沙盒环境只能生成代码与测试工具，**不能生成测试通过结论**。

## 发布条件核对

| # | 条件 | 状态 |
|---|---|---|
| 1 | ✅ Windows 构建成功（BUILD_RESULT.md） | ⬜ NOT_TESTED |
| 2 | ✅ NVENC 真实工作（hardware_report.json realNvencHardware=true） | ⬜ NOT_TESTED |
| 3 | ✅ 1080p60 录制成功（recording.mp4 + ffprobe 验证） | ⬜ NOT_TESTED |
| 4 | ✅ 音频正常（recording_with_audio.mp4 含 AAC + 人工试听） | ⬜ NOT_TESTED |
| 5 | ✅ 2 小时稳定运行（STABILITY_REPORT.md errors 为空） | ⬜ NOT_TESTED |

**判定规则：以上 5 项全部满足才宣布 Release Candidate；任何一项 FAIL 则进入缺陷修复循环（仅 Bug/Build/Crash Fix）。**

## 证据链（真机回填后挂接）

| 证据 | 文件 |
|---|---|
| 构建结果 | BUILD_RESULT.md |
| 运行验证 | runtime_report.json（rc_verify.ps1 生成） |
| GUI 人工测试 | GUI_TEST_REPORT.md |
| 稳定性测试 | STABILITY_REPORT.md |
| 发布检查清单 | docs/RELEASE_CHECKLIST.md |

## 当前结论

**NOT_RELEASED** —— 等待真机执行：
```powershell
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware -RunSmoke
powershell -ExecutionPolicy Bypass -File scripts/rc_verify.ps1
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1
```

回填 5 项证据后，将最终结论更新于此。

## 发布产物（待打包）

- `dist/ScreenForge-0.8.0/`（exe + Qt/FFmpeg DLL + 插件 + version.json + README）
- `dist/ScreenForge-0.8.0-win64.zip`
