#include "SyntheticFrameSource.h"

#include <Windows.h>

namespace sf {

SyntheticFrameSource::SyntheticFrameSource(uint32_t width, uint32_t height, uint32_t fps)
    : m_width(width), m_height(height), m_fps(fps) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    m_qpcFreq = f.QuadPart;
}

CaptureFrame SyntheticFrameSource::Generate(uint64_t frameId) {
    CaptureFrame f;
    f.texture = nullptr;                        // 模拟帧：无 GPU 纹理
    f.width   = m_width;
    f.height  = m_height;
    f.format  = DXGI_FORMAT_B8G8R8A8_UNORM;
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    f.captureQpc = q.QuadPart;                  // QPC 时间戳（真实时钟）
    f.index      = frameId + 1;
    f.ts100ns    = int64_t(frameId) * (10000000LL / m_fps);   // 名义时间
    return f;
}

} // namespace sf
