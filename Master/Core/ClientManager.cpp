#include "ClientManager.h"
#include "../../Common/ClientTypes.h"
#include "../GlobalState.h"
#include <map>
#include <string>
#include <vector>

// External globals from GlobalState.cpp
extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;

namespace Formidable {
namespace Core {

std::shared_ptr<Formidable::ConnectedClient> ClientManager::CreateClient(CONNID connId, const std::string& ip, uint16_t port) {
    auto client = std::make_shared<ConnectedClient>();
    client->connId = connId;
    client->ip = ip;
    client->port = port;
    client->active = true;
    client->lastHeartbeat = GetTickCount64();
    return client;
}

void ClientManager::UpdateClientInfo(std::shared_ptr<Formidable::ConnectedClient> client, const Formidable::ClientInfo& info) {
    if (!client) return;
    
    memcpy(&client->info, &info, sizeof(Formidable::ClientInfo));
}

std::shared_ptr<Formidable::ConnectedClient> ClientManager::GetClientByConnId(CONNID connId) {
    std::lock_guard<std::mutex> lock(g_ClientsMutex);
    for (auto& pair : g_Clients) {
        if (pair.second->connId == connId) {
            return pair.second;
        }
    }
    return nullptr;
}

void ClientManager::RemoveClient(uint32_t clientId) {
    std::lock_guard<std::mutex> lock(g_ClientsMutex);
    auto it = g_Clients.find(clientId);
    if (it != g_Clients.end()) {
        g_Clients.erase(it);
    }
}

void ClientManager::RemoveInactiveClients() {
    std::lock_guard<std::mutex> lock(g_ClientsMutex);
    std::vector<uint32_t> toRemove;
    for (const auto& pair : g_Clients) {
        if (!pair.second->active) {
            toRemove.push_back(pair.first);
        }
    }
    for (uint32_t id : toRemove) {
        g_Clients.erase(id);
    }
}

} // namespace Core
} // namespace Formidable
