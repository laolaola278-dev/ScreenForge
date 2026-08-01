#pragma once

#include <cstdint>
#include <string>

namespace sf {

// 模拟基准测试报告（对应 simulation_report.json）
struct SimulationReport {
    std::string mode = "SIMULATION";
    uint64_t framesSubmitted = 0;
    uint64_t framesEncoded = 0;
    uint64_t droppedFrames = 0;
    uint64_t failedFrames = 0;
    double   avgEncodeLatencyMs = 0.0;
    double   p95EncodeLatencyMs = 0.0;
    uint32_t queueMaxDepth = 0;
    bool     ptsContinuity = false;
    double   durationSec = 0.0;
    bool     outputMarked = false;   // simulation_test.h264 是否含 SIMULATION_OUTPUT=true 标记
};

// 运行完整录制链路模拟：
//   SyntheticFrameSource → FrameQueue → Pacing(60fps 网格) → IEncoder(NvencSimulator) → Bitstream
// 输出：simulation_test.h264（SIMULATION_OUTPUT=true 标记）+ simulation_report.json
// 返回 0 = 成功（链路跑通）；1 = 失败
int RunSimulationBenchmark(uint32_t width, uint32_t height, uint32_t fps,
                           uint64_t frames,
                           const std::string& outPath,
                           const std::string& reportPath,
                           SimulationReport* outReport = nullptr);

} // namespace sf
