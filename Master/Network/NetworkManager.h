#pragma once
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include "../../Common/Config.h"
#include "../../Common/NetworkServer.h"

namespace Formidable {
    struct ConnectedClient;
}

namespace Formidable {
namespace Network {

// 网络管理器 - 处理所有网络通信
class NetworkManager {
public:
    // 初始化网络管理器并启动服务
    static bool Initialize(int listenPort);
    
    // 停止网络服务
    static void Shutdown();
    
    // 发送数据到客户端
    static bool SendToClient(std::shared_ptr<ConnectedClient> client, const void* pData, int iLength);
    
    // 发送命令包
    static bool SendCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1 = 0, uint32_t arg2 = 0, const std::string& data = "");
    
    // 发送模块到客户端
    static bool SendModule(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2 = 0);
    
    // 断开客户端
    static void DisconnectClient(CONNID connId);
    
    // HPSocket回调
    static void OnConnect(CONNID connId, const char* ip);
    static void OnReceive(CONNID connId, const BYTE* pData, int iLength);
    static void OnClose(CONNID connId);

    // HTTP Server 相关 (用于客户端下载)
    static bool InitializeHttpServer(int port);
    static void ShutdownHttpServer();

private:
    static NetworkServer* s_pServer;
    static IHttpServer* s_pHttpServer; // HTTP 服务器对象
    static std::map<CONNID, std::vector<BYTE>> s_recvBuffers;
    static std::mutex s_recvBufferMutex;
};

} // namespace Network
} // namespace Formidable
