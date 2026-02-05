#include "NetworkManager.h"
#include "../Utils/StringHelper.h"
#include "../../Common/ClientTypes.h"
#include <sstream>

namespace Formidable {
namespace Network {

using namespace Formidable::Utils;
namespace Network {

// 静态成员初始化
NetworkServer* NetworkManager::s_pServer = nullptr;
NetworkManager::OnClientConnectCallback NetworkManager::s_onClientConnect = nullptr;
NetworkManager::OnClientDisconnectCallback NetworkManager::s_onClientDisconnect = nullptr;
NetworkManager::OnDataReceivedCallback NetworkManager::s_onDataReceived = nullptr;
std::map<CONNID, std::vector<BYTE>> NetworkManager::s_recvBuffers;
std::mutex NetworkManager::s_recvBufferMutex;

// 初始化网络服务
bool NetworkManager::Initialize(int port) {
    // 创建HPSocket服务器
    s_pServer = new NetworkServer();
    
    // 设置回调
    s_pServer->SetOnConnect(OnConnect);
    s_pServer->SetOnReceive(OnReceive);
    s_pServer->SetOnClose(OnClose);
    
    // 启动服务器
    if (!s_pServer->Start("0.0.0.0", (USHORT)port)) {
        EnSocketError errCode = s_pServer->GetLastError();
        std::wstring errMsg = L"启动HPSocket服务器失败\n端口: " + std::to_wstring(port) + 
                             L"\n错误码: " + std::to_wstring((int)errCode);
        
        switch (errCode) {
            case SE_ILLEGAL_STATE:
                errMsg += L"\n原因: 服务器已启动或状态非法";
                break;
            case SE_SOCKET_CREATE:
                errMsg += L"\n原因: 创建Socket失败";
                break;
            case SE_SOCKET_BIND:
                errMsg += L"\n原因: 绑定端口失败（端口可能被占用）";
                break;
            case SE_SOCKET_LISTEN:
                errMsg += L"\n原因: 监听失败";
                break;
            case SE_NETWORK:
                errMsg += L"\n原因: 网络错误";
                break;
            default:
                errMsg += L"\n原因: 未知错误";
                break;
        }
        
        MessageBoxW(nullptr, 
            (errMsg + L"\n\n建议：\n1. 检查端口" + std::to_wstring(port) + L"是否被占用\n2. 以管理员权限运行程序\n3. 检查防火墙设置").c_str(), 
            L"网络启动失败", MB_OK | MB_ICONERROR);
        
        delete s_pServer;
        s_pServer = nullptr;
        return false;
    }
    
    return true;
}

// 关闭网络服务
void NetworkManager::Shutdown() {
    if (s_pServer) {
        s_pServer->Stop();
        delete s_pServer;
        s_pServer = nullptr;
    }
    
    std::lock_guard<std::mutex> lock(s_recvBufferMutex);
    s_recvBuffers.clear();
}

// 发送数据到客户端
bool NetworkManager::SendToClient(std::shared_ptr<ClientConnection> client, const void* data, int length) {
    if (!s_pServer || !client || !client->active) {
        return false;
    }
    
    return s_pServer->Send(client->connId, (const BYTE*)data, length);
}

// 发送命令到客户端
bool NetworkManager::SendCommand(std::shared_ptr<ClientConnection> client, int cmd, int arg1, int arg2, const std::string& data) {
    if (!client) return false;
    onnectedClient> client, int cmd, int arg1, int arg2, const std::string& data) {
    if (!client) return false;
    
    // 构建命令包
    CommandPkg pkg = { 0 };
    pkg.cmd = cmd;
    pkg.arg1 = arg1;
    pkg.arg2 = arg2;
    
    size_t bodySize = sizeof(CommandPkg);
    if (!data.empty()) {
        bodySize += data.size();
    }
    
    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
    
    // 填充包头
    PkgHeader* header = (PkgHeader*)buffer.data();
    memcpy(header->flag, "FRMD26?", 7);
    header->originLen = (int)bodySize;
    header->totalLen = (int)buffer.size();
    
    // 填充命令包
    CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pPkg->cmd = pkg.cmd;
    pPkg->arg1 = pkg.arg1;
    pPkg->arg2 = pkg.arg2;
    
    // 填充数据
    if (!data.empty()) {
        memcpy(pPkg->data, data.c_str(), data.size());
    }
    
    return SendToClient(client, buffer.data(), (int)buffer.size());
}

// 发送模块到客户端
bool NetworkManager::SendModule(std::shared_ptr<ConnectedClient
    // 构建模块包
    size_t totalSize = sizeof(PkgHeader) + sizeof(CommandPkg) + moduleSize;
    std::vector<char> buffer(totalSize);
    
    // 填充包头
    PkgHeader* header = (PkgHeader*)buffer.data();
    memcpy(header->flag, "FRMD26?", 7);
    header->originLen = (int)(sizeof(CommandPkg) + moduleSize);
    header->totalLen = (int)totalSize;
    
    // 填充命令包
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = CMD_LOAD_MODULE;
    pkg->arg1 = moduleId;
    pkg->arg2 = moduleSize;
    
    // 填充模块数据
    memcpy(pkg->data, moduleData, moduleSize);
    
    return SendToClient(client, buffer.data(), (int)totalSize);
}

// 断开客户端
void NetworkManager::DisconnectClient(CONNID connId) {
    if (s_pServer) {
        s_pServer->Disconnect(connId);
    }
}

// HPSocket回调：客户端连接
void NetworkManager::OnConnect(CONNID connId, LPCTSTR lpszRemoteIP, USHORT usRemotePort) {
    std::string ip = StringHelper::WideToUTF8(lpszRemoteIP);
    
    // 通知上层
    if (s_onClientConnect) {
        s_onClientConnect(connId, ip, usRemotePort);
    }
}

// HPSocket回调：接收数据
void NetworkManager::OnReceive(CONNID connId, const BYTE* pData, int iLength) {
    // 追加到缓冲区
    {
        std::lock_guard<std::mutex> lock(s_recvBufferMutex);
        std::vector<BYTE>& buffer = s_recvBuffers[connId];
        buffer.insert(buffer.end(), pData, pData + iLength);
    }
    
    // 处理缓冲区中的完整数据包
    while (true) {
        std::vector<BYTE> buffer;
        {
            std::lock_guard<std::mutex> lock(s_recvBufferMutex);
            buffer = s_recvBuffers[connId];
        }
        
        if (buffer.size() < sizeof(PkgHeader)) {
            break;
        }
        
        PkgHeader* header = (PkgHeader*)buffer.data();
        
        // 验证包头标识
        if (memcmp(header->flag, "FRMD26?", 7) != 0) {
            // 包头损坏，清空缓冲区
            std::lock_guard<std::mutex> lock(s_recvBufferMutex);
            s_recvBuffers[connId].clear();
            break;
        }
        
        // 检查是否接收完整
        if (buffer.size() < (size_t)header->totalLen) {
            // 等待更多数据
            break;
        }
        
        // 提取完整数据包
        const BYTE* pkgData = buffer.data() + sizeof(PkgHeader);
        int pkgLen = header->originLen;
        
        // 通知上层处理
        if (s_onDataReceived) {
            s_onDataReceived(connId, pkgData, pkgLen);
        }
        
        // 从缓冲区移除已处理的数据包
        {
            std::lock_guard<std::mutex> lock(s_recvBufferMutex);
            s_recvBuffers[connId].erase(s_recvBuffers[connId].begin(), 
                                        s_recvBuffers[connId].begin() + header->totalLen);
        }
    }
}

// HPSocket回调：客户端断开
void NetworkManager::OnClose(CONNID connId) {
    // 通知上层
    if (s_onClientDisconnect) {
        s_onClientDisconnect(connId);
    }
    
    // 清理缓冲区
    {
        std::lock_guard<std::mutex> lock(s_recvBufferMutex);
        s_recvBuffers.erase(connId);
    }
}

} // namespace Network
} // namespace Formidable
