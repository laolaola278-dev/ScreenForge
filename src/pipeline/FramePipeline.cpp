#include "FramePipeline.h"

#include <Windows.h>

#include <cmath>
#include <utility>

#include "WgcCaptureSource.h"

namespace sf {

namespace {
constexpr double kTargetFps    = 60.0;                  // 目标帧率
constexpr double kFrameMs      = 1000.0 / kTargetFps;   // 16.667ms
constexpr double kSpinMs       = 0.8;                   // 自旋精修阈值
constexpr double kEmaAlpha     = 0.1;                   // EMA 平滑系数
} // namespace

FramePipeline::FramePipeline() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    m_qpcFreq = freq.QuadPart;
}

FramePipeline::~FramePipeline() { Stop(); }

bool FramePipeline::Start(WgcCaptureSource& source, Consumer consumer) {
    if (m_running.load()) return false;

    m_consumer = std::move(consumer);
    m_consumed.store(0);
    m_missed.store(0);
    m_inputFps.store(0.0);
    m_lastInQpc.store(0);
    m_avgIntervalMs.store(0.0);
    m_avgJitterMs.store(0.0);
    m_queue.Reset();

    m_running.store(true);

    // 生产端：WGC 回调线程 → SPSC 队列
    source.SetFrameCallback([this](CaptureFrame&& f) { OnCaptureFrame(std::move(f)); });

    if (!source.IsRunning() && !source.Start()) {
        m_running.store(false);
        return false;
    }

    m_thread = std::thread(&FramePipeline::ConsumerLoop, this, std::ref(source));
    return true;
}

void FramePipeline::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
    m_queue.Reset();          // 释放残留帧引用（无 GPU 纹理泄漏）
    m_consumer = {};
}

bool FramePipeline::IsRunning() const { return m_running.load(); }

void FramePipeline::OnCaptureFrame(CaptureFrame&& f) {
    // 生产端 Input FPS（EMA）
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    const int64_t now = q.QuadPart;
    const int64_t last = m_lastInQpc.exchange(now);
    if (last != 0 && m_qpcFreq > 0) {
        const double d = double(now - last);
        if (d > 0) {
            const double inst = double(m_qpcFreq) / d;
            const double cur  = m_inputFps.load();
            m_inputFps.store(cur == 0.0 ? inst : cur + (inst - cur) * kEmaAlpha);
        }
    }
    m_queue.Push(std::move(f));     // 满时丢弃最旧（队列内部处理）
}

void FramePipeline::ConsumerLoop(WgcCaptureSource& source) {
    const double freqD = double(m_qpcFreq);
    const int64_t frameQpc = int64_t(freqD * kFrameMs / 1000.0);

    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    const int64_t t0     = q.QuadPart;
    const int64_t endAt  = t0 + int64_t(freqD) * 60 * 30;   // 30 分钟上限保护
    int64_t deadline     = t0;
    int64_t lastEmit     = 0;

    while (m_running.load(std::memory_order_relaxed)) {
        const int64_t target = deadline;

        // QPC 高精度等待：睡到剩 ~0.8ms，随后自旋精修
        QueryPerformanceCounter(&q);
        int64_t now = q.QuadPart;
        const double remainMs = double(target - now) * 1000.0 / freqD;
        if (remainMs > kSpinMs + 0.5) Sleep(DWORD(remainMs - kSpinMs));
        do { QueryPerformanceCounter(&q); now = q.QuadPart; } while (now < target);

        if (now >= endAt) break;

        // 消费：弹出最新帧（取最新，队列已保证丢最旧）
        CaptureFrame f;
        if (m_queue.Pop(f)) {
            if (m_consumer) m_consumer(std::move(f));   // 消费端持有/释放
            m_consumed.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_missed.fetch_add(1, std::memory_order_relaxed);   // 空槽（无新帧）
        }

        // 帧间隔 / 抖动（EMA）
        if (lastEmit != 0) {
            const double intervalMs = double(now - lastEmit) * 1000.0 / freqD;
            const double jitterMs   = std::abs(intervalMs - kFrameMs);
            double cur = m_avgIntervalMs.load();
            m_avgIntervalMs.store(cur == 0.0 ? intervalMs
                                             : cur + (intervalMs - cur) * kEmaAlpha);
            cur = m_avgJitterMs.load();
            m_avgJitterMs.store(cur == 0.0 ? jitterMs
                                           : cur + (jitterMs - cur) * kEmaAlpha);
        }
        lastEmit = now;

        // 推进网格：落后时跳过中间槽位（零累积漂移），计为丢弃
        deadline += frameQpc;
        while (deadline <= now) {
            m_missed.fetch_add(1, std::memory_order_relaxed);
            deadline += frameQpc;
        }
    }
    m_running.store(false);
}

uint32_t FramePipeline::InputFps() const { return uint32_t(m_inputFps.load() + 0.5); }
uint32_t FramePipeline::QueueDepth() const { return m_queue.Size(); }
uint32_t FramePipeline::QueueCapacity() const { return m_queue.Capacity(); }
uint64_t FramePipeline::DroppedFrames() const {
    return m_queue.TotalDropped() + m_missed.load(std::memory_order_relaxed);
}
double FramePipeline::AvgFrameIntervalMs() const { return m_avgIntervalMs.load(); }
double FramePipeline::AvgJitterMs() const { return m_avgJitterMs.load(); }
uint64_t FramePipeline::ConsumedFrames() const { return m_consumed.load(); }

} // namespace sf
