#include "Logger.h"

#include <Windows.h>

#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace sf {
namespace {

constexpr size_t kRingCapacity  = 65536;                // 满则丢弃最旧
constexpr DWORD  kFlushMs       = 100;                  // 批量落盘周期
constexpr size_t kRotateBytes   = 64ULL * 1024 * 1024;  // 64MB 轮转

std::deque<std::string> g_queue;
std::mutex              g_mutex;
std::ofstream           g_file;
HANDLE                  g_thread = nullptr;
volatile LONG           g_run    = 1;

std::string NowStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

void RotateFile() {
    g_file.close();
    DeleteFileA("logs/app.3.log");
    MoveFileA("logs/app.2.log", "logs/app.3.log");
    MoveFileA("logs/app.1.log", "logs/app.2.log");
    MoveFileA("logs/app.log",   "logs/app.1.log");
    g_file.open("logs/app.log", std::ios::app);
}

DWORD WINAPI WriterLoop(LPVOID) {
    while (InterlockedCompareExchange(&g_run, 1, 1)) {
        Sleep(kFlushMs);
        std::lock_guard<std::mutex> lock(g_mutex);
        while (!g_queue.empty()) {
            g_file << g_queue.front() << '\n';
            g_queue.pop_front();
        }
        g_file.flush();
        if (g_file.tellp() > static_cast<std::streampos>(kRotateBytes)) {
            RotateFile();
        }
    }
    return 0;
}

} // namespace

void Logger::Init() {
    CreateDirectoryA("logs", nullptr);
    g_file.open("logs/app.log", std::ios::app);
    g_thread = CreateThread(nullptr, 0, WriterLoop, nullptr, 0, nullptr);
    Log(LogLevel::Info, "logger initialized");
}

void Logger::Shutdown() {
    InterlockedExchange(&g_run, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    while (!g_queue.empty()) {
        g_file << g_queue.front() << '\n';
        g_queue.pop_front();
    }
    g_file.close();
}

void Logger::Log(LogLevel lv, std::string_view msg) {
    static const char* kLevel[] = { "DBG", "INF", "WRN", "ERR" };
    std::ostringstream os;
    os << '[' << NowStamp() << "][" << kLevel[static_cast<int>(lv)]
       << "][tid=" << GetCurrentThreadId() << "] " << msg;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kRingCapacity) g_queue.pop_front();
    g_queue.emplace_back(os.str());
}

std::vector<std::string> Logger::DrainPending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // deque 与 vector 类型不同，不能直接 swap —— 逐条搬移
    std::vector<std::string> out;
    out.reserve(g_queue.size());
    while (!g_queue.empty()) {
        out.emplace_back(std::move(g_queue.front()));
        g_queue.pop_front();
    }
    return out;
}

} // namespace sf
