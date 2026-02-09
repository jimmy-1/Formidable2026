#include "ConnectionPool.h"

namespace Formidable {
namespace Server {
namespace Network {

// Connection Implementation
ConnectionPool::Connection::Connection(SOCKET s, const std::string& ip)
    : socket(s), clientIp(ip), lastActive(time(nullptr)), isActive(true), inUse(false) {
}

ConnectionPool::Connection::~Connection() {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
    }
}

bool ConnectionPool::Connection::SendData(const void* data, size_t length) {
    if (socket == INVALID_SOCKET) return false;
    int sent = send(socket, (const char*)data, (int)length, 0);
    if (sent > 0) {
        lastActive = time(nullptr);
        return true;
    }
    return false;
}

int ConnectionPool::Connection::ReceiveData(void* buffer, size_t length) {
    if (socket == INVALID_SOCKET) return -1;
    int received = recv(socket, (char*)buffer, (int)length, 0);
    if (received > 0) {
        lastActive = time(nullptr);
    }
    return received;
}

void ConnectionPool::Connection::Reset() {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
    clientIp = "";
    lastActive = 0;
    isActive = false;
    inUse = false;
}

// ConnectionPool Implementation
std::vector<std::shared_ptr<ConnectionPool::Connection>> ConnectionPool::s_pool;
std::mutex ConnectionPool::s_mutex;
size_t ConnectionPool::s_maxSize = 0;

void ConnectionPool::InitializePool(size_t maxSize) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_maxSize = maxSize;
    s_pool.reserve(maxSize);
    for (size_t i = 0; i < maxSize; ++i) {
        // Pre-allocate connections with invalid socket
        s_pool.push_back(std::make_shared<Connection>(INVALID_SOCKET, ""));
    }
}

std::shared_ptr<ConnectionPool::Connection> ConnectionPool::GetConnection() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto& conn : s_pool) {
        bool expected = false;
        if (conn->inUse.compare_exchange_strong(expected, true)) {
            return conn;
        }
    }
    // If pool is full/exhausted, we might return null or expand (if allowed)
    // For now, return nullptr if no free connection
    return nullptr;
}

void ConnectionPool::ReleaseConnection(std::shared_ptr<Connection> connection) {
    if (!connection) return;
    
    // Reset connection state
    connection->Reset();
    
    // inUse flag is part of Connection, so just setting it to false makes it available
    // But we set it to false in Reset? No, Reset sets inUse = false.
    // However, we should ensure thread safety if needed.
    // Since Reset sets inUse = false, the next GetConnection can pick it up.
}

void ConnectionPool::CleanupPool() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_pool.clear();
}

} // namespace Network
} // namespace Server
} // namespace Formidable
