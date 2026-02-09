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
static std::map<HWND, uint64_t> s_transferTotalBytes;
static std::map<HWND, uint64_t> s_transferLastBytes;
static std::map<HWND, uint64_t> s_transferLastTick;
static std::map<HWND, double> s_transferSpeedBps;
static std::map<HWND, bool> s_dlgMonitorActive;
static std::mutex s_transferMutex;
static HIMAGELIST s_hFileImageListSmall = NULL;
static HIMAGELIST s_hFileImageListLarge = NULL;
static const unsigned char kTransferKey[] = {
    0x3A, 0x7F, 0x12, 0x9C, 0x55, 0xE1, 0x08, 0x6D,
    0x4B, 0x90, 0x2E, 0xA7, 0x1C, 0xF3, 0xB5, 0x63
};
static const uint32_t kEncryptFlag = 0x80000000u;
static const bool kEnableFileTransferEncrypt = true;
static const uint64_t kDownloadChunkSize = 64 * 1024;

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

static bool IsFullRemotePath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
    return false;
}

static std::wstring GetDirectoryPart(const std::wstring& full) {
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return full.substr(0, pos + 1);
}

static std::wstring BuildRemoteFullPath(HWND hDlg, const std::wstring& name) {
    if (IsFullRemotePath(name)) return name;
    wchar_t szPath[MAX_PATH] = { 0 };
    GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
    std::wstring full = szPath;
    if (full.empty()) full = L"C:\\";
    if (full.back() != L'\\') full += L'\\';
    full += name;
    return full;
}

static std::wstring FormatSpeedText(double bps) {
    wchar_t buf[64] = { 0 };
    if (bps >= 1024.0 * 1024.0 * 1024.0)
        swprintf_s(buf, L"%.2f GB/s", bps / (1024.0 * 1024.0 * 1024.0));
    else if (bps >= 1024.0 * 1024.0)
        swprintf_s(buf, L"%.2f MB/s", bps / (1024.0 * 1024.0));
    else if (bps >= 1024.0)
        swprintf_s(buf, L"%.2f KB/s", bps / 1024.0);
    else
        swprintf_s(buf, L"%.0f B/s", bps);
    return buf;
}

static std::wstring FormatDurationText(uint64_t seconds) {
    uint64_t h = seconds / 3600;
    uint64_t m = (seconds % 3600) / 60;
    uint64_t s = seconds % 60;
    wchar_t buf[32] = { 0 };
    if (h > 0) swprintf_s(buf, L"%llu:%02llu:%02llu", h, m, s);
    else swprintf_s(buf, L"%02llu:%02llu", m, s);
    return buf;
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
    uint64_t finalSent = 0;
    if (file.is_open()) {
        // 获取文件大小
        file.seekg(0, std::ios::end);
        int64_t totalSize = (int64_t)file.tellg();
        file.seekg(0, std::ios::beg);
        int64_t totalSent = 0;
        {
            std::lock_guard<std::mutex> lock(s_transferMutex);
            s_transferTotalBytes[hDlg] = (totalSize > 0) ? (uint64_t)totalSize : 0;
            s_transferLastBytes[hDlg] = 0;
            s_transferLastTick[hDlg] = GetTickCount64();
            s_transferSpeedBps[hDlg] = 0.0;
        }

        char buf[16384]; // 16KB 块
        uint32_t chunkIndex = 0;
        while (file.read(buf, sizeof(buf)) || (file.gcount() > 0)) {
            int len = (int)file.gcount();
            uint32_t arg2 = 0;
            if (kEnableFileTransferEncrypt) {
                XorCryptBuffer(buf, len, chunkIndex);
                arg2 = kEncryptFlag | chunkIndex;
            }
            SendRemoteCommand(clientId, CMD_FILE_DATA, (uint32_t)len, arg2, buf, (size_t)len);
            chunkIndex++;
            
            totalSent += (int64_t)len;
            if (totalSize > 0) {
                int progress = (int)((totalSent * 100) / totalSize);
                PostMessageW(hDlg, WM_UPDATE_PROGRESS, progress, (LPARAM)totalSent);
            }

            // 简单控流，防止消息队列缓冲区满
            Sleep(10);
        }
        file.close();
        if (totalSent > 0) finalSent = (uint64_t)totalSent;
    }

    // 3. 结束上传 (arg2=1)
    SendRemoteCommand(clientId, CMD_FILE_UPLOAD, 0, 1);
    PostMessageW(hDlg, WM_UPDATE_PROGRESS, 100, (LPARAM)finalSent);
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
        s_dlgMonitorActive[hDlg] = false;
        {
            std::lock_guard<std::mutex> lock(s_transferMutex);
            s_transferTotalBytes[hDlg] = 0;
            s_transferLastBytes[hDlg] = 0;
            s_transferLastTick[hDlg] = GetTickCount64();
            s_transferSpeedBps[hDlg] = 0.0;
        }

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
        {
            HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
            if (hStatusBar) {
                uint64_t bytes = (uint64_t)lParam;
                double speed = 0.0;
                uint64_t totalBytes = 0;
                uint64_t remainSec = 0;
                {
                    std::lock_guard<std::mutex> lock(s_transferMutex);
                    uint64_t now = GetTickCount64();
                    uint64_t lastTick = s_transferLastTick[hDlg];
                    uint64_t lastBytes = s_transferLastBytes[hDlg];
                    if (bytes > 0 && lastTick > 0 && now > lastTick && bytes >= lastBytes) {
                        speed = (double)(bytes - lastBytes) * 1000.0 / (double)(now - lastTick);
                        s_transferSpeedBps[hDlg] = speed;
                    } else {
                        speed = s_transferSpeedBps[hDlg];
                    }
                    s_transferLastTick[hDlg] = now;
                    s_transferLastBytes[hDlg] = bytes;
                    totalBytes = s_transferTotalBytes[hDlg];
                }
                if (totalBytes > bytes && speed > 0.0) {
                    remainSec = (uint64_t)((double)(totalBytes - bytes) / speed);
                }
                std::wstring speedText = FormatSpeedText(speed);
                std::wstring remainText = FormatDurationText(remainSec);
                wchar_t szProgress[256] = {0};
                if (totalBytes > 0 && speed > 0.0) {
                    swprintf_s(szProgress, L"传输进度: %d%% | 速度: %s | 剩余: %s", (int)wParam, speedText.c_str(), remainText.c_str());
                } else if (speed > 0.0) {
                    swprintf_s(szProgress, L"传输进度: %d%% | 速度: %s", (int)wParam, speedText.c_str());
                } else {
                    swprintf_s(szProgress, L"传输进度: %d%%", (int)wParam);
                }
                SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)szProgress);
            }
        }
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
                bool monitorActive = s_dlgMonitorActive[hDlg];

                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 1 ? MF_CHECKED : 0), IDM_FILE_VIEW_LIST, L"列表");
                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 2 ? MF_CHECKED : 0), IDM_FILE_VIEW_ICON, L"图标");

                AppendMenuW(hMenu, MF_STRING, IDM_FILE_REFRESH, L"刷新(&R)");
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"查看(&V)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_OPEN, L"打开(&O)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_EXECUTE, L"远程执行(&E)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_PREVIEW, L"预览(&P)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_HISTORY, L"历史记录(&H)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_MONITOR, monitorActive ? L"停止监控(&T)" : L"开始监控(&T)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_UPLOAD, L"上传文件(&U)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_DOWNLOAD, L"下载文件(&D)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_NEW_FOLDER, L"新建文件夹(&N)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_RENAME, L"重命名(&M)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_DELETE, L"删除(&L)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_COMPRESS, L"压缩(&Z)");
                AppendMenuW(hMenu, MF_STRING, IDM_FILE_UNCOMPRESS, L"解压(&J)");
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
                std::wstring oldName = s_dlgRenameOldName[hDlg];
                std::wstring oldFull;
                std::wstring newFull;
                std::wstring dir;
                if (IsFullRemotePath(oldName)) {
                    dir = GetDirectoryPart(oldName);
                    oldFull = oldName;
                    newFull = dir + info->item.pszText;
                } else {
                    dir = szPath;
                    if (dir.empty()) dir = L"C:\\";
                    if (dir.back() != L'\\') dir += L'\\';
                    oldFull = dir + oldName;
                    newFull = dir + info->item.pszText;
                }

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
        case IDOK: {
            wchar_t szSearch[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_SEARCH, szSearch, MAX_PATH);
            if (wcslen(szSearch) == 0) {
                RefreshRemoteList(hDlg);
                break;
            }
            wchar_t szPath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
            std::wstring dir = szPath;
            if (dir.empty()) dir = L"C:\\";
            std::string payload = WideToUTF8(dir) + "|" + WideToUTF8(szSearch);
            SendRemoteCommand(clientId, CMD_FILE_SEARCH, (uint32_t)payload.size(), 1, payload.c_str(), payload.size());
            break;
        }
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
                    std::wstring newPath = IsFullRemotePath(szName) ? std::wstring(szName) : BuildRemoteFullPath(hDlg, szName);
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

                std::wstring full = BuildRemoteFullPath(hDlg, szName);

                std::string rPath = WideToUTF8(full);
                SendRemoteCommand(clientId, CMD_FILE_RUN, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
            }
            break;
        }
        case IDM_FILE_PREVIEW: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szName[MAX_PATH] = { 0 };
                wchar_t szType[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                ListView_GetItemText(hList, selected, 2, szType, MAX_PATH);
                if (wcscmp(szType, L"文件") != 0) {
                    MessageBoxW(hDlg, L"仅支持预览文件", L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                std::wstring full = BuildRemoteFullPath(hDlg, szName);
                std::string rPath = WideToUTF8(full);
                SendRemoteCommand(clientId, CMD_FILE_PREVIEW, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
            }
            break;
        }
        case IDM_FILE_HISTORY: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szName[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                std::wstring full = BuildRemoteFullPath(hDlg, szName);
                std::string rPath = WideToUTF8(full);
                SendRemoteCommand(clientId, CMD_FILE_HISTORY, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
            }
            break;
        }
        case IDM_FILE_MONITOR: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szName[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                std::wstring full = BuildRemoteFullPath(hDlg, szName);
                std::string rPath = WideToUTF8(full);
                bool active = s_dlgMonitorActive[hDlg];
                SendRemoteCommand(clientId, CMD_FILE_MONITOR, active ? 0 : 1, 0, rPath.c_str(), rPath.size());
                s_dlgMonitorActive[hDlg] = !active;
                HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
                if (hStatusBar) {
                    std::wstring msg = s_dlgMonitorActive[hDlg] ? (L"已开启监控: " + full) : L"已停止监控";
                    SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)msg.c_str());
                }
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
                ListView_GetItemText(hList, selected, 0, name, MAX_PATH);
                ListView_GetItemText(hList, selected, 1, size, MAX_PATH);
                ListView_GetItemText(hList, selected, 2, type, MAX_PATH);
                ListView_GetItemText(hList, selected, 3, time, MAX_PATH);

                std::wstring full = BuildRemoteFullPath(hDlg, name);

                std::wstring msg = L"路径: " + full + L"\r\n类型: " + type + L"\r\n大小: " + size + L"\r\n修改时间: " + time;
                MessageBoxW(hDlg, msg.c_str(), L"属性", MB_OK | MB_ICONINFORMATION);

                std::string rPath = WideToUTF8(full);
                SendRemoteCommand(clientId, CMD_FILE_SIZE, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
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

                std::wstring fullRemote = BuildRemoteFullPath(hDlg, szName);
                std::wstring baseName = szName;
                if (IsFullRemotePath(szName)) {
                    size_t pos = fullRemote.find_last_of(L"\\/");
                    if (pos != std::wstring::npos) baseName = fullRemote.substr(pos + 1);
                }

                // 选择本地保存路径
                OPENFILENAMEW ofn = { 0 };
                wchar_t szLocal[MAX_PATH];
                wcscpy_s(szLocal, baseName.c_str());
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
                        uint64_t resumeOffset = 0;
                        bool resume = false;
                        DWORD attr = GetFileAttributesW(szLocal);
                        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                            HANDLE hExist = CreateFileW(szLocal, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hExist != INVALID_HANDLE_VALUE) {
                                LARGE_INTEGER liSize;
                                if (GetFileSizeEx(hExist, &liSize) && liSize.QuadPart > 0) {
                                    int res = MessageBoxW(hDlg, L"检测到本地已有文件，是否断点续传？", L"提示", MB_YESNOCANCEL | MB_ICONQUESTION);
                                    if (res == IDCANCEL) {
                                        CloseHandle(hExist);
                                        break;
                                    }
                                    if (res == IDYES) {
                                        resumeOffset = (uint64_t)(liSize.QuadPart / (LONGLONG)kDownloadChunkSize) * kDownloadChunkSize;
                                        resume = (resumeOffset > 0);
                                    }
                                }
                                CloseHandle(hExist);
                            }
                        }
                        if (resume) {
                            client->hFileDownload = CreateFileW(szLocal, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (client->hFileDownload != INVALID_HANDLE_VALUE) {
                                LARGE_INTEGER liMove;
                                liMove.QuadPart = (LONGLONG)resumeOffset;
                                SetFilePointerEx(client->hFileDownload, liMove, NULL, FILE_BEGIN);
                                SetEndOfFile(client->hFileDownload);
                            }
                        } else {
                            client->hFileDownload = CreateFileW(szLocal, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        }
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
                        client->currentDownloadSize = resume ? resumeOffset : 0;
                        {
                            std::lock_guard<std::mutex> lock(s_transferMutex);
                            s_transferTotalBytes[hDlg] = totalBytes;
                            s_transferLastBytes[hDlg] = client->currentDownloadSize;
                            s_transferLastTick[hDlg] = GetTickCount64();
                            s_transferSpeedBps[hDlg] = 0.0;
                        }
                        
                        // 重置进度条
                        int initProgress = 0;
                        if (client->totalDownloadSize > 0 && client->currentDownloadSize > 0) {
                            initProgress = (int)((client->currentDownloadSize * 100) / client->totalDownloadSize);
                        }
                        SendDlgItemMessage(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, initProgress, 0);

                        // 发送下载命令
                        std::string rPath = WideToUTF8(fullRemote);
                        if (resume) {
                            std::string rPathResume = rPath + "|" + std::to_string(resumeOffset);
                            SendRemoteCommand(clientId, CMD_FILE_DOWNLOAD, (uint32_t)rPathResume.size(), 0, rPathResume.c_str(), rPathResume.size());
                        } else {
                            SendRemoteCommand(clientId, CMD_FILE_DOWNLOAD, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());
                        }
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
                    std::vector<std::wstring> targets;
                    int idx = -1;
                    while ((idx = ListView_GetNextItem(hList, idx, LVNI_SELECTED)) >= 0) {
                        wchar_t szName[MAX_PATH] = { 0 };
                        ListView_GetItemText(hList, idx, 0, szName, MAX_PATH);
                        targets.push_back(BuildRemoteFullPath(hDlg, szName));
                    }
                    std::string payload;
                    for (size_t i = 0; i < targets.size(); ++i) {
                        if (i > 0) payload += "\n";
                        payload += WideToUTF8(targets[i]);
                    }
                    SendRemoteCommand(clientId, CMD_FILE_DELETE, (uint32_t)payload.size(), 0, payload.c_str(), payload.size());

                    wchar_t szPath[MAX_PATH] = { 0 };
                    GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
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
        case IDM_FILE_COMPRESS:
        case IDM_FILE_UNCOMPRESS: {
            std::vector<std::wstring> targets;
            int idx = -1;
            while ((idx = ListView_GetNextItem(hList, idx, LVNI_SELECTED)) >= 0) {
                wchar_t szName[MAX_PATH] = { 0 };
                ListView_GetItemText(hList, idx, 0, szName, MAX_PATH);
                targets.push_back(BuildRemoteFullPath(hDlg, szName));
            }
            if (targets.empty()) break;
            std::string payload;
            for (size_t i = 0; i < targets.size(); ++i) {
                if (i > 0) payload += "\n";
                payload += WideToUTF8(targets[i]);
            }
            uint32_t cmd = (LOWORD(wParam) == IDM_FILE_COMPRESS) ? CMD_FILE_COMPRESS : CMD_FILE_UNCOMPRESS;
            SendRemoteCommand(clientId, cmd, (uint32_t)payload.size(), 1, payload.c_str(), payload.size());
            break;
        }
        case IDM_FILE_COPY_PATH: {
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szPath[MAX_PATH] = { 0 };
                wchar_t szName[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
                ListView_GetItemText(hList, selected, 0, szName, MAX_PATH);
                std::wstring fullPath = BuildRemoteFullPath(hDlg, szName);
                
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
            s_dlgMonitorActive.erase(hDlg);
            {
                std::lock_guard<std::mutex> lock(s_transferMutex);
                s_transferTotalBytes.erase(hDlg);
                s_transferLastBytes.erase(hDlg);
                s_transferLastTick.erase(hDlg);
                s_transferSpeedBps.erase(hDlg);
            }
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
