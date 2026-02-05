#pragma once

// 防止 windows.h 包含旧的 winsock.h，必须在任何 Windows 头文件之前定义
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "../thirdparty/Include/HPSocket/HPSocket.h"
#include <windows.h>
#include <functional>
#include "Config.h"

namespace Formidable {

// 客户端网络事件回调
using OnConnectedCallback = std::function<void()>;
using OnDataReceivedCallback = std::function<void(const BYTE* pData, int iLength)>;
using OnDisconnectedCallback = std::function<void()>;

// HPSocket 客户端监听器
class CClientListener : public CTcpClientListener
{
public:
    CClientListener() = default;
    virtual ~CClientListener() = default;

    // 设置回调
    void SetOnConnected(OnConnectedCallback cb) { m_onConnected = cb; }
    void SetOnReceived(OnDataReceivedCallback cb) { m_onReceived = cb; }
    void SetOnDisconnected(OnDisconnectedCallback cb) { m_onDisconnected = cb; }

protected:
    virtual EnHandleResult OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket) override;
    virtual EnHandleResult OnConnect(ITcpClient* pSender, CONNID dwConnID) override;
    virtual EnHandleResult OnHandShake(ITcpClient* pSender, CONNID dwConnID) override;
    virtual EnHandleResult OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override;
    virtual EnHandleResult OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override;
    virtual EnHandleResult OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) override;

private:
    OnConnectedCallback m_onConnected;
    OnDataReceivedCallback m_onReceived;
    OnDisconnectedCallback m_onDisconnected;
};

// 客户端网络管理类
class NetworkClient
{
public:
    NetworkClient();
    ~NetworkClient();

    // 连接服务器
    bool Connect(const char* serverIP, USHORT port);
    
    // 断开连接
    void Disconnect();

    // 发送数据
    bool Send(const BYTE* pData, int iLength);

    // 是否已连接
    bool IsConnected() { return m_pClient->IsConnected(); }

    // 设置回调
    void SetOnConnected(OnConnectedCallback cb) { m_listener.SetOnConnected(cb); }
    void SetOnReceived(OnDataReceivedCallback cb) { m_listener.SetOnReceived(cb); }
    void SetOnDisconnected(OnDisconnectedCallback cb) { m_listener.SetOnDisconnected(cb); }

private:
    CClientListener m_listener;
    CTcpClientPtr m_pClient;
};

} // namespace Formidable
