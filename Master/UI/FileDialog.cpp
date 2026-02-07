#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// FileDialog.cpp - 文件管理对话框实现
#include "FileDialog.h"
#include "FileManagerUI.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
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
static std::map<HWND, int> s_dlgViewMode; // 0=详细, 1=列表, 2=图标
static HIMAGELIST s_hFileImageList = NULL;

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
static void SendFileTask(uint32_t clientId, std::wstring localPath, std::wstring remotePath) {
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
        char buf[16384]; // 16KB 块
        while (file.read(buf, sizeof(buf)) || (file.gcount() > 0)) {
            SendRemoteCommand(clientId, CMD_FILE_DATA, (uint32_t)file.gcount(), 0, buf, (size_t)file.gcount());
            // 简单控流，防止消息队列缓冲区满
            Sleep(10);
        }
        file.close();
    }

    // 3. 结束上传 (arg2=1)
    SendRemoteCommand(clientId, CMD_FILE_UPLOAD, 0, 1);
}

// 初始化文件图标列表
void InitFileImageList() {
    if (s_hFileImageList) return;
    
    SHFILEINFOW sfi = { 0 };
    s_hFileImageList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), 
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
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
    DWORD dwStyle = GetWindowLong(hList, GWL_STYLE);
    dwStyle &= ~(LVS_TYPEMASK);
    
    switch (mode) {
    case 0: // 详细列表
        dwStyle |= LVS_REPORT;
        break;
    case 1: // 列表
        dwStyle |= LVS_LIST;
        break;
    case 2: // 图标
        dwStyle |= LVS_ICON;
        break;
    }
    SetWindowLong(hList, GWL_STYLE, dwStyle);
}

INT_PTR CALLBACK FileDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        s_dlgViewMode[hDlg] = 0; // 默认详细视图
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_FILE)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_FILE)));
        
        // 初始化文件图标
        InitFileImageList();
        
        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        // 设置图标列表
        if (s_hFileImageList) {
            ListView_SetImageList(hList, s_hFileImageList, LVSIL_SMALL);
            ListView_SetImageList(hList, s_hFileImageList, LVSIL_NORMAL);
        }

        // 启用拖放上传
        DragAcceptFiles(hDlg, TRUE);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"文件名"; lvc.cx = 250; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"大小";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"类型";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"修改时间"; lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, L"C:\\");
        SetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_LOCAL, L"C:\\");
        
        // 初始化本地文件列表列
        HWND hListLocal = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
        ListView_SetExtendedListViewStyle(hListLocal, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        // 使用与远程相同的图标列表(如果有)
        if (s_hFileImageList) {
            ListView_SetImageList(hListLocal, s_hFileImageList, LVSIL_SMALL);
            ListView_SetImageList(hListLocal, s_hFileImageList, LVSIL_NORMAL);
        }
        
        LVCOLUMNW lvcLocal = { 0 };
        lvcLocal.mask = LVCF_TEXT | LVCF_WIDTH;
        lvcLocal.pszText = (LPWSTR)L"文件名"; lvcLocal.cx = 250; SendMessageW(hListLocal, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvcLocal);
        lvcLocal.pszText = (LPWSTR)L"大小";   lvcLocal.cx = 100; SendMessageW(hListLocal, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvcLocal);
        lvcLocal.pszText = (LPWSTR)L"修改时间"; lvcLocal.cx = 150; SendMessageW(hListLocal, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvcLocal);
        
        // 刷新本地列表
        FileManagerUI::RefreshLocalFileList(hDlg, L"C:\\");
        
        Formidable::CommandPkg pkg = { 0 };
        pkg.cmd = Formidable::CMD_FILE_LIST;
        std::string path = "C:\\";
        pkg.arg1 = (uint32_t)path.size();
        
        size_t bodySize = sizeof(Formidable::CommandPkg) + path.size();
        std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
        Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
        memcpy(h->flag, "FRMD26?", 7);
        h->originLen = (int)bodySize;
        h->totalLen = (int)sendBuf.size();
        
        memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, sizeof(Formidable::CommandPkg));
        memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader) + sizeof(Formidable::CommandPkg), path.c_str(), path.size());
        
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
        }
        
        return (INT_PTR)TRUE;
    }
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
            std::thread(SendFileTask, clientId, std::wstring(szLocalPath), fullRemotePath).detach();
        }
        DragFinish(hDrop);
        MessageBoxW(hDlg, L"已开始后台上传拖入的文件", L"提示", MB_OK);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE), 0, 30, rc.right, rc.bottom - 30, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_FILE_REMOTE) {
            if (nm->code == NM_RCLICK) {
                // 获取鼠标位置
                POINT pt;
                GetCursorPos(&pt);
                
                // 创建右键菜单
                HMENU hMenu = CreatePopupMenu();
                HMENU hViewMenu = CreatePopupMenu();
                
                // 视图子菜单
                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 0 ? MF_CHECKED : 0), IDM_FILE_VIEW_DETAIL, L"详细信息");
                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 1 ? MF_CHECKED : 0), IDM_FILE_VIEW_LIST, L"列表");
                AppendMenuW(hViewMenu, MF_STRING | (s_dlgViewMode[hDlg] == 2 ? MF_CHECKED : 0), IDM_FILE_VIEW_ICON, L"图标");
                
                // 主菜单项
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
                // 双击打开文件夹或文件
                LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
                if (lpnmitem->iItem >= 0) {
                    // 发送打开命令
                    SendMessage(hDlg, WM_COMMAND, IDM_FILE_OPEN, 0);
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_REMOTE);
        
        switch (LOWORD(wParam)) {
        case IDM_FILE_VIEW_DETAIL:
            s_dlgViewMode[hDlg] = 0;
            SetListViewMode(hList, 0);
            break;
        case IDM_FILE_VIEW_LIST:
            s_dlgViewMode[hDlg] = 1;
            SetListViewMode(hList, 1);
            break;
        case IDM_FILE_VIEW_ICON:
            s_dlgViewMode[hDlg] = 2;
            SetListViewMode(hList, 2);
            break;
        case IDC_BTN_FILE_GO_LOCAL: {
            wchar_t szPath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_LOCAL, szPath, MAX_PATH);
            FileManagerUI::RefreshLocalFileList(hDlg, szPath);
            break;
        }
        case IDC_BTN_FILE_GO_REMOTE: {
            wchar_t szPath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
            std::string path = WideToUTF8(szPath);
            if (path.empty()) path = "C:\\";
            // 发送刷新命令
            SendRemoteCommand(clientId, CMD_FILE_LIST, (uint32_t)path.size(), 0, path.c_str(), path.size());
            break;
        }
        case IDM_FILE_REFRESH: {
            wchar_t szPath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_FILE_PATH_REMOTE, szPath, MAX_PATH);
            std::string path = WideToUTF8(szPath);
            if (path.empty()) path = "C:\\";
            SendRemoteCommand(clientId, CMD_FILE_LIST, (uint32_t)path.size(), 0, path.c_str(), path.size());
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
                std::thread(SendFileTask, clientId, std::wstring(szFile), fullRemotePath).detach();
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
                if (MessageBoxW(hDlg, L"确定要删除选中的文件吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    // TODO: 发送删除命令
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
            // TODO: 创建新文件夹对话框
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
