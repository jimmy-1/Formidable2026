/**
 * Formidable2026 - ProcessManager Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef FORMIDABLE_MODULE_DLL
#define FORMIDABLE_MODULE_DLL
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <map>
#include "../../Common/Config.h"
#include "../../Common/Utils.h"
#include <sddl.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#endif

using namespace Formidable;

// 用于计算 CPU 占用率的结构
struct CpuTimeInfo {
    FILETIME lastKernel;
    FILETIME lastUser;
    ULONGLONG lastTime;
};
static std::map<uint32_t, CpuTimeInfo> s_cpuHistory;

// 从 Formidable-Professional-Edition 移植的安全代码
// Check if the process is 64bit.
bool IsProcess64Bit(HANDLE hProcess, BOOL& is64Bit)
{
    is64Bit = FALSE;
    BOOL bWow64 = FALSE;
    typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS2)(HANDLE, USHORT*, USHORT*);
    HMODULE hKernel = GetModuleHandleA("kernel32.dll");

    LPFN_ISWOW64PROCESS2 fnIsWow64Process2 = hKernel ?
        (LPFN_ISWOW64PROCESS2)::GetProcAddress(hKernel, "IsWow64Process2") : nullptr;

    if (fnIsWow64Process2) {
        USHORT processMachine = 0, nativeMachine = 0;
        if (fnIsWow64Process2(hProcess, &processMachine, &nativeMachine)) {
            is64Bit = (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) &&
                      (nativeMachine == IMAGE_FILE_MACHINE_AMD64 || nativeMachine == IMAGE_FILE_MACHINE_ARM64);
            return true;
        }
    } else {
        // Old system use IsWow64Process
        if (IsWow64Process(hProcess, &bWow64)) {
            if (bWow64) {
                is64Bit = FALSE;    // WOW64 -> 32 bit
            } else {
#ifdef _WIN64
                is64Bit = TRUE;     // 64 bit app on 64 bit OS
#else
                is64Bit = FALSE;    // 32 bit app on 32 bit OS
#endif
            }
            return true;
        }
    }
    return false;
}

// 获取进程所有者
bool GetProcessOwner(HANDLE hProcess, std::string& owner) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        return false;
    }

    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
    if (dwSize == 0) {
        CloseHandle(hToken);
        return false;
    }

    std::vector<BYTE> buffer(dwSize);
    PTOKEN_USER pTokenUser = (PTOKEN_USER)buffer.data();
    if (GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
        wchar_t szName[256], szDomain[256];
        DWORD dwNameLen = 256, dwDomainLen = 256;
        SID_NAME_USE snu;
        if (LookupAccountSidW(NULL, pTokenUser->User.Sid, szName, &dwNameLen, szDomain, &dwDomainLen, &snu)) {
            owner = WideToUTF8(szName);
            CloseHandle(hToken);
            return true;
        }
    }

    CloseHandle(hToken);
    return false;
}

void SendResponse(SOCKET s, uint32_t cmd, uint32_t arg1, uint32_t arg2, const void* data, int len) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = sizeof(CommandPkg) - 1 + len;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = arg1;
    pkg->arg2 = arg2;
    if (len > 0 && data) {
        memcpy(pkg->data, data, len);
    }
    
    const char* pData = buffer.data();
    int remaining = (int)buffer.size();
    while (remaining > 0) {
        int sent = send(s, pData, remaining, 0);
        if (sent == SOCKET_ERROR) break;
        pData += sent;
        remaining -= sent;
    }
}

void ListProcesses(SOCKET s) {
    EnableDebugPrivilege(); // 使用 Utils.h 中的实现
    std::vector<ProcessInfo> processes;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            ProcessInfo info = { 0 };
            info.pid = pe32.th32ProcessID;
            info.threads = pe32.cntThreads;
            info.priority = pe32.pcPriClassBase;
            
            std::string utf8Name = WideToUTF8(pe32.szExeFile);
            strncpy(info.name, utf8Name.c_str(), sizeof(info.name) - 1);
            
            // 使用标准 OpenProcess
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);

            if (hProcess) {
                // 获取内存信息
                PROCESS_MEMORY_COUNTERS_EX pmc;
                if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                    info.workingSet = pmc.WorkingSetSize;
                }

                // 计算 CPU 占用率 (估算值)
                FILETIME ftCreate, ftExit, ftKernel, ftUser;
                SYSTEMTIME stNow;
                GetSystemTime(&stNow);
                FILETIME ftNow;
                SystemTimeToFileTime(&stNow, &ftNow);
                ULONGLONG qwNow = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;

                if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                    if (s_cpuHistory.count(pe32.th32ProcessID)) {
                        auto& history = s_cpuHistory[pe32.th32ProcessID];
                        ULONGLONG qwLastKernel = ((ULONGLONG)history.lastKernel.dwHighDateTime << 32) | history.lastKernel.dwLowDateTime;
                        ULONGLONG qwLastUser = ((ULONGLONG)history.lastUser.dwHighDateTime << 32) | history.lastUser.dwLowDateTime;
                        ULONGLONG qwKernel = ((ULONGLONG)ftKernel.dwHighDateTime << 32) | ftKernel.dwLowDateTime;
                        ULONGLONG qwUser = ((ULONGLONG)ftUser.dwHighDateTime << 32) | ftUser.dwLowDateTime;

                        ULONGLONG totalDelta = (qwKernel - qwLastKernel) + (qwUser - qwLastUser);
                        ULONGLONG timeDelta = qwNow - history.lastTime;
                        
                        if (timeDelta > 0) {
                            // 简单的近似计算
                            info.cpuUsage = (float)((totalDelta * 100.0) / timeDelta);
                            if (info.cpuUsage > 100.0f) info.cpuUsage = 100.0f;
                        }
                    }
                    
                    // 更新历史
                    CpuTimeInfo& history = s_cpuHistory[pe32.th32ProcessID];
                    history.lastKernel = ftKernel;
                    history.lastUser = ftUser;
                    history.lastTime = qwNow;
                }

                DWORD dwSize = MAX_PATH;
                wchar_t szPath[MAX_PATH] = { 0 };
                if (QueryFullProcessImageNameW(hProcess, 0, szPath, &dwSize)) {
                    std::string utf8Path = WideToUTF8(szPath);
                    strncpy(info.path, utf8Path.c_str(), sizeof(info.path) - 1);
                } else {
                    // 如果 QueryFullProcessImageNameW 失败，尝试 GetModuleFileNameExW
                    if (GetModuleFileNameExW(hProcess, NULL, szPath, MAX_PATH)) {
                        std::string utf8Path = WideToUTF8(szPath);
                        strncpy(info.path, utf8Path.c_str(), sizeof(info.path) - 1);
                    }
                }
                
                // 获取架构信息 (x86/x64)
                BOOL is64 = FALSE;
                if (IsProcess64Bit(hProcess, is64)) {
                    strncpy(info.arch, is64 ? "x64" : "x86", sizeof(info.arch) - 1);
                } else {
                    strncpy(info.arch, "Unk", sizeof(info.arch) - 1);
                }

                // 获取所有者
                std::string owner;
                if (GetProcessOwner(hProcess, owner)) {
                    strncpy(info.owner, owner.c_str(), sizeof(info.owner) - 1);
                } else {
                    strncpy(info.owner, "System/Service", sizeof(info.owner) - 1);
                }

                CloseHandle(hProcess);
            }

            if (strlen(info.path) == 0) {
                strncpy(info.path, info.name, sizeof(info.path) - 1);
            }
            
            processes.push_back(info);
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    // EnableDebugPrivilege(FALSE); // Utils 没有提供关闭的接口，通常开启后不需要关闭
    
    SendResponse(s, CMD_PROCESS_LIST, (uint32_t)(processes.size() * sizeof(ProcessInfo)), 0, processes.data(), (int)(processes.size() * sizeof(ProcessInfo)));
}

void ListProcessModules(SOCKET s, uint32_t pid) {
    std::vector<ModuleInfo> modules;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        // 尝试只用 TH32CS_SNAPMODULE
        hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    }
    
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me32;
        me32.dwSize = sizeof(MODULEENTRY32W);
        if (Module32FirstW(hSnapshot, &me32)) {
            do {
                ModuleInfo info = { 0 };
                info.baseAddr = (uint64_t)me32.modBaseAddr;
                info.size = me32.modBaseSize;

                std::string utf8Name = WideToUTF8(me32.szModule);
                std::string utf8Path = WideToUTF8(me32.szExePath);

                strncpy(info.name, utf8Name.c_str(), sizeof(info.name) - 1);
                strncpy(info.path, utf8Path.c_str(), sizeof(info.path) - 1);

                modules.push_back(info);
            } while (Module32NextW(hSnapshot, &me32));
        }
        CloseHandle(hSnapshot);
    }

    SendResponse(s, CMD_PROCESS_MODULES, (uint32_t)(modules.size() * sizeof(ModuleInfo)), pid, modules.data(), (int)(modules.size() * sizeof(ModuleInfo)));
}

void KillProcess(SOCKET s, uint32_t pid) {
    // 使用标准 OpenProcess
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);

    if (hProcess) {
        // 使用标准 TerminateProcess
        if (TerminateProcess(hProcess, 0)) {
            SendResponse(s, CMD_PROCESS_KILL, 1, pid, NULL, 0);
        } else {
            SendResponse(s, CMD_PROCESS_KILL, 0, pid, NULL, 0);
        }
        CloseHandle(hProcess);
    } else {
        SendResponse(s, CMD_PROCESS_KILL, 0, pid, NULL, 0);
    }
}

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_LOAD_MODULE || pkg->cmd == CMD_PROCESS_LIST) {
        ListProcesses(s);
    } else if (pkg->cmd == CMD_PROCESS_MODULES) {
        ListProcessModules(s, pkg->arg2); // arg2 contains PID
    } else if (pkg->cmd == CMD_PROCESS_KILL) {
        EnableDebugPrivilege(); // 确保有权限
        KillProcess(s, pkg->arg2);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
