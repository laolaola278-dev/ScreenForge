// ScreenForge Phase 3-A/B — NVENC 硬件编码器实现
// 输入 ID3D11Texture2D（BGRA）→ GPU compute shader 转 NV12（BT.709 有限范围）
// → nvEncEncodeFrame（DirectX 输入，PTS 取自 QPC 时间戳）
// 全程 GPU，禁止纹理回读 CPU。

#include "NvEncoder.h"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace sf {

namespace {

constexpr UINT kPoolSize = 3;   // 轮换输入缓冲数（NVENC 异步，多缓冲防阻塞）

// BGRA → NV12 色彩转换（BT.709 有限范围，HDTV 标准）
// D3D11 的 B8G8R8A8 纹理在 HLSL 中读取为 r=Blue, g=Green, b=Red
// Y  = 16 + 219*(0.2126R + 0.7152G + 0.0722B)
// Cb = 128 + 224*(B - Y')/1.8556
// Cr = 128 + 224*(R - Y')/1.5748
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
    float r = c0.b, g = c0.g, b = c0.r;   // BGRA 内存序

    // Y 平面（BT.709 有限范围）
    float yp = 0.2126*r + 0.7152*g + 0.0722*b;
    float y  = 16.0 + 219.0 * yp;
    gY[px] = saturate(y / 255.0);

    // UV 平面（2x2 平均，BT.709）
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

} // namespace

NvEncoder::~NvEncoder() { Shutdown(); }

bool NvEncoder::LoadApi() {
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

bool NvEncoder::CreateSession() {
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params{};
    params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.device     = m_device.Get();               // D3D11 设备
    params.deviceType = NV_ENC_DEVICE_TYPE_D3D11;
    if (m_api.nvEncOpenEncodeSessionEx(&params, &m_session) != NV_ENC_SUCCESS) {
        m_lastError = "nvEncOpenEncodeSessionEx 失败（GPU 可能不支持 NVENC）";
        return false;
    }
    return true;
}

bool NvEncoder::CreateConfig() {
    NV_ENC_CONFIG cfg{};
    cfg.version = NV_ENC_CONFIG_VER;
    cfg.profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
    cfg.gopLength    = m_fps * 2;                    // 2 秒 GOP
    cfg.frameIntervalP = 1;                          // 全 P 帧间隔 1

    // CBR 码控：稳定码率（录屏场景）
    cfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    cfg.rcParams.averageBitRate  = m_bitrateKbps * 1000;
    cfg.rcParams.maxBitRate      = m_bitrateKbps * 1000;
    cfg.rcParams.vbvBufferSize   = m_bitrateKbps * 1000 * 2 / m_fps;

    cfg.encodeCodecConfig.h264Config.idrPeriod = m_fps * 2;

    NV_ENC_INITIALIZE_PARAMS init{};
    init.version        = NV_ENC_INITIALIZE_PARAMS_VER;
    init.encodeGUID     = NV_ENC_H264;
    init.presetGUID     = NV_ENC_PRESET_P5_GUID;     // 低延迟高质量
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

bool NvEncoder::CreateConvertPipeline() {
    // 1) 编译 BGRA→NV12 compute shader（BT.709）
    Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(kCsSource, std::strlen(kCsSource), nullptr, nullptr, nullptr,
                            "main", "cs_5_0", 0, 0, &blob, &err);
    if (FAILED(hr)) {
        m_lastError = "D3DCompile 失败（BGRA→NV12 shader）";
        return false;
    }
    hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                       blob->GetBufferSize(), nullptr, &m_cs);
    if (FAILED(hr)) {
        m_lastError = "CreateComputeShader 失败";
        return false;
    }

    // 2) NV12 纹理池 + UAV（Y=slice0 / UV=slice1）+ NVENC 注册
    m_pool.resize(kPoolSize);
    for (UINT i = 0; i < kPoolSize; ++i) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width          = m_width;
        td.Height         = m_height;
        td.MipLevels      = 1;
        td.ArraySize      = 1;
        td.Format         = DXGI_FORMAT_NV12;
        td.SampleDesc.Count = 1;
        td.Usage          = D3D11_USAGE_DEFAULT;
        td.BindFlags      = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_device->CreateTexture2D(&td, nullptr, &m_pool[i].nv12))) {
            m_lastError = "CreateTexture2D(NV12) 失败";
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uv.Texture2DArray.ArraySize = 1;
        uv.Format = DXGI_FORMAT_R8_UNORM;            // Y 平面
        uv.Texture2DArray.FirstArraySlice = 0;
        if (FAILED(m_device->CreateUnorderedAccessView(m_pool[i].nv12.Get(), &uv, &m_pool[i].yUav))) {
            m_lastError = "CreateUAV(Y) 失败";
            return false;
        }
        uv.Format = DXGI_FORMAT_R8G8_UNORM;          // UV 平面
        uv.Texture2DArray.FirstArraySlice = 1;
        if (FAILED(m_device->CreateUnorderedAccessView(m_pool[i].nv12.Get(), &uv, &m_pool[i].uvUav))) {
            m_lastError = "CreateUAV(UV) 失败";
            return false;
        }

        NV_ENC_REGISTER_RESOURCE reg{};
        reg.version       = NV_ENC_REGISTER_RESOURCE_VER;
        reg.resourceType  = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        reg.resource      = m_pool[i].nv12.Get();    // DirectX 输入：GPU 纹理直通编码
        reg.width         = m_width;
        reg.height        = m_height;
        reg.bufferFormat  = NV_ENC_BUFFER_FORMAT_NV12;
        reg.bufferUsage   = NV_ENC_INPUT_IMAGE;
        if (m_api.nvEncRegisterResource(m_session, &reg) != NV_ENC_SUCCESS) {
            m_lastError = "nvEncRegisterResource 失败";
            return false;
        }
        m_pool[i].registered = reg.registeredResource;
    }
    return true;
}

bool NvEncoder::ConvertToNv12(ID3D11Texture2D* src, InputBuffer& dst) {
    D3D11_TEXTURE2D_DESC sd{};
    src->GetDesc(&sd);
    if (sd.Width != m_width || sd.Height != m_height) return false;  // 尺寸不匹配，丢帧

    // SRV 缓存（WGC 帧池纹理只有 2-3 个，避免每帧创建）
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

    // GPU 色彩转换：BGRA 纹理 → NV12 纹理（零 CPU 参与）
    m_ctx->CSSetShader(m_cs.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = { srv.Get() };
    m_ctx->CSSetShaderResources(0, 1, srvs);
    ID3D11UnorderedAccessView* uavs[] = { dst.yUav.Get(), dst.uvUav.Get() };
    m_ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    m_ctx->Dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);

    // 解绑，确保 NVENC 读取纹理前 GPU 已完成写入
    ID3D11UnorderedAccessView* nullUavs[] = { nullptr, nullptr };
    m_ctx->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
    ID3D11ShaderResourceView* nullSrvs[] = { nullptr };
    m_ctx->CSSetShaderResources(0, 1, nullSrvs);
    m_ctx->Flush();
    return true;
}

void NvEncoder::SubmitFrame(const CaptureFrame& f, InputBuffer& buf) {
    // PTS：QPC 捕获时间戳 → 100ns 单位（真实捕获时刻，而非帧序号）
    int64_t pts = 0;
    if (f.captureQpc > 0 && m_qpcFreq > 0) {
        pts = (f.captureQpc * 10000000LL) / m_qpcFreq;
    } else {
        pts = m_frameIdx * (10000000LL / m_fps);
    }

    NV_ENC_PIC_PARAMS pic{};
    pic.version          = NV_ENC_PIC_PARAMS_VER;
    pic.inputBuffer      = buf.registered;           // DirectX 输入缓冲
    pic.bufferFmt        = NV_ENC_BUFFER_FORMAT_NV12;
    pic.inputWidth       = m_width;
    pic.inputHeight      = m_height;
    pic.inputTimeStamp   = pts;                      // QPC 时间戳（100ns）
    pic.outputBitstream  = m_bitstreamBuffer;
    pic.pictureStruct    = NV_ENC_PIC_STRUCT_FRAME;

    if (m_api.nvEncEncodeFrame(m_session, &pic) == NV_ENC_SUCCESS) {
        LARGE_INTEGER q{};
        QueryPerformanceCounter(&q);
        m_pending.emplace_back(m_frameIdx, q.QuadPart);
        m_frameIdx++;
        m_submitted.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_failed.fetch_add(1, std::memory_order_relaxed);   // 提交失败计数
    }
}

void NvEncoder::DrainBitstream() {
    NV_ENC_LOCK_BITSTREAM lock{};
    lock.version     = NV_ENC_LOCK_BITSTREAM_VER;
    lock.outputBuffer = m_bitstreamBuffer;
    lock.doNotWait    = 0;

    const NVENCSTATUS st = m_api.nvEncLockBitstream(m_session, &lock);
    if (st == NV_ENC_SUCCESS && lock.bitstreamSizeInBytes > 0) {
        if (m_out) {
            fwrite(lock.bitstreamBufferPtr, 1, lock.bitstreamSizeInBytes, m_out);
            m_bytes.fetch_add(lock.bitstreamSizeInBytes, std::memory_order_relaxed);
        }
        // 延迟：提交时刻 → 码流输出时刻
        if (!m_pending.empty()) {
            const int64_t submitQpc = m_pending.front().second;
            m_pending.pop_front();
            if (m_qpcFreq > 0) {
                LARGE_INTEGER q{};
                QueryPerformanceCounter(&q);
                const double ms = double(q.QuadPart - submitQpc) * 1000.0 / double(m_qpcFreq);
                const double cur = m_latencyMs.load();
                m_latencyMs.store(cur == 0.0 ? ms : cur + (ms - cur) * 0.1);
            }
        }
        // 编码输出 FPS（EMA）
        if (m_qpcFreq > 0) {
            LARGE_INTEGER q{};
            QueryPerformanceCounter(&q);
            if (m_lastEncQpc != 0) {
                const double d = double(q.QuadPart - m_lastEncQpc);
                if (d > 0) {
                    const double inst = double(m_qpcFreq) / d;
                    const double cur = m_fps.load();
                    m_fps.store(cur == 0.0 ? inst : cur + (inst - cur) * 0.1);
                }
            }
            m_lastEncQpc = q.QuadPart;
        }
        m_encoded.fetch_add(1, std::memory_order_relaxed);
        m_api.nvEncUnlockBitstream(m_session, lock.outputBuffer);
    }
}

void NvEncoder::EncoderThread() {
    while (m_running.load(std::memory_order_relaxed)) {
        CaptureFrame f;
        if (m_queue.Pop(f)) {
            InputBuffer& buf = m_pool[m_poolIdx % kPoolSize];
            m_poolIdx++;
            if (ConvertToNv12(f.texture.Get(), buf)) {
                SubmitFrame(f, buf);
            } else {
                m_failed.fetch_add(1, std::memory_order_relaxed);   // 转换失败计数
            }
            f.texture.Reset();   // 释放输入纹理引用（帧所有权：Consumer→Encoder→释放）
        }
        DrainBitstream();
        Sleep(1);
    }
    FlushAndClose();
}

void NvEncoder::FlushAndClose() {
    // flush 编码器，收尾剩余码流
    if (m_session && m_api.nvEncFlushEncoder) {
        m_api.nvEncFlushEncoder(m_session);
        for (;;) {
            NV_ENC_LOCK_BITSTREAM lock{};
            lock.version     = NV_ENC_LOCK_BITSTREAM_VER;
            lock.outputBuffer = m_bitstreamBuffer;
            lock.doNotWait    = 1;
            const NVENCSTATUS st = m_api.nvEncLockBitstream(m_session, &lock);
            if (st == NV_ENC_ERR_END_OF_STREAM) break;      // 全部输出完毕
            if (st == NV_ENC_SUCCESS && lock.bitstreamSizeInBytes > 0) {
                if (m_out) {
                    fwrite(lock.bitstreamBufferPtr, 1, lock.bitstreamSizeInBytes, m_out);
                    m_bytes.fetch_add(lock.bitstreamSizeInBytes, std::memory_order_relaxed);
                }
                if (!m_pending.empty()) m_pending.pop_front();
                m_encoded.fetch_add(1, std::memory_order_relaxed);
                m_api.nvEncUnlockBitstream(m_session, lock.outputBuffer);
            } else {
                break;
            }
        }
    }
    if (m_out) {
        fflush(m_out);
        fclose(m_out);
        m_out = nullptr;
    }
    m_running.store(false);
}

bool NvEncoder::Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
                           uint32_t fps, uint32_t bitrateKbps,
                           const std::string& outputPath) {
    if (m_running.load()) return false;
    m_device = device;
    m_device->GetImmediateContext(&m_ctx);
    m_width = width; m_height = height; m_fps = fps; m_bitrateKbps = bitrateKbps;
    m_outPath = outputPath;
    m_lastError.clear();

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    m_qpcFreq = freq.QuadPart;

    if (!LoadApi()) return false;
    if (!CreateSession()) return false;
    if (!CreateConfig()) return false;
    if (!CreateConvertPipeline()) return false;

    m_out = fopen(m_outPath.c_str(), "wb");
    if (!m_out) {
        m_lastError = "无法创建输出文件: " + m_outPath;
        return false;
    }

    m_queue.Reset();
    m_running.store(true);
    m_thread = std::thread(&NvEncoder::EncoderThread, this);
    return true;
}

void NvEncoder::Shutdown() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();

    // 释放资源
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
    if (m_nvModule) {
        FreeLibrary(m_nvModule);
        m_nvModule = nullptr;
    }
    m_pending.clear();
}

void NvEncoder::PushFrame(CaptureFrame&& f) {
    if (m_running.load(std::memory_order_relaxed)) {
        m_queue.Push(std::move(f));     // 满时丢最旧（有界队列）
    }
}

bool NvEncoder::IsRunning() const { return m_running.load(); }
uint64_t NvEncoder::Submitted() const { return m_submitted.load(std::memory_order_relaxed); }
uint64_t NvEncoder::Encoded() const { return m_encoded.load(std::memory_order_relaxed); }
uint64_t NvEncoder::FailedFrames() const { return m_failed.load(std::memory_order_relaxed); }
double NvEncoder::AvgLatencyMs() const { return m_latencyMs.load(); }
uint32_t NvEncoder::EncoderFps() const { return uint32_t(m_fps.load() + 0.5); }
uint32_t NvEncoder::EncoderBusyPct() const {
    // 编码引擎占用估算：平均延迟 / 帧间隔
    const double lat = m_latencyMs.load();
    if (lat <= 0.0 || m_fps == 0) return 0;
    const double pct = lat / (1000.0 / double(m_fps)) * 100.0;
    return uint32_t(std::min(100.0, pct));
}
uint64_t NvEncoder::BitstreamBytes() const { return m_bytes.load(std::memory_order_relaxed); }
std::string NvEncoder::LastError() const { return m_lastError; }

} // namespace sf
