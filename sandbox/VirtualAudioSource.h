#pragma once

// SANDBOX SIMULATOR — 虚拟音频源
// 生成静音合成音频块（48kHz/stereo/int16，全 0 样本），仅用于链路与同步验证
// 明确标记：SIMULATOR · NOT real WASAPI

#include "audio/IAudioSource.h"

namespace sf {

class VirtualAudioSource : public IAudioSource {
public:
    VirtualAudioSource(uint32_t sampleRate = 48000, uint16_t channels = 2)
        : m_rate(sampleRate), m_ch(channels) {}

    bool Start() override { m_running = true; return true; }
    void Stop() override { m_running = false; }

    // 每 10ms 调用一次产出音频块（由 recorder 音频线程驱动）
    bool GetFrame(AudioFrame& out) override {
        if (!m_running) return false;
        const size_t perSec = 100;                 // 100 块/秒 ≈ 10ms/块
        const size_t samples = (size_t(m_rate) / perSec) * m_ch;
        out.samples.assign(samples, 0);            // 静音样本
        out.sampleRate = m_rate;
        out.channels   = m_ch;
        LARGE_INTEGER q{};
        QueryPerformanceCounter(&q);
        out.captureQpc = q.QuadPart;               // QPC 统一时钟（音画同步基准）
        ++m_frames;
        return true;
    }

    uint64_t FramesCaptured() const override { return m_frames; }
    bool IsRunning() const override { return m_running; }
    std::string LastError() const override { return {}; }

private:
    uint32_t m_rate;
    uint16_t m_ch;
    bool m_running = false;
    uint64_t m_frames = 0;
};

} // namespace sf
