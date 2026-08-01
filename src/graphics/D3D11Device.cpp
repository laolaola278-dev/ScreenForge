#include "D3D11Device.h"

#include <Windows.h>

#include <dxgi1_2.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace sf {
namespace {

// 微软基本显示适配器（无 GPU 时的软件适配器）
constexpr UINT kVendorIdMicrosoft = 0x1414;
constexpr UINT kVendorIdNvidia    = 0x10DE;

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 1 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// 不引入 NVIDIA Video Codec SDK 头文件，仅声明所需符号
typedef int(__stdcall* NvEncGetMaxSupportedVersionFn)(unsigned int* version);
constexpr int kNvEncApiSuccess = 0;

} // namespace

bool D3D11Device::Create(ComPtr<ID3D11Device>& device,
                         ComPtr<ID3D11DeviceContext>& context) {
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;

    // BGRA_SUPPORT：供 Phase 1 WGC 捕获纹理互操作
    const HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &device, &got, &context);
    return SUCCEEDED(hr);
}

GpuInfo D3D11Device::Detect() {
    GpuInfo info;

    // 1) DXGI 枚举适配器，取第一个真实 GPU（跳过微软基本显示适配器）
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc))) continue;
            if (desc.VendorId == kVendorIdMicrosoft) continue;   // 软渲染适配器跳过

            info.name   = WideToUtf8(desc.Description);
            info.vramMB = desc.DedicatedVideoMemory / (1024 * 1024);
            info.nvidia = (desc.VendorId == kVendorIdNvidia);

            // DXGI_ADAPTER_DESC1/2 均无 DriverVersion 成员（新版 Windows SDK 已移除），
            // 改用 CheckInterfaceSupport 查询 UMD 驱动版本
            LARGE_INTEGER umdVersion{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(
                    __uuidof(IDXGIDevice), &umdVersion))) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u",
                              (umdVersion.HighPart >> 16) & 0xFFFF,
                              umdVersion.HighPart & 0xFFFF,
                              umdVersion.LowPart);
                info.driver = buf;
            }
            break;   // 只取主 GPU
        }
    }

    // 2) D3D11 设备创建测试（Phase 0 验收项：D3D11 Available）
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    info.d3d11 = Create(device, context);

    // 3) NVENC SDK 探测：只 LoadLibrary 取版本号，不初始化编码器（Phase 3 才使用）
    HMODULE nv = LoadLibraryA("nvEncodeAPI64.dll");
    if (nv) {
        auto getVer = reinterpret_cast<NvEncGetMaxSupportedVersionFn>(
            GetProcAddress(nv, "NvEncodeAPIGetMaxSupportedVersion"));
        if (getVer) {
            unsigned int ver = 0;
            if (getVer(&ver) == kNvEncApiSuccess) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u.%u", ver / 1000, (ver % 1000) / 10);
                info.nvencVersion = buf;
                info.nvenc = true;
            }
        }
    }
    return info;
}

} // namespace sf
