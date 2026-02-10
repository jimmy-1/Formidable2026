// FileDialog.h - 文件管理对话框
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>

namespace Formidable {
namespace UI {

constexpr UINT WM_FILE_UPDATE_DRIVES = WM_APP + 301;
constexpr UINT WM_FILE_SHOW_LIST = WM_APP + 302;
constexpr UINT WM_FILE_LOADING_STATE = WM_APP + 303;
constexpr UINT WM_FILE_UPDATE_LIST = WM_APP + 304;

/// <summary>
/// 文件管理对话框管理器
/// </summary>
class FileDialog {
public:
    /// <summary>
    /// 对话框过程
    /// </summary>
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// 显示文件管理对话框
    /// </summary>
    static HWND Show(HWND hParent, uint32_t clientId);
};

} // namespace UI
} // namespace Formidable
