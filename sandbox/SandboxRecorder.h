#pragma once

// SANDBOX SIMULATOR — 沙盒录制器（组装模拟链路，不修改核心 RecorderEngine）
//   SyntheticCapture ─┐
//                     ├→ FrameQueue(SPSC) → SimEncoder → SimMuxer → 虚拟 MP4/报告
//   VirtualAudioSource┘（音频线程独立）
// 状态机：Idle / Recording / Paused / Stopping / Error

#include <atomic>
#include <memory>
#include <thread>

#include "SimEncoder.h"
#include "SimMuxer.h"
#include "SyntheticCapture.h"
#include "VirtualAudioSource.h"
#include "pipeline/FrameQueue.h"

namespace sf {

class SandboxRecorder {
public:
    SandboxRecorder() = default;
    ~SandboxRecorder() { Stop(); }

    bool Start(uint32_t width, uint32_t height, uint32_t fps,
               uint64_t framesLimit, const std::string& mp4Path,
               const std::string& reportPath) {
        if (m_state.load() != State::Idle) return false;
        m_w = width; m_h = height; m_fps = fps;
        m_framesLimit = framesLimit;
        m_mp4Path = mp4Path; m_reportPath = reportPath;

        m_capture = std::make_unique<SyntheticCapture>(width, height, fps);
        m_audio   = std::make_unique<VirtualAudioSource>(48000, 2);
        m_encoder = std::make_unique<SimEncoder>();
        m_muxer   = std::make_unique<SimMuxer>();

        MuxConfig mc;
        mc.outputPath = mp4Path;
        mc.width = width; mc.height = height; mc.fps = fps;
        if (!m_muxer->Initialize(mc)) return false;
        m_encoder->SetPacketSink([this](const EncodedPacket& p) { m_muxer->WritePacket(p); });
        m_encoder->Initialize(nullptr, width, height, fps, 12000, "");
        m_capture->Start();
        m_audio->Start();

        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        m_qpcFreq = f.QuadPart;
        m_startQpc = QpcNow();
        m_pushed = 0; m_paused.store(false);
        m_state.store(State::Recording);
        m_thread = std::thread(&SandboxRecorder::CaptureLoop, this);
        return true;
    }

    void Stop() {
        const State st = m_state.load();
        if (st != State::Recording && st != State::Paused) return;
        m_state.store(State::Stopping);
        if (m_thread.joinable()) m_thread.join();
        m_capture->Stop();
        m_audio->Stop();
        m_encoder->Shutdown();
        m_muxer->Finalize();
        WriteReport();
        m_state.store(State::Idle);
    }

    bool Pause() {
        if (m_state.load() != State::Recording) return false;
        m_paused.store(true);
        m_state.store(State::Paused);
        return true;
    }
    bool Resume() {
        if (m_state.load() != State::Paused) return false;
        m_paused.store(false);
        m_state.store(State::Recording);
        return true;
    }

    enum class State { Idle, Recording, Paused, Stopping, Error };
    State GetState() const { return m_state.load(); }
    bool IsRecording() const {
        const State s = m_state.load();
        return s == State::Recording || s == State::Paused;
    }

    uint64_t Frames() const { return m_encoder ? m_encoder->Encoded() : 0; }
    uint64_t Pushed() const { return m_pushed.load(); }
    uint64_t Dropped() const { return m_encoder ? m_encoder->DroppedFrames() : 0; }
    double LatencyMs() const { return m_encoder ? m_encoder->AvgLatencyMs() : 0.0; }
    uint32_t QueueDepth() const { return m_encoder ? m_encoder->QueueDepth() : 0; }
    double DurationSec() const {
        if (m_qpcFreq <= 0 || m_startQpc == 0) return 0.0;
        return double(QpcNow() - m_startQpc) / double(m_qpcFreq);
    }
    uint32_t Fps() const {
        const double d = DurationSec();
        return d > 0.5 ? uint32_t(double(Frames()) / d + 0.5) : 0;
    }
    double AudioSyncMs() const {
        // 音画同步偏移：视频首帧 PTS(100ns) vs 音频首帧 QPC→100ns
        if (!m_muxer || !m_muxer->HaveVideoStart() || !m_muxer->HaveAudioStart())
            return -1.0;
        const int64_t audioPts = (m_muxer->AudioStartQpc() * 10000000LL) / m_qpcFreq;
        return double(std::llabs(m_muxer->VideoStartPts() - audioPts)) / 10000.0;
    }

private:
    static int64_t QpcNow() {
        LARGE_INTEGER q{};
        QueryPerformanceCounter(&q);
        return q.QuadPart;
    }

    void CaptureLoop() {
        const double freqD = double(m_qpcFreq);
        const int64_t intervalQpc = int64_t(freqD / double(m_fps));
        int64_t deadline = QpcNow();
        uint64_t idx = 0;

        while (m_state.load() == State::Recording || m_state.load() == State::Paused) {
            if (!m_paused.load()) {
                CaptureFrame f;
                if (m_capture->GetFrame(f)) {
                    m_encoder->PushFrame(std::move(f));   // SPSC（满丢最旧）
                    ++idx;
                    m_pushed.store(idx);
                    if (m_framesLimit > 0 && idx >= m_framesLimit) break;
                }
                // 音频线程：每 10ms 一帧
                AudioFrame af;
                while (m_audio->GetFrame(af)) m_muxer->WriteAudioFrame(af);
            }
            // QPC pacing（60fps）
            deadline += intervalQpc;
            int64_t now = QpcNow();
            const double remainMs = double(deadline - now) * 1000.0 / freqD;
            if (remainMs > 1.5) Sleep(DWORD(remainMs - 1.0));
            while (QpcNow() < deadline) {}
        }
        m_state.store(State::Stopping);
    }

    void WriteReport() {
        FILE* f = fopen(m_reportPath.c_str(), "w");
        if (!f) return;
        fprintf(f, "{\n");
        fprintf(f, " \"mode\": \"SANDBOX\",\n");
        fprintf(f, " \"capture\": \"Synthetic\",\n");
        fprintf(f, " \"encoder\": \"Simulator\",\n");
        fprintf(f, " \"muxer\": \"SimMuxer (virtual MP4 report)\",\n");
        fprintf(f, " \"fps\": %u,\n", Fps());
        fprintf(f, " \"frames\": %llu,\n", (unsigned long long)Frames());
        fprintf(f, " \"duration\": %.2f,\n", DurationSec());
        fprintf(f, " \"latency\": %.2f,\n", LatencyMs());
        fprintf(f, " \"droppedFrames\": %llu,\n", (unsigned long long)Dropped());
        fprintf(f, " \"audioSync\": %.2f,\n", AudioSyncMs());
        fprintf(f, " \"queueDepth\": %u,\n", QueueDepth());
        fprintf(f, " \"note\": \"SANDBOX SIMULATION ONLY — NOT real WGC/NVENC/FFmpeg\"\n");
        fprintf(f, "}\n");
        fclose(f);
    }

    std::thread m_thread;
    std::atomic<State> m_state{State::Idle};
    std::atomic<bool> m_paused{false};
    std::atomic<uint64_t> m_pushed{0};

    std::unique_ptr<SyntheticCapture>  m_capture;
    std::unique_ptr<VirtualAudioSource> m_audio;
    std::unique_ptr<SimEncoder>         m_encoder;
    std::unique_ptr<SimMuxer>           m_muxer;

    uint32_t m_w = 1920, m_h = 1080, m_fps = 60;
    uint64_t m_framesLimit = 0;
    std::string m_mp4Path, m_reportPath;
    int64_t m_qpcFreq = 0, m_startQpc = 0;
};

} // namespace sf
