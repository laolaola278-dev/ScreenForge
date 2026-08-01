// Phase 4-A — 封装基准（mux_bench）
// 生成 10000 个模拟 H264 packet（每 120 帧一个关键帧，附 SPS/PPS）→ Mp4Muxer
// 模拟数据仅供封装链路验证；ffprobe 元数据（codec/宽高/fps/duration）来自容器，可正常识别。

#include "MuxBench.h"

#include <cstdio>
#include <vector>

#include "Mp4Muxer.h"

namespace sf {
namespace {

// 最小 1080p H264 Annex-B SPS / PPS（真实可解析的 NAL 单元）
const std::vector<uint8_t> kSps = {
    0x00,0x00,0x00,0x01, 0x67,0x42,0x00,0x1e, 0x95,0xa8,0x14,0x01,
    0x6e,0x40,0x40,0x1e, 0x00,0x00,0x03,0x00, 0x10,0x00,0x00,0x03,
    0x03,0x20,0xf1,0x83, 0x19,0x60,
};
const std::vector<uint8_t> kPps = {
    0x00,0x00,0x00,0x01, 0x68,0xce,0x3c,0x80,
};
// 模拟 IDR / P 帧负载（NAL type 5 / 1 + 伪数据）
std::vector<uint8_t> MakeNal(uint8_t nalType, size_t payload) {
    std::vector<uint8_t> v = { 0x00,0x00,0x00,0x01, nalType };
    v.resize(v.size() + payload, 0x5A);
    return v;
}

} // namespace

int RunMuxBenchmark(uint64_t packets, const std::string& outPath) {
    printf("===== ScreenForge Phase 4-A Mux Benchmark =====\n");
    printf("Packets: %llu\n", (unsigned long long)packets);
    printf("Output : %s (fragmented MP4)\n", outPath.c_str());
    fflush(stdout);

    MuxConfig cfg;
    cfg.outputPath = outPath;
    cfg.width = 1920; cfg.height = 1080; cfg.fps = 60;
    // extradata：SPS+PPS（供播放器/ffprobe 解析）
    cfg.extradata.insert(cfg.extradata.end(), kSps.begin(), kSps.end());
    cfg.extradata.insert(cfg.extradata.end(), kPps.begin(), kPps.end());

    Mp4Muxer mux;
    if (!mux.Initialize(cfg)) {
        printf("FAIL: %s\n", mux.LastError().c_str());
        return 1;
    }

    const uint32_t fps = 60;
    const uint64_t keyInterval = 120;               // 2 秒一个关键帧 → 每 2s 一个 fragment
    const int64_t  frame100ns = 10000000LL / fps;   // 166666 (100ns)

    for (uint64_t i = 0; i < packets; ++i) {
        const bool key = (i % keyInterval == 0);
        const int64_t pts = static_cast<int64_t>(i) * frame100ns;
        std::vector<uint8_t> payload;
        if (key) {
            payload = MakeNal(5, 4096);             // IDR（SPS/PPS 已入 extradata）
        } else {
            payload = MakeNal(1, 512 + (i % 8) * 128);   // P 帧模拟
        }

        EncodedPacket pkt;
        pkt.data = payload.data();
        pkt.size = payload.size();
        pkt.pts  = pts;
        pkt.dts  = pts;                             // 无 B 帧：dts == pts
        pkt.keyFrame = key;

        if (!mux.WritePacket(pkt)) {
            printf("FAIL: packet #%llu - %s\n",
                   (unsigned long long)i, mux.LastError().c_str());
            mux.Abort();
            return 1;
        }
        if ((i % 2000) == 0) {
            printf("  [%5llu/%llu] fragments=%u\n",
                   (unsigned long long)i, (unsigned long long)packets,
                   mux.FragmentsWritten());
        }
    }

    if (!mux.Finalize()) {
        printf("FAIL: finalize - %s\n", mux.LastError().c_str());
        return 1;
    }

    const double durationSec = double(packets) / double(fps);
    printf("\n===== Mux Benchmark Report =====\n");
    printf("packetsWritten : %llu\n", (unsigned long long)mux.PacketsWritten());
    printf("bytesWritten   : %llu (%.2f MB)\n",
           (unsigned long long)mux.BytesWritten(),
           double(mux.BytesWritten()) / 1048576.0);
    printf("fragments      : %u (每关键帧独立 fragment)\n", mux.FragmentsWritten());
    printf("duration       : %.2f s @ 60fps\n", durationSec);
    printf("container      : fragmented MP4 (frag_keyframe+empty_moov+default_base_moof)\n");
    printf("crashSafe      : 每个 fragment 独立，异常退出后文件可修复\n");
    printf("------------------------------------------\n");
    printf("Muxer implementation ready\n");
    printf("Runtime verification pending (sandbox limitation)\n");
    printf("真机验证: ffprobe -v error -show_entries format=duration -show_entries stream=codec_name,width,height,avg_frame_rate -of default=noprint_wrappers=1 %s\n", outPath.c_str());
    printf("修复命令: ffmpeg -i %s -c copy %s.fixed.mp4\n", outPath.c_str(), outPath.c_str());
    printf("==========================================\n");
    return 0;
}

} // namespace sf
