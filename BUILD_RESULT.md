# ScreenForge 真实构建结果

Phase RC-1 · 真实 Windows 构建验证（MSVC + CMake + Qt6）

> **状态：NOT_TESTED** —— 本报告由真实构建环境生成；当前（沙盒）未执行任何编译。

## 构建命令（真机执行）

```powershell
powershell -ExecutionPolicy Bypass -File scripts/windows_build.ps1 -Mode Hardware
# 或手动：
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.5.0/msvc2019_64 -DCMAKE_BUILD_TYPE=Release -DSF_REQUIRE_NVENC=ON
cmake --build build --config Release
windeployqt build/bin/Release/ScreenForge.exe
```

## 环境信息（真机填写）

| 项目 | 值 |
|---|---|
| Windows 版本 | — |
| CPU | — |
| GPU | — |
| NVIDIA 驱动版本 | — |
| CUDA/NVENC 版本 | — |
| Qt 版本 | — |
| FFmpeg 版本 | — |
| 编译时间 | — |
| MSVC 版本 | — |
| CMake 版本 | — |

## 编译结果

- [ ] Simulation Build 成功（`-DSF_REQUIRE_NVENC=OFF`）：**NOT_TESTED**
- [ ] Hardware Build 成功（`-DSF_REQUIRE_NVENC=ON`）：**NOT_TESTED**
- 产物 ScreenForge.exe：**NOT_BUILT**

## 编译错误（真机回填）

（如实粘贴编译器错误输出、修复措施；无错误则填「无」）

---

## 执行方法

详细步骤见 `docs/BUILD_SMOKE_TEST.md`；一键脚本见 `scripts/windows_build.ps1`。
