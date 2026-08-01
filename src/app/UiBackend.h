#pragma once

// Phase 7-A — UI 后端实现（硬件装配层，位于 app 层而非 ui 层）
// 装配：WgcCaptureSource + RecorderEngine + NvEncoderHardware(或回退 NvencSimulator)
//       + Mp4Muxer + AudioCapture/MicrophoneCapture
// 本文件允许 include d3d11/nvenc/wasapi/ffmpeg（架构隔离：src/ui/ 内禁止）

#include <atomic>
#include <memory>
#include <thread>

#include "ui/IRecorderUiBackend.h"

namespace sf {

class RecorderEngine;
class WgcCaptureSource;
class IEncoder;
class IMuxer;
class AudioCapture;
class MicrophoneCapture;
class IAudioSource;

class UiBackend : public IRecorderUiBackend {
public:
    UiBackend();
    ~UiBackend() override;

    std::vector<CaptureTargetInfo> EnumMonitors() override;
    std::vector<CaptureTargetInfo> EnumWindows() override;

    bool Start(const UiStartConfig& cfg, const CaptureTargetInfo& target) override;
    void Stop() override;
    bool Pause() override;
    bool Resume() override;

    LiveStats Poll() override;
    bool IsRecording() const override;

private:
    void AudioDrainLoop();

    std::unique_ptr<RecorderEngine>  m_engine;
    std::unique_ptr<WgcCaptureSource> m_source;
    std::unique_ptr<IEncoder>         m_encoder;
    std::unique_ptr<IMuxer>           m_muxer;
    std::unique_ptr<AudioCapture>     m_sysAudio;
    std::unique_ptr<MicrophoneCapture> m_micAudio;

    std::thread m_audioThread;
    std::atomic<bool> m_audioRun{false};
    std::atomic<bool> m_paused{false};
    std::atomic<uint64_t> m_lastBytes{0};
    std::string m_lastError;
    int64_t m_qpcFreq = 0;
    int64_t m_lastCpuTick = 0;
    double  m_lastCpuPct = 0.0;
};

} // namespace sf
