#include <QApplication>
#include <QTimer>

#include <cstring>
#include <string>

#include "AudioRecordTest.h"
#include "HardwareRecordTest.h"
#include "Logger.h"
#include "MuxBench.h"
#include "PipelineBenchmark.h"
#include "RecordTest.h"
#include "SandboxDemo.h"
#include "SmokeTest.h"
#include "StabilityTest.h"
#include "UiBackend.h"
#include "ui/MainWindow.h"

namespace {

// 解析 --name value 形式参数
bool ParseU64(int argc, char* argv[], const char* name, uint64_t& dst) {
    for (int i = 2; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            dst = std::stoull(argv[i + 1]);
            return true;
        }
    }
    return false;
}

bool ParseStr(int argc, char* argv[], const char* name, std::string& dst) {
    for (int i = 2; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            dst = argv[i + 1];
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    sf::Logger::Init();

    // 命令行测试模式（保持 Phase 3-6 全部入口）
    // （旧 --verify 入口已并入 --hardware-record-test；源码保留 src/app/VerifyRun.* 未编译）
    // Phase 3-B-Sim：录制链路模拟
    if (argc > 1 && std::strcmp(argv[1], "--simulate-encode") == 0) {
        uint64_t w = 1920, h = 1080, fps = 60, frames = 10000;
        std::string out = "simulation_test.h264";
        std::string report = "simulation_report.json";
        ParseU64(argc, argv, "--width", w);
        ParseU64(argc, argv, "--height", h);
        ParseU64(argc, argv, "--fps", fps);
        ParseU64(argc, argv, "--frames", frames);
        ParseStr(argc, argv, "--out", out);
        ParseStr(argc, argv, "--report", report);
        const int rc = sf::RunSimulationBenchmark(
            uint32_t(w), uint32_t(h), uint32_t(fps), frames, out, report);
        sf::Logger::Shutdown();
        return rc;
    }
    // Phase 4-A：封装基准
    if (argc > 1 && std::strcmp(argv[1], "--mux-bench") == 0) {
        uint64_t packets = 10000;
        std::string out = "mux_bench.mp4";
        ParseU64(argc, argv, "--packets", packets);
        ParseStr(argc, argv, "--out", out);
        const int rc = sf::RunMuxBenchmark(packets, out);
        sf::Logger::Shutdown();
        return rc;
    }
    // Phase 4-B：录制测试（沙盒视频链路）
    if (argc > 1 && std::strcmp(argv[1], "--record-test") == 0) {
        uint64_t seconds = 60, fps = 60;
        std::string out = "recording.mp4";
        std::string json = "recording_session.json";
        ParseU64(argc, argv, "--seconds", seconds);
        ParseU64(argc, argv, "--fps", fps);
        ParseStr(argc, argv, "--out", out);
        ParseStr(argc, argv, "--json", json);
        const int rc = sf::RunRecordTest(seconds, uint32_t(fps), out, json);
        sf::Logger::Shutdown();
        return rc;
    }
    // Phase 5-A：真实硬件录制
    if (argc > 1 && std::strcmp(argv[1], "--hardware-record-test") == 0) {
        uint64_t seconds = 60, fps = 60;
        std::string out = "recording.mp4";
        std::string json = "hardware_report.json";
        ParseU64(argc, argv, "--seconds", seconds);
        ParseU64(argc, argv, "--fps", fps);
        ParseStr(argc, argv, "--out", out);
        ParseStr(argc, argv, "--json", json);
        const int rc = sf::RunHardwareRecordTest(seconds, uint32_t(fps), out, json);
        sf::Logger::Shutdown();
        return rc;
    }
    // Phase 5-B：稳定性测试
    if (argc > 1 && std::strcmp(argv[1], "--stability-test") == 0) {
        double hours = 2.0;
        uint64_t fps = 60, bitrate = 4000;
        std::string out = "stability_test.mp4";
        std::string json = "stability_report.json";
        for (int i = 2; i < argc - 1; ++i) {
            if (std::strcmp(argv[i], "--hours") == 0) hours = std::atof(argv[i + 1]);
            else if (std::strcmp(argv[i], "--fps") == 0) fps = std::stoull(argv[i + 1]);
            else if (std::strcmp(argv[i], "--bitrate") == 0) bitrate = std::stoull(argv[i + 1]);
            else if (std::strcmp(argv[i], "--out") == 0) out = argv[i + 1];
            else if (std::strcmp(argv[i], "--json") == 0) json = argv[i + 1];
        }
        const int rc = sf::RunStabilityTest(hours, uint32_t(fps), uint32_t(bitrate), out, json);
        sf::Logger::Shutdown();
        return rc;
    }
    // Phase 6-A：音视频录制
    if (argc > 1 && std::strcmp(argv[1], "--audio-record-test") == 0) {
        uint64_t seconds = 60, fps = 60;
        std::string out = "recording_with_audio.mp4";
        std::string json = "audio_report.json";
        ParseU64(argc, argv, "--seconds", seconds);
        ParseU64(argc, argv, "--fps", fps);
        ParseStr(argc, argv, "--out", out);
        ParseStr(argc, argv, "--json", json);
        const int rc = sf::RunAudioRecordTest(seconds, uint32_t(fps), out, json);
        sf::Logger::Shutdown();
        return rc;
    }

    // Phase 8-A：Smoke Test（真实环境五项检查）
    // ScreenForge.exe --smoke-test [--json smoke_report.json]
    if (argc > 1 && std::strcmp(argv[1], "--smoke-test") == 0) {
        std::string report = "smoke_report.json";
        ParseStr(argc, argv, "--json", report);
        const int rc = sf::RunSmokeTest(report);
        sf::Logger::Shutdown();
        return rc;
    }

    // Sandbox Demo：完整模拟录制链路（无硬件依赖，任何机器可运行）
    // ScreenForge.exe --sandbox-demo [--seconds 10] [--out sandbox_recording.mp4] [--json sandbox_recording_report.json]
    if (argc > 1 && std::strcmp(argv[1], "--sandbox-demo") == 0) {
        uint64_t seconds = 10;
        std::string out = "sandbox_recording.mp4";
        std::string json = "sandbox_recording_report.json";
        ParseU64(argc, argv, "--seconds", seconds);
        ParseStr(argc, argv, "--out", out);
        ParseStr(argc, argv, "--json", json);
        const int rc = sf::RunSandboxDemo(seconds, out, json);
        sf::Logger::Shutdown();
        return rc;
    }

    // --qt-test：仅验证 Qt 6 初始化是否正常
    if (argc > 1 && std::strcmp(argv[1], "--qt-test") == 0) {
        QApplication app(argc, argv);
        LOG_INFO("Qt 6 initialized OK");
        QTimer::singleShot(0, [&app]() {
            LOG_INFO("Qt event loop running");
            app.quit();
        });
        const int rc = app.exec();
        LOG_INFO("Qt event loop exited");
        sf::Logger::Shutdown();
        return rc;
    }

    // --ui-test[N]：逐步定位 UI 卡死点
    // 1: QApplication + UiBackend 构造（无 MainWindow）
    // 2: + MainWindow 构造（无 show()）
    // 3: + MainWindow show()
    // 4: + app.exec()
    if (argc > 1 && std::strcmp(argv[1], "--ui-test") == 0) {
        int level = 4;
        ParseU64(argc, argv, "--level", (uint64_t&)level);

        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        app.setStyleSheet(R"(
            QMainWindow  { background-color: #0d1117; }
            QGroupBox    { color:#e6edf3; border:1px solid #21262d; border-radius:8px;
                           margin-top:10px; padding-top:8px; font-weight:600; }
            QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }
            QLabel       { color:#c9d1d9; }
            QComboBox, QPushButton { background-color:#21262d; color:#e6edf3;
                           border:1px solid #30363d; border-radius:6px; padding:6px 10px; }
            QPushButton:hover { background-color:#30363d; }
            QPushButton#start { background-color:#238636; font-weight:700; }
            QPushButton#start:hover { background-color:#2ea043; }
        )");
        LOG_INFO("ui-test level=%d: QApplication OK", level);

        sf::UiBackend backend;
        LOG_INFO("ui-test level=%d: UiBackend constructed", level);

        if (level >= 2) {
            sf::MainWindow w(&backend);
            LOG_INFO("ui-test level=%d: MainWindow constructed", level);

            if (level >= 3) {
                w.show();
                LOG_INFO("ui-test level=%d: MainWindow shown", level);
            }

            if (level >= 4) {
                QTimer::singleShot(2000, [&app]() {
                    LOG_INFO("ui-test: event loop quit after 2s");
                    app.quit();
                });
                const int rc = app.exec();
                LOG_INFO("ui-test: app.exec() returned rc=%d", rc);
                sf::Logger::Shutdown();
                return rc;
            }
        }

        LOG_INFO("ui-test level=%d: exiting without event loop", level);
        sf::Logger::Shutdown();
        return 0;
    }

    // Phase 7-A：图形界面（默认启动）
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);   // 托盘常驻

    app.setStyleSheet(R"(
        QMainWindow  { background-color: #0d1117; }
        QGroupBox    { color:#e6edf3; border:1px solid #21262d; border-radius:8px;
                       margin-top:10px; padding-top:8px; font-weight:600; }
        QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }
        QLabel       { color:#c9d1d9; }
        QComboBox, QPushButton { background-color:#21262d; color:#e6edf3;
                       border:1px solid #30363d; border-radius:6px; padding:6px 10px; }
        QPushButton:hover { background-color:#30363d; }
        QPushButton#start { background-color:#238636; font-weight:700; }
        QPushButton#start:hover { background-color:#2ea043; }
    )");

    LOG_INFO("ScreenForge v0.8.0 starting (Phase 7-A UI)");

    sf::UiBackend backend;
    sf::MainWindow w(&backend);
    w.show();
    const int rc = app.exec();

    sf::Logger::Shutdown();
    return rc;
}
