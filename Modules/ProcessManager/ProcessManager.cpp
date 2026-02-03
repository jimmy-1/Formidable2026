/**
 * Formidable2026 - ProcessManager Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include "../../Common/Config.h"
#include "../../Common/Utils.h"
#include <sddl.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#endif

using namespace Formidable;

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

void SendResponse(SOCKET s, uint32_t cmd, const void* data, int len) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = sizeof(CommandPkg) - 1 + len;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = len;
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
            
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProcess) {
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
    
    SendResponse(s, CMD_PROCESS_LIST, processes.data(), (int)(processes.size() * sizeof(ProcessInfo)));
}

void ListProcessModules(SOCKET s, uint32_t pid) {
    EnableDebugPrivilege();
    std::vector<ModuleInfo> modules;
    // 使用 TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 以获取 64 位和 32 位模块
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        SendResponse(s, CMD_PROCESS_MODULES, NULL, 0);
        return;
    }

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

    SendResponse(s, CMD_PROCESS_MODULES, modules.data(), (int)(modules.size() * sizeof(ModuleInfo)));
}

// KillProcess 已在 Utils.h 中声明并在 Utils.cpp 中实现，此处不再重复定义

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_LOAD_MODULE || pkg->cmd == CMD_PROCESS_LIST) {
        ListProcesses(s);
    } else if (pkg->cmd == CMD_PROCESS_MODULES) {
        ListProcessModules(s, pkg->arg2); // arg2 contains PID
    } else if (pkg->cmd == CMD_PROCESS_KILL) {
        EnableDebugPrivilege(); // 确保有权限
        // arg1 might be DLL size if loaded via MemoryModule, so use arg2 for PID
        bool ok = KillProcess(pkg->arg2);
        std::string msg = ok ? "进程已结束" : "结束进程失败";
        SendResponse(s, CMD_PROCESS_KILL, msg.c_str(), (int)msg.size());
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
