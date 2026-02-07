# Formidable 2026 开发指南

## 目录
- [开发环境设置](#开发环境设置)
- [项目编译](#项目编译)
- [调试技巧](#调试技巧)
- [开发流程](#开发流程)
- [代码规范](#代码规范)
- [测试指南](#测试指南)
- [常见问题](#常见问题)
- [贡献指南](#贡献指南)

---

## 开发环境设置

### 系统要求
- **操作系统**: Windows 10/11 (x64)
- **开发工具**: Visual Studio 2022 (推荐) 或 Visual Studio 2019
- **必需组件**:
  - 使用 C++ 的桌面开发工作负载
  - Windows 10/11 SDK
  - CMake (可选，用于构建第三方库)

### 安装步骤

1. **安装 Visual Studio**
   ```
   下载 Visual Studio 2022 Community (免费)
   选择 "使用 C++ 的桌面开发" 工作负载
   包含: MSVC v143 - VS 2022 C++ x64/x86 生成工具
         Windows 10 SDK (或 Windows 11 SDK)
   ```

2. **克隆项目**
   ```bash
   git clone https://github.com/yourusername/Formidable2026.git
   cd Formidable2026
   ```

3. **配置项目**
   - 打开 `Formidable2026.sln`
   - 等待 Visual Studio 加载项目
   - 确保所有项目引用正确

4. **下载依赖项**
   - HPSocket 库已包含在 `thirdparty/` 目录
   - FFmpeg 库已包含在 `thirdparty/Include/ffmpeg/` 目录
   - 其他第三方库按需配置

---

## 项目编译

### 编译顺序

由于项目存在依赖关系，必须按以下顺序编译：

```
1. Common (公共库)
   ↓
2. Modules (功能模块)
   ├── Terminal
   ├── FileManager
   ├── ProcessManager
   ├── WindowManager
   ├── RegistryManager
   ├── ServiceManager
   ├── Multimedia
   └── SystemInfo
   ↓
3. Client (被控端)
   ↓
4. Master (主控端)
```

### 快速编译

#### 使用 Visual Studio
```
1. 打开 Formidable2026.sln
2. 选择配置: Release | x64 (或 Win32)
3. 右键点击解决方案 → "生成解决方案"
```

#### 使用命令行
```bash
# 设置 Visual Studio 环境变量
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

# 编译 Release 版本
msbuild Formidable2026.sln /p:Configuration=Release /p:Platform=x64 /m

# 编译 Debug 版本
msbuild Formidable2026.sln /p:Configuration=Debug /p:Platform=x64 /m
```

### 编译配置说明

| 配置 | 平台 | 输出目录 | 用途 |
|------|------|---------|------|
| Debug | Win32 | `x86\Debug\` | 调试 x86 版本 |
| Debug | x64 | `x64\Debug\` | 调试 x64 版本 |
| Release | Win32 | `x86\Release\` | 发布 x86 版本 |
| Release | x64 | `x64\Release\` | 发布 x64 版本 |

### 编译常见错误及解决

#### 错误 1: 无法打开 HPSocket 头文件
```
错误: fatal error C1083: 无法打开包含文件: "HPSocket.h"
解决: 检查项目属性 → C/C++ → 常规 → 附加包含目录
     确保包含: $(SolutionDir)thirdparty\Include\HPSocket
```

#### 错误 2: 链接错误 - 无法解析的外部符号
```
错误: LNK2019: 无法解析的外部符号 _HP_Server_Create
解决: 检查项目属性 → 链接器 → 输入 → 附加依赖项
     确保包含: HPSocket_UD.lib (Debug) 或 HPSocket.lib (Release)
```

#### 错误 3: 字符编码错误
```
错误: C4819: 文件包含不能在当前代码页中表示的字符
解决: 
1. 文件 → 高级保存选项 → 编码: UTF-8 with BOM
2. 或运行: python convert_to_utf8_bom.py
```

---

## 调试技巧

### 1. Visual Studio 调试

#### 设置断点
```
- 在代码行号左侧点击设置断点
- 按 F5 启动调试
- 断点命中后可以查看变量值、调用堆栈
```

#### 条件断点
```
- 右键断点 → 条件
- 输入条件表达式，例如: clientId == 1001
- 断点只在条件为真时触发
```

#### 监视变量
```
- 调试 → 窗口 → 监视
- 添加变量名实时查看值变化
- 支持复杂表达式计算
```

#### 调用堆栈
```
- 调试 → 窗口 → 调用堆栈
- 查看函数调用链
- 双击跳转到对应函数
```

### 2. 日志输出

#### 添加日志
```cpp
#include <fstream>
#include <ctime>

void WriteLog(const std::wstring& msg) {
    time_t now = time(nullptr);
    char timeStr[26];
    ctime_s(timeStr, sizeof(timeStr), &now);
    timeStr[24] = '\0'; // 去除换行符
    
    std::wofstream logFile(L"debug.log", std::ios::app);
    logFile << L"[" << timeStr << L"] " << msg << std::endl;
    logFile.close();
}

// 使用
WriteLog(L"客户端连接: " + std::to_wstring(clientId));
```

#### 主控端日志 (AddLog)
```cpp
// Master 端已有日志系统
AddLog(L"网络", L"客户端上线: " + ip);
AddLog(L"文件", L"下载完成: " + fileName);
```

### 3. 内存泄漏检测

#### 使用 CRT 调试库
```cpp
// 在 main.cpp 开头添加
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

// 在程序退出前
_CrtDumpMemoryLeaks();
```

### 4. 网络调试

#### 抓包工具
- 使用 Wireshark 抓取 TCP 流量
- 过滤器: `tcp.port == 8080`
- 查看数据包格式和内容

#### 网络日志
```cpp
// 在 NetworkManager::OnReceive 添加日志
std::string hexDump;
char buf[16];
for(int i = 0; i < iLength; i++) {
    sprintf_s(buf, "%02X ", pData[i]);
    hexDump += buf;
}
AddLog(L"网络", L"接收数据: " + ToWString(hexDump));
```

---

## 开发流程

### 功能开发流程

```
1. 需求分析
   ├─ 确定功能需求
   ├─ 设计数据结构
   └─ 设计通信协议

2. 创建分支
   git checkout -b feature/your-feature-name

3. 编码实现
   ├─ 修改配置文件 (Common/Config.h)
   ├─ 实现模块代码 (Modules/)
   ├─ 实现主控端 UI (Master/UI/)
   └─ 实现命令处理 (Master/Core/)

4. 测试验证
   ├─ 单元测试
   ├─ 集成测试
   └─ 手动测试

5. 代码审查
   ├─ 自我审查
   ├─ 同事审查
   └─ 修复问题

6. 合并分支
   git checkout main
   git merge feature/your-feature-name

7. 发布版本
   ├─ 更新版本号
   ├─ 编译发布
   └─ 更新文档
```

### 添加新功能模块

#### 步骤 1: 定义命令
在 `Common/Config.h` 添加命令常量：
```cpp
enum CommandType : uint32_t {
    // ... 现有命令
    CMD_YOUR_NEW_CMD = 110,  // 新命令
};
```

#### 步骤 2: 实现模块 DLL
创建 `Modules/YourModule/YourModule.cpp`：
```cpp
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(
    SOCKET s, CommandPkg* pkg
) {
    uint32_t cmd = pkg->cmd;
    
    switch(cmd) {
        case CMD_YOUR_NEW_CMD:
            HandleYourCmd(s, pkg);
            break;
    }
}

void HandleYourCmd(SOCKET s, CommandPkg* pkg) {
    // 实现功能
    // 发送响应: SendData(s, data, len);
}
```

#### 步骤 3: 实现主控端 UI
创建 `Master/UI/YourDialog.h` 和 `YourDialog.cpp`：
```cpp
class YourDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static HWND Show(HWND hParent, uint32_t clientId);
};
```

#### 步骤 4: 集成到主窗口
在 `Master/MainWindow.cpp` 添加菜单项和事件处理：
```cpp
case IDM_YOUR_FEATURE: {
    int clientId = GetSelectedClientId();
    if(clientId > 0) {
        YourDialog::Show(hWnd, clientId);
    }
    break;
}
```

#### 步骤 5: 实现命令处理
在 `Master/Core/CommandHandler.cpp` 添加处理函数：
```cpp
void CommandHandler::HandleYourData(uint32_t clientId, const CommandPkg* pkg, int dataLen) {
    // 处理响应数据
    // 更新 UI
}
```

#### 步骤 6: 添加资源
- 在 `Master/res/` 添加图标
- 在 `Master/MasterGUI.rc` 添加资源引用
- 在 `Master/resource.h` 添加 ID 定义

---

## 代码规范

### 1. 文件结构

```cpp
/**
 * FileName.cpp - 文件描述
 * Encoding: UTF-8 BOM
 * 
 * 功能说明:
 * - 功能点1
 * - 功能点2
 * 
 * 依赖:
 * - 模块1
 * - 模块2
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// 头文件包含
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <system_headers...>

#include "local_headers.h"

// 命名空间
using namespace Formidable;

// 函数实现
```

### 2. 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 类名 | PascalCase | `TerminalDialog` |
| 函数名 | PascalCase | `HandleProcessList` |
| 变量名 | camelCase | `clientId` |
| 成员变量 | m_camelCase | `m_clientId` |
| 常量 | UPPER_CASE | `CMD_TERMINAL_OPEN` |
| 宏定义 | UPPER_CASE | `WIN32_LEAN_AND_MEAN` |
| 枚举值 | UPPER_CASE | `STATUS_RUNNING` |

### 3. 注释规范

#### 文件头注释
```cpp
/**
 * FileName.cpp - 文件描述
 * Encoding: UTF-8 BOM
 * 
 * 作者: Your Name
 * 创建日期: 2026-02-08
 * 最后修改: 2026-02-08 by Your Name
 * 
 * 功能说明:
 * - 功能点1
 * - 功能点2
 * 
 * 依赖:
 * - 模块1
 * - 模块2
 */
```

#### 函数注释
```cpp
/// <summary>
/// 发送命令到指定客户端
/// </summary>
/// <param name="clientId">客户端ID</param>
/// <param name="cmd">命令类型</param>
/// <param name="arg1">参数1</param>
/// <param name="arg2">参数2</param>
/// <returns>成功返回true，失败返回false</returns>
bool SendCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1, uint32_t arg2);
```

#### 行内注释
```cpp
// 创建 TCP 连接
SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

// 连接服务器
if(connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
    // 连接失败，记录日志
    WriteLog(L"连接失败");
    closesocket(sock);
    return false;
}
```

### 4. 格式规范

#### 缩进
- 使用 4 空格缩进
- 不使用 Tab 字符

#### 大括号
```cpp
// 推荐: K&R 风格
if(condition) {
    // code
}

// 或 Allman 风格
if(condition)
{
    // code
}
```

#### 空格
```cpp
// 运算符前后加空格
int result = a + b * c;

// 逗号后加空格
void func(int a, int b, int c);

// 控制语句关键字后加空格
if(condition) {
    while(condition) {
        for(int i = 0; i < 10; i++) {
        }
    }
}
```

---

## 测试指南

### 1. 单元测试

#### 创建测试用例
```cpp
// Test/TestNetwork.cpp
#include <gtest/gtest.h>

TEST(NetworkTest, SendCommand) {
    uint32_t clientId = 1001;
    uint32_t cmd = CMD_HEARTBEAT;
    
    bool result = SendCommand(clientId, cmd, 0, 0);
    EXPECT_TRUE(result);
}

TEST(NetworkTest, InvalidClientId) {
    uint32_t clientId = 9999; // 不存在的客户端
    bool result = SendCommand(clientId, CMD_HEARTBEAT, 0, 0);
    EXPECT_FALSE(result);
}
```

### 2. 集成测试

#### 测试场景 1: 客户端上线
```
步骤:
1. 启动 Master
2. 启动 Client
3. 验证客户端出现在 Master 列表
4. 验证客户端信息正确

预期结果:
- 客户端成功上线
- 显示正确的 IP、端口、系统信息
```

#### 测试场景 2: 终端功能
```
步骤:
1. 选择客户端
2. 打开终端对话框
3. 执行命令: dir
4. 验证输出正确

预期结果:
- 终端对话框成功打开
- 命令正确执行
- 输出正确显示
```

#### 测试场景 3: 文件传输
```
步骤:
1. 选择客户端
2. 打开文件管理器
3. 上传测试文件
4. 下载文件
5. 验证文件内容

预期结果:
- 文件成功上传
- 文件成功下载
- 文件内容一致
```

### 3. 压力测试

#### 多客户端连接
```
测试: 同时连接 100 个客户端
工具: 使用 Client 多开
验证: Master 是否稳定
```

#### 大数据传输
```
测试: 传输 100MB 文件
工具: 文件管理器
验证: 传输速度、内存使用
```

### 4. 兼容性测试

#### 平台兼容性
```
测试矩阵:
- Master Win32 + Client Win32
- Master Win32 + Client x64
- Master x64 + Client Win32
- Master x64 + Client x64

验证: 所有组合正常工作
```

#### 系统兼容性
```
测试平台:
- Windows 10 x64
- Windows 11 x64

验证: 所有功能正常
```

---

## 常见问题

### Q1: 编译时提示 "无法解析的外部符号"
**原因**: 链接器找不到库文件
**解决**:
```
1. 检查项目属性 → 链接器 → 输入 → 附加依赖项
2. 确保库文件路径正确
3. 检查库文件是否已编译
4. 确保平台匹配 (x86/x64)
```

### Q2: 运行时提示 "找不到 DLL"
**原因**: 依赖的 DLL 不在 PATH 中
**解决**:
```
1. 将 DLL 复制到可执行文件同级目录
2. 或将 DLL 目录添加到系统 PATH
3. 或使用安装脚本 deploy_dlls.bat
```

### Q3: 客户端无法连接到主控端
**原因**: 网络问题或防火墙阻止
**解决**:
```
1. 检查主控端是否启动
2. 检查 IP 和端口是否正确
3. 检查防火墙设置
4. 使用 ping 测试网络连通性
5. 使用 telnet 测试端口: telnet 127.0.0.1 8080
```

### Q4: 模块加载失败
**原因**: DLL 文件不存在或资源 ID 错误
**解决**:
```
1. 检查 DLL 文件是否存在
2. 检查资源 ID 是否正确
3. 检查 DLL 依赖项是否满足
4. 使用 Dependency Walker 检查依赖
```

### Q5: 中文显示乱码
**原因**: 字符编码问题
**解决**:
```
1. 确保所有源文件使用 UTF-8 with BOM
2. 检查编译器字符集设置
3. 使用宽字符函数 (W 版本)
4. 转换函数: WideToUTF8 / UTF8ToWide
```

### Q6: 内存泄漏
**原因**: 资源未正确释放
**解决**:
```
1. 使用 _CrtDumpMemoryLeaks() 检测
2. 检查 new/delete 配对
3. 检查句柄释放 (CloseHandle, closesocket)
4. 使用智能指针自动管理
```

---

## 贡献指南

### 贡献流程

```
1. Fork 项目到你的 GitHub 账号
2. 克隆你的 Fork
   git clone https://github.com/yourusername/Formidable2026.git
3. 创建功能分支
   git checkout -b feature/your-feature
4. 提交更改
   git commit -m "Add your feature"
5. 推送到你的 Fork
   git push origin feature/your-feature
6. 创建 Pull Request
```

### 提交规范

#### Commit Message 格式
```
<type>(<scope>): <subject>

<body>

<footer>
```

#### Type 类型
```
feat: 新功能
fix: 修复 Bug
docs: 文档更新
style: 代码格式调整
refactor: 重构
perf: 性能优化
test: 测试相关
chore: 构建/工具相关
```

#### 示例
```
feat(terminal): 添加 PowerShell 支持支持 PowerShell 命令执行，包括管道和重定向功能。

- 修改 Terminal.dll 支持 PowerShell
- 添加 CMD_POWER_SHELL 命令
- 更新 UI 添加模式选择

Closes #123
```

### 代码审查清单

提交 Pull Request 前请确认：

- [ ] 代码遵循项目编码规范
- [ ] 添加了必要的注释
- [ ] 通过所有测试
- [ ] 更新了相关文档
- [ ] 没有引入新的警告
- [ ] 没有内存泄漏
- [ ] 兼容 Win32 和 x64 平台
- [ ] Commit message 清晰明确

---

## 性能优化建议

### 1. 网络优化
- 使用异步 I/O
- 批量发送数据
- 减少网络往返次数
- 使用压缩算法

### 2. 内存优化
- 使用对象池
- 避免频繁分配/释放
- 使用智能指针
- 及时释放资源

### 3. UI 优化
- 使用虚拟列表
- 控制刷新频率
- 后台线程处理耗时操作
- 使用双缓冲

---

## 参考资源

### 官方文档
- [Windows API 文档](https://docs.microsoft.com/en-us/windows/win32/api/)
- [Visual Studio 文档](https://docs.microsoft.com/en-us/visualstudio/)
- [MSVC 编译器选项](https://docs.microsoft.com/en-us/cpp/build/reference/compiler-options)

### 开源项目
- [HPSocket](https://github.com/ldcsaa/HP-Socket)
- [MemoryModule](https://github.com/fancycode/MemoryModule)
- [FFmpeg](https://ffmpeg.org/)

### 学习资源
- [C++ 参考手册](https://en.cppreference.com/w/)
- [Windows 编程指南](https://docs.microsoft.com/en-us/windows/win32/)

---

**Formidable 2026 - 开发指南**
