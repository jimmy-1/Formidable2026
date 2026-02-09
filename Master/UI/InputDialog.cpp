/**
 * InputDialog.cpp - 通用输入对话框实现
 * Encoding: UTF-8 BOM
 */
#include "InputDialog.h"
#include "../resource.h"
#include "../GlobalState.h"
#include <windowsx.h>

namespace Formidable {
namespace UI {

bool InputDialog::Show(HWND hParent, const wchar_t* title, const wchar_t* prompt, std::wstring& result) {
    DialogData data = { title, prompt, &result, nullptr, false };
    INT_PTR ret = DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_INPUT), hParent, DlgProc, (LPARAM)&data);
    return (ret == IDOK);
}

bool InputDialog::Show(HWND hParent, const wchar_t* title, const wchar_t* prompt, std::wstring& result, std::wstring& result2, bool multiInput) {
    DialogData data = { title, prompt, &result, &result2, multiInput };
    INT_PTR ret = DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_INPUT), hParent, DlgProc, (LPARAM)&data);
    return (ret == IDOK);
}

INT_PTR CALLBACK InputDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static DialogData* s_data = nullptr;
    
    switch (message) {
    case WM_INITDIALOG: {
        s_data = (DialogData*)lParam;
        
        // 设置对话框标题
        SetWindowTextW(hDlg, s_data->title);
        
        // 设置提示文本
        SetDlgItemTextW(hDlg, IDC_STATIC_PROMPT, s_data->prompt);
        
        // 如果是单输入，隐藏第二个输入框（如果存在）
        // 这里简化处理，假设总是显示一个输入框，多输入功能后续扩展
        
        // 居中显示
        RECT rcDlg, rcParent;
        GetWindowRect(hDlg, &rcDlg);
        GetWindowRect(GetParent(hDlg), &rcParent);
        int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        
        // 设置焦点到输入框
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_INPUT));
        ApplyModernTheme(hDlg);
        return FALSE; // 返回FALSE因为我们手动设置了焦点
    }
    
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDOK: {
            if (s_data && s_data->result) {
                wchar_t buffer[1024] = { 0 };
                GetDlgItemTextW(hDlg, IDC_EDIT_INPUT, buffer, 1024);
                *s_data->result = buffer;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    
    return FALSE;
}

} // namespace UI
} // namespace Formidable
