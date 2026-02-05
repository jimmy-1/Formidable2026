#pragma once
#include "../../Common/ClientTypes.h"
#include "../../Common/Config.h"
#include <memory>
#include <vector>
#include <string>

// 客户端管理器 - 辅助工具类
namespace Formidable {
namespace Core {
class ClientManager {
public:
    // 创建新客户端
    static std::shared_ptr<Formidable::ConnectedClient> CreateClient(CONNID connId, const std::string& ip, uint16_t port);
    
    // 更新客户端信息
    static void UpdateClientInfo(std::shared_ptr<Formidable::ConnectedClient> client, const Formidable::ClientInfo& info);
    
    // 通过CONNID获取客户端
    static std::shared_ptr<Formidable::ConnectedClient> GetClientByConnId(CONNID connId);
    
    // 移除客户端
    static void RemoveClient(uint32_t clientId);
    
    // 清理不活跃客户端
    static void RemoveInactiveClients();
};
}
}
