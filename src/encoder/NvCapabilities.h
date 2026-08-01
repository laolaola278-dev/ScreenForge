#pragma once

// Phase 7-B — 硬件能力检测结果（独立头，无 NVENC SDK 依赖）
// 供 recorder 测试模块在 SIMULATION-only 构建（SF_HAVE_NVENC_HW=0）下也可引用

#include <cstdint>
#include <string>

namespace sf {

struct NvHwCapabilities {
    bool        nvencAvailable = false;   // NVENC 会话可建立
    bool        h264Supported  = false;   // H264 编码能力
    std::string gpuName;                  // 如 "NVIDIA GeForce RTX 4070"
    std::string driverVersion;
    uint64_t    vramMB = 0;
    std::string nvencVersion;             // 如 "12.1"
    int         maxWidth  = 0;
    int         maxHeight = 0;
};

} // namespace sf
