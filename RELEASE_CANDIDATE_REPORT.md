# ScreenForge Release Candidate 报告

Phase 8-C · 候选发布版本 `0.8.0`

## 版本信息

| 项目 | 值 |
|---|---|
| appName | ScreenForge |
| version | 0.8.0 |
| buildDate | **NOT_BUILT_YET**（version.json 待打包时生成） |
| gitCommit | unknown |

## 发布检查清单执行状态

| 类别 | 检查项 | 状态 |
|---|---|---|
| Build | Simulation Build | **NOT_TESTED** |
| Build | Hardware Build | **NOT_TESTED** |
| Build | 打包脚本（package_release.ps1 → dist/） | **NOT_TESTED** |
| Runtime | GUI 启动 | **NOT_TESTED** |
| Runtime | Smoke Test | **NOT_TESTED** |
| Runtime | 录制 60 秒 | **NOT_TESTED** |
| Runtime | 音频正常 | **NOT_TESTED** |
| Runtime | MP4 可播放 | **NOT_TESTED** |
| Performance | 1080p60 CPU 占用 | **NOT_TESTED** |
| Performance | GPU 占用 | **NOT_TESTED** |
| Performance | 内存增长 | **NOT_TESTED** |

> **诚实声明：** 当前生成环境为浏览器沙盒，无法执行 Windows 构建/运行。
> 以上全部项目标记 **NOT_TESTED**，未填写任何模拟数据。
> 真机执行步骤见 `docs/RELEASE_CHECKLIST.md` 与 `docs/BUILD_SMOKE_TEST.md`。

## 交付物清单（已创建，待真机验证）

| 交付物 | 路径 | 状态 |
|---|---|---|
| 发布检查清单 | docs/RELEASE_CHECKLIST.md | ✅ 已创建 |
| 打包脚本 | scripts/package_release.ps1 | ✅ 已创建（待执行） |
| 版本信息 | version.json | ✅ 已创建（buildDate 待打包填充） |
| 代码冻结规则 | CODE_FREEZE.md | ✅ 已生效 |
| 验收指南 | docs/HARDWARE_ACCEPTANCE.md | ✅ 已创建（8-B） |
| 构建指南 | docs/BUILD_SMOKE_TEST.md | ✅ 已创建（8-A） |

## 候选发布判定

- [ ] Build 两项通过
- [ ] Runtime 五项通过
- [ ] Performance 三项通过
- [ ] 所有检查项无 FAIL（或记录例外并经评审）
- [ ] **候选发布判定：待真机验证后回填**

## 失败原因记录（待回填）

（真机执行出现失败时在此记录：错误输出、修复措施、回归结果）
