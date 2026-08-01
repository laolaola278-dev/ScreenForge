#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sf {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static void Init();
    static void Shutdown();
    static void Log(LogLevel lv, std::string_view msg);

    // UI 侧 300ms 轮询拉取，避免高频信号
    static std::vector<std::string> DrainPending();
};

} // namespace sf

#define LOG_INFO(msg)  ::sf::Logger::Log(::sf::LogLevel::Info,  msg)
#define LOG_WARN(msg)  ::sf::Logger::Log(::sf::LogLevel::Warn,  msg)
#define LOG_ERROR(msg) ::sf::Logger::Log(::sf::LogLevel::Error, msg)
