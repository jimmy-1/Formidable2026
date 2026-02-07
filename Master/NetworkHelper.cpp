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
#include "Network/NetworkManager.h"
#include "GlobalState.h"
#include "MainWindow.h"
#include "Core/CommandHandler.h"
#include "resource.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
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
    return Formidable::Network::NetworkManager::SendToClient(client, pData, iLength);
}

// 发送模块 DLL
bool SendModuleToClient(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2) {
    return Formidable::Network::NetworkManager::SendModule(clientId, cmd, dllName, arg2);
}

bool SendSimpleCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1, uint32_t arg2, const std::string& data) {
    return Formidable::Network::NetworkManager::SendCommand(clientId, cmd, arg1, arg2, data);
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
            if (client && client->active) {
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
    // 此函数内容已移至 NetworkManager::Initialize
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