#pragma once
#ifndef GLOBALSTATE_H
#define GLOBALSTATE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <mutex>
#include "resource.h"
#include "../Common/ClientTypes.h"
#include "../Common/NetworkServer.h"

// 全局常量和消息定义
#define WM_TRAYICON      (WM_USER + 100)
#define WM_LOC_UPDATE    (WM_USER + 101)

// 控件ID
#define IDC_LIST_CLIENTS 1001
#define IDC_LIST_LOGS    1002
#define IDC_TOOLBAR      1003
#define IDC_STATUSBAR    1004
#define IDC_GROUP_TAB    1005

// 托盘菜单 ID
#define IDM_TRAY_STARTUP 2014
#define IDM_TRAY_RESTART 2015
#define IDM_TRAY_EXIT    2016

// 全局变量声明
extern HINSTANCE g_hInstance;
extern HANDLE g_hInstanceMutex;
extern HWND g_hMainWnd;
extern HWND g_hListClients;
extern HWND g_hListLogs;
extern HWND g_hToolbar;
extern HWND g_hStatusBar;
extern HWND g_hGroupTab;
extern std::set<std::string> g_GroupList;
extern std::string g_selectedGroup;
extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern uint32_t g_NextClientId;
extern Formidable::NetworkServer* g_pNetworkServer;
extern std::map<CONNID, uint32_t> g_ConnIdToClientId;
extern std::mutex g_ConnIdMapMutex;
extern std::map<CONNID, std::vector<BYTE>> g_RecvBuffers;
extern std::mutex g_RecvBufferMutex;
extern int g_nListenPort;

// 终端专用资源
extern HFONT g_hTermFont;
extern HBRUSH g_hTermEditBkBrush;

// 配置结构
struct ServerSettings {
    int listenPort;
    wchar_t szConfigPath[MAX_PATH];
    
    // FRP 内网穿透设置
    bool bEnableFrp;
    wchar_t szFrpServer[128];
    int frpServerPort;
    wchar_t szFrpToken[64];
    int frpRemotePort;
    wchar_t szFrpProxyName[64];
    int frpDownloadPort; // 下载端口

    // 桌面管理设置
    int screenCaptureMethod; // 0=GDI, 1=DirectX
    int imageCompressMethod; // 0=JPEG, 1=PNG
    bool enableMultiMonitor; // 是否启用多显示器支持
    bool useDiffTransmission; // 是否使用差异传输
};
extern ServerSettings g_Settings;

// 窗口预览图缓存 (HWND -> HBITMAP)
extern std::map<HWND, HBITMAP> g_WindowPreviews;

extern std::map<HWND, Formidable::ListViewSortInfo> g_SortInfo;

// 预存的备注信息 (UniqueKey -> Remark)
extern std::map<std::wstring, std::wstring> g_SavedRemarks;
extern std::mutex g_SavedRemarksMutex;

// 历史主机信息
struct HistoryHost {
    uint64_t clientUniqueId = 0;
    std::wstring location;
    std::wstring ip;
    std::wstring computerName;
    std::wstring userName;
    std::wstring osVersion;
    std::wstring installTime;
    std::wstring lastSeen;
    std::wstring remark;
    std::wstring programPath;
};
extern std::map<std::wstring, HistoryHost> g_HistoryHosts; // uniqueKey -> HistoryHost
extern std::mutex g_HistoryHostsMutex;

// 排序回调
int CALLBACK ListViewCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
void ApplyModernTheme(HWND hWnd);
void ReleaseModernTheme();

#endif // GLOBALSTATE_H
