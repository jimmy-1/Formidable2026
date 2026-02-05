/**
 * InputDialog.h - 通用输入对话框
 * Encoding: UTF-8 BOM
 */
#pragma once
#include <windows.h>
#include <string>

namespace Formidable {
namespace UI {

class InputDialog {
public:
    static bool Show(HWND hParent, const wchar_t* title, const wchar_t* prompt, std::wstring& result);
    static bool Show(HWND hParent, const wchar_t* title, const wchar_t* prompt, std::wstring& result, std::wstring& result2, bool multiInput);
    
private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    struct DialogData {
        const wchar_t* title;
        const wchar_t* prompt;
        std::wstring* result;
        std::wstring* result2;
        bool multiInput;
    };
};

} // namespace UI
} // namespace Formidable
