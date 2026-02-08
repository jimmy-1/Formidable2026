#include "Utils.h"
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <wininet.h>
#include <shlobj.h>
#include <vfw.h>
#include <sstream>
#include <ctime>
#include <vector>
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Wininet.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "vfw32.lib")
namespace Formidable {
    std::string WideToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }
    std::wstring UTF8ToWide(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }
    std::string GetOSVersion() {
        typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        HMODULE hMod = GetModuleHandleA("ntdll.dll");
        if (!hMod) return "Windows";

        RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (!pRtlGetVersion) return "Windows";

        RTL_OSVERSIONINFOEXW osvi = { 0 };
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        if (pRtlGetVersion((PRTL_OSVERSIONINFOW)&osvi) != 0) return "Windows";

        DWORD major = osvi.dwMajorVersion;
        DWORD minor = osvi.dwMinorVersion;
        DWORD build = osvi.dwBuildNumber;
        bool isServer = (osvi.wProductType != VER_NT_WORKSTATION);
        std::string vname;
        if (major == 10 && minor == 0) {
            if (isServer) {
                if (build >= 20348) vname = "Windows Server 2022";
                else if (build >= 17763) vname = "Windows Server 2019";
                else vname = "Windows Server 2016";
            } else {
                if (build >= 22000) vname = "Windows 11";
                else vname = "Windows 10";
            }
        } else if (major == 6) {
            switch (minor) {
                case 3: vname = isServer ? "Windows Server 2012 R2" : "Windows 8.1"; break;
                case 2: vname = isServer ? "Windows Server 2012" : "Windows 8"; break;
                case 1: vname = isServer ? "Windows Server 2008 R2" : "Windows 7"; break;
                case 0: vname = isServer ? "Windows Server 2008" : "Windows Vista"; break;
            }
        } else if (major == 5) {
            switch (minor) {
                case 2: vname = "Windows Server 2003"; break;
                case 1: vname = "Windows XP"; break;
                case 0: vname = "Windows 2000"; break;
            }
        }
        if (vname.empty()) {
            char buf[64];
            sprintf_s(buf, "Windows %d.%d (Build %d)", major, minor, build);
            vname = buf;
        }
        return vname;
    }
    int GetOSBits() {
        SYSTEM_INFO si;
        GetNativeSystemInfo(&si);
        return (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? 64 : 32;
    }
    int GetCPUCores() {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return (int)si.dwNumberOfProcessors;
    }
    double GetTotalMemoryGB() {
        MEMORYSTATUSEX mst;
        mst.dwLength = sizeof(mst);
        if (GlobalMemoryStatusEx(&mst)) {
            return (double)mst.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        }
        return 0.0;
    }
    std::string GetCpuBrand() {
        int cpuInfo[4] = { -1 };
        char cpuBrand[0x40];
        memset(cpuBrand, 0, sizeof(cpuBrand));
        
        __cpuid(cpuInfo, 0x80000002);
        memcpy(cpuBrand, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        memcpy(cpuBrand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        memcpy(cpuBrand + 32, cpuInfo, sizeof(cpuInfo));
        
        std::string brand = cpuBrand;
        // 去除首尾空格
        brand.erase(0, brand.find_first_not_of(" "));
        brand.erase(brand.find_last_not_of(" ") + 1);
        return brand;
    }
    std::string GetProcessStartTime() {
        FILETIME creationTime, exitTime, kernelTime, userTime;
        if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
            ULARGE_INTEGER ull;
            ull.LowPart = creationTime.dwLowDateTime;
            ull.HighPart = creationTime.dwHighDateTime;
            time_t startTime = (time_t)((ull.QuadPart / 10000000ULL) - 11644473600ULL);
            
            tm tstruct;
            localtime_s(&tstruct, &startTime);
            char buffer[64];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tstruct);
            return buffer;
        }
        return "Unknown";
    }
    std::string GetExeVersion() {
        char filePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, filePath, MAX_PATH) == 0) return "Unknown";
        DWORD handle = 0;
        DWORD verSize = GetFileVersionInfoSizeA(filePath, &handle);
        if (verSize == 0) return "1.0.0.0";
        std::vector<BYTE> verData(verSize);
        if (!GetFileVersionInfoA(filePath, handle, verSize, verData.data())) return "1.0.0.0";
        VS_FIXEDFILEINFO* pFileInfo = nullptr;
        UINT len = 0;
        if (VerQueryValueA(verData.data(), "\\", (LPVOID*)&pFileInfo, &len) && pFileInfo) {
            std::ostringstream oss;
            oss << HIWORD(pFileInfo->dwFileVersionMS) << "."
                << LOWORD(pFileInfo->dwFileVersionMS) << "."
                << HIWORD(pFileInfo->dwFileVersionLS) << "."
                << LOWORD(pFileInfo->dwFileVersionLS);
            return oss.str();
        }
        return "1.0.0.0";
    }
    std::string GetSystemUptime() {
        uint64_t uptimeMs = GetTickCount64();
        uint64_t seconds = uptimeMs / 1000;
        uint64_t days = seconds / (24 * 3600);
        seconds %= (24 * 3600);
        uint64_t hours = seconds / 3600;
        seconds %= 3600;
        uint64_t minutes = seconds / 60;
        seconds %= 60;

        char buf[128];
        if (days > 0)
            sprintf_s(buf, "%llu%s %llu:%02llu:%02llu", days, WideToUTF8(L"天").c_str(), hours, minutes, seconds);
        else
            sprintf_s(buf, "%llu:%02llu:%02llu", hours, minutes, seconds);
        return buf;
    }
    std::string GetLocalIP() {
        ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ULONG bufLen = 15000;
        std::vector<unsigned char> buffer(bufLen);
        IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)buffer.data();
        ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL, addrs, &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(bufLen);
            addrs = (IP_ADAPTER_ADDRESSES*)buffer.data();
            ret = GetAdaptersAddresses(AF_INET, flags, NULL, addrs, &bufLen);
        }
        if (ret == NO_ERROR) {
            for (IP_ADAPTER_ADDRESSES* p = addrs; p; p = p->Next) {
                if (p->OperStatus != IfOperStatusUp) continue;
                if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                for (IP_ADAPTER_UNICAST_ADDRESS* ua = p->FirstUnicastAddress; ua; ua = ua->Next) {
                    if (!ua->Address.lpSockaddr) continue;
                    if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
                    sockaddr_in* sin = (sockaddr_in*)ua->Address.lpSockaddr;
                    char ip[INET_ADDRSTRLEN] = { 0 };
                    inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                    std::string ipStr = ip;
                    if (!ipStr.empty() && ipStr != "127.0.0.1" && ipStr.rfind("169.254.", 0) != 0) {
                        return ipStr;
                    }
                }
            }
        }

        char host[256];
        if (gethostname(host, sizeof(host)) == SOCKET_ERROR) return "127.0.0.1";
        struct addrinfo hints = { 0 }, * res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, NULL, &hints, &res) != 0) return "127.0.0.1";
        
        char ip[INET_ADDRSTRLEN] = "127.0.0.1";
        for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
            void* addr = &((struct sockaddr_in*)p->ai_addr)->sin_addr;
            inet_ntop(p->ai_family, addr, ip, sizeof(ip));
            if (strcmp(ip, "127.0.0.1") != 0) break;
        }
        freeaddrinfo(res);
        return ip;
    }
    std::string GetLocationByIP(const std::string& ip) {
        std::string location = WideToUTF8(L"未知");
        HINTERNET hInternet = InternetOpenA("Formidable2026", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInternet) {
            std::string url = "http://ip-api.com/json/" + ip + "?fields=status,country,regionName,city&lang=zh-CN";
            HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
            if (hUrl) {
                char buffer[1024];
                DWORD bytesRead = 0;
                if (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead)) {
                    buffer[bytesRead] = '\0';
                    std::string response = buffer;
                    // 简单的 JSON 解析 (提取 country, regionName, city)
                    auto get_val = [&](const std::string& key) {
                        size_t pos = response.find("\"" + key + "\":\"");
                        if (pos != std::string::npos) {
                            pos += key.length() + 4;
                            size_t end = response.find("\"", pos);
                            if (end != std::string::npos) return response.substr(pos, end - pos);
                        }
                        return std::string("");
                    };
                    std::string country = get_val("country");
                    std::string region = get_val("regionName");
                    std::string city = get_val("city");
                    if (!country.empty()) {
                        location = country;
                        if (!region.empty()) location += " " + region;
                        if (!city.empty()) location += " " + city;
                    }
                }
                InternetCloseHandle(hUrl);
            }
            InternetCloseHandle(hInternet);
        }
        return location;
    }

    // 异常方式辅助函数，避免 C2712 错误 (SEH 与 C++ 析构对象冲突)
    static void SetThreadNameExceptionHelper(DWORD dwThreadID, const char* szName) {
        const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push, 8)
        typedef struct tagTHREADNAME_INFO {
            DWORD dwType;     // Must be 0x1000.
            LPCSTR szName;    // Pointer to name (in user addr space).
            DWORD dwThreadID; // Thread ID (-1=caller thread).
            DWORD dwFlags;    // Reserved for future use, must be zero.
        } THREADNAME_INFO;
#pragma pack(pop)

        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = szName;
        info.dwThreadID = dwThreadID;
        info.dwFlags = 0;

        __try {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void SetThreadName(HANDLE hThread, const std::wstring& name) {
        // 使用异常方式设置线程名称 (兼容所有 Windows 版本)
        std::string nameAnsi = WideToUTF8(name);
        SetThreadNameExceptionHelper(GetThreadId(hThread), nameAnsi.c_str());
    }

    void SetThreadName(const std::wstring& name) {
        SetThreadName(GetCurrentThread(), name);
    }

    bool EnableDebugPrivilege() {
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
        
        LUID luid;
        if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            CloseHandle(hToken);
            return false;
        }
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        bool result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        CloseHandle(hToken);
        return result && GetLastError() == ERROR_SUCCESS;
    }
    bool IsAdmin() {
        BOOL bIsAdmin = FALSE;
        PSID pAdministratorsGroup = NULL;
        SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
        
        // 1. 检查是否在管理员组
        if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
            CheckTokenMembership(NULL, pAdministratorsGroup, &bIsAdmin);
            FreeSid(pAdministratorsGroup);
        }
        
        if (bIsAdmin) return true;

        // 2. 检查是否为 SYSTEM 账户
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            DWORD dwSize = 0;
            GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                PTOKEN_USER pTokenUser = (PTOKEN_USER)malloc(dwSize);
                if (pTokenUser && GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
                    PSID pLocalSystemSid = NULL;
                    if (AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pLocalSystemSid)) {
                        if (EqualSid(pTokenUser->User.Sid, pLocalSystemSid)) {
                            bIsAdmin = TRUE;
                        }
                        FreeSid(pLocalSystemSid);
                    }
                }
                if (pTokenUser) free(pTokenUser);
            }
            CloseHandle(hToken);
        }

        return bIsAdmin == TRUE;
    }

    bool SelfElevate() {
        if (IsAdmin()) return false;

        wchar_t szPath[MAX_PATH];
        if (GetModuleFileNameW(NULL, szPath, MAX_PATH)) {
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = szPath;
            sei.hwnd = NULL;
            sei.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&sei)) {
                return true; // 启动了新进程，当前进程应该退出
            }
        }
        return false; // 提权失败
    }

    std::string ActivityMonitor::GetStatus() {
        DWORD idle = GetUserIdleTime();
        if (idle > 60000) { // 1分钟无操作
            return (IsWorkstationLocked() ? WideToUTF8(L"[已锁屏] ") : WideToUTF8(L"[离开] ")) + GetActiveWindowTitle();
        }
        return GetActiveWindowTitle();
    }
    DWORD ActivityMonitor::GetUserIdleTime() {
        LASTINPUTINFO lii = { sizeof(LASTINPUTINFO) };
        if (GetLastInputInfo(&lii)) {
            return (DWORD)(GetTickCount64() - lii.dwTime);
        }
        return 0;
    }
    bool ActivityMonitor::IsWorkstationLocked() {
        HDESK hInput = OpenInputDesktop(0, FALSE, GENERIC_READ);
        if (!hInput) return true;
        
        char name[256] = { 0 };
        DWORD needed;
        bool isLocked = false;
        if (GetUserObjectInformationA(hInput, UOI_NAME, name, sizeof(name), &needed)) {
            isLocked = (_stricmp(name, "Winlogon") == 0);
        }
        CloseDesktop(hInput);
        return isLocked;
    }
    std::string ActivityMonitor::GetActiveWindowTitle() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return "No Active Window";
        wchar_t title[256];
        if (GetWindowTextW(hwnd, title, 256)) {
            return WideToUTF8(title);
        }
        return "Unknown Window";
    }
    // --- 进程管理增强 ---
    std::vector<ProcessDetail> GetDetailedProcessList() {
        std::vector<ProcessDetail> list;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return list;
        PROCESSENTRY32W pe32 = { sizeof(pe32) };
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                ProcessDetail detail;
                detail.pid = pe32.th32ProcessID;
                detail.parentPid = pe32.th32ParentProcessID;
                detail.threads = pe32.cntThreads;
                detail.name = WideToUTF8(pe32.szExeFile);
                detail.is64Bit = false;
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, detail.pid);
                if (hProcess) {
                    wchar_t path[MAX_PATH];
                    DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                        detail.path = WideToUTF8(path);
                    }
                    BOOL isWow64 = FALSE;
                    if (IsWow64Process(hProcess, &isWow64)) {
                        if (isWow64) {
                            detail.is64Bit = false;
                        } else {
                            SYSTEM_INFO si;
                            GetNativeSystemInfo(&si);
                            detail.is64Bit = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);
                        }
                    }
                    CloseHandle(hProcess);
                }
                list.push_back(detail);
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
        return list;
    }
    bool KillProcess(uint32_t pid) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!hProcess) return false;
        bool result = TerminateProcess(hProcess, 0) != FALSE;
        CloseHandle(hProcess);
        return result;
    }
    // --- 窗口管理增强 ---
    struct EnumWindowsContext {
        std::vector<WindowDetail>* list;
    };
    BOOL CALLBACK InternalEnumWindowsProc(HWND hWnd, LPARAM lParam) {
        EnumWindowsContext* context = (EnumWindowsContext*)lParam;
        
        wchar_t title[512];
        GetWindowTextW(hWnd, title, 512);
        if (wcslen(title) == 0) return TRUE;
        WindowDetail detail;
        detail.hwnd = (uint64_t)hWnd;
        detail.title = WideToUTF8(title);
        detail.isVisible = IsWindowVisible(hWnd) != FALSE;
        
        wchar_t className[256];
        GetClassNameW(hWnd, className, 256);
        detail.className = WideToUTF8(className);
        GetWindowThreadProcessId(hWnd, (LPDWORD)&detail.pid);
        if (IsIconic(hWnd)) detail.status = "minimized";
        else if (IsZoomed(hWnd)) detail.status = "maximized";
        else detail.status = "normal";
        context->list->push_back(detail);
        return TRUE;
    }
    std::vector<WindowDetail> GetDetailedWindowList() {
        std::vector<WindowDetail> list;
        EnumWindowsContext context = { &list };
        EnumWindows(InternalEnumWindowsProc, (LPARAM)&context);
        return list;
    }
    // --- 网络连接信息 ---
    std::vector<NetConnection> GetNetConnections() {
        std::vector<NetConnection> list;
        
        // TCP
        ULONG size = 0;
        GetExtendedTcpTable(NULL, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        std::vector<BYTE> buffer(size);
        if (GetExtendedTcpTable(buffer.data(), &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            PMIB_TCPTABLE_OWNER_PID pTcpTable = (PMIB_TCPTABLE_OWNER_PID)buffer.data();
            for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++) {
                NetConnection conn;
                conn.type = "TCP";
                conn.pid = pTcpTable->table[i].dwOwningPid;
                
                char addr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &pTcpTable->table[i].dwLocalAddr, addr, INET_ADDRSTRLEN);
                conn.localAddr = addr;
                conn.localPort = ntohs((u_short)pTcpTable->table[i].dwLocalPort);
                inet_ntop(AF_INET, &pTcpTable->table[i].dwRemoteAddr, addr, INET_ADDRSTRLEN);
                conn.remoteAddr = addr;
                conn.remotePort = ntohs((u_short)pTcpTable->table[i].dwRemotePort);
                switch (pTcpTable->table[i].dwState) {
                    case MIB_TCP_STATE_CLOSED: conn.state = "CLOSED"; break;
                    case MIB_TCP_STATE_LISTEN: conn.state = "LISTENING"; break;
                    case MIB_TCP_STATE_SYN_SENT: conn.state = "SYN_SENT"; break;
                    case MIB_TCP_STATE_SYN_RCVD: conn.state = "SYN_RCVD"; break;
                    case MIB_TCP_STATE_ESTAB: conn.state = "ESTABLISHED"; break;
                    case MIB_TCP_STATE_FIN_WAIT1: conn.state = "FIN_WAIT1"; break;
                    case MIB_TCP_STATE_FIN_WAIT2: conn.state = "FIN_WAIT2"; break;
                    case MIB_TCP_STATE_CLOSE_WAIT: conn.state = "CLOSE_WAIT"; break;
                    case MIB_TCP_STATE_CLOSING: conn.state = "CLOSING"; break;
                    case MIB_TCP_STATE_LAST_ACK: conn.state = "LAST_ACK"; break;
                    case MIB_TCP_STATE_TIME_WAIT: conn.state = "TIME_WAIT"; break;
                    case MIB_TCP_STATE_DELETE_TCB: conn.state = "DELETE_TCB"; break;
                    default: conn.state = "UNKNOWN"; break;
                }
                list.push_back(conn);
            }
        }
        // UDP
        size = 0;
        GetExtendedUdpTable(NULL, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        buffer.assign(size, 0);
        if (GetExtendedUdpTable(buffer.data(), &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            PMIB_UDPTABLE_OWNER_PID pUdpTable = (PMIB_UDPTABLE_OWNER_PID)buffer.data();
            for (DWORD i = 0; i < pUdpTable->dwNumEntries; i++) {
                NetConnection conn;
                conn.type = "UDP";
                conn.pid = pUdpTable->table[i].dwOwningPid;
                conn.state = "-";
                char addr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &pUdpTable->table[i].dwLocalAddr, addr, INET_ADDRSTRLEN);
                conn.localAddr = addr;
                conn.localPort = ntohs((u_short)pUdpTable->table[i].dwLocalPort);
                conn.remoteAddr = "*";
                conn.remotePort = 0;
                list.push_back(conn);
            }
        }
        return list;
    }
    // --- 服务管理 ---
    std::vector<ServiceDetail> GetServiceList() {
        std::vector<ServiceDetail> list;
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
        if (!hSCM) return list;
        DWORD bytesNeeded = 0;
        DWORD servicesCount = 0;
        DWORD resumeHandle = 0;
        EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, 
            NULL, 0, &bytesNeeded, &servicesCount, &resumeHandle, NULL);
        std::vector<BYTE> buffer(bytesNeeded);
        if (EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, 
            buffer.data(), bytesNeeded, &bytesNeeded, &servicesCount, &resumeHandle, NULL)) {
            
            LPENUM_SERVICE_STATUS_PROCESSW pServices = (LPENUM_SERVICE_STATUS_PROCESSW)buffer.data();
            for (DWORD i = 0; i < servicesCount; i++) {
                ServiceDetail detail;
                detail.name = WideToUTF8(pServices[i].lpServiceName);
                detail.displayName = WideToUTF8(pServices[i].lpDisplayName);
                detail.pid = pServices[i].ServiceStatusProcess.dwProcessId;
                switch (pServices[i].ServiceStatusProcess.dwCurrentState) {
                    case SERVICE_STOPPED: detail.status = "Stopped"; break;
                    case SERVICE_START_PENDING: detail.status = "Starting"; break;
                    case SERVICE_STOP_PENDING: detail.status = "Stopping"; break;
                    case SERVICE_RUNNING: detail.status = "Running"; break;
                    case SERVICE_CONTINUE_PENDING: detail.status = "Continuing"; break;
                    case SERVICE_PAUSE_PENDING: detail.status = "Pausing"; break;
                    case SERVICE_PAUSED: detail.status = "Paused"; break;
                    default: detail.status = "Unknown"; break;
                }
                SC_HANDLE hService = OpenServiceW(hSCM, pServices[i].lpServiceName, SERVICE_QUERY_CONFIG);
                if (hService) {
                    DWORD configSize = 0;
                    QueryServiceConfigW(hService, NULL, 0, &configSize);
                    std::vector<BYTE> configBuffer(configSize);
                    if (QueryServiceConfigW(hService, (LPQUERY_SERVICE_CONFIGW)configBuffer.data(), configSize, &configSize)) {
                        LPQUERY_SERVICE_CONFIGW pConfig = (LPQUERY_SERVICE_CONFIGW)configBuffer.data();
                        switch (pConfig->dwStartType) {
                            case SERVICE_AUTO_START: detail.startType = "Auto"; break;
                            case SERVICE_DEMAND_START: detail.startType = "Manual"; break;
                            case SERVICE_DISABLED: detail.startType = "Disabled"; break;
                            case SERVICE_BOOT_START: detail.startType = "Boot"; break;
                            case SERVICE_SYSTEM_START: detail.startType = "System"; break;
                            default: detail.startType = "Unknown"; break;
                        }
                    }
                    CloseHandle(hService);
                }
                list.push_back(detail);
            }
        }
        CloseHandle(hSCM);
        return list;
    }
    // --- 自动启动管理 ---
    bool AddToStartup(const std::string& name, const std::string& path) {
        HKEY hKey;
        std::wstring wName = UTF8ToWide(name);
        std::wstring wPath = UTF8ToWide(path);
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            LSTATUS status = RegSetValueExW(hKey, wName.c_str(), 0, REG_SZ, (const BYTE*)wPath.c_str(), (DWORD)((wPath.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }
        return false;
    }
    bool RemoveFromStartup(const std::string& name) {
        HKEY hKey;
        std::wstring wName = UTF8ToWide(name);
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            LSTATUS status = RegDeleteValueW(hKey, wName.c_str());
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }
        return false;
    }

    // --- 硬件与软件检测 ---
    bool CheckCameraExistence() {
        char szDeviceName[80];
        char szDeviceVersion[80];
        for (int i = 0; i < 10; i++) {
            if (capGetDriverDescriptionA(i, szDeviceName, sizeof(szDeviceName), szDeviceVersion, sizeof(szDeviceVersion))) {
                return true;
            }
        }
        return false;
    }

    bool CheckTelegramInstalled() {
        // Check Registry (Uninstall key)
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char subKeyName[256];
            DWORD subKeyLen = 256;
            DWORD index = 0;
            while (RegEnumKeyExA(hKey, index++, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                if (strstr(subKeyName, "Telegram Desktop") != NULL) {
                    RegCloseKey(hKey);
                    return true;
                }
                subKeyLen = 256;
            }
            RegCloseKey(hKey);
        }

        // Check AppData path
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
            std::string tPath = std::string(path) + "\\Telegram Desktop\\Telegram.exe";
            if (GetFileAttributesA(tPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return true;
            }
        }
        return false;
    }

    std::wstring ExpandPath(const std::wstring& path) {
        wchar_t expanded[MAX_PATH];
        ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);
        return expanded;
    }

    uint64_t Fnv1a64(const void* data, size_t len) {
        const unsigned char* p = (const unsigned char*)data;
        uint64_t hash = 1469598103934665603ULL;
        for (size_t i = 0; i < len; ++i) {
            hash ^= (uint64_t)p[i];
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    uint64_t GetStableClientUniqueId(uint64_t configId) {
        if (configId != 0) return configId;

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

    bool IsUserSessionActive() {
        #include <wtsapi32.h>
        #pragma comment(lib, "wtsapi32.lib")
        WTS_SESSION_INFOW* pSessions = NULL;
        DWORD count = 0;
        if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessions, &count)) {
            for (DWORD i = 0; i < count; i++) {
                if (pSessions[i].SessionId != 0 && pSessions[i].State == WTSActive) {
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

}
