#include "RecorderEngine.h"

#include <Windows.h>

#include <cstdio>

#include "../capture/WgcCaptureSource.h"

namespace sf {
namespace {

int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

} // namespace

const char* RecorderStateName(RecorderState s) {
    switch (s) {
        case RecorderState::Idle:          return "Idle";
        case RecorderState::Initializing:  return "Initializing";
        case RecorderState::Ready:         return "Ready";
        case RecorderState::Recording:     return "Recording";
        case RecorderState::Stopping:      return "Stopping";
        case RecorderState::Error:         return "Error";
    }
    return "?";
}

RecorderEngine::~RecorderEngine() {
    if (m_state.load() == RecorderState::Recording) StopRecording();
}

bool RecorderEngine::Initialize(const RecorderConfig& cfg, IEncoder* encoder, IMuxer* muxer) {
    m_cfg = cfg;
    m_enc = encoder;
    m_mux = muxer;
    m_lastError.clear();
    m_state.store(RecorderState::Initializing);

    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    m_qpcFreq = f.QuadPart;

    if (!m_enc || !m_mux) {
        m_lastError = "encoder / muxer 为空";
        m_state.store(RecorderState::Error);
        return false;
    }

    // 初始化封装器（fragmented MP4）
    MuxConfig mc;
    mc.outputPath = cfg.mp4Path;
    mc.width  = cfg.width;
    mc.height = cfg.height;
    mc.fps    = cfg.fps;
    mc.extradata = cfg.extradata;
    if (!m_mux->Initialize(mc)) {
        m_lastError = "muxer 初始化失败（FFmpeg 是否可用？）: " + cfg.mp4Path;
        m_state.store(RecorderState::Error);
        return false;
    }

    m_state.store(RecorderState::Ready);
    return true;
}

bool RecorderEngine::StartRecording() {
    if (m_state.load() != RecorderState::Ready) {
        m_lastError = "状态不是 Ready（当前 " + std::string(RecorderStateName(m_state.load())) + "）";
        return false;
    }
    m_paused.store(false);
    m_pushed.store(0);
    m_startQpc = QpcNow();
    m_state.store(RecorderState::Recording);
    m_thread = std::thread(&RecorderEngine::ProducerLoop, this);
    return true;
}

bool RecorderEngine::StopRecording() {
    const RecorderState st = m_state.load();
    if (st != RecorderState::Recording && st != RecorderState::Stopping) return false;

    if (st == RecorderState::Recording) m_state.store(RecorderState::Stopping);
    if (m_thread.joinable()) m_thread.join();

    // 排空编码器（上限 15 秒）
    for (int k = 0; k < 300 && m_enc->Encoded() < m_enc->Submitted(); ++k) {
        Sleep(50);
    }

    if (m_mux->IsOpen()) m_mux->Finalize();
    WriteSessionJson();
    m_state.store(RecorderState::Ready);
    return true;
}

bool RecorderEngine::Pause() {
    if (m_state.load() != RecorderState::Recording) return false;
    m_paused.store(true);
    return true;
}

bool RecorderEngine::Resume() {
    if (m_state.load() != RecorderState::Recording) return false;
    m_paused.store(false);
    return true;
}

double RecorderEngine::DurationSec() const {
    if (m_qpcFreq <= 0 || m_startQpc == 0) return 0.0;
    return double(QpcNow() - m_startQpc) / double(m_qpcFreq);
}

// 生产者：60fps QPC 定速生成帧 → 编码器（帧队列/Pacing 语义）
void RecorderEngine::ProducerLoop() {
    const double freqD = double(m_qpcFreq);
    const int64_t intervalQpc = int64_t(freqD / double(m_cfg.fps));
    int64_t deadline = QpcNow();
    uint64_t idx = 0;

    while (m_state.load(std::memory_order_relaxed) == RecorderState::Recording) {
        if (!m_paused.load(std::memory_order_relaxed)) {
            CaptureFrame fr;
            if (m_cfg.captureSource) {
                // 真实捕获路径（Phase 1 WGC，真机可用）
                if (m_cfg.captureSource->GetFrame(fr)) {
                    m_enc->PushFrame(std::move(fr));
                    ++idx;
                }
            } else {
                // 沙盒路径：合成帧源
                if (!m_synth) {
                    m_synth = std::make_unique<SyntheticFrameSource>(
                        m_cfg.width, m_cfg.height, m_cfg.fps);
                }
                fr = m_synth->Generate(idx);
                m_enc->PushFrame(std::move(fr));
                ++idx;
            }
            m_pushed.store(idx);

            if (m_cfg.framesLimit > 0 && idx >= m_cfg.framesLimit) break;
        }

        // QPC pacing
        deadline += intervalQpc;
        int64_t now = QpcNow();
        const double remainMs = double(deadline - now) * 1000.0 / freqD;
        if (remainMs > 1.5) Sleep(DWORD(remainMs - 1.0));
        while (QpcNow() < deadline) {}
    }

    m_state.store(RecorderState::Stopping);   // 达到帧数上限自动进入 Stopping
}

void RecorderEngine::WriteSessionJson() {
    if (m_cfg.sessionJsonPath.empty()) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[64];
    std::snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    const uint64_t frames = m_enc ? m_enc->Encoded() : 0;
    const double dur = DurationSec();

    if (FILE* f = fopen(m_cfg.sessionJsonPath.c_str(), "w")) {
        fprintf(f, "{\n");
        fprintf(f, " \"session\": \"%s\",\n", ts);
        fprintf(f, " \"startTime\": \"%s\",\n", ts);
        fprintf(f, " \"resolution\": \"%ux%u\",\n", m_cfg.width, m_cfg.height);
        fprintf(f, " \"fps\": %u,\n", m_cfg.fps);
        fprintf(f, " \"codec\": \"%s\",\n",
                m_cfg.encoderIsSimulator ? "h264 (simulated by NvencSimulator)"
                                         : "h264 (NVENC)");
        fprintf(f, " \"encoder\": \"%s\",\n",
                m_cfg.encoderIsSimulator ? "NvencSimulator" : "NvEncoder");
        fprintf(f, " \"muxer\": \"Mp4Muxer (FFmpeg libavformat)\",\n");
        fprintf(f, " \"frames\": %llu,\n", (unsigned long long)frames);
        fprintf(f, " \"durationSec\": %.2f,\n", dur);
        fprintf(f, " \"output\": \"%s\",\n", m_cfg.mp4Path.c_str());
        fprintf(f, " \"realNvencHardware\": \"NOT TESTED (sandbox limitation)\"\n");
        fprintf(f, "}\n");
        fclose(f);
    }
}

RecorderEngine::~RecorderEngine() = default;

} // namespace sf
