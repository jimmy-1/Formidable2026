#include "NetworkClient.h"

namespace Formidable {

// ==================== CClientListener 实现 ====================

EnHandleResult CClientListener::OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket)
{
    return HR_OK;
}

EnHandleResult CClientListener::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
    if (m_onConnected) {
        m_onConnected();
    }
    return HR_OK;
}

EnHandleResult CClientListener::OnHandShake(ITcpClient* pSender, CONNID dwConnID)
{
    return HR_OK;
}

EnHandleResult CClientListener::OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
    if (m_onReceived) {
        m_onReceived(pData, iLength);
    }
    return HR_OK;
}

EnHandleResult CClientListener::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
    return HR_OK;
}

EnHandleResult CClientListener::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
{
    if (m_onDisconnected) {
        m_onDisconnected();
    }
    return HR_OK;
}

// ==================== NetworkClient 实现 ====================

NetworkClient::NetworkClient()
    : m_pClient(&m_listener)
{
    // 设置客户端参数
    m_pClient->SetKeepAliveTime(60000);      // 心跳60秒
    m_pClient->SetKeepAliveInterval(10000);  // 心跳间隔10秒  
    m_pClient->SetSocketBufferSize(8192);    // 缓冲区8KB
}

NetworkClient::~NetworkClient()
{
    Disconnect();
}

bool NetworkClient::Connect(const char* serverIP, USHORT port)
{
    if (IsConnected()) {
        return true;
    }

    // 将char*转换为wchar_t*
    wchar_t szServerIP[50] = { 0 };
    MultiByteToWideChar(CP_ACP, 0, serverIP, -1, szServerIP, 50);
    
    return m_pClient->Start((TCHAR*)szServerIP, port);
}

void NetworkClient::Disconnect()
{
    if (IsConnected()) {
        m_pClient->Stop();
    }
}

bool NetworkClient::Send(const BYTE* pData, int iLength)
{
    return m_pClient->Send(pData, iLength);
}

} // namespace Formidable
