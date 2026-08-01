#pragma once

// SANDBOX SIMULATOR — 模拟编码器（实现 IEncoder 接口）
// 模拟：1~5ms 编码延迟（平均 2ms）· 编码线程 · 数据包 sink → SimMuxer
// 明确标记：SIMULATOR · NOT real NVENC

#include <atomic>
#include <functional>
#include <thread>

#include "encoder/IEncoder.h"
#include "muxer/IMuxer.h"
#include "pipeline/FrameQueue.h"

namespace sf {

class SimEncoder : public IEncoder {
public:
    using PacketSink = std::function<void(const EncodedPacket&)>;

    SimEncoder() = default;
    ~SimEncoder() override { Shutdown(); }

    bool Initialize(ID3D11Device*, uint32_t width, uint32_t height,
                    uint32_t fps, uint32_t bitrateKbps,
                    const std::string& outputPath) override {
        m_w = width; m_h = height; m_fps = fps;
        (void)bitrateKbps; (void)outputPath;
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        m_qpcFreq = f.QuadPart;
        m_running.store(true);
        m_thread = std::thread(&SimEncoder::EncodeLoop, this);
        return true;
    }

    void Shutdown() override {
        if (!m_running.load()) return;
        m_running.store(false);
        if (m_thread.joinable()) m_thread.join();
    }

    void SetPacketSink(PacketSink sink) { m_sink = std::move(sink); }

    void PushFrame(CaptureFrame&& f) override {
        if (!m_running.load()) return;
        m_submitted.fetch_add(1, std::memory_order_relaxed);
        if (m_queue.Push(std::move(f)))
            m_dropped.fetch_add(1, std::memory_order_relaxed);   // 满丢最旧
    }

    bool IsRunning() const override { return m_running.load(); }
    uint64_t Submitted() const override { return m_submitted.load(); }
    uint64_t Encoded() const override { return m_encoded.load(); }
    uint64_t FailedFrames() const override { return m_failed.load(); }
    uint64_t DroppedFrames() const override { return m_dropped.load(); }
    double AvgLatencyMs() const override { return m_latencyMs.load(); }
    uint64_t BitstreamBytes() const override { return m_bytes.load(); }
    uint32_t QueueDepth() const { return m_queue.Size(); }
    uint32_t QueueCapacity() const { return m_queue.Capacity(); }

private:
    void EncodeLoop() {
        std::mt19937 rng(12345);
        std::uniform_real_distribution<double> jit(1.0, 5.0);   // 1~5ms
        while (true) {
            CaptureFrame f;
            if (!m_queue.Pop(f)) {
                if (!m_running.load()) break;
                Sleep(1);
                continue;
            }
            const double delayMs = jit(rng);
            const auto t0 = std::chrono::steady_clock::now();
            Sleep(DWORD(delayMs));
            const double latMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();

            // PTS：QPC → 100ns（与真实编码器同一换算）
            int64_t pts = 0;
            if (f.captureQpc > 0 && m_qpcFreq > 0)
                pts = (f.captureQpc * 10000000LL) / m_qpcFreq;
            else
                pts = f.index * (10000000LL / m_fps);

            EncodedPacket ep;
            ep.data = nullptr; ep.size = 4096;                 // 虚拟数据包
            ep.pts = pts; ep.dts = pts;
            ep.keyFrame = (f.index % (uint64_t(m_fps) * 2) == 1);
            if (m_sink) m_sink(ep);

            m_bytes.fetch_add(ep.size, std::memory_order_relaxed);
            const double cur = m_latencyMs.load();
            m_latencyMs.store(cur == 0.0 ? latMs : cur + (latMs - cur) * 0.1);
            m_encoded.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    FrameQueue m_queue{16};
    PacketSink m_sink;
    uint32_t m_w = 1920, m_h = 1080, m_fps = 60;
    int64_t m_qpcFreq = 0;
    std::atomic<uint64_t> m_submitted{0}, m_encoded{0}, m_failed{0}, m_dropped{0}, m_bytes{0};
    std::atomic<double> m_latencyMs{0.0};
};

} // namespace sf
