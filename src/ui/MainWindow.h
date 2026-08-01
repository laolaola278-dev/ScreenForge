#pragma once

// Phase 7-A — 主窗口（纯 Qt6 Widgets UI）
// 架构隔离：本文件禁止 include d3d11/nvenc/wasapi/ffmpeg
// 所有录制操作经 IRecorderUiBackend 抽象接口

#include <QMainWindow>

#include <memory>
#include <string>

#include "IRecorderUiBackend.h"
#include "RecorderConfig.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSystemTrayIcon;
class QTimer;
class QShortcut;

namespace sf {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(IRecorderUiBackend* backend, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartStop();
    void onPauseResume();
    void onRefreshTargets();
    void onPoll();
    void onTrayActivated();
    void onExit();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void BuildUi();
    void ApplyConfig(const UiPersistConfig& c);
    UiPersistConfig CollectConfig() const;
    void UpdateState(const std::string& state);
    void SetupTray();
    void SetupHotkey();

    IRecorderUiBackend* m_backend = nullptr;

    // 目标 / 音频选择
    QComboBox* m_cmbTargets = nullptr;     // 显示器或窗口
    QComboBox* m_cmbAudio   = nullptr;     // None/System/Mic/Both

    // 配置
    QComboBox* m_cmbRes  = nullptr;        // 1080p/1440p/4K
    QComboBox* m_cmbFps  = nullptr;        // 30/60
    QComboBox* m_cmbBitrate = nullptr;     // 8/12/20 Mbps
    QLineEdit* m_editPath = nullptr;        // 输出路径

    // 控制
    QPushButton* m_btnStart  = nullptr;
    QPushButton* m_btnPause  = nullptr;
    QPushButton* m_btnRefresh = nullptr;

    // 实时状态
    QLabel* m_lblState    = nullptr;
    QLabel* m_lblFps      = nullptr;
    QLabel* m_lblBitrate  = nullptr;
    QLabel* m_lblTime     = nullptr;
    QLabel* m_lblFileSize = nullptr;
    QLabel* m_lblCpu      = nullptr;
    QLabel* m_lblGpu      = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QTimer*  m_pollTimer = nullptr;
    QShortcut* m_hotkey  = nullptr;

    std::vector<CaptureTargetInfo> m_monitors;
    std::vector<CaptureTargetInfo> m_windows;
    bool m_paused = false;
    bool m_exiting = false;
    std::string m_lastError;
};

} // namespace sf
