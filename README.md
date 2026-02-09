# Formidable2026

![Platform](https://img.shields.io/badge/Platform-Windows-blue.svg)
![Language](https://img.shields.io/badge/Language-C%2B%2B17-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

**Formidable2026** 是一个下一代 Windows 远程管理工具（RAT），专为红队行动和安全研究设计。它具备高度模块化、隐蔽性强、通信稳定等特点，支持从 Windows 7 到 Windows 11 的全系列操作系统。

> **⚠️ 免责声明**: 本项目仅供网络安全研究和教育使用。严禁用于非法用途。开发者不会对任何因使用本软件造成的损失承担责任。

---

## ✨ 功能特性

### 核心功能
*   **多启动模式**: 支持标准 EXE 启动，并集成多种自启动持久化技术。
*   **高性能通信**: 基于 IOCP (HPSocket) 模型，支持 TCP/UDP 多协议切换，稳定支持数千台设备并发在线。
*   **完全无文件落地**: 核心功能模块采用反射式 DLL 注入（Reflective DLL Injection）技术，全内存执行。
*   **通信加密**: 支持 AES/RSA 加密传输，抗流量分析；上线配置信息支持 XOR 混淆加密。
*   **灵活上线策略**: 支持随机上线（打乱服务器列表）与并发上线模式。

### 管理模块
*   **💻 远程桌面**: 基于 GDI/DXGI 的高性能屏幕监控，支持画质调节、实时鼠标键盘控制。
*   **📂 文件管理**: 资源管理器级体验，支持断点续传、文件夹递归下载、文件搜索与预览。
*   **🚀 进程管理**: 实时进程列表、结束进程、模块查看、进程挂起/恢复。
*   **⌨️ 交互式终端**: 完美对接 CMD/PowerShell，支持 ANSI 颜色输出与交互。
*   **🔧 系统管理**: 服务管理、注册表编辑、窗口控制（最大化/最小化/置顶等）。
*   **🔊 音视频监控**: 远程麦克风录音、摄像头实时监控（基于 FFmpeg 优化）。
*   **🛡️ 生成器 (Builder)**: 集成配置同步、UPX 自动化加壳、程序增肥（Pump Size）等隐蔽性增强技术。

---

## 🛠️ 项目结构

```text
Formidable2026/
├── ClientSide/         # [被控端] 启动器与安全增强
├── Common/             # [公共库] 协议定义、内存加载引擎 (MemoryModule)、工具类
├── Master/             # [主控端] Win32 原生 GUI 管理控制台
├── Modules/            # [功能模块] 独立编译的功能插件 DLL
├── thirdparty/         # 第三方依赖 (HPSocket, FFmpeg, LAME, UPX 等)
└── scripts/            # 部署与环境辅助脚本
```

---

## 🚀 快速开始

### 1. 环境准备
*   Visual Studio 2022 (推荐) 或 2019
*   Windows SDK 10.0+
*   Python 3 (可选，用于辅助脚本)

### 2. 编译项目
请严格按照以下顺序编译 (详细指南见 [DEVELOPMENT.md](DEVELOPMENT.md)):
1.  编译 `Common` (生成核心静态库)
2.  编译 `Modules` 下的所有项目 (生成功能 DLL)
3.  编译 `ClientSide/Client` (生成 Loader)
4.  编译 `Master` (生成管理端)

### 3. 生成与运行
1.  启动 `Master.exe`。
2.  进入“生成器”配置监听地址、上线模式、加壳选项等。
3.  生成被控端并在目标机器执行。

---

## 📚 文档资源

*   **[ARCHITECTURE.md](ARCHITECTURE.md)**: 详细的系统架构设计、通信协议封包说明、模块化加载原理。
*   **[DEVELOPMENT.md](DEVELOPMENT.md)**: 开发环境搭建、详细编译步骤、编码规范与调试技巧。

---

## 🤝 贡献
欢迎提交 PR 或 Issue！提交代码前请确保文件编码为 **UTF-8 with BOM**。

## 📄 许可证
MIT License
