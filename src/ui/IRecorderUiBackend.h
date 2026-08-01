#pragma once

// Phase 7-A — UI ↔ 录制核心 桥接接口（架构隔离）
// 本头文件只含纯数据结构 + 抽象接口，不 include 任何硬件头
// （d3d11 / nvenc / wasapi / ffmpeg 均禁止出现在 src/ui/）
// 真实实现：src/app/UiBackend.cpp（装配 WGC/NVENC/WASAPI/Mp4Muxer/RecorderEngine）

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// 录制目标（显示器 / 窗口）
struct CaptureTargetInfo {
    int         id = 0;
    std::string name;        // 如 "显示器 1 · 1920x1080" 或窗口标题
    bool        isWindow = false;
    uintptr_t   handle = 0;  // HMONITOR / HWND（由 backend 解释）
    uint32_t    width = 0, height = 0;
};

enum class AudioMode { None, SystemOnly, MicOnly, Both };

// 实时统计
struct LiveStats {
    uint32_t    fps = 0;
    uint32_t    bitrateKbps = 0;      // 估算
    double      durationSec = 0.0;
    uint64_t    fileSizeMB = 0;
    double      cpuPct = 0.0;
    uint32_t    gpuMemMB = 0;
    std::string state;                 // Idle / Recording / Paused / Error
    std::string lastError;
};

// UI 启动参数（来自 config.json）
struct UiStartConfig {
    std::string outputPath = "recording.mp4";
    uint32_t    width  = 1920, height = 1080;
    uint32_t    fps    = 60;
    uint32_t    bitrateKbps = 12000;
    int         targetId = 0;
    AudioMode   audioMode = AudioMode::None;
};

class IRecorderUiBackend {
public:
    virtual ~IRecorderUiBackend() = default;

    // 枚举
    virtual std::vector<CaptureTargetInfo> ListMonitors() = 0;
    virtual std::vector<CaptureTargetInfo> ListWindows() = 0;

    // 控制（内部装配 RecorderEngine + 编码器 + 封装器 + 音频）
    virtual bool Start(const UiStartConfig& cfg, const CaptureTargetInfo& target) = 0;
    virtual void Stop() = 0;
    virtual bool Pause() = 0;          // true=已暂停
    virtual bool Resume() = 0;

    virtual LiveStats Poll() = 0;      // 实时状态（500ms 轮询）
    virtual bool IsRecording() const = 0;
};

} // namespace sf
