// RegistryDialog.cpp - 注册表管理对话框实现
#include "RegistryDialog.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
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
        
        // 发送注册表初始化命令
        Formidable::CommandPkg pkg = { 0 };
        pkg.cmd = Formidable::CMD_REGISTRY_CTRL;
        pkg.arg1 = 2; // HKEY_LOCAL_MACHINE
        pkg.arg2 = 0; // List action
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
        // 树形控件右键菜单
        if (nm->idFrom == IDC_TREE_REGISTRY && nm->code == NM_RCLICK) {
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
        }
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDM_REG_REFRESH:
            // TODO: 刷新注册表
            break;
        case IDM_REG_DELETE:
            if (MessageBoxW(hDlg, L"确定要删除选中的项吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                // TODO: 删除注册表项
            }
            break;
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
