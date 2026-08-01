// Phase 4-B — 录制测试（record-test）
// 链路：SyntheticFrameSource → RecorderEngine(60fps Pacing) → NvencSimulator
//       → EncodedPacket(Sink) → Mp4Muxer → recording.mp4
// 沙盒模式：使用 NvencSimulator（允许）；禁止伪装真实 NVENC。

#include "RecordTest.h"

#include <Windows.h>

#include <cstdio>
#include <vector>

#include "Mp4Muxer.h"
#include "NvencSimulator.h"
#include "RecorderEngine.h"

namespace sf {
namespace {

// 最小 1080p H264 Annex-B SPS / PPS（与 mux_bench 一致）
const std::vector<uint8_t> kSps = {
    0x00,0x00,0x00,0x01, 0x67,0x42,0x00,0x1e, 0x95,0xa8,0x14,0x01,
    0x6e,0x40,0x40,0x1e, 0x00,0x00,0x03,0x00, 0x10,0x00,0x00,0x03,
    0x03,0x20,0xf1,0x83, 0x19,0x60,
};
const std::vector<uint8_t> kPps = {
    0x00,0x00,0x00,0x01, 0x68,0xce,0x3c,0x80,
};

} // namespace

int RunRecordTest(uint64_t seconds, uint32_t fps,
                  const std::string& mp4Path,
                  const std::string& sessionJsonPath) {
    const uint32_t w = 1920, h = 1080;
    const uint64_t frames = seconds * fps;

    printf("===== ScreenForge Phase 4-B Record Test =====\n");
    printf("Target : %ux%u @ %u fps, %llu s (%llu frames)\n",
           w, h, fps, (unsigned long long)seconds, (unsigned long long)frames);
    printf("Output : %s\n", mp4Path.c_str());
    printf("Encoder: NvencSimulator (simulation — sandbox allowed)\n");
    printf("Muxer  : Mp4Muxer (FFmpeg libavformat)\n");
    fflush(stdout);

    // 1) 编码器（模拟）
    NvencSimulator enc;
    if (!enc.Initialize(nullptr, w, h, fps, 12000, "")) {
        printf("FAIL: encoder init failed\n");
        return 1;
    }

    // 2) 封装器
    Mp4Muxer mux;
    bool muxOk = false;

    // 3) 引擎接线：编码器数据包 → 封装器
    enc.SetPacketSink([&](const EncodedPacket& pkt) {
        if (muxOk) mux.WritePacket(pkt);
    });

    RecorderConfig cfg;
    cfg.width = w; cfg.height = h; cfg.fps = fps;
    cfg.bitrateKbps = 12000;
    cfg.framesLimit = frames;
    cfg.mp4Path = mp4Path;
    cfg.sessionJsonPath = sessionJsonPath;
    cfg.extradata.insert(cfg.extradata.end(), kSps.begin(), kSps.end());
    cfg.extradata.insert(cfg.extradata.end(), kPps.begin(), kPps.end());
    cfg.captureSource = nullptr;          // 合成帧源（沙盒）
    cfg.encoderIsSimulator = true;        // 会话 JSON 明确标注

    RecorderEngine engine;
    if (!engine.Initialize(cfg, &enc, &mux)) {
        printf("FAIL: engine init - %s\n", engine.LastError().c_str());
        printf("  （若为 muxer 失败：需安装 FFmpeg dev 并设置 FFMPEG_ROOT）\n");
        enc.Shutdown();
        return 1;
    }
    muxOk = true;

    if (!engine.StartRecording()) {
        printf("FAIL: start - %s\n", engine.LastError().c_str());
        enc.Shutdown();
        return 1;
    }

    // 4) 等待录制完成（达到帧数上限自动 Stopping）
    while (engine.State() == RecorderState::Recording) {
        Sleep(200);
        if (engine.FramesPushed() % (fps * 10) == 0 && engine.FramesPushed() > 0) {
            printf("  [%llu/%llu frames] state=%s\n",
                   (unsigned long long)engine.FramesPushed(),
                   (unsigned long long)frames,
                   RecorderStateName(engine.State()));
        }
    }

    if (!engine.StopRecording()) {
        printf("FAIL: stop - %s\n", engine.LastError().c_str());
        enc.Shutdown();
        return 1;
    }
    enc.Shutdown();

    // 5) 报告
    printf("\n===== Record Test Report =====\n");
    printf("state         : %s\n", RecorderStateName(engine.State()));
    printf("frames        : %llu\n", (unsigned long long)engine.FramesRecorded());
    printf("duration      : %.2f s (target %.2f s)\n",
           engine.DurationSec(), double(seconds));
    printf("output        : %s\n", mp4Path.c_str());
    printf("session       : %s\n", sessionJsonPath.c_str());
    printf("------------------------------------------\n");
    printf("Encoder       : NvencSimulator (simulation)\n");
    printf("Real NVENC    : NOT TESTED (sandbox limitation)\n");
    printf("Muxer         : Mp4Muxer — runtime verification pending\n");
    printf("真机验证:\n");
    printf("  ffprobe -v error -show_entries format=duration -show_entries stream=codec_name,width,height,avg_frame_rate -of default=noprint_wrappers=1 %s\n", mp4Path.c_str());
    printf("  期望: codec=h264 · 1920x1080 · fps=60 · duration≈%llus\n", (unsigned long long)seconds);
    printf("==========================================\n");
    return 0;
}

} // namespace sf
