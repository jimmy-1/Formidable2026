#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <memory>
#include <map>
#include "../resource.h"
#include "../../Common/Config.h"

namespace Formidable {
namespace UI {

// 注册表管理器UI类
class RegistryManagerUI {
public:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    // 刷新注册表树
    static void RefreshRegistryTree(HWND hDlg, HTREEITEM hItem);
    
    // 注册表操作
    static void DeleteRegistryKey(HWND hDlg, uint32_t clientId, HTREEITEM hItem);
    static void DeleteRegistryValue(HWND hDlg, uint32_t clientId, const std::wstring& name);
    static void EditRegistryValue(HWND hDlg, uint32_t clientId, int itemIndex);
    
private:
    static std::map<HWND, uint32_t> s_dlgToClientId;
    static std::map<HWND, HIMAGELIST> s_dlgToImageList;
    
    static void InitializeDialog(HWND hDlg, uint32_t clientId);
    static void HandleTreeExpand(HWND hDlg, LPNMTREEVIEW pnmtv);
    static void HandleTreeSelect(HWND hDlg, LPNMTREEVIEW pnmtv);
    static void HandleRightClick(HWND hDlg, LPNMHDR pnmh);
    static std::wstring GetRegistryPath(HWND hTree, HTREEITEM hItem, uint32_t& rootIdx);
    static void CleanupDialog(HWND hDlg);
};

} // namespace UI
} // namespace Formidable
