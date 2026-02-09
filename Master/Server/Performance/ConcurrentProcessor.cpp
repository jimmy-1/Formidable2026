#include "ConcurrentProcessor.h"
#include <iostream>

namespace Formidable {
namespace Server {
namespace Performance {

ConcurrentProcessor& ConcurrentProcessor::GetInstance() {
    static ConcurrentProcessor instance;
    return instance;
}

ConcurrentProcessor::~ConcurrentProcessor() {
    if (m_initialized) {
        Shutdown();
    }
}

void ConcurrentProcessor::Initialize(size_t threads) {
    if (m_initialized.exchange(true)) {
        return; // 已经初始化
    }

    m_stop = false;
    
    // 如果传入0，则使用硬件并发数，最少为4
    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads < 4) threads = 4;
    }

    for(size_t i = 0; i < threads; ++i) {
        m_workers.emplace_back(
            [this]
            {
                for(;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->m_queueMutex);
                        this->m_condition.wait(lock,
                            [this]{ return this->m_stop || !this->m_tasks.empty(); });
                        
                        if(this->m_stop && this->m_tasks.empty())
                            return;
                        
                        task = std::move(this->m_tasks.front());
                        this->m_tasks.pop();
                    }

                    task();
                }
            }
        );
    }
}

void ConcurrentProcessor::Shutdown() {
    if (!m_initialized.exchange(false)) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    
    m_condition.notify_all();
    
    for(std::thread &worker: m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    m_workers.clear();
}

size_t ConcurrentProcessor::GetThreadCount() const {
    return m_workers.size();
}

} // namespace Performance
} // namespace Server
} // namespace Formidable
