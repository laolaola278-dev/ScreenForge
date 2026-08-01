#include "NvencSimulator.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace sf {

namespace {
constexpr uint32_t kBusyDropThreshold = 12;    // 编码器输入队列过深 → encoder busy 丢帧
constexpr double   kBaseLatencyMs     = 2.0;   // 平均编码延迟 2ms
constexpr double   kLatencyMinMs      = 1.0;
constexpr double   kLatencyMaxMs      = 5.0;
constexpr uint64_t kFailEveryN        = 10000; // 0.01% 随机编码失败
constexpr uint64_t kKeyInterval       = 120;   // 每 2 秒一个关键帧（60fps）
} // namespace

NvencSimulator::NvencSimulator() : m_rng(std::random_device{}()) {}
NvencSimulator::~NvencSimulator() { Shutdown(); }

bool NvencSimulator::Initialize(ID3D11Device* /*device*/, uint32_t width, uint32_t height,
                                uint32_t fps, uint32_t /*bitrateKbps*/,
                                const std::string& outputPath) {
    if (m_running.load()) return false;
    m_width = width; m_height = height; m_fps = fps;
    m_outPath = outputPath;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    m_qpcFreq = f.QuadPart;

    // 输出文件可选：为空 → 纯数据包模式（Phase 4-B：经 Sink 交给 Muxer）
    m_out = nullptr;
    if (!outputPath.empty()) {
        m_out = fopen(outputPath.c_str(), "wb");
        if (!m_out) return false;
        // 文件头标记：明确为模拟输出，禁止伪装真实 NVENC 码流
        fputs("SIMULATION_OUTPUT=true\n", m_out);
    }

    m_queue.Reset();
    m_running.store(true);
    m_thread = std::thread(&NvencSimulator::EncodeThread, this);
    return true;
}

void NvencSimulator::Shutdown() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();   // 线程会处理完队列剩余帧后退出（flush）
    if (m_out) { fclose(m_out); m_out = nullptr; }
}

void NvencSimulator::SetPacketSink(PacketSink sink) {
    std::lock_guard<std::mutex> lk(m_ptsMtx);
    m_sink = std::move(sink);
}

void NvencSimulator::PushFrame(CaptureFrame&& f) {
    if (!m_running.load(std::memory_order_relaxed)) return;
    m_submitted.fetch_add(1, std::memory_order_relaxed);
    if (m_queue.Push(std::move(f))) {           // queue full → 丢最旧
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void NvencSimulator::EncodeThread() {
    std::uniform_real_distribution<double> latencyJitter(-1.0, 3.0);  // 2+U(-1,3) → [1,5]ms
    std::vector<double> latSamples;

    while (true) {
        CaptureFrame f;
        if (!m_queue.Pop(f)) {
            if (!m_running.load(std::memory_order_relaxed)) break;    // 停止且队列空 → 退出
            Sleep(1);
            continue;
        }

        // encoder busy：输入队列过深 → 丢帧
        if (m_queue.Size() > kBusyDropThreshold) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // 模拟编码延迟：平均 2ms，范围 1~5ms
        const double delayMs = std::clamp(kBaseLatencyMs + latencyJitter(m_rng),
                                          kLatencyMinMs, kLatencyMaxMs);
        const auto t0 = std::chrono::steady_clock::now();
        Sleep(DWORD(delayMs));
        const double latMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        // 0.01% 随机编码失败
        if (m_rng() % kFailEveryN == 0) {
            m_failed.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // PTS：QPC 捕获时间戳 → 100ns（与真实 NvEncoder 换算一致）
        int64_t pts = 0;
        if (f.captureQpc > 0 && m_qpcFreq > 0) {
            pts = (f.captureQpc * 10000000LL) / m_qpcFreq;
        } else {
            pts = f.index * (10000000LL / m_fps);
        }
        {
            std::lock_guard<std::mutex> lk(m_ptsMtx);
            m_ptsSeq.push_back(pts);
        }

        // 模拟码流 packet
        const uint32_t payload = uint32_t(2048 + (f.index % 16) * 512);
        if (m_out) {
            uint32_t hdr[4] = { uint32_t(f.index), uint32_t(pts >> 32),
                                uint32_t(pts & 0xFFFFFFFF), payload };
            fwrite(hdr, 1, sizeof(hdr), m_out);
            static const uint8_t kPad[512] = { 0x5A };
            for (uint32_t n = payload; n > 0; n -= 512) {
                fwrite(kPad, 1, n < 512 ? n : 512, m_out);
            }
            m_bytes.fetch_add(sizeof(hdr) + payload, std::memory_order_relaxed);
        }

        // Phase 4-B：经数据包回调交给 Muxer（RecorderEngine 链路）
        {
            std::lock_guard<std::mutex> lk(m_ptsMtx);
            m_pktBuf.assign(payload, 0x5A);
            EncodedPacket ep;
            ep.data     = m_pktBuf.data();
            ep.size     = m_pktBuf.size();
            ep.pts      = pts;
            ep.dts      = pts;
            ep.keyFrame = (f.index % kKeyInterval == 1);
            if (m_sink) m_sink(ep);
        }

        latSamples.push_back(latMs);
        const double cur = m_latencyMs.load();
        m_latencyMs.store(cur == 0.0 ? latMs : cur + (latMs - cur) * 0.1);
        m_encoded.fetch_add(1, std::memory_order_relaxed);
    }

    // p95 延迟
    if (!latSamples.empty()) {
        std::sort(latSamples.begin(), latSamples.end());
        const size_t idx = std::min(latSamples.size() - 1,
                                    size_t(latSamples.size() * 0.95));
        m_p95Ms.store(latSamples[idx]);
    }
}

bool NvencSimulator::IsRunning() const { return m_running.load(); }
uint64_t NvencSimulator::Submitted() const { return m_submitted.load(std::memory_order_relaxed); }
uint64_t NvencSimulator::Encoded() const { return m_encoded.load(std::memory_order_relaxed); }
uint64_t NvencSimulator::FailedFrames() const { return m_failed.load(std::memory_order_relaxed); }
uint64_t NvencSimulator::DroppedFrames() const { return m_dropped.load(std::memory_order_relaxed); }
double NvencSimulator::AvgLatencyMs() const { return m_latencyMs.load(); }
double NvencSimulator::P95LatencyMs() const { return m_p95Ms.load(); }
uint64_t NvencSimulator::BitstreamBytes() const { return m_bytes.load(std::memory_order_relaxed); }

std::vector<int64_t> NvencSimulator::TakePtsSequence() {
    std::lock_guard<std::mutex> lk(m_ptsMtx);
    return m_ptsSeq;
}

} // namespace sf
