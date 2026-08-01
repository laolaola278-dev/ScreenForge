#pragma once

#include <Windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <string>

#include "graphics/Frame.h"

namespace sf {

// 捕获源接口
class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool GetFrame(CaptureFrame& out) = 0;   // 非阻塞，取最新帧
};

// WGC 主显示器捕获（Windows.Graphics.Capture）
// Phase 2：支持 SetFrameCallback 直推 SPSC 队列（生产端）；无回调时退化为内部队列
class WgcCaptureSource : public ICaptureSource {
public:
    using FrameCallback = std::function<void(CaptureFrame&&)>;

    explicit WgcCaptureSource(Microsoft::WRL::ComPtr<ID3D11Device> device);
    ~WgcCaptureSource() override;

    bool Start() override;
    void Stop() override;
    bool GetFrame(CaptureFrame& out) override;

    // Phase 2：设置帧回调（SPSC 生产者端，WGC 回调线程调用）
    void SetFrameCallback(FrameCallback cb);

    // Phase 7-A：设置捕获目标（显示器 HMONITOR / 窗口 HWND；均空 = 主显示器）
    void SetCaptureTarget(HMONITOR monitor, HWND window);

    uint32_t Width() const;
    uint32_t Height() const;
    uint32_t Fps() const;            // 实测帧率（EMA 平滑）
    uint64_t FrameCount() const;
    bool IsRunning() const;
    std::string LastError() const;

private:
    struct Impl;
    struct Item;
    std::unique_ptr<Impl> m_impl;
    HMONITOR m_targetMon = nullptr;   // Phase 7-A
    HWND     m_targetWnd = nullptr;   // Phase 7-A
};

} // namespace sf
