#pragma once

// Phase 7-A — 配置持久化（config.json）
// 纯数据 + Qt JSON 读写；无硬件依赖（不 include 任何硬件头）

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>

namespace sf {

struct UiPersistConfig {
    std::string outputPath = "视频存储/recording.mp4";
    std::string resolution = "1920x1080";   // 1080p / 1440p / 4K
    int  fps         = 60;
    int  bitrateMbps = 12;
    int  audioMode   = 3;                   // 0=None 1=System 2=Mic 3=Both
    int  targetType  = 0;                   // 0=monitor 1=window
    int  targetIndex = 0;
    bool trayMinimize = true;
    std::string hotkey = "Ctrl+F9";
};

// 保存 / 加载 config.json
inline void SaveUiConfig(const std::string& path, const UiPersistConfig& c) {
    QJsonObject o;
    o["outputPath"]  = QString::fromStdString(c.outputPath);
    o["resolution"]  = QString::fromStdString(c.resolution);
    o["fps"]         = c.fps;
    o["bitrateMbps"] = c.bitrateMbps;
    o["audioMode"]   = c.audioMode;
    o["targetType"]  = c.targetType;
    o["targetIndex"] = c.targetIndex;
    o["trayMinimize"] = c.trayMinimize;
    o["hotkey"]      = QString::fromStdString(c.hotkey);
    QFile f(QString::fromStdString(path));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
}

inline UiPersistConfig LoadUiConfig(const std::string& path) {
    UiPersistConfig c;
    QFile f(QString::fromStdString(path));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        c.outputPath  = o.value("outputPath").toString("视频存储/recording.mp4").toStdString();
        c.resolution  = o.value("resolution").toString("1920x1080").toStdString();
        c.fps         = o.value("fps").toInt(60);
        c.bitrateMbps = o.value("bitrateMbps").toInt(12);
        c.audioMode   = o.value("audioMode").toInt(3);
        c.targetType  = o.value("targetType").toInt(0);
        c.targetIndex = o.value("targetIndex").toInt(0);
        c.trayMinimize = o.value("trayMinimize").toBool(true);
        c.hotkey      = o.value("hotkey").toString("Ctrl+F9").toStdString();
    }
    return c;
}

} // namespace sf
