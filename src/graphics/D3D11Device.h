#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace sf {

// GPU 检测结果（Phase 0 验收输出）
struct GpuInfo {
    bool        d3d11  = false;   // D3D11 设备是否创建成功
    bool        nvidia = false;   // 主适配器是否为 NVIDIA
    bool        nvenc  = false;   // 是否探测到 NVENC SDK（仅探测，不初始化）
    std::string name;             // 显卡型号，如 "NVIDIA GeForce RTX 4070"
    std::string driver;           // 驱动版本（UMD 版本，经 CheckInterfaceSupport 获取）
    std::string nvencVersion;     // NVENC SDK 版本，如 "12.1"
    uint64_t    vramMB = 0;       // 显存大小
};

class D3D11Device {
public:
    // 创建 D3D11 硬件设备（BGRA_SUPPORT 供后续 WGC 互操作）
    static bool Create(Microsoft::WRL::ComPtr<ID3D11Device>& device,
                       Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context);

    // 完整检测：DXGI 枚举适配器 + D3D11 设备测试 + NVENC SDK 探测
    static GpuInfo Detect();
};

} // namespace sf
