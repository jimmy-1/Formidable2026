# Formidable2026

![Platform](https://img.shields.io/badge/Platform-Windows-blue.svg)
![Language](https://img.shields.io/badge/Language-C%2B%2B17-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

**Formidable2026** 是一个下一代 Windows 远程管理工具（RAT），专为红队行动和安全研究设计。它具备高度模块化、隐蔽性强、通信稳定等特点，支持从 Windows 7 到 Windows 11 的全系列操作系统。

> **⚠️ 免责声明**: 本项目仅供网络安全研究和教育使用。严禁用于非法用途。开发者不对任何因使用本软件造成的损失承担责任。

---

## ✨ 功能特性

### 核心功能
*   **多启动模式**: 支持 EXE、DLL、ShellCode、PowerShell 等多种加载方式。
*   **高并发通信**: 基于 IOCP (HPSocket) 模型，稳定支持数千台设备同时在线。
*   **无文件落地**: 核心功能模块采用反射式 DLL 注入（Reflective DLL Injection）技术，全内存执行。
*   **通信加密**: 支持 AES/RSA 加密传输，抗流量分析。

### 管理模块
*   **💻 远程桌面**: 高性能屏幕监控，支持画质调节、鼠标键盘控制。
*   **📂 文件管理**: 像资源管理器一样浏览远程文件，支持上传、下载、运行、删除、重命名。
*   **🚀 进程管理**: 查看进程列表、结束进程、查看进程模块信息。
*   **⌨️ 终端 Shell**: 交互式远程 CMD/PowerShell 命令行。
*   **🔧 系统管理**: 服务管理、注册表编辑、窗口管理。
*   **📊 系统信息**: 获取详细的软硬件信息（OS版本、CPU、内存、磁盘等）。
*   **🔊 音视频监控**: 远程开启麦克风录音、摄像头监控。
*   **📋 其他功能**: 剪贴板管理、键盘记录、聊天弹窗。

---

## 🛠️ 项目结构

```text
Formidable2026/
├── ClientSide/         # [被控端] 核心组件
│   ├── Client/         # 标准 EXE 启动器
│   ├── ClientDLL/      # DLL 劫持/侧加载启动器
│   └── Loaders/        # ShellCode/PowerShell 加载器
├── Common/             # [公共库] 协议定义、加密算法、工具类
├── Master/             # [主控端] 管理控制台 (GUI)
├── Modules/            # [功能模块] 动态加载的插件 (DLLs)
└── thirdparty/         # 第三方依赖 (HPSocket 等)
```

---

## 🚀 快速开始

### 1. 环境准备
*   Visual Studio 2026 (或 2019/2022)
*   Windows SDK 10.0+
*   Git

### 2. 编译项目
请严格按照以下顺序编译 (详细指南请参阅 [DEVELOPMENT.md](DEVELOPMENT.md)):
1.  编译 `Common` (生成 Common.lib)
2.  编译 `Modules` 下的所有项目 (生成功能 DLL)
3.  编译 `ClientSide/Client` (生成 Client.exe)
4.  编译 `Master` (生成 Master.exe)

### 3. 运行测试
1.  运行 `Master.exe`，默认监听 `0.0.0.0:8080`。
2.  在目标机器（或本机虚拟机）运行 `Client.exe`。
3.  Client 上线后，Master 列表会出现新设备。
4.  右键点击设备，选择相应功能进行操作。

---

## 📚 文档资源

*   **[ARCHITECTURE.md](ARCHITECTURE.md)**: 详细的系统架构设计、通信协议说明、模块化原理。
*   **[DEVELOPMENT.md](DEVELOPMENT.md)**: 开发环境搭建、编译步骤、代码规范与调试技巧。

---

## 🤝 贡献

欢迎提交 Pull Request 或 Issue！
在提交代码前，请确保遵循我们的代码规范（特别是 UTF-8 with BOM 编码要求）。

## 📄 许可证

MIT License
