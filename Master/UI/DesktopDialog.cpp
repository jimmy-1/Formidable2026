// DesktopDialog.cpp - 远程桌面对话框实现
#include "DesktopDialog.h"
#include "../resource.h"
#include "../GlobalState.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include <CommCtrl.h>
#include <windowsx.h>
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include "../Utils/StringHelper.h"
#include "../MainWindow.h"
#pragma comment(lib, "comctl32.lib")

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);
extern bool SendModuleToClient(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2 = 0);

namespace Formidable {
namespace UI {

// 每个窗口独立状态
struct DesktopState {
    uint32_t clientId = 0;
    int fps = 15;
    int quality = 0; // 0=1080p, 1=Original
    int compress = 1; // 0=Raw, 1=JPEG
    bool useGrayscale = false;
    bool useDiff = true;
    bool isControlEnabled = false;
    bool isFullscreen = false;
    bool isStretched = true;
    
    // 运行时状态
    int retryCount = 0;
    bool hasFrame = false;
    bool isRecording = false;

    // 远程屏幕信息 (用于滚动和坐标计算)
    int remoteWidth = 0;
    int remoteHeight = 0;
    int scrollX = 0;
    int scrollY = 0;
};

static std::map<HWND, DesktopState> s_desktopStates;
static const UINT WM_APP_DESKTOP_FRAME = WM_APP + 220;
static const UINT WM_APP_UPDATE_SCROLLBARS = WM_APP + 221;
static const UINT_PTR TIMER_DESKTOP_FIRST_FRAME = 0xD260;

// 辅助函数：更新滚动条
static void UpdateScrollBars(HWND hDlg, DesktopState& state, int clientW, int clientH) {
    if (state.isStretched) {
        ShowScrollBar(hDlg, SB_BOTH, FALSE);
        return;
    }

    // 水平滚动条
    SCROLLINFO si = { 0 };
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_ALL;
    si.nMin = 0;
    si.nMax = state.remoteWidth;
    si.nPage = clientW;
    si.nPos = state.scrollX;
    SetScrollInfo(hDlg, SB_HORZ, &si, TRUE);

    // 垂直滚动条
    si.nMax = state.remoteHeight;
    si.nPage = clientH;
    si.nPos = state.scrollY;
    SetScrollInfo(hDlg, SB_VERT, &si, TRUE);

    ShowScrollBar(hDlg, SB_BOTH, TRUE);
}

// 远程桌面子类化过程
LRESULT CALLBACK DesktopScreenProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    uint32_t cid = (uint32_t)uIdSubclass; 
    HWND hDlg = (HWND)dwRefData; // 父窗口句柄
    
    std::shared_ptr<Formidable::ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(cid)) client = g_Clients[cid];
    }

    // 获取状态引用
    if (s_desktopStates.find(hDlg) == s_desktopStates.end()) {
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }
    DesktopState& state = s_desktopStates[hDlg];

    // 状态同步：确保对话框内部状态与全局 client->isMonitoring 同步
    if (client) {
        state.isControlEnabled = client->isMonitoring;
    }

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

            // 检查远程分辨率是否变化
            if (bmp.bmWidth != state.remoteWidth || bmp.bmHeight != state.remoteHeight) {
                state.remoteWidth = bmp.bmWidth;
                state.remoteHeight = bmp.bmHeight;
                PostMessage(hDlg, WM_APP_UPDATE_SCROLLBARS, 0, 0);
            }
            
            if (state.isStretched) {
                // 强制拉伸显示
                SetStretchBltMode(hdc, HALFTONE); // 使用 HALFTONE 提高缩放质量
                SetBrushOrgEx(hdc, 0, 0, NULL);
                StretchBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
            } else {
                // 原始大小显示 (带滚动)
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, state.scrollX, state.scrollY, SRCCOPY);
                
                // 如果图片小于窗口，填充背景
                if (bmp.bmWidth < rc.right) {
                    RECT bg = { bmp.bmWidth, 0, rc.right, rc.bottom };
                    FillRect(hdc, &bg, (HBRUSH)GetStockObject(BLACK_BRUSH));
                }
                if (bmp.bmHeight < rc.bottom) {
                    RECT bg = { 0, bmp.bmHeight, rc.right, rc.bottom };
                    FillRect(hdc, &bg, (HBRUSH)GetStockObject(BLACK_BRUSH));
                }
            }
            
            SelectObject(hMemDC, hOldBmp);
            DeleteDC(hMemDC);
        } else {
            // 绘制黑色背景
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        
        EndPaint(hWnd, &ps);
        return 0;
    }

    // 处理鼠标和键盘事件
    if (client) {
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
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL: {
            int clientX = (int)(short)LOWORD(lParam);
            int clientY = (int)(short)HIWORD(lParam);

            // 区域划分逻辑：
            // 全局屏幕显示区域：用于操作远程屏幕，映射所有键盘鼠标
            
            // 如果未启用控制，直接拦截消息但不发送
            if (!state.isControlEnabled) {
                return 0;
            }

            Formidable::RemoteMouseEvent ev = { 0 };
            ev.msg = message;
            RECT rc;
            GetClientRect(hWnd, &rc);
            
            // 计算远程坐标
            int remoteX = 0, remoteY = 0;

            if (state.isStretched) {
                if (rc.right > 0 && rc.bottom > 0 && state.remoteWidth > 0 && state.remoteHeight > 0) {
                    remoteX = clientX * state.remoteWidth / rc.right;
                    remoteY = clientY * state.remoteHeight / rc.bottom;
                }
            } else {
                remoteX = clientX + state.scrollX;
                remoteY = clientY + state.scrollY;
            }

            if (state.remoteWidth > 0 && state.remoteHeight > 0) {
                ev.x = remoteX * 65535 / state.remoteWidth;
                ev.y = remoteY * 65535 / state.remoteHeight;
            } else {
                if (rc.right > 0 && rc.bottom > 0) {
                    ev.x = clientX * 65535 / rc.right;
                    ev.y = clientY * 65535 / rc.bottom;
                }
            }

            if (message == WM_MOUSEWHEEL) ev.data = (short)HIWORD(wParam);

            if (message == WM_LBUTTONDBLCLK) ev.msg = WM_LBUTTONDOWN;
            else if (message == WM_RBUTTONDBLCLK) ev.msg = WM_RBUTTONDOWN;
            else if (message == WM_MBUTTONDBLCLK) ev.msg = WM_MBUTTONDOWN;

            SendRemoteControl(Formidable::CMD_MOUSE_EVENT, &ev, sizeof(Formidable::RemoteMouseEvent));
            
            // 重要：处理完消息后直接返回 0，不再执行 DefSubclassProc
            // 否则系统可能会根据 RBUTTONUP 消息再次触发 WM_CONTEXTMENU
            return 0; 
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            // 如果未启用控制，直接拦截消息但不发送
            if (!state.isControlEnabled) {
                return 0;
            }

            Formidable::RemoteKeyEvent ev = { 0 };
            ev.msg = message;
            ev.vk = (uint32_t)wParam;
            SendRemoteControl(Formidable::CMD_KEY_EVENT, &ev, sizeof(Formidable::RemoteKeyEvent));
            return 0;
        }
        }
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

#define IDM_DESKTOP_COMPRESS_GRAYSCALE 50001
#define IDM_DESKTOP_COMPRESS_DIFF 50002

INT_PTR CALLBACK DesktopDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        struct InitParams {
            uint32_t clientId;
            bool isGrayscale;
        };
        InitParams* params = (InitParams*)lParam;
        uint32_t clientId = params->clientId;
        bool isGrayscale = params->isGrayscale;
        delete params; // Clean up

        // Initialize state
        DesktopState state;
        state.clientId = clientId;
        state.useGrayscale = isGrayscale;
        state.useDiff = g_Settings.useDiffTransmission; // Use global setting
        state.compress = g_Settings.imageCompressMethod; // Use global setting
        state.retryCount = 0;
        state.hasFrame = false;
        state.isRecording = false;
        
        s_desktopStates[hDlg] = state;

        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_DESKTOP)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_DESKTOP)));

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            client->isMonitoring = false; // 初始禁用控制
            client->hDesktopDlg = hDlg;
            
            // 设置默认标题
            std::wstring title = L"远程桌面 - " + Formidable::Utils::StringHelper::UTF8ToWide(client->info.computerName);
            if (state.useGrayscale) title += L" (灰度模式)";
            SetWindowTextW(hDlg, title.c_str());
            
            // 初始化屏幕显示控件，设置文本提示
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            if (hStatic) {
                SetWindowTextW(hStatic, L"正在请求画面...");
            }
            
            // 先加载多媒体模块（必须携带 arg2，客户端才会将其识别为多媒体常驻模块）
            if (!SendModuleToClient(clientId, Formidable::CMD_LOAD_MODULE, L"Multimedia.dll", Formidable::CMD_SCREEN_CAPTURE)) {
                if (hStatic) SetWindowTextW(hStatic, L"加载多媒体模块失败");
                break;
            }
            
            // 等待模块加载完成
            Sleep(100);
            
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = Formidable::CMD_SCREEN_CAPTURE;
            pkg.arg1 = 1;
            pkg.arg2 = 0;
            
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
            memcpy(header->flag, "FRMD26?", 7);
            header->originLen = (int)bodySize;
            header->totalLen = (int)buffer.size();
            memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            
            SendDataToClient(client, buffer.data(), (int)buffer.size());
            
            Formidable::CommandPkg compressPkg = { 0 };
            compressPkg.cmd = Formidable::CMD_SCREEN_COMPRESS;
            compressPkg.arg1 = state.compress;
            compressPkg.arg2 = (state.useDiff ? 2 : 0) | (state.useGrayscale ? 1 : 0);
            
            size_t compressBodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> compressBuffer(sizeof(Formidable::PkgHeader) + compressBodySize);
            Formidable::PkgHeader* compressHeader = (Formidable::PkgHeader*)compressBuffer.data();
            memcpy(compressHeader->flag, "FRMD26?", 7);
            compressHeader->originLen = (int)compressBodySize;
            compressHeader->totalLen = (int)compressBuffer.size();
            memcpy(compressBuffer.data() + sizeof(Formidable::PkgHeader), &compressPkg, compressBodySize);
            
            SendDataToClient(client, compressBuffer.data(), (int)compressBuffer.size());
            
            Formidable::CommandPkg qualityPkg = { 0 };
            qualityPkg.cmd = Formidable::CMD_SCREEN_QUALITY;
            qualityPkg.arg1 = state.quality;
            qualityPkg.arg2 = 0;

            size_t qualityBodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> qualityBuffer(sizeof(Formidable::PkgHeader) + qualityBodySize);
            Formidable::PkgHeader* qualityHeader = (Formidable::PkgHeader*)qualityBuffer.data();
            memcpy(qualityHeader->flag, "FRMD26?", 7);
            qualityHeader->originLen = (int)qualityBodySize;
            qualityHeader->totalLen = (int)qualityBuffer.size();
            memcpy(qualityBuffer.data() + sizeof(Formidable::PkgHeader), &qualityPkg, qualityBodySize);

            SendDataToClient(client, qualityBuffer.data(), (int)qualityBuffer.size());

            Formidable::CommandPkg fpsPkg = { 0 };
            fpsPkg.cmd = Formidable::CMD_SCREEN_FPS;
            fpsPkg.arg1 = state.fps;
            fpsPkg.arg2 = 0;

            size_t fpsBodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> fpsBuffer(sizeof(Formidable::PkgHeader) + fpsBodySize);
            Formidable::PkgHeader* fpsHeader = (Formidable::PkgHeader*)fpsBuffer.data();
            memcpy(fpsHeader->flag, "FRMD26?", 7);
            fpsHeader->originLen = (int)fpsBodySize;
            fpsHeader->totalLen = (int)fpsBuffer.size();
            memcpy(fpsBuffer.data() + sizeof(Formidable::PkgHeader), &fpsPkg, fpsBodySize);

            SendDataToClient(client, fpsBuffer.data(), (int)fpsBuffer.size());

            SetTimer(hDlg, TIMER_DESKTOP_FIRST_FRAME, 2500, NULL);
            
            // Subclass the static control to capture inputs
            hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            SetWindowSubclass(hStatic, DesktopScreenProc, clientId, (DWORD_PTR)hDlg);
        }

        ApplyModernTheme(hDlg);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
        if (hStatic) {
            RECT rc;
            GetClientRect(hDlg, &rc);
            MoveWindow(hStatic, 0, 0, rc.right, rc.bottom, TRUE);

            if (s_desktopStates.count(hDlg)) {
                DesktopState& state = s_desktopStates[hDlg];
                UpdateScrollBars(hDlg, state, rc.right, rc.bottom);
            }
        }
        return (INT_PTR)TRUE;
    }
    case WM_APP_UPDATE_SCROLLBARS: {
        if (s_desktopStates.count(hDlg)) {
            DesktopState& state = s_desktopStates[hDlg];
            RECT rc;
            GetClientRect(hDlg, &rc);
            UpdateScrollBars(hDlg, state, rc.right, rc.bottom);
            // 触发重绘
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            if (hStatic) InvalidateRect(hStatic, NULL, FALSE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_HSCROLL: {
        if (s_desktopStates.count(hDlg)) {
            DesktopState& state = s_desktopStates[hDlg];
            SCROLLINFO si = { 0 };
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(hDlg, SB_HORZ, &si);
            
            int newPos = si.nPos;
            switch (LOWORD(wParam)) {
            case SB_LINELEFT: newPos -= 10; break;
            case SB_LINERIGHT: newPos += 10; break;
            case SB_PAGELEFT: newPos -= si.nPage; break;
            case SB_PAGERIGHT: newPos += si.nPage; break;
            case SB_THUMBTRACK: newPos = si.nTrackPos; break;
            }
            
            if (newPos < 0) newPos = 0;
            if (newPos > si.nMax - (int)si.nPage + 1) newPos = si.nMax - (int)si.nPage + 1;
            
            if (newPos != state.scrollX) {
                state.scrollX = newPos;
                SetScrollPos(hDlg, SB_HORZ, newPos, TRUE);
                HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
                if (hStatic) InvalidateRect(hStatic, NULL, FALSE);
            }
        }
        return (INT_PTR)TRUE;
    }
    case WM_VSCROLL: {
        if (s_desktopStates.count(hDlg)) {
            DesktopState& state = s_desktopStates[hDlg];
            SCROLLINFO si = { 0 };
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(hDlg, SB_VERT, &si);
            
            int newPos = si.nPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: newPos -= 10; break;
            case SB_LINEDOWN: newPos += 10; break;
            case SB_PAGEUP: newPos -= si.nPage; break;
            case SB_PAGEDOWN: newPos += si.nPage; break;
            case SB_THUMBTRACK: newPos = si.nTrackPos; break;
            }
            
            if (newPos < 0) newPos = 0;
            if (newPos > si.nMax - (int)si.nPage + 1) newPos = si.nMax - (int)si.nPage + 1;
            
            if (newPos != state.scrollY) {
                state.scrollY = newPos;
                SetScrollPos(hDlg, SB_VERT, newPos, TRUE);
                HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
                if (hStatic) InvalidateRect(hStatic, NULL, FALSE);
            }
        }
        return (INT_PTR)TRUE;
    }
    case WM_TIMER: {
        if (wParam == TIMER_DESKTOP_FIRST_FRAME) {
            DesktopState& state = s_desktopStates[hDlg];
            
            if (!state.hasFrame) {
                if (state.retryCount >= 3) {
                    KillTimer(hDlg, TIMER_DESKTOP_FIRST_FRAME);
                    HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
                    if (hStatic) SetWindowTextW(hStatic, L"连接超时，未收到画面");
                    AddLog(L"桌面", L"连接超时，未收到画面");
                    return (INT_PTR)TRUE;
                }

                state.retryCount++;
                HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
                
                if (state.retryCount == 1 && state.compress != 0) {
                    state.compress = 0;
                    if (hStatic) SetWindowTextW(hStatic, L"未收到画面，切换RAW重试...");
                    AddLog(L"桌面", L"未收到画面，切换RAW重试");
                } else {
                    if (hStatic) SetWindowTextW(hStatic, L"未收到画面，正在重试...");
                    AddLog(L"桌面", L"未收到画面，正在重试");
                }

                uint32_t clientId = state.clientId;
                std::shared_ptr<Formidable::ConnectedClient> client;
                {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    if (g_Clients.count(clientId)) client = g_Clients[clientId];
                }
                if (!client) return (INT_PTR)TRUE;

                SendModuleToClient(clientId, Formidable::CMD_LOAD_MODULE, L"Multimedia.dll", Formidable::CMD_SCREEN_CAPTURE);

                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_SCREEN_CAPTURE;
                pkg.arg1 = 1;
                // arg2: Capture Method (0=GDI, 1=DXGI)
                pkg.arg2 = g_Settings.screenCaptureMethod;

                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                SendDataToClient(client, buffer.data(), (int)buffer.size());

                Formidable::CommandPkg compressPkg = { 0 };
                compressPkg.cmd = Formidable::CMD_SCREEN_COMPRESS;
                compressPkg.arg1 = state.compress;
                compressPkg.arg2 = (state.useDiff ? 2 : 0) | (state.useGrayscale ? 1 : 0);

                size_t compressBodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> compressBuffer(sizeof(Formidable::PkgHeader) + compressBodySize);
                Formidable::PkgHeader* compressHeader = (Formidable::PkgHeader*)compressBuffer.data();
                memcpy(compressHeader->flag, "FRMD26?", 7);
                compressHeader->originLen = (int)compressBodySize;
                compressHeader->totalLen = (int)compressBuffer.size();
                memcpy(compressBuffer.data() + sizeof(Formidable::PkgHeader), &compressPkg, compressBodySize);
                SendDataToClient(client, compressBuffer.data(), (int)compressBuffer.size());
            }
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_APP_DESKTOP_FRAME: {
        if (s_desktopStates.count(hDlg)) {
            s_desktopStates[hDlg].hasFrame = true;
            KillTimer(hDlg, TIMER_DESKTOP_FIRST_FRAME);
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            if (hStatic) SetWindowTextW(hStatic, L"");
        }
        return (INT_PTR)TRUE;
    }
    case WM_CONTEXTMENU: {
        // 获取点击位置
        POINT pt;
        bool isKeyboard = false;
        if (lParam == -1) {
            GetCursorPos(&pt);
            isKeyboard = true;
        } else {
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
        }

        // 检查点击位置
        POINT ptClient = pt;
        ScreenToClient(hDlg, &ptClient);

        // 逻辑调整：
        // 1. 如果是键盘触发 (Shift+F10 / Menu键)，允许弹出菜单
        // 2. 如果点击的是非客户区（如标题栏、边框等，不在 GetClientRect 范围内），允许弹出菜单 (这就是用户说的“右键边框”)
        // 3. 如果点击的是客户区 (远程屏幕显示区域)，则拦截，不弹出本地菜单（因为这部分区域要留给远程控制）
        
        RECT rcClient;
        GetClientRect(hDlg, &rcClient);

        if (!isKeyboard && PtInRect(&rcClient, ptClient)) {
            // 在屏幕显示区域：拦截本地菜单弹出
            return (INT_PTR)TRUE; 
        }

        DesktopState& state = s_desktopStates[hDlg];
        HMENU hMenu = CreatePopupMenu();
        HMENU hFpsMenu = CreatePopupMenu();
        HMENU hResMenu = CreatePopupMenu();
        HMENU hCompMenu = CreatePopupMenu();
        
        // FPS子菜单
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_5, L"5 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_10, L"10 FPS");
        AppendMenuW(hFpsMenu, (state.fps == 15 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FPS_15, L"15 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_20, L"20 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_25, L"25 FPS");
        AppendMenuW(hFpsMenu, MF_STRING, IDM_DESKTOP_FPS_30, L"30 FPS");
        
        // 分辨率子菜单
        AppendMenuW(hResMenu, (state.quality == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_RES_ORIGINAL, L"原始分辨率");
        AppendMenuW(hResMenu, (state.quality == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_RES_1080P, L"1080P");

        // 压缩方案子菜单
        AppendMenuW(hCompMenu, (state.compress == 0 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_COMPRESS_RAW, L"未压缩 (RAW)");
        AppendMenuW(hCompMenu, (state.compress == 1 ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_COMPRESS_JPEG, L"高效压缩 (JPEG)");
        // 移除灰度模式和差异传输选项 (已改为设置或启动参数)
        
        // 主菜单
        AppendMenuW(hMenu, (state.isControlEnabled ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_CONTROL, L"控制屏幕(&C)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        // AppendMenuW(hMenu, (state.isFullscreen ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_FULLSCREEN, L"全屏显示(&F)");
        AppendMenuW(hMenu, (state.isStretched ? MF_CHECKED : 0) | MF_STRING, IDM_DESKTOP_STRETCH, L"拉伸显示(&S)");
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
        DesktopState& state = s_desktopStates[hDlg];
        uint32_t clientId = state.clientId;
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
            state.isControlEnabled = !state.isControlEnabled;
            // 同步到 ConnectedClient，以便其他地方（如 main_gui.cpp）也能获取到控制状态
            client->isMonitoring = state.isControlEnabled;
            AddLog(L"桌面", state.isControlEnabled ? L"已启用鼠标控制" : L"已禁用鼠标控制");
            break;
            
        case IDM_DESKTOP_FULLSCREEN: {
            state.isFullscreen = !state.isFullscreen;
            if (state.isFullscreen) {
                SetWindowLongPtr(hDlg, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                ShowWindow(hDlg, SW_MAXIMIZE);
            } else {
                SetWindowLongPtr(hDlg, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                ShowWindow(hDlg, SW_NORMAL);
            }
            break;
        }
        
        case IDM_DESKTOP_STRETCH: {
            state.isStretched = !state.isStretched;
            RECT rc;
            GetClientRect(hDlg, &rc);
            UpdateScrollBars(hDlg, state, rc.right, rc.bottom);
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            if (hStatic) InvalidateRect(hStatic, NULL, FALSE);
            break;
        }            
        case IDM_DESKTOP_FPS_5:
        case IDM_DESKTOP_FPS_10:
        case IDM_DESKTOP_FPS_15:
        case IDM_DESKTOP_FPS_20:
        case IDM_DESKTOP_FPS_25:
        case IDM_DESKTOP_FPS_30: {
            int fps = 5 + (LOWORD(wParam) - IDM_DESKTOP_FPS_5) * 5;
            state.fps = fps;
            SendCommand(Formidable::CMD_SCREEN_FPS, fps);
            wchar_t msg[64];
            wsprintfW(msg, L"已设置帧率为 %d FPS", fps);
            MessageBoxW(hDlg, msg, L"提示", MB_OK);
            break;
        }
        
        case IDM_DESKTOP_RES_ORIGINAL:
            state.quality = 1;
            SendCommand(Formidable::CMD_SCREEN_QUALITY, 1);
            MessageBoxW(hDlg, L"已设置原始分辨率", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_RES_1080P:
            state.quality = 0;
            SendCommand(Formidable::CMD_SCREEN_QUALITY, 0);
            MessageBoxW(hDlg, L"已设置1080P限制", L"提示", MB_OK);
            break;

        case IDM_DESKTOP_COMPRESS_RAW:
            state.compress = 0;
            SendCommand(Formidable::CMD_SCREEN_COMPRESS, 0, (state.useDiff ? 2 : 0) | (state.useGrayscale ? 1 : 0));
            MessageBoxW(hDlg, L"已切换至 RAW (无损) 传输", L"提示", MB_OK);
            break;

        case IDM_DESKTOP_COMPRESS_JPEG:
            state.compress = 1;
            SendCommand(Formidable::CMD_SCREEN_COMPRESS, 1, (state.useDiff ? 2 : 0) | (state.useGrayscale ? 1 : 0));
            MessageBoxW(hDlg, L"已切换至 JPEG (压缩) 传输", L"提示", MB_OK);
            break;
            
        case IDM_DESKTOP_COMPRESS_GRAYSCALE:
            // 移除热切换支持
            break;

        case IDM_DESKTOP_COMPRESS_DIFF:
            // 移除热切换支持
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
            HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_SCREEN);
            HBITMAP hBmp = (HBITMAP)SendMessage(hStatic, STM_GETIMAGE, IMAGE_BITMAP, 0);
            if (hBmp) {
                BITMAP bmp;
                GetObject(hBmp, sizeof(BITMAP), &bmp);
                
                BITMAPINFOHEADER bi;
                bi.biSize = sizeof(BITMAPINFOHEADER);
                bi.biWidth = bmp.bmWidth;
                bi.biHeight = bmp.bmHeight;
                bi.biPlanes = 1;
                bi.biBitCount = 24;
                bi.biCompression = BI_RGB;
                bi.biSizeImage = 0;
                bi.biXPelsPerMeter = 0;
                bi.biYPelsPerMeter = 0;
                bi.biClrUsed = 0;
                bi.biClrImportant = 0;
                
                int rowSize = ((bmp.bmWidth * 24 + 31) / 32) * 4;
                DWORD dwSizeImage = rowSize * bmp.bmHeight;
                std::vector<char> bmpData(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwSizeImage);
                
                BITMAPFILEHEADER* pBFH = (BITMAPFILEHEADER*)bmpData.data();
                pBFH->bfType = 0x4D42;
                pBFH->bfSize = (DWORD)bmpData.size();
                pBFH->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
                
                memcpy(bmpData.data() + sizeof(BITMAPFILEHEADER), &bi, sizeof(bi));
                
                HDC hdc = GetDC(hStatic);
                GetDIBits(hdc, hBmp, 0, bmp.bmHeight, bmpData.data() + pBFH->bfOffBits, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
                ReleaseDC(hStatic, hdc);
                
                wchar_t filename[MAX_PATH];
                SYSTEMTIME st;
                GetLocalTime(&st);
                wsprintfW(filename, L"screenshot_%04d%02d%02d_%02d%02d%02d.bmp", 
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                
                HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD dwWritten;
                    WriteFile(hFile, bmpData.data(), (DWORD)bmpData.size(), &dwWritten, NULL);
                    CloseHandle(hFile);
                    
                    wchar_t msg[MAX_PATH + 100];
                    wsprintfW(msg, L"截图已保存到: %s", filename);
                    MessageBoxW(hDlg, msg, L"提示", MB_OK);
                } else {
                    MessageBoxW(hDlg, L"保存截图失败", L"错误", MB_OK | MB_ICONERROR);
                }
            } else {
                MessageBoxW(hDlg, L"没有可保存的截图", L"提示", MB_OK);
            }
            break;
        }
        
        case IDM_DESKTOP_REC_MJPEG:
            if (!state.isRecording) {
                SendCommand(Formidable::CMD_VIDEO_STREAM, 1);
                state.isRecording = true;
                MessageBoxW(hDlg, L"视频录制已开始", L"提示", MB_OK);
            } else {
                SendCommand(Formidable::CMD_VIDEO_STREAM, 0);
                state.isRecording = false;
                MessageBoxW(hDlg, L"视频录制已停止", L"提示", MB_OK);
            }
            break;
            
        case IDCANCEL:
            PostMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE: {
        KillTimer(hDlg, TIMER_DESKTOP_FIRST_FRAME);
        
        if (s_desktopStates.count(hDlg)) {
            DesktopState& state = s_desktopStates[hDlg];
            uint32_t clientId = state.clientId;
            
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
            
            s_desktopStates.erase(hDlg); // Cleanup state
        }
        
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}

HWND DesktopDialog::Show(HWND hParent, uint32_t clientId, bool isGrayscale) {
    // Pass params as a struct pointer
    struct InitParams {
        uint32_t clientId;
        bool isGrayscale;
    };
    InitParams* params = new InitParams{ clientId, isGrayscale };
    return CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_DESKTOP), hParent, DlgProc, (LPARAM)params);
}

} // namespace UI
} // namespace Formidable
