# Formidable2026 开发指南

## 1. 开发环境准备

在开始贡献代码之前，请确保您的开发环境满足以下要求：

*   **操作系统**: Windows 10/11 (x64)
*   **IDE**: Visual Studio 2022 (推荐) 或 2019
    *   **工作负载**: "使用 C++ 的桌面开发"
    *   **关键组件**: MSVC v145 (VS2017) 或 v143 (VS2022), Windows 10/11 SDK, MFC/ATL。
        *   注意：FormidableNim 和 FormidableLoader 项目默认使用 **v145** 工具集，请确保已安装相应组件。
*   **Git**: 用于版本管理。
*   **Python 3**: (可选) 用于运行辅助脚本（如编码转换、DLL 部署）。
*   **Nim 编译器**: 用于编译 `FormidableNim` 模块。
    *   项目内置路径: `tools/nim-2.2.0/bin`
    *   **重要**: 必须将上述 `bin` 目录添加到系统环境变量 `Path` 中，否则编译 `FormidableNim` 时会报 `MSB3073` 错误。

---

## 2. 编译指南

项目采用多项目解决方案，项目间存在严格的依赖关系。

### 2.1 编译步骤

1.  **Common (核心静态库)**
    *   **作用**: 生成 `Common.lib`。它是所有其他项目的基础，包含协议定义、加密算法及内存加载引擎。
    *   **路径**: `Common/Common.vcxproj`
2.  **Modules (功能 DLL)**
    *   **作用**: 生成各功能的 DLL 插件。
    *   **注意**: 必须在 `Common` 编译完成后进行。请逐一或批量编译 `Modules/` 目录下的所有子项目。
3.  **ClientSide (被控端)**
    *   **FormidableNim (推荐)**:
        *   **路径**: `ClientSide/FormidableNim/FormidableNim.vcxproj`
        *   **作用**: 生成 `FormidableNim.exe`。这是新一代被控端，具备更好的免杀能力。
        *   **注意**: **必须**在编译 `Master` 之前编译此项目。主控端在编译时会将生成的 `FormidableNim.exe` 作为资源嵌入。
    *   **Client (Legacy)**:
        *   **路径**: `ClientSide/Client/Client.vcxproj`
        *   **作用**: 生成 `Client.exe` (C++ 版本)。
4.  **Master (管理端 GUI)**
    *   **作用**: 生成 `Master.exe`。
    *   **资源依赖**: 编译时会自动寻找并嵌入 `FormidableNim.exe`。请确保第 3 步已完成。
    *   **部署**: 编译完成后，建议运行根目录下的 `deploy_dlls.bat`，将生成的模块 DLL 自动拷贝到主控端可调用的目录下。

### 2.2 平台配置 (Configuration)
*   **x86 (Win32)**: 建议用于被控端，以获得最佳的系统兼容性（支持 32/64 位系统）。
*   **x64**: 建议用于管理端，以获得更好的处理性能。

---

## 3. 编码规范

**为了防止中文字符在不同环境下的乱码问题，本项目强制执行以下规范：**

*   **文件编码**: 必须使用 **UTF-8 with BOM (带签名)**。
*   **VS 设置**: 
    *   建议安装插件 `Force UTF-8 with BOM`。
    *   或通过 `文件 -> 高级保存选项` 手动设置。
*   **注释**: 核心逻辑及协议定义部分必须包含清晰的中文注释。
*   **命名**: 遵循 PascalCase (类、函数) 和 camelCase (变量)。

---

## 4. 调试与测试

### 4.1 本地回环测试
1.  启动 `Master.exe`，默认监听端口为 8080。
2.  启动 `Client.exe`，它将尝试连接 `127.0.0.1:8080`。
3.  在 Master 界面右键点击上线机器，测试各项功能（如文件管理、终端）。

### 4.2 模块调试
由于模块是在内存中动态加载的，常规断点可能无效：
*   **日志调试**: 使用 `OutputDebugStringA` 输出调试信息，配合 `DbgView` 查看。
*   **附加调试**: 在 Client 运行期间，通过 VS 的 `调试 -> 附加到进程` 连接到 Client。

---

## 5. 维护说明

*   **协议更改**: 如果修改了 `Common/Config.h` 或 `ClientTypes.h` 中的结构体，**必须**重新编译整个解决方案，否则会导致通信崩溃或数据错乱。
*   **新增模块**: 参考现有模块结构，实现导出接口函数，并在 Master 的 `CommandHandler.cpp` 中添加对应的命令映射。
*   **资源更新**: 主控端的图标及 UI 资源位于 `Master/res/`，更新后需重新编译 `Master` 项目。
