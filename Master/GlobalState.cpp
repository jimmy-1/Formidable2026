#include "GlobalState.h"
#include "../Common/ClientTypes.h"
#include "../Common/Config.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>

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
std::map<HWND, ListViewSortInfo> g_SortInfo;

// ListView排序回调函数
int CALLBACK ListViewCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
    ListViewSortInfo* psi = (ListViewSortInfo*)lParamSort;
    HWND hList = psi->hwndList;
    int col = psi->column;
    bool asc = psi->ascending;
    
    wchar_t text1[512] = { 0 };
    wchar_t text2[512] = { 0 };
    
    LVITEMW lvi1 = { 0 };
    lvi1.mask = LVIF_TEXT;
    lvi1.iItem = (int)lParam1;
    lvi1.iSubItem = col;
    lvi1.pszText = text1;
    lvi1.cchTextMax = 512;
    SendMessageW(hList, LVM_GETITEMTEXT, (WPARAM)lParam1, (LPARAM)&lvi1);
    
    LVITEMW lvi2 = { 0 };
    lvi2.mask = LVIF_TEXT;
    lvi2.iItem = (int)lParam2;
    lvi2.iSubItem = col;
    lvi2.pszText = text2;
    lvi2.cchTextMax = 512;
    SendMessageW(hList, LVM_GETITEMTEXT, (WPARAM)lParam2, (LPARAM)&lvi2);
    
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
