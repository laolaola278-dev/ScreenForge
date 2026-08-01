# ScreenForge 代码冻结

生效日期：Phase 8-C（Release Candidate 验证阶段）起生效。

## 冻结范围

当前版本 `0.8.0` 进入 Release Candidate 状态。此后对 `src/` 的修改**仅允许**：

- **Bug fix** — 修复运行时缺陷（崩溃、死锁、资源泄漏、数据错误）
- **Build fix** — 修复编译/链接/CMake 配置问题
- **Crash fix** — 修复崩溃（含异常处理、空指针、越界）

## 冻结期禁止

- ❌ 新功能（录制、编码、音频、封装、UI 任何新能力）
- ❌ 新架构（新增模块、新增抽象层、重构核心链路）
- ❌ 修改核心录制链路（WGC → Pipeline → Encoder → Muxer → RecorderEngine 的既有行为）
- ❌ AI 功能 / 插件系统 / 编辑器功能 / 云功能
- ❌ HTML / 网页展示层交付

## 变更流程

1. 所有变更必须**先记录**：在 commit message 或 issue 标注 `[BUGFIX]` / `[BUILD]` / `[CRASH]`
2. 变更后必须**回归验证**：相关测试入口重跑（--smoke-test / --record-test 等）
3. 冻结期评估：Release Candidate 验收通过（RELEASE_CANDIDATE_REPORT.md 全部 PASS 或记录例外）后方可解除冻结

## 当前例外（冻结前已存在，记录在案，冻结期内不处理）

- FFmpeg 为强制依赖（无 SF_REQUIRE_FFMPEG 选项）
- `src/app/MainWindow.cpp` / `VerifyRun.*` 为历史冻结文件（未编译）
- 音频尾帧补零策略（8-A 已修复，行为符合预期）
