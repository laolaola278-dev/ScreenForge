// Phase 6-A — 系统声音捕获（WASAPI Loopback）
// 独立线程：WASAPI GetBuffer → AudioFrame{captureQpc} → 有界队列（满丢最旧）
// 视频线程不受任何阻塞。

#include "AudioCapture.h"

#include <algorithm>

namespace sf {

AudioCapture::~AudioCapture() { Stop(); }

bool AudioCapture::Start() {
    if (m_running.load()) return true;
    m_lastError.clear();

    if (!wasapi_common::InitCapture(true, m_client.GetAddressOf(),
                                    m_capture.GetAddressOf(), m_lastError)) {
        return false;
    }
    if (FAILED(m_client->Start())) {
        m_lastError = "IAudioClient::Start 失败";
        return false;
    }

    m_running.store(true);
    m_thread = std::thread(&AudioCapture::CaptureLoop, this);
    return true;
}

void AudioCapture::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
    if (m_client) { m_client->Stop(); m_client.Reset(); }
    m_capture.Reset();
    std::lock_guard<std::mutex> lk(m_mtx);
    m_queue.clear();
}

void AudioCapture::CaptureLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        Sleep(8);
        UINT32 n = 0;
        if (FAILED(m_capture->GetNextPacketSize(&n)) || n == 0) continue;
        while (n > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            const HRESULT hr = m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (SUCCEEDED(hr) && frames > 0) {
                AudioFrame af;
                af.sampleRate = 48000;
                af.channels   = 2;
                const size_t cnt = size_t(frames) * 2;   // stereo int16
                af.samples.resize(cnt);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::fill(af.samples.begin(), af.samples.end(), 0);
                } else if (data) {
                    std::memcpy(af.samples.data(), data, cnt * sizeof(int16_t));
                }
                af.captureQpc = wasapi_common::QpcNow();   // 同一 QPC clock

                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    if (m_queue.size() >= kQueueMax) m_queue.pop_front();  // 满丢最旧
                    m_queue.push_back(std::move(af));
                }
                m_captured.fetch_add(1, std::memory_order_relaxed);
            }
            if (FAILED(m_capture->ReleaseBuffer(frames))) break;
            if (FAILED(m_capture->GetNextPacketSize(&n))) break;
        }
    }
}

bool AudioCapture::GetFrame(AudioFrame& out) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_queue.empty()) return false;
    out = std::move(m_queue.front());
    m_queue.pop_front();
    return true;
}

} // namespace sf
