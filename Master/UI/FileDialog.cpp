#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// FileDialog.cpp - 文件管理对话框实现
#include "FileDialog.h"
#include "InputDialog.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include "../GlobalState.h"
#include <CommCtrl.h>
#include <commdlg.h>
#include <vector>
#include <map>
#include <mutex>
#include <shellapi.h>
#include <thread>
#include <fstream>
#include "../../Common/Utils.h"

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);

namespace Formidable {
namespace UI {

static std::map<HWND, uint32_t> s_dlgToClientId;
static std::map<HWND, int> s_dlgViewMode; // 1=列表(详细), 2=图标
static std::map<HWND, std::wstring> s_dlgRenameOldName;
static HIMAGELIST s_hFileImageListSmall = NULL;
static HIMAGELIST s_hFileImageListLarge = NULL;

#define WM_UPDATE_PROGRESS (WM_USER + 201)
#define IDC_PROGRESS_BAR 1099

// 辅助：发送控制命令
static void SendRemoteCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1, uint32_t arg2, const void* data = nullptr, size_t dataLen = 0) {
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return;

    size_t bodySize = sizeof(CommandPkg) + dataLen;
    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
    PkgHeader* h = (PkgHeader*)buffer.data();
    memcpy(h->flag, "FRMD26?", 7);
    h->originLen = (int)bodySize;
    h->totalLen = (int)buffer.size();

    CommandPkg* p = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    p->cmd = (CommandType)cmd;
    p->arg1 = arg1;
    p->arg2 = arg2;
    if (data && dataLen > 0) memcpy(p->data, data, dataLen);

    SendDataToClient(client, buffer.data(), (int)buffer.size());
}

// 文件上传后台任务
static void SendFileTask(HWND hDlg, uint32_t clientId, std::wstring localPath, std::wstring remotePath) {
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return;

    // 1. 初始化上传 (arg2=0)
    std::string rPath = WideToUTF8(remotePath);
    SendRemoteCommand(clientId, CMD_FILE_UPLOAD, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
    
    // 给客户端一点点准备时间
    Sleep(200);

    // 2. 发送数据块
    std::ifstream file(localPath, std::ios::binary);
    if (file.is_open()) {
        // 获取文件大小
        file.seekg(0, std::ios::end);
        int64_t totalSize = (int64_t)file.tellg();
        file.seekg(0, std::ios::beg);
        int64_t totalSent = 0;

        char buf[16384]; // 16KB 块
        while (file.read(buf, sizeof(buf)) || (file.gcount() > 0)) {
            SendRemoteCommand(clientId, CMD_FILE_DATA, (uint32_t)file.gcount(), 0, buf, (size_t)file.gcount());
            
            totalSent += (int64_t)file.gcount();
            if (totalSize > 0) {
                int progress = (int)((totalSent * 100) / totalSize);
                PostMessageW(hDlg, WM_UPDATE_PROGRESS, progress, 0);
            }

            // 简单控流，防止消息队列缓冲区满
            Sleep(10);
        }
        file.close();
    }

    // 3. 结束上传 (arg2=1)
    SendRemoteCommand(clientId, CMD_FILE_UPLOAD, 0, 1);
    PostMessageW(hDlg, WM_UPDATE_PROGRESS, 100, 0);
}

// 初始化文件图标列表
void InitFileImageList() {
    if (s_hFileImageListSmall && s_hFileImageListLarge) return;

    SHFILEINFOW sfi = { 0 };
    if (!s_hFileImageListSmall) {
        s_hFileImageListSmall = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    }
    if (!s_hFileImageListLarge) {
        s_hFileImageListLarge = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_LARGEICON);
    }
}

// 获取文件图标索引
int GetFileIconIndex(const std::wstring& fileName, bool isDirectory) {
    SHFILEINFOW sfi = { 0 };
    DWORD flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
    DWORD attr = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    SHGetFileInfoW(fileName.c_str(), attr, &sfi, sizeof(sfi), flags);
    return sfi.iIcon;
}

// 设置列表视图模式
void SetListViewMode(HWND hList, int mode) {
    if (!hList) return;

    LONG_PTR dwStyle = GetWindowLongPtrW(hList, GWL_STYLE);
    dwStyle &= ~(LVS_TYPEMASK);
    if (mode == 2) dwStyle |= LVS_ICON; else dwStyle |= LVS_REPORT;
    SetWindowLongPtrW(hList, GWL_STYLE, dwStyle);
    SetWindowPos(hList, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    if (s_hFileImageListSmall) ListView_SetImageList(hList, s_hFileImageListSmall, LVSIL_SMALL);
    if (s_hFileImageListLarge) ListView_SetImageList(hList, s_hFileImageListLarge, LVSIL_NORMAL);

    DWORD ex = ListView_GetExtendedListViewStyle(hList);
    ex |= LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(hList, ex);

    if (mode == 2) {
        int cx = 140;
        int cy = 90;
        SendMessageW(hList, LVM_SETICONSPACING, 0, MAKELPARAM(cx, cy));
    }
}

static std::wstring GetRemoteParentPath(const std::wstring& currentPath) {
    std::wstring p = currentPath;
    if (p.empty()) return L"";

    while (p.length() > 0 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    
    // 如果是盘符 (C: 或 C)
    if (p.length() <= 2 && p.back() == L':') return L"";
    
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    if (pos < 2) { // "C:\" -> "C:"
         return L"";
    }
    return p.substr(0, pos + 1);
}

static void RefreshRemoteList(HWND hDlg) {
    uint32_t clientId = s_dlgToClientId[hDlg];

    wchar_t szPath[MAX_PATH] = { 0 };
    GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
    std::string path = WideToUTF8(szPath);
    
    if (path.empty()) {
        SendRemoteCommand(clientId, CMD_DRIVE_LIST, 0, 0);
    } else {
        if (path.back() != '\\') path += "\\";
        SendRemoteCommand(clientId, CMD_FILE_LIST, (uint32_t)path.size(), 0, path.c_str(), path.size());
    }
}

INT_PTR CALLBACK FileDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        s_dlgViewMode[hDlg] = 1; // 默认：列表(报表)

        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_FILE)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_FILE)));

        // 初始化文件图标
        InitFileImageList();

        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);

        // 设置图标列表（小/大）
        if (s_hFileImageListSmall) ListView_SetImageList(hList, s_hFileImageListSmall, LVSIL_SMALL);
        if (s_hFileImageListLarge) ListView_SetImageList(hList, s_hFileImageListLarge, LVSIL_NORMAL);

        // 启用拖放上传
        DragAcceptFiles(hDlg, TRUE);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"文件名"; lvc.cx = 250; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"大小";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"类型";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"修改时间"; lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, L"C:\\");
        
        // 创建进度条
        HWND hProg = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 
            0, 0, 100, 15, hDlg, (HMENU)IDC_PROGRESS_BAR, g_hInstance, NULL);
        SendMessage(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // 延迟自动刷新
        SetTimer(hDlg, 1, 500, NULL);
        
        return (INT_PTR)TRUE;
    }
    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hDlg, 1);
            RefreshRemoteList(hDlg);
        }
        break;
    case WM_UPDATE_PROGRESS:
        SendDlgItemMessage(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, wParam, 0);
        return (INT_PTR)TRUE;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        uint32_t clientId = s_dlgToClientId[hDlg];
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

        wchar_t szRemotePath[MAX_PATH];
        GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szRemotePath, MAX_PATH);

        std::wstring remoteDir = szRemotePath;
        if (remoteDir.empty()) remoteDir = L"C:\\";
        if (remoteDir.back() != L'\\') remoteDir += L'\\';

        for (UINT i = 0; i < fileCount; i++) {
            wchar_t szLocalPath[MAX_PATH];
            DragQueryFileW(hDrop, i, szLocalPath, MAX_PATH);
            
            // 获取文件名
            wchar_t* fileName = wcsrchr(szLocalPath, L'\\');
            if (fileName) fileName++; else fileName = szLocalPath;

            std::wstring fullRemotePath = remoteDir + fileName;

            // 启动上传线程
            std::thread(SendFileTask, hDlg, clientId, std::wstring(szLocalPath), fullRemotePath).detach();
        }
        MessageBoxW(hDlg, L"已开始后台上传拖入的文件", L"提示", MB_OK);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        int addressBarHeight = 28;
        int toolbarHeight = 36;
        int statusBarHeight = 22;
        int bottomAreaHeight = 28;
        int spacing = 4;
        int margin = 6;

        // 地址栏区域
        int addressBarY = margin;
        int searchBoxWidth = 50;
        MoveWindow(GetDlgItem(hDlg, IDC_STATIC_FILE_PATH_REMOTE), margin, addressBarY + 8, 50, 12, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_FILE_PATH_REMOTE), margin + 55, addressBarY + 4, width - margin * 2 - 120 - searchBoxWidth - 10, 20, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_GO_REMOTE), width - margin - 45 - searchBoxWidth - 10, addressBarY + 2, 42, 24, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_FILE_SEARCH), width - margin - searchBoxWidth - 2, addressBarY + 4, searchBoxWidth, 20, TRUE);

        // 工具栏区域
        int toolbarY = addressBarY + addressBarHeight + spacing;
        int toolbarBtnWidth = 60;
        int toolbarBtnHeight = 26;
        int toolbarBtnSpacing = 4;
        int toolbarX = margin;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_BACK), toolbarX, toolbarY + 5, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_MKDIR), toolbarX, toolbarY + 5, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_RENAME), toolbarX, toolbarY + 5, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_DELETE), toolbarX, toolbarY + 5, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_REFRESH), toolbarX, toolbarY + 5, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_VIEW), toolbarX, toolbarY + 5, toolbarBtnWidth, toolbarBtnHeight, TRUE);

        // 底部区域（进度条）
        int bottomAreaY = height - statusBarHeight - bottomAreaHeight - margin;
        MoveWindow(GetDlgItem(hDlg, IDC_PROGRESS_BAR), margin, bottomAreaY + 7, width - margin * 2, 14, TRUE);

        // 状态栏
        HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
        MoveWindow(hStatusBar, 0, height - statusBarHeight, width, statusBarHeight, TRUE);

        // 文件列表区域
        int listY = toolbarY + toolbarHeight + spacing;
        int listHeight = bottomAreaY - listY - spacing;
        if (listHeight < 0) listHeight = 0;
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE), margin, listY, width - margin * 2, listHeight, TRUE);

        // 动态调整列宽
        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        int listWidth = width - margin * 2 - 20;
        LVCOLUMNW col = { 0 };
        col.mask = LVCF_WIDTH;
        col.cx = (int)(listWidth * 0.40);
        SendMessageW(hList, LVM_SETCOLUMNW, 0, (LPARAM)&col);
        col.cx = (int)(listWidth * 0.15);
        SendMessageW(hList, LVM_SETCOLUMNW, 1, (LPARAM)&col);
        col.cx = (int)(listWidth * 0.15);
        SendMessageW(hList, LVM_SETCOLUMNW, 2, (LPARAM)&col);
        col.cx = (int)(listWidth * 0.30);
        SendMessageW(hList, LVM_SETCOLUMNW, 3, (LPARAM)&col);

        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;

        if (nm->idFrom == IDC_LIST_FILE_REMOTE) {
            if (nm->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);

                HMENU hMenu = CreatePopupMenu();
                HMENU hViewMenu = CreatePopupMenu();

                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 1 ? MF_CHECKED : 0), IDM_FILE_VIEW_LIST, L"列表");
                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 2 ? MF_CHECKED : 0), IDM_FILE_VIEW_ICON, L"图标");

                AppendMenuW(hMenu, MF_STRING, IDM_FILE_REFRESH, L"刷新(&R)");
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"查看(&V)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_OPEN, L"打开(&O)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_EXECUTE, L"远程执行(&E)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_UPLOAD, L"上传文件(&U)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_DOWNLOAD, L"下载文件(&D)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_NEW_FOLDER, L"新建文件夹(&N)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_RENAME, L"重命名(&M)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_DELETE, L"删除(&L)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_COPY_PATH, L"复制路径(&C)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_PROPERTIES, L"属性(&P)");

                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
            } else if (nm->code == NM_DBLCLK) {
                auto* lpnmitem = (LPNMITEMACTIVATE)lParam;
                if (lpnmitem->iItem >= 0) {
                    SendMessage(hDlg, WM_COMMAND, IDM_FILE_OPEN, 0);
                }
            } else if (nm->code == LVN_BEGINLABELEDITW) {
                auto* info = (NMLVDISPINFOW*)lParam;
                wchar_t oldName[MAX_PATH] = { 0 };
                ListView_GetItemText(nm->hwndFrom, info->item.iItem, 0, oldName, MAX_PATH);
                s_dlgRenameOldName[hDlg] = oldName;
                return (INT_PTR)FALSE;
            } else if (nm->code == LVN_ENDLABELEDITW) {
                auto* info = (NMLVDISPINFOW*)lParam;
                if (!info->item.pszText || !info->item.pszText[0]) return (INT_PTR)FALSE;

                uint32_t clientId = s_dlgToClientId[hDlg];

                wchar_t szPath[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
                std::wstring dir = szPath;
                if (dir.empty()) dir = L"C:\\";
                if (dir.back() != L'\\') dir += L'\\';

                std::wstring oldFull = dir + s_dlgRenameOldName[hDlg];
                std::wstring newFull = dir + info->item.pszText;

                std::string payload = WideToUTF8(oldFull) + "|" + WideToUTF8(newFull);
                SendRemoteCommand(clientId, CMD_FILE_RENAME, (uint32_t)payload.size(), 0, payload.c_str(), payload.size());

                std::string refresh = WideToUTF8(dir);
                SendRemoteCommand(clientId, CMD_FILE_LIST, (uint32_t)refresh.size(), 0, refresh.c_str(), refresh.size());

                return (INT_PTR)TRUE;
            } else if (nm->code == LVN_COLUMNCLICK) {
                auto* pnmlv = (LPNMLISTVIEW)lParam;
                HWND hList = pnmlv->hdr.hwndFrom;

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
            }
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        
        switch (LOWORD(wParam)) {
        case IDC_BTN_FILE_REMOTE_BACK: {
            wchar_t szPath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
            std::wstring parent = GetRemoteParentPath(szPath);
            SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, parent.c_str());
            RefreshRemoteList(hDlg);
            break;
        }
        case IDC_BTN_FILE_REMOTE_MKDIR:
            SendMessageW(hDlg, WM_COMMAND, IDM_FILE_NEW_FOLDER, 0);
            break;
        case IDC_BTN_FILE_REMOTE_RENAME:
            SendMessageW(hDlg, WM_COMMAND, IDM_FILE_RENAME, 0);
            break;
        case IDC_BTN_FILE_REMOTE_DELETE:
            SendMessageW(hDlg, WM_COMMAND, IDM_FILE_DELETE, 0);
            break;
        case IDC_BTN_FILE_REMOTE_REFRESH:
            SendMessageW(hDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
            break;
        case IDC_BTN_FILE_VIEW: {
            HWND hViewBtn = (HWND)lParam;
            RECT rc;
            GetWindowRect(hViewBtn, &rc);
            HMENU hViewMenu = CreatePopupMenu();
            AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 1 ? MF_CHECKED : 0), IDM_FILE_VIEW_LIST, L"列表(&L)");
            AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 2 ? MF_CHECKED : 0), IDM_FILE_VIEW_ICON, L"图标(&I)");
            TrackPopupMenu(hViewMenu, TPM_LEFTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, rc.left, rc.bottom, 0, hDlg, NULL);
            DestroyMenu(hViewMenu);
            break;
        }
        case IDM_FILE_VIEW_LIST:
            s_dlgViewMode[hDlg] = 1;
            SetListViewMode(hList, 1);
            break;
        case IDM_FILE_VIEW_ICON:
            s_dlgViewMode[hDlg] = 2;
            SetListViewMode(hList, 2);
            break;
        case IDC_BTN_FILE_GO_REMOTE: {
            RefreshRemoteList(hDlg);
            break;
        }
        case IDC_EDIT_FILE_SEARCH: {
            if (HIWORD(wParam) == EN_CHANGE) {
                HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
                wchar_t szSearch[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_SEARCH, szSearch, MAX_PATH);

                size_t searchLen = wcslen(szSearch);

                if (searchLen == 0) {
                    RefreshRemoteList(hDlg);
                    break;
                }

                int itemCount = ListView_GetItemCount(hList);
                for (int i = 0; i < itemCount; i++) {
                    wchar_t szName[MAX_PATH] = { 0 };
                    ListView_GetItemText(hList, i, 0, szName, MAX_PATH);

                    if (_wcsnicmp(szName, szSearch, searchLen) != 0) {
                        ListView_DeleteItem(hList, i);
                        i--;
                        itemCount--;
                    }
                }
            }
            break;
        }
        case IDM_FILE_REFRESH: {
            RefreshRemoteList(hDlg);
            break;
        }
        case IDM_FILE_OPEN: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szName[MAX_PATH] = { 0 };
                wchar_t szType[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                ListView_GetItemText(hList, selected, 2, szType, MAX_PATH);

                // 只要不是文件，都认为是目录/驱动器，可以进入
                if (wcscmp(szType, L"文件") != 0) {
                    wchar_t szPath[MAX_PATH] = { 0 };
                    GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
                    std::wstring newPath = szPath;
                    
                    if (newPath.empty()) {
                        newPath = szName;
                    } else {
                        if (newPath.back() != L'\\') newPath += L'\\';
                        newPath += szName;
                    }
                    SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, newPath.c_str());

                    RefreshRemoteList(hDlg);
                } else {
                    SendMessageW(hDlg, WM_COMMAND, IDM_FILE_DOWNLOAD, 0);
                }
            }
            break;
        }
        case IDM_FILE_EXECUTE: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szName[MAX_PATH] = { 0 };
                wchar_t szType[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                ListView_GetItemText(hList, selected, 2, szType, MAX_PATH);
                if (wcscmp(szType, L"文件夹") == 0) break;

                wchar_t szPath[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
                std::wstring full = szPath;
                if (full.empty()) full = L"C:\\";
                if (full.back() != L'\\') full += L'\\';
                full += szName;

                std::string rPath = WideToUTF8(full);
                SendRemoteCommand(clientId, CMD_FILE_RUN, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
            }
            break;
        }
        case IDM_FILE_PROPERTIES: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t name[MAX_PATH] = { 0 };
                wchar_t size[MAX_PATH] = { 0 };
                wchar_t type[MAX_PATH] = { 0 };
                wchar_t time[MAX_PATH] = { 0 };
                wchar_t dir[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, selected, 0, name, MAX_PATH);
                ListView_GetItemText(hList, selected, 1, size, MAX_PATH);
                ListView_GetItemText(hList, selected, 2, type, MAX_PATH);
                ListView_GetItemText(hList, selected, 3, time, MAX_PATH);
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, dir, MAX_PATH);

                std::wstring full = dir;
                if (full.empty()) full = L"C:\\";
                if (full.back() != L'\\') full += L'\\';
                full += name;

                std::wstring msg = L"路径: " + full + L"\r\n类型: " + type + L"\r\n大小: " + size + L"\r\n修改时间: " + time;
                MessageBoxW(hDlg, msg.c_str(), L"属性", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case IDM_FILE_UPLOAD: {
            // 选择本地文件上传
            OPENFILENAMEW ofn = { 0 };
            wchar_t szFile[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"所有文件\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                wchar_t szRemotePath[MAX_PATH];
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szRemotePath, MAX_PATH);

                std::wstring remoteDir = szRemotePath;
                if (remoteDir.empty()) remoteDir = L"C:\\";
                if (remoteDir.back() != L'\\') remoteDir += L'\\';

                wchar_t* fileName = wcsrchr(szFile, L'\\');
                if (fileName) fileName++; else fileName = szFile;

                std::wstring fullRemotePath = remoteDir + fileName;
                std::thread(SendFileTask, hDlg, clientId, std::wstring(szFile), fullRemotePath).detach();
                MessageBoxW(hDlg, L"已开始上传文件", L"提示", MB_OK);
            }
            break;
        }
        case IDM_FILE_DOWNLOAD: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szName[MAX_PATH];
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);

                wchar_t szPath[MAX_PATH];
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);

                std::wstring fullRemote = szPath;
                if (fullRemote.back() != L'\\') fullRemote += L'\\';
                fullRemote += szName;

                // 选择本地保存路径
                OPENFILENAMEW ofn = { 0 };
                wchar_t szLocal[MAX_PATH];
                wcscpy_s(szLocal, szName);
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hDlg;
                ofn.lpstrFile = szLocal;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = L"所有文件\0*.*\0";
                ofn.Flags = OFN_OVERWRITEPROMPT;
                if (GetSaveFileNameW(&ofn)) {
                    std::shared_ptr<ConnectedClient> client;
                    {
                        std::lock_guard<std::mutex> lock(g_ClientsMutex);
                        if (g_Clients.count(clientId)) client = g_Clients[clientId];
                    }
                    if (client) {
                        if (client->hFileDownload != INVALID_HANDLE_VALUE) CloseHandle(client->hFileDownload);
                        client->hFileDownload = CreateFileW(szLocal, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        client->downloadPath = szLocal;
                        
                        // 获取/估算文件大小以显示进度
                        wchar_t szSize[64] = {0};
                        ListView_GetItemText(hList, selected, 1, szSize, 64);
                        // 解析大小字符串 (e.g. "1.5 MB")
                        double sizeVal = 0.0;
                        try {
                            sizeVal = std::stod(szSize);
                        } catch(...) {}
                        
                        unsigned long long totalBytes = 0;
                        if (wcsstr(szSize, L"GB")) totalBytes = (unsigned long long)(sizeVal * 1024 * 1024 * 1024);
                        else if (wcsstr(szSize, L"MB")) totalBytes = (unsigned long long)(sizeVal * 1024 * 1024);
                        else if (wcsstr(szSize, L"KB")) totalBytes = (unsigned long long)(sizeVal * 1024);
                        else totalBytes = (unsigned long long)sizeVal;
                        
                        client->totalDownloadSize = totalBytes;
                        client->currentDownloadSize = 0;
                        
                        // 重置进度条
                        SendDlgItemMessage(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, 0, 0);

                        // 发送下载命令
                        std::string rPath = WideToUTF8(fullRemote);
                        SendRemoteCommand(clientId, CMD_FILE_DOWNLOAD, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
                        MessageBoxW(hDlg, L"已开始下载文件", L"提示", MB_OK);
                    }
                }
            }
            break;
        }
        case IDM_FILE_DELETE: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                if (MessageBoxW(hDlg, L"确定要删除选中的文件/文件夹吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    wchar_t szName[MAX_PATH] = { 0 };
                    wchar_t szPath[MAX_PATH] = { 0 };
                    ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                    GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);

                    std::wstring full = szPath;
                    if (full.empty()) full = L"C:\\";
                    if (full.back() != L'\\') full += L'\\';
                    full += szName;

                    std::string rPath = WideToUTF8(full);
                    SendRemoteCommand(clientId, CMD_FILE_DELETE, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());

                    std::string refresh = WideToUTF8(std::wstring(szPath));
                    if (refresh.empty()) refresh = "C:\\";
                    SendRemoteCommand(clientId, CMD_FILE_LIST, (uint32_t)refresh.size(), 0, refresh.c_str(), refresh.size());
                }
            }
            break;
        }
        case IDM_FILE_RENAME: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                ListView_EditLabel(hList, selected);
            }
            break;
        }
        case IDM_FILE_NEW_FOLDER: {
            std::wstring folderName;
            if (!InputDialog::Show(hDlg, L"新建文件夹", L"文件夹名:", folderName)) break;
            if (folderName.empty()) break;

            wchar_t szPath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);

            std::wstring full = szPath;
            if (full.empty()) full = L"C:\\";
            if (full.back() != L'\\') full += L'\\';
            full += folderName;

            std::string rPath = WideToUTF8(full);
            SendRemoteCommand(clientId, CMD_FILE_MKDIR, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());

            std::string refresh = WideToUTF8(std::wstring(szPath));
            if (refresh.empty()) refresh = "C:\\";
            SendRemoteCommand(clientId, CMD_FILE_LIST, (uint32_t)refresh.size(), 0, refresh.c_str(), refresh.size());
            break;
        }
        case IDM_FILE_COPY_PATH: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szPath[MAX_PATH] = { 0 };
                wchar_t szName[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                
                std::wstring fullPath = szPath;
                if (fullPath.back() != L'\\') fullPath += L'\\';
                fullPath += szName;
                
                // 复制到剪贴板
                if (OpenClipboard(hDlg)) {
                    EmptyClipboard();
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (fullPath.size() + 1) * sizeof(wchar_t));
                    if (hMem) {
                        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                        wcscpy_s(pMem, fullPath.size() + 1, fullPath.c_str());
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                    CloseClipboard();
                }
            }
            break;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (s_dlgToClientId.count(hDlg)) {
            uint32_t clientId = s_dlgToClientId[hDlg];
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) {
                g_Clients[clientId]->hFileDlg = NULL;
            }
            s_dlgToClientId.erase(hDlg);
            s_dlgViewMode.erase(hDlg);
            s_dlgRenameOldName.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

HWND FileDialog::Show(HWND hParent, uint32_t clientId) {
    return CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_FILE), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
