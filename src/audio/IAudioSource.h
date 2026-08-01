#pragma once

#include <Windows.h>

#include <wrl/client.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <string>

#include "AudioFrame.h"

namespace sf {

// 音频捕获源接口（Phase 6-A）
// 每个源独立线程运行；GetFrame 非阻塞
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool GetFrame(AudioFrame& out) = 0;   // 非阻塞，FIFO
    virtual bool IsRunning() const = 0;
    virtual uint64_t FramesCaptured() const = 0;
    virtual std::string LastError() const = 0;
};

// ── WASAPI 公共助手（内联，供 AudioCapture / MicrophoneCapture 共用） ──
namespace wasapi_common {

inline int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

// 初始化 WASAPI 捕获客户端
// loopback=true  → 系统声音（eRender + LOOPBACK）
// loopback=false → 麦克风（eCapture）
// 输出固定 48kHz / stereo / 16bit（AUTOCONVERTPCM 由 WASAPI 负责转换）
inline bool InitCapture(bool loopback, IAudioClient** clientOut,
                        IAudioCaptureClient** captureOut, std::string& err) {
    *clientOut = nullptr;
    *captureOut = nullptr;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        err = "CoInitializeEx 失败";
        return false;
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { err = "MMDeviceEnumerator 创建失败"; return false; }

    Microsoft::WRL::ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture,
                                             loopback ? eConsole : eConsole, &device);
    if (FAILED(hr)) {
        err = loopback ? "无默认渲染设备（系统声音不可用）"
                       : "无默认捕获设备（麦克风不可用）";
        return false;
    }

    Microsoft::WRL::ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) { err = "IAudioClient 激活失败"; return false; }

    // 固定格式：48kHz / stereo / 16bit PCM
    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 2;
    fmt.nSamplesPerSec  = 48000;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = 2 * 2;
    fmt.nAvgBytesPerSec = 48000 * 2 * 2;
    fmt.cbSize          = 0;

    const REFERENCE_TIME hnsBuffer = 2000000;   // 200ms 缓冲
    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;   // WASAPI 自动转 48k stereo 16bit
    if (loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, hnsBuffer, 0, &fmt, nullptr);
    if (FAILED(hr)) {
        err = "IAudioClient::Initialize 失败（WASAPI 不可用）";
        return false;
    }

    Microsoft::WRL::ComPtr<IAudioCaptureClient> cap;
    hr = client->GetService(IID_PPV_ARGS(&cap));
    if (FAILED(hr)) { err = "GetService(IAudioCaptureClient) 失败"; return false; }

    *clientOut = client.Detach();
    *captureOut = cap.Detach();
    return true;
}

} // namespace wasapi_common

} // namespace sf
