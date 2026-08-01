#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace sf {

// 一帧捕获结果（Phase 2：扩展字段）
// 生命周期所有权：
//   Capture 获得引用（WGC 回调构造）→ Queue 持有引用（Push 移入）
//   → Consumer 释放引用（Pop 移出，析构/消费）
// 纹理始终驻留 GPU，禁止回读 CPU。
struct CaptureFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;   // BGRA 纹理（GPU）
    uint32_t    width      = 0;                        // 纹理宽度
    uint32_t    height     = 0;                        // 纹理高度
    DXGI_FORMAT format     = DXGI_FORMAT_B8G8R8A8_UNORM;
    int64_t     captureQpc = 0;                        // 捕获时刻（QPC ticks）
    uint64_t    index      = 0;                        // 帧序号（从 1 开始）
    int64_t     ts100ns    = 0;                        // SystemRelativeTime（100ns）
};

} // namespace sf
