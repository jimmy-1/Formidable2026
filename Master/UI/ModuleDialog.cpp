#include "ModuleDialog.h"
#include "../GlobalState.h"
#include "../NetworkHelper.h"
#include "../StringUtils.h"
#include "../../Common/Config.h"
#include "../resource.h"
#include "../Utils/StringHelper.h"
#include "../MainWindow.h"
#include <CommCtrl.h>
#include <map>

INT_PTR CALLBACK ModuleDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, ModParam> dlgParams;

    switch (message) {
    case WM_INITDIALOG: {
        ModParam* p = (ModParam*)lParam;
        dlgParams[hDlg] = *p;

        // 窗口消息定义
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_PROCESS)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_PROCESS)));

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(p->cid)) client = g_Clients[p->cid];
        }
        if (client) client->hModuleDlg = hDlg;

        HWND hList = GetDlgItem(hDlg, IDC_LIST_MODULES);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"模块名"; lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"基址";   lvc.cx = 120; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"大小";     lvc.cx = 80;  SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"路径";     lvc.cx = 300; SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

        ApplyModernTheme(hDlg);
        // 获取选中客户端
        AddLog(L"查询", L"获取进程模块 PID: " + std::to_wstring((int)p->pid));
        SendModuleToClient(p->cid, Formidable::CMD_PROCESS_MODULES, L"ProcessManager.dll", p->pid);

        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_MODULES), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_MODULES) {
            if (nm->code == LVN_COLUMNCLICK) {
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
                UpdateListViewSortHeader(hList, g_SortInfo[hList].column, g_SortInfo[hList].ascending);
            }
        }
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDCANCEL) {
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE: {
        ModParam p = dlgParams[hDlg];
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(p.cid)) client = g_Clients[p.cid];
        }
        if (client) client->hModuleDlg = NULL;

        EndDialog(hDlg, 0);
        dlgParams.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}

void ShowModuleDialog(HWND hParent, uint32_t clientId, uint32_t pid) {
    ModParam param{ clientId, pid };
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_MODULES), hParent, ModuleDlgProc, (LPARAM)&param);
}
