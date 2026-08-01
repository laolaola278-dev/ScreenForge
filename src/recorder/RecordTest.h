#pragma once

#include <cstdint>
#include <string>

namespace sf {

// Phase 4-B 录制测试：完整视频链路集成（沙盒可运行）
//   SyntheticFrameSource → RecorderEngine(Pacing) → NvencSimulator → Mp4Muxer → recording.mp4
// 输出：recording.mp4 + recording_session.json + 控制台报告
// 声明：Encoder 为 NvencSimulator（模拟，沙盒允许）；真实 NVENC NOT TESTED
int RunRecordTest(uint64_t seconds, uint32_t fps,
                  const std::string& mp4Path,
                  const std::string& sessionJsonPath);

} // namespace sf
