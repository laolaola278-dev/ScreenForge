// Phase 5-B — 真实录制稳定性测试
// 真实链路：WGC → FramePipeline → NvEncoderHardware → Mp4Muxer
// 无任何模拟；无 NVIDIA GPU 直接失败退出。所有统计来自真实运行采样。

#include "StabilityTest.h"

#include <Windows.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <psapi.h>

#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

#include "FramePipeline.h"
#include "Mp4Muxer.h"
#include "NvCapabilities.h"
#include "WgcCaptureSource.h"
#include "graphics/D3D11Device.h"
#include "graphics/Frame.h"
#ifdef SF_HAVE_NVENC_HW
#include "NvEncoderHardware.h"
#endif

#pragma comment(lib, "psapi.lib")

namespace sf {
namespace {

constexpr uint32_t kTickMs      = 1000;    // 采样周期
constexpr uint32_t kPrintEvery  = 10;      // 每 10 秒打印一行
constexpr uint64_t kMinDiskFree = 64ULL * 1024 * 1024;   // 磁盘剩余 <64MB → disk full

int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}
double QpcFreq() {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return double(f.QuadPart);
}

struct SysStats {
    double   cpuPct = 0.0;      // 进程 CPU 占用 %
    uint64_t wsMB = 0;          // working set MB
    uint64_t gpuMemMB = 0;      // 专用 GPU 显存当前占用 MB
    bool     gpuMemOk = false;
};

void SampleSysStats(ID3D11Device* dev, SysStats& s,
                    int64_t& lastKernel, int64_t& lastUser, int64_t& lastWall) {
    FILETIME create, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) {
        const int64_t kk = (int64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
        const int64_t uu = (int64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
        const int64_t now = QpcNow();
        if (lastWall != 0) {
            const double wallSec = double(now - lastWall) / QpcFreq();
            const double cpuSec  = double((kk - lastKernel) + (uu - lastUser)) / 1e7;
            if (wallSec > 0) s.cpuPct = cpuSec / wallSec * 100.0;
        }
        lastKernel = kk; lastUser = uu; lastWall = now;
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        s.wsMB = pmc.WorkingSetSize / (1024 * 1024);
    }

    // 专用显存占用（DXGI 1.4 QueryVideoMemoryInfo）
    if (dev) {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDev;
        if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&dxgiDev)))) {
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgiDev->GetAdapter(&adapter))) {
                Microsoft::WRL::ComPtr<IDXGIAdapter3> a3;
                if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&a3)))) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO mi{};
                    if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi))) {
                        s.gpuMemMB = mi.CurrentUsage / (1024 * 1024);
                        s.gpuMemOk = true;
                    }
                }
            }
        }
    }
}

bool WriteReport(const char* path, double targetHours, double durationSec,
                 uint64_t frames, double avgFps, double maxLatencyMs,
                 double memGrowthMB, double gpuMemGrowthMB,
                 uint64_t failed, uint64_t dropped,
                 const NvHwCapabilities& caps,
                 const std::vector<std::string>& errors) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n");
    fprintf(f, " \"mode\": \"REAL_HARDWARE\",\n");
    fprintf(f, " \"targetHours\": %.2f,\n", targetHours);
    fprintf(f, " \"durationSec\": %.1f,\n", durationSec);
    fprintf(f, " \"frames\": %llu,\n", (unsigned long long)frames);
    fprintf(f, " \"avgFps\": %.2f,\n", avgFps);
    fprintf(f, " \"maxLatencyMs\": %.2f,\n", maxLatencyMs);
    fprintf(f, " \"memoryGrowthMB\": %.1f,\n", memGrowthMB);
    fprintf(f, " \"gpuMemoryGrowthMB\": %.1f,\n", gpuMemGrowthMB);
    fprintf(f, " \"failedFrames\": %llu,\n", (unsigned long long)failed);
    fprintf(f, " \"droppedFrames\": %llu,\n", (unsigned long long)dropped);
    fprintf(f, " \"gpuName\": \"%s\",\n", caps.gpuName.c_str());
    fprintf(f, " \"nvencVersion\": \"%s\",\n", caps.nvencVersion.c_str());
    fprintf(f, " \"encoder\": \"NvEncoderHardware\",\n");
    fprintf(f, " \"resolution\": \"1920x1080\",\n");
    fprintf(f, " \"errors\": [");
    for (size_t i = 0; i < errors.size(); ++i) {
        fprintf(f, "%s\"%s\"", i ? ", " : "", errors[i].c_str());
    }
    fprintf(f, "]\n");
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

} // namespace

int RunStabilityTest(double hours, uint32_t fps, uint32_t bitrateKbps,
                     const std::string& mp4Path,
                     const std::string& reportPath) {
    const uint32_t w = 1920, h = 1080;
    printf("===== ScreenForge Phase 5-B Stability Test =====\n");
    printf("Mode   : REAL HARDWARE (WGC → Pipeline → NVENC → MP4)\n");
    printf("Target : %.1f hours @ %ux%u %u fps, CBR %u kbps\n",
           hours, w, h, fps, bitrateKbps);
    fflush(stdout);

#ifdef SF_HAVE_NVENC_HW
    // 1) 硬件检测：无 NVIDIA / NVENC 不可用 → 失败退出（不回退）
    const NvHwCapabilities caps = NvEncoderHardware::DetectCapabilities();
    printf("GPU    : %s\n", caps.gpuName.empty() ? "(未检测到)" : caps.gpuName.c_str());
    printf("NVENC  : %s\n", caps.nvencVersion.empty() ? "(未检测到)" : caps.nvencVersion.c_str());
    if (!caps.nvencAvailable || !caps.h264Supported) {
        printf("FAIL: 稳定性测试需要真实 NVIDIA NVENC 硬件（禁止模拟器回退）\n");
        return 1;
    }

    // 2) D3D11 设备
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    if (!D3D11Device::Create(device, ctx)) {
        printf("FAIL: D3D11 设备创建失败\n");
        return 1;
    }

    // 3) 真实链路：WGC + FramePipeline + NvEncoderHardware + Mp4Muxer
    NvEncoderHardware enc;
    Mp4Muxer mux;
    bool muxOk = false;
    enc.SetPacketSink([&](const EncodedPacket& p) { if (muxOk) mux.WritePacket(p); });

    if (!enc.Initialize(device.Get(), w, h, fps, bitrateKbps, "")) {
        printf("FAIL: NvEncoderHardware 初始化 - %s\n", enc.LastError().c_str());
        return 1;
    }
    MuxConfig mc;
    mc.outputPath = mp4Path;
    mc.width = w; mc.height = h; mc.fps = fps;
    mc.extradata = enc.SequenceParams();
    if (!mux.Initialize(mc)) {
        printf("FAIL: Mp4Muxer 初始化 - %s\n", mux.LastError().c_str());
        enc.Shutdown();
        return 1;
    }
    muxOk = true;

    WgcCaptureSource wgc(device);
    FramePipeline pipeline;
    auto consumer = [&](CaptureFrame&& f) { enc.PushFrame(std::move(f)); };
    if (!pipeline.Start(wgc, consumer)) {
        printf("FAIL: FramePipeline/WGC 启动 - %s\n", wgc.LastError().c_str());
        mux.Abort();
        enc.Shutdown();
        return 1;
    }
    printf("链路已启动: WGC %ux%u → Pipeline → NVENC → MP4\n\n", w, h);

    // 4) 主循环：1s 采样 / 10s 打印 / 异常检测
    SysStats s0{}, s{};
    int64_t lastKernel = 0, lastUser = 0, lastWall = 0;
    SampleSysStats(device.Get(), s0, lastKernel, lastUser, lastWall);
    const uint64_t ws0 = s0.wsMB, gpu0 = s0.gpuMemMB;

    const int64_t t0 = QpcNow();
    const int64_t endAt = t0 + int64_t(QpcFreq() * hours * 3600.0);
    uint64_t elapsedSec = 0;
    uint64_t lastPrintedEnc = 0;
    double maxLatency = 0.0;
    uint64_t lastFailed = 0;
    std::vector<std::string> errors;
    bool abort = false;

    while (QpcNow() < endAt && !abort) {
        Sleep(kTickMs);
        elapsedSec = uint64_t(double(QpcNow() - t0) / QpcFreq());

        SampleSysStats(device.Get(), s, lastKernel, lastUser, lastWall);

        const uint64_t encFrames = enc.Encoded();
        const uint64_t dropped = pipeline.DroppedFrames() + enc.DroppedFrames();
        const double lat = enc.AvgLatencyMs();
        if (lat > maxLatency) maxLatency = lat;

        // 异常检测
        const HRESULT removed = device->GetDeviceRemovedReason();
        if (removed != S_OK) {
            char b[64];
            std::snprintf(b, sizeof(b), "GPU device removed (0x%08X)", unsigned(removed));
            errors.push_back(b);
            abort = true;
        }
        if (!enc.IsRunning()) {
            errors.push_back("encoder failure (thread stopped)");
            abort = true;
        }
        const uint64_t failed = enc.FailedFrames();
        if (failed > lastFailed) {
            errors.push_back("encoder failure (failedFrames increased)");
            abort = true;
        }
        lastFailed = failed;
        ULARGE_INTEGER freeAvail{};
        if (GetDiskFreeSpaceExA(mp4Path.c_str(), &freeAvail, nullptr, nullptr) &&
            freeAvail.QuadPart < kMinDiskFree) {
            errors.push_back("disk full");
            abort = true;
        }
        if (elapsedSec > 10 && pipeline.InputFps() == 0) {
            if (errors.empty() || errors.back() != "capture lost") {
                errors.push_back("capture lost (input fps = 0)");
            }
        }
        if (!wgc.IsRunning()) {
            if (errors.empty() || errors.back() != "capture lost (WGC stopped)") {
                errors.push_back("capture lost (WGC stopped)");
            }
        }

        // 每 10 秒打印
        if (elapsedSec % kPrintEvery == 0 && elapsedSec != lastPrintedEnc) {
            lastPrintedEnc = elapsedSec;
            printf("[%5llu s] fps=%2u  enc=%7llu  drop=%4llu  gpuMem=%6llu MB  "
                   "cpu=%4.1f%%  ws=%6llu MB  lat=%4.1f ms  q=%u\n",
                   (unsigned long long)elapsedSec,
                   pipeline.InputFps(),
                   (unsigned long long)encFrames,
                   (unsigned long long)dropped,
                   (unsigned long long)s.gpuMemMB,
                   s.cpuPct,
                   (unsigned long long)s.wsMB,
                   lat,
                   pipeline.QueueDepth());
            fflush(stdout);
        }
    }

    // 5) 停止链路
    pipeline.Stop();
    enc.Shutdown();
    if (mux.IsOpen()) mux.Finalize();
    wgc.Stop();

    const double durationSec = double(QpcNow() - t0) / QpcFreq();
    const uint64_t frames = enc.Encoded();
    const double avgFps = durationSec > 0 ? double(frames) / durationSec : 0.0;

    SysStats sEnd{};
    int64_t k2 = 0, u2 = 0, w2 = 0;
    SampleSysStats(device.Get(), sEnd, k2, u2, w2);
    const double memGrowthMB = double(sEnd.wsMB) - double(ws0);
    const double gpuGrowthMB = (sEnd.gpuMemOk && s0.gpuMemOk)
        ? double(sEnd.gpuMemMB) - double(gpu0) : 0.0;

    WriteReport(reportPath.c_str(), hours, durationSec, frames, avgFps, maxLatency,
                memGrowthMB, gpuGrowthMB, enc.FailedFrames(),
                pipeline.DroppedFrames() + enc.DroppedFrames(), caps, errors);

    // 6) 报告
    printf("\n===== Stability Test Report =====\n");
    printf("duration        : %.1f s (target %.1f h)\n", durationSec, hours);
    printf("frames          : %llu\n", (unsigned long long)frames);
    printf("avgFps          : %.2f\n", avgFps);
    printf("maxLatency      : %.2f ms\n", maxLatency);
    printf("memoryGrowth    : %.1f MB (验收 <100MB)\n", memGrowthMB);
    printf("gpuMemGrowth    : %.1f MB\n", gpuGrowthMB);
    printf("failedFrames    : %llu (验收 =0)\n", (unsigned long long)enc.FailedFrames());
    printf("droppedFrames   : %llu\n",
           (unsigned long long)(pipeline.DroppedFrames() + enc.DroppedFrames()));
    printf("errors          : %zu\n", errors.size());
    for (const auto& e : errors) printf("  - %s\n", e.c_str());
    printf("report          : %s\n", reportPath.c_str());
    printf("output          : %s\n", mp4Path.c_str());
    printf("==========================================\n");
    return errors.empty() ? 0 : 3;
#else
    printf("FAIL: 本构建未包含 NVENC 硬件编码器（缺少 NVENC SDK，SF_HAVE_NVENC_HW=0）。\n");
    printf("      稳定性测试必须真实硬件链路（禁止模拟器回退）。\n");
    printf("      安装 NVIDIA Video Codec SDK 并设置 NVENC_SDK_ROOT 后重新 cmake configure。\n");
    return 1;
#endif
}

} // namespace sf
