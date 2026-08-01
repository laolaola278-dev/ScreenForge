// ScreenForge Phase 1/2 — WGC 屏幕捕获源（主显示器）
// 输出 CaptureFrame{ ID3D11Texture2D(BGRA), width, height, format, captureQpc }
// 纹理始终驻留 GPU，禁止回读 CPU。

#include <unknwn.h>
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

#include "WgcCaptureSource.h"

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace sf {

namespace {
constexpr UINT kQueueCapacity = 2;   // 内部回退队列容量（仅未设置回调时使用）

// 从 Direct3D11CaptureFrame 提取 ID3D11Texture2D
Microsoft::WRL::ComPtr<ID3D11Texture2D> GetD3D11Texture(Direct3D11CaptureFrame const& frame) {
    auto surface = frame.Surface();
    auto inspectable = surface.as<::IInspectable>();
    winrt::com_ptr<::ID3D11Texture2D> spTexture;
    // 通过 IUnknown QI 获取 ID3D11Texture2D
    auto unk = inspectable.as<::IUnknown>();
    if (FAILED(unk->QueryInterface(guid_of<::ID3D11Texture2D>(), spTexture.put_void()))) {
        return nullptr;
    }
    return Microsoft::WRL::ComPtr<ID3D11Texture2D>(spTexture.get());
}

}

struct WgcCaptureSource::Item {
    Direct3D11CaptureFrame frame{nullptr};   // 持有 frame，防止纹理被帧池回收
    winrt::com_ptr<::ID3D11Texture2D> texture;
    uint32_t    width  = 0;
    uint32_t    height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    int64_t     captureQpc = 0;
    uint64_t    index = 0;
    int64_t     ts100ns = 0;
};

struct WgcCaptureSource::Impl {
    Microsoft::WRL::ComPtr<ID3D11Device> device;

    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
    event_token token{};

    std::atomic<bool>   running{false};
    std::atomic<uint32_t> width{0}, height{0};
    std::atomic<uint64_t> frames{0};
    std::atomic<double> fps{0.0};

    std::mutex mtx;
    std::deque<Item> queue;
    Item active;                     // 回退路径：消费端当前持有的帧
    FrameCallback callback;          // Phase 2：直推 SPSC 队列

    int64_t lastTs = 0;
    std::string lastError;

    void OnFrameArrived(Direct3D11CaptureFramePool const& pool) {
        Direct3D11CaptureFrame frame = pool.TryGetNextFrame();
        if (!frame) return;

        // 帧池纹理 → ID3D11Texture2D（零拷贝，通过 GetD3D11Texture 获取）
        auto texture = GetD3D11Texture(frame);
        if (!texture) return;

        // 构造 CaptureFrame（所有权：Capture 获得引用）
        CaptureFrame fr;
        fr.texture = texture;
        fr.index   = frames.load() + 1;
        fr.ts100ns = frame.SystemRelativeTime().count();

        D3D11_TEXTURE2D_DESC d{};
        fr.texture->GetDesc(&d);
        fr.width  = d.Width;
        fr.height = d.Height;

        frames.fetch_add(1);

        if (callback) {
            callback(fr);
        } else {
            // 回退：内部队列
            std::lock_guard lk(mtx);
            Item item;
            item.frame = std::move(frame);
            item.texture = texture;
            item.width  = d.Width;
            item.height = d.Height;
            item.format = d.Format;
            item.captureQpc = 0;
            item.index  = fr.index;
            item.ts100ns = fr.ts100ns;
            queue.push_back(std::move(item));
            if (queue.size() > kQueueCapacity) queue.pop_front();
            active = queue.back();
        }
    }

        // 构造 CaptureFrame（所有权：Capture 获得引用）
        CaptureFrame fr;
        fr.texture = Microsoft::WRL::ComPtr<ID3D11Texture2D>(texture.get());
        fr.index   = frames.load() + 1;
        fr.ts100ns = frame.SystemRelativeTime().count();

        D3D11_TEXTURE2D_DESC d{};
        fr.texture->GetDesc(&d);
        fr.width  = d.Width;
        fr.height = d.Height;
        fr.format = d.Format;

        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        fr.captureQpc = qpc.QuadPart;

        // FPS（EMA 平滑）
        const int64_t ts = fr.ts100ns;
        if (lastTs != 0) {
            const double diff = double(ts - lastTs);
            if (diff > 0) {
                const double inst = 1e7 / diff;
                const double cur  = fps.load();
                fps.store(cur == 0.0 ? inst : cur + (inst - cur) * 0.1);
            }
        }
        lastTs = ts;

        FrameCallback cb;
        {
            std::lock_guard<std::mutex> lock(mtx);
            cb = callback;
            if (!cb) {
                // 回退路径：内部有界队列（容量 2，丢最旧）
                Item it;
                it.frame      = frame;
                it.texture    = texture;
                it.width      = fr.width;
                it.height     = fr.height;
                it.format     = fr.format;
                it.captureQpc = fr.captureQpc;
                it.index      = fr.index;
                it.ts100ns    = fr.ts100ns;
                if (queue.size() >= kQueueCapacity) queue.pop_front();
                queue.push_back(std::move(it));
            }
        }
        if (cb) cb(std::move(fr));    // Phase 2：直推 SPSC 队列（所有权转移）
        frames.fetch_add(1);
    }
};

WgcCaptureSource::WgcCaptureSource(Microsoft::WRL::ComPtr<ID3D11Device> device)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->device = std::move(device);
}

WgcCaptureSource::~WgcCaptureSource() { Stop(); }

void WgcCaptureSource::SetFrameCallback(FrameCallback cb) {
    std::lock_guard<std::mutex> lock(m_impl->mtx);
    m_impl->callback = std::move(cb);
}

void WgcCaptureSource::SetCaptureTarget(HMONITOR monitor, HWND window) {
    m_targetMon = monitor;
    m_targetWnd = window;
}

bool WgcCaptureSource::Start() {
    auto& im = *m_impl;
    if (im.running.load()) return true;

    try {
        init_apartment(apartment_type::multi_threaded);

        if (!GraphicsCaptureSession::IsSupported()) {
            im.lastError = "WGC 不受支持（需要 Windows 10 1903+）";
            return false;
        }
        if (!im.device) {
            im.lastError = "D3D11 设备为空";
            return false;
        }

        // D3D11 设备 → DXGI 设备 → IDirect3DDevice（WGC 互操作）
        Microsoft::WRL::ComPtr<::IDXGIDevice> dxgiDevice;
        if (FAILED(im.device.As(&dxgiDevice))) {
            im.lastError = "无法获取 IDXGIDevice";
            return false;
        }
        winrt::com_ptr<::IDirect3DDevice> d3d;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.Get(), reinterpret_cast<::IInspectable**>(put_abi(d3d))));
        auto d3dProjected =
            d3d.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

        // 目标：窗口 > 指定显示器 > 主显示器（Phase 7-A）
        auto interop = get_activation_factory<GraphicsCaptureItem, ::IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        if (m_targetWnd) {
            check_hresult(interop->CreateForWindow(
                m_targetWnd, guid_of<GraphicsCaptureItem>(), put_abi(item)));
        } else {
            HMONITOR hmon = m_targetMon
                ? m_targetMon
                : MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
            check_hresult(interop->CreateForMonitor(
                hmon, guid_of<GraphicsCaptureItem>(), put_abi(item)));
        }

        const auto sz = item.Size();
        im.width.store(uint32_t(sz.Width));
        im.height.store(uint32_t(sz.Height));

        im.framePool = Direct3D11CaptureFramePool::Create(
            d3dProjected, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, sz);
        im.session = im.framePool.CreateCaptureSession(item);

        im.token = im.framePool.FrameArrived(
            [this](Direct3D11CaptureFramePool const& pool,
                   winrt::Windows::Foundation::IInspectable const&) {
                m_impl->OnFrameArrived(pool);
            });

        im.session.StartCapture();
        im.running.store(true);
        im.lastError.clear();
        return true;
    } catch (winrt::hresult_error const& e) {
        im.lastError = winrt::to_string(e.message());
        im.running.store(false);
        return false;
    }
}

void WgcCaptureSource::Stop() {
    auto& im = *m_impl;
    if (!im.running.load() && !im.session) return;
    im.running.store(false);
    try {
        if (im.framePool) {
            im.framePool.FrameArrived(im.token);
            im.framePool.Close();
            im.framePool = nullptr;
        }
        if (im.session) {
            im.session.Close();
            im.session = nullptr;
        }
    } catch (...) { /* 关闭阶段忽略异常 */ }
    std::lock_guard<std::mutex> lock(im.mtx);
    im.queue.clear();
    im.active = Item{};
}

bool WgcCaptureSource::GetFrame(CaptureFrame& out) {
    auto& im = *m_impl;
    std::lock_guard<std::mutex> lock(im.mtx);
    if (im.queue.empty()) return false;
    // takeLatest：只取最新帧，中间帧直接丢弃（丢弃旧帧策略）
    im.active = std::move(im.queue.back());
    im.queue.clear();
    out.texture    = Microsoft::WRL::ComPtr<ID3D11Texture2D>(im.active.texture.get());
    out.width      = im.active.width;
    out.height     = im.active.height;
    out.format     = im.active.format;
    out.captureQpc = im.active.captureQpc;
    out.index      = im.active.index;
    out.ts100ns    = im.active.ts100ns;
    return true;
}

uint32_t WgcCaptureSource::Width() const    { return m_impl->width.load(); }
uint32_t WgcCaptureSource::Height() const   { return m_impl->height.load(); }
uint32_t WgcCaptureSource::Fps() const      { return uint32_t(m_impl->fps.load() + 0.5); }
uint64_t WgcCaptureSource::FrameCount() const { return m_impl->frames.load(); }
bool WgcCaptureSource::IsRunning() const    { return m_impl->running.load(); }
std::string WgcCaptureSource::LastError() const { return m_impl->lastError; }

} // namespace sf
