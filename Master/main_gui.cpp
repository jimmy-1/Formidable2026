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
#include <commctrl.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <string>
#include <thread>
#include <ctime>
#include "resource.h"
#include "GlobalState.h"
#include "Config.h"
#include "MainWindow.h"
#include "Network/NetworkManager.h"
#include "NetworkHelper.h"
#include "Server/Network/AsyncNetworkManager.h"
#include "Server/Network/ConnectionPool.h"
#include "Server/Performance/MemoryManager.h"
#include "Server/Performance/ConcurrentProcessor.h"
#include "Server/Utils/Logger.h"

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

static std::string WideToUtf8String(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (len <= 0) return std::string();
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len, NULL, NULL);
    return out;
}

static std::wstring GetWin32ErrorMessage(DWORD errorCode) {
    if (errorCode == 0) return L"";
    LPWSTR buffer = NULL;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buffer,
        0,
        NULL
    );
    std::wstring msg;
    if (len > 0 && buffer) {
        msg.assign(buffer, buffer + len);
        LocalFree(buffer);
    }
    return msg;
}

/**
 * Formidable2026 - Master GUI Bootstrapper
 * Encoding: UTF-8 BOM
 */

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    try {
        g_hInstance = hInstance;
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "WinMain start");
    
    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBoxW(NULL, L"Winsock 初始化失败", L"错误", MB_OK | MB_ICONERROR);
        return 0;
    }
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "WSAStartup ok");

    // Initialize Optimization Components (Phase 1)
    Formidable::Server::Performance::MemoryManager::InitializeMemoryPool();
    Formidable::Server::Network::AsyncNetworkManager::InitializeAsyncIO();
    Formidable::Server::Network::ConnectionPool::InitializePool(10000);
    Formidable::Server::Utils::Logger::Initialize("server.log", true);
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "Phase1 init ok");

    // Initialize Optimization Components (Phase 2)
    try {
        Formidable::Server::Performance::ConcurrentProcessor::GetInstance().Initialize();
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "Phase2 init ok");
    } catch (...) {
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_ERROR, "ConcurrentProcessor init failed");
    }

    // Start Network Event Loop in background
    try {
        std::thread networkThread([](){
            Formidable::Server::Network::AsyncNetworkManager::HandleAsyncIO();
        });
        networkThread.detach();
    } catch (...) {
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_ERROR, "AsyncNetworkManager thread start failed");
    }

    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    LoadSettings();
    {
        std::string cfg = WideToUtf8String(g_Settings.szConfigPath);
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "ConfigPath: " + cfg);
    }
    
    g_hInstanceMutex = CreateMutexW(NULL, FALSE, L"Formidable2026_Master_Instance");
    if (g_hInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"FormidableMasterGUI", NULL);
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_WARNING, "Instance already exists");
        if (hExisting) {
            if (IsHungAppWindow(hExisting)) {
                MessageBoxW(NULL, L"检测到已运行的实例无响应。请打开任务管理器结束 FormidableMaster.exe 进程后重试。", L"错误", MB_OK | MB_ICONERROR);
            } else {
                ShowWindow(hExisting, SW_RESTORE);
                SetForegroundWindow(hExisting);
            }
            Gdiplus::GdiplusShutdown(gdiplusToken);
            CloseHandle(g_hInstanceMutex);
            g_hInstanceMutex = NULL;
            return 0;
        }
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
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"FormidableMasterGUI";
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
    if (!RegisterClassExW(&wcex)) {
        DWORD err = GetLastError();
        std::wstring msg = L"窗口类注册失败: " + std::to_wstring(err) + L" " + GetWin32ErrorMessage(err);
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_ERROR, WideToUtf8String(msg));
        MessageBoxW(NULL, msg.c_str(), L"错误", MB_OK | MB_ICONERROR);
        if (g_hInstanceMutex) CloseHandle(g_hInstanceMutex);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        Formidable::Server::Utils::Logger::Shutdown();
        WSACleanup();
        return 0;
    }
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "RegisterClassEx ok");

    g_hMainWnd = CreateWindowExW(0, L"FormidableMasterGUI", L"Formidable 2026 - Professional Edition", 
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800, NULL, NULL, hInstance, NULL);
    
    if (!g_hMainWnd) {
        DWORD err = GetLastError();
        std::wstring msg = L"主窗口创建失败: " + std::to_wstring(err) + L" " + GetWin32ErrorMessage(err);
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_ERROR, WideToUtf8String(msg));
        MessageBoxW(NULL, msg.c_str(), L"错误", MB_OK | MB_ICONERROR);
        if (g_hInstanceMutex) CloseHandle(g_hInstanceMutex);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        Formidable::Server::Utils::Logger::Shutdown();
        WSACleanup();
        return 0;
    }
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "CreateWindowEx ok");

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);
    AddTrayIcon(g_hMainWnd);
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "ShowWindow ok");

    // 启动网络和心跳线程
    try {
        std::thread([]() {
            Formidable::Network::NetworkManager::Initialize(g_Settings.listenPort);
        }).detach();
    } catch (...) {
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_ERROR, "NetworkManager thread start failed");
    }
    std::thread(HeartbeatThread).detach();

    AddLog(L"系统", L"主程序启动，监听端口: " + std::to_wstring(g_Settings.listenPort));
    UpdateStatusBar();
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "Enter message loop");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hInstanceMutex) CloseHandle(g_hInstanceMutex);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    Formidable::Server::Utils::Logger::Shutdown();
    WSACleanup();
    return (int)msg.wParam;

    } catch (const std::exception& e) {
        std::string err = "发生未捕获的异常: ";
        err += e.what();
        MessageBoxA(NULL, err.c_str(), "严重错误", MB_OK | MB_ICONERROR);
        return -1;
    } catch (...) {
        MessageBoxW(NULL, L"发生未知的严重错误", L"严重错误", MB_OK | MB_ICONERROR);
        return -1;
    }
}


// ============================================================================
// 模块化迁移说明：
// - WinMain: 应用程序入口点，负责初始化和主循环。
// - WndProc: 主窗口过程，已移至 MainWindow.cpp
// - NetworkThread: 网络监听线程，已移至 NetworkHelper.cpp
// - 对话框过程 (DlgProc): 已移至 Dialogs/ 和 UI/ 目录下的对应模块。
// - 全局状态: 定义在 GlobalState.h / GlobalState.cpp
// ============================================================================


// RefreshLocalFileList implementation moved to UI/FileManagerUI.cpp



