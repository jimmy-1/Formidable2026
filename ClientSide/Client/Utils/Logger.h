#pragma once
#include <string>
#include <fstream>
#include <mutex>

namespace Formidable {
namespace Client {
namespace Utils {

enum class LogLevel {
    LL_DEBUG,
    LL_INFO,
    LL_WARNING,
    LL_ERROR
};

class Logger {
public:
    static void Init(const std::string& path);
    static void Log(LogLevel level, const std::string& msg);
    static void Close();

private:
    static std::ofstream s_file;
    static std::mutex s_lock;
    static bool s_ready;
};

} // namespace Utils
} // namespace Client
} // namespace Formidable
