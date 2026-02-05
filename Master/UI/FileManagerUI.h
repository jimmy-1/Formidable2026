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

// 文件管理器UI类
class FileManagerUI {
public:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    // 刷新文件列表
    static void RefreshRemoteFileList(HWND hDlg, const std::wstring& path);
    static void RefreshLocalFileList(HWND hDlg, const std::wstring& path);
    
    // 文件操作
    static void UploadFile(HWND hDlg, uint32_t clientId);
    static void DownloadFile(HWND hDlg, uint32_t clientId);
    static void DeleteFile(HWND hDlg, bool isRemote);
    static void CreateFolder(HWND hDlg, bool isRemote);
    
private:
    static std::map<HWND, uint32_t> s_dlgToClientId;
    static std::map<HWND, HIMAGELIST> s_dlgToImageList;
    static std::map<HWND, std::wstring> s_remotePath;
    static std::map<HWND, std::wstring> s_localPath;
    
    static void InitializeDialog(HWND hDlg, uint32_t clientId);
    static void HandleDragDrop(HWND hDlg, LPARAM lParam);
    static void HandleRightClick(HWND hDlg, LPNMHDR pnmh);
    static void HandleDoubleClick(HWND hDlg, LPNMHDR pnmh);
    static void CleanupDialog(HWND hDlg);
};

} // namespace UI
} // namespace Formidable
