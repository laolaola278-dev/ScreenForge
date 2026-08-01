#pragma once

// ScreenForge Phase 5-A — 真实 NVENC 硬件编码器（实现 IEncoder 接口）
// 链路：WGC ID3D11Texture2D（BGRA）→ GPU CS 转 NV12（BT.709）→ nvEncRegisterResource
//       → nvEncEncodePicture（D3D11 Session，PTS=QPC→100ns）→ nvEncLockBitstream
//       → EncodedPacket → Mp4Muxer → recording.mp4
// 禁止：CPU Map / Staging Texture / GPU→CPU 像素回读（全程 GPU 驻留）
// 无 NVIDIA GPU / NVENC 不可用 → Initialize 返回 false → 上层自动回退 NvencSimulator

#include <nvEncodeAPI.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "encoder/IEncoder.h"
#include "graphics/Frame.h"
#include "muxer/IMuxer.h"
#include "pipeline/FrameQueue.h"

#include "NvCapabilities.h"   // 7-B：能力结构独立（无 SDK 依赖）

namespace sf {

class NvEncoderHardware : public IEncoder {
public:
    using PacketSink = std::function<void(const EncodedPacket&)>;

    NvEncoderHardware() = default;
    ~NvEncoderHardware() override;

    // 硬件能力检测（启动时输出 GPU 型号 / NVENC 版本 / 编码能力）
    static NvHwCapabilities DetectCapabilities();

    bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
                    uint32_t fps, uint32_t bitrateKbps,
                    const std::string& outputPath) override;   // outputPath 可为空（纯数据包模式）
    void Shutdown() override;
    void PushFrame(CaptureFrame&& f) override;
    bool IsRunning() const override;

    uint64_t Submitted() const override;
    uint64_t Encoded() const override;
    uint64_t FailedFrames() const override;
    uint64_t DroppedFrames() const override { return m_queue.TotalDropped(); }
    double   AvgLatencyMs() const override;
    uint64_t BitstreamBytes() const override;

    // 编码后数据包 → Muxer（与 NvencSimulator 平行）
    void SetPacketSink(PacketSink sink);

    // H264 序列参数（SPS/PPS，Annex-B）→ MuxConfig.extradata
    const std::vector<uint8_t>& SequenceParams() const { return m_extradata; }
    std::string LastError() const { return m_lastError; }

private:
    struct InputBuffer {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> yUav, uvUav;
        NV_ENC_INPUT_PTR registered = nullptr;
    };

    bool LoadApi();
    bool CreateSession();
    bool CreateConfig();                      // H264 1080p60 CBR 12Mbps GOP 120
    bool CreateConvertPipeline();             // CS(BT.709) + NV12 池 + RegisterResource
    bool GetSequenceParams();                 // nvEncGetSequenceParams → extradata
    bool ConvertToNv12(ID3D11Texture2D* src, InputBuffer& dst);
    void EncodeOneFrame(const CaptureFrame& f, InputBuffer& buf);   // nvEncEncodePicture
    void DrainBitstream();                    // nvEncLockBitstream → sink/文件
    void EncoderThread();
    void FlushAndClose();

    HMODULE m_nvModule = nullptr;
    NV_ENCODE_API_FUNCTION_LIST m_api{};
    void* m_session = nullptr;
    NV_ENC_INPUT_PTR m_bitstreamBuffer = nullptr;
    const uint32_t m_bitstreamSize = 2 * 1024 * 1024;

    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_ctx;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_cs;
    std::unordered_map<ID3D11Texture2D*,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_srvCache;

    std::vector<InputBuffer> m_pool;
    uint32_t m_poolIdx = 0;

    uint32_t m_width = 1920, m_height = 1080, m_fps = 60;
    uint32_t m_bitrateKbps = 12000;
    FILE* m_out = nullptr;
    std::vector<uint8_t> m_extradata;
    std::vector<uint8_t> m_pktBuf;
    PacketSink m_sink;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    FrameQueue m_queue{16};

    std::deque<std::pair<uint64_t, int64_t>> m_pending;
    std::atomic<uint64_t> m_submitted{0}, m_encoded{0}, m_failed{0}, m_bytes{0};
    std::atomic<double> m_latencyMs{0.0};
    int64_t m_qpcFreq = 0;
    uint64_t m_frameIdx = 0;
    std::string m_lastError;
};

} // namespace sf
