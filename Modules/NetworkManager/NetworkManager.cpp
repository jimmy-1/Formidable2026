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
#include <map>
#include <tlhelp32.h>
#include "../../Common/Config.h"
#include "../../Common/Utils.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace Formidable;

struct ProcInfo {
    std::string name;
    std::string folder;
    std::string dlls;
};

std::string GetProcessNameByPid(uint32_t pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return "";
    PROCESSENTRY32W pe32 = { sizeof(pe32) };
    if (Process32FirstW(snapshot, &pe32)) {
        do {
            if (pe32.th32ProcessID == pid) {
                std::string name = WideToUTF8(pe32.szExeFile);
                CloseHandle(snapshot);
                return name;
            }
        } while (Process32NextW(snapshot, &pe32));
    }
    CloseHandle(snapshot);
    return "";
}

std::string GetProcessPathByPid(uint32_t pid) {
    std::string path;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        wchar_t szPath[MAX_PATH] = { 0 };
        DWORD dwSize = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, szPath, &dwSize)) {
            path = WideToUTF8(szPath);
        }
        CloseHandle(hProcess);
    }
    return path;
}

std::string GetProcessFolderFromPath(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return path;
    return path.substr(0, pos);
}

std::string GetProcessModulesByPid(uint32_t pid, size_t maxCount) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return "";
    MODULEENTRY32W me32 = { sizeof(me32) };
    std::vector<std::string> names;
    bool truncated = false;
    if (Module32FirstW(snapshot, &me32)) {
        do {
            names.push_back(WideToUTF8(me32.szModule));
            if (names.size() >= maxCount) {
                truncated = true;
                break;
            }
        } while (Module32NextW(snapshot, &me32));
    }
    CloseHandle(snapshot);
    if (names.empty()) return "";
    std::string result;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) result.append(",");
        result.append(names[i]);
    }
    if (truncated) {
        result.append(",...");
    }
    return result;
}

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
    std::map<uint32_t, ProcInfo> procCache;
    
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
                
                uint32_t pid = pTcpTable->table[i].dwOwningPid;
                auto it = procCache.find(pid);
                if (it == procCache.end()) {
                    ProcInfo info;
                    info.name = GetProcessNameByPid(pid);
                    std::string fullPath = GetProcessPathByPid(pid);
                    info.folder = GetProcessFolderFromPath(fullPath);
                    info.dlls = GetProcessModulesByPid(pid, 30);
                    if (info.name.empty()) info.name = "Unknown";
                    if (info.folder.empty()) info.folder = "-";
                    if (info.dlls.empty()) info.dlls = "-";
                    it = procCache.emplace(pid, info).first;
                }

                ss << "TCP|" << localIp << "|" << ntohs((u_short)pTcpTable->table[i].dwLocalPort) << "|"
                   << remoteIp << "|" << ntohs((u_short)pTcpTable->table[i].dwRemotePort) << "|"
                   << state << "|" << pid << "|" << it->second.name << "|" << it->second.folder << "|" << it->second.dlls << "\n";
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
                
                uint32_t pid = pUdpTable->table[i].dwOwningPid;
                auto it = procCache.find(pid);
                if (it == procCache.end()) {
                    ProcInfo info;
                    info.name = GetProcessNameByPid(pid);
                    std::string fullPath = GetProcessPathByPid(pid);
                    info.folder = GetProcessFolderFromPath(fullPath);
                    info.dlls = GetProcessModulesByPid(pid, 30);
                    if (info.name.empty()) info.name = "Unknown";
                    if (info.folder.empty()) info.folder = "-";
                    if (info.dlls.empty()) info.dlls = "-";
                    it = procCache.emplace(pid, info).first;
                }

                ss << "UDP|" << localIp << "|" << ntohs((u_short)pUdpTable->table[i].dwLocalPort) << "|"
                   << "*|*|" // Remote IP/Port unknown for UDP listener
                   << "N/A|" << pid << "|" << it->second.name << "|" << it->second.folder << "|" << it->second.dlls << "\n";
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
