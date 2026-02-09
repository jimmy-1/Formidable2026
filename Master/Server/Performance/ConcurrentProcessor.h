#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <stdexcept>

namespace Formidable {
namespace Server {
namespace Performance {

class ConcurrentProcessor {
public:
    // 获取单例实例
    static ConcurrentProcessor& GetInstance();

    // 初始化线程池
    void Initialize(size_t threads = std::thread::hardware_concurrency());
    
    // 停止线程池
    void Shutdown();

    // 提交任务到线程池
    template<class F, class... Args>
    auto Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // 获取当前活动线程数
    size_t GetThreadCount() const;

private:
    ConcurrentProcessor() : m_stop(false), m_initialized(false) {}
    ~ConcurrentProcessor();
    
    // 禁止拷贝
    ConcurrentProcessor(const ConcurrentProcessor&) = delete;
    ConcurrentProcessor& operator=(const ConcurrentProcessor&) = delete;

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop;
    std::atomic<bool> m_initialized;
};

// 模板实现必须在头文件中
template<class F, class... Args>
auto ConcurrentProcessor::Enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // 允许在停止前入队吗？通常不。但这里简化处理，如果停止了抛出异常或者不做
        if(m_stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        m_tasks.emplace([task](){ (*task)(); });
    }
    m_condition.notify_one();
    return res;
}

} // namespace Performance
} // namespace Server
} // namespace Formidable
