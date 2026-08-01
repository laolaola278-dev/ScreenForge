#pragma once

#include <cstdint>

#include "graphics/Frame.h"

namespace sf {

// 合成帧源（Phase 3-B Sandbox）：生成确定性模拟帧
// 无 GPU 纹理（texture=nullptr），仅填充元数据，供模拟链路使用
class SyntheticFrameSource {
public:
    SyntheticFrameSource(uint32_t width, uint32_t height, uint32_t fps);

    // 同步生成一帧（frameId 从 0 开始）
    CaptureFrame Generate(uint64_t frameId);

    uint32_t Width() const  { return m_width; }
    uint32_t Height() const { return m_height; }
    uint32_t Fps() const    { return m_fps; }

private:
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    int64_t  m_qpcFreq = 0;
};

} // namespace sf
