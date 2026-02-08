#include "Config.h"
#include "GlobalState.h"
#include "../Common/Config.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <mutex>
#include <vector>

// 配置持久化
void LoadSettings() {
    wchar_t szDir[MAX_PATH] = { 0 };
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szDir) == S_OK) {
        wcscat_s(szDir, L"\\Formidable2026\\");
        CreateDirectoryW(szDir, NULL);
    } else {
        GetTempPathW(MAX_PATH, szDir);
        wcscat_s(szDir, L"Formidable2026\\");
        CreateDirectoryW(szDir, NULL);
    }

    wchar_t szPath[MAX_PATH] = { 0 };
    wcscpy_s(szPath, szDir);
    wcscat_s(szPath, L"config.ini");
    wcscpy_s(g_Settings.szConfigPath, szPath);

    g_Settings.listenPort = GetPrivateProfileIntW(L"Server", L"Port", Formidable::DEFAULT_PORT, g_Settings.szConfigPath);

    // 加载 FRP 设置
    g_Settings.bEnableFrp = GetPrivateProfileIntW(L"FRP", L"Enable", 0, g_Settings.szConfigPath) != 0;
    GetPrivateProfileStringW(L"FRP", L"Server", L"127.0.0.1", g_Settings.szFrpServer, 128, g_Settings.szConfigPath);
    g_Settings.frpServerPort = GetPrivateProfileIntW(L"FRP", L"ServerPort", 7000, g_Settings.szConfigPath);
    GetPrivateProfileStringW(L"FRP", L"Token", L"", g_Settings.szFrpToken, 64, g_Settings.szConfigPath);
    g_Settings.frpRemotePort = GetPrivateProfileIntW(L"FRP", L"RemotePort", 8080, g_Settings.szConfigPath);
    GetPrivateProfileStringW(L"FRP", L"ProxyName", L"Formidable_Master", g_Settings.szFrpProxyName, 64, g_Settings.szConfigPath);
    g_Settings.frpDownloadPort = GetPrivateProfileIntW(L"FRP", L"DownloadPort", 80, g_Settings.szConfigPath);

    // 加载桌面管理设置
    g_Settings.screenCaptureMethod = GetPrivateProfileIntW(L"Desktop", L"CaptureMethod", 0, g_Settings.szConfigPath);
    g_Settings.imageCompressMethod = GetPrivateProfileIntW(L"Desktop", L"CompressMethod", 0, g_Settings.szConfigPath);
    g_Settings.enableMultiMonitor = GetPrivateProfileIntW(L"Desktop", L"MultiMonitor", 0, g_Settings.szConfigPath) != 0;
    g_Settings.useDiffTransmission = GetPrivateProfileIntW(L"Desktop", L"UseDiff", 1, g_Settings.szConfigPath) != 0;

    // 加载预存备注
    LoadClientRemarks();

    // 加载历史主机
    LoadHistoryHosts();
}

void SaveSettings() {
    wchar_t szBuf[128];
    swprintf_s(szBuf, L"%d", g_Settings.listenPort);
    WritePrivateProfileStringW(L"Server", L"Port", szBuf, g_Settings.szConfigPath);

    // 保存 FRP 设置
    WritePrivateProfileStringW(L"FRP", L"Enable", g_Settings.bEnableFrp ? L"1" : L"0", g_Settings.szConfigPath);
    WritePrivateProfileStringW(L"FRP", L"Server", g_Settings.szFrpServer, g_Settings.szConfigPath);
    swprintf_s(szBuf, L"%d", g_Settings.frpServerPort);
    WritePrivateProfileStringW(L"FRP", L"ServerPort", szBuf, g_Settings.szConfigPath);
    WritePrivateProfileStringW(L"FRP", L"Token", g_Settings.szFrpToken, g_Settings.szConfigPath);
    swprintf_s(szBuf, L"%d", g_Settings.frpRemotePort);
    WritePrivateProfileStringW(L"FRP", L"RemotePort", szBuf, g_Settings.szConfigPath);
    WritePrivateProfileStringW(L"FRP", L"ProxyName", g_Settings.szFrpProxyName, g_Settings.szConfigPath);
    swprintf_s(szBuf, L"%d", g_Settings.frpDownloadPort);
    WritePrivateProfileStringW(L"FRP", L"DownloadPort", szBuf, g_Settings.szConfigPath);

    // 保存桌面管理设置
    swprintf_s(szBuf, L"%d", g_Settings.screenCaptureMethod);
    WritePrivateProfileStringW(L"Desktop", L"CaptureMethod", szBuf, g_Settings.szConfigPath);
    swprintf_s(szBuf, L"%d", g_Settings.imageCompressMethod);
    WritePrivateProfileStringW(L"Desktop", L"CompressMethod", szBuf, g_Settings.szConfigPath);
    WritePrivateProfileStringW(L"Desktop", L"MultiMonitor", g_Settings.enableMultiMonitor ? L"1" : L"0", g_Settings.szConfigPath);
    WritePrivateProfileStringW(L"Desktop", L"UseDiff", g_Settings.useDiffTransmission ? L"1" : L"0", g_Settings.szConfigPath);

    // 保存预存备注
    SaveClientRemarks();

    // 保存历史主机
    SaveHistoryHosts();
}

void LoadClientRemarks() {
    wchar_t szPath[MAX_PATH];
    wcscpy_s(szPath, g_Settings.szConfigPath);
    wchar_t* p = wcsrchr(szPath, L'\\');
    if (p) *(p + 1) = L'\0';
    wcscat_s(szPath, L"remarks.ini");

    wchar_t szBuffer[32768];
    DWORD dwLen = GetPrivateProfileSectionW(L"Remarks", szBuffer, 32768, szPath);
    if (dwLen > 0) {
        std::lock_guard<std::mutex> lock(g_SavedRemarksMutex);
        g_SavedRemarks.clear();
        wchar_t* pEntry = szBuffer;
        while (*pEntry) {
            std::wstring entry = pEntry;
            size_t pos = entry.find(L'=');
            if (pos != std::wstring::npos) {
                std::wstring key = entry.substr(0, pos);
                std::wstring val = entry.substr(pos + 1);
                g_SavedRemarks[key] = val;
            }
            pEntry += wcslen(pEntry) + 1;
        }
    }
}

void SaveClientRemarks() {
    wchar_t szPath[MAX_PATH];
    wcscpy_s(szPath, g_Settings.szConfigPath);
    wchar_t* p = wcsrchr(szPath, L'\\');
    if (p) *(p + 1) = L'\0';
    wcscat_s(szPath, L"remarks.ini");

    std::lock_guard<std::mutex> lock(g_SavedRemarksMutex);
    // 清空现有段落内容（简单方式是先删除文件或段落）
    WritePrivateProfileStringW(L"Remarks", NULL, NULL, szPath);

    for (std::map<std::wstring, std::wstring>::const_iterator it = g_SavedRemarks.begin(); it != g_SavedRemarks.end(); ++it) {
        WritePrivateProfileStringW(L"Remarks", it->first.c_str(), it->second.c_str(), szPath);
    }
}

void LoadHistoryHosts() {
    wchar_t szPath[MAX_PATH];
    wcscpy_s(szPath, g_Settings.szConfigPath);
    wchar_t* p = wcsrchr(szPath, L'\\');
    if (p) *(p + 1) = L'\0';
    wcscat_s(szPath, L"history.ini");

    wchar_t szBuffer[65536];
    DWORD dwLen = GetPrivateProfileSectionW(L"History", szBuffer, 65536, szPath);
    if (dwLen > 0) {
        std::lock_guard<std::mutex> lock(g_HistoryHostsMutex);
        g_HistoryHosts.clear();
        wchar_t* pEntry = szBuffer;
        while (*pEntry) {
            std::wstring entry = pEntry;
            size_t pos = entry.find(L'=');
            if (pos != std::wstring::npos) {
                std::wstring key = entry.substr(0, pos);
                std::wstring val = entry.substr(pos + 1);
                
                std::vector<std::wstring> parts;
                parts.reserve(10);
                size_t start = 0;
                while (true) {
                    size_t sep = val.find(L'|', start);
                    if (sep == std::wstring::npos) {
                        parts.push_back(val.substr(start));
                        break;
                    }
                    parts.push_back(val.substr(start, sep - start));
                    start = sep + 1;
                }

                HistoryHost host;
                if (parts.size() >= 4) {
                    host.ip = parts[0];
                    host.computerName = parts[1];
                    host.userName = parts[2];
                    host.osVersion = parts[3];
                }
                if (parts.size() == 6) {
                    host.lastSeen = parts[4];
                    host.remark = parts[5];
                } else if (parts.size() == 7) {
                    host.installTime = parts[4];
                    host.lastSeen = parts[5];
                    host.remark = parts[6];
                } else if (parts.size() == 8) {
                    host.installTime = parts[4];
                    host.lastSeen = parts[5];
                    host.remark = parts[6];
                    host.location = parts[7];
                } else if (parts.size() == 9) {
                    host.installTime = parts[4];
                    host.lastSeen = parts[5];
                    host.remark = parts[6];
                    host.location = parts[7];
                    host.programPath = parts[8];
                } else if (parts.size() >= 10) {
                    host.installTime = parts[4];
                    host.lastSeen = parts[5];
                    host.remark = parts[6];
                    host.location = parts[7];
                    host.programPath = parts[8];
                    try {
                        host.clientUniqueId = _wcstoui64(parts[9].c_str(), nullptr, 10);
                    } catch (...) {
                        host.clientUniqueId = 0;
                    }
                }

                if (!host.ip.empty() || !host.computerName.empty() || !host.userName.empty()) {
                    g_HistoryHosts[key] = host;
                }
            }
            pEntry += wcslen(pEntry) + 1;
        }
    }
}

void SaveHistoryHosts() {
    wchar_t szPath[MAX_PATH];
    wcscpy_s(szPath, g_Settings.szConfigPath);
    wchar_t* p = wcsrchr(szPath, L'\\');
    if (p) *(p + 1) = L'\0';
    wcscat_s(szPath, L"history.ini");

    std::lock_guard<std::mutex> lock(g_HistoryHostsMutex);
    WritePrivateProfileStringW(L"History", NULL, NULL, szPath);

    for (std::map<std::wstring, HistoryHost>::const_iterator it = g_HistoryHosts.begin(); it != g_HistoryHosts.end(); ++it) {
        const std::wstring& key = it->first;
        const HistoryHost& host = it->second;
        std::wstring val =
            host.ip + L"|" +
            host.computerName + L"|" +
            host.userName + L"|" +
            host.osVersion + L"|" +
            host.installTime + L"|" +
            host.lastSeen + L"|" +
            host.remark + L"|" +
            host.location + L"|" +
            host.programPath + L"|" +
            std::to_wstring(host.clientUniqueId);
        WritePrivateProfileStringW(L"History", key.c_str(), val.c_str(), szPath);
    }
}
