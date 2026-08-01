// Phase 3-B：NVENC 真实硬件验证运行器
// 目的：在真实 Windows + NVIDIA 机器上证明 NVENC 模块可运行。
// 输入为确定性测试图案（CPU 生成 → GPU 动态纹理），验证编码链路本身；
// 不涉及 WGC 捕获（Phase 1 已验证），因此可在无交互会话下运行。

#include "VerifyRun.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Logger.h"
#include "NvEncoder.h"
#include "graphics/D3D11Device.h"
#include "graphics/Frame.h"

namespace sf {
namespace {

int64_t QpcNow() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

// 生成确定性测试图案：渐变 + 循环色相 + 移动白条 + 帧号条
void FillPattern(std::vector<uint8_t>& px, uint32_t w, uint32_t h, uint64_t frameIdx) {
    const uint32_t stride = w * 4;
    const uint32_t barW = std::max<uint32_t>(8, w / 80);
    const int32_t barX = int32_t((frameIdx * 12) % (w + barW * 2)) - int32_t(barW);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = px.data() + size_t(y) * stride;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = uint8_t((x * 255) / w);
            uint8_t g = uint8_t((y * 255) / h);
            uint8_t b = uint8_t((x + y + frameIdx * 4) & 0xFF);
            // 移动白条
            if (int32_t(x) >= barX && int32_t(x) < barX + int32_t(barW)) {
                r = g = b = 255;
            }
            // 底部帧号条
            if (y > h - 24) {
                r = uint8_t((frameIdx >> 16) & 0xFF);
                g = uint8_t((frameIdx >> 8) & 0xFF);
                b = uint8_t(frameIdx & 0xFF);
            }
            row[x * 4 + 0] = b;   // BGRA
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = 255;
        }
    }
}

} // namespace

int RunHardwareVerify(int argc, char* argv[]) {
    uint32_t w = 1920, h = 1080, fps = 60, bitrate = 12000;
    uint64_t frames = 10000;
    std::string out = "test.h264";

    for (int i = 2; i < argc; ++i) {
        const auto arg = [&](const char* name, auto& dst) {
            if (std::strcmp(argv[i], name) == 0 && i + 1 < argc) {
                dst = std::stoull(argv[++i]);
                return true;
            }
            return false;
        };
        if (arg("--width", w)) continue;
        if (arg("--height", h)) continue;
        if (arg("--fps", fps)) continue;
        if (arg("--bitrate", bitrate)) continue;
        if (arg("--frames", frames)) continue;
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out = argv[++i];
            continue;
        }
    }

    printf("===== ScreenForge Phase 3-B NVENC 硬件验证 =====\n");
    printf("配置: %ux%u @ %u fps, CBR %u kbps, %llu 帧 → %s\n",
           w, h, fps, bitrate, (unsigned long long)frames, out.c_str());
    LOG_INFO("verify: start " + std::to_string(w) + "x" + std::to_string(h) +
             " " + std::to_string(frames) + " frames");

    // 1) D3D11 设备
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    if (!D3D11Device::Create(device, ctx)) {
        printf("✕ D3D11 设备创建失败\n");
        return 1;
    }
    LOG_INFO("verify: d3d11 ok");

    // 2) 编码器初始化（真实调用 nvEncodeAPI64.dll）
    NvEncoder enc;
    if (!enc.Initialize(device.Get(), w, h, fps, bitrate, out)) {
        printf("✕ NVENC 初始化失败: %s\n", enc.LastError().c_str());
        LOG_ERROR("verify: nvenc init failed - " + enc.LastError());
        return 1;
    }
    printf("✓ NVENC 会话已建立 (D3D11, H264 %ux%u @ %u)\n", w, h, fps);
    LOG_INFO("verify: nvenc session ok");

    // 3) 测试图案纹理（CPU→GPU 写入，DYNAMIC）
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &tex))) {
        printf("✕ 测试纹理创建失败\n");
        return 1;
    }

    std::vector<uint8_t> px(size_t(w) * h * 4);

    // 4) 60fps 定速推帧（QPC pacing）
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    const double freqD = double(freq.QuadPart);
    const int64_t intervalQpc = int64_t(freqD / double(fps));
    int64_t deadline = QpcNow();

    const int64_t t0 = QpcNow();
    for (uint64_t i = 0; i < frames && enc.IsRunning(); ++i) {
        FillPattern(px, w, h, i);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const uint32_t srcStride = w * 4;
            const uint8_t* src = px.data();
            uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(dst + size_t(y) * mapped.RowPitch, src + size_t(y) * srcStride, srcStride);
            }
            ctx->Unmap(tex.Get(), 0);
        }

        CaptureFrame f;
        f.texture    = tex;
        f.width      = w;
        f.height     = h;
        f.format     = DXGI_FORMAT_B8G8R8A8_UNORM;
        f.captureQpc = QpcNow();      // QPC 时间戳 → 编码器换算 100ns PTS
        f.index      = i + 1;
        f.ts100ns    = 0;
        enc.PushFrame(std::move(f));

        // 定速等待（睡到剩 1ms + 自旋）
        deadline += intervalQpc;
        int64_t now = QpcNow();
        const double remainMs = double(deadline - now) * 1000.0 / freqD;
        if (remainMs > 1.5) Sleep(DWORD(remainMs - 1.0));
        while (QpcNow() < deadline) { /* 自旋精修 */ }

        if ((i % 1000) == 0) {
            printf("  [%5llu/%llu] submitted=%llu encoded=%llu failed=%llu\n",
                   (unsigned long long)i, (unsigned long long)frames,
                   (unsigned long long)enc.Submitted(),
                   (unsigned long long)enc.Encoded(),
                   (unsigned long long)enc.FailedFrames());
        }
    }
    const int64_t t1 = QpcNow();

    // 5) 等待编码器排空（上限 20 秒），随后 Shutdown flush
    for (int k = 0; k < 400; ++k) {
        if (enc.Encoded() >= enc.Submitted()) break;
        Sleep(50);
    }
    enc.Shutdown();

    // 6) 报告
    const double durSec = double(t1 - t0) / freqD;
    const uint64_t sub = enc.Submitted(), encd = enc.Encoded(),
                   fail = enc.FailedFrames(), bytes = enc.BitstreamBytes();
    const double lat = enc.AvgLatencyMs();
    const uint32_t busy = enc.EncoderBusyPct();

    printf("\n===== NVENC 硬件验证报告 =====\n");
    printf("分辨率        : %ux%u @ %ufps\n", w, h, fps);
    printf("测试时长      : %.1f s（目标 %llu 帧）\n", durSec, (unsigned long long)frames);
    printf("submittedFrames: %llu\n", (unsigned long long)sub);
    printf("encodedFrames : %llu\n", (unsigned long long)encd);
    printf("failedFrames  : %llu\n", (unsigned long long)fail);
    printf("encodeLatency : %.2f ms (avg)\n", lat);
    printf("GPU Video Encode 占用估算: %u%%（请在任务管理器核对实际占用）\n", busy);
    printf("码流          : %s (%.2f MB)\n", out.c_str(), double(bytes) / 1048576.0);
    printf("色彩转换      : BT.709 有限范围\n");
    printf("PTS           : QPC 时间戳 (100ns)\n");
    printf("\n下一步: ffprobe %s\n", out.c_str());
    printf("  ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,avg_frame_rate -of default=noprint_wrappers=1 %s\n", out.c_str());
    printf("=================================\n");

    // 写验证报告文件
    if (FILE* rp = fopen("verify_report.txt", "w")) {
        fprintf(rp, "ScreenForge Phase 3-B NVENC 硬件验证报告\n");
        fprintf(rp, "分辨率: %ux%u @ %ufps\n", w, h, fps);
        fprintf(rp, "测试时长: %.1f s\n", durSec);
        fprintf(rp, "submittedFrames: %llu\n", (unsigned long long)sub);
        fprintf(rp, "encodedFrames: %llu\n", (unsigned long long)encd);
        fprintf(rp, "failedFrames: %llu\n", (unsigned long long)fail);
        fprintf(rp, "encodeLatency: %.2f ms\n", lat);
        fprintf(rp, "gpuVideoEncodeBusyEstimatePct: %u\n", busy);
        fprintf(rp, "bitstreamBytes: %llu\n", (unsigned long long)bytes);
        fclose(rp);
    }

    LOG_INFO("verify: done submitted=" + std::to_string(sub) +
             " encoded=" + std::to_string(encd) +
             " failed=" + std::to_string(fail));
    return encd > 0 && fail == 0 ? 0 : 1;
}

} // namespace sf
