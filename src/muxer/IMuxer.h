#pragma once

// ScreenForge Phase 4-A/6-A — 封装器统一接口
// 输入：EncodedPacket（data/size/pts/dts/keyFrame）+ AudioFrame（音频，Phase 6-A）
// 输出：recording.mp4（fragmented MP4，可含视频流 + AAC 音频流）
// 当前实现：Mp4Muxer（FFmpeg libavformat）
// 崩溃安全：frag_keyframe + empty_moov + default_base_moof，每个 fragment 独立

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "audio/AudioFrame.h"

namespace sf {

// 编码后数据包（pts/dts 单位：100ns，与 NVENC/模拟器一致）
struct EncodedPacket {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    int64_t        pts  = 0;    // 100ns
    int64_t        dts  = 0;    // 100ns
    bool           keyFrame = false;
};

struct MuxConfig {
    std::string outputPath;
    uint32_t    width  = 1920;
    uint32_t    height = 1080;
    uint32_t    fps    = 60;
    std::vector<uint8_t> extradata;   // 可选：H264 SPS/PPS（Annex-B 拼接）
    // Phase 6-A：音频流（AAC 48kHz stereo）
    bool        audioEnabled    = false;
    uint32_t    audioSampleRate = 48000;
    uint16_t    audioChannels   = 2;
};

class IMuxer {
public:
    virtual ~IMuxer() = default;

    virtual bool Initialize(const MuxConfig& cfg) = 0;
    virtual bool WritePacket(const EncodedPacket& pkt) = 0;
    // Phase 6-A：写入音频帧（默认空实现，仅支持音频的 Muxer 覆写）
    virtual bool WriteAudioFrame(const AudioFrame& af) { (void)af; return false; }
    virtual bool Finalize() = 0;      // 正常结束：写 trailer
    virtual void Abort() = 0;         // 异常结束：尽力 flush 已写 fragment
    virtual bool IsOpen() const = 0;

    virtual uint64_t PacketsWritten() const = 0;
    virtual uint64_t BytesWritten() const = 0;
    virtual uint32_t FragmentsWritten() const = 0;
    virtual std::string LastError() const { return {}; }
};

} // namespace sf
