#include "GpuDetect.h"

#include <Windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace sf {
namespace {

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 1 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// 不引入 NVIDIA Video Codec SDK 头文件，仅声明所需符号
typedef int (__stdcall* NvEncGetMaxSupportedVersionFn)(unsigned int* version);
#define NVENCAPI_SUCCESS 0

} // namespace

GpuInfo GpuDetect::Run() {
    GpuInfo info;

    // 1) DXGI 枚举适配器，锁定 NVIDIA
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == 0x10DE) {
                info.nvidia = true;
                info.vendor = WideToUtf8(desc.Description);
                info.vramMB = desc.DedicatedVideoMemory / (1024 * 1024);

                // DXGI_ADAPTER_DESC1/2 均无 DriverVersion 成员（新版 Windows SDK 已移除），
                // 改用 CheckInterfaceSupport 查询 UMD 驱动版本
                LARGE_INTEGER umdVersion{};
                if (SUCCEEDED(adapter->CheckInterfaceSupport(
                        __uuidof(IDXGIDevice), &umdVersion))) {
                    char dbuf[64];
                    std::snprintf(dbuf, sizeof(dbuf), "%u.%u.%u",
                                  (umdVersion.HighPart >> 16) & 0xFFFF,
                                  umdVersion.HighPart & 0xFFFF,
                                  umdVersion.LowPart);
                    info.driver = dbuf;
                }
                break;
            }
        }
    }

    // 2) D3D11 设备测试（BGRA_SUPPORT 供 Phase 1 WGC 互操作）
    ComPtr<ID3D11Device> device;
    info.d3d11 = SUCCEEDED(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &device, nullptr, nullptr));

    // 3) NVENC SDK 探测：只 LoadLibrary 取版本号，不初始化编码器
    HMODULE nv = LoadLibraryA("nvEncodeAPI64.dll");
    if (nv) {
        auto getVer = reinterpret_cast<NvEncGetMaxSupportedVersionFn>(
            GetProcAddress(nv, "NvEncodeAPIGetMaxSupportedVersion"));
        if (getVer) {
            unsigned int ver = 0;
            if (getVer(&ver) == NVENCAPI_SUCCESS) {
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
