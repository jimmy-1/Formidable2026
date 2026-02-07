#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "FileManagerUI.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include "../resource.h"

namespace Formidable {
namespace UI {

// Initialize static members if necessary
std::map<HWND, uint32_t> FileManagerUI::s_dlgToClientId;
std::map<HWND, HIMAGELIST> FileManagerUI::s_dlgToImageList;
std::map<HWND, std::wstring> FileManagerUI::s_remotePath;
std::map<HWND, std::wstring> FileManagerUI::s_localPath;

void FileManagerUI::RefreshLocalFileList(HWND hDlg, const std::wstring& path) {
    HWND hList = GetDlgItem(hDlg, IDC_LIST_FILE_LOCAL);
    if (!hList) return;

    ListView_DeleteAllItems(hList);
    int i = 0;
    
    // Add ".." if not root
    if (path.length() > 3) {
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = i++;
        lvi.pszText = (LPWSTR)L"..";
        lvi.lParam = 1; // Dir
        lvi.iImage = 1; // Folder icon
        ListView_InsertItem(hList, &lvi);
    }

    std::wstring searchPath = path;
    if (searchPath.back() != L'\\') searchPath += L"\\";
    searchPath += L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        SendMessage(hList, WM_SETREDRAW, FALSE, 0);
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;

            bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            lvi.iItem = i++;
            lvi.pszText = ffd.cFileName;
            lvi.lParam = isDir ? 1 : 0;
            lvi.iImage = isDir ? 1 : 2; // Folder or File icon
            int idx = ListView_InsertItem(hList, &lvi);


            if (!isDir) {
                ULARGE_INTEGER fileSize;
                fileSize.LowPart = ffd.nFileSizeLow;
                fileSize.HighPart = ffd.nFileSizeHigh;
                wchar_t szSize[64];
                if (fileSize.QuadPart < 1024) swprintf_s(szSize, L"%llu B", fileSize.QuadPart);
                else if (fileSize.QuadPart < 1024 * 1024) swprintf_s(szSize, L"%.2f KB", fileSize.QuadPart / 1024.0);
                else if (fileSize.QuadPart < 1024 * 1024 * 1024) swprintf_s(szSize, L"%.2f MB", fileSize.QuadPart / (1024.0 * 1024.0));
                else swprintf_s(szSize, L"%.2f GB", fileSize.QuadPart / (1024.0 * 1024.0 * 1024.0));
                ListView_SetItemText(hList, idx, 1, szSize);
            } else {
                ListView_SetItemText(hList, idx, 1, (LPWSTR)L"<DIR>");
            }

            SYSTEMTIME st;
            FileTimeToSystemTime(&ffd.ftLastWriteTime, &st);
            wchar_t szTime[64];
            swprintf_s(szTime, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            ListView_SetItemText(hList, idx, 2, szTime);

        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
        SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    }
}

// Placeholder implementations for other methods to avoid linker errors if referenced
INT_PTR CALLBACK FileManagerUI::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    return FALSE;
}

void FileManagerUI::RefreshRemoteFileList(HWND hDlg, const std::wstring& path) {}
void FileManagerUI::UploadFile(HWND hDlg, uint32_t clientId) {}
void FileManagerUI::DownloadFile(HWND hDlg, uint32_t clientId) {}
void FileManagerUI::DeleteFile(HWND hDlg, bool isRemote) {}
void FileManagerUI::CreateFolder(HWND hDlg, bool isRemote) {}

void FileManagerUI::InitializeDialog(HWND hDlg, uint32_t clientId) {}
void FileManagerUI::HandleDragDrop(HWND hDlg, LPARAM lParam) {}
void FileManagerUI::HandleRightClick(HWND hDlg, LPNMHDR pnmh) {}
void FileManagerUI::HandleDoubleClick(HWND hDlg, LPNMHDR pnmh) {}
void FileManagerUI::CleanupDialog(HWND hDlg) {}

} // namespace UI
} // namespace Formidable
