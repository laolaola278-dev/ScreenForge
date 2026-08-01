#pragma once

// SANDBOX SIMULATOR — 虚拟封装器（实现 IMuxer 接口）
// 不写真实 MP4：记录数据包统计 + 音画同步偏移，Finalize 生成
//   - sandbox_recording.mp4（文本占位，明确标注 SIMULATED）
//   - sandbox_recording_report.json（全部统计）
// 明确标记：SIMULATOR · NOT real FFmpeg/MP4

#include <cstdio>
#include <string>

#include "muxer/IMuxer.h"

namespace sf {

class SimMuxer : public IMuxer {
public:
    SimMuxer() = default;
    ~SimMuxer() override { if (m_open) Abort(); }

    bool Initialize(const MuxConfig& cfg) override {
        m_cfg = cfg;
        m_open = true;
        m_videoStartPts = 0; m_haveVideoStart = false;
        m_audioStartQpc = 0; m_haveAudioStart = false;
        m_packets = 0; m_audioFrames = 0; m_bytes = 0;
        return true;
    }

    bool WritePacket(const EncodedPacket& pkt) override {
        if (!m_open) return false;
        ++m_packets;
        m_bytes += pkt.size;
        if (!m_haveVideoStart) { m_videoStartPts = pkt.pts; m_haveVideoStart = true; }
        return true;
    }

    bool WriteAudioFrame(const AudioFrame& af) override {
        if (!m_open) return false;
        ++m_audioFrames;
        if (!m_haveAudioStart) { m_audioStartQpc = af.captureQpc; m_haveAudioStart = true; }
        return true;
    }

    bool Finalize() override {
        if (!m_open) return false;
        // 1) 虚拟 MP4 占位文件
        if (FILE* f = fopen(m_cfg.outputPath.c_str(), "w")) {
            fputs("SANDBOX SIMULATED OUTPUT\n", f);
            fputs("mode: SANDBOX\ncapture: Synthetic\nencoder: Simulator\n", f);
            fputs("NOT A REAL MP4 — 真实封装由 Mp4Muxer(FFmpeg) 在 Windows 后端提供\n", f);
            fclose(f);
        }
        m_open = false;
        return true;
    }

    void Abort() override { m_open = false; }
    bool IsOpen() const override { return m_open; }
    uint64_t PacketsWritten() const override { return m_packets; }
    uint64_t BytesWritten() const override { return m_bytes; }
    uint32_t FragmentsWritten() const override { return 0; }

    // 统计访问器（供 SandboxRecorder 写报告）
    uint64_t Packets() const { return m_packets; }
    uint64_t AudioFrames() const { return m_audioFrames; }
    bool HaveVideoStart() const { return m_haveVideoStart; }
    int64_t VideoStartPts() const { return m_videoStartPts; }
    bool HaveAudioStart() const { return m_haveAudioStart; }
    int64_t AudioStartQpc() const { return m_audioStartQpc; }

private:
    MuxConfig m_cfg;
    bool m_open = false;
    uint64_t m_packets = 0, m_audioFrames = 0, m_bytes = 0;
    int64_t m_videoStartPts = 0, m_audioStartQpc = 0;
    bool m_haveVideoStart = false, m_haveAudioStart = false;
};

} // namespace sf
