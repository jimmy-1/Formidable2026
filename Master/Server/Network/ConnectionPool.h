#pragma once
#include <winsock2.h>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <ctime>

namespace Formidable {
namespace Server {
namespace Network {

class ConnectionPool {
public:
    class Connection {
    public:
        SOCKET socket;
        std::string clientIp;
        time_t lastActive;
        bool isActive;
        std::atomic<bool> inUse;
        
        Connection(SOCKET s, const std::string& ip);
        ~Connection();
        
        bool SendData(const void* data, size_t length);
        int ReceiveData(void* buffer, size_t length);
        
        void Reset(); // Helper to reset state for reuse
    };

    static void InitializePool(size_t maxSize);
    static std::shared_ptr<Connection> GetConnection(); // Get an available connection object
    static void ReleaseConnection(std::shared_ptr<Connection> connection);
    static void CleanupPool();
    
private:
    static std::vector<std::shared_ptr<Connection>> s_pool;
    static std::mutex s_mutex;
    static size_t s_maxSize;
};

} // namespace Network
} // namespace Server
} // namespace Formidable
