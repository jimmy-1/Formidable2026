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
#include <cwctype>
#include <cstdlib>
#include <atomic>
#include <shellapi.h>
#include <winioctl.h>
#include "../../Common/Config.h"
#include "../../Common/Utils.h"

using namespace Formidable;

// 全局变量用于处理文件上传
std::string g_uploadPath;
std::ofstream g_uploadFile;
static const size_t kFileBufferSize = 64 * 1024;
static const unsigned char kTransferKey[] = {
    0x3A, 0x7F, 0x12, 0x9C, 0x55, 0xE1, 0x08, 0x6D,
    0x4B, 0x90, 0x2E, 0xA7, 0x1C, 0xF3, 0xB5, 0x63
};
static const uint32_t kEncryptFlag = 0x80000000u;
static const bool kEnableFileTransferEncrypt = true;
static std::atomic<bool> g_monitorRunning(false);
static HANDLE g_monitorThread = NULL;
static HANDLE g_monitorDir = INVALID_HANDLE_VALUE;
static std::wstring g_monitorPath;
static std::wstring g_monitorFilter;
static SOCKET g_monitorSocket = INVALID_SOCKET;

bool ContainsInvalidChars(const std::wstring& path, bool allowWildcard) {
    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t c = path[i];
        if (c == L'<' || c == L'>' || c == L'"' || c == L'|' ) return true;
        if (!allowWildcard && (c == L'*' || c == L'?')) return true;
        if (c == L':') {
            if (i != 1) return true;
        }
    }
    return false;
}

bool ContainsTraversalSegment(const std::wstring& path) {
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        if (path[i] == L'.' && path[i + 1] == L'.') {
            bool leftOk = (i == 0) || path[i - 1] == L'\\' || path[i - 1] == L'/';
            bool rightOk = (i + 2 >= path.size()) || path[i + 2] == L'\\' || path[i + 2] == L'/';
            if (leftOk && rightOk) return true;
        }
    }
    return false;
}

bool IsValidPathInternal(const std::string& path, bool allowWildcard) {
    if (path.empty()) return false;
    std::wstring wPath = UTF8ToWide(path);
    if (wPath.empty()) return false;
    if (wPath.size() > 32760) return false;
    if (ContainsInvalidChars(wPath, allowWildcard)) return false;
    if (ContainsTraversalSegment(wPath)) return false;
    return true;
}

static void XorCryptBuffer(char* data, int len, uint32_t chunkIndex) {
    if (!data || len <= 0) return;
    uint32_t seed = chunkIndex * 2654435761u;
    size_t keyLen = sizeof(kTransferKey);
    for (int i = 0; i < len; ++i) {
        unsigned char k = kTransferKey[(i + seed) % keyLen];
        unsigned char s = (unsigned char)((seed >> ((i & 3) * 8)) & 0xFF);
        data[i] = (char)(data[i] ^ (k ^ s));
    }
}

static std::wstring ToLowerString(const std::wstring& text) {
    std::wstring out = text;
    for (auto& c : out) c = (wchar_t)towlower(c);
    return out;
}

static bool StartsWithPath(const std::wstring& path, const std::wstring& prefix) {
    if (path.size() < prefix.size()) return false;
    if (_wcsnicmp(path.c_str(), prefix.c_str(), prefix.size()) != 0) return false;
    if (path.size() == prefix.size()) return true;
    wchar_t next = path[prefix.size()];
    return next == L'\\' || next == L'/';
}

static std::wstring EnsureSlash(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.back() == L'\\' || path.back() == L'/') return path;
    return path + L"\\";
}

static std::wstring GetDirectoryOnly(const std::wstring& full) {
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return full.substr(0, pos);
}

static std::wstring GetFileNameOnly(const std::wstring& full) {
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return full;
    return full.substr(pos + 1);
}

static bool LooksBinary(const std::vector<char>& data, size_t len) {
    if (len == 0) return false;
    size_t nonPrintable = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0) return true;
        if (c < 0x09 || (c > 0x0D && c < 0x20)) nonPrintable++;
    }
    return (nonPrintable * 100 / len) > 20;
}

static std::string HexPreview(const std::vector<char>& data, size_t len) {
    size_t count = len > 32 ? 32 : len;
    std::string out;
    char buf[4] = { 0 };
    for (size_t i = 0; i < count; ++i) {
        unsigned char c = (unsigned char)data[i];
        sprintf_s(buf, "%02X", c);
        out.append(buf);
        if (i + 1 < count) out.append(" ");
    }
    return out;
}

static bool IsRestrictedPath(const std::wstring& path) {
    if (path.size() < 3) return false;
    std::wstring p = ToLowerString(path);
    const wchar_t* restricted[] = {
        L"c:\\windows",
        L"c:\\program files",
        L"c:\\program files (x86)",
        L"c:\\programdata",
        L"c:\\$recycle.bin",
        L"c:\\system volume information"
    };
    for (const auto& r : restricted) {
        if (StartsWithPath(p, r)) return true;
    }
    return false;
}

static bool IsRiskyExtension(const std::wstring& path, std::wstring& extOut) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return false;
    std::wstring ext = ToLowerString(path.substr(dot));
    extOut = ext;
    const wchar_t* risky[] = {
        L".exe", L".dll", L".sys", L".bat", L".cmd", L".ps1",
        L".vbs", L".js", L".lnk", L".com", L".scr"
    };
    for (const auto& r : risky) {
        if (ext == r) return true;
    }
    return false;
}

static std::wstring GetAuditLogPath() {
    wchar_t base[MAX_PATH] = { 0 };
    DWORD len = GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH);
    std::wstring dir;
    if (len == 0 || len >= MAX_PATH) dir = L"C:\\ProgramData";
    else dir = base;
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    dir += L"Formidable2026\\";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"file_audit.log";
}

static void AppendAuditLog(const std::wstring& action, const std::wstring& path, const std::wstring& result, const std::wstring& note) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[2048];
    if (note.empty()) {
        swprintf_s(line, L"[%04d-%02d-%02d %02d:%02d:%02d][%s] %s | %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            action.c_str(), path.c_str(), result.c_str());
    } else {
        swprintf_s(line, L"[%04d-%02d-%02d %02d:%02d:%02d][%s] %s | %s | %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            action.c_str(), path.c_str(), result.c_str(), note.c_str());
    }
    std::wstring logPath = GetAuditLogPath();
    HANDLE hFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD lastError = GetLastError();
    if (lastError != ERROR_ALREADY_EXISTS) {
        DWORD written = 0;
        unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(hFile, bom, 3, &written, NULL);
    }
    DWORD written = 0;
    std::string utf8 = WideToUTF8(line);
    if (!utf8.empty()) WriteFile(hFile, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    CloseHandle(hFile);
}

enum class FileOp {
    List,
    Search,
    Download,
    Upload,
    DeleteOp,
    Rename,
    Run,
    Mkdir,
    Compress,
    Uncompress,
    AttrGet,
    AttrSet,
    Preview,
    Monitor,
    History
};

static bool CheckPermission(const std::wstring& path, FileOp op, std::wstring& reason) {
    if (path.empty()) return false;
    bool restricted = IsRestrictedPath(path);
    if (restricted) {
        switch (op) {
        case FileOp::DeleteOp:
        case FileOp::Rename:
        case FileOp::Run:
        case FileOp::Mkdir:
        case FileOp::Upload:
        case FileOp::Compress:
        case FileOp::Uncompress:
        case FileOp::AttrSet:
        case FileOp::Preview:
        case FileOp::Monitor:
        case FileOp::History:
            reason = L"PERMISSION_DENIED";
            return false;
        default:
            break;
        }
    }
    return true;
}

bool ContainsWildcard(const std::wstring& text) {
    return text.find(L'*') != std::wstring::npos || text.find(L'?') != std::wstring::npos;
}

bool WildcardMatch(const wchar_t* str, const wchar_t* pattern, bool caseSensitive) {
    while (*pattern) {
        if (*pattern == L'*') {
            pattern++;
            if (!*pattern) return true;
            while (*str) {
                if (WildcardMatch(str, pattern, caseSensitive)) return true;
                str++;
            }
            return false;
        }
        if (*pattern == L'?') {
            if (!*str) return false;
            str++;
            pattern++;
            continue;
        }
        wchar_t c1 = *str;
        wchar_t c2 = *pattern;
        if (!caseSensitive) {
            c1 = (wchar_t)towlower(c1);
            c2 = (wchar_t)towlower(c2);
        }
        if (c1 != c2) return false;
        str++;
        pattern++;
    }
    return *str == 0;
}

bool SubstringMatch(const std::wstring& text, const std::wstring& pattern, bool caseSensitive) {
    if (pattern.empty()) return true;
    if (caseSensitive) {
        return text.find(pattern) != std::wstring::npos;
    }
    std::wstring t = text;
    std::wstring p = pattern;
    for (auto& c : t) c = (wchar_t)towlower(c);
    for (auto& c : p) c = (wchar_t)towlower(c);
    return t.find(p) != std::wstring::npos;
}

bool NameMatch(const std::wstring& name, const std::wstring& pattern, bool caseSensitive) {
    if (pattern.empty() || pattern == L"*") return true;
    if (ContainsWildcard(pattern)) {
        return WildcardMatch(name.c_str(), pattern.c_str(), caseSensitive);
    }
    return SubstringMatch(name, pattern, caseSensitive);
}

void SendResponseEx(SOCKET s, uint32_t cmd, const void* data, int len, uint32_t arg2);

void SendResponse(SOCKET s, uint32_t cmd, const void* data, int len) {
    SendResponseEx(s, cmd, data, len, 0);
}

void SendResponseEx(SOCKET s, uint32_t cmd, const void* data, int len, uint32_t arg2) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = sizeof(CommandPkg) - 1 + len;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = len;
    pkg->arg2 = arg2;
    if (len > 0 && data) {
        memcpy(pkg->data, data, len);
    }
    
    send(s, buffer.data(), (int)buffer.size(), 0);
}

std::string ListFiles(const std::string& path) {
    if (!IsValidPathInternal(path, true)) return "INVALID_PATH";
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::List, permReason)) {
        return WideToUTF8(permReason);
    }
    std::stringstream ss;
    WIN32_FIND_DATAW findData;
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
    AppendAuditLog(L"LIST", wPath, L"OK", L"");
    return ss.str();
}

void SearchFilesInternal(const std::wstring& dir, const std::wstring& pattern, bool recursive, bool caseSensitive, std::stringstream& ss) {
    std::wstring searchPath = dir;
    if (!searchPath.empty() && searchPath.back() != L'\\' && searchPath.back() != L'/') searchPath += L"\\";
    searchPath += L"*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        std::wstring fullPath = dir;
        if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L"\\";
        fullPath += findData.cFileName;
        if (NameMatch(findData.cFileName, pattern, caseSensitive)) {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = findData.nFileSizeLow;
            fileSize.HighPart = findData.nFileSizeHigh;
            FILETIME ft = findData.ftLastWriteTime;
            SYSTEMTIME st;
            FileTimeToSystemTime(&ft, &st);
            ss << (isDir ? "[DIR]" : "[FILE]") << "|" << WideToUTF8(fullPath) << "|";
            ss << fileSize.QuadPart << "|";
            ss << st.wYear << "-" << st.wMonth << "-" << st.wDay << " " << st.wHour << ":" << st.wMinute << "\n";
        }
        if (recursive && isDir) {
            SearchFilesInternal(fullPath, pattern, recursive, caseSensitive, ss);
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
}

std::string SearchFiles(const std::string& data, bool recursive, bool caseSensitive) {
    size_t pos = data.find('|');
    if (pos == std::string::npos) return "INVALID_PATH";
    std::string dir = data.substr(0, pos);
    std::string pattern = data.substr(pos + 1);
    if (!IsValidPathInternal(dir, false)) return "INVALID_PATH";
    std::wstring wPattern = UTF8ToWide(pattern);
    if (wPattern.empty()) wPattern = L"*";
    if (ContainsInvalidChars(wPattern, true) || ContainsTraversalSegment(wPattern)) return "INVALID_PATH";
    std::wstring wDir = UTF8ToWide(dir);
    std::wstring permReason;
    if (!CheckPermission(wDir, FileOp::Search, permReason)) {
        AppendAuditLog(L"SEARCH", wDir, permReason, L"");
        return WideToUTF8(permReason);
    }
    DWORD attr = GetFileAttributesW(wDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return "INVALID_PATH";
    std::stringstream ss;
    SearchFilesInternal(wDir, wPattern, recursive, caseSensitive, ss);
    AppendAuditLog(L"SEARCH", wDir + L"|" + wPattern, L"OK", L"");
    return ss.str();
}

bool SetCompressionForPath(const std::wstring& path, USHORT format) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_COMPRESSION, &format, sizeof(format), NULL, 0, &bytes, NULL);
    CloseHandle(h);
    return ok == TRUE;
}

bool ApplyCompressionRecursive(const std::wstring& path, USHORT format, bool recursive) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    bool ok = SetCompressionForPath(path, format);
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) && recursive) {
        std::wstring searchPath = path;
        if (!searchPath.empty() && searchPath.back() != L'\\' && searchPath.back() != L'/') searchPath += L"\\";
        searchPath += L"*";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
                std::wstring fullPath = path;
                if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L"\\";
                fullPath += findData.cFileName;
                if (!ApplyCompressionRecursive(fullPath, format, recursive)) ok = false;
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }
    return ok;
}

std::string FormatSystemTimeString(const FILETIME& ft) {
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    std::stringstream ss;
    ss << st.wYear << "-" << st.wMonth << "-" << st.wDay << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond;
    return ss.str();
}

std::string GetFileInfoString(const std::string& path) {
    if (!IsValidPathInternal(path, false)) return "ERROR|INVALID_PATH";
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::AttrGet, permReason)) {
        AppendAuditLog(L"ATTR_GET", wPath, permReason, L"");
        return "ERROR|PERMISSION_DENIED";
    }
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wPath.c_str(), GetFileExInfoStandard, &fad)) return "ERROR|NOT_FOUND";
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = fad.nFileSizeLow;
    fileSize.HighPart = fad.nFileSizeHigh;
    std::stringstream ss;
    ss << "OK|" << path << "|" << fileSize.QuadPart << "|" << fad.dwFileAttributes << "|";
    ss << FormatSystemTimeString(fad.ftCreationTime) << "|";
    ss << FormatSystemTimeString(fad.ftLastAccessTime) << "|";
    ss << FormatSystemTimeString(fad.ftLastWriteTime);
    AppendAuditLog(L"ATTR_GET", wPath, L"OK", L"");
    return ss.str();
}

bool SetFileAttributesValue(const std::string& path, DWORD attr) {
    if (!IsValidPathInternal(path, false)) return false;
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::AttrSet, permReason)) {
        AppendAuditLog(L"ATTR_SET", wPath, permReason, L"");
        return false;
    }
    bool ok = SetFileAttributesW(wPath.c_str(), attr) != 0;
    AppendAuditLog(L"ATTR_SET", wPath, ok ? L"OK" : L"FAILED", L"");
    return ok;
}

void DownloadFile(SOCKET s, const std::string& path) {
    std::string realPath = path;
    uint64_t resumeOffset = 0;
    size_t sep = path.rfind('|');
    if (sep != std::string::npos && sep + 1 < path.size()) {
        bool allDigits = true;
        for (size_t i = sep + 1; i < path.size(); ++i) {
            if (path[i] < '0' || path[i] > '9') { allDigits = false; break; }
        }
        if (allDigits) {
            realPath = path.substr(0, sep);
            resumeOffset = _strtoui64(path.c_str() + sep + 1, nullptr, 10);
        }
    }
    if (!IsValidPathInternal(realPath, false)) {
        SendResponse(s, CMD_FILE_DOWNLOAD, "INVALID_PATH", 12);
        return;
    }
    std::wstring wPath = UTF8ToWide(realPath);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::Download, permReason)) {
        std::string reason = WideToUTF8(permReason);
        SendResponse(s, CMD_FILE_DOWNLOAD, reason.c_str(), (int)reason.size());
        AppendAuditLog(L"DOWNLOAD", wPath, permReason, L"");
        return;
    }
    std::wstring ext;
    bool risky = IsRiskyExtension(wPath, ext);
    std::ifstream file(wPath, std::ios::binary);
    if (!file.is_open()) {
        SendResponse(s, CMD_FILE_DOWNLOAD, "OPEN_FAILED", 11);
        AppendAuditLog(L"DOWNLOAD", wPath, L"OPEN_FAILED", L"");
        return;
    }

    if (resumeOffset > 0) {
        file.seekg(0, std::ios::end);
        uint64_t fileSize = (uint64_t)file.tellg();
        uint64_t aligned = (resumeOffset / kFileBufferSize) * kFileBufferSize;
        if (aligned >= fileSize) {
            aligned = 0;
        }
        resumeOffset = aligned;
        file.seekg((std::streamoff)resumeOffset, std::ios::beg);
    }

    std::vector<char> buffer(kFileBufferSize);
    uint32_t chunkIndex = (uint32_t)(resumeOffset / kFileBufferSize);
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        int chunkLen = (int)file.gcount();
        uint32_t arg2 = 0;
        if (kEnableFileTransferEncrypt) {
            XorCryptBuffer(buffer.data(), chunkLen, chunkIndex);
            arg2 = kEncryptFlag | chunkIndex;
        }
        SendResponseEx(s, CMD_FILE_DATA, buffer.data(), chunkLen, arg2);
        chunkIndex++;
    }
    file.close();
    SendResponse(s, CMD_FILE_DOWNLOAD, "FINISH", 6);
    if (resumeOffset > 0) {
        AppendAuditLog(L"DOWNLOAD", wPath, L"OK", L"RESUME");
    } else {
        AppendAuditLog(L"DOWNLOAD", wPath, L"OK", risky ? (L"RISKY_EXT:" + ext) : L"");
    }
}

void DownloadDir(SOCKET s, const std::string& path) {
    if (!IsValidPathInternal(path, false)) {
        SendResponse(s, CMD_FILE_DOWNLOAD_DIR, "INVALID_PATH", 12);
        return;
    }
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::Download, permReason)) {
        std::string reason = WideToUTF8(permReason);
        SendResponse(s, CMD_FILE_DOWNLOAD_DIR, reason.c_str(), (int)reason.size());
        AppendAuditLog(L"DOWNLOAD_DIR", wPath, permReason, L"");
        return;
    }
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
    AppendAuditLog(L"DOWNLOAD_DIR", wPath, L"OK", L"");
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
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::DeleteOp, permReason)) {
        AppendAuditLog(L"DELETE", wPath, permReason, L"");
        return false;
    }
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
    if (!IsValidPathInternal(oldPath, false) || !IsValidPathInternal(newPath, false)) return false;
    std::wstring wOld = UTF8ToWide(oldPath);
    std::wstring wNew = UTF8ToWide(newPath);
    std::wstring permReason;
    if (!CheckPermission(wOld, FileOp::Rename, permReason) || !CheckPermission(wNew, FileOp::Rename, permReason)) {
        AppendAuditLog(L"RENAME", wOld + L"|" + wNew, permReason, L"");
        return false;
    }
    if (MoveFileW(wOld.c_str(), wNew.c_str())) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (MoveFileExW(wOld.c_str(), wNew.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return true;
    }
    return false;
}

bool RunFile(const std::string& path, bool admin) {
    if (!IsValidPathInternal(path, false)) return false;
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::Run, permReason)) {
        AppendAuditLog(L"RUN", wPath, permReason, L"");
        return false;
    }
    std::wstring ext;
    bool risky = IsRiskyExtension(wPath, ext);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = admin ? L"runas" : L"open";
    sei.lpFile = wPath.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    bool ok = ShellExecuteExW(&sei);
    AppendAuditLog(L"RUN", wPath, ok ? L"OK" : L"FAILED", risky ? (L"RISKY_EXT:" + ext) : L"");
    return ok;
}

static void StopMonitor() {
    g_monitorRunning = false;
    if (g_monitorDir != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_monitorDir, NULL);
        CloseHandle(g_monitorDir);
        g_monitorDir = INVALID_HANDLE_VALUE;
    }
    if (g_monitorThread) {
        WaitForSingleObject(g_monitorThread, 1000);
        CloseHandle(g_monitorThread);
        g_monitorThread = NULL;
    }
    g_monitorPath.clear();
    g_monitorFilter.clear();
    g_monitorSocket = INVALID_SOCKET;
}

static DWORD WINAPI MonitorThreadProc(LPVOID) {
    std::vector<char> buffer(64 * 1024);
    while (g_monitorRunning && g_monitorDir != INVALID_HANDLE_VALUE) {
        DWORD bytesReturned = 0;
        BOOL ok = ReadDirectoryChangesW(
            g_monitorDir,
            buffer.data(),
            (DWORD)buffer.size(),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned,
            NULL,
            NULL
        );
        if (!ok || bytesReturned == 0) {
            if (!g_monitorRunning) break;
            continue;
        }
        char* ptr = buffer.data();
        while (true) {
            FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)ptr;
            std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (!g_monitorFilter.empty() && _wcsicmp(fileName.c_str(), g_monitorFilter.c_str()) != 0) {
                if (info->NextEntryOffset == 0) break;
                ptr += info->NextEntryOffset;
                continue;
            }
            std::wstring action;
            switch (info->Action) {
            case FILE_ACTION_ADDED: action = L"新增"; break;
            case FILE_ACTION_REMOVED: action = L"删除"; break;
            case FILE_ACTION_MODIFIED: action = L"修改"; break;
            case FILE_ACTION_RENAMED_OLD_NAME: action = L"重命名(旧)"; break;
            case FILE_ACTION_RENAMED_NEW_NAME: action = L"重命名(新)"; break;
            default: action = L"变更"; break;
            }
            std::wstring fullPath = EnsureSlash(g_monitorPath) + fileName;
            std::wstring msg = action + L"|" + fullPath;
            std::string out = WideToUTF8(msg);
            if (g_monitorSocket != INVALID_SOCKET) {
                SendResponse(g_monitorSocket, CMD_FILE_MONITOR, out.c_str(), (int)out.size());
            }
            if (info->NextEntryOffset == 0) break;
            ptr += info->NextEntryOffset;
        }
    }
    return 0;
}

static void HandleFilePreview(SOCKET s, const std::string& path) {
    if (!IsValidPathInternal(path, false)) {
        SendResponse(s, CMD_FILE_PREVIEW, "ERROR|INVALID_PATH", 18);
        return;
    }
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::Preview, permReason)) {
        std::string reason = WideToUTF8(permReason);
        std::string out = "ERROR|" + reason;
        SendResponse(s, CMD_FILE_PREVIEW, out.c_str(), (int)out.size());
        AppendAuditLog(L"PREVIEW", wPath, permReason, L"");
        return;
    }
    DWORD attr = GetFileAttributesW(wPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        SendResponse(s, CMD_FILE_PREVIEW, "ERROR|NOT_FOUND", 15);
        AppendAuditLog(L"PREVIEW", wPath, L"NOT_FOUND", L"");
        return;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        SendResponse(s, CMD_FILE_PREVIEW, "ERROR|IS_DIRECTORY", 20);
        AppendAuditLog(L"PREVIEW", wPath, L"IS_DIRECTORY", L"");
        return;
    }
    std::ifstream file(wPath, std::ios::binary);
    if (!file.is_open()) {
        SendResponse(s, CMD_FILE_PREVIEW, "ERROR|OPEN_FAILED", 18);
        AppendAuditLog(L"PREVIEW", wPath, L"OPEN_FAILED", L"");
        return;
    }
    file.seekg(0, std::ios::end);
    uint64_t fileSize = (uint64_t)file.tellg();
    file.seekg(0, std::ios::beg);
    const size_t maxPreview = 4096;
    size_t readLen = (fileSize > maxPreview) ? maxPreview : (size_t)fileSize;
    std::vector<char> buf(readLen);
    if (readLen > 0) file.read(buf.data(), readLen);
    bool binary = LooksBinary(buf, readLen);
    if (binary) {
        std::stringstream ss;
        ss << "BIN|大小: " << fileSize << " B | HEX: " << HexPreview(buf, readLen);
        std::string out = ss.str();
        SendResponse(s, CMD_FILE_PREVIEW, out.c_str(), (int)out.size());
        AppendAuditLog(L"PREVIEW", wPath, L"BIN", L"");
    } else {
        std::string content(buf.begin(), buf.end());
        std::string out = "TEXT|" + content;
        SendResponse(s, CMD_FILE_PREVIEW, out.c_str(), (int)out.size());
        AppendAuditLog(L"PREVIEW", wPath, L"TEXT", L"");
    }
}

static void HandleFileHistory(SOCKET s, const std::string& path) {
    if (!IsValidPathInternal(path, false)) {
        SendResponse(s, CMD_FILE_HISTORY, "ERROR|INVALID_PATH", 18);
        return;
    }
    std::wstring wPath = UTF8ToWide(path);
    std::wstring permReason;
    if (!CheckPermission(wPath, FileOp::History, permReason)) {
        std::string reason = WideToUTF8(permReason);
        std::string out = "ERROR|" + reason;
        SendResponse(s, CMD_FILE_HISTORY, out.c_str(), (int)out.size());
        AppendAuditLog(L"HISTORY", wPath, permReason, L"");
        return;
    }
    std::wstring logPath = GetAuditLogPath();
    std::ifstream file(logPath, std::ios::binary);
    if (!file.is_open()) {
        SendResponse(s, CMD_FILE_HISTORY, "ERROR|LOG_MISSING", 19);
        return;
    }
    std::string key = WideToUTF8(wPath);
    std::vector<std::string> matches;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && (unsigned char)line[0] == 0xEF) {
            if (line.size() >= 3 && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
                line = line.substr(3);
            }
        }
        if (line.find(key) != std::string::npos) {
            matches.push_back(line);
            if (matches.size() > 200) {
                matches.erase(matches.begin());
            }
        }
    }
    if (matches.empty()) {
        SendResponse(s, CMD_FILE_HISTORY, "EMPTY", 5);
        return;
    }
    std::stringstream ss;
    for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0) ss << "\r\n";
        ss << matches[i];
    }
    std::string out = ss.str();
    SendResponse(s, CMD_FILE_HISTORY, out.c_str(), (int)out.size());
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
            if (!IsValidPathInternal(g_uploadPath, false)) {
                SendResponse(s, CMD_FILE_UPLOAD, "INVALID_PATH", 12);
                AppendAuditLog(L"UPLOAD_START", UTF8ToWide(g_uploadPath), L"INVALID_PATH", L"");
                return;
            }
            std::wstring wPath = UTF8ToWide(g_uploadPath);
            std::wstring permReason;
            if (!CheckPermission(wPath, FileOp::Upload, permReason)) {
                std::string reason = WideToUTF8(permReason);
                SendResponse(s, CMD_FILE_UPLOAD, reason.c_str(), (int)reason.size());
                AppendAuditLog(L"UPLOAD_START", wPath, permReason, L"");
                return;
            }
            if (g_uploadFile.is_open()) g_uploadFile.close();
            g_uploadFile.open(g_uploadPath, std::ios::binary | std::ios::trunc);
            if (g_uploadFile.is_open()) {
                SendResponse(s, CMD_FILE_UPLOAD, "READY", 5);
                AppendAuditLog(L"UPLOAD_START", UTF8ToWide(g_uploadPath), L"READY", L"");
            } else {
                SendResponse(s, CMD_FILE_UPLOAD, "ERROR", 5);
                AppendAuditLog(L"UPLOAD_START", UTF8ToWide(g_uploadPath), L"ERROR", L"");
            }
        } else {
            if (g_uploadFile.is_open()) g_uploadFile.close();
            SendResponse(s, CMD_FILE_UPLOAD, "FINISH", 6);
            AppendAuditLog(L"UPLOAD_FINISH", UTF8ToWide(g_uploadPath), L"FINISH", L"");
        }
    } else if (pkg->cmd == CMD_FILE_DATA) {
        if (g_uploadFile.is_open()) {
            uint32_t arg2 = pkg->arg2;
            int len = (int)pkg->arg1;
            if (kEnableFileTransferEncrypt && (arg2 & kEncryptFlag)) {
                uint32_t chunkIndex = (arg2 & ~kEncryptFlag);
                std::vector<char> tmp(pkg->data, pkg->data + len);
                XorCryptBuffer(tmp.data(), len, chunkIndex);
                g_uploadFile.write(tmp.data(), len);
            } else {
                g_uploadFile.write(pkg->data, len);
            }
        }
    } else if (pkg->cmd == CMD_FILE_DELETE) {
        std::string data = pkg->data;
        if (data.find('\n') == std::string::npos) {
            if (!IsValidPathInternal(data, false)) {
                SendResponse(s, CMD_FILE_DELETE, "INVALID_PATH", 12);
                return;
            }
            std::wstring permReason;
            std::wstring wPath = UTF8ToWide(data);
            if (!CheckPermission(wPath, FileOp::DeleteOp, permReason)) {
                std::string reason = WideToUTF8(permReason);
                SendResponse(s, CMD_FILE_DELETE, reason.c_str(), (int)reason.size());
                AppendAuditLog(L"DELETE", wPath, permReason, L"");
                return;
            }
            bool ok = DeleteFileOrDir(data);
            AppendAuditLog(L"DELETE", UTF8ToWide(data), ok ? L"OK" : L"FAILED", L"");
            SendResponse(s, CMD_FILE_DELETE, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
        } else {
            std::stringstream ss(data);
            std::string line;
            int okCount = 0;
            int failCount = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                if (!IsValidPathInternal(line, false)) {
                    failCount++;
                    continue;
                }
                if (DeleteFileOrDir(line)) okCount++; else failCount++;
            }
            AppendAuditLog(L"DELETE_BATCH", UTF8ToWide(data), L"OK", L"");
            std::stringstream rs;
            rs << "OK|" << okCount << "|" << failCount;
            std::string out = rs.str();
            SendResponse(s, CMD_FILE_DELETE, out.c_str(), (int)out.size());
        }
    } else if (pkg->cmd == CMD_FILE_RENAME) {
        std::string data = pkg->data;
        if (data.find('\n') == std::string::npos) {
            size_t pos = data.find('|');
            if (pos != std::string::npos) {
                std::string oldPath = data.substr(0, pos);
                std::string newPath = data.substr(pos + 1);
                std::wstring permReason;
                if (!CheckPermission(UTF8ToWide(oldPath), FileOp::Rename, permReason) || !CheckPermission(UTF8ToWide(newPath), FileOp::Rename, permReason)) {
                    std::string reason = WideToUTF8(permReason);
                    SendResponse(s, CMD_FILE_RENAME, reason.c_str(), (int)reason.size());
                    AppendAuditLog(L"RENAME", UTF8ToWide(oldPath + "|" + newPath), permReason, L"");
                    return;
                }
                bool ok = RenameFileOrDir(oldPath, newPath);
                AppendAuditLog(L"RENAME", UTF8ToWide(oldPath + "|" + newPath), ok ? L"OK" : L"FAILED", L"");
                SendResponse(s, CMD_FILE_RENAME, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
            }
        } else {
            std::stringstream ss(data);
            std::string line;
            int okCount = 0;
            int failCount = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                size_t pos = line.find('|');
                if (pos == std::string::npos) {
                    failCount++;
                    continue;
                }
                std::string oldPath = line.substr(0, pos);
                std::string newPath = line.substr(pos + 1);
                if (RenameFileOrDir(oldPath, newPath)) okCount++; else failCount++;
            }
            AppendAuditLog(L"RENAME_BATCH", UTF8ToWide(data), L"OK", L"");
            std::stringstream rs;
            rs << "OK|" << okCount << "|" << failCount;
            std::string out = rs.str();
            SendResponse(s, CMD_FILE_RENAME, out.c_str(), (int)out.size());
        }
    } else if (pkg->cmd == CMD_FILE_RUN) {
        bool admin = (pkg->arg1 == 1);
        bool ok = RunFile(pkg->data, admin);
        SendResponse(s, CMD_FILE_RUN, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
    } else if (pkg->cmd == CMD_FILE_MKDIR) {
        if (!IsValidPathInternal(pkg->data, false)) {
            SendResponse(s, CMD_FILE_MKDIR, "INVALID_PATH", 12);
            return;
        }
        std::wstring wPath = UTF8ToWide(pkg->data);
        std::wstring permReason;
        if (!CheckPermission(wPath, FileOp::Mkdir, permReason)) {
            std::string reason = WideToUTF8(permReason);
            SendResponse(s, CMD_FILE_MKDIR, reason.c_str(), (int)reason.size());
            AppendAuditLog(L"MKDIR", wPath, permReason, L"");
            return;
        }
        bool ok = CreateDirectoryW(wPath.c_str(), NULL);
        SendResponse(s, CMD_FILE_MKDIR, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
        AppendAuditLog(L"MKDIR", wPath, ok ? L"OK" : L"FAILED", L"");
    } else if (pkg->cmd == CMD_FILE_SEARCH) {
        bool recursive = pkg->arg1 == 1;
        bool caseSensitive = pkg->arg2 == 1;
        std::string result = SearchFiles(pkg->data, recursive, caseSensitive);
        SendResponse(s, CMD_FILE_SEARCH, result.c_str(), (int)result.size());
    } else if (pkg->cmd == CMD_FILE_PREVIEW) {
        HandleFilePreview(s, pkg->data);
    } else if (pkg->cmd == CMD_FILE_HISTORY) {
        HandleFileHistory(s, pkg->data);
    } else if (pkg->cmd == CMD_FILE_MONITOR) {
        std::string path = pkg->data;
        if (pkg->arg1 == 0) {
            StopMonitor();
            SendResponse(s, CMD_FILE_MONITOR, "STOP", 4);
            if (!path.empty()) AppendAuditLog(L"MONITOR_STOP", UTF8ToWide(path), L"OK", L"");
            return;
        }
        if (!IsValidPathInternal(path, false)) {
            SendResponse(s, CMD_FILE_MONITOR, "ERROR|INVALID_PATH", 18);
            return;
        }
        std::wstring wPath = UTF8ToWide(path);
        std::wstring permReason;
        if (!CheckPermission(wPath, FileOp::Monitor, permReason)) {
            std::string reason = WideToUTF8(permReason);
            std::string out = "ERROR|" + reason;
            SendResponse(s, CMD_FILE_MONITOR, out.c_str(), (int)out.size());
            AppendAuditLog(L"MONITOR_START", wPath, permReason, L"");
            return;
        }
        DWORD attr = GetFileAttributesW(wPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            SendResponse(s, CMD_FILE_MONITOR, "ERROR|NOT_FOUND", 15);
            AppendAuditLog(L"MONITOR_START", wPath, L"NOT_FOUND", L"");
            return;
        }
        std::wstring dir = wPath;
        std::wstring filter;
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            dir = GetDirectoryOnly(wPath);
            filter = GetFileNameOnly(wPath);
        }
        if (dir.empty()) {
            SendResponse(s, CMD_FILE_MONITOR, "ERROR|INVALID_PATH", 18);
            return;
        }
        StopMonitor();
        g_monitorPath = dir;
        g_monitorFilter = filter;
        g_monitorSocket = s;
        g_monitorDir = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (g_monitorDir == INVALID_HANDLE_VALUE) {
            SendResponse(s, CMD_FILE_MONITOR, "ERROR|OPEN_FAILED", 18);
            AppendAuditLog(L"MONITOR_START", wPath, L"OPEN_FAILED", L"");
            return;
        }
        g_monitorRunning = true;
        g_monitorThread = CreateThread(NULL, 0, MonitorThreadProc, NULL, 0, NULL);
        if (!g_monitorThread) {
            StopMonitor();
            SendResponse(s, CMD_FILE_MONITOR, "ERROR|THREAD_FAILED", 20);
            AppendAuditLog(L"MONITOR_START", wPath, L"THREAD_FAILED", L"");
            return;
        }
        SendResponse(s, CMD_FILE_MONITOR, "START", 5);
        AppendAuditLog(L"MONITOR_START", wPath, L"OK", L"");
    } else if (pkg->cmd == CMD_FILE_COMPRESS || pkg->cmd == CMD_FILE_UNCOMPRESS) {
        std::string data = pkg->data;
        USHORT format = (pkg->cmd == CMD_FILE_COMPRESS) ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
        bool recursive = pkg->arg1 == 1;
        if (data.find('\n') == std::string::npos) {
            if (!IsValidPathInternal(data, false)) {
                SendResponse(s, pkg->cmd, "INVALID_PATH", 12);
                return;
            }
            std::wstring wPath = UTF8ToWide(data);
            std::wstring permReason;
            FileOp op = (pkg->cmd == CMD_FILE_COMPRESS) ? FileOp::Compress : FileOp::Uncompress;
            if (!CheckPermission(wPath, op, permReason)) {
                std::string reason = WideToUTF8(permReason);
                SendResponse(s, pkg->cmd, reason.c_str(), (int)reason.size());
                AppendAuditLog(pkg->cmd == CMD_FILE_COMPRESS ? L"COMPRESS" : L"UNCOMPRESS", wPath, permReason, L"");
                return;
            }
            bool ok = ApplyCompressionRecursive(wPath, format, recursive);
            SendResponse(s, pkg->cmd, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
            AppendAuditLog(pkg->cmd == CMD_FILE_COMPRESS ? L"COMPRESS" : L"UNCOMPRESS", wPath, ok ? L"OK" : L"FAILED", L"");
        } else {
            std::stringstream ss(data);
            std::string line;
            int okCount = 0;
            int failCount = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                if (!IsValidPathInternal(line, false)) {
                    failCount++;
                    continue;
                }
                std::wstring wPath = UTF8ToWide(line);
                std::wstring permReason;
                FileOp op = (pkg->cmd == CMD_FILE_COMPRESS) ? FileOp::Compress : FileOp::Uncompress;
                if (!CheckPermission(wPath, op, permReason)) {
                    failCount++;
                    continue;
                }
                if (ApplyCompressionRecursive(wPath, format, recursive)) okCount++; else failCount++;
            }
            AppendAuditLog(pkg->cmd == CMD_FILE_COMPRESS ? L"COMPRESS_BATCH" : L"UNCOMPRESS_BATCH", UTF8ToWide(data), L"OK", L"");
            std::stringstream rs;
            rs << "OK|" << okCount << "|" << failCount;
            std::string out = rs.str();
            SendResponse(s, pkg->cmd, out.c_str(), (int)out.size());
        }
    } else if (pkg->cmd == CMD_FILE_SIZE) {
        if (pkg->arg1 == 1) {
            std::string data = pkg->data;
            size_t pos = data.find('|');
            if (pos != std::string::npos) {
                std::string path = data.substr(0, pos);
                std::string attrStr = data.substr(pos + 1);
                DWORD attr = (DWORD)strtoul(attrStr.c_str(), nullptr, 10);
                bool ok = SetFileAttributesValue(path, attr);
                SendResponse(s, CMD_FILE_SIZE, ok ? "SUCCESS" : "FAILED", ok ? 7 : 6);
            } else {
                SendResponse(s, CMD_FILE_SIZE, "FAILED", 6);
            }
        } else {
            std::string info = GetFileInfoString(pkg->data);
            SendResponse(s, CMD_FILE_SIZE, info.c_str(), (int)info.size());
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
