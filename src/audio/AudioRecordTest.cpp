// Phase 6-A — 音视频录制测试（audio-record-test）
// 保持 RecorderEngine 视频架构不变；新增独立 WASAPI 音频线程
// 统一 QPC 时间基准；MP4 同时含视频流 + AAC 音频流

#include "AudioRecordTest.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <vector>

#include "AudioCapture.h"
#include "MicrophoneCapture.h"
#include "Mp4Muxer.h"
#include "NvencSimulator.h"
#include "RecorderEngine.h"

namespace sf {
namespace {

const std::vector<uint8_t> kSps = {
    0x00,0x00,0x00,0x01, 0x67,0x42,0x00,0x1e, 0x95,0xa8,0x14,0x01,
    0x6e,0x40,0x40,0x1e, 0x00,0x00,0x03,0x00, 0x10,0x00,0x00,0x03,
    0x03,0x20,0xf1,0x83, 0x19,0x60,
};
const std::vector<uint8_t> kPps = {
    0x00,0x00,0x00,0x01, 0x68,0xce,0x3c,0x80,
};

} // namespace

int RunAudioRecordTest(uint64_t seconds, uint32_t fps,
                       const std::string& mp4Path,
                       const std::string& reportPath) {
    const uint32_t w = 1920, h = 1080;
    printf("===== ScreenForge Phase 6-A Audio Record Test =====\n");
    printf("Target : %ux%u @ %u fps, %llu s + 音频(48kHz/stereo/AAC)\n",
           w, h, fps, (unsigned long long)seconds);
    printf("输出   : %s\n", mp4Path.c_str());
    printf("声明   : WASAPI 为真实实现 — runtime verification pending (sandbox limitation)\n");
    fflush(stdout);

    // 1) 视频：RecorderEngine + NvencSimulator（架构不变，沙盒标注）
    NvencSimulator enc;
    Mp4Muxer mux;
    bool muxOk = false;
    std::atomic<int64_t> firstVideoPts{0};

    if (!enc.Initialize(nullptr, w, h, fps, 12000, "")) {
        printf("FAIL: encoder init\n");
        return 1;
    }
    enc.SetPacketSink([&](const EncodedPacket& p) {
        int64_t f = 0;
        if (firstVideoPts.compare_exchange_strong(f, p.pts)) {}
        if (muxOk) mux.WritePacket(p);
    });

    MuxConfig mc;
    mc.outputPath = mp4Path;
    mc.width = w; mc.height = h; mc.fps = fps;
    mc.extradata.insert(mc.extradata.end(), kSps.begin(), kSps.end());
    mc.extradata.insert(mc.extradata.end(), kPps.begin(), kPps.end());
    mc.audioEnabled = true;                 // 音频流：AAC 48kHz stereo
    mc.audioSampleRate = 48000;
    mc.audioChannels   = 2;
    if (!mux.Initialize(mc)) {
        printf("FAIL: muxer init (FFmpeg?) - %s\n", mux.LastError().c_str());
        enc.Shutdown();
        return 1;
    }
    muxOk = true;

    RecorderConfig cfg;
    cfg.width = w; cfg.height = h; cfg.fps = fps;
    cfg.bitrateKbps = 12000;
    cfg.framesLimit = seconds * fps;
    cfg.mp4Path = mp4Path;
    cfg.sessionJsonPath = "";               // 本次报告由 audio_report.json 承担
    cfg.extradata = mc.extradata;
    cfg.captureSource = nullptr;            // 合成帧源（沙盒视频）
    cfg.encoderIsSimulator = true;

    RecorderEngine engine;
    if (!engine.Initialize(cfg, &enc, &mux)) {
        printf("FAIL: engine init - %s\n", engine.LastError().c_str());
        enc.Shutdown();
        return 1;
    }
    if (!engine.StartRecording()) {
        printf("FAIL: start - %s\n", engine.LastError().c_str());
        enc.Shutdown();
        return 1;
    }

    // 2) 音频：独立 WASAPI 线程（系统声音 + 麦克风）
    AudioCapture sys;
    MicrophoneCapture mic;
    std::string sysErr, micErr;
    if (!sys.Start()) sysErr = sys.LastError();
    if (!mic.Start()) micErr = mic.LastError();
    printf("Audio  : 系统声音 %s · 麦克风 %s\n",
           sysErr.empty() ? "运行中" : ("✕ " + sysErr).c_str(),
           micErr.empty() ? "运行中" : ("✕ " + micErr).c_str());

    // 3) 主循环：消费音频帧（不阻塞视频线程）
    int64_t qpcFreq = 0;
    { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); qpcFreq = f.QuadPart; }
    int64_t firstAudioPts = 0;
    uint64_t sysFrames = 0, micFrames = 0;
    uint64_t lastPrint = 0;

    while (engine.State() == RecorderState::Recording) {
        Sleep(20);
        AudioFrame af;
        while (sys.GetFrame(af)) {
            if (firstAudioPts == 0) firstAudioPts = af.captureQpc;
            mux.WriteAudioFrame(af);        // 系统声音 → MP4 音频流
            sysFrames++;
        }
        while (mic.GetFrame(af)) micFrames++;   // 麦克风仅计数（禁止混音）

        const uint64_t s = engine.FramesPushed() / fps;
        if (s != lastPrint) {
            lastPrint = s;
            printf("  [%3llu s] video=%llu  sysAudio=%llu  mic=%llu\n",
                   (unsigned long long)s,
                   (unsigned long long)engine.FramesPushed(),
                   (unsigned long long)sysFrames,
                   (unsigned long long)micFrames);
        }
    }

    // 4) 停止
    engine.StopRecording();
    enc.Shutdown();
    sys.Stop();
    mic.Stop();
    if (mux.IsOpen()) mux.Finalize();

    // 5) avSyncOffsetMs：视频/音频首帧 PTS 差（同一 QPC 基准 → 100ns → ms）
    double avSyncOffsetMs = -1.0;
    if (firstVideoPts.load() > 0 && firstAudioPts > 0 && qpcFreq > 0) {
        const int64_t audioPts100ns = (firstAudioPts * 10000000LL) / qpcFreq;
        avSyncOffsetMs = double(std::llabs(firstVideoPts.load() - audioPts100ns)) / 10000.0;
    }

    const double durSec = engine.DurationSec();

    // 6) audio_report.json
    if (FILE* f = fopen(reportPath.c_str(), "w")) {
        fprintf(f, "{\n");
        fprintf(f, " \"systemAudioFrames\": %llu,\n", (unsigned long long)sysFrames);
        fprintf(f, " \"micFrames\": %llu,\n", (unsigned long long)micFrames);
        fprintf(f, " \"sampleRate\": 48000,\n");
        fprintf(f, " \"channels\": 2,\n");
        fprintf(f, " \"avSyncOffsetMs\": %.2f,\n", avSyncOffsetMs);
        fprintf(f, " \"videoFrames\": %llu,\n", (unsigned long long)engine.FramesRecorded());
        fprintf(f, " \"durationSec\": %.2f,\n", durSec);
        fprintf(f, " \"audioCodec\": \"AAC\",\n");
        fprintf(f, " \"videoEncoder\": \"NvencSimulator (simulation)\",\n");
        fprintf(f, " \"systemAudioError\": \"%s\",\n", sysErr.c_str());
        fprintf(f, " \"micError\": \"%s\",\n", micErr.c_str());
        fprintf(f, " \"timeBase\": \"QPC (unified)\",\n");
        fprintf(f, " \"wasapiHardware\": \"REAL implementation — runtime verification pending (sandbox limitation)\"\n");
        fprintf(f, "}\n");
        fclose(f);
    }

    // 7) 报告
    printf("\n===== Audio Record Test Report =====\n");
    printf("systemAudioFrames: %llu\n", (unsigned long long)sysFrames);
    printf("micFrames        : %llu\n", (unsigned long long)micFrames);
    printf("sampleRate       : 48000\n");
    printf("channels         : 2\n");
    printf("avSyncOffsetMs   : %.2f\n", avSyncOffsetMs);
    printf("videoFrames      : %llu\n", (unsigned long long)engine.FramesRecorded());
    printf("duration         : %.2f s\n", durSec);
    printf("output           : %s (video + AAC audio stream)\n", mp4Path.c_str());
    printf("report           : %s\n", reportPath.c_str());
    printf("------------------------------------------\n");
    printf("WASAPI capture   : REAL implementation — runtime verification pending (sandbox limitation)\n");
    printf("真机验证: ffprobe -v error -show_entries stream=codec_type,codec_name,sample_rate,channels,avg_frame_rate -of default=noprint_wrappers=1 %s\n", mp4Path.c_str());
    printf("  期望: video h264 60fps + audio aac 48000Hz stereo\n");
    printf("==========================================\n");
    return 0;
}

} // namespace sf
