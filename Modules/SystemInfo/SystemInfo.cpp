/**
 * Formidable2026 - SystemInfo Module (DLL)
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
#include "../../Common/Config.h"
#include "../../Common/Module.h"
#include <iomanip>

using namespace Formidable;

#include "../../Common/Utils.h"

// Helper to format bytes
std::string FormatBytes(uint64_t bytes) {
    const char* suffixes[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    double dblBytes = (double)bytes;
    while (dblBytes > 1024 && i < 4) {
        dblBytes /= 1024;
        i++;
    }
    char buf[64];
    sprintf_s(buf, "%.2f %s", dblBytes, suffixes[i]);
    return std::string(buf);
}

// Helper to get uptime
std::string GetUptime() {
    uint64_t tick = GetTickCount64();
    uint64_t seconds = tick / 1000;
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;
    uint64_t days = hours / 24;
    
    char buf[128];
    sprintf_s(buf, "%llu %s %llu %s %llu %s", 
        days, WideToUTF8(L"天").c_str(), 
        hours % 24, WideToUTF8(L"小时").c_str(), 
        minutes % 60, WideToUTF8(L"分钟").c_str());
    return std::string(buf);
}

void SendResponse(SOCKET s, const std::string& data) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    
    // Construct CommandPkg
    size_t bodySize = sizeof(CommandPkg) - 1 + data.size() + 1; // +1 for null terminator
    header.originLen = (int)bodySize;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = CMD_GET_SYSINFO;
    pkg->arg1 = (uint32_t)data.size();
    memcpy(pkg->data, data.c_str(), data.size() + 1);
    
    send(s, buffer.data(), (int)buffer.size(), 0);
}
std::string CollectSystemInfo() {
    std::stringstream ss;
    wchar_t buffer[MAX_PATH];
    DWORD size = sizeof(buffer) / sizeof(wchar_t);
    if (GetComputerNameW(buffer, &size)) ss << WideToUTF8(L"计算机名: ") << WideToUTF8(buffer) << "\r\n";
    size = sizeof(buffer) / sizeof(wchar_t);
    if (GetUserNameW(buffer, &size)) ss << WideToUTF8(L"当前用户: ") << WideToUTF8(buffer) << "\r\n";
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    ss << WideToUTF8(L"处理器核心数: ") << sysInfo.dwNumberOfProcessors << "\r\n";
    ss << WideToUTF8(L"架构: ") << (sysInfo.wProcessorArchitecture == 9 ? "x64" : "x86") << "\r\n";
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        ss << WideToUTF8(L"物理内存总计: ") << memStatus.ullTotalPhys / (1024 * 1024) << " MB\r\n";
        ss << WideToUTF8(L"物理内存可用: ") << memStatus.ullAvailPhys / (1024 * 1024) << " MB\r\n";
    }
    // 获取系统版本
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    typedef LONG (WINAPI* PFN_RtlGetVersion)(POSVERSIONINFOEXW);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        PFN_RtlGetVersion pRtlGetVersion = (PFN_RtlGetVersion)GetProcAddress(hNtdll, "RtlGetVersion");
        if (pRtlGetVersion && pRtlGetVersion(&osvi) == 0) {
            ss << WideToUTF8(L"操作系统版本: ") << osvi.dwMajorVersion << "." << osvi.dwMinorVersion << " (Build " << osvi.dwBuildNumber << ")\r\n";
        }
    }

    ss << WideToUTF8(L"系统运行时间: ") << GetUptime() << "\r\n";
    
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    ss << WideToUTF8(L"屏幕分辨率: ") << cx << " x " << cy << "\r\n";

    ss << "\r\n" << WideToUTF8(L"[磁盘信息]") << "\r\n";
    wchar_t drives[256];
    if (GetLogicalDriveStringsW(sizeof(drives)/sizeof(wchar_t), drives)) {
        wchar_t* drive = drives;
        while (*drive) {
            ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
            if (GetDiskFreeSpaceExW(drive, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
                ss << WideToUTF8(drive) << WideToUTF8(L" 总大小: ") << FormatBytes(totalNumberOfBytes.QuadPart) 
                   << WideToUTF8(L", 可用: ") << FormatBytes(freeBytesAvailable.QuadPart) << "\r\n";
            }
            drive += wcslen(drive) + 1;
        }
    }

    return ss.str();
}
// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_GET_SYSINFO) {
        std::string info = CollectSystemInfo();
        SendResponse(s, info);
    }
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
