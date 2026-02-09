# Formidable2026 系统架构文档

## 1. 系统概览

Formidable2026 是一个基于 Windows 平台的高性能远程管理系统（RAT），采用 C++17 标准开发。项目设计注重模块化、隐蔽性和高性能，支持 EXE 加载方式和丰富的功能模块。

### 核心设计理念
*   **极致模块化**: 核心引擎仅负责网络维护与任务调度，所有具体功能均由 DLL 模块实现，按需加载。
*   **高并发架构**: 基于 HPSocket (IOCP) 的异步通信模型，单服务端可支撑万级并发连接。
*   **全内存隐蔽**: 采用内存加载模块（Reflective DLL Injection）技术，功能 DLL 传输后直接在内存中执行，不留文件痕迹。
*   **跨架构兼容**: 核心代码兼容 x86 和 x64，支持管理不同架构的被控端。

---

## 2. 核心架构

系统采用 Client-Server (C/S) 架构，通过自定义的二进制协议进行通信。

### 2.1 整体流程
```mermaid
graph TD
    User[操作员] --> Master[主控端 (Master.exe)]
    Master -- TCP/UDP (IOCP) --> Internet[互联网/局域网]
    Internet --> Client[被控端 (Client)]
    
    subgraph ClientSide [被控端架构]
        Loader[启动器 (EXE)] --> Core[核心引擎 (ClientCore)]
        Core --> Network[网络模块]
        Core --> TaskMgr[任务调度]
        Core --> ModMgr[内存模块管理器]
        
        ModMgr -.-> Mod1[文件管理模块]
        ModMgr -.-> Mod2[进程管理模块]
        ModMgr -.-> Mod3[桌面监控模块]
        ModMgr -.-> Mod4[交互式终端]
        ModMgr -.-> Mod5[其他功能模块...]
    end
```

### 2.2 目录结构详解
*   **ClientSide**: 包含被控端的加载器逻辑，负责自启动、环境检测与核心引擎初始化。
*   **Common**: 项目的基石，包含全局配置 `Config.h`、通信数据结构 `ClientTypes.h`、以及关键的 `MemoryModule` 内存加载引擎。
*   **Master**: Win32 原生 GUI 实现，负责客户端状态维护、指令下发、模块分发及各功能的 UI 呈现。
*   **Modules**: 独立的功能插件集，每个模块编译为 DLL，被主控端加载并分发。

---

## 3. 通信协议

协议设计兼顾了效率与安全性，支持粘包处理与分片传输。

### 3.1 封包结构 (Packet Structure)

**包头 (PkgHeader) - 16 Bytes**
```cpp
struct PkgHeader {
    char flag[8];       // 魔法标识 "FRMD26?"
    int32_t totalLen;   // 整个包的总长度 (Header + Body)
    int32_t originLen;  // 原始数据长度 (用于校验或解压)
};
```

**命令包体 (CommandPkg)**
```cpp
struct CommandPkg {
    uint32_t cmd;       // 命令 ID (枚举自 CommandType)
    uint32_t arg1;      // 通用参数 1
    uint32_t arg2;      // 通用参数 2
    char data[1];       // 变长负载数据 (Payload)
};
```

### 3.2 上线配置 (CONNECT_ADDRESS)
客户端配置信息硬编码或在生成时注入。关键字段包括：
*   `szServerIP`: C2 服务器地址（支持加密存储）。
*   `protoType`: 0 为 TCP，1 为 UDP。
*   `runningType`: 0 为随机上线，1 为并发上线。
*   `iPumpSize`: 用于程序增肥的填充大小。

---

## 4. 内存模块加载机制

这是本项目的核心安全特性。

1.  **分发**: 当用户在 Master 界面打开某个功能（如文件管理）时，Master 将对应的 `FileManager.dll` 读入内存。
2.  **传输**: DLL 数据经过加密后通过加密通道发送给 Client。
3.  **内存映射**: Client 接收到 DLL 字节流后，调用 `MemoryModule` 引擎。
4.  **执行**: 引擎在内存中完成 DLL 的重定位、导入表修复，并调用入口函数，无需将 DLL 写入硬盘。
5.  **交互**: DLL 运行后直接接管当前的 Socket 连接，进行后续的功能交互。

---

## 5. 安全增强特性

*   **流量加密**: 支持端到端加密，避免被 IDS/IPS 识别。
*   **静态增强**: 生成器集成了 UPX 自动化加壳与 Pump Size 程序增肥，有效对抗静态扫描。
*   **行为隐蔽**: 禁用了被控端的日志输出，优化了自启动注册表/服务的路径伪装。
*   **动态对抗**: (持续更新中) 包括反调试、反虚拟机检测以及进程注入等技术。
