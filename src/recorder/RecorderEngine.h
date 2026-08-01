#pragma once

// ScreenForge Phase 4-B — 录制总控（视频链路集成）
// 链路：Capture → Pipeline(帧队列+Pacing) → Encoder(IEncoder) → Muxer(IMuxer)
// 状态机：Idle → Initializing → Ready → Recording → Stopping → Ready | Error
// 沙盒模式：内置合成帧源 + NvencSimulator（允许）；禁止伪装真实 NVENC

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "encoder/IEncoder.h"
#include "muxer/IMuxer.h"

namespace sf {

class ICaptureSource;
class SyntheticFrameSource;

enum class RecorderState {
    Idle, Initializing, Ready, Recording, Stopping, Error
};
const char* RecorderStateName(RecorderState s);

struct RecorderConfig {
    uint32_t width = 1920, height = 1080, fps = 60, bitrateKbps = 12000;
    uint64_t framesLimit = 0;                 // 0 = 无限
    std::string mp4Path = "recording.mp4";
    std::string sessionJsonPath = "recording_session.json";
    std::vector<uint8_t> extradata;           // H264 SPS/PPS（Annex-B）
    ICaptureSource* captureSource = nullptr;  // 为空 → 内置合成帧源（沙盒）
    bool encoderIsSimulator = true;           // 会话 JSON 注明编码器模式（禁止伪装）
};

class RecorderEngine {
public:
    RecorderEngine() = default;
    ~RecorderEngine();

    bool Initialize(const RecorderConfig& cfg, IEncoder* encoder, IMuxer* muxer);
    bool StartRecording();
    bool StopRecording();
    bool Pause();
    bool Resume();

    RecorderState State() const { return m_state.load(); }
    const char* StateName() const;
    uint64_t FramesRecorded() const { return m_enc ? m_enc->Encoded() : 0; }
    uint64_t FramesPushed() const { return m_pushed.load(); }
    double   DurationSec() const;
    std::string LastError() const { return m_lastError; }

private:
    void ProducerLoop();
    void WriteSessionJson();

    RecorderConfig m_cfg;
    IEncoder* m_enc = nullptr;
    IMuxer*   m_mux = nullptr;
    std::unique_ptr<SyntheticFrameSource> m_synth;

    std::thread m_thread;
    std::atomic<RecorderState> m_state{RecorderState::Idle};
    std::atomic<bool> m_paused{false};
    std::atomic<uint64_t> m_pushed{0};
    int64_t m_startQpc = 0;
    int64_t m_qpcFreq = 0;
    std::string m_lastError;
};

} // namespace sf
