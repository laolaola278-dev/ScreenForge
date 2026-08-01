// ScreenForge Phase 5-A — 真实 NVENC 硬件编码器实现
// LoadLibrary nvEncodeAPI64.dll → NvEncodeAPICreateInstance → nvEncOpenEncodeSessionEx(D3D11)
// → nvEncInitializeEncoder(H264 1080p60 CBR 12Mbps GOP120) → NV12 纹理 nvEncRegisterResource
// → nvEncEncodePicture（PTS=QPC→100ns）→ nvEncLockBitstream → EncodedPacket → Muxer
// 全程 GPU：输入 ID3D11Texture2D 直通，禁止 CPU Map / Staging / 像素回读

#include "NvEncoderHardware.h"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "graphics/D3D11Device.h"

namespace sf {

namespace {

constexpr UINT kPoolSize = 3;   // 轮换 NV12 输入缓冲（异步编码防阻塞）

// BGRA → NV12（BT.709 有限范围；D3D11 B8G8R8A8 在 HLSL 读作 r=B, g=G, b=R）
const char* kCsSource = R"(
Texture2D<float4> gInput : register(t0);
RWTexture2D<float>  gY  : register(u0);
RWTexture2D<float2> gUV : register(u1);

[numthreads(16,16,1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint2 px = dtid.xy;
    uint2 dims; gInput.GetDimensions(dims.x, dims.y);
    uint2 px1 = min(px + uint2(1,0), dims - 1);
    uint2 px2 = min(px + uint2(0,1), dims - 1);
    uint2 px3 = min(px + uint2(1,1), dims - 1);

    float4 c0 = gInput[px];
    float r = c0.b, g = c0.g, b = c0.r;

    float yp = 0.2126*r + 0.7152*g + 0.0722*b;
    gY[px] = saturate((16.0 + 219.0*yp) / 255.0);

    if ((px.x & 1) == 0 && (px.y & 1) == 0) {
        float4 c1 = gInput[px1], c2 = gInput[px2], c3 = gInput[px3];
        float r4 = (r + c1.b + c2.b + c3.b) * 0.25;
        float g4 = (g + c1.g + c2.g + c3.g) * 0.25;
        float b4 = (b + c1.r + c2.r + c3.r) * 0.25;
        float yp4 = 0.2126*r4 + 0.7152*g4 + 0.0722*b4;
        float u = 128.0 + 224.0 * ((b4 - yp4) / 1.8556);
        float v = 128.0 + 224.0 * ((r4 - yp4) / 1.5748);
        gUV[px >> 1] = float2(u, v) / 255.0;
    }
}
)";

int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

} // namespace

NvEncoderHardware::~NvEncoderHardware() { Shutdown(); }

NvHwCapabilities NvEncoderHardware::DetectCapabilities() {
    NvHwCapabilities caps;
    const GpuInfo g = D3D11Device::Detect();
    caps.gpuName       = g.name;
    caps.driverVersion = g.driver;
    caps.vramMB        = g.vramMB;

    if (!g.nvidia) {
        return caps;                        // 非 NVIDIA：NVENC 不可用
    }

    HMODULE h = LoadLibraryA("nvEncodeAPI64.dll");
    if (!h) return caps;
    auto create = reinterpret_cast<NVENCSTATUS(WINAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(
        GetProcAddress(h, "NvEncodeAPICreateInstance"));
    if (!create) { FreeLibrary(h); return caps; }

    NV_ENCODE_API_FUNCTION_LIST api{};
    api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (create(&api) != NV_ENC_SUCCESS) { FreeLibrary(h); return caps; }

    // NVENC 版本
    if (api.nvEncGetMaxSupportedVersion) {
        unsigned int ver = 0;
        if (api.nvEncGetMaxSupportedVersion(&ver) == NV_ENC_SUCCESS) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u.%u", ver / 1000, (ver % 1000) / 10);
            caps.nvencVersion = buf;
        }
    }

    // 编码能力查询（需要 session）
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    if (D3D11Device::Create(device, ctx)) {
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sp{};
        sp.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        sp.device     = device.Get();
        sp.deviceType = NV_ENC_DEVICE_TYPE_D3D11;
        void* session = nullptr;
        if (api.nvEncOpenEncodeSessionEx(&sp, &session) == NV_ENC_SUCCESS) {
            caps.nvencAvailable = true;
            if (api.nvEncGetEncodeCapability) {
                int val = 0;
                if (api.nvEncGetEncodeCapability(session, NV_ENC_H264,
                        NV_ENC_CAPABILITY_SUPPORTED_RATECONTROL_MODES, &val) == NV_ENC_SUCCESS) {
                    caps.h264Supported = (val & NV_ENC_PARAMS_RC_CBR) != 0;
                }
                if (api.nvEncGetEncodeCapability(session, NV_ENC_H264,
                        NV_ENC_CAPABILITY_WIDTH_MAX, &val) == NV_ENC_SUCCESS) {
                    caps.maxWidth = val;
                }
                if (api.nvEncGetEncodeCapability(session, NV_ENC_H264,
                        NV_ENC_CAPABILITY_HEIGHT_MAX, &val) == NV_ENC_SUCCESS) {
                    caps.maxHeight = val;
                }
            }
            api.nvEncDestroyEncoder(session);
        }
    }
    FreeLibrary(h);
    return caps;
}

bool NvEncoderHardware::LoadApi() {
    m_nvModule = LoadLibraryA("nvEncodeAPI64.dll");
    if (!m_nvModule) {
        m_lastError = "nvEncodeAPI64.dll 未找到（需要 NVIDIA 驱动）";
        return false;
    }
    auto create = reinterpret_cast<NVENCSTATUS(WINAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(
        GetProcAddress(m_nvModule, "NvEncodeAPICreateInstance"));
    if (!create) {
        m_lastError = "nvEncodeAPI64.dll 未导出 NvEncodeAPICreateInstance";
        return false;
    }
    std::memset(&m_api, 0, sizeof(m_api));
    m_api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (create(&m_api) != NV_ENC_SUCCESS) {
        m_lastError = "NvEncodeAPICreateInstance 失败";
        return false;
    }
    return true;
}

bool NvEncoderHardware::CreateSession() {
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params{};
    params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.device     = m_device.Get();
    params.deviceType = NV_ENC_DEVICE_TYPE_D3D11;
    if (m_api.nvEncOpenEncodeSessionEx(&params, &m_session) != NV_ENC_SUCCESS) {
        m_lastError = "nvEncOpenEncodeSessionEx 失败（GPU 不支持 NVENC）";
        return false;
    }
    return true;
}

bool NvEncoderHardware::CreateConfig() {
    NV_ENC_CONFIG cfg{};
    cfg.version = NV_ENC_CONFIG_VER;
    cfg.profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
    cfg.gopLength    = 120;                   // GOP 120（2 秒 @60fps）
    cfg.frameIntervalP = 1;

    cfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    cfg.rcParams.averageBitRate  = 12000 * 1000;   // CBR 12 Mbps
    cfg.rcParams.maxBitRate      = 12000 * 1000;
    cfg.rcParams.vbvBufferSize   = 12000 * 1000 * 2 / 60;

    cfg.encodeCodecConfig.h264Config.idrPeriod = 120;

    NV_ENC_INITIALIZE_PARAMS init{};
    init.version        = NV_ENC_INITIALIZE_PARAMS_VER;
    init.encodeGUID     = NV_ENC_H264;
    init.presetGUID     = NV_ENC_PRESET_P5_GUID;
    init.encodeWidth    = m_width;
    init.encodeHeight   = m_height;
    init.darWidth       = m_width;
    init.darHeight      = m_height;
    init.frameRateNum   = m_fps;
    init.frameRateDen   = 1;
    init.encodeConfig   = &cfg;

    if (m_api.nvEncInitializeEncoder(m_session, &init) != NV_ENC_SUCCESS) {
        m_lastError = "nvEncInitializeEncoder 失败";
        return false;
    }

    NV_ENC_CREATE_BITSTREAM_BUFFER bs{};
    bs.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    bs.size    = m_bitstreamSize;
    if (m_api.nvEncCreateBitstreamBuffer(m_session, &bs) != NV_ENC_SUCCESS) {
        m_lastError = "nvEncCreateBitstreamBuffer 失败";
        return false;
    }
    m_bitstreamBuffer = bs.bitstreamBuffer;
    return true;
}

bool NvEncoderHardware::CreateConvertPipeline() {
    Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(kCsSource, std::strlen(kCsSource), nullptr, nullptr, nullptr,
                            "main", "cs_5_0", 0, 0, &blob, &err);
    if (FAILED(hr)) { m_lastError = "D3DCompile 失败（BGRA→NV12 shader）"; return false; }
    hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                       blob->GetBufferSize(), nullptr, &m_cs);
    if (FAILED(hr)) { m_lastError = "CreateComputeShader 失败"; return false; }

    m_pool.resize(kPoolSize);
    for (UINT i = 0; i < kPoolSize; ++i) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = m_width; td.Height = m_height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_NV12; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_device->CreateTexture2D(&td, nullptr, &m_pool[i].nv12))) {
            m_lastError = "CreateTexture2D(NV12) 失败"; return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uv.Texture2DArray.ArraySize = 1;
        uv.Format = DXGI_FORMAT_R8_UNORM;                       // Y slice0
        uv.Texture2DArray.FirstArraySlice = 0;
        if (FAILED(m_device->CreateUnorderedAccessView(m_pool[i].nv12.Get(), &uv, &m_pool[i].yUav))) {
            m_lastError = "CreateUAV(Y) 失败"; return false;
        }
        uv.Format = DXGI_FORMAT_R8G8_UNORM;                     // UV slice1
        uv.Texture2DArray.FirstArraySlice = 1;
        if (FAILED(m_device->CreateUnorderedAccessView(m_pool[i].nv12.Get(), &uv, &m_pool[i].uvUav))) {
            m_lastError = "CreateUAV(UV) 失败"; return false;
        }

        NV_ENC_REGISTER_RESOURCE reg{};
        reg.version      = NV_ENC_REGISTER_RESOURCE_VER;
        reg.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        reg.resource     = m_pool[i].nv12.Get();
        reg.width        = m_width;
        reg.height       = m_height;
        reg.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
        reg.bufferUsage  = NV_ENC_INPUT_IMAGE;
        if (m_api.nvEncRegisterResource(m_session, &reg) != NV_ENC_SUCCESS) {
            m_lastError = "nvEncRegisterResource 失败"; return false;
        }
        m_pool[i].registered = reg.registeredResource;
    }
    return true;
}

bool NvEncoderHardware::GetSequenceParams() {
    NV_ENC_SEQUENCE_PARAM_PAYLOAD payload{};
    payload.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    payload.inBufferSize  = 0;
    payload.spsppsBuffer[0] = nullptr;
    if (m_api.nvEncGetSequenceParams(m_session, &payload) != NV_ENC_SUCCESS) {
        m_lastError = "nvEncGetSequenceParams 失败"; return false;
    }
    m_extradata.assign(payload.spsppsBuffer[0],
                       payload.spsppsBuffer[0] + payload.spsppsBufferSize[0]);
    return true;
}

bool NvEncoderHardware::ConvertToNv12(ID3D11Texture2D* src, InputBuffer& dst) {
    D3D11_TEXTURE2D_DESC sd{};
    src->GetDesc(&sd);
    if (sd.Width != m_width || sd.Height != m_height) return false;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    const auto it = m_srvCache.find(src);
    if (it == m_srvCache.end()) {
        D3D11_SHADER_RESOURCE_VIEW_DESC rd{};
        rd.Format = sd.Format;
        rd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        rd.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateShaderResourceView(src, &rd, &srv))) return false;
        m_srvCache.emplace(src, srv);
    } else {
        srv = it->second;
    }

    // GPU 色彩转换：BGRA 纹理 → NV12 纹理（零 CPU 参与，无回读）
    m_ctx->CSSetShader(m_cs.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = { srv.Get() };
    m_ctx->CSSetShaderResources(0, 1, srvs);
    ID3D11UnorderedAccessView* uavs[] = { dst.yUav.Get(), dst.uvUav.Get() };
    m_ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    m_ctx->Dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);

    ID3D11UnorderedAccessView* nullUavs[] = { nullptr, nullptr };
    m_ctx->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
    ID3D11ShaderResourceView* nullSrvs[] = { nullptr };
    m_ctx->CSSetShaderResources(0, 1, nullSrvs);
    m_ctx->Flush();
    return true;
}

void NvEncoderHardware::EncodeOneFrame(const CaptureFrame& f, InputBuffer& buf) {
    int64_t pts = 0;
    if (f.captureQpc > 0 && m_qpcFreq > 0) {
        pts = (f.captureQpc * 10000000LL) / m_qpcFreq;   // QPC → 100ns
    } else {
        pts = m_frameIdx * (10000000LL / m_fps);
    }

    NV_ENC_PIC_PARAMS pic{};
    pic.version         = NV_ENC_PIC_PARAMS_VER;
    pic.inputBuffer     = buf.registered;
    pic.bufferFmt       = NV_ENC_BUFFER_FORMAT_NV12;
    pic.inputWidth      = m_width;
    pic.inputHeight     = m_height;
    pic.inputTimeStamp  = pts;
    pic.outputBitstream = m_bitstreamBuffer;
    pic.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;

    NVENCSTATUS rc = NV_ENC_ERR_INVALID_PARAM;
    if (m_api.nvEncEncodePicture) {              // 新 SDK 保留的旧入口
        rc = m_api.nvEncEncodePicture(m_session, &pic);
    } else if (m_api.nvEncEncodeFrame) {         // SDK 11+ 推荐入口（同参数结构）
        rc = m_api.nvEncEncodeFrame(m_session, &pic);
    }
    if (rc == NV_ENC_SUCCESS) {
        m_pending.emplace_back(m_frameIdx, QpcNow());
        m_frameIdx++;
        m_submitted.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_failed.fetch_add(1, std::memory_order_relaxed);
    }
}

void NvEncoderHardware::DrainBitstream() {
    NV_ENC_LOCK_BITSTREAM lock{};
    lock.version      = NV_ENC_LOCK_BITSTREAM_VER;
    lock.outputBuffer = m_bitstreamBuffer;
    lock.doNotWait    = 0;

    const NVENCSTATUS st = m_api.nvEncLockBitstream(m_session, &lock);
    if (st == NV_ENC_SUCCESS && lock.bitstreamSizeInBytes > 0) {
        // 构造 EncodedPacket → Muxer（或写裸文件）
        m_pktBuf.assign(static_cast<const uint8_t*>(lock.bitstreamBufferPtr),
                        static_cast<const uint8_t*>(lock.bitstreamBufferPtr) +
                        lock.bitstreamSizeInBytes);
        if (m_out) {
            fwrite(m_pktBuf.data(), 1, m_pktBuf.size(), m_out);
        }
        m_bytes.fetch_add(m_pktBuf.size(), std::memory_order_relaxed);

        EncodedPacket ep;
        ep.data = m_pktBuf.data();
        ep.size = m_pktBuf.size();
        ep.pts  = lock.outputTimeStamp;
        ep.dts  = lock.outputTimeStamp;
        ep.keyFrame = (lock.pictureType == NV_ENC_PIC_TYPE_IDR ||
                       lock.pictureType == NV_ENC_PIC_TYPE_I);
        if (m_sink) m_sink(ep);

        // 延迟统计
        if (!m_pending.empty() && m_qpcFreq > 0) {
            const int64_t submitQpc = m_pending.front().second;
            m_pending.pop_front();
            const double ms = double(QpcNow() - submitQpc) * 1000.0 / double(m_qpcFreq);
            const double cur = m_latencyMs.load();
            m_latencyMs.store(cur == 0.0 ? ms : cur + (ms - cur) * 0.1);
        }
        m_encoded.fetch_add(1, std::memory_order_relaxed);
        m_api.nvEncUnlockBitstream(m_session, lock.outputBuffer);
    }
}

void NvEncoderHardware::EncoderThread() {
    while (m_running.load(std::memory_order_relaxed)) {
        CaptureFrame f;
        if (m_queue.Pop(f)) {
            InputBuffer& buf = m_pool[m_poolIdx % kPoolSize];
            m_poolIdx++;
            if (ConvertToNv12(f.texture.Get(), buf)) {
                EncodeOneFrame(f, buf);
            } else {
                m_failed.fetch_add(1, std::memory_order_relaxed);
            }
            f.texture.Reset();   // 释放输入纹理引用（所有权：Capture→Queue→Encoder→释放）
        }
        DrainBitstream();
        Sleep(1);
    }
    FlushAndClose();
}

void NvEncoderHardware::FlushAndClose() {
    if (m_session && m_api.nvEncFlushEncoder) {
        m_api.nvEncFlushEncoder(m_session);
        for (;;) {
            NV_ENC_LOCK_BITSTREAM lock{};
            lock.version      = NV_ENC_LOCK_BITSTREAM_VER;
            lock.outputBuffer = m_bitstreamBuffer;
            lock.doNotWait    = 1;
            const NVENCSTATUS st = m_api.nvEncLockBitstream(m_session, &lock);
            if (st == NV_ENC_ERR_END_OF_STREAM) break;
            if (st == NV_ENC_SUCCESS && lock.bitstreamSizeInBytes > 0) {
                m_pktBuf.assign(static_cast<const uint8_t*>(lock.bitstreamBufferPtr),
                                static_cast<const uint8_t*>(lock.bitstreamBufferPtr) +
                                lock.bitstreamSizeInBytes);
                if (m_out) fwrite(m_pktBuf.data(), 1, m_pktBuf.size(), m_out);
                m_bytes.fetch_add(m_pktBuf.size(), std::memory_order_relaxed);
                EncodedPacket ep;
                ep.data = m_pktBuf.data();
                ep.size = m_pktBuf.size();
                ep.pts = ep.dts = lock.outputTimeStamp;
                ep.keyFrame = (lock.pictureType == NV_ENC_PIC_TYPE_IDR ||
                               lock.pictureType == NV_ENC_PIC_TYPE_I);
                if (m_sink) m_sink(ep);
                if (!m_pending.empty()) m_pending.pop_front();
                m_encoded.fetch_add(1, std::memory_order_relaxed);
                m_api.nvEncUnlockBitstream(m_session, lock.outputBuffer);
            } else {
                break;
            }
        }
    }
    if (m_out) { fflush(m_out); fclose(m_out); m_out = nullptr; }
    m_running.store(false);
}

bool NvEncoderHardware::Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
                                   uint32_t fps, uint32_t bitrateKbps,
                                   const std::string& outputPath) {
    if (m_running.load()) return false;
    if (!device) { m_lastError = "D3D11 设备为空（硬件编码必需）"; return false; }
    m_device = device;
    m_device->GetImmediateContext(&m_ctx);
    m_width = width; m_height = height; m_fps = fps; m_bitrateKbps = bitrateKbps;
    m_lastError.clear();

    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    m_qpcFreq = f.QuadPart;

    if (!LoadApi()) return false;
    if (!CreateSession()) return false;
    if (!CreateConfig()) return false;
    if (!CreateConvertPipeline()) return false;
    if (!GetSequenceParams()) return false;

    if (!outputPath.empty()) {
        m_out = fopen(outputPath.c_str(), "wb");
        if (!m_out) { m_lastError = "无法创建输出文件"; return false; }
    }

    m_queue.Reset();
    m_running.store(true);
    m_thread = std::thread(&NvEncoderHardware::EncoderThread, this);
    return true;
}

void NvEncoderHardware::Shutdown() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();

    if (m_session) {
        for (auto& b : m_pool) {
            if (b.registered && m_api.nvEncUnregisterResource) {
                m_api.nvEncUnregisterResource(m_session, b.registered);
                b.registered = nullptr;
            }
        }
        if (m_bitstreamBuffer && m_api.nvEncDestroyBitstreamBuffer) {
            m_api.nvEncDestroyBitstreamBuffer(m_session, m_bitstreamBuffer);
            m_bitstreamBuffer = nullptr;
        }
        m_api.nvEncDestroyEncoder(m_session);
        m_session = nullptr;
    }
    m_pool.clear();
    m_srvCache.clear();
    m_cs.Reset();
    m_ctx.Reset();
    m_device.Reset();
    if (m_nvModule) { FreeLibrary(m_nvModule); m_nvModule = nullptr; }
    m_pending.clear();
}

void NvEncoderHardware::PushFrame(CaptureFrame&& f) {
    if (m_running.load(std::memory_order_relaxed)) {
        m_queue.Push(std::move(f));
    }
}

void NvEncoderHardware::SetPacketSink(PacketSink sink) { m_sink = std::move(sink); }

bool NvEncoderHardware::IsRunning() const { return m_running.load(); }
uint64_t NvEncoderHardware::Submitted() const { return m_submitted.load(std::memory_order_relaxed); }
uint64_t NvEncoderHardware::Encoded() const { return m_encoded.load(std::memory_order_relaxed); }
uint64_t NvEncoderHardware::FailedFrames() const { return m_failed.load(std::memory_order_relaxed); }
double NvEncoderHardware::AvgLatencyMs() const { return m_latencyMs.load(); }
uint64_t NvEncoderHardware::BitstreamBytes() const { return m_bytes.load(std::memory_order_relaxed); }

} // namespace sf
