// CommandHandler.h - 命令处理模块
#pragma once
#include "../Common/ClientTypes.h"
#include <cstdint>
#include <Windows.h>

class CommandHandler {
public:
    // 处理接收到的完整数据包
    static void HandlePacket(uint32_t clientId, const BYTE* pData, int iLength);
    
    // 具体命令处理函数
    static void HandleHeartbeat(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleClientInfo(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleProcessList(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleModuleList(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleServiceList(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleWindowList(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleKeylog(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleTerminalData(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleSysInfo(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleRegistryData(uint32_t clientId, const BYTE* pData, int iLength);
    static void HandleFileList(uint32_t clientId, const BYTE* pData, int iLength);
};
