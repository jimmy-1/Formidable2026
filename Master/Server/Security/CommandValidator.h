#pragma once
#include <set>
#include <mutex>
#include <vector>
#include <string>
#include <map>
#include "../../../Common/Config.h"

namespace Formidable {
namespace Server {
namespace Security {

class CommandValidator {
public:
    static void InitializeCommands();
    static bool IsValidCommand(int commandId);
    static bool IsValidPacketSize(int commandId, int packetSize);
    static void SetMaxPacketSize(int size);

    // New methods for Phase 3
    static bool ValidateCommand(uint32_t cmd);
    static bool ValidateArguments(const CommandPkg* pkg, size_t dataSize);
    static bool CheckCommandPermissions(uint32_t clientId, uint32_t cmd);

    class CommandFilter {
    public:
        static bool FilterCommand(uint32_t cmd, const std::string& data);
        static bool IsMaliciousCommand(uint32_t cmd);
        static std::vector<std::string> GetAllowedCommands();
    };

    class PermissionChecker {
    public:
        static bool HasPermission(uint32_t clientId, uint32_t cmd);
        static void SetCommandPermissions(uint32_t clientId, const std::vector<uint32_t>& allowedCommands);
    private:
        static std::map<uint32_t, std::set<uint32_t>> s_clientPermissions;
        static std::mutex s_permMutex;
    };

private:
    static std::set<int> s_validCommands;
    static int s_maxPacketSize;
    static std::mutex s_mutex;
    static bool s_initialized;
};

} // namespace Security
} // namespace Server
} // namespace Formidable
