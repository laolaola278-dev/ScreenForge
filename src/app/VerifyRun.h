#pragma once

namespace sf {

// Phase 3-B：NVENC 真实硬件验证（无 UI 命令行模式）
// 用法：ScreenForge.exe --verify [--frames 10000] [--width 1920] [--height 1080]
//                          [--fps 60] [--bitrate 12000] [--out test.h264]
// 生成确定性测试图案（CPU→GPU 写入，非捕获管线）→ 编码器 → test.h264
// 输出 verify_report.txt（submitted / encoded / failed / latency / GPU 占用估算）
int RunHardwareVerify(int argc, char* argv[]);

} // namespace sf
