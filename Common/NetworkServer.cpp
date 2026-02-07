#include "NetworkServer.h"
#include <ws2tcpip.h>

namespace Formidable {

// ==================== CServerListener 实现 ====================

EnHandleResult CServerListener::OnPrepareListen(ITcpServer* pSender, SOCKET soListen)
{
    return HR_OK;
}

EnHandleResult CServerListener::OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient)
{
    wchar_t szAddress[50] = { 0 };
    int iAddressLen = 50;
    USHORT usPort = 0;
    
    pSender->GetRemoteAddress(dwConnID, (TCHAR*)szAddress, iAddressLen, usPort);
    
    if (m_onConnect) {
        // 转换wchar_t到char*供回调使用
        char szAddressA[50] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, szAddress, -1, szAddressA, sizeof(szAddressA), NULL, NULL);
        m_onConnect(dwConnID, szAddressA);
    }
    
    return HR_OK;
}

EnHandleResult CServerListener::OnHandShake(ITcpServer* pSender, CONNID dwConnID)
{
    return HR_OK;
}

EnHandleResult CServerListener::OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
    if (m_onReceive) {
        m_onReceive(dwConnID, pData, iLength);
    }
    
    return HR_OK;
}

EnHandleResult CServerListener::OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
    return HR_OK;
}

EnHandleResult CServerListener::OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
{
    if (m_onClose) {
        m_onClose(dwConnID);
    }
    
    return HR_OK;
}

EnHandleResult CServerListener::OnShutdown(ITcpServer* pSender)
{
    return HR_OK;
}

// ==================== NetworkServer 实现 ====================

NetworkServer::NetworkServer()
    : m_pServer(&m_listener)
    , m_bStarted(false)
{
    if (m_pServer.IsValid()) {
        // 设置 Socket 参数
        m_pServer->SetDualStack(FALSE);           // 关闭双栈，优先使用 IPv4
        m_pServer->SetMaxConnectionCount(10000);  // 支持10000并发
        m_pServer->SetSocketBufferSize(8192);      // 缓冲区8KB
        m_pServer->SetKeepAliveTime(60000);        // 心跳60秒
        m_pServer->SetKeepAliveInterval(10000);    // 心跳间隔10秒
    }
}

NetworkServer::~NetworkServer()
{
    Stop();
}

bool NetworkServer::Start(const char* bindIP, USHORT port)
{
    if (m_bStarted) {
        return true;
    }

    if (!m_pServer.IsValid()) {
        return false;
    }

    // 将char*转换为wchar_t*
    wchar_t szBindIP[50] = { 0 };
    TCHAR* pszBindAddr = nullptr;

    if (bindIP && strcmp(bindIP, "0.0.0.0") != 0 && strlen(bindIP) > 0) {
        MultiByteToWideChar(CP_ACP, 0, bindIP, -1, szBindIP, 50);
        pszBindAddr = (TCHAR*)szBindIP;
    }
    
    if (!m_pServer->Start(pszBindAddr, port)) {
        // 启动失败，错误码已保存在 HPSocket 对象中
        return false;
    }

    m_bStarted = true;
    return true;
}

void NetworkServer::Stop()
{
    if (m_bStarted) {
        m_pServer->Stop();
        m_bStarted = false;
    }
}

bool NetworkServer::Send(CONNID dwConnID, const BYTE* pData, int iLength)
{
    return m_pServer->Send(dwConnID, pData, iLength);
}

bool NetworkServer::Disconnect(CONNID dwConnID)
{
    return m_pServer->Disconnect(dwConnID);
}

bool NetworkServer::GetClientAddress(CONNID dwConnID, char* lpszAddress, int& iAddressLen, USHORT& usPort)
{
    wchar_t szAddress[50] = { 0 };
    int iTempLen = 50;
    bool result = m_pServer->GetRemoteAddress(dwConnID, (TCHAR*)szAddress, iTempLen, usPort);
    
    if (result && lpszAddress) {
        WideCharToMultiByte(CP_ACP, 0, szAddress, -1, lpszAddress, iAddressLen, NULL, NULL);
    }
    
    return result;
}

} // namespace Formidable
