#include "Logger.h"
#include <iomanip>
#include <sstream>

namespace Formidable {
namespace Server {
namespace Utils {

std::ofstream Logger::s_logFile;
std::mutex Logger::s_mutex;
bool Logger::s_enableConsole = true;
bool Logger::s_initialized = false;

void Logger::Initialize(const std::string& logFile, bool enableConsole) {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_initialized) return;
        s_logFile.open(logFile, std::ios::app);
        s_enableConsole = enableConsole;
        s_initialized = true;
    }
    Log(LogLevel::LL_INFO, "Logger initialized");
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_logFile.is_open()) {
        s_logFile.close();
    }
    s_initialized = false;
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized && level != LogLevel::LL_ERROR) return; // Allow errors to stderr if not init?

    std::string timestamp = GetTimestamp();
    std::string levelStr = LevelToString(level);
    std::stringstream ss;
    ss << "[" << timestamp << "] [" << levelStr << "] " << message;
    std::string logEntry = ss.str();

    if (s_logFile.is_open()) {
        s_logFile << logEntry << std::endl;
        s_logFile.flush();
    }

    if (s_enableConsole) {
        std::cout << logEntry << std::endl;
    }
}

std::string Logger::GetTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::LL_DEBUG: return "DEBUG";
        case LogLevel::LL_INFO: return "INFO";
        case LogLevel::LL_WARNING: return "WARN";
        case LogLevel::LL_ERROR: return "ERROR";
        case LogLevel::LL_SECURITY: return "SEC";
        default: return "UNKNOWN";
    }
}

} // namespace Utils
} // namespace Server
} // namespace Formidable
