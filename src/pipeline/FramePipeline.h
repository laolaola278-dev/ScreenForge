#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "FrameQueue.h"
#include "graphics/Frame.h"

namespace sf {

class WgcCaptureSource;

// Phase 2：GPU 帧管线
//   WGC Capture Thread → SPSC FrameQueue → Frame Consumer Thread（Pacing 60fps）
// 帧所有权：Capture 构造 → Queue 持有 → Consumer 消费/释放
class FramePipeline {
public:
    using Consumer = std::function<void(CaptureFrame&&)>;

    FramePipeline();
    ~FramePipeline();

    // 启动：订阅捕获回调 + 启动消费线程（QPC 60fps 网格）
    bool Start(WgcCaptureSource& source, Consumer consumer = {});
    void Stop();                       // 停止并释放全部残留帧
    bool IsRunning() const;

    // 统计（UI 轮询读取）
    uint32_t InputFps() const;         // 生产端实测帧率（EMA）
    uint32_t QueueDepth() const;
    uint32_t QueueCapacity() const;
    uint64_t DroppedFrames() const;    // 队列丢弃 + Pacing 错过槽位
    double   AvgFrameIntervalMs() const;
    double   AvgJitterMs() const;
    uint64_t ConsumedFrames() const;

private:
    void OnCaptureFrame(CaptureFrame&& f);   // 生产端（WGC 回调线程）
    void ConsumerLoop(WgcCaptureSource& source);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    Consumer m_consumer;

    FrameQueue m_queue{8};             // 可用容量 7（8 槽含守卫）

    // 生产端统计
    std::atomic<double>  m_inputFps{0.0};
    std::atomic<int64_t> m_lastInQpc{0};
    // 消费端统计
    std::atomic<uint64_t> m_consumed{0};
    std::atomic<uint64_t> m_missed{0};
    std::atomic<double>   m_avgIntervalMs{0.0};
    std::atomic<double>   m_avgJitterMs{0.0};

    int64_t m_qpcFreq = 0;
};

} // namespace sf
