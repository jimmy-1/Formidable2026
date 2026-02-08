// NetworkDialog.h - 网络管理对话框
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>

namespace Formidable {
namespace UI {

/// <summary>
/// 网络管理对话框管理器
/// </summary>
class NetworkDialog {
public:
    /// <summary>
    /// 对话框过程
    /// </summary>
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// 显示网络管理对话框
    /// </summary>
    static HWND Show(HWND hParent, uint32_t clientId);
};

} // namespace UI
} // namespace Formidable