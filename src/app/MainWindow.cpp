#include "MainWindow.h"

#include <Windows.h>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <cstdio>
#include <string>

#include "FramePipeline.h"
#include "Logger.h"
#include "NvEncoder.h"
#include "WgcCaptureSource.h"
#include "graphics/D3D11Device.h"

namespace {

void SetCard(QLabel* card, const QString& title, const QString& value, bool ok) {
    card->setTextFormat(Qt::RichText);
    card->setText(QString("<div style='font-size:11px;color:#8b949e'>%1</div>"
                          "<div style='font-size:16px;font-weight:600;color:%2'>%3</div>")
                      .arg(title, ok ? "#3fb950" : "#f85149", value));
}

QLabel* MakeCard(const QString& title) {
    auto* card = new QLabel;
    card->setObjectName("card");
    SetCard(card, title, "检测中…", true);
    return card;
}

QLabel* MakeStat(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("card");
    l->setStyleSheet("font-size:12px;font-weight:600;");
    return l;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ScreenForge v0.4.0 — Phase 3-A NVENC 编码");
    resize(880, 640);

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    m_qpcFreq = freq.QuadPart;

    auto* root = new QVBoxLayout;
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel("ScreenForge 环境检测");
    title->setObjectName("title");
    root->addWidget(title);

    auto* grid = new QGridLayout;
    grid->setSpacing(10);
    m_gpu    = MakeCard("GPU 型号");
    m_d3d11  = MakeCard("Direct3D 11");
    m_vram   = MakeCard("显存");
    m_driver = MakeCard("驱动版本");
    m_nvenc  = MakeCard("NVENC SDK");
    grid->addWidget(m_gpu,    0, 0);
    grid->addWidget(m_d3d11,  0, 1);
    grid->addWidget(m_vram,   1, 0);
    grid->addWidget(m_driver, 1, 1);
    grid->addWidget(m_nvenc,  2, 0, 1, 2);
    root->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    auto* redetect = new QPushButton("重新检测");
    btnRow->addWidget(redetect);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // 捕获控制（Phase 1/2）
    auto* capRow = new QHBoxLayout;
    m_btnCapture = new QPushButton("开始捕获（主显示器）");
    m_capStatus  = new QLabel("未捕获");
    m_capStatus->setObjectName("card");
    capRow->addWidget(m_btnCapture);
    capRow->addWidget(m_capStatus, 1);
    root->addLayout(capRow);

    // Phase 2 帧管线统计
    auto* statRow = new QHBoxLayout;
    statRow->setSpacing(8);
    m_lblInFps    = MakeStat("输入 FPS: —");
    m_lblQueue    = MakeStat("队列: —");
    m_lblDrop     = MakeStat("丢弃: —");
    m_lblInterval = MakeStat("帧间隔: — ms");
    statRow->addWidget(m_lblInFps);
    statRow->addWidget(m_lblQueue);
    statRow->addWidget(m_lblDrop);
    statRow->addWidget(m_lblInterval);
    root->addLayout(statRow);

    // Phase 3-A 编码控制
    auto* encRow = new QHBoxLayout;
    m_btnEncode = new QPushButton("开始编码测试（test.h264）");
    m_encStatus = new QLabel("未编码");
    m_encStatus->setObjectName("card");
    encRow->addWidget(m_btnEncode);
    encRow->addWidget(m_encStatus, 1);
    root->addLayout(encRow);

    m_log = new QLabel;
    m_log->setObjectName("log");
    m_log->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_log->setWordWrap(false);
    root->addWidget(m_log, 1);

    auto* host = new QWidget;
    host->setLayout(root);
    setCentralWidget(host);

    connect(redetect, &QPushButton::clicked, this, &MainWindow::onRedetect);
    connect(m_btnCapture, &QPushButton::clicked, this, &MainWindow::onToggleCapture);
    connect(m_btnEncode, &QPushButton::clicked, this, &MainWindow::onToggleEncode);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::onLogTimer);
    timer->start(300);

    auto* capTimer = new QTimer(this);
    connect(capTimer, &QTimer::timeout, this, &MainWindow::onCapTimer);
    capTimer->start(500);

    onRedetect();
}

MainWindow::~MainWindow() {
    if (m_pipeline) m_pipeline->Stop();
    if (m_encoder)  m_encoder->Shutdown();
    if (m_source)   m_source->Stop();
}

void MainWindow::onRedetect() {
    LOG_INFO("detect: start");
    const sf::GpuInfo g = sf::D3D11Device::Detect();

    const QString gpuName = g.name.empty() ? "未检测到 GPU" : QString::fromStdString(g.name);
    SetCard(m_gpu,    "GPU 型号",    gpuName, !g.name.empty());
    SetCard(m_d3d11,  "Direct3D 11", g.d3d11 ? "✓ Available" : "✕ Unavailable", g.d3d11);
    SetCard(m_vram,   "显存",        g.vramMB > 0 ? QString("%1 GB").arg(g.vramMB / 1024.0, 0, 'f', 1) : "—", g.vramMB > 0);
    SetCard(m_driver, "驱动版本",    g.driver.empty() ? "—" : QString::fromStdString(g.driver), !g.driver.empty());
    SetCard(m_nvenc,  "NVENC SDK",   g.nvenc ? QString::fromStdString(g.nvencVersion) : "✕ 未检测到", g.nvenc);

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "detect: gpu=\"%s\" d3d11=%d nvidia=%d nvenc=%d vram=%.1fGB",
                  g.name.c_str(), g.d3d11 ? 1 : 0, g.nvidia ? 1 : 0,
                  g.nvenc ? 1 : 0, g.vramMB / 1024.0);
    LOG_INFO(buf);
}

void MainWindow::onToggleCapture() {
    if (!m_source) {
        if (!sf::D3D11Device::Create(m_device, m_context)) {
            LOG_ERROR("capture: D3D11 设备创建失败");
            m_capStatus->setText("✕ D3D11 设备创建失败");
            return;
        }
        m_source = std::make_unique<sf::WgcCaptureSource>(m_device);
    }

    if (m_pipeline && m_pipeline->IsRunning()) {
        m_pipeline->Stop();
        m_source->Stop();
        m_btnCapture->setText("开始捕获（主显示器）");
        m_capStatus->setText("已停止");
        LOG_INFO("pipeline: stopped");
        return;
    }

    if (!m_pipeline) m_pipeline = std::make_unique<sf::FramePipeline>();

    auto consumer = [](sf::CaptureFrame&& f) {
        static std::atomic<uint64_t> lastLog{0};
        const uint64_t idx = f.index;
        if (idx - lastLog.load() >= 300) {
            lastLog.store(idx);
            LOG_INFO("consume: frame #" + std::to_string(idx) +
                     " ok " + std::to_string(f.width) + "x" + std::to_string(f.height));
        }
    };

    if (m_pipeline->Start(*m_source, consumer)) {
        m_btnCapture->setText("停止捕获");
        m_capStatus->setText("帧管线运行中");
        LOG_INFO("pipeline: started (QPC 60fps pacing)");
    } else {
        m_capStatus->setText("✕ 帧管线启动失败");
        LOG_ERROR("pipeline: start failed - " + m_source->LastError());
    }
}

void MainWindow::onToggleEncode() {
    if (m_encActive) {
        // 停止：管线 → 编码器 → 捕获
        if (m_pipeline) m_pipeline->Stop();
        if (m_encoder)  m_encoder->Shutdown();
        if (m_source)   m_source->Stop();
        m_encActive = false;
        m_btnEncode->setText("开始编码测试（test.h264）");
        m_encStatus->setText("已停止");
        LOG_INFO("encode: stopped");
        return;
    }

    if (!m_source) {
        if (!sf::D3D11Device::Create(m_device, m_context)) {
            LOG_ERROR("encode: D3D11 设备创建失败");
            return;
        }
        m_source = std::make_unique<sf::WgcCaptureSource>(m_device);
    }

    // 编码尺寸：用捕获分辨率（1080p 测试环境即为 1920x1080），默认 1920x1080
    const uint32_t w = m_source->IsRunning() && m_source->Width() > 0
                           ? m_source->Width() : 1920;
    const uint32_t h = m_source->IsRunning() && m_source->Height() > 0
                           ? m_source->Height() : 1080;

    m_encoder = std::make_unique<sf::NvEncoder>();
    if (!m_encoder->Initialize(m_device.Get(), w, h, 60, 12000, "test.h264")) {
        m_encStatus->setText("✕ " + QString::fromStdString(m_encoder->LastError()));
        LOG_ERROR("encode: init failed - " + m_encoder->LastError());
        return;
    }

    if (!m_pipeline) m_pipeline = std::make_unique<sf::FramePipeline>();

    // 管线 Consumer → 编码器队列（帧所有权：Capture→Queue→Consumer→Encoder→释放）
    sf::NvEncoder* enc = m_encoder.get();
    auto consumer = [enc](sf::CaptureFrame&& f) {
        enc->PushFrame(std::move(f));
    };

    if (!m_pipeline->Start(*m_source, consumer)) {
        m_encStatus->setText("✕ 帧管线启动失败");
        LOG_ERROR("encode: pipeline start failed - " + m_source->LastError());
        return;
    }

    m_encActive = true;
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    m_encStartQpc = q.QuadPart;
    m_btnEncode->setText("停止编码（10 分钟自动停）");
    m_encStatus->setText("NVENC 编码中…");
    LOG_INFO("encode: started " + std::to_string(w) + "x" + std::to_string(h) +
             " 60fps CBR " + std::to_string(12000) + "kbps → test.h264");
}

void MainWindow::onCapTimer() {
    if (!m_pipeline || !m_pipeline->IsRunning() || !m_source) return;

    m_capStatus->setText(QString("WGC 捕获中 · %1×%2")
        .arg(m_source->Width()).arg(m_source->Height()));

    m_lblInFps->setText(QString("输入 FPS: %1").arg(m_pipeline->InputFps()));
    m_lblQueue->setText(QString("队列: %1/%2").arg(m_pipeline->QueueDepth()).arg(m_pipeline->QueueCapacity()));
    m_lblDrop->setText(QString("丢弃: %1").arg(m_pipeline->DroppedFrames()));
    m_lblInterval->setText(QString("帧间隔: %1 ms").arg(m_pipeline->AvgFrameIntervalMs(), 0, 'f', 2));

    // Phase 3-A 编码统计 + 10 分钟自动停止
    if (m_encActive && m_encoder) {
        m_encStatus->setText(QString("NVENC · 提交 %1 · 完成 %2 · %3 fps · 延迟 %4 ms · %5 KB")
            .arg(m_encoder->Submitted())
            .arg(m_encoder->Encoded())
            .arg(m_encoder->EncoderFps())
            .arg(m_encoder->AvgLatencyMs(), 0, 'f', 1)
            .arg(m_encoder->BitstreamBytes() / 1024));

        LARGE_INTEGER q{};
        QueryPerformanceCounter(&q);
        if ((q.QuadPart - m_encStartQpc) >= m_qpcFreq * 600LL) {
            LOG_INFO("encode: 10 分钟测试完成，自动停止");
            onToggleEncode();
        }
    }
}

void MainWindow::onLogTimer() {
    auto lines = sf::Logger::DrainPending();
    if (lines.empty()) return;
    QString text = m_log->text();
    for (const auto& l : lines) {
        text += QString::fromStdString(l) + '\n';
    }
    if (text.size() > 8000) text = text.right(8000);
    m_log->setText(text);
}
