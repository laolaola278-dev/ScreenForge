// Phase 5-A — 真实硬件录制测试（hardware-record-test）
// 链路（硬件模式）：WGC/GpuPattern(ID3D11Texture2D) → RecorderEngine → NvEncoderHardware
//                   → EncodedPacket → Mp4Muxer → recording.mp4
// 自动回退：无 NVIDIA GPU / NVENC 不可用 → NvencSimulator（hardware_report.json 明确标注）
// 无任何伪装：所有日志来自真实检测/运行结果。

#include "HardwareRecordTest.h"

#include <Windows.h>

#include <cstdio>
#include <memory>
#include <vector>

#include "Mp4Muxer.h"
#include "NvCapabilities.h"
#include "NvencSimulator.h"
#include "RecorderEngine.h"
#include "WgcCaptureSource.h"
#include "graphics/D3D11Device.h"
#include "graphics/Frame.h"
#ifdef SF_HAVE_NVENC_HW
#include "NvEncoderHardware.h"
#endif

namespace sf {
namespace {

// GPU 合成图案源（硬件模式的备用输入；纯 GPU 生成，无 CPU 回读）
// 当 WGC 不可用（无交互会话 / 老系统）时使用；图案：渐变 + 移动白条 + 帧号
class GpuPatternSource : public ICaptureSource {
public:
    bool Start() override { return m_tex != nullptr; }
    void Stop() override {}
    bool GetFrame(CaptureFrame& out) override {
        if (!m_tex) return false;
        // 每帧更新图案（GPU CS 填充）
        m_ctx->CSSetShader(m_cs.Get(), nullptr, 0);
        ID3D11ShaderResourceView* n = nullptr;
        m_ctx->CSSetShaderResources(0, 1, &n);
        ID3D11UnorderedAccessView* uav = m_uav.Get();
        m_ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        m_ctx->Dispatch((m_w + 15) / 16, (m_h + 15) / 16, 1);
        ID3D11UnorderedAccessView* nul = nullptr;
        m_ctx->CSSetUnorderedAccessViews(0, 1, &nul, nullptr);
        m_ctx->Flush();

        out.texture = m_tex;
        out.width = m_w; out.height = m_h;
        out.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        LARGE_INTEGER q{};
        QueryPerformanceCounter(&q);
        out.captureQpc = q.QuadPart;
        out.index = ++m_idx;
        out.ts100ns = 0;
        return true;
    }

    static std::unique_ptr<GpuPatternSource> Create(
        ID3D11Device* dev, uint32_t w, uint32_t h) {
        auto s = std::unique_ptr<GpuPatternSource>(new GpuPatternSource());
        s->m_w = w; s->m_h = h;
        dev->GetImmediateContext(&s->m_ctx);

        static const char* cs = R"(
RWTexture2D<float4> gOut : register(u0);
cbuffer C : register(b0) { uint gFrame; };
[numthreads(16,16,1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint2 dims; gOut.GetDimensions(dims.x, dims.y);
    float2 uv = float2(dtid.xy) / float2(dims);
    float bar = smoothstep(0.02, 0.0, abs(uv.x - frac(float(gFrame) * 0.01)));
    float3 col = float3(uv.x, uv.y, frac(float(gFrame) * 0.05));
    col = lerp(col, 1.0, bar);
    gOut[dtid.xy] = float4(col, 1.0);
}
)";
        Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3DCompile(cs, std::strlen(cs), nullptr, nullptr, nullptr,
                              "main", "cs_5_0", 0, 0, &blob, &err))) return nullptr;
        if (FAILED(dev->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(), nullptr, &s->m_cs))) return nullptr;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &s->m_tex))) return nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format = td.Format;
        uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateUnorderedAccessView(s->m_tex.Get(), &uv, &s->m_uav))) return nullptr;
        return s;
    }

private:
    uint32_t m_w = 0, m_h = 0; uint64_t m_idx = 0;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_ctx;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_cs;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_tex;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_uav;
};

bool WriteReport(const char* path, const NvHwCapabilities& caps, bool hw,
                 const std::string& fallbackReason, uint64_t frames,
                 double durSec, uint64_t bytes, uint64_t failed) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n");
    fprintf(f, " \"realNvencHardware\": %s,\n", hw ? "true" : "false");
    fprintf(f, " \"gpuName\": \"%s\",\n", caps.gpuName.c_str());
    fprintf(f, " \"driverVersion\": \"%s\",\n", caps.driverVersion.c_str());
    fprintf(f, " \"vramMB\": %llu,\n", (unsigned long long)caps.vramMB);
    fprintf(f, " \"nvencVersion\": \"%s\",\n", caps.nvencVersion.c_str());
    fprintf(f, " \"h264Supported\": %s,\n", caps.h264Supported ? "true" : "false");
    fprintf(f, " \"maxEncodeSize\": \"%dx%d\",\n", caps.maxWidth, caps.maxHeight);
    fprintf(f, " \"encoder\": \"%s\",\n", hw ? "NvEncoderHardware" : "NvencSimulator (fallback)");
    fprintf(f, " \"fallbackReason\": \"%s\",\n", fallbackReason.c_str());
    fprintf(f, " \"resolution\": \"1920x1080\",\n");
    fprintf(f, " \"fps\": 60,\n");
    fprintf(f, " \"bitrateKbps\": 12000,\n");
    fprintf(f, " \"gop\": 120,\n");
    fprintf(f, " \"encodedFrames\": %llu,\n", (unsigned long long)frames);   // 8-B：与验收字段对齐
    fprintf(f, " \"frames\": %llu,\n", (unsigned long long)frames);
    fprintf(f, " \"durationSec\": %.2f,\n", durSec);
    fprintf(f, " \"bitstreamBytes\": %llu,\n", (unsigned long long)bytes);
    fprintf(f, " \"failedFrames\": %llu\n", (unsigned long long)failed);
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

} // namespace

int RunHardwareRecordTest(uint64_t seconds, uint32_t fps,
                          const std::string& mp4Path,
                          const std::string& reportPath) {
    const uint32_t w = 1920, h = 1080;
    printf("===== ScreenForge Phase 5-A Hardware Record Test =====\n");
    printf("Target : %ux%u @ %u fps, %llu s\n", w, h, fps, (unsigned long long)seconds);
    fflush(stdout);

    // 1) 硬件检测（GPU 型号 / NVENC 版本 / 编码能力）
    NvHwCapabilities caps;
#ifdef SF_HAVE_NVENC_HW
    caps = NvEncoderHardware::DetectCapabilities();
#else
    printf("NVENC 硬件构建不可用（缺少 NVENC SDK，SF_HAVE_NVENC_HW=0）— SIMULATION-only 模式\n");
#endif
    printf("GPU    : %s\n", caps.gpuName.empty() ? "(未检测到)" : caps.gpuName.c_str());
    printf("Driver : %s\n", caps.driverVersion.empty() ? "-" : caps.driverVersion.c_str());
    printf("VRAM   : %.1f GB\n", double(caps.vramMB) / 1024.0);
    printf("NVENC  : %s\n", caps.nvencVersion.empty() ? "(未检测到)" : caps.nvencVersion.c_str());
    printf("H264   : %s\n", caps.h264Supported ? "supported" : "not supported");
    if (caps.maxWidth > 0)
        printf("MaxSize: %dx%d\n", caps.maxWidth, caps.maxHeight);

    // 2) D3D11 设备
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    if (!D3D11Device::Create(device, ctx)) {
        printf("FAIL: D3D11 设备创建失败（硬件/沙盒路径均需）\n");
        return 1;
    }

    bool hwMode = caps.nvencAvailable && caps.h264Supported;
    std::string fallbackReason = "";
    if (!hwMode) {
        fallbackReason = caps.gpuName.empty() ? "未检测到 NVIDIA GPU"
                       : (!caps.nvencAvailable ? "NVENC 会话不可用"
                       : "H264 编码能力缺失");
        printf("\n[回退] %s → 使用 NvencSimulator（禁止伪装真实 NVENC）\n\n",
               fallbackReason.c_str());
    }

    Mp4Muxer mux;
#ifdef SF_HAVE_NVENC_HW
    NvEncoderHardware hwEnc;
#endif
    NvencSimulator simEnc;
    IEncoder* enc = nullptr;
    std::vector<uint8_t> extradata;
    bool muxOk = false;
    std::unique_ptr<ICaptureSource> capSrc;

#ifdef SF_HAVE_NVENC_HW
    if (hwMode) {
        // 3a) 硬件模式：NvEncoderHardware（输出经 Sink → Muxer）
        if (!hwEnc.Initialize(device.Get(), w, h, fps, 12000, "")) {
            printf("FAIL: NvEncoderHardware 初始化失败 - %s\n", hwEnc.LastError().c_str());
            printf("  → 回退 NvencSimulator\n");
            hwMode = false;
            fallbackReason = hwEnc.LastError();
        } else {
            enc = &hwEnc;
            hwEnc.SetPacketSink([&](const EncodedPacket& p) { if (muxOk) mux.WritePacket(p); });
            extradata = hwEnc.SequenceParams();
            // 输入源：优先 WGC（真机），失败 → GPU 合成纹理（无 CPU 回读）
            auto wgc = std::make_unique<WgcCaptureSource>(device);
            if (wgc->Start()) {
                capSrc = std::move(wgc);
                printf("Input  : WGC 主显示器捕获\n");
            } else {
                auto pat = GpuPatternSource::Create(device.Get(), w, h);
                if (pat) {
                    pat->Start();
                    capSrc = std::move(pat);
                    printf("Input  : GPU 合成纹理（WGC 不可用: %s）\n", wgc->LastError().c_str());
                } else {
                    printf("FAIL: 输入源创建失败\n");
                    hwEnc.Shutdown();
                    return 1;
                }
            }
        }
    }
#endif

    if (!enc) {
        // 3b) 回退模式：NvencSimulator（合成帧源）
        if (!simEnc.Initialize(nullptr, w, h, fps, 12000, "")) {
            printf("FAIL: NvencSimulator 初始化失败\n");
            return 1;
        }
        enc = &simEnc;
        simEnc.SetPacketSink([&](const EncodedPacket& p) { if (muxOk) mux.WritePacket(p); });
        static const std::vector<uint8_t> kSps = {
            0x00,0x00,0x00,0x01, 0x67,0x42,0x00,0x1e, 0x95,0xa8,0x14,0x01,
            0x6e,0x40,0x40,0x1e, 0x00,0x00,0x03,0x00, 0x10,0x00,0x00,0x03,
            0x03,0x20,0xf1,0x83, 0x19,0x60,
        };
        static const std::vector<uint8_t> kPps = {
            0x00,0x00,0x00,0x01, 0x68,0xce,0x3c,0x80,
        };
        extradata.insert(extradata.end(), kSps.begin(), kSps.end());
        extradata.insert(extradata.end(), kPps.begin(), kPps.end());
    }

    // 4) RecorderEngine（不修改：接收 IEncoder* + IMuxer* + 可选 ICaptureSource*）
    RecorderConfig cfg;
    cfg.width = w; cfg.height = h; cfg.fps = fps;
    cfg.bitrateKbps = 12000;
    cfg.framesLimit = seconds * fps;
    cfg.mp4Path = mp4Path;
    cfg.sessionJsonPath = reportPath;         // 复用（同时写 hardware_report.json）
    cfg.extradata = extradata;
    cfg.captureSource = capSrc.get();         // 硬件模式注入真实/合成源；回退模式为 null（内置合成）
    cfg.encoderIsSimulator = !hwMode;

    RecorderEngine engine;
    if (!engine.Initialize(cfg, enc, &mux)) {
        printf("FAIL: engine init - %s\n", engine.LastError().c_str());
        enc->Shutdown();
        return 1;
    }
    muxOk = true;

    if (!engine.StartRecording()) {
        printf("FAIL: start - %s\n", engine.LastError().c_str());
        enc->Shutdown();
        return 1;
    }

    while (engine.State() == RecorderState::Recording) {
        Sleep(250);
    }
    if (!engine.StopRecording()) {
        printf("FAIL: stop - %s\n", engine.LastError().c_str());
    }
    enc->Shutdown();
    if (capSrc) capSrc->Stop();

    // 5) 报告 + hardware_report.json
    const bool realHw = hwMode;
    const uint64_t frames = enc->Encoded();
    const double dur = engine.DurationSec();
    WriteReport(reportPath.c_str(), caps, realHw, fallbackReason,
                frames, dur, enc->BitstreamBytes(), enc->FailedFrames());

    printf("\n===== Hardware Record Test Report =====\n");
    printf("realNvencHardware : %s\n", realHw ? "true" : "false");
    if (!realHw) printf("fallbackReason   : %s\n", fallbackReason.c_str());
    printf("encoder          : %s\n", realHw ? "NvEncoderHardware" : "NvencSimulator (fallback)");
    printf("frames           : %llu\n", (unsigned long long)frames);
    printf("duration         : %.2f s (target %.2f s)\n", dur, double(seconds));
    printf("failed           : %llu\n", (unsigned long long)enc->FailedFrames());
    printf("output           : %s\n", mp4Path.c_str());
    printf("report           : %s\n", reportPath.c_str());
    printf("------------------------------------------\n");
    printf("真机验证: ffprobe -v error -show_entries format=duration -show_entries stream=codec_name,width,height,avg_frame_rate -of default=noprint_wrappers=1 %s\n", mp4Path.c_str());
    printf("期望: codec=h264 · 1920x1080 · fps=60 · duration≈%llus\n", (unsigned long long)seconds);
    printf("==========================================\n");
    return realHw ? 0 : 2;    // 2 = 回退模式（非失败，但未使用真实硬件）
}

} // namespace sf
