# ScreenForge 真实构建报告

Phase 8-A · 真实 Windows 构建与运行验证

## 状态总览

| 项目 | 状态 |
|---|---|
| 交付物：scripts/windows_build.ps1 | ✅ 已创建（待真机执行） |
| 交付物：SmokeTest（--smoke-test） | ✅ 代码已创建（待真机执行） |
| 交付物：Mp4Muxer 尾帧修复 | ✅ 代码已修复（待真机验证） |
| cmake configure | **NOT_TESTED**（本环境无 Windows/MSVC/CMake） |
| cmake build | **NOT_TESTED** |
| windeployqt 部署 | **NOT_TESTED** |
| --smoke-test | **NOT_TESTED** |
| 测试入口运行 | **NOT_TESTED** |

> **诚实声明：** 当前生成环境为浏览器沙盒，无法执行 Windows API、MSVC、CMake、FFmpeg、NVENC。
> 本报告不伪造任何成功结果。以下全部为**待真机执行后回填**的项目。

## 环境信息（待回填）

| 项目 | 值 |
|---|---|
| Windows 版本 | — |
| MSVC 版本 | — |
| Qt 版本 | — |
| CMake 版本 | — |
| NVIDIA Video Codec SDK | — |
| FFmpeg dev | — |
| GPU | — |
| 驱动版本 | — |

## 编译结果（待回填）

- Simulation 构建（`-DSF_REQUIRE_NVENC=OFF`）：待执行
- Hardware 构建（`-DSF_REQUIRE_NVENC=ON`）：待执行
- 预期目标：`[100%] Built target ScreenForge`

## 运行结果（待回填）

- `--smoke-test` → smoke_report.json：5 项检查（D3D11 / DXGI / FFmpeg / WASAPI / Encoder）
- 测试入口：--simulate-encode / --mux-bench / --record-test / --hardware-record-test / --stability-test

## 失败原因（待回填）

（若真机执行出现失败，在此记录：错误输出、修复措施）

---

## 执行方法（真机）

```powershell
# Simulation 构建 + 冒烟测试
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Simulation -RunSmoke

# Hardware 构建（需 NVENC SDK）+ 冒烟测试
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware -RunSmoke

# 手动冒烟
build/bin/Release/ScreenForge.exe --smoke-test --json smoke_report.json
```

详细步骤见 `docs/BUILD_SMOKE_TEST.md`。
