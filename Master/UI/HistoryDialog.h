#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

namespace Formidable {
namespace UI {

class HistoryDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static void Show(HWND hParent);
};

} // namespace UI
} // namespace Formidable
