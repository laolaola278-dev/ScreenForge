// Phase 7-A — 主窗口实现（纯 Qt6 Widgets）
// 无任何硬件头；全部经 IRecorderUiBackend

#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace sf {

namespace {
const char* kConfigPath = "config.json";
}

MainWindow::MainWindow(IRecorderUiBackend* backend, QWidget* parent)
    : QMainWindow(parent), m_backend(backend) {
    setWindowTitle("ScreenForge");
    resize(520, 460);

    BuildUi();
    ApplyConfig(LoadUiConfig(kConfigPath));
    SetupTray();
    SetupHotkey();
    onRefreshTargets();

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPoll);
    m_pollTimer->start(500);
}

MainWindow::~MainWindow() {
    m_exiting = true;
    if (m_backend && m_backend->IsRecording()) m_backend->Stop();
    SaveUiConfig(kConfigPath, CollectConfig());
}

void MainWindow::BuildUi() {
    auto* root = new QVBoxLayout;

    // ── 目标选择 ──
    auto* targetBox = new QGroupBox("录制目标");
    auto* tform = new QFormLayout;
    m_cmbTargets = new QComboBox;
    auto* typeRow = new QHBoxLayout;
    m_btnRefresh = new QPushButton("刷新");
    connect(m_btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshTargets);
    typeRow->addWidget(m_btnRefresh);
    typeRow->addStretch();
    tform->addRow("目标", m_cmbTargets);
    tform->addRow("", typeRow);
    m_cmbAudio = new QComboBox;
    m_cmbAudio->addItem("无音频", 0);
    m_cmbAudio->addItem("系统声音", 1);
    m_cmbAudio->addItem("麦克风", 2);
    m_cmbAudio->addItem("系统 + 麦克风", 3);
    tform->addRow("音频", m_cmbAudio);
    targetBox->setLayout(tform);
    root->addWidget(targetBox);

    // ── 录制配置 ──
    auto* cfgBox = new QGroupBox("录制配置");
    auto* cform = new QFormLayout;
    m_cmbRes = new QComboBox;
    m_cmbRes->addItem("1080p (1920x1080)", 0);
    m_cmbRes->addItem("1440p (2560x1440)", 1);
    m_cmbRes->addItem("4K (3840x2160)", 2);
    m_cmbFps = new QComboBox;
    m_cmbFps->addItem("30", 30);
    m_cmbFps->addItem("60", 60);
    m_cmbBitrate = new QComboBox;
    m_cmbBitrate->addItem("8 Mbps", 8);
    m_cmbBitrate->addItem("12 Mbps", 12);
    m_cmbBitrate->addItem("20 Mbps", 20);
    cform->addRow("分辨率", m_cmbRes);
    cform->addRow("FPS", m_cmbFps);
    cform->addRow("码率", m_cmbBitrate);
    auto* codecLbl = new QLabel("H264 (NVENC / 模拟器回退)");
    cform->addRow("编码", codecLbl);
    cfgBox->setLayout(cform);
    root->addWidget(cfgBox);

    // ── 控制按钮 ──
    auto* btnRow = new QHBoxLayout;
    m_btnStart = new QPushButton("开始录制");
    m_btnStart->setMinimumHeight(40);
    m_btnPause = new QPushButton("暂停");
    m_btnPause->setEnabled(false);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartStop);
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onPauseResume);
    btnRow->addWidget(m_btnStart, 1);
    btnRow->addWidget(m_btnPause, 1);
    root->addLayout(btnRow);

    // ── 实时状态 ──
    auto* statBox = new QGroupBox("实时状态");
    auto* sform = new QFormLayout;
    m_lblState    = new QLabel("Idle");
    m_lblFps      = new QLabel("—");
    m_lblBitrate  = new QLabel("—");
    m_lblTime     = new QLabel("00:00");
    m_lblFileSize = new QLabel("—");
    m_lblCpu      = new QLabel("—");
    m_lblGpu      = new QLabel("—");
    sform->addRow("状态", m_lblState);
    sform->addRow("FPS", m_lblFps);
    sform->addRow("码率", m_lblBitrate);
    sform->addRow("录制时间", m_lblTime);
    sform->addRow("文件大小", m_lblFileSize);
    sform->addRow("CPU", m_lblCpu);
    sform->addRow("GPU 显存", m_lblGpu);
    statBox->setLayout(sform);
    root->addWidget(statBox);

    root->addStretch();
    auto* hint = new QLabel("快捷键: Ctrl+F9 开始/停止 · 关闭窗口最小化到托盘");
    hint->setStyleSheet("color:#8b949e;font-size:11px;");
    root->addWidget(hint);

    auto* host = new QWidget;
    host->setLayout(root);
    setCentralWidget(host);
}

void MainWindow::ApplyConfig(const UiPersistConfig& c) {
    const int resIdx = (c.resolution == "2560x1440") ? 1
                     : (c.resolution == "3840x2160") ? 2 : 0;
    m_cmbRes->setCurrentIndex(resIdx);
    m_cmbFps->setCurrentIndex(c.fps >= 60 ? 1 : 0);
    m_cmbBitrate->setCurrentIndex(m_cmbBitrate->findData(c.bitrateMbps));
    m_cmbAudio->setCurrentIndex(c.audioMode);
    if (m_cmbBitrate->currentIndex() < 0) m_cmbBitrate->setCurrentIndex(1);
}

UiPersistConfig MainWindow::CollectConfig() const {
    UiPersistConfig c;
    const int res = m_cmbRes->currentIndex();
    c.resolution = res == 1 ? "2560x1440" : res == 2 ? "3840x2160" : "1920x1080";
    c.fps = m_cmbFps->currentData().toInt();
    c.bitrateMbps = m_cmbBitrate->currentData().toInt();
    c.audioMode = m_cmbAudio->currentData().toInt();
    return c;
}

void MainWindow::onRefreshTargets() {
    if (!m_backend) return;
    m_monitors = m_backend->EnumMonitors();
    m_windows  = m_backend->EnumWindows();
    // 重建列表：显示器 + 窗口（窗口 id 偏移 1000 区分）
    m_cmbTargets->clear();
    for (const auto& t : m_monitors) {
        m_cmbTargets->addItem(QString::fromStdString(t.name), t.id);
    }
    for (const auto& t : m_windows) {
        m_cmbTargets->addItem(QString::fromStdString(t.name), 1000 + t.id);
    }
    if (m_cmbTargets->count() == 0) {
        m_cmbTargets->addItem("（无可用目标）", -1);
    }
}

void MainWindow::onStartStop() {
    if (!m_backend) return;
    if (m_backend->IsRecording()) {
        m_backend->Stop();
        m_btnStart->setText("开始录制");
        m_btnPause->setEnabled(false);
        m_btnPause->setText("暂停");
        m_paused = false;
        return;
    }
    if (m_cmbTargets->count() == 0 || m_cmbTargets->currentData().toInt() < 0) {
        m_lblState->setText("Error: 无可用目标");
        return;
    }
    UiStartConfig cfg;
    // 解析分辨率
    const int res = m_cmbRes->currentIndex();
    if (res == 1) { cfg.width = 2560; cfg.height = 1440; }
    else if (res == 2) { cfg.width = 3840; cfg.height = 2160; }
    else { cfg.width = 1920; cfg.height = 1080; }
    cfg.fps = m_cmbFps->currentData().toInt();
    cfg.bitrateKbps = uint32_t(m_cmbBitrate->currentData().toInt()) * 1000;
    cfg.audioMode = AudioMode(m_cmbAudio->currentData().toInt());

    // 目标（优先显示器列表；若目标 id 在窗口列表则用窗口）
    CaptureTargetInfo target;
    bool found = false;
    for (const auto& t : m_monitors) if (t.id == m_cmbTargets->currentData().toInt()) { target = t; found = true; break; }
    if (!found) for (const auto& t : m_windows) if (t.id == m_cmbTargets->currentData().toInt()) { target = t; found = true; break; }
    if (!found) { m_lblState->setText("Error: 目标失效，请刷新"); return; }

    if (m_backend->Start(cfg, target)) {
        m_btnStart->setText("停止录制");
        m_btnPause->setEnabled(true);
        m_paused = false;
    } else {
        m_lblState->setText("Error: 启动失败（见日志）");
    }
}

void MainWindow::onPauseResume() {
    if (!m_backend || !m_backend->IsRecording()) return;
    if (m_paused) {
        m_backend->Resume();
        m_paused = false;
        m_btnPause->setText("暂停");
    } else {
        m_backend->Pause();
        m_paused = true;
        m_btnPause->setText("继续");
    }
}

void MainWindow::onPoll() {
    if (!m_backend) return;
    const LiveStats s = m_backend->Poll();

    // 状态映射：Idle / Recording / Paused / Error
    std::string st = s.state;
    if (st == "Recording" && m_paused) st = "Paused";
    m_lblState->setText(QString::fromStdString(st));
    if (st == "Recording") m_lblState->setStyleSheet("color:#3fb950;font-weight:600;");
    else if (st == "Error") m_lblState->setStyleSheet("color:#f85149;font-weight:600;");
    else m_lblState->setStyleSheet("color:#e6edf3;");

    m_lblFps->setText(QString("%1").arg(s.fps));
    m_lblBitrate->setText(QString("%1 kbps").arg(s.bitrateKbps));
    const int mm = int(s.durationSec) / 60, ss = int(s.durationSec) % 60;
    m_lblTime->setText(QString("%1:%2").arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0')));
    m_lblFileSize->setText(QString("%1 MB").arg(s.fileSizeMB));
    m_lblCpu->setText(QString("%1 %").arg(s.cpuPct, 0, 'f', 1));
    m_lblGpu->setText(QString("%1 MB").arg(s.gpuMemMB));

    if (!s.lastError.empty()) {
        m_lastError = s.lastError;
        m_lblState->setText("Error");
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_exiting) { e->accept(); return; }
    // 最小化到托盘（trayMinimize 默认 true）
    hide();
    if (m_tray) m_tray->showMessage("ScreenForge", "已最小化到托盘，仍在后台运行",
                                    QSystemTrayIcon::Information, 2000);
    e->ignore();
}

void MainWindow::SetupTray() {
    // 简单图标：绘制红点（无资源文件）
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setBrush(Qt::red);
    p.setPen(Qt::NoPen);
    p.drawEllipse(6, 6, 20, 20);
    p.end();

    m_tray = new QSystemTrayIcon(QIcon(pm), this);
    auto* menu = new QMenu(this);
    menu->addAction("显示主窗口", this, [this] { show(); raise(); activateWindow(); });
    menu->addAction("开始/停止录制", this, &MainWindow::onStartStop);
    menu->addSeparator();
    menu->addAction("退出", this, &MainWindow::onExit);
    m_tray->setContextMenu(menu);
    m_tray->show();
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
}

void MainWindow::SetupHotkey() {
    m_hotkey = new QShortcut(QKeySequence("Ctrl+F9"), this);
    connect(m_hotkey, &QShortcut::activated, this, &MainWindow::onStartStop);
}

void MainWindow::onTrayActivated() {
    show();
    raise();
    activateWindow();
}

void MainWindow::onExit() {
    m_exiting = true;
    if (m_backend && m_backend->IsRecording()) m_backend->Stop();
    SaveUiConfig(kConfigPath, CollectConfig());
    QApplication::quit();
}

} // namespace sf
