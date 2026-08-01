#pragma once

// SANDBOX SIMULATOR — 合成捕获源
// 生成无 GPU 纹理的 CaptureFrame（texture=nullptr），仅元数据 + QPC 时间戳
// 明确标记：SIMULATOR · NOT real WGC

#include "capture/WgcCaptureSource.h"

namespace sf {

class SyntheticCapture : public ICaptureSource {
public:
    SyntheticCapture(uint32_t width, uint32_t height, uint32_t fps)
        : m_w(width), m_h(height), m_fps(fps) {}

    bool Start() override { m_running = true; m_idx = 0; return true; }
    void Stop() override { m_running = false; }

    // 非阻塞：每次调用产出一帧（由 recorder 按 60fps pacing 驱动）
    bool GetFrame(CaptureFrame& out) override {
        if (!m_running) return false;
        out.texture = nullptr;                     // 合成帧：无 GPU 纹理
        out.width   = m_w;
        out.height  = m_h;
        out.format  = DXGI_FORMAT_B8G8R8A8_UNORM;
        LARGE_INTEGER q{};
        QueryPerformanceCounter(&q);
        out.captureQpc = q.QuadPart;               // QPC 统一时钟
        out.index      = ++m_idx;
        out.ts100ns    = int64_t(m_idx - 1) * (10000000LL / m_fps);
        return true;
    }

private:
    uint32_t m_w, m_h, m_fps;
    bool m_running = false;
    uint64_t m_idx = 0;
};

} // namespace sf
