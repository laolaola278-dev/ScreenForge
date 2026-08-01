#pragma once

#include <string>

namespace sf {

// Phase 8-A — Smoke Test（真实环境检查，无模拟）
// 检查项：
//   1. D3D11 Device 创建（D3D11CreateDevice）
//   2. DXGI GPU 枚举（适配器 + 驱动版本）
//   3. FFmpeg 初始化（avformat/avcodec/avutil 版本查询）
//   4. WASAPI 音频设备枚举（渲染/捕获端点计数）
//   5. 编码器能力检测（NVENC 会话 + H264 能力；Simulation 构建标注不可用）
// 输出：smoke_report.json（每项 pass/fail + 信息 + 环境信息）
// 返回：0 = 全部通过；1 = 存在失败项
int RunSmokeTest(const std::string& reportPath);

} // namespace sf
