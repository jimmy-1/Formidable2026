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
#include <map>
#include <mutex>
#include "Config.h"

namespace Formidable {

// 网络事件回调
using OnClientConnectCallback = std::function<void(CONNID dwConnID, const char* ip)>;
using OnClientReceiveCallback = std::function<void(CONNID dwConnID, const BYTE* pData, int iLength)>;
using OnClientCloseCallback = std::function<void(CONNID dwConnID)>;

// HPSocket 服务端监听器
class CServerListener : public CTcpServerListener
{
public:
    CServerListener() = default;
    virtual ~CServerListener() = default;

    // 设置回调函数
    void SetOnConnect(OnClientConnectCallback cb) { m_onConnect = cb; }
    void SetOnReceive(OnClientReceiveCallback cb) { m_onReceive = cb; }
    void SetOnClose(OnClientCloseCallback cb) { m_onClose = cb; }

protected:
    // HPSocket 事件处理
    virtual EnHandleResult OnPrepareListen(ITcpServer* pSender, SOCKET soListen) override;
    virtual EnHandleResult OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient) override;
    virtual EnHandleResult OnHandShake(ITcpServer* pSender, CONNID dwConnID) override;
    virtual EnHandleResult OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override;
    virtual EnHandleResult OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override;
    virtual EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) override;
    virtual EnHandleResult OnShutdown(ITcpServer* pSender) override;

private:
    OnClientConnectCallback m_onConnect;
    OnClientReceiveCallback m_onReceive;
    OnClientCloseCallback m_onClose;
};

// 服务端网络管理类
class NetworkServer
{
public:
    NetworkServer();
    ~NetworkServer();

    // 启动服务器
    bool Start(const char* bindIP, USHORT port);
    
    // 停止服务器
    void Stop();

    // 发送数据
    bool Send(CONNID dwConnID, const BYTE* pData, int iLength);

    // 断开连接
    bool Disconnect(CONNID dwConnID);

    // 获取客户端IP
    bool GetClientAddress(CONNID dwConnID, char* lpszAddress, int& iAddressLen, USHORT& usPort);

    // 设置回调
    void SetOnConnect(OnClientConnectCallback cb) { m_listener.SetOnConnect(cb); }
    void SetOnReceive(OnClientReceiveCallback cb) { m_listener.SetOnReceive(cb); }
    void SetOnClose(OnClientCloseCallback cb) { m_listener.SetOnClose(cb); }

    // 获取连接数
    DWORD GetConnectionCount() { return m_pServer->GetConnectionCount(); }
    
    // 获取最后的错误码
    EnSocketError GetLastError() { return m_pServer->GetLastError(); }

private:
    CServerListener m_listener;
    CTcpServerPtr m_pServer;
    bool m_bStarted;
};

} // namespace Formidable
