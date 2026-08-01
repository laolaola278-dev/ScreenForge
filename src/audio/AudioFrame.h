#pragma once

#include <cstdint>
#include <vector>

namespace sf {

// 音频帧（Phase 6-A）
// 48kHz / stereo / PCM int16（当前阶段固定）
// captureQpc：捕获时刻 QPC 时间戳 —— 与视频 Frame.captureQpc 同一时间基准
struct AudioFrame {
    std::vector<int16_t> samples;   // 交错 PCM int16（channels 交错）
    uint32_t sampleRate = 48000;
    uint16_t channels   = 2;
    int64_t  captureQpc = 0;        // QPC ticks（同一 QPC clock）
};

} // namespace sf
