#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <ctime>

namespace Formidable {
namespace Utils {

class ResourceHelper {
public:
    // 从资源加载数据
    static bool LoadResource(HINSTANCE hInstance, int resourceId, const char* resourceType, 
                            std::vector<BYTE>& outData) {
        HRSRC hRes = FindResourceA(hInstance, MAKEINTRESOURCEA(resourceId), resourceType);
        if (!hRes) return false;
        
        HGLOBAL hGlobal = ::LoadResource(hInstance, hRes);
        if (!hGlobal) return false;
        
        DWORD size = SizeofResource(hInstance, hRes);
        void* pData = LockResource(hGlobal);
        if (!pData || size == 0) return false;
        
        outData.resize(size);
        memcpy(outData.data(), pData, size);
        
        return true;
    }
    
    // 加载图标
    static HICON LoadIconResource(HINSTANCE hInstance, int iconId) {
        return (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(iconId), IMAGE_ICON, 
                                 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    }
    
    // 加载位图
    static HBITMAP LoadBitmapResource(HINSTANCE hInstance, int bitmapId) {
        return (HBITMAP)LoadImageW(hInstance, MAKEINTRESOURCEW(bitmapId), IMAGE_BITMAP, 
                                   0, 0, LR_DEFAULTSIZE | LR_SHARED);
    }
};

class FileHelper {
public:
    // 检查文件是否存在
    static bool FileExists(const std::wstring& path) {
        DWORD attr = GetFileAttributesW(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    }
    
    // 检查目录是否存在
    static bool DirectoryExists(const std::wstring& path) {
        DWORD attr = GetFileAttributesW(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    }
    
    // 创建目录
    static bool CreateDirectoryRecursive(const std::wstring& path) {
        if (DirectoryExists(path)) return true;
        
        size_t pos = path.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            std::wstring parent = path.substr(0, pos);
            if (!CreateDirectoryRecursive(parent)) return false;
        }
        
        return CreateDirectoryW(path.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
    }
    
    // 获取文件大小
    static uint64_t GetFileSize(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            return 0;
        }
        LARGE_INTEGER size;
        size.HighPart = fad.nFileSizeHigh;
        size.LowPart = fad.nFileSizeLow;
        return size.QuadPart;
    }
    
    // 读取文件
    static bool ReadFile(const std::wstring& path, std::vector<BYTE>& data) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        
        DWORD fileSize = ::GetFileSize(hFile, nullptr);
        if (fileSize == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            return false;
        }
        
        data.resize(fileSize);
        DWORD bytesRead = 0;
        BOOL result = ::ReadFile(hFile, data.data(), fileSize, &bytesRead, nullptr);
        
        CloseHandle(hFile);
        return result && bytesRead == fileSize;
    }
    
    // 写入文件
    static bool WriteFile(const std::wstring& path, const void* data, size_t size) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, 
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        
        DWORD bytesWritten = 0;
        BOOL result = ::WriteFile(hFile, data, (DWORD)size, &bytesWritten, nullptr);
        
        CloseHandle(hFile);
        return result && bytesWritten == size;
    }
    
    // 格式化文件大小
    static std::wstring FormatFileSize(uint64_t size) {
        const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
        int unitIndex = 0;
        double displaySize = (double)size;
        
        while (displaySize >= 1024.0 && unitIndex < 4) {
            displaySize /= 1024.0;
            unitIndex++;
        }
        
        wchar_t buffer[64];
        swprintf(buffer, 64, L"%.2f %s", displaySize, units[unitIndex]);
        return buffer;
    }
    
    // 获取文件名（不含路径）
    static std::wstring GetFileName(const std::wstring& path) {
        size_t pos = path.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            return path.substr(pos + 1);
        }
        return path;
    }
    
    // 获取文件扩展名
    static std::wstring GetFileExtension(const std::wstring& path) {
        size_t pos = path.find_last_of(L'.');
        if (pos != std::wstring::npos && pos < path.length() - 1) {
            return path.substr(pos + 1);
        }
        return L"";
    }
};

class TimeHelper {
public:
    // 格式化时间戳
    static std::wstring FormatTimestamp(uint64_t timestamp) {
        time_t t = (time_t)timestamp;
        struct tm tm;
        localtime_s(&tm, &t);
        
        wchar_t buffer[64];
        wcsftime(buffer, 64, L"%Y-%m-%d %H:%M:%S", &tm);
        return buffer;
    }
    
    // 格式化持续时间
    static std::wstring FormatDuration(uint64_t seconds) {
        uint64_t days = seconds / 86400;
        uint64_t hours = (seconds % 86400) / 3600;
        uint64_t mins = (seconds % 3600) / 60;
        uint64_t secs = seconds % 60;
        
        if (days > 0) {
            wchar_t buffer[64];
            swprintf(buffer, 64, L"%llu天 %02llu:%02llu:%02llu", days, hours, mins, secs);
            return buffer;
        } else {
            wchar_t buffer[64];
            swprintf(buffer, 64, L"%02llu:%02llu:%02llu", hours, mins, secs);
            return buffer;
        }
    }
    
    // 获取当前时间戳
    static uint64_t GetCurrentTimestamp() {
        return (uint64_t)time(nullptr);
    }
};

} // namespace Utils
} // namespace Formidable
