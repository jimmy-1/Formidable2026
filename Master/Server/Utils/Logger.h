#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <iostream>
#include <ctime>

namespace Formidable {
namespace Server {
namespace Utils {

enum class LogLevel {
    LL_DEBUG,
    LL_INFO,
    LL_WARNING,
    LL_ERROR,
    LL_SECURITY
};

class Logger {
public:
    static void Initialize(const std::string& logFile, bool enableConsole = true);
    static void Log(LogLevel level, const std::string& message);
    static void Shutdown();

private:
    static std::string GetTimestamp();
    static std::string LevelToString(LogLevel level);

    static std::ofstream s_logFile;
    static std::mutex s_mutex;
    static bool s_enableConsole;
    static bool s_initialized;
};

} // namespace Utils
} // namespace Server
} // namespace Formidable
