#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>
#include <thread>
#include <sstream>

#include "HistoryDialog.h"
#include "../resource.h"
#include "../GlobalState.h"
#include "../Config.h"
#include "../../Common/Utils.h"

namespace Formidable {
namespace UI {

static const UINT WM_HIST_LOC_UPDATE = WM_USER + 510;

INT_PTR CALLBACK HistoryDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_HISTORY)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_HISTORY)));

        HWND hList = GetDlgItem(hDlg, IDC_LIST_HISTORY);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        const wchar_t* headers[] = { L"ID", L"备注", L"计算机名称", L"位置", L"IP", L"系统", L"安装时间", L"最后登录", L"程序路径" };
        int widths[] = { 110, 120, 140, 160, 120, 140, 150, 150, 260 };

        for (int i = 0; i < 9; i++) {
            LVCOLUMNW lvc = { 0 };
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.pszText = (LPWSTR)headers[i];
            lvc.cx = widths[i];
            SendMessageW(hList, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
        }

        ApplyModernTheme(hDlg);

        // 加载数据
        struct LocTask { int item; std::wstring key; std::wstring ip; };
        std::vector<LocTask> locTasks;
        std::lock_guard<std::mutex> lock(g_HistoryHostsMutex);
        int i = 0;
        for (auto it = g_HistoryHosts.begin(); it != g_HistoryHosts.end(); ++it) {
            const std::wstring& key = it->first;
            const auto& host = it->second;
            
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = i;
            std::wstring wId = host.clientUniqueId == 0 ? L"" : std::to_wstring(host.clientUniqueId);
            lvi.pszText = (LPWSTR)wId.c_str();
            lvi.lParam = (LPARAM)i;
            int index = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
            
            LVITEMW lviSub = { 0 };
            lviSub.iSubItem = 1; lviSub.pszText = (LPWSTR)host.remark.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
            
            lviSub.iSubItem = 2; lviSub.pszText = (LPWSTR)host.computerName.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
            
            if (!host.location.empty()) {
                lviSub.iSubItem = 3; lviSub.pszText = (LPWSTR)host.location.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
            } else {
                lviSub.iSubItem = 3; lviSub.pszText = (LPWSTR)L"正在获取...";
                SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
                if (!host.ip.empty()) {
                    LocTask t;
                    t.item = index;
                    t.key = key;
                    t.ip = host.ip;
                    locTasks.push_back(t);
                }
            }

            lviSub.iSubItem = 4; lviSub.pszText = (LPWSTR)host.ip.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
            
            lviSub.iSubItem = 5; lviSub.pszText = (LPWSTR)host.osVersion.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
            
            lviSub.iSubItem = 6; lviSub.pszText = (LPWSTR)host.installTime.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);

            lviSub.iSubItem = 7; lviSub.pszText = (LPWSTR)host.lastSeen.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);

            lviSub.iSubItem = 8; lviSub.pszText = (LPWSTR)host.programPath.c_str();
            SendMessageW(hList, LVM_SETITEMTEXTW, index, (LPARAM)&lviSub);
            i++;
        }

        if (!locTasks.empty()) {
            std::vector<LocTask>* pTasks = new std::vector<LocTask>(locTasks);
            std::thread([hDlg, pTasks]() {
                for (size_t i = 0; i < pTasks->size(); ++i) {
                    if (!IsWindow(hDlg)) break;
                    const LocTask& t = (*pTasks)[i];
                    std::string ipUtf8 = Formidable::WideToUTF8(t.ip);
                    std::string locUtf8 = Formidable::GetLocationByIP(ipUtf8);
                    std::wstring wLoc = Formidable::UTF8ToWide(locUtf8);
                    struct UpdateData { int item; std::wstring key; std::wstring loc; };
                    UpdateData* data = new UpdateData{ t.item, t.key, wLoc };
                    PostMessageW(hDlg, WM_HIST_LOC_UPDATE, 0, (LPARAM)data);
                }
                delete pTasks;
            }).detach();
        }

        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        int margin = 8;
        int btnAreaHeight = 44;
        int btnWidth = 96;
        int btnHeight = 28;
        int btnY = height - btnAreaHeight + (btnAreaHeight - btnHeight) / 2;

        // 调整按钮位置
        HWND hClear = GetDlgItem(hDlg, IDC_BTN_CLEAR_HISTORY);
        HWND hExport = GetDlgItem(hDlg, IDC_BTN_EXPORT_HISTORY);
        HWND hClose = GetDlgItem(hDlg, IDCANCEL); 
        // 尝试获取 IDOK 作为关闭按钮（如果是 IDOK）
        if (!hClose || !IsWindow(hClose)) hClose = GetDlgItem(hDlg, IDOK);

        if (hClear) MoveWindow(hClear, margin, btnY, btnWidth, btnHeight, TRUE);
        if (hExport) MoveWindow(hExport, margin * 2 + btnWidth, btnY, btnWidth, btnHeight, TRUE);
        if (hClose) MoveWindow(hClose, width - margin - btnWidth, btnY, btnWidth, btnHeight, TRUE);

        // 调整列表控件大小
        HWND hList = GetDlgItem(hDlg, IDC_LIST_HISTORY);
        if (hList) {
            MoveWindow(hList, margin, margin, width - margin * 2, height - btnAreaHeight - margin * 2, TRUE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm && nm->idFrom == IDC_LIST_HISTORY) {
            if (nm->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                HWND hList = GetDlgItem(hDlg, IDC_LIST_HISTORY);

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
                return (INT_PTR)TRUE;
            } else if (nm->code == LVN_KEYDOWN) {
                NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
                if (kd && (GetKeyState(VK_CONTROL) & 0x8000) && (kd->wVKey == 'C' || kd->wVKey == 'c')) {
                    HWND hList = GetDlgItem(hDlg, IDC_LIST_HISTORY);
                    int colCount = Header_GetItemCount(ListView_GetHeader(hList));
                    std::wstringstream ss;

                    int idx = -1;
                    while ((idx = ListView_GetNextItem(hList, idx, LVNI_SELECTED)) != -1) {
                        for (int c = 0; c < colCount; ++c) {
                            wchar_t buf[1024] = { 0 };
                            LVITEMW lviText = { 0 };
                            lviText.iSubItem = c;
                            lviText.pszText = buf;
                            lviText.cchTextMax = 1024;
                            SendMessageW(hList, LVM_GETITEMTEXTW, (WPARAM)idx, (LPARAM)&lviText);
                            if (c > 0) ss << L"\t";
                            ss << buf;
                        }
                        ss << L"\r\n";
                    }

                    std::wstring text = ss.str();
                    if (!text.empty()) {
                        if (OpenClipboard(hDlg)) {
                            EmptyClipboard();
                            size_t size = (text.size() + 1) * sizeof(wchar_t);
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                            if (hMem) {
                                void* p = GlobalLock(hMem);
                                if (p) {
                                    memcpy(p, text.c_str(), size);
                                    GlobalUnlock(hMem);
                                    SetClipboardData(CF_UNICODETEXT, hMem);
                                }
                            }
                            CloseClipboard();
                        }
                    }
                    return (INT_PTR)TRUE;
                }
            }
        }
        break;
    }
    case WM_HIST_LOC_UPDATE: {
        struct UpdateData { int item; std::wstring key; std::wstring loc; };
        UpdateData* data = (UpdateData*)lParam;
        if (data) {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_HISTORY);
            if (hList && data->item >= 0) {
                LVITEMW lvi = { 0 };
                lvi.iSubItem = 3;
                lvi.pszText = (LPWSTR)data->loc.c_str();
                SendMessageW(hList, LVM_SETITEMTEXTW, data->item, (LPARAM)&lvi);
            }

            {
                std::lock_guard<std::mutex> lock(g_HistoryHostsMutex);
                if (g_HistoryHosts.count(data->key)) {
                    g_HistoryHosts[data->key].location = data->loc;
                }
            }

            delete data;
        }
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BTN_CLEAR_HISTORY) {
            if (MessageBoxW(hDlg, L"确定要清除所有历史记录吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                {
                    std::lock_guard<std::mutex> lock(g_HistoryHostsMutex);
                    g_HistoryHosts.clear();
                }
                SaveHistoryHosts();
                ListView_DeleteAllItems(GetDlgItem(hDlg, IDC_LIST_HISTORY));
            }
        } else if (LOWORD(wParam) == IDC_BTN_EXPORT_HISTORY) {
            // 简单导出到文本文件
            OPENFILENAMEW ofn = { 0 };
            wchar_t szFile[MAX_PATH] = L"hosts_export.txt";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"文本文件\0*.txt\0所有文件\0*.*\0";
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn)) {
                FILE* fp = nullptr;
                if (_wfopen_s(&fp, szFile, L"w, csc=UTF-8") == 0) {
                    fwprintf(fp, L"ID\t备注\t计算机名称\t位置\tIP\t系统\t安装时间\t最后登录\t程序路径\n");
                    std::lock_guard<std::mutex> lock(g_HistoryHostsMutex);
                    for (auto it = g_HistoryHosts.begin(); it != g_HistoryHosts.end(); ++it) {
                        const auto& host = it->second;
                        std::wstring wId = host.clientUniqueId == 0 ? L"" : std::to_wstring(host.clientUniqueId);
                        fwprintf(fp, L"%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                            wId.c_str(),
                            host.remark.c_str(),
                            host.computerName.c_str(),
                            host.location.c_str(),
                            host.ip.c_str(),
                            host.osVersion.c_str(),
                            host.installTime.c_str(),
                            host.lastSeen.c_str(),
                            host.programPath.c_str());
                    }
                    fclose(fp);
                    MessageBoxW(hDlg, L"导出成功！", L"提示", MB_OK | MB_ICONINFORMATION);
                }
            }
        } else if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void HistoryDialog::Show(HWND hParent) {
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_HISTORY), hParent, DlgProc, 0);
}

} // namespace UI
} // namespace Formidable
