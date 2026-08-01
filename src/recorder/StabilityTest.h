#pragma once

#include <cstdint>
#include <string>

namespace sf {

// Phase 5-B 真实录制稳定性测试
// 真实链路（禁止 Simulator）：
//   WGC → FramePipeline → NvEncoderHardware → Mp4Muxer → stability_test.mp4
// 每 10 秒输出：fps / encodedFrames / droppedFrames / GPU memory / CPU usage
//              / working set / encoder latency / queue depth
// 异常检测：GPU device removed · encoder failure · disk full · capture lost
// 输出：stability_report.json（duration/frames/avgFps/maxLatency/memoryGrowthMB/
//        gpuMemoryGrowthMB/failedFrames）
// 无 NVIDIA GPU / NVENC 不可用 → 直接失败退出（稳定性测试必须真实硬件，不回退）
int RunStabilityTest(double hours, uint32_t fps, uint32_t bitrateKbps,
                     const std::string& mp4Path,
                     const std::string& reportPath);

} // namespace sf
