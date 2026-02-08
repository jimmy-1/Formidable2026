# Formidable2026 开发指南

## 1. 开发环境准备

在开始贡献代码之前，请确保您的开发环境满足以下要求：

*   **操作系统**: Windows 10 或 Windows 11 (x64)
*   **IDE**: Visual Studio 2026 (或 VS2022/2019 支持 C++17)
    *   安装工作负载: "使用 C++ 的桌面开发"
    *   组件: MSVC v143+, Windows 10/11 SDK, MFC/ATL (可选，部分模块可能需要)
*   **Git**: 用于版本控制
*   **第三方库**:
    *   **HPSocket**: 已包含在 `thirdparty/Include` 和 `thirdparty/Lib` 中，无需额外安装。
    *   **FFmpeg**: 用于音视频处理 (如有需要，需自行配置 include/lib 路径)。

---

## 2. 编译指南

本项目包含多个相互依赖的子项目，请严格按照以下顺序进行编译，以确保依赖关系正确解析。

### 2.1 编译顺序

1.  **Common (公共库)**
    *   **路径**: `e:\github\Formidable2026\Common\Common.vcxproj`
    *   **作用**: 生成 `Common.lib`，包含通信协议、核心逻辑和工具函数。
    *   **注意**: 必须优先编译，否则其他项目会报链接错误。

2.  **Modules (功能模块)**
    *   **路径**: `e:\github\Formidable2026\Modules\*\*.vcxproj`
    *   **项目**: `FileManager`, `ProcessManager`, `SystemInfo`, `Desktop`, `Terminal` 等。
    *   **作用**: 生成各功能的 DLL 文件。主控端需要这些 DLL 来分发给被控端。

3.  **ClientSide (被控端)**
    *   **路径**: 
        *   EXE版本: `e:\github\Formidable2026\ClientSide\Client\Client.vcxproj`
        *   DLL版本: `e:\github\Formidable2026\ClientSide\ClientDLL\ClientDLL.vcxproj`
    *   **依赖**: 依赖 `Common.lib`。
    *   **输出**: 生成最终的被控端程序。

4.  **Master (主控端)**
    *   **路径**: `e:\github\Formidable2026\Master\Master.vcxproj`
    *   **作用**: 生成控制台程序。
    *   **注意**: 确保所有 Module DLL 放置在 Master 可访问的目录下（通常是 `bin/Release/Modules`）。

### 2.2 编译配置 (Configuration)

*   **Debug**: 包含调试符号，无优化。用于开发调试。
*   **Release**: 开启 O2 优化，去除调试符号。用于发布。
*   **平台**:
    *   **x86 (Win32)**: 兼容性最好，支持 Win7+ 32/64位系统。
    *   **x64**: 性能更好，仅支持 64 位系统。
    *   **注意**: Client 和 Master 必须使用相同的协议定义，但可以是不同的架构（如 Master x64 控制 Client x86）。

---

## 3. 代码规范

为了保持代码库的整洁和可维护性，请遵守以下规则：

*   **文件编码**: **必须使用 UTF-8 with BOM**。
    *   VS 设置: `文件` -> `高级保存选项` -> `编码: Unicode (UTF-8 带签名) - 代码页 65001`。
    *   这是为了防止 MSVC 在处理中文字符串时出现乱码或编译错误。
*   **命名约定**:
    *   类名: `PascalCase` (如 `NetworkClient`)
    *   函数名: `PascalCase` (如 `ConnectServer`)
    *   变量名: `camelCase` (如 `packetSize`)
    *   成员变量: `m_` 前缀 (如 `m_socket`)
    *   常量/宏: `UPPER_CASE` (如 `MAX_BUFFER_SIZE`)
*   **注释**: 关键逻辑必须添加中文注释。

---

## 4. 调试技巧

### 4.1 本地回环测试
1.  启动 `Master.exe` (Debug/x64)。
2.  确保监听端口（默认 8080）未被占用。
3.  启动 `Client.exe` (Debug/x64)。
4.  Client 会自动连接 `127.0.0.1:8080`。
5.  在 Master 界面查看上线机器。

### 4.2 模块调试
由于模块是动态加载的 DLL，直接调试比较困难。建议：
1.  **日志调试**: 在模块中使用 `OutputDebugString` 或写日志文件。
2.  **附加进程**: 在 Client 加载模块后，使用 VS "附加到进程" 功能。
3.  **独立测试**: 编写一个简单的 TestLoader，静态链接模块代码进行逻辑测试，确认无误后再封装为 DLL。

### 4.3 常见编译错误
*   **LNK2019/LNK2001**: 通常是因为 `Common.lib` 未重新编译或路径配置错误。请重新生成 `Common` 项目。
*   **C4819 (编码警告)**: 文件未保存为 UTF-8 with BOM。请按“代码规范”中的说明转换文件编码。

---

## 5. 项目结构维护

*   新增源文件时，请务必更新 `.vcxproj` 和 `.filters` 文件。
*   如果修改了 `Common/Config.h` 中的协议结构，**必须**同时重新编译 Master 和 Client，否则会导致通信错乱。
*   保持 `README.md` 和 `ARCHITECTURE.md` 与代码实现同步。

