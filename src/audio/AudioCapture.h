#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "IAudioSource.h"

namespace sf {

// 系统声音捕获（WASAPI Loopback）
// 独立线程运行；48kHz / stereo / int16；QPC 时间戳
// 有界队列（容量 64 帧，满丢最旧）——不阻塞、不影响视频线程
class AudioCapture : public IAudioSource {
public:
    AudioCapture() = default;
    ~AudioCapture() override;

    bool Start() override;
    void Stop() override;
    bool GetFrame(AudioFrame& out) override;
    bool IsRunning() const override { return m_running.load(); }
    uint64_t FramesCaptured() const override { return m_captured.load(); }
    std::string LastError() const override { return m_lastError; }

private:
    void CaptureLoop();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_captured{0};
    std::string m_lastError;

    std::mutex m_mtx;
    std::deque<AudioFrame> m_queue;      // 有界：满丢最旧
    static constexpr size_t kQueueMax = 64;

    Microsoft::WRL::ComPtr<IAudioClient>        m_client;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> m_capture;
};

} // namespace sf
