# Formidable2026 系统架构文档

## 1. 系统概览

Formidable2026 是一个基于 Windows 平台的高性能远程管理系统（RAT），采用 C++17 标准开发。项目设计注重模块化、隐蔽性和高性能，支持多种加载方式（EXE, DLL, ShellCode, PowerShell）和丰富的功能模块。

### 核心设计理念
*   **模块化**: 核心功能与扩展功能分离，支持动态加载/卸载 DLL 模块。
*   **高性能**: 基于 IOCP 模型（HPSocket）的高并发通信，支持数千客户端同时在线。
*   **隐蔽性**: 支持内存加载模块（Reflective DLL Injection），避免文件落地。
*   **多平台兼容**: 核心代码同时支持 x86 和 x64 架构。

---

## 2. 核心架构

系统采用经典的 Client-Server (C/S) 架构。

```mermaid
graph TD
    User[操作员] --> Master[主控端 (Master.exe)]
    Master -- TCP/IOCP --> Internet[互联网/局域网]
    Internet --> Client[被控端 (Client)]
    
    subgraph ClientSide [被控端架构]
        Loader[启动器 (EXE/DLL/ShellCode)] --> Core[核心引擎 (ClientCore)]
        Core --> Network[网络模块]
        Core --> TaskMgr[任务调度]
        Core --> ModMgr[模块管理器]
        
        ModMgr -.-> Mod1[文件管理模块]
        ModMgr -.-> Mod2[进程管理模块]
        ModMgr -.-> Mod3[桌面监控模块]
        ModMgr -.-> Mod4[终端管理模块]
        ModMgr -.-> Mod5[其他功能模块...]
    end
```

### 2.1 目录结构

```text
E:\GITHUB\FORMIDABLE2026
├─ClientSide        # [被控端] 组件集合
│  ├─Client         # 标准 EXE 启动器工程
│  ├─ClientDLL      # DLL 版本启动器工程 (用于劫持/侧加载)
│  └─Loaders        # 其他加载器 (ShellCode, PowerShell)
├─Common            # [公共库] 核心逻辑与定义
│  ├─ClientCore.cpp # 被控端核心逻辑实现 (所有Loader共用)
│  ├─Config.h       # 全局配置与命令枚举
│  ├─ClientTypes.h  # 数据结构定义
│  └─...            # 网络库、加密库等
├─Master            # [主控端] GUI 管理控制台
├─Modules           # [功能模块] 独立 DLL 项目
│  ├─FileManager    # 文件管理
│  ├─ProcessManager # 进程管理
│  ├─SystemInfo     # 系统信息
│  └─...            # 其他模块
└─thirdparty        # [第三方库] (HPSocket, etc.)
```

---

## 3. 通信协议

通信层基于 TCP 协议，采用自定义的二进制封包格式，支持粘包处理和数据加密。

### 3.1 封包结构 (Packet Structure)

每个数据包由定长包头和变长包体组成。

**包头 (PkgHeader) - 16 Bytes**
```cpp
struct PkgHeader {
    char flag[8];       // 魔法标识 "FRMD26?"
    int32_t totalLen;   // 包总长度 (Header + Body)
    int32_t originLen;  // 原始数据长度 (解压/解密后)
};
```

**命令包体 (CommandPkg)**
```cpp
struct CommandPkg {
    uint32_t cmd;       // 命令ID (CommandType)
    uint32_t arg1;      // 参数1 (上下文相关)
    uint32_t arg2;      // 参数2 (上下文相关)
    char data[1];       // 变长数据负载 (Payload)
};
```

### 3.2 关键命令定义 (CommandType)

| 命令ID范围 | 功能分类 | 示例命令 |
| :--- | :--- | :--- |
| 1 | 心跳 | CMD_HEARTBEAT |
| 10-19 | 系统信息 | CMD_GET_SYSINFO |
| 20-39 | 文件管理 | CMD_FILE_LIST, CMD_FILE_DOWNLOAD |
| 40-49 | 进程管理 | CMD_PROCESS_LIST, CMD_PROCESS_KILL |
| 50-59 | 桌面/窗口 | CMD_SCREEN_CAPTURE, CMD_WINDOW_CTRL |
| 60-69 | 终端Shell | CMD_SHELL_EXEC, CMD_TERMINAL_DATA |
| 70-79 | 音视频 | CMD_AUDIO_START, CMD_VIDEO_STREAM |
| 80-89 | 系统控制 | CMD_SERVICE_LIST, CMD_POWER_SHUTDOWN |
| 100+ | 核心控制 | CMD_LOAD_MODULE (加载DLL), CMD_UPDATE_CLIENT |

### 3.3 上线配置 (CONNECT_ADDRESS)

客户端内置的配置结构体，通常在生成客户端时通过 Patch 技术写入文件末尾或特定段。

```cpp
struct CONNECT_ADDRESS {
    char szFlag[32];      // 标识
    char szServerIP[100]; // C2 服务器 IP/域名
    char szPort[8];       // 端口
    int32_t bEncrypt;     // 通信是否加密
    char szGroupName[24]; // 分组名称
    char runasAdmin;      // 是否尝试提权
    char szInstallDir[MAX_PATH]; // 安装路径
    // ... 其他配置项 (详见 Config.h)
};
```

---

## 4. 模块化系统 (Module System)

Formidable2026 采用强大的动态模块加载机制，旨在减小核心体积并提高扩展性。

### 4.1 模块接口 (IModule)

所有功能模块必须实现 `IModule` 接口：

```cpp
class IModule {
public:
    virtual ~IModule() {}
    virtual std::string GetModuleName() = 0;
    virtual void Execute(SOCKET s, CommandPkg* pkg) = 0;
};
```

### 4.2 加载机制

1.  **按需加载**: 客户端初始运行时不加载任何功能模块。
2.  **远程分发**: 当主控端发起功能请求（如“文件管理”）时，服务器将对应的 DLL 文件（如 `FileManager.dll`）发送给客户端。
3.  **内存加载**: 客户端接收 DLL 数据后，使用 `MemoryModule` 技术直接在内存中加载 DLL，**不释放文件到硬盘**，有效规避杀软扫描。
4.  **执行与卸载**: 模块执行完毕后可选择驻留内存或立即卸载以释放资源。

---

## 5. 核心组件详解

### 5.1 ClientSide (被控端)
*   **ClientCore**: 实现了网络连接维护、心跳包发送、指令分发逻辑。它是所有 Loader 的静态链接库。
*   **Loaders**:
    *   `Client (EXE)`: 标准 Windows 可执行文件，支持自启动安装。
    *   `ClientDLL`: 导出特定函数的 DLL，可用于 DLL 侧加载 (Side-Loading) 攻击。
    *   `ShellCode`: 提取核心汇编代码，可注入到其他进程运行。

### 5.2 Master (主控端)
*   **GUI**: 使用原生 Win32 API 构建的高效图形界面。
*   **Client Manager**: 维护 `ConnectedClient` 列表，管理每个客户端的状态、窗口句柄和数据缓冲。
*   **IOCP Server**: 基于 HPSocket 的高性能异步 TCP 服务器，处理成千上万的并发连接。

### 5.3 Modules (功能模块)
*   **FileManager**: 遍历目录、上传/下载文件（支持断点续传）、文件操作。
*   **ProcessManager**: 遍历进程（ToolHelp32Snapshot）、结束进程、查看模块。
*   **Desktop**: GDI/DXGI 截屏，JPEG 压缩传输，支持鼠标键盘控制。
*   **Terminal**: 创建匿名管道 (Anonymous Pipe) 对接 cmd.exe 或 powershell.exe，实现交互式 Shell。

---

## 6. 安全与防御

*   **通信加密**: 支持 AES 加密传输数据，防止流量分析。
*   **无文件落地**: 功能模块全内存加载。
*   **反调试/反虚拟机**: (计划中/部分实现) 检测调试器和虚拟化环境。
*   **多态变异**: 支持生成不同哈希的客户端文件。

