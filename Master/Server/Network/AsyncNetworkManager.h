#pragma once

#include <winsock2.h>
#include <windows.h>
#include <functional>
#include <vector>
#include <thread>
#include <map>
#include <mutex>
#include <atomic>

namespace Formidable {
namespace Server {
namespace Network {

class AsyncNetworkManager {
public:
    // 基于IOCP的异步网络架构
    static void InitializeAsyncIO();
    static void HandleAsyncIO();
    
    class IOCPManager {
    public:
        enum IOOperation {
            READ,
            WRITE,
            ACCEPT,
            DISCONNECT
        };

        static void Initialize();
        static void AddSocket(SOCKET socket);
        static void RemoveSocket(SOCKET socket);
        static void PostIOOperation(SOCKET socket, IOOperation operation);
        static void ProcessEvents(int timeout);
        
    private:
        static HANDLE s_hIOCP;
    };
    
    class EventLoop {
    public:
        enum EventType {
            NETWORK_EVENT,
            TIMER_EVENT,
            SIGNAL_EVENT
        };

        static void Run();
        static void Stop();
        static void AddEventHandler(EventType type, std::function<void()> handler);
        
    private:
        static std::atomic<bool> s_running;
        static std::map<EventType, std::vector<std::function<void()>>> s_handlers;
        static std::mutex s_mutex;
    };
};

} // namespace Network
} // namespace Server
} // namespace Formidable
