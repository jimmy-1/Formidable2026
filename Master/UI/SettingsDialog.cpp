#include "SettingsDialog.h"
#include "../GlobalState.h"
#include "../Config.h"
#include "../StringUtils.h"
#include "../resource.h"

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        // 窗口消息定义
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_SETTINGS)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_SETTINGS)));
        
        SetDlgItemTextW(hDlg, IDC_EDIT_LISTEN_PORT, std::to_wstring(g_Settings.listenPort).c_str());
        
        // FRP 设置
        SetDlgItemTextW(hDlg, IDC_EDIT_PUBLIC_IP, g_Settings.szFrpServer);
        SetDlgItemInt(hDlg, IDC_EDIT_FRP_FORWARD, g_Settings.frpRemotePort, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_DOWNLOAD_PORT, g_Settings.frpDownloadPort, FALSE);
        SetDlgItemTextW(hDlg, IDC_EDIT_TOKEN, g_Settings.szFrpToken);
        CheckRadioButton(hDlg, IDC_RADIO_FRP_NO, IDC_RADIO_FRP_YES, 
                         g_Settings.bEnableFrp ? IDC_RADIO_FRP_YES : IDC_RADIO_FRP_NO);

        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDC_BTN_SAVE_SETTINGS) {
            wchar_t wBuf[128];
            GetDlgItemTextW(hDlg, IDC_EDIT_LISTEN_PORT, wBuf, 8);
            g_Settings.listenPort = _wtoi(wBuf);

            // 保存 FRP 设置
            GetDlgItemTextW(hDlg, IDC_EDIT_PUBLIC_IP, g_Settings.szFrpServer, 128);
            g_Settings.frpRemotePort = GetDlgItemInt(hDlg, IDC_EDIT_FRP_FORWARD, NULL, FALSE);
            g_Settings.frpDownloadPort = GetDlgItemInt(hDlg, IDC_EDIT_DOWNLOAD_PORT, NULL, FALSE);
            GetDlgItemTextW(hDlg, IDC_EDIT_TOKEN, g_Settings.szFrpToken, 64);
            g_Settings.bEnableFrp = IsDlgButtonChecked(hDlg, IDC_RADIO_FRP_YES) == BST_CHECKED;

            SaveSettings();
            MessageBoxW(hDlg, L"设置已保存", L"提示", MB_OK | MB_ICONINFORMATION);
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
