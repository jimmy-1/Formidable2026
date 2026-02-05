#include "Config.h"
#include "GlobalState.h"
#include "../Common/Config.h"  // For DEFAULT_PORT
#include <windows.h>
#include <stdio.h> // for swprintf_s

// 配置持久化
void LoadSettings() {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    wchar_t* p = wcsrchr(szPath, L'\\');
    if (p) *(p + 1) = L'\0';
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
}
