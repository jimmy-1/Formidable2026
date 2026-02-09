#include "AutomationManager.h"

namespace Formidable {
namespace Client {
namespace Core {

std::vector<Task> AutomationManager::s_tasks;
std::mutex AutomationManager::s_mutex;
int AutomationManager::s_nextId = 1;
std::atomic<bool> AutomationManager::s_running(false);
std::thread AutomationManager::s_thread;

void AutomationManager::Initialize() {
    // Nothing special needed yet
}

void AutomationManager::Start() {
    if (s_running) return;
    s_running = true;
    s_thread = std::thread([]() {
        while (s_running) {
            Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    s_thread.detach(); // Detach to run in background
}

void AutomationManager::Stop() {
    s_running = false;
    if (s_thread.joinable()) {
        s_thread.join();
    }
}

int AutomationManager::AddTask(std::function<void()> action, int intervalMs, bool repeat) {
    std::lock_guard<std::mutex> lock(s_mutex);
    Task t;
    t.id = s_nextId++;
    t.action = action;
    t.interval = std::chrono::milliseconds(intervalMs);
    t.lastRun = std::chrono::steady_clock::now();
    t.repeat = repeat;
    s_tasks.push_back(t);
    return t.id;
}

void AutomationManager::RemoveTask(int id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_tasks.begin();
    while (it != s_tasks.end()) {
        if (it->id == id) {
            it = s_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

void AutomationManager::Update() {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto now = std::chrono::steady_clock::now();
    
    auto it = s_tasks.begin();
    while (it != s_tasks.end()) {
        if (now - it->lastRun >= it->interval) {
            // Run task
            if (it->action) it->action();
            
            it->lastRun = now;
            
            if (!it->repeat) {
                it = s_tasks.erase(it);
                continue;
            }
        }
        ++it;
    }
}

} // namespace Core
} // namespace Client
} // namespace Formidable
