#pragma once

#include <cstdint>
#include <string>

namespace sf {

// SANDBOX SIMULATOR — 沙盒演示入口
// 用法：ScreenForge.exe --sandbox-demo [--seconds 10] [--out sandbox_recording.mp4]
//                                      [--json sandbox_recording_report.json]
// 运行完整模拟链路并打印实时统计；生成虚拟 MP4 + JSON 报告。
// 声明：SANDBOX SIMULATION ONLY · NOT real WGC/NVENC/FFmpeg
int RunSandboxDemo(uint64_t seconds, const std::string& mp4Path,
                   const std::string& reportPath);

} // namespace sf
