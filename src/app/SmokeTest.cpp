// Phase 8-A — Smoke Test 实现
// 全部为真实 Windows API 调用；任何一项失败即返回非零并如实写入报告。

#include "SmokeTest.h"

#include <Windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "graphics/D3D11Device.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#ifdef SF_HAVE_NVENC_HW
#include "NvEncoderHardware.h"
#endif

namespace sf {
namespace {

struct Check {
    std::string name;
    bool        pass;
    std::string info;
};

std::string OsVersion() {
    // 简化：RtlGetVersion 方式获取真实版本号
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return "unknown";
    const auto fn = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!fn) return "unknown";
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn(&vi) != 0) return "unknown";
    return std::to_string(vi.dwMajorVersion) + "." +
           std::to_string(vi.dwMinorVersion) + "." +
           std::to_string(vi.dwBuildNumber);
}

bool CheckD3D11(Check& c) {
    Microsoft::WRL::ComPtr<ID3D11Device> dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    c.pass = D3D11Device::Create(dev, ctx);
    c.info = c.pass ? "D3D11CreateDevice OK" : "D3D11CreateDevice 失败";
    return c.pass;
}

bool CheckDxgi(Check& c) {
    const GpuInfo g = D3D11Device::Detect();
    c.pass = !g.name.empty();
    c.info = c.pass
        ? g.name + " · 驱动 " + (g.driver.empty() ? "?" : g.driver) +
          " · VRAM " + std::to_string(g.vramMB / 1024) + " GB"
        : "未枚举到 GPU";
    return c.pass;
}

bool CheckFfmpeg(Check& c) {
    const unsigned avf = avformat_version();
    const unsigned avc = avcodec_version();
    const unsigned avu = avutil_version();
    c.pass = (avf != 0) && (avc != 0) && (avu != 0);
    if (c.pass) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "libavformat %u.%u.%u · libavcodec %u.%u.%u · libavutil %u.%u.%u",
                      (avf >> 16) & 0xFF, (avf >> 8) & 0xFF, avf & 0xFF,
                      (avc >> 16) & 0xFF, (avc >> 8) & 0xFF, avc & 0xFF,
                      (avu >> 16) & 0xFF, (avu >> 8) & 0xFF, avu & 0xFF);
        c.info = buf;
    } else {
        c.info = "FFmpeg 版本查询失败（链接缺失?）";
    }
    return c.pass;
}

bool CheckAudio(Check& c) {
    int render = 0, capture = 0;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                       CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
            Microsoft::WRL::ComPtr<IMMDeviceCollection> coll;
            if (SUCCEEDED(enumerator->EnumAudioEndpoints(
                    eRender, DEVICE_STATE_ACTIVE, &coll))) {
                UINT n = 0;
                coll->GetCount(&n);
                render = int(n);
            }
            if (SUCCEEDED(enumerator->EnumAudioEndpoints(
                    eCapture, DEVICE_STATE_ACTIVE, &coll))) {
                UINT n = 0;
                coll->GetCount(&n);
                capture = int(n);
            }
        }
        CoUninitialize();
    }
    c.pass = (render + capture) > 0;
    c.info = "渲染端点 " + std::to_string(render) + " · 捕获端点 " +
             std::to_string(capture) + (c.pass ? "" : "（无活动音频端点）");
    return c.pass;
}

bool CheckEncoder(Check& c) {
#ifdef SF_HAVE_NVENC_HW
    const NvHwCapabilities caps = NvEncoderHardware::DetectCapabilities();
    c.pass = caps.nvencAvailable && caps.h264Supported;
    c.info = caps.nvencAvailable
        ? "NVENC " + (caps.nvencVersion.empty() ? "?" : caps.nvencVersion) +
          " · H264 " + (caps.h264Supported ? "支持" : "不支持") +
          " · 最大 " + std::to_string(caps.maxWidth) + "x" +
          std::to_string(caps.maxHeight)
        : "NVENC 会话不可用（GPU: " +
          (caps.gpuName.empty() ? "无 NVIDIA GPU" : caps.gpuName) + "）";
#else
    c.pass = false;
    c.info = "SIMULATION build（无 NVENC SDK，SF_HAVE_NVENC_HW=0）— 硬件编码器未编译，此项预期失败";
#endif
    return c.pass;
}

} // namespace

int RunSmokeTest(const std::string& reportPath) {
    printf("===== ScreenForge Phase 8-A Smoke Test =====\n");
    printf("OS: %s\n", OsVersion().c_str());
    fflush(stdout);

    std::vector<Check> checks;
    checks.push_back({ "D3D11 Device", false, "" });
    checks.push_back({ "DXGI GPU 枚举", false, "" });
    checks.push_back({ "FFmpeg 初始化", false, "" });
    checks.push_back({ "WASAPI 音频枚举", false, "" });
    checks.push_back({ "编码器能力", false, "" });

    bool allPass = true;
    for (size_t i = 0; i < checks.size(); ++i) {
        switch (i) {
            case 0: CheckD3D11(checks[i]); break;
            case 1: CheckDxgi(checks[i]); break;
            case 2: CheckFfmpeg(checks[i]); break;
            case 3: CheckAudio(checks[i]); break;
            case 4: CheckEncoder(checks[i]); break;
        }
        printf("[%s] %-18s %s\n",
               checks[i].pass ? "PASS" : "FAIL",
               checks[i].name.c_str(),
               checks[i].info.c_str());
        if (!checks[i].pass) allPass = false;
    }

    // 写 smoke_report.json
    FILE* f = fopen(reportPath.c_str(), "w");
    if (!f) {
        printf("FAIL: 无法写入 %s\n", reportPath.c_str());
        return 1;
    }
    fprintf(f, "{\n");
    fprintf(f, " \"osVersion\": \"%s\",\n", OsVersion().c_str());
    fprintf(f, " \"allPass\": %s,\n", allPass ? "true" : "false");
    fprintf(f, " \"checks\": [\n");
    for (size_t i = 0; i < checks.size(); ++i) {
        fprintf(f, "  { \"name\": \"%s\", \"pass\": %s, \"info\": \"%s\" }%s\n",
                checks[i].name.c_str(),
                checks[i].pass ? "true" : "false",
                checks[i].info.c_str(),
                (i + 1 < checks.size()) ? "," : "");
    }
    fprintf(f, " ]\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("\n%s\n", allPass ? "SMOKE TEST: PASS" : "SMOKE TEST: FAIL");
    printf("Report: %s\n", reportPath.c_str());
    printf("==========================================\n");
    return allPass ? 0 : 1;
}

} // namespace sf
