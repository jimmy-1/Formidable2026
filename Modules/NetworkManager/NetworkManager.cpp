/**
 * Formidable2026 - NetworkManager Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <sstream>
#include <vector>
#include <iomanip>
#include "../../Common/Config.h"
#include "../../Common/Module.h"
#include "../../Common/Utils.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

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
    
    send(s, buffer.data(), (int)buffer.size(), 0);
}

std::string ListConnections() {
    std::stringstream ss;
    
    // TCP
    PMIB_TCPTABLE_OWNER_PID pTcpTable;
    DWORD dwSize = 0;
    
    if (GetExtendedTcpTable(NULL, &dwSize, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER) {
        pTcpTable = (PMIB_TCPTABLE_OWNER_PID)malloc(dwSize);
        if (GetExtendedTcpTable(pTcpTable, &dwSize, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++) {
                char localIp[32], remoteIp[32];
                
                in_addr addr;
                addr.S_un.S_addr = pTcpTable->table[i].dwLocalAddr;
                inet_ntop(AF_INET, &addr, localIp, 32);
                
                addr.S_un.S_addr = pTcpTable->table[i].dwRemoteAddr;
                inet_ntop(AF_INET, &addr, remoteIp, 32);
                
                const char* state = "Unknown";
                switch (pTcpTable->table[i].dwState) {
                    case MIB_TCP_STATE_CLOSED: state = "CLOSED"; break;
                    case MIB_TCP_STATE_LISTEN: state = "LISTEN"; break;
                    case MIB_TCP_STATE_SYN_SENT: state = "SYN_SENT"; break;
                    case MIB_TCP_STATE_SYN_RCVD: state = "SYN_RCVD"; break;
                    case MIB_TCP_STATE_ESTAB: state = "ESTABLISHED"; break;
                    case MIB_TCP_STATE_FIN_WAIT1: state = "FIN_WAIT1"; break;
                    case MIB_TCP_STATE_FIN_WAIT2: state = "FIN_WAIT2"; break;
                    case MIB_TCP_STATE_CLOSE_WAIT: state = "CLOSE_WAIT"; break;
                    case MIB_TCP_STATE_CLOSING: state = "CLOSING"; break;
                    case MIB_TCP_STATE_LAST_ACK: state = "LAST_ACK"; break;
                    case MIB_TCP_STATE_TIME_WAIT: state = "TIME_WAIT"; break;
                    case MIB_TCP_STATE_DELETE_TCB: state = "DELETE_TCB"; break;
                }
                
                ss << "TCP|" << localIp << "|" << ntohs((u_short)pTcpTable->table[i].dwLocalPort) << "|"
                   << remoteIp << "|" << ntohs((u_short)pTcpTable->table[i].dwRemotePort) << "|"
                   << state << "|" << pTcpTable->table[i].dwOwningPid << "\n";
            }
        }
        free(pTcpTable);
    }
    
    // UDP
    PMIB_UDPTABLE_OWNER_PID pUdpTable;
    dwSize = 0;
    
    if (GetExtendedUdpTable(NULL, &dwSize, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == ERROR_INSUFFICIENT_BUFFER) {
        pUdpTable = (PMIB_UDPTABLE_OWNER_PID)malloc(dwSize);
        if (GetExtendedUdpTable(pUdpTable, &dwSize, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            for (DWORD i = 0; i < pUdpTable->dwNumEntries; i++) {
                char localIp[32];
                in_addr addr;
                addr.S_un.S_addr = pUdpTable->table[i].dwLocalAddr;
                inet_ntop(AF_INET, &addr, localIp, 32);
                
                ss << "UDP|" << localIp << "|" << ntohs((u_short)pUdpTable->table[i].dwLocalPort) << "|"
                   << "*|*|" // Remote IP/Port unknown for UDP listener
                   << "N/A|" << pUdpTable->table[i].dwOwningPid << "\n";
            }
        }
        free(pUdpTable);
    }
    
    return ss.str();
}

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_NETWORK_LIST) {
        std::string result = ListConnections();
        SendResponse(s, CMD_NETWORK_LIST, result.c_str(), (int)result.size());
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}