// Phase 7-A — UI 后端实现
// 硬件装配层（允许 include d3d11/nvenc/wasapi/ffmpeg）
// RecorderEngine 核心零修改：仅通过 Engine API（Initialize/Start/Stop/Pause/Resume）

#include "UiBackend.h"

#include <Windows.h>
#include <psapi.h>

#include <chrono>

#include "AudioCapture.h"
#include "MicrophoneCapture.h"
#include "Mp4Muxer.h"
#include "NvencSimulator.h"
#include "RecorderEngine.h"
#include "WgcCaptureSource.h"
#include "graphics/D3D11Device.h"
#ifdef SF_HAVE_NVENC_HW
#include "NvEncoderHardware.h"
#endif

#pragma comment(lib, "psapi.lib")

namespace sf {
namespace {

// 显示器枚举回调
struct MonInfo { HMONITOR h; RECT rc; };
BOOL CALLBACK EnumMonProc(HMONITOR h, HDC, LPRECT rc, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<MonInfo>*>(lp);
    v->push_back({ h, *rc });
    return TRUE;
}
// 窗口枚举回调
BOOL CALLBACK EnumWinProc(HWND h, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<HWND>*>(lp);
    if (IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
        wchar_t buf[256];
        if (GetWindowTextW(h, buf, 256) > 0) v->push_back(h);
    }
    return TRUE;
}

int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

} // namespace

UiBackend::UiBackend() {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    m_qpcFreq = f.QuadPart;
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        m_lastCpuTick = (int64_t(k.dwHighDateTime) << 32) | k.dwLowDateTime
                      + ((int64_t(u.dwHighDateTime) << 32) | u.dwLowDateTime);
    }
}

UiBackend::~UiBackend() { Stop(); }

std::vector<CaptureTargetInfo> UiBackend::EnumMonitors() {
    std::vector<CaptureTargetInfo> out;
    std::vector<MonInfo> ms;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&ms));
    int i = 0;
    for (const auto& m : ms) {
        CaptureTargetInfo t;
        t.id = i++;
        t.isWindow = false;
        t.handle = reinterpret_cast<uintptr_t>(m.h);
        t.width  = uint32_t(m.rc.right - m.rc.left);
        t.height = uint32_t(m.rc.bottom - m.rc.top);
        t.name = "显示器 " + std::to_string(i) + " · " +
                 std::to_string(t.width) + "x" + std::to_string(t.height);
        out.push_back(t);
    }
    return out;
}

std::vector<CaptureTargetInfo> UiBackend::EnumWindows() {
    std::vector<CaptureTargetInfo> out;
    std::vector<HWND> ws;
    EnumWindows(EnumWinProc, reinterpret_cast<LPARAM>(&ws));
    int i = 0;
    for (const auto& h : ws) {
        wchar_t buf[256];
        GetWindowTextW(h, buf, 256);
        CaptureTargetInfo t;
        t.id = i++;
        t.isWindow = true;
        t.handle = reinterpret_cast<uintptr_t>(h);
        char utf8[512];
        const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, 512, nullptr, nullptr);
        t.name = "窗口 · " + std::to_string(i) + " · " +
                 (n > 1 ? std::string(utf8) : std::string("(无标题)"));
        out.push_back(t);
    }
    return out;
}

bool UiBackend::Start(const UiStartConfig& cfg, const CaptureTargetInfo& target) {
    if (m_engine) Stop();

    m_lastError.clear();
    m_paused.store(false);

    // 1) D3D11 设备
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    if (!D3D11Device::Create(device, ctx)) {
        m_lastError = "D3D11 设备创建失败";
        return false;
    }

    // 2) 捕获源（显示器 / 窗口）
    m_source = std::make_unique<WgcCaptureSource>(device);
    if (target.isWindow) {
        m_source->SetCaptureTarget(nullptr, reinterpret_cast<HWND>(target.handle));
    } else {
        m_source->SetCaptureTarget(reinterpret_cast<HMONITOR>(target.handle), nullptr);
    }
    if (!m_source->Start()) {
        m_lastError = "WGC 捕获启动失败: " + m_source->LastError();
        return false;
    }

    // 3) 编码器（真实 NVENC，失败回退模拟器并标注）
#ifdef SF_HAVE_NVENC_HW
    auto hw = std::make_unique<NvEncoderHardware>();
    if (hw->Initialize(device.Get(), cfg.width, cfg.height, cfg.fps,
                       cfg.bitrateKbps, "")) {
        m_encoder = std::move(hw);
    } else {
        auto sim = std::make_unique<NvencSimulator>();
        if (!sim->Initialize(nullptr, cfg.width, cfg.height, cfg.fps,
                             cfg.bitrateKbps, "")) {
            m_lastError = "编码器初始化失败: " + sim->LastError();
            return false;
        }
        m_encoder = std::move(sim);   // 模拟器回退（UI 显示由 Poll 提供）
    }
#else
    auto sim = std::make_unique<NvencSimulator>();
    if (!sim->Initialize(nullptr, cfg.width, cfg.height, cfg.fps,
                         cfg.bitrateKbps, "")) {
        m_lastError = "编码器初始化失败（SIMULATION-only 构建）: " + sim->LastError();
        return false;
    }
    m_encoder = std::move(sim);       // SIMULATION-only：NvencSimulator
#endif

    // 4) 封装器（含音频流支持）
    MuxConfig mc;
    mc.outputPath = cfg.outputPath;
    mc.width = cfg.width; mc.height = cfg.height; mc.fps = cfg.fps;
#ifdef SF_HAVE_NVENC_HW
    if (auto* h = dynamic_cast<NvEncoderHardware*>(m_encoder.get())) {
        mc.extradata = h->SequenceParams();
    } else
#endif
    {
        static const std::vector<uint8_t> kSps = {
            0x00,0x00,0x00,0x01, 0x67,0x42,0x00,0x1e, 0x95,0xa8,0x14,0x01,
            0x6e,0x40,0x40,0x1e, 0x00,0x00,0x03,0x00, 0x10,0x00,0x00,0x03,
            0x03,0x20,0xf1,0x83, 0x19,0x60,
        };
        static const std::vector<uint8_t> kPps = {
            0x00,0x00,0x00,0x01, 0x68,0xce,0x3c,0x80,
        };
        mc.extradata.insert(mc.extradata.end(), kSps.begin(), kSps.end());
        mc.extradata.insert(mc.extradata.end(), kPps.begin(), kPps.end());
    }
    mc.audioEnabled = (cfg.audioMode != AudioMode::None);
    mc.audioSampleRate = 48000;
    mc.audioChannels = 2;

    m_muxer = std::make_unique<Mp4Muxer>();
    if (!m_muxer->Initialize(mc)) {
        m_lastError = "MP4 封装器初始化失败: " + m_muxer->LastError();
        return false;
    }

    // 5) 编码器数据包 → 封装器
#ifdef SF_HAVE_NVENC_HW
    if (auto* h = dynamic_cast<NvEncoderHardware*>(m_encoder.get())) {
        h->SetPacketSink([this](const EncodedPacket& p) { m_muxer->WritePacket(p); });
    } else
#endif
    if (auto* s = dynamic_cast<NvencSimulator*>(m_encoder.get())) {
        s->SetPacketSink([this](const EncodedPacket& p) { m_muxer->WritePacket(p); });
    }

    // 6) RecorderEngine（核心零修改，仅 API 调用）
    RecorderConfig rcfg;
    rcfg.width = cfg.width; rcfg.height = cfg.height; rcfg.fps = cfg.fps;
    rcfg.bitrateKbps = cfg.bitrateKbps;
    rcfg.framesLimit = 0;
    rcfg.mp4Path = cfg.outputPath;
    rcfg.sessionJsonPath = "";
    rcfg.extradata = mc.extradata;
    rcfg.captureSource = m_source.get();
    rcfg.encoderIsSimulator = (dynamic_cast<NvencSimulator*>(m_encoder.get()) != nullptr);

    m_engine = std::make_unique<RecorderEngine>();
    if (!m_engine->Initialize(rcfg, m_encoder.get(), m_muxer.get())) {
        m_lastError = "引擎初始化失败: " + m_engine->LastError();
        return false;
    }
    if (!m_engine->StartRecording()) {
        m_lastError = "启动录制失败: " + m_engine->LastError();
        return false;
    }

    // 7) 音频源（独立线程 → 封装器）
    if (cfg.audioMode == AudioMode::SystemOnly || cfg.audioMode == AudioMode::Both) {
        m_sysAudio = std::make_unique<AudioCapture>();
        if (!m_sysAudio->Start()) m_lastError = "系统声音不可用: " + m_sysAudio->LastError();
    }
    if (cfg.audioMode == AudioMode::MicOnly || cfg.audioMode == AudioMode::Both) {
        m_micAudio = std::make_unique<MicrophoneCapture>();
        if (!m_micAudio->Start()) m_lastError = "麦克风不可用: " + m_micAudio->LastError();
    }
    if (cfg.audioMode != AudioMode::None && (m_sysAudio || m_micAudio)) {
        m_audioRun.store(true);
        m_audioThread = std::thread(&UiBackend::AudioDrainLoop, this);
    }
    return true;
}

void UiBackend::Stop() {
    if (m_engine) {
        m_engine->StopRecording();
        m_engine.reset();
    }
    m_audioRun.store(false);
    if (m_audioThread.joinable()) m_audioThread.join();
    if (m_sysAudio) { m_sysAudio->Stop(); m_sysAudio.reset(); }
    if (m_micAudio) { m_micAudio->Stop(); m_micAudio.reset(); }
    if (m_encoder) { m_encoder->Shutdown(); m_encoder.reset(); }
    if (m_muxer) { m_muxer.reset(); }
    if (m_source) { m_source->Stop(); m_source.reset(); }
}

bool UiBackend::Pause() {
    if (!m_engine) return false;
    m_paused.store(true);
    return m_engine->Pause();
}

bool UiBackend::Resume() {
    if (!m_engine) return false;
    m_paused.store(false);
    return m_engine->Resume();
}

void UiBackend::AudioDrainLoop() {
    while (m_audioRun.load()) {
        Sleep(16);
        AudioFrame af;
        if (m_sysAudio) {
            while (m_sysAudio->GetFrame(af)) m_muxer->WriteAudioFrame(af);
        }
        if (m_micAudio) {
            while (m_micAudio->GetFrame(af)) { /* 麦克风：Phase 6 仅计数，不混音 */ }
        }
    }
}

LiveStats UiBackend::Poll() {
    LiveStats s;
    if (!m_engine) { s.state = "Idle"; return s; }

    const RecorderState st = m_engine->State();
    if (st == RecorderState::Recording) s.state = "Recording";
    else if (st == RecorderState::Stopping) s.state = "Stopping";
    else if (st == RecorderState::Error) s.state = "Error";
    else s.state = "Idle";
    if (m_paused.load() && s.state == "Recording") s.state = "Paused";

    s.fps = uint32_t(m_engine->FramesPushed() /
                     (m_engine->DurationSec() > 0.1 ? m_engine->DurationSec() : 1.0) + 0.5);
    s.durationSec = m_engine->DurationSec();

    // 码率 / 文件大小
    const uint64_t bytes = m_encoder ? m_encoder->BitstreamBytes() : 0;
    if (s.durationSec > 0.5) s.bitrateKbps = uint32_t(bytes * 8.0 / 1000.0 / s.durationSec);
    s.fileSizeMB = bytes / (1024 * 1024);

    // CPU（按 500ms 轮询间隔估算）
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        const int64_t tick = (int64_t(k.dwHighDateTime) << 32) | k.dwLowDateTime
                           + ((int64_t(u.dwHighDateTime) << 32) | u.dwLowDateTime);
        const double cpuSec = double(tick - m_lastCpuTick) / 1e7;
        m_lastCpuTick = tick;
        s.cpuPct = cpuSec / 0.5 * 100.0;
    }

    s.gpuMemMB = 0;
    s.lastError = m_lastError;
    return s;
}

bool UiBackend::IsRecording() const {
    return m_engine && m_engine->State() == RecorderState::Recording;
}

} // namespace sf
