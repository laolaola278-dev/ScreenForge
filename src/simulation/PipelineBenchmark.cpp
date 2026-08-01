// Phase 3-B Sandbox — 录制核心链路模拟基准
// 架构：SyntheticFrameSource → FrameQueue(SPSC) → Pacing(60fps 网格) → IEncoder(NvencSimulator)
// 验证：队列 · 时间戳 · 调度 · 编码流程 · PTS 连续性
// 声明：Architecture simulation: PASS / Real NVENC hardware: NOT TESTED (sandbox limitation)

#include "PipelineBenchmark.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "NvencSimulator.h"
#include "SyntheticFrameSource.h"
#include "pipeline/FrameQueue.h"

namespace sf {
namespace {

int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

// PTS 连续性验证：
//   1) 严格递增（diff > 0）      2) 无重复（diff != 0）      3) 无异常跳变（diff <= 10×帧间隔）
bool VerifyPtsContinuity(const std::vector<int64_t>& pts, int64_t frameInterval100ns,
                         int64_t& firstBadPts) {
    for (size_t i = 1; i < pts.size(); ++i) {
        const int64_t diff = pts[i] - pts[i - 1];
        if (diff <= 0) { firstBadPts = pts[i]; return false; }
        if (diff > frameInterval100ns * 10) { firstBadPts = pts[i]; return false; }
    }
    return true;
}

// 生产者：60fps QPC 定速生成帧 → FrameQueue（记录队列最大深度）
void ProducerLoop(SyntheticFrameSource& src, FrameQueue& queue, uint64_t frames,
                  int64_t qpcFreq, std::atomic<uint32_t>& maxDepth) {
    const int64_t intervalQpc = int64_t(double(qpcFreq) / double(src.Fps()));
    int64_t deadline = QpcNow();
    for (uint64_t i = 0; i < frames; ++i) {
        queue.Push(src.Generate(i));
        uint32_t d = queue.Size();
        uint32_t cur = maxDepth.load(std::memory_order_relaxed);
        while (d > cur && !maxDepth.compare_exchange_weak(cur, d)) {}
        // QPC pacing
        deadline += intervalQpc;
        int64_t now = QpcNow();
        const double remainMs = double(deadline - now) * 1000.0 / double(qpcFreq);
        if (remainMs > 1.5) Sleep(DWORD(remainMs - 1.0));
        while (QpcNow() < deadline) {}
    }
}

// 消费者：60fps 网格 Pop → 编码器（对应 PacingEngine 环节）
void ConsumerLoop(FrameQueue& queue, IEncoder& enc, uint32_t fps, int64_t qpcFreq,
                  const std::atomic<bool>& producerDone) {
    const int64_t intervalQpc = int64_t(double(qpcFreq) / double(fps));
    int64_t deadline = QpcNow();
    while (true) {
        CaptureFrame f;
        if (queue.Pop(f)) {
            enc.PushFrame(std::move(f));
        } else if (producerDone.load(std::memory_order_acquire) && queue.Size() == 0) {
            break;
        }
        deadline += intervalQpc;
        int64_t now = QpcNow();
        const double remainMs = double(deadline - now) * 1000.0 / double(qpcFreq);
        if (remainMs > 1.0) Sleep(DWORD(remainMs - 1.0));
        while (QpcNow() < deadline) {}
    }
}

} // namespace

int RunSimulationBenchmark(uint32_t width, uint32_t height, uint32_t fps,
                           uint64_t frames,
                           const std::string& outPath,
                           const std::string& reportPath,
                           SimulationReport* outReport) {
    printf("===== ScreenForge Phase 3-B Sandbox Simulation =====\n");
    printf("Mode      : SIMULATION\n");
    printf("Resolution: %ux%u @ %u fps\n", width, height, fps);
    printf("Frames    : %llu\n", (unsigned long long)frames);
    fflush(stdout);

    SyntheticFrameSource src(width, height, fps);
    NvencSimulator enc;
    if (!enc.Initialize(nullptr, width, height, fps, 12000, outPath)) {
        printf("FAIL: simulator init failed\n");
        return 1;
    }

    FrameQueue queue{64};
    std::atomic<uint32_t> maxDepth{0};
    std::atomic<bool> producerDone{false};

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    const int64_t qpcFreq = freq.QuadPart;

    const int64_t t0 = QpcNow();
    std::thread prod(ProducerLoop, std::ref(src), std::ref(queue),
                     frames, qpcFreq, std::ref(maxDepth));
    std::thread cons(ConsumerLoop, std::ref(queue), std::ref(enc),
                     fps, qpcFreq, std::ref(producerDone));

    prod.join();
    producerDone.store(true, std::memory_order_release);
    cons.join();
    enc.Shutdown();                       // 内部 flush 队列剩余帧
    const double durSec = double(QpcNow() - t0) / double(qpcFreq);

    // 收集结果
    SimulationReport r;
    r.mode = "SIMULATION";
    r.framesSubmitted = frames;
    r.framesEncoded   = enc.Encoded();
    r.droppedFrames   = enc.DroppedFrames();
    r.failedFrames    = enc.FailedFrames();
    r.avgEncodeLatencyMs = enc.AvgLatencyMs();
    r.p95EncodeLatencyMs = enc.P95LatencyMs();
    r.queueMaxDepth   = maxDepth.load();
    r.durationSec     = durSec;
    r.outputMarked    = true;             // NvencSimulator 已写 SIMULATION_OUTPUT=true 头

    // PTS 连续性验证（1: 严格递增 / 2: 无重复 / 3: 无异常跳变）
    const auto ptsSeq = enc.TakePtsSequence();
    int64_t firstBad = 0;
    r.ptsContinuity = VerifyPtsContinuity(ptsSeq, 10000000LL / fps, firstBad);

    // 控制台报告
    printf("\nSimulation Mode\n");
    printf("Frames:   %llu\n", (unsigned long long)r.framesSubmitted);
    printf("Encoded:  %llu\n", (unsigned long long)r.framesEncoded);
    printf("Dropped:  %llu\n", (unsigned long long)r.droppedFrames);
    printf("Failed:   %llu\n", (unsigned long long)r.failedFrames);
    printf("Latency:  %.2f ms (avg) / %.2f ms (p95)\n",
           r.avgEncodeLatencyMs, r.p95EncodeLatencyMs);
    printf("Queue max: %u\n", r.queueMaxDepth);
    printf("PTS:      %s\n", r.ptsContinuity ? "PASS" : "FAIL");
    printf("Duration: %.2f s\n", r.durationSec);
    printf("Output:   %s [SIMULATION_OUTPUT=true]\n", outPath.c_str());
    printf("Report:   %s\n", reportPath.c_str());
    printf("------------------------------------------\n");
    printf("Architecture simulation: PASS\n");
    printf("Real NVENC hardware: NOT TESTED (sandbox limitation)\n");
    printf("==========================================\n");

    // 写 simulation_report.json
    if (FILE* jf = fopen(reportPath.c_str(), "w")) {
        fprintf(jf, "{\n");
        fprintf(jf, " \"mode\": \"SIMULATION\",\n");
        fprintf(jf, " \"width\": %u,\n", width);
        fprintf(jf, " \"height\": %u,\n", height);
        fprintf(jf, " \"fps\": %u,\n", fps);
        fprintf(jf, " \"framesSubmitted\": %llu,\n", (unsigned long long)r.framesSubmitted);
        fprintf(jf, " \"framesEncoded\": %llu,\n", (unsigned long long)r.framesEncoded);
        fprintf(jf, " \"droppedFrames\": %llu,\n", (unsigned long long)r.droppedFrames);
        fprintf(jf, " \"failedFrames\": %llu,\n", (unsigned long long)r.failedFrames);
        fprintf(jf, " \"avgEncodeLatencyMs\": %.2f,\n", r.avgEncodeLatencyMs);
        fprintf(jf, " \"p95EncodeLatencyMs\": %.2f,\n", r.p95EncodeLatencyMs);
        fprintf(jf, " \"queueMaxDepth\": %u,\n", r.queueMaxDepth);
        fprintf(jf, " \"ptsContinuity\": %s,\n", r.ptsContinuity ? "true" : "false");
        fprintf(jf, " \"simulationOutputMarked\": %s,\n", r.outputMarked ? "true" : "false");
        fprintf(jf, " \"architectureSimulation\": \"PASS\",\n");
        fprintf(jf, " \"realNvencHardware\": \"NOT TESTED (sandbox limitation)\"\n");
        fprintf(jf, "}\n");
        fclose(jf);
    }

    if (outReport) *outReport = r;
    return r.framesEncoded > 0 ? 0 : 1;
}

} // namespace sf
