#pragma once

#include <cstdint>
#include <string>

namespace sf {

// Phase 4-A 封装基准：模拟 10000 个编码后 packet → fragmented MP4
// 验证：封装流程 · fragment 划分 · PTS/DTS 换算 · 崩溃安全结构
// 输出：mux_bench.mp4 + 统计（packets / bytes / fragments / duration）
// 真机验证：ffprobe mux_bench.mp4（codec=h264 · duration≈167s · fps=60）
// 声明：Muxer implementation ready / Runtime verification pending
int RunMuxBenchmark(uint64_t packets,
                    const std::string& outPath);

} // namespace sf
