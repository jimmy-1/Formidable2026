/**
 * Formidable2026 - ServiceManager Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../../Common/Config.h"
#include "../../Common/Module.h"
#include "../../Common/Utils.h"

using namespace Formidable;

#include "../../Common/NetworkHelper.h"

namespace Formidable {
    ProtocolEncoder* g_pProtocolEncoder = nullptr;
}
using namespace Formidable;

void SendResponse(SOCKET s, uint32_t cmd, const void* data, int len) {
    SendPkg(s, cmd, data, len, 0, 0, g_pProtocolEncoder);
}

BOOL ControlServiceInternal(const char* utf8Name, DWORD dwControl) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return FALSE;
    
    std::wstring wName = UTF8ToWide(utf8Name);
    SC_HANDLE hService = OpenServiceW(hSCM, wName.c_str(), SERVICE_ALL_ACCESS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return FALSE;
    }

    BOOL bRet = FALSE;
    if (dwControl == SERVICE_CONTROL_STOP) {
        SERVICE_STATUS status;
        bRet = ControlService(hService, SERVICE_CONTROL_STOP, &status);
    } else if (dwControl == 0xFFFFFFFF) { // 自定义：启动服务
        bRet = StartServiceW(hService, 0, NULL);
    } else if (dwControl == 0xEEEEEEEE) { // 自定义：删除服务
        bRet = DeleteService(hService);
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return bRet;
}

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg, ProtocolEncoder* encoder) {
    g_pProtocolEncoder = encoder;
    if (pkg->cmd == CMD_SERVICE_LIST) {
        // arg1: 0=获取列表, 1=启动, 2=停止, 3=删除
        if (pkg->arg1 == 0) {
            std::stringstream ss;
            SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
            if (!hSCM) {
                std::string err = WideToUTF8(L"无法打开服务管理器");
                SendResponse(s, CMD_SERVICE_LIST, err.c_str(), (int)err.size());
                return;
            }
            
            DWORD bytesNeeded = 0;
            DWORD servicesReturned = 0;
            DWORD resumeHandle = 0;
            
            // 使用SERVICE_TYPE_ALL枚举所有服务类型（包括驱动程序）
            EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_TYPE_ALL, SERVICE_STATE_ALL, NULL, 0, &bytesNeeded, &servicesReturned, &resumeHandle, NULL);
            
            if (bytesNeeded == 0) {
                CloseServiceHandle(hSCM);
                SendResponse(s, CMD_SERVICE_LIST, "", 0);
                return;
            }

            std::vector<BYTE> buffer(bytesNeeded);
            if (!EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_TYPE_ALL, SERVICE_STATE_ALL, buffer.data(), bytesNeeded, &bytesNeeded, &servicesReturned, &resumeHandle, NULL)) {
                CloseServiceHandle(hSCM);
                std::string err = WideToUTF8(L"枚举服务失败");
                SendResponse(s, CMD_SERVICE_LIST, err.c_str(), (int)err.size());
                return;
            }

            LPENUM_SERVICE_STATUS_PROCESSW pServices = (LPENUM_SERVICE_STATUS_PROCESSW)buffer.data();
            for (DWORD i = 0; i < servicesReturned; i++) {
                // 格式: 服务名|显示名称|状态|启动类型|二进制路径|服务类型
                char szName[512] = {0};
                char szDisp[512] = {0};
                WideCharToMultiByte(CP_UTF8, 0, pServices[i].lpServiceName, -1, szName, sizeof(szName), NULL, NULL);
                WideCharToMultiByte(CP_UTF8, 0, pServices[i].lpDisplayName, -1, szDisp, sizeof(szDisp), NULL, NULL);
                
                // 获取服务类型
                DWORD serviceType = pServices[i].ServiceStatusProcess.dwServiceType;
                std::string typeStr = WideToUTF8(L"未知");
                if (serviceType & SERVICE_WIN32_OWN_PROCESS) typeStr = WideToUTF8(L"Win32独立进程");
                else if (serviceType & SERVICE_WIN32_SHARE_PROCESS) typeStr = WideToUTF8(L"Win32共享进程");
                else if (serviceType & SERVICE_KERNEL_DRIVER) typeStr = WideToUTF8(L"内核驱动");
                else if (serviceType & SERVICE_FILE_SYSTEM_DRIVER) typeStr = WideToUTF8(L"文件系统驱动");
                else if (serviceType & SERVICE_ADAPTER) typeStr = WideToUTF8(L"适配器");
                else if (serviceType & SERVICE_RECOGNIZER_DRIVER) typeStr = WideToUTF8(L"识别器驱动");
                
                // Open service to get config
                SC_HANDLE hService = OpenServiceW(hSCM, pServices[i].lpServiceName, SERVICE_QUERY_CONFIG);
                std::string startType = WideToUTF8(L"未知");
                std::string binaryPath = "";
                
                if (hService) {
                    DWORD bytesNeeded = 0;
                    QueryServiceConfigW(hService, NULL, 0, &bytesNeeded);
                    if (bytesNeeded > 0) {
                        std::vector<BYTE> cfgBuf(bytesNeeded);
                        LPQUERY_SERVICE_CONFIGW pConfig = (LPQUERY_SERVICE_CONFIGW)cfgBuf.data();
                        if (QueryServiceConfigW(hService, pConfig, bytesNeeded, &bytesNeeded)) {
                            switch (pConfig->dwStartType) {
                                case SERVICE_BOOT_START: startType = WideToUTF8(L"引导"); break;
                                case SERVICE_SYSTEM_START: startType = WideToUTF8(L"系统"); break;
                                case SERVICE_AUTO_START: startType = WideToUTF8(L"自动"); break;
                                case SERVICE_DEMAND_START: startType = WideToUTF8(L"手动"); break;
                                case SERVICE_DISABLED: startType = WideToUTF8(L"禁用"); break;
                            }
                            if (pConfig->lpBinaryPathName) {
                                char path[MAX_PATH * 2] = {0};
                                WideCharToMultiByte(CP_UTF8, 0, pConfig->lpBinaryPathName, -1, path, sizeof(path), NULL, NULL);
                                binaryPath = path;
                            }
                        }
                    }
                    CloseServiceHandle(hService);
                }

                int statusInt = pServices[i].ServiceStatusProcess.dwCurrentState;
                std::string statusStr = WideToUTF8(L"未知");
                switch (statusInt) {
                    case SERVICE_STOPPED: statusStr = WideToUTF8(L"已停止"); break;
                    case SERVICE_START_PENDING: statusStr = WideToUTF8(L"正在启动"); break;
                    case SERVICE_STOP_PENDING: statusStr = WideToUTF8(L"正在停止"); break;
                    case SERVICE_RUNNING: statusStr = WideToUTF8(L"正在运行"); break;
                    case SERVICE_CONTINUE_PENDING: statusStr = WideToUTF8(L"正在继续"); break;
                    case SERVICE_PAUSE_PENDING: statusStr = WideToUTF8(L"正在暂停"); break;
                    case SERVICE_PAUSED: statusStr = WideToUTF8(L"已暂停"); break;
                }

                // 添加服务类型到输出
                ss << szName << "|" << szDisp << "|" << statusStr << "|" << startType << "|" << binaryPath << "|" << typeStr << "\n";
            }
            CloseServiceHandle(hSCM);
            SendResponse(s, CMD_SERVICE_LIST, ss.str().c_str(), (int)ss.str().size());
        } else {
            const char* szName = pkg->data;
            BOOL bRet = FALSE;
            if (pkg->arg1 == 1) bRet = ControlServiceInternal(szName, 0xFFFFFFFF);
            else if (pkg->arg1 == 2) bRet = ControlServiceInternal(szName, SERVICE_CONTROL_STOP);
            else if (pkg->arg1 == 3) bRet = ControlServiceInternal(szName, 0xEEEEEEEE);
            
            SendResponse(s, CMD_SERVICE_LIST, bRet ? "OK" : "FAIL", bRet ? 2 : 4);
        }
    } else if (pkg->cmd == CMD_SERVICE_START) {
        BOOL bRet = ControlServiceInternal(pkg->data, 0xFFFFFFFF);
        SendResponse(s, CMD_SERVICE_START, bRet ? "OK" : "FAIL", bRet ? 2 : 4);
    } else if (pkg->cmd == CMD_SERVICE_STOP) {
        BOOL bRet = ControlServiceInternal(pkg->data, SERVICE_CONTROL_STOP);
        SendResponse(s, CMD_SERVICE_STOP, bRet ? "OK" : "FAIL", bRet ? 2 : 4);
    } else if (pkg->cmd == CMD_SERVICE_DELETE) {
        BOOL bRet = ControlServiceInternal(pkg->data, 0xEEEEEEEE);
        SendResponse(s, CMD_SERVICE_DELETE, bRet ? "OK" : "FAIL", bRet ? 2 : 4);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
