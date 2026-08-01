#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IMuxer.h"

namespace sf {

// MP4 封装器（FFmpeg libavformat）
// 特性：
//   - fragmented MP4：movflags = frag_keyframe + empty_moov + default_base_moof
//   - 每个关键帧独立 fragment → 异常退出后文件仍可修复
//   - PTS/DTS 从 100ns 换算到 stream timebase
// Phase 6-A：
//   - 音频流支持（AAC 48kHz stereo，libavcodec 编码）
//   - WriteAudioFrame(AudioFrame) — QPC 时间戳 → 1/48000 timebase
class Mp4Muxer : public IMuxer {
public:
    Mp4Muxer() = default;
    ~Mp4Muxer() override;

    bool Initialize(const MuxConfig& cfg) override;
    bool WritePacket(const EncodedPacket& pkt) override;
    bool WriteAudioFrame(const AudioFrame& af) override;
    bool Finalize() override;
    void Abort() override;
    bool IsOpen() const override;

    uint64_t PacketsWritten() const override;
    uint64_t BytesWritten() const override;
    uint32_t FragmentsWritten() const override;

    std::string LastError() const { return m_lastError; }

private:
    bool EncodePcmChunk();   // 8-A：从 pcmBuf 取一个完整 AAC 帧（1024×ch）编码写入；不足返回 false

    struct Impl;
    Impl* m_impl = nullptr;
    std::string m_lastError;
};

} // namespace sf
