#pragma once

#include <cstdint>
#include <string>

namespace sf {

// Phase 6-A 音视频录制测试
// 视频：RecorderEngine（架构不变）+ NvencSimulator（沙盒标注）
// 音频：AudioCapture（系统声音 Loopback）→ Mp4Muxer AAC 音频流
//       MicrophoneCapture（麦克风）→ 仅计数（禁止混音器）
// 时间同步：视频/音频 PTS 均来自同一 QPC clock
// 输出：recording_with_audio.mp4 + audio_report.json
//       （systemAudioFrames / micFrames / sampleRate / channels / avSyncOffsetMs）
// 声明：WASAPI 为真实实现，运行时验证需真机（沙盒限制）
int RunAudioRecordTest(uint64_t seconds, uint32_t fps,
                       const std::string& mp4Path,
                       const std::string& reportPath);

} // namespace sf
