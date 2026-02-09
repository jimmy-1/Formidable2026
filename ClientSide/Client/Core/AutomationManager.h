#pragma once
#include <functional>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>

namespace Formidable {
namespace Client {
namespace Core {

struct Task {
    int id;
    std::function<void()> action;
    std::chrono::milliseconds interval;
    std::chrono::steady_clock::time_point lastRun;
    bool repeat;
};

class AutomationManager {
public:
    static void Initialize();
    static void Start();
    static void Stop();
    static int AddTask(std::function<void()> action, int intervalMs, bool repeat = true);
    static void RemoveTask(int id);
    static void Update(); 

private:
    static std::vector<Task> s_tasks;
    static std::mutex s_mutex;
    static int s_nextId;
    static std::atomic<bool> s_running;
    static std::thread s_thread;
};

} // namespace Core
} // namespace Client
} // namespace Formidable
