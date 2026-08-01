#pragma once

// ScreenForge 编码器统一接口（Phase 3-B Sandbox）
// 实现：
//   - NvencSimulator（模拟器，沙盒验证）  ← 当前
//   - NvEncoder（真实 NVENC 硬件，Phase 3-A）
// 未来接入真实硬件：仅替换实现类，调用方（PipelineBenchmark 等）不变。

#include <cstdint>
#include <string>

#include "graphics/Frame.h"

struct ID3D11Device;

namespace sf {

class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual bool Initialize(ID3D11Device* device,
                            uint32_t width, uint32_t height,
                            uint32_t fps, uint32_t bitrateKbps,
                            const std::string& outputPath) = 0;
    virtual void Shutdown() = 0;
    virtual void PushFrame(CaptureFrame&& f) = 0;
    virtual bool IsRunning() const = 0;

    virtual uint64_t Submitted() const = 0;
    virtual uint64_t Encoded() const = 0;
    virtual uint64_t FailedFrames() const = 0;
    virtual uint64_t DroppedFrames() const = 0;
    virtual double   AvgLatencyMs() const = 0;
    virtual uint64_t BitstreamBytes() const = 0;
    virtual std::string LastError() const { return {}; }
};

} // namespace sf
