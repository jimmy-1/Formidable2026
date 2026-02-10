// BackgroundDialog.h - 后台屏幕管理对话框
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>

namespace Formidable {
namespace UI {

class BackgroundDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static HWND Show(HWND hParent, uint32_t clientId);
};

} // namespace UI
} // namespace Formidable
