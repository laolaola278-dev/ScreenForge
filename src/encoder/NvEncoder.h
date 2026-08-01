#pragma once

// ScreenForge Phase 3-A — NVENC 硬件编码器（实现 IEncoder 接口）
// 输入：ID3D11Texture2D（BGRA，GPU 驻留，禁止 CPU 回读）
// 处理：GPU compute shader 色彩转换 BGRA→NV12（BT.709 有限范围）
//       → nvEncEncodeFrame（DirectX 输入，PTS 取自 QPC 时间戳）
// 输出：H264 裸码流文件（test.h264）
// 线程：Pipeline Consumer → FrameQueue(SPSC) → NVENC Thread
// 说明：与 NvencSimulator 实现同一 IEncoder 接口，可直接互换

#include <nvEncodeAPI.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "IEncoder.h"
#include "graphics/Frame.h"
#include "pipeline/FrameQueue.h"

namespace sf {

class NvEncoder : public IEncoder {
public:
    NvEncoder() = default;
    ~NvEncoder() override;

    // 初始化：加载 NVENC → 打开会话（D3D11 设备）→ 配置 H264 → 创建 GPU 转换管线
    bool Initialize(ID3D11Device* device,
                    uint32_t width, uint32_t height,
                    uint32_t fps, uint32_t bitrateKbps,
                    const std::string& outputPath) override;
    void Shutdown() override;                        // 停止线程 + flush 剩余码流 + 释放全部资源
    bool IsRunning() const override;

    // 生产端入口（Pipeline Consumer / 验证器调用，SPSC Push）
    void PushFrame(CaptureFrame&& f) override;

    // 统计（UI / 验证报告读取）
    uint64_t Submitted() const override;             // 已提交编码帧数
    uint64_t Encoded() const override;               // 已输出码流帧数
    uint64_t FailedFrames() const override;          // 失败帧数（提交失败 + 转换失败）
    uint64_t DroppedFrames() const override { return m_queue.TotalDropped(); }  // 队列丢弃
    double   AvgLatencyMs() const override;          // 提交→码流输出 平均延迟（EMA）
    uint32_t EncoderFps() const;                     // 编码输出帧率（EMA）
    uint32_t EncoderBusyPct() const;                 // 编码引擎占用估算（延迟/帧间隔）
    uint64_t BitstreamBytes() const override;        // 已写入文件字节数
    std::string LastError() const;

private:
    struct InputBuffer {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12;         // NV12 转换目标
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> yUav, uvUav;
        NV_ENC_INPUT_PTR registered = nullptr;                // 已注册的编码输入
    };

    bool LoadApi();                         // LoadLibrary nvEncodeAPI64.dll
    bool CreateSession();                   // nvEncOpenEncodeSessionEx(D3D11)
    bool CreateConfig();                    // NV_ENC_INITIALIZE_PARAMS (H264 1080p60 CBR)
    bool CreateConvertPipeline();           // D3DCompile CS + NV12 纹理池 + RegisterResource
    bool ConvertToNv12(ID3D11Texture2D* src, InputBuffer& dst);   // GPU 色彩转换
    void SubmitFrame(const CaptureFrame& f, InputBuffer& buf);    // nvEncEncodeFrame
    void DrainBitstream();                  // nvEncLockBitstream → 写文件
    void EncoderThread();
    void FlushAndClose();                   // nvEncFlushEncoder + 收尾码流 + 关文件

    HMODULE m_nvModule = nullptr;
    NV_ENCODE_API_FUNCTION_LIST m_api{};
    void* m_session = nullptr;
    NV_ENC_INPUT_PTR m_bitstreamBuffer = nullptr;
    const uint32_t m_bitstreamSize = 2 * 1024 * 1024;

    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_ctx;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_cs;
    std::unordered_map<ID3D11Texture2D*,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_srvCache;  // WGC 帧池纹理缓存

    std::vector<InputBuffer> m_pool;        // 3 个轮换输入缓冲
    uint32_t m_poolIdx = 0;

    uint32_t m_width = 1920, m_height = 1080, m_fps = 60;
    uint32_t m_bitrateKbps = 12000;
    std::string m_outPath;
    FILE* m_out = nullptr;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    FrameQueue m_queue{8};                  // SPSC：Pipeline Consumer → NVENC Thread

    std::deque<std::pair<uint64_t, int64_t>> m_pending;   // frameIdx -> submitQpc
    std::atomic<uint64_t> m_submitted{0}, m_encoded{0}, m_failed{0}, m_bytes{0};
    std::atomic<double> m_latencyMs{0.0}, m_fps{0.0};
    int64_t m_qpcFreq = 0;
    int64_t m_lastEncQpc = 0;
    uint64_t m_frameIdx = 0;
    std::string m_lastError;
};

} // namespace sf
