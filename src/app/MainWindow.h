#pragma once

#include <QMainWindow>

#include <wrl/client.h>

#include <cstdint>
#include <memory>

class QLabel;
class QPushButton;

namespace sf { class FramePipeline; class NvEncoder; class WgcCaptureSource; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRedetect();
    void onToggleCapture();
    void onToggleEncode();
    void onLogTimer();
    void onCapTimer();

private:
    QLabel* m_gpu    = nullptr;
    QLabel* m_d3d11  = nullptr;
    QLabel* m_vram   = nullptr;
    QLabel* m_driver = nullptr;
    QLabel* m_nvenc  = nullptr;
    QLabel* m_log    = nullptr;

    QPushButton* m_btnCapture = nullptr;
    QLabel*      m_capStatus  = nullptr;

    // Phase 2 统计
    QLabel* m_lblInFps    = nullptr;
    QLabel* m_lblQueue    = nullptr;
    QLabel* m_lblDrop     = nullptr;
    QLabel* m_lblInterval = nullptr;

    // Phase 3-A 编码
    QPushButton* m_btnEncode = nullptr;
    QLabel*      m_encStatus = nullptr;
    bool         m_encActive = false;
    int64_t      m_encStartQpc = 0;
    int64_t      m_qpcFreq = 0;

    std::unique_ptr<sf::FramePipeline>   m_pipeline;
    std::unique_ptr<sf::WgcCaptureSource> m_source;
    std::unique_ptr<sf::NvEncoder>        m_encoder;
    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
};
