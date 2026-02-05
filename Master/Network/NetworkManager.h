#pragma once
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include "../../Common/Config.h"
#include "../../Common/NetworkServer.h"

// 前向声明 - 使用main_gui.cpp中定义的ConnectedClient
struct ConnectedClient;

namespace Formidable {
namespace Network {

// 网络管理器 - 处理所有网络通信
class NetworkManager {
public:
    // 初始化网络管理器
    static bool Initialize(int listenPort);
    
    // 停止网络服务
    static void Shutdown();
    
    // 发送数据到客户端
    static bool SendToClient(std::shared_ptr<ConnectedClient> client, const void* pData, int iLength);
    
    // 发送命令包
    static bool SendCommand(std::shared_ptr<ConnectedClient> client, uint32_t cmd, uint32_t arg1 = 0, uint32_t arg2 = 0, const std::string& data = "");
    
    // 发送模块到客户端
    static bool SendModule(std::shared_ptr<ConnectedClient> client, const BYTE* moduleData, DWORD moduleSize);
    
    // 断开客户端
    static void DisconnectClient(CONNID connId);
    
    // 设置回调函数
    using OnClientConnectCallback = std::function<void(CONNID connId, const std::string& ip, uint16_t port)>;
    using OnClientDisconnectCallback = std::function<void(CONNID connId)>;
    using OnDataReceivedCallback = std::function<void(CONNID connId, const BYTE* pData, int iLength)>;
    
    static void SetOnClientConnect(OnClientConnectCallback cb) { s_onClientConnect = cb; }
    static void SetOnClientDisconnect(OnClientDisconnectCallback cb) { s_onClientDisconnect = cb; }
    static void SetOnDataReceived(OnDataReceivedCallback cb) { s_onDataReceived = cb; }
    
    // HPSocket回调
    static void OnConnect(CONNID connId, LPCTSTR lpszRemoteIP, USHORT usRemotePort);
    static void OnReceive(CONNID connId, const BYTE* pData, int iLength);
    static void OnClose(CONNID connId);
    
private:
    static NetworkServer* s_pServer;
    static std::map<CONNID, std::vector<BYTE>> s_recvBuffers;
    static std::mutex s_recvBufferMutex;
    
    static OnClientConnectCallback s_onClientConnect;
    static OnClientDisconnectCallback s_onClientDisconnect;
    static OnDataReceivedCallback s_onDataReceived;
};

} // namespace Network
} // namespace Formidable
