#include "GlobalState.h"
#include "../Common/ClientTypes.h"
#include "../Common/Config.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <commctrl.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

// 全局变量定义
HINSTANCE g_hInstance;
HANDLE g_hInstanceMutex = NULL;
HWND g_hMainWnd;
HWND g_hListClients;
HWND g_hListLogs;
HWND g_hToolbar;
HWND g_hStatusBar;
HWND g_hGroupTab = NULL;
std::set<std::string> g_GroupList;
std::string g_selectedGroup = "默认";
std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
std::mutex g_ClientsMutex;
uint32_t g_NextClientId = 1;
Formidable::NetworkServer* g_pNetworkServer = nullptr;
std::map<CONNID, uint32_t> g_ConnIdToClientId;
std::mutex g_ConnIdMapMutex;
std::map<CONNID, std::vector<BYTE>> g_RecvBuffers;
std::mutex g_RecvBufferMutex;
int g_nListenPort = Formidable::DEFAULT_PORT;

// 终端专用资源
HFONT g_hTermFont = NULL;
HBRUSH g_hTermEditBkBrush = NULL;

ServerSettings g_Settings;
std::map<HWND, HBITMAP> g_WindowPreviews;
std::map<HWND, Formidable::ListViewSortInfo> g_SortInfo;

std::map<std::wstring, std::wstring> g_SavedRemarks;
std::mutex g_SavedRemarksMutex;

std::map<std::wstring, HistoryHost> g_HistoryHosts;
std::mutex g_HistoryHostsMutex;

static HBRUSH g_hUiBackgroundBrush = NULL;
static COLORREF g_uiBackgroundColor = RGB(248, 248, 248);
static COLORREF g_uiTextColor = RGB(20, 20, 20);
static COLORREF g_uiListBackgroundColor = RGB(255, 255, 255);

static void EnsureUiBrush() {
    if (!g_hUiBackgroundBrush) {
        g_hUiBackgroundBrush = CreateSolidBrush(g_uiBackgroundColor);
    }
}

static void ApplyWindowCorner(HWND hWnd) {
    typedef HRESULT (WINAPI* DwmSetWindowAttributePtr)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetWindowAttributePtr pDwmSetWindowAttribute = nullptr;
    static bool s_checked = false;
    if (!s_checked) {
        s_checked = true;
        HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
        if (hDwm) {
            pDwmSetWindowAttribute = (DwmSetWindowAttributePtr)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        }
    }
    if (pDwmSetWindowAttribute) {
        int pref = DWMWCP_ROUND;
        pDwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    }
}

static void ApplyThemeToControl(HWND hCtrl) {
    if (!hCtrl) return;
    wchar_t cls[64] = { 0 };
    GetClassNameW(hCtrl, cls, 63);

    auto removeBorder = [hCtrl]() {
        LONG_PTR style = GetWindowLongPtrW(hCtrl, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtrW(hCtrl, GWL_EXSTYLE);
        bool changed = false;
        if (style & WS_BORDER) {
            style &= ~WS_BORDER;
            changed = true;
        }
        if (exStyle & WS_EX_CLIENTEDGE) {
            exStyle &= ~WS_EX_CLIENTEDGE;
            changed = true;
        }
        if (changed) {
            SetWindowLongPtrW(hCtrl, GWL_STYLE, style);
            SetWindowLongPtrW(hCtrl, GWL_EXSTYLE, exStyle);
            SetWindowPos(hCtrl, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    };

    typedef HRESULT (WINAPI* SetWindowThemePtr)(HWND, LPCWSTR, LPCWSTR);
    static SetWindowThemePtr pSetWindowTheme = nullptr;
    static bool s_themeChecked = false;
    if (!s_themeChecked) {
        s_themeChecked = true;
        HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
        if (hUxTheme) {
            pSetWindowTheme = (SetWindowThemePtr)GetProcAddress(hUxTheme, "SetWindowTheme");
        }
    }
    if (pSetWindowTheme) {
        pSetWindowTheme(hCtrl, L"Explorer", NULL);
    }

    if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
        removeBorder();
        DWORD ex = ListView_GetExtendedListViewStyle(hCtrl);
        ex |= LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP;
        ex &= ~LVS_EX_GRIDLINES;
        ListView_SetExtendedListViewStyle(hCtrl, ex);
        ListView_SetBkColor(hCtrl, g_uiListBackgroundColor);
        ListView_SetTextBkColor(hCtrl, g_uiListBackgroundColor);
        ListView_SetTextColor(hCtrl, g_uiTextColor);
        return;
    }
    if (_wcsicmp(cls, WC_TREEVIEWW) == 0) {
        removeBorder();
        TreeView_SetBkColor(hCtrl, g_uiListBackgroundColor);
        TreeView_SetTextColor(hCtrl, g_uiTextColor);
        return;
    }
    if (_wcsicmp(cls, WC_TABCONTROLW) == 0) {
        return;
    }
    if (_wcsicmp(cls, STATUSCLASSNAMEW) == 0) {
        return;
    }
    if (_wcsicmp(cls, TOOLBARCLASSNAMEW) == 0) {
        return;
    }
}

static BOOL CALLBACK ApplyThemeEnumProc(HWND hWnd, LPARAM lParam) {
    ApplyThemeToControl(hWnd);
    return TRUE;
}

void ApplyModernTheme(HWND hWnd) {
    if (!hWnd) return;
    EnsureUiBrush();
    ApplyWindowCorner(hWnd);
    EnumChildWindows(hWnd, ApplyThemeEnumProc, 0);
}

void ReleaseModernTheme() {
    if (g_hUiBackgroundBrush) {
        DeleteObject(g_hUiBackgroundBrush);
        g_hUiBackgroundBrush = NULL;
    }
}

// ListView排序回调函数
int CALLBACK ListViewCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
    Formidable::ListViewSortInfo* psi = (Formidable::ListViewSortInfo*)lParamSort;
    HWND hList = psi->hwndList;
    int col = psi->column;
    bool asc = psi->ascending;
    
    wchar_t text1[512] = { 0 };
    wchar_t text2[512] = { 0 };
    
    LVFINDINFOW fi1 = { 0 };
    fi1.flags = LVFI_PARAM;
    fi1.lParam = lParam1;
    int idx1 = ListView_FindItem(hList, -1, &fi1);
    if (idx1 < 0) idx1 = (int)lParam1;

    LVFINDINFOW fi2 = { 0 };
    fi2.flags = LVFI_PARAM;
    fi2.lParam = lParam2;
    int idx2 = ListView_FindItem(hList, -1, &fi2);
    if (idx2 < 0) idx2 = (int)lParam2;

    LVITEMW lvi1 = { 0 };
    lvi1.mask = LVIF_TEXT;
    lvi1.iItem = idx1;
    lvi1.iSubItem = col;
    lvi1.pszText = text1;
    lvi1.cchTextMax = 512;
    SendMessageW(hList, LVM_GETITEMTEXT, (WPARAM)idx1, (LPARAM)&lvi1);
    
    LVITEMW lvi2 = { 0 };
    lvi2.mask = LVIF_TEXT;
    lvi2.iItem = idx2;
    lvi2.iSubItem = col;
    lvi2.pszText = text2;
    lvi2.cchTextMax = 512;
    SendMessageW(hList, LVM_GETITEMTEXT, (WPARAM)idx2, (LPARAM)&lvi2);
    
    // 尝试数值比较
    wchar_t* end1 = nullptr;
    wchar_t* end2 = nullptr;
    long long num1 = wcstoll(text1, &end1, 10);
    long long num2 = wcstoll(text2, &end2, 10);
    
    int result = 0;
    if (end1 != text1 && end2 != text2 && *end1 == L'\0' && *end2 == L'\0') {
        // 都是数字，按数值比较
        if (num1 < num2) result = -1;
        else if (num1 > num2) result = 1;
    } else {
        // 按字符串比较
        result = _wcsicmp(text1, text2);
    }
    
    return asc ? result : -result;
}
