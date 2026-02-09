#include "Logger.h"
#include <ctime>
#include <sstream>
#include <iomanip>

namespace Formidable {
namespace Client {
namespace Utils {

std::ofstream Logger::s_file;
std::mutex Logger::s_lock;
bool Logger::s_ready = false;

void Logger::Init(const std::string& path) {
    std::lock_guard<std::mutex> lock(s_lock);
    s_file.open(path, std::ios::app);
    s_ready = true;
}

void Logger::Close() {
    std::lock_guard<std::mutex> lock(s_lock);
    if (s_file.is_open()) s_file.close();
    s_ready = false;
}

void Logger::Log(LogLevel level, const std::string& msg) {
    return; // 被控端禁用日志输出
    std::lock_guard<std::mutex> lock(s_lock);
    if (!s_ready || !s_file.is_open()) return;

    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    const char* lvlStr = "[UNK]";
    switch(level) {
        case LogLevel::LL_DEBUG:   lvlStr = "[DBG]"; break;
        case LogLevel::LL_INFO:    lvlStr = "[INF]"; break;
        case LogLevel::LL_WARNING: lvlStr = "[WRN]"; break;
        case LogLevel::LL_ERROR:   lvlStr = "[ERR]"; break;
    }

    s_file << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S]") << " " << lvlStr << " " << msg << std::endl;
}

} // namespace Utils
} // namespace Client
} // namespace Formidable
