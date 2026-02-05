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

// 前向声明
struct ConnectedClient;

namespace Formidable {
namespace UI {

// 进程管理器UI类
class ProcessManagerUI {
public:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    // 刷新进程列表
    static void RefreshProcessList(HWND hDlg, uint32_t clientId);
    
    // 结束进程
    static void KillProcess(HWND hDlg, uint32_t clientId, uint32_t pid);
    
    // 显示进程模块
    static void ShowProcessModules(HWND hDlg, uint32_t clientId, uint32_t pid);
    
private:
    static std::map<HWND, uint32_t> s_dlgToClientId;
    
    static void InitializeDialog(HWND hDlg, uint32_t clientId);
    static void HandleCommand(HWND hDlg, WPARAM wParam);
    static void HandleNotify(HWND hDlg, LPARAM lParam);
    static void CleanupDialog(HWND hDlg);
};

} // namespace UI
} // namespace Formidable
