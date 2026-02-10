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
#include <sstream>
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
static std::map<HWND, std::vector<HWND>> s_dlgDriveCards;
static std::map<HWND, std::vector<std::wstring>> s_dlgDrivePaths;
static std::map<HWND, std::vector<std::wstring>> s_dlgDriveNames;
static std::map<HWND, std::vector<std::wstring>> s_dlgDriveTypes;
static std::map<HWND, std::vector<std::wstring>> s_dlgDriveFree;
static std::map<HWND, std::vector<std::wstring>> s_dlgDriveTotal;
static std::map<HWND, int> s_dlgDriveControlIndex;
static std::map<HWND, bool> s_dlgShowDriveView;
static std::mutex s_transferMutex;
static HIMAGELIST s_hFileImageListSmall = NULL;
static HIMAGELIST s_hFileImageListLarge = NULL;
static HIMAGELIST s_hNavImageList = NULL;
static int s_navIconFolder = -1;
static int s_navIconDrive = -1;
static int s_navIconComputer = -1;
static const unsigned char kTransferKey[] = {
    0x3A, 0x7F, 0x12, 0x9C, 0x55, 0xE1, 0x08, 0x6D,
    0x4B, 0x90, 0x2E, 0xA7, 0x1C, 0xF3, 0xB5, 0x63
};
static const uint32_t kEncryptFlag = 0x80000000u;
static const bool kEnableFileTransferEncrypt = true;
static const uint64_t kDownloadChunkSize = 64 * 1024;

#define WM_UPDATE_PROGRESS (WM_USER + 201)
#define IDC_PROGRESS_BAR 1099

static bool IsTransferActive(HWND hDlg) {
    std::lock_guard<std::mutex> lock(s_transferMutex);
    auto it = s_transferTotalBytes.find(hDlg);
    if (it == s_transferTotalBytes.end()) return false;
    return it->second > 0;
}

static void SetLoadingState(HWND hDlg, bool loading, const wchar_t* statusText) {
    HWND hProg = GetDlgItem(hDlg, IDC_PROGRESS_BAR);
    if (hProg) {
        if (loading && !IsTransferActive(hDlg)) {
            SendMessageW(hProg, PBM_SETMARQUEE, TRUE, 30);
        } else {
            SendMessageW(hProg, PBM_SETMARQUEE, FALSE, 0);
        }
    }
    if (statusText) {
        HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
        if (hStatusBar) {
            SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)statusText);
        }
    }
}

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

static std::wstring GetNavPathText(HWND hDlg);
static void SetNavPathText(HWND hDlg, const std::wstring& path);

static std::wstring BuildRemoteFullPath(HWND hDlg, const std::wstring& name) {
    if (IsFullRemotePath(name)) return name;
    std::wstring full = GetNavPathText(hDlg);
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

static std::wstring FormatSizeText(uint64_t bytes) {
    wchar_t buf[64] = { 0 };
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        swprintf_s(buf, L"%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ULL * 1024ULL)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ULL)
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else
        swprintf_s(buf, L"%llu B", bytes);
    return buf;
}

static std::wstring GetNavPathText(HWND hDlg) {
    wchar_t buf[MAX_PATH] = { 0 };
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_NAV_PATH);
    if (hCombo) GetWindowTextW(hCombo, buf, MAX_PATH);
    return buf;
}

static void SetNavPathText(HWND hDlg, const std::wstring& path) {
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_NAV_PATH);
    if (!hCombo) return;
    SetWindowTextW(hCombo, path.c_str());
    if (!path.empty()) {
        LRESULT idx = SendMessageW(hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)path.c_str());
        if (idx == CB_ERR) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)path.c_str());
        }
    }
}

static std::wstring ExtractDrivePath(const std::wstring& text) {
    for (size_t i = 1; i < text.size(); ++i) {
        if (text[i] == L':') {
            wchar_t letter = text[i - 1];
            if ((letter >= L'A' && letter <= L'Z') || (letter >= L'a' && letter <= L'z')) {
                std::wstring path;
                path.push_back(towupper(letter));
                path.append(L":\\");
                return path;
            }
        }
    }
    return L"";
}

static void UpdateDriveViewVisibility(HWND hDlg, bool showDrive) {
    HWND hPanel = GetDlgItem(hDlg, IDC_PANEL_DRIVES);
    HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
    if (hPanel) ShowWindow(hPanel, showDrive ? SW_SHOW : SW_HIDE);
    if (hList) ShowWindow(hList, showDrive ? SW_HIDE : SW_SHOW);
    s_dlgShowDriveView[hDlg] = showDrive;
}

static void ClearDriveControlIndex() {
    for (auto it = s_dlgDriveControlIndex.begin(); it != s_dlgDriveControlIndex.end(); ) {
        if (!IsWindow(it->first)) it = s_dlgDriveControlIndex.erase(it);
        else ++it;
    }
}

static void ClearDriveCards(HWND hDlg) {
    auto& cards = s_dlgDriveCards[hDlg];
    for (HWND hCard : cards) {
        if (hCard) DestroyWindow(hCard);
    }
    cards.clear();
    ClearDriveControlIndex();
}

static void RenderDriveCards(HWND hDlg, const std::vector<std::wstring>& names, const std::vector<std::wstring>& paths, const std::vector<std::wstring>& types, const std::vector<std::wstring>& freeSizes, const std::vector<std::wstring>& totalSizes) {
    HWND hPanel = GetDlgItem(hDlg, IDC_PANEL_DRIVES);
    if (!hPanel) return;

    ClearDriveCards(hDlg);

    RECT rc;
    GetClientRect(hPanel, &rc);
    int padding = 10;
    int cardWidth = 220;
    int cardHeight = 90;
    int x = padding;
    int y = padding;
    int maxX = rc.right - padding;

    size_t count = names.size();
    for (size_t i = 0; i < count; ++i) {
        if (x + cardWidth > maxX) {
            x = padding;
            y += cardHeight + padding;
        }
        std::wstring title = names[i];
        if (i < types.size() && !types[i].empty()) {
            title = types[i] + L" (" + names[i] + L")";
        }
        HWND hCard = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | SS_NOTIFY, x, y, cardWidth, cardHeight, hPanel, (HMENU)(UINT_PTR)(IDC_DRIVE_CARD_BASE + (int)i), g_hInstance, NULL);
        s_dlgDriveCards[hDlg].push_back(hCard);

        HWND hIcon = CreateWindowExW(0, L"STATIC", NULL, SS_ICON | SS_NOTIFY | WS_CHILD | WS_VISIBLE, 12, 10, 32, 32, hCard, NULL, g_hInstance, NULL);
        SendMessageW(hIcon, STM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_DRIVE)));

        HWND hName = CreateWindowExW(0, L"STATIC", title.c_str(), SS_NOTIFY | WS_CHILD | WS_VISIBLE, 54, 8, cardWidth - 64, 20, hCard, NULL, g_hInstance, NULL);

        HWND hProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH | WS_DISABLED, 12, 44, cardWidth - 24, 10, hCard, NULL, g_hInstance, NULL);
        SendMessageW(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        std::wstring freeStr = (i < freeSizes.size()) ? freeSizes[i] : L"-";
        std::wstring totalStr = (i < totalSizes.size()) ? totalSizes[i] : L"-";
        
        std::wstring info = L"可用: - / 总: -";
        if (freeStr != L"-" && totalStr != L"-") {
            try {
                unsigned long long freeB = std::stoull(freeStr);
                unsigned long long totalB = std::stoull(totalStr);
                int percent = (totalB > 0) ? (int)((totalB - freeB) * 100 / totalB) : 0;
                SendMessageW(hProgress, PBM_SETPOS, percent, 0);
                info = L"可用: " + FormatSizeText(freeB) + L" / 总: " + FormatSizeText(totalB);
            } catch(...) {}
        }

        HWND hInfo = CreateWindowExW(0, L"STATIC", info.c_str(), SS_NOTIFY | WS_CHILD | WS_VISIBLE, 12, 58, cardWidth - 24, 20, hCard, NULL, g_hInstance, NULL);

        s_dlgDriveControlIndex[hCard] = (int)i;
        s_dlgDriveControlIndex[hIcon] = (int)i;
        s_dlgDriveControlIndex[hName] = (int)i;
        s_dlgDriveControlIndex[hProgress] = (int)i;
        s_dlgDriveControlIndex[hInfo] = (int)i;

        x += cardWidth + padding;
    }
}

static HTREEITEM ResetNavTree(HWND hDlg) {
    HWND hTree = GetDlgItem(hDlg, IDC_TREE_NAV);
    if (!hTree) return NULL;
    TreeView_DeleteAllItems(hTree);
    TVINSERTSTRUCTW tvis = { 0 };
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvis.item.pszText = (LPWSTR)L"此电脑";
    int rootIcon = (s_navIconComputer >= 0) ? s_navIconComputer : s_navIconFolder;
    tvis.item.iImage = rootIcon;
    tvis.item.iSelectedImage = rootIcon;
    HTREEITEM root = TreeView_InsertItem(hTree, &tvis);
    TreeView_Expand(hTree, root, TVE_EXPAND);
    return root;
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
        uint64_t lastUpdateTick = 0;
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
                uint64_t now = GetTickCount64();
                if (now - lastUpdateTick >= 100) {
                    int progress = (int)((totalSent * 100) / totalSize);
                    PostMessageW(hDlg, WM_UPDATE_PROGRESS, progress, (LPARAM)totalSent);
                    lastUpdateTick = now;
                }
            }

            // 简单控流，防止消息队列缓冲区满
            Sleep(5); // 降低延迟，提高传输速度
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

static void InitNavImageList() {
    if (s_hNavImageList) return;
    s_hNavImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 3);
    HICON hFolder = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_FOLDER), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    HICON hDrive = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_DRIVE), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    HICON hComputer = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_COMPUTER), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (s_hNavImageList) {
        if (hFolder) s_navIconFolder = ImageList_AddIcon(s_hNavImageList, hFolder);
        if (hDrive) s_navIconDrive = ImageList_AddIcon(s_hNavImageList, hDrive);
        if (hComputer) s_navIconComputer = ImageList_AddIcon(s_hNavImageList, hComputer);
    }
    if (hFolder) DestroyIcon(hFolder);
    if (hDrive) DestroyIcon(hDrive);
    if (hComputer) DestroyIcon(hComputer);
}

static void SetFlatButton(HWND hBtn) {
    if (!hBtn) return;
    LONG_PTR style = GetWindowLongPtrW(hBtn, GWL_STYLE);
    style |= BS_FLAT;
    SetWindowLongPtrW(hBtn, GWL_STYLE, style);
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
    std::wstring wPath = GetNavPathText(hDlg);
    std::string path = WideToUTF8(wPath);
    HWND hSearch = GetDlgItem(hDlg, IDC_EDIT_NAV_SEARCH);
    if (hSearch) {
        SendMessageW(hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索文件");
    }
    HWND hBack = GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_BACK);
    if (hBack) EnableWindow(hBack, !wPath.empty());
    
    if (path.empty()) {
        UpdateDriveViewVisibility(hDlg, true);
        SetPropW(hDlg, L"FILE_DRIVE_PENDING", (HANDLE)1);
        RemovePropW(hDlg, L"FILE_DRIVE_RETRY");
        SetTimer(hDlg, 2, 2000, NULL);
        SetLoadingState(hDlg, true, L"正在加载驱动器...");
        SendRemoteCommand(clientId, CMD_DRIVE_LIST, 0, 0);
    } else {
        UpdateDriveViewVisibility(hDlg, false);
        RemovePropW(hDlg, L"FILE_DRIVE_PENDING");
        RemovePropW(hDlg, L"FILE_DRIVE_RETRY");
        KillTimer(hDlg, 2);
        if (path.back() != '\\') path += "\\";
        SetLoadingState(hDlg, true, L"正在加载目录...");
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
        InitNavImageList();

        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);

        // 设置图标列表（小/大）
        if (s_hFileImageListSmall) ListView_SetImageList(hList, s_hFileImageListSmall, LVSIL_SMALL);
        if (s_hFileImageListLarge) ListView_SetImageList(hList, s_hFileImageListLarge, LVSIL_NORMAL);

        // 启用拖放上传
        DragAcceptFiles(hDlg, TRUE);

        HWND hTree = GetDlgItem(hDlg, IDC_TREE_NAV);
        if (hTree && s_hNavImageList) {
            TreeView_SetImageList(hTree, s_hNavImageList, TVSIL_NORMAL);
        }
        if (hTree) {
            LONG_PTR style = GetWindowLongPtrW(hTree, GWL_STYLE);
            style |= TVS_SHOWSELALWAYS;
            SetWindowLongPtrW(hTree, GWL_STYLE, style);
            SetWindowPos(hTree, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }

        SetFlatButton(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_BACK));
        SetFlatButton(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_MKDIR));
        SetFlatButton(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_RENAME));
        SetFlatButton(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_DELETE));
        SetFlatButton(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_REFRESH));
        SetFlatButton(GetDlgItem(hDlg, IDC_BTN_FILE_VIEW));
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"文件名"; lvc.cx = 250; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"大小";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"类型";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"修改时间"; lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);
        
        SetNavPathText(hDlg, L"");
        ResetNavTree(hDlg);
        UpdateDriveViewVisibility(hDlg, true);
        HWND hSearch = GetDlgItem(hDlg, IDC_EDIT_NAV_SEARCH);
        if (hSearch) {
            SendMessageW(hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索文件 (回车搜索)");
            // 确保搜索框有边框且样式统一
            SetWindowLongPtrW(hSearch, GWL_EXSTYLE, GetWindowLongPtrW(hSearch, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
            SetWindowPos(hSearch, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        
        HWND hNavBar = GetDlgItem(hDlg, IDC_STATIC_NAV_BAR);
        if (hNavBar) {
            SetWindowTextW(hNavBar, L"  地址:");
            // 优化样式：设置字体或边距
            HFONT hFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
            SendMessageW(hNavBar, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        // 创建进度条
        HWND hProg = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH | PBS_MARQUEE, 
            0, 31, 760, 2, hDlg, (HMENU)IDC_PROGRESS_BAR, g_hInstance, NULL); // 移动到导航栏下方，高度减小，避免黑点
        SendMessage(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // 延迟自动刷新
        SetTimer(hDlg, 1, 500, NULL);

        ApplyModernTheme(hDlg);
        
        return (INT_PTR)TRUE;
    }
    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hDlg, 1);
            RefreshRemoteList(hDlg);
        } else if (wParam == 2) {
            if (GetPropW(hDlg, L"FILE_DRIVE_PENDING")) {
                if (GetPropW(hDlg, L"FILE_DRIVE_RETRY") == NULL) {
                    SetPropW(hDlg, L"FILE_DRIVE_RETRY", (HANDLE)1);
                    uint32_t clientId = s_dlgToClientId[hDlg];
                    SetLoadingState(hDlg, true, L"驱动器加载超时，正在重试...");
                    SendRemoteCommand(clientId, CMD_DRIVE_LIST, 0, 0);
                } else {
                    RemovePropW(hDlg, L"FILE_DRIVE_PENDING");
                    RemovePropW(hDlg, L"FILE_DRIVE_RETRY");
                    KillTimer(hDlg, 2);
                    SetLoadingState(hDlg, false, L"驱动器加载失败，请点击刷新重试");
                    UpdateDriveViewVisibility(hDlg, true);
                }
            } else {
                KillTimer(hDlg, 2);
            }
        }
        break;
    case WM_FILE_UPDATE_DRIVES: {
        RemovePropW(hDlg, L"FILE_DRIVE_PENDING");
        RemovePropW(hDlg, L"FILE_DRIVE_RETRY");
        KillTimer(hDlg, 2);
        std::string* payload = (std::string*)lParam;
        std::vector<std::wstring> paths;
        std::vector<std::wstring> names;
        std::vector<std::wstring> types;
        std::vector<std::wstring> freeSizes;
        std::vector<std::wstring> totalSizes;
        if (payload) {
            if (payload->find('|') == std::string::npos) {
                HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
                std::wstring wMsg = Formidable::UTF8ToWide(*payload);
                if (hStatusBar) {
                    SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)wMsg.c_str());
                }
                delete payload;
                UpdateDriveViewVisibility(hDlg, true);
                SetLoadingState(hDlg, false, nullptr);
                return (INT_PTR)TRUE;
            }
            std::stringstream ss(*payload);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                std::vector<std::string> parts;
                std::stringstream lineSS(line);
                std::string part;
                while (std::getline(lineSS, part, '|')) {
                    parts.push_back(part);
                }

                if (parts.size() < 2) continue;

                std::string name = parts[0];
                std::string type = parts[1];
                std::string freeS = (parts.size() >= 3) ? parts[2] : "-";
                std::string totalS = (parts.size() >= 4) ? parts[3] : "-";

                std::wstring wName = Formidable::UTF8ToWide(name);
                std::wstring wType = Formidable::UTF8ToWide(type);
                std::wstring path = wName;
                if (path.size() == 2 && path[1] == L':') path += L"\\";
                if (path.empty()) continue;
                names.push_back(wName);
                paths.push_back(path);
                types.push_back(wType);
                freeSizes.push_back(Formidable::UTF8ToWide(freeS));
                totalSizes.push_back(Formidable::UTF8ToWide(totalS));
            }
            delete payload;
        }

        s_dlgDrivePaths[hDlg] = paths;
        s_dlgDriveNames[hDlg] = names;
        s_dlgDriveTypes[hDlg] = types;
        s_dlgDriveFree[hDlg] = freeSizes;
        s_dlgDriveTotal[hDlg] = totalSizes;

        HTREEITEM root = ResetNavTree(hDlg);
        HWND hTree = GetDlgItem(hDlg, IDC_TREE_NAV);
        if (hTree && root) {
            for (size_t i = 0; i < names.size(); ++i) {
                std::wstring text = names[i];
                if (i < types.size() && !types[i].empty()) {
                    text = types[i] + L" (" + names[i] + L")";
                }
                TVINSERTSTRUCTW tvis = { 0 };
                tvis.hParent = root;
                tvis.hInsertAfter = TVI_LAST;
                tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
                tvis.item.pszText = (LPWSTR)text.c_str();
                tvis.item.iImage = s_navIconDrive;
                tvis.item.iSelectedImage = s_navIconDrive;
                TreeView_InsertItem(hTree, &tvis);
            }
            TreeView_Expand(hTree, root, TVE_EXPAND);
        }

        UpdateDriveViewVisibility(hDlg, true);
        RenderDriveCards(hDlg, names, paths, types, freeSizes, totalSizes);
        HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
        if (hStatusBar) {
            wchar_t szStatus[128] = { 0 };
            swprintf_s(szStatus, L" 设备和驱动器：%d", (int)names.size());
            SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)szStatus);
        }
        SetLoadingState(hDlg, false, nullptr);
        return (INT_PTR)TRUE;
    }
    case WM_FILE_UPDATE_LIST: {
        std::string* payload = (std::string*)lParam;
        std::string data = payload ? *payload : std::string();
        delete payload;

        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        bool appendMode = GetPropW(hDlg, L"FILE_SEARCH_APPEND") != NULL;
        bool clearFirst = GetPropW(hDlg, L"FILE_SEARCH_CLEAR") != NULL;
        if (!appendMode || clearFirst) {
            ListView_DeleteAllItems(hList);
            if (clearFirst) RemovePropW(hDlg, L"FILE_SEARCH_CLEAR");
        }
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);

        if (data == "PERMISSION_DENIED") {
            SendMessage(hList, WM_SETREDRAW, TRUE, 0);
            HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
            if (hStatusBar) {
                SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)L"权限不足，无法访问该目录");
            }
            MessageBoxW(hDlg, L"权限不足，无法访问该目录", L"错误", MB_ICONERROR);
            SetLoadingState(hDlg, false, nullptr);
            PostMessageW(hDlg, Formidable::UI::WM_FILE_SHOW_LIST, 0, 0);
            return (INT_PTR)TRUE;
        }

        std::stringstream ss(data);
        std::string line;
        int index = ListView_GetItemCount(hList);
        int dirCount = 0;
        int fileCount = 0;
        unsigned long long totalSize = 0;

        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            size_t p1 = line.find('|');
            size_t p2 = line.find('|', p1 + 1);
            size_t p3 = line.find('|', p2 + 1);

            if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
                std::string typeStr = line.substr(0, p1);
                std::string nameStr = line.substr(p1 + 1, p2 - p1 - 1);
                std::string sizeStr = line.substr(p2 + 1, p3 - p2 - 1);
                std::string timeStr = line.substr(p3 + 1);

                bool isDir = (typeStr == "[DIR]");
                if (isDir) dirCount++;
                else fileCount++;

                std::wstring wName = Formidable::UTF8ToWide(nameStr);
                SHFILEINFOW sfi = { 0 };
                DWORD flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
                DWORD attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
                SHGetFileInfoW(wName.c_str(), attr, &sfi, sizeof(sfi), flags);

                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
                lvi.iItem = index;
                lvi.pszText = (LPWSTR)wName.c_str();
                lvi.lParam = index;
                lvi.iImage = sfi.iIcon;

                int idx = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

                LVITEMW lviSet = { 0 };
                std::wstring wSize;
                if (isDir) {
                    wSize = L"<DIR>";
                } else {
                    unsigned long long size = std::stoull(sizeStr);
                    totalSize += size;
                    if (size > 1024 * 1024 * 1024)
                        wSize = std::to_wstring(size / (1024 * 1024 * 1024)) + L" GB";
                    else if (size > 1024 * 1024)
                        wSize = std::to_wstring(size / (1024 * 1024)) + L" MB";
                    else if (size > 1024)
                        wSize = std::to_wstring(size / 1024) + L" KB";
                    else
                        wSize = std::to_wstring(size) + L" B";
                }
                lviSet.iSubItem = 1; lviSet.pszText = (LPWSTR)wSize.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);

                std::wstring wType = isDir ? L"文件夹" : L"文件";
                lviSet.iSubItem = 2; lviSet.pszText = (LPWSTR)wType.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);

                std::wstring wTime = Formidable::UTF8ToWide(timeStr);
                lviSet.iSubItem = 3; lviSet.pszText = (LPWSTR)wTime.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, idx, (LPARAM)&lviSet);

                index++;
            }
        }
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);

        HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
        if (hStatusBar) {
            wchar_t szTotalSize[64] = {0};
            if (totalSize > 1024 * 1024 * 1024)
                swprintf_s(szTotalSize, L"%.2f GB", totalSize / (double)(1024 * 1024 * 1024));
            else if (totalSize > 1024 * 1024)
                swprintf_s(szTotalSize, L"%.2f MB", totalSize / (double)(1024 * 1024));
            else if (totalSize > 1024)
                swprintf_s(szTotalSize, L"%.2f KB", totalSize / (double)1024);
            else
                swprintf_s(szTotalSize, L"%llu B", totalSize);

            wchar_t szStatus[256] = {0};
            swprintf_s(szStatus, L" %d 个项目  |  %d 个文件夹  |  %d 个文件  |  总大小: %s", 
                      dirCount + fileCount, dirCount, fileCount, szTotalSize);
            SendMessageW(hStatusBar, WM_SETTEXT, 0, (LPARAM)szStatus);
        }
        if (appendMode) {
            HANDLE hPending = GetPropW(hDlg, L"FILE_SEARCH_PENDING");
            UINT_PTR pending = (UINT_PTR)hPending;
            if (pending > 0) pending--;
            if (pending <= 1) {
                RemovePropW(hDlg, L"FILE_SEARCH_APPEND");
                RemovePropW(hDlg, L"FILE_SEARCH_CLEAR");
                RemovePropW(hDlg, L"FILE_SEARCH_PENDING");
            } else {
                SetPropW(hDlg, L"FILE_SEARCH_PENDING", (HANDLE)pending);
            }
        }
        SetLoadingState(hDlg, false, nullptr);
        PostMessageW(hDlg, Formidable::UI::WM_FILE_SHOW_LIST, 0, 0);
        return (INT_PTR)TRUE;
    }
    case WM_FILE_SHOW_LIST:
        UpdateDriveViewVisibility(hDlg, false);
        return (INT_PTR)TRUE;
    case WM_FILE_LOADING_STATE:
        SetLoadingState(hDlg, wParam != 0, nullptr);
        return (INT_PTR)TRUE;
    case WM_UPDATE_PROGRESS:
        SetLoadingState(hDlg, false, nullptr);
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

        std::wstring remoteDir = GetNavPathText(hDlg);
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

        int navHeight = 34;
        int toolbarHeight = 34;
        int statusBarHeight = 22;
        int progressHeight = 18;
        int spacing = 8;
        int margin = 8;

        int labelWidth = 40;
        int labelHeight = 22;
        int labelY = 6;
        MoveWindow(GetDlgItem(hDlg, IDC_STATIC_NAV_BAR), margin, labelY, labelWidth, labelHeight, TRUE);

        int searchWidth = 180;
        int comboHeight = 22; // 稍微增加高度以匹配现代风格
        int comboY = 6;
        int comboX = margin + labelWidth + spacing;
        int comboWidth = 0;
        int available = width - comboX - margin;
        
        // 改进布局逻辑：路径框占据剩余的大部分空间，搜索框固定宽度或按比例
        int minSearchWidth = 100;
        int minComboWidth = 150;
        
        if (available < minComboWidth + minSearchWidth + spacing) {
            // 空间极度受限时，按比例分配
            comboWidth = (available - spacing) * 2 / 3;
            searchWidth = available - spacing - comboWidth;
        } else {
            // 空间充足，搜索框固定 180，路径框占据剩余
            searchWidth = 180;
            comboWidth = available - searchWidth - spacing;
        }

        MoveWindow(GetDlgItem(hDlg, IDC_COMBO_NAV_PATH), comboX, comboY, comboWidth, comboHeight, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_NAV_SEARCH), comboX + comboWidth + spacing, comboY, searchWidth, comboHeight, TRUE);

        int toolbarY = navHeight + spacing;
        int toolbarBtnWidth = 62;
        int toolbarBtnHeight = 24;
        int toolbarBtnSpacing = 8;
        int toolbarX = margin;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_BACK), toolbarX, toolbarY + 3, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_MKDIR), toolbarX, toolbarY + 3, 86, toolbarBtnHeight, TRUE);
        toolbarX += 86 + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_RENAME), toolbarX, toolbarY + 3, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_DELETE), toolbarX, toolbarY + 3, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_REMOTE_REFRESH), toolbarX, toolbarY + 3, toolbarBtnWidth, toolbarBtnHeight, TRUE);
        toolbarX += toolbarBtnWidth + toolbarBtnSpacing;
        MoveWindow(GetDlgItem(hDlg, IDC_BTN_FILE_VIEW), toolbarX, toolbarY + 3, toolbarBtnWidth, toolbarBtnHeight, TRUE);

        int progressWidth = 200;
        int statusWidth = width - progressWidth;

        HWND hStatusBar = GetDlgItem(hDlg, IDC_STATUS_FILE_BAR);
        MoveWindow(hStatusBar, 0, height - statusBarHeight, statusWidth, statusBarHeight, TRUE);

        MoveWindow(GetDlgItem(hDlg, IDC_PROGRESS_BAR), statusWidth, height - statusBarHeight + 4, progressWidth - 5, statusBarHeight - 8, TRUE);

        int listY = toolbarY + toolbarHeight + spacing;
        int listHeight = height - statusBarHeight - listY - spacing;
        if (listHeight < 0) listHeight = 0;

        int leftWidth = 220;
        int contentX = margin;
        int contentWidth = width - margin * 2;
        if (contentWidth - leftWidth - spacing < 160) {
            leftWidth = max(140, contentWidth - 160 - spacing);
        }
        int listX = contentX + leftWidth + spacing;
        int listWidth = contentWidth - leftWidth - spacing;

        MoveWindow(GetDlgItem(hDlg, IDC_TREE_NAV), contentX, listY, leftWidth, listHeight, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_PANEL_DRIVES), listX, listY, listWidth, listHeight, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE), listX, listY, listWidth, listHeight, TRUE);

        // 动态调整列宽
        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        int listDataWidth = listWidth - 20;
        LVCOLUMNW col = { 0 };
        col.mask = LVCF_WIDTH;
        col.cx = (int)(listDataWidth * 0.40);
        SendMessageW(hList, LVM_SETCOLUMNW, 0, (LPARAM)&col);
        col.cx = (int)(listDataWidth * 0.15);
        SendMessageW(hList, LVM_SETCOLUMNW, 1, (LPARAM)&col);
        col.cx = (int)(listDataWidth * 0.15);
        SendMessageW(hList, LVM_SETCOLUMNW, 2, (LPARAM)&col);
        col.cx = (int)(listDataWidth * 0.30);
        SendMessageW(hList, LVM_SETCOLUMNW, 3, (LPARAM)&col);

        if (s_dlgShowDriveView[hDlg]) {
            RenderDriveCards(hDlg, s_dlgDriveNames[hDlg], s_dlgDrivePaths[hDlg], s_dlgDriveTypes[hDlg], s_dlgDriveFree[hDlg], s_dlgDriveTotal[hDlg]);
        }

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

                std::wstring dir = GetNavPathText(hDlg);
                std::wstring oldName = s_dlgRenameOldName[hDlg];
                std::wstring oldFull;
                std::wstring newFull;
                if (IsFullRemotePath(oldName)) {
                    dir = GetDirectoryPart(oldName);
                    oldFull = oldName;
                    newFull = dir + info->item.pszText;
                } else {
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
        } else if (nm->idFrom == IDC_TREE_NAV) {
            if (nm->code == TVN_SELCHANGEDW) {
                auto* info = (LPNMTREEVIEWW)lParam;
                wchar_t text[MAX_PATH] = { 0 };
                TVITEMW item = { 0 };
                item.hItem = info->itemNew.hItem;
                item.mask = TVIF_TEXT;
                item.pszText = text;
                item.cchTextMax = MAX_PATH;
                if (TreeView_GetItem(nm->hwndFrom, &item)) {
                    std::wstring path = ExtractDrivePath(text);
                    if (wcscmp(text, L"此电脑") == 0) {
                        TreeView_Expand(nm->hwndFrom, info->itemNew.hItem, TVE_EXPAND);
                        SetNavPathText(hDlg, L"");
                        RefreshRemoteList(hDlg); // 明确刷新，触发驱动器列表请求
                    } else if (!path.empty()) {
                        SetNavPathText(hDlg, path);
                        RefreshRemoteList(hDlg);
                    }
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);

        if (HIWORD(wParam) == STN_DBLCLK) {
            HWND hCtrl = (HWND)lParam;
            auto it = s_dlgDriveControlIndex.find(hCtrl);
            if (it != s_dlgDriveControlIndex.end()) {
                int idx = it->second;
                if (s_dlgDrivePaths.count(hDlg) && idx >= 0 && idx < (int)s_dlgDrivePaths[hDlg].size()) {
                    SetNavPathText(hDlg, s_dlgDrivePaths[hDlg][idx]);
                    RefreshRemoteList(hDlg);
                }
                return (INT_PTR)TRUE;
            }
        }
        
        switch (LOWORD(wParam)) {
        case IDOK: {
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_NAV_PATH);
            HWND hSearch = GetDlgItem(hDlg, IDC_EDIT_NAV_SEARCH);
            HWND hFocus = GetFocus();
            
            // 检查焦点是否在搜索框
            if (hSearch && (hFocus == hSearch || IsChild(hSearch, hFocus))) {
                wchar_t szSearch[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_NAV_SEARCH, szSearch, MAX_PATH);
                if (wcslen(szSearch) > 0) {
                    // 执行搜索逻辑
                    std::vector<std::wstring> targets;
                    std::wstring dir = GetNavPathText(hDlg);
                    if (dir.empty() || dir == L"此电脑") {
                        if (s_dlgDrivePaths.count(hDlg)) targets = s_dlgDrivePaths[hDlg];
                        if (targets.empty()) targets.push_back(L"C:\\");
                    } else {
                        targets.push_back(dir);
                    }
                    
                    RemovePropW(hDlg, L"FILE_SEARCH_APPEND");
                    RemovePropW(hDlg, L"FILE_SEARCH_CLEAR");
                    RemovePropW(hDlg, L"FILE_SEARCH_PENDING");
                    if (targets.size() > 1) {
                        SetPropW(hDlg, L"FILE_SEARCH_APPEND", (HANDLE)1);
                        SetPropW(hDlg, L"FILE_SEARCH_CLEAR", (HANDLE)1);
                        SetPropW(hDlg, L"FILE_SEARCH_PENDING", (HANDLE)(ULONG_PTR)targets.size());
                    }
                    
                    UpdateDriveViewVisibility(hDlg, false);
                    for (const auto& target : targets) {
                        std::string payload = WideToUTF8(target) + "|" + WideToUTF8(szSearch);
                        SendRemoteCommand(clientId, CMD_FILE_SEARCH, (uint32_t)payload.size(), 1, payload.c_str(), payload.size());
                    }
                    return (INT_PTR)TRUE;
                }
            }

            if (hCombo && hFocus && (hFocus == hCombo || IsChild(hCombo, hFocus))) {
                RefreshRemoteList(hDlg);
                return (INT_PTR)TRUE;
            }
            break;
        }
        case IDC_BTN_FILE_REMOTE_BACK: {
            std::wstring parent = GetRemoteParentPath(GetNavPathText(hDlg));
            SetNavPathText(hDlg, parent);
            RefreshRemoteList(hDlg);
            break;
        }
        case IDC_BTN_FILE_REMOTE_MKDIR:
            PostMessageW(hDlg, WM_COMMAND, IDM_FILE_NEW_FOLDER, 0);
            break;
        case IDC_BTN_FILE_REMOTE_RENAME:
            PostMessageW(hDlg, WM_COMMAND, IDM_FILE_RENAME, 0);
            break;
        case IDC_BTN_FILE_REMOTE_DELETE:
            PostMessageW(hDlg, WM_COMMAND, IDM_FILE_DELETE, 0);
            break;
        case IDC_BTN_FILE_REMOTE_REFRESH:
            PostMessageW(hDlg, WM_COMMAND, IDM_FILE_REFRESH, 0);
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
        case IDC_COMBO_NAV_PATH: {
            if (HIWORD(wParam) == CBN_SELENDOK) {
                RefreshRemoteList(hDlg);
            }
            break;
        }
        case IDC_EDIT_NAV_SEARCH: {
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
                    SetNavPathText(hDlg, newPath);

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
                std::wstring remoteDir = GetNavPathText(hDlg);
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

                    std::string refresh = WideToUTF8(GetNavPathText(hDlg));
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

            std::wstring full = GetNavPathText(hDlg);
            if (full.empty()) full = L"C:\\";
            if (full.back() != L'\\') full += L'\\';
            full += folderName;

            std::string rPath = WideToUTF8(full);
            SendRemoteCommand(clientId, CMD_FILE_MKDIR, (uint32_t)rPath.size(), 0, rPath.c_str(), rPath.size());

            std::string refresh = WideToUTF8(GetNavPathText(hDlg));
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
                wchar_t szName[MAX_PATH] = { 0 };
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
            ClearDriveCards(hDlg);
            s_dlgDrivePaths.erase(hDlg);
            s_dlgDriveNames.erase(hDlg);
            s_dlgDriveTypes.erase(hDlg);
            s_dlgShowDriveView.erase(hDlg);
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
