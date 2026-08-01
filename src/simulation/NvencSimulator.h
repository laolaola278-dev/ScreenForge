#pragma once

// Phase 3-B Sandbox — NVENC 链路模拟器（实现 IEncoder 接口）
// 模拟：编码延迟（平均 2ms，范围 1~5ms）· GPU 编码队列 · 随机 0.01% 失败
//       · encoder busy / queue full / frame drop
// 输出：simulation_test.h264（文件头标记 SIMULATION_OUTPUT=true，禁止伪装真实 NVENC）
// Phase 4-B：新增 SetPacketSink（编码后数据包 → RecorderEngine → Mp4Muxer）
// 未来替换：将本类换成 NvEncoder 即可接入真实硬件（接口一致）。

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "encoder/IEncoder.h"
#include "muxer/IMuxer.h"
#include "pipeline/FrameQueue.h"

namespace sf {

class NvencSimulator : public IEncoder {
public:
    using PacketSink = std::function<void(const EncodedPacket&)>;

    NvencSimulator();
    ~NvencSimulator() override;

    bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
                    uint32_t fps, uint32_t bitrateKbps,
                    const std::string& outputPath) override;
    void Shutdown() override;
    void PushFrame(CaptureFrame&& f) override;
    bool IsRunning() const override;

    uint64_t Submitted() const override;
    uint64_t Encoded() const override;
    uint64_t FailedFrames() const override;
    uint64_t DroppedFrames() const override;
    double   AvgLatencyMs() const override;
    double   P95LatencyMs() const;
    uint64_t BitstreamBytes() const override;

    // Phase 4-B：编码后数据包回调（RecorderEngine → Muxer 链路）
    void SetPacketSink(PacketSink sink);

    // 取回已编码帧的 PTS 序列（供连续性验证；Shutdown 后调用）
    std::vector<int64_t> TakePtsSequence();

private:
    void EncodeThread();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    FrameQueue m_queue{16};          // 编码器输入队列（满 → 丢帧）

    FILE* m_out = nullptr;
    std::string m_outPath;
    uint32_t m_width = 1920, m_height = 1080, m_fps = 60;

    std::mt19937 m_rng;
    std::mutex m_ptsMtx;
    std::vector<int64_t> m_ptsSeq;
    PacketSink m_sink;               // Phase 4-B
    std::vector<uint8_t> m_pktBuf;   // Phase 4-B：当前数据包缓冲

    std::atomic<uint64_t> m_submitted{0}, m_encoded{0}, m_failed{0}, m_dropped{0}, m_bytes{0};
    std::atomic<double> m_latencyMs{0.0}, m_p95Ms{0.0};
    int64_t m_qpcFreq = 0;
};

} // namespace sf
