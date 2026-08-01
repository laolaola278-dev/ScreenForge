// SANDBOX SIMULATOR — 沙盒演示（命令行）
// 完整模拟链路 + 实时统计打印 + 虚拟 MP4 报告

#include "SandboxDemo.h"

#include <Windows.h>

#include <cstdio>

#include "SandboxRecorder.h"

namespace sf {

int RunSandboxDemo(uint64_t seconds, const std::string& mp4Path,
                   const std::string& reportPath) {
    printf("===== ScreenForge Sandbox Demo =====\n");
    printf("mode    : SANDBOX\n");
    printf("capture : SyntheticCapture\n");
    printf("encoder : SimEncoder (1~5ms)\n");
    printf("muxer   : SimMuxer (virtual MP4)\n");
    printf("target  : 1920x1080 @ 60fps · %llu s\n", (unsigned long long)seconds);
    printf("NOTE    : SANDBOX SIMULATION ONLY — NOT real WGC/NVENC/FFmpeg\n");
    fflush(stdout);

    SandboxRecorder rec;
    const uint64_t frames = seconds * 60;
    if (!rec.Start(1920, 1080, 60, frames, mp4Path, reportPath)) {
        printf("FAIL: sandbox recorder start\n");
        return 1;
    }

    uint64_t lastPrint = 0;
    while (rec.IsRecording()) {
        Sleep(250);
        const uint64_t s = uint64_t(rec.DurationSec());
        if (s != lastPrint) {
            lastPrint = s;
            printf("  [%3llu s] fps=%u frames=%llu drop=%llu lat=%.2fms q=%u\n",
                   (unsigned long long)s, rec.Fps(),
                   (unsigned long long)rec.Frames(),
                   (unsigned long long)rec.Dropped(),
                   rec.LatencyMs(), rec.QueueDepth());
        }
    }
    rec.Stop();

    printf("\n===== Sandbox Report =====\n");
    printf("mode        : SANDBOX\n");
    printf("frames      : %llu\n", (unsigned long long)rec.Frames());
    printf("duration    : %.2f s\n", rec.DurationSec());
    printf("fps         : %u\n", rec.Fps());
    printf("latency     : %.2f ms\n", rec.LatencyMs());
    printf("dropped     : %llu\n", (unsigned long long)rec.Dropped());
    printf("queueDepth  : %u\n", rec.QueueDepth());
    printf("audioSync   : %.2f ms\n", rec.AudioSyncMs());
    printf("report      : %s\n", reportPath.c_str());
    printf("virtual mp4 : %s\n", mp4Path.c_str());
    printf("------------------------------------------\n");
    printf("SANDBOX SIMULATION ONLY — NOT real hardware results\n");
    printf("==========================================\n");
    return 0;
}

} // namespace sf
