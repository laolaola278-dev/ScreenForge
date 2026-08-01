#pragma once

#include <cstdint>
#include <string>

namespace sf {

// Phase 5-A 真实硬件录制测试
// 流程：
//   1) 硬件检测（GPU 型号 / NVENC 版本 / 编码能力）
//   2) 有 NVIDIA GPU + NVENC → NvEncoderHardware + WGC（或 GPU 合成纹理）
//   3) 无 NVIDIA GPU / NVENC 不可用 → 自动回退 NvencSimulator（报告注明）
// 输出：recording.mp4 + hardware_report.json（明确 realNvencHardware=true/false）
// 禁止伪造：所有结果来自真实检测/运行；沙盒无法运行时不执行硬件路径
int RunHardwareRecordTest(uint64_t seconds, uint32_t fps,
                          const std::string& mp4Path,
                          const std::string& reportPath);

} // namespace sf
