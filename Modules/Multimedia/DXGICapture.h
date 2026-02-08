#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class DXGICapture {
public:
    DXGICapture() : m_pDevice(nullptr), m_pContext(nullptr), m_pOutputDuplication(nullptr), m_pTexture(nullptr) {}

    ~DXGICapture() {
        Cleanup();
    }

    bool Initialize() {
        HRESULT hr = S_OK;

        // 1. 创建 D3D11 设备
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION, &m_pDevice, &featureLevel, &m_pContext);
        if (FAILED(hr)) return false;

        // 2. 获取 DXGI 输出
        IDXGIDevice* pDxgiDevice = nullptr;
        hr = m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDxgiDevice);
        if (FAILED(hr)) return false;

        IDXGIAdapter* pDxgiAdapter = nullptr;
        hr = pDxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&pDxgiAdapter);
        pDxgiDevice->Release();
        if (FAILED(hr)) return false;

        IDXGIOutput* pDxgiOutput = nullptr;
        hr = pDxgiAdapter->EnumOutputs(0, &pDxgiOutput);
        pDxgiAdapter->Release();
        if (FAILED(hr)) return false;

        IDXGIOutput1* pDxgiOutput1 = nullptr;
        hr = pDxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&pDxgiOutput1);
        pDxgiOutput->Release();
        if (FAILED(hr)) return false;

        // 3. 创建 Desktop Duplication
        hr = pDxgiOutput1->DuplicateOutput(m_pDevice, &m_pOutputDuplication);
        pDxgiOutput1->Release();
        if (FAILED(hr)) return false;

        return true;
    }

    void Cleanup() {
        if (m_pTexture) { m_pTexture->Release(); m_pTexture = nullptr; }
        if (m_pOutputDuplication) { m_pOutputDuplication->Release(); m_pOutputDuplication = nullptr; }
        if (m_pContext) { m_pContext->Release(); m_pContext = nullptr; }
        if (m_pDevice) { m_pDevice->Release(); m_pDevice = nullptr; }
    }

    // 抓取一帧
    // 返回: 成功返回 true，否则 false
    // timeout: 等待超时 (ms)
    // pOutBits: 输出缓冲区 (RGB/BGR)
    // outWidth, outHeight: 输出尺寸
    bool CaptureFrame(std::vector<BYTE>& outBuffer, int& outWidth, int& outHeight, int timeout = 100) {
        if (!m_pOutputDuplication) return false;

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        IDXGIResource* pDesktopResource = nullptr;
        
        // 获取下一帧
        HRESULT hr = m_pOutputDuplication->AcquireNextFrame(timeout, &frameInfo, &pDesktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false; // 无新帧
        }
        if (FAILED(hr)) {
            // 可能是设备丢失或分辨率改变，需要重新初始化
            Cleanup();
            Initialize();
            return false;
        }

        ID3D11Texture2D* pAcquiredDesktopImage = nullptr;
        hr = pDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pAcquiredDesktopImage);
        pDesktopResource->Release();
        
        if (FAILED(hr)) {
            m_pOutputDuplication->ReleaseFrame();
            return false;
        }

        // 获取描述
        D3D11_TEXTURE2D_DESC frameDesc;
        pAcquiredDesktopImage->GetDesc(&frameDesc);
        outWidth = frameDesc.Width;
        outHeight = frameDesc.Height;

        // 确保我们有 Staging Texture 用于 CPU 读取
        if (!m_pTexture || m_desc.Width != frameDesc.Width || m_desc.Height != frameDesc.Height) {
            if (m_pTexture) m_pTexture->Release();
            
            D3D11_TEXTURE2D_DESC desc = frameDesc;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            
            hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pTexture);
            if (FAILED(hr)) {
                pAcquiredDesktopImage->Release();
                m_pOutputDuplication->ReleaseFrame();
                return false;
            }
            m_desc = desc;
        }

        // 复制 GPU 纹理到 Staging 纹理
        m_pContext->CopyResource(m_pTexture, pAcquiredDesktopImage);
        pAcquiredDesktopImage->Release();
        m_pOutputDuplication->ReleaseFrame();

        // 映射内存
        D3D11_MAPPED_SUBRESOURCE mapInfo;
        hr = m_pContext->Map(m_pTexture, 0, D3D11_MAP_READ, 0, &mapInfo);
        if (FAILED(hr)) return false;

        // 复制数据到输出 buffer (注意 stride)
        // DXGI 格式通常是 B8G8R8A8_UNORM (32bit)
        // 我们通常需要 24bit BGR 或者保持 32bit BGRA
        // Multimedia.cpp 里的逻辑似乎是用 24bit BGR (GDI default)
        // 为了兼容，我们转成 24bit BGR
        
        int rowPitch = mapInfo.RowPitch;
        BYTE* pSrc = (BYTE*)mapInfo.pData;
        
        size_t size = outWidth * outHeight * 3;
        if (outBuffer.size() != size) outBuffer.resize(size);
        
        for (int y = 0; y < outHeight; ++y) {
            BYTE* s = pSrc + y * rowPitch;
            BYTE* d = outBuffer.data() + (outHeight - 1 - y) * outWidth * 3; // 倒序(Bottom-Up)以匹配 GDI DIBSection? 
            // 之前的代码我改成了 Top-Down (height为负)。
            // 如果 g_screenCaptureMethod 切换，DIBSection 逻辑可能会变。
            // 让我们统一使用 Top-Down。
            // 修正：DIBSection Top-Down 意味着内存也是 Top-Down。
            // 所以这里直接拷贝。
            d = outBuffer.data() + y * outWidth * 3;

            for (int x = 0; x < outWidth; ++x) {
                d[0] = s[0]; // B
                d[1] = s[1]; // G
                d[2] = s[2]; // R
                // s[3] is Alpha, ignore
                s += 4;
                d += 3;
            }
        }

        m_pContext->Unmap(m_pTexture, 0);
        return true;
    }

private:
    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pContext;
    IDXGIOutputDuplication* m_pOutputDuplication;
    ID3D11Texture2D* m_pTexture;
    D3D11_TEXTURE2D_DESC m_desc;
};
