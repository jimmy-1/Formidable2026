#ifndef FORMIDABLE_CLIENT_CORE_H
#define FORMIDABLE_CLIENT_CORE_H

#include "Config.h"
#include "Module.h"
#include "MemoryModule.h"
#include <atomic>
#include <map>

namespace Formidable {

    // 共享全局变量 (由 ClientCore 维护)
    extern CONNECT_ADDRESS g_ServerConfig;
    extern bool g_IsService;
    extern HMEMORYMODULE g_hTerminalModule;
    extern PFN_MODULE_ENTRY g_pTerminalEntry;
    extern HMEMORYMODULE g_hMultimediaModule;
    extern PFN_MODULE_ENTRY g_pMultimediaEntry;
    extern std::map<uint32_t, HMEMORYMODULE> g_ModuleCache;
    extern std::map<uint32_t, PFN_MODULE_ENTRY> g_ModuleEntryCache;

    extern CRITICAL_SECTION g_ModuleMutex;
    extern CRITICAL_SECTION g_TerminalMutex;
    extern CRITICAL_SECTION g_MultimediaMutex;

    // 连接上下文
    struct ConnectionContext {
        SOCKET s;
        std::string serverIp;
        std::string serverPort;
        std::atomic<bool>* bConnected;
        std::atomic<bool>* bShouldExit;
    };

    // 核心功能
    void InitClientCore();
    void CleanupClientCore();
    void GetClientInfo(ClientInfo& info);
    void SendPkg(SOCKET s, const void* data, int len);
    void HandleCommand(SOCKET s, CommandPkg* pkg, int totalDataLen);
    void HandleConnection(ConnectionContext* ctx);
    void LoadModuleFromMemory(SOCKET s, CommandPkg* pkg, int totalDataLen);
    uint32_t GetModuleKey(uint32_t cmd);
    
    // 新增：提取到 Common 的逻辑
    void RunClientLoop(std::atomic<bool>& bShouldExit, std::atomic<bool>& bConnected);
    void InstallClient();
    void ProcessPayload();

}

#endif // FORMIDABLE_CLIENT_CORE_H
