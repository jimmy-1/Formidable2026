# Formidable2026 Master 模块化重构说明

## 重构目标
将原 4665 行的 main_gui.cpp 拆分为多个小文件，每个文件不超过 500 行，防止编码保存错误导致项目损坏。

## 新文件结构

### 核心模块
- **GlobalState.h / GlobalState.cpp** (约120行)
  - 所有全局变量声明和定义
  - ListView排序回调函数
  
- **Config.h / Config.cpp** (约30行)
  - LoadSettings / SaveSettings
  - 配置文件读写

- **StringUtils.h / StringUtils.cpp** (约20行)
  - ToWString 重载函数
  
- **MainWindow.h / MainWindow.cpp** (待创建，约600行)
  - WndProc 窗口过程
  - CreateMainMenu / CreateMainToolbar
  - UpdateStatusBar
  - InitListView / AddLog
  - HandleCommand (命令分发)
  - 托盘图标管理

- **NetworkHelper.h / NetworkHelper.cpp** (待创建，约400行)
  - SendDataToClient
  - SendModuleToClient
  - SendSimpleCommand
  - GetResourceData
  - NetworkThread / HeartbeatThread

### 对话框模块
- **Dialogs/BuilderDialog.h / .cpp** (待创建，约400行)
  - BuilderDlgProc 完整实现
  
- **Dialogs/SettingsDialog.h / .cpp** (待创建，约200行)
  - SettingsDlgProc 实现

- **Dialogs/ModuleDialog.h / .cpp** (待创建，约100行)
  - ModuleDlgProc (进程模块查看)

### 主入口
- **main_new.cpp** (约170行)
  - WinMain 入口函数
  - 包含所有模块头文件
  - 最小化核心逻辑

## 迁移步骤

### 第一阶段 (已完成)
✅ 创建 GlobalState.h/cpp  
✅ 创建 Config.h/cpp  
✅ 创建 StringUtils.h/cpp  
✅ 创建 NetworkHelper.h  
✅ 创建 BuilderDialog.h / SettingsDialog.h  
✅ 创建 main_new.cpp 框架

### 第二阶段 (进行中)
- [ ] 创建 MainWindow.cpp - 提取窗口过程代码
- [ ] 创建 NetworkHelper.cpp - 提取网络发送代码
- [ ] 创建 Dialogs/BuilderDialog.cpp - 提取生成器对话框
- [ ] 创建 Dialogs/SettingsDialog.cpp - 提取设置对话框

### 第三阶段
- [ ] 更新 Formidable2026.vcxproj 项目文件
- [ ] 将 main_gui.cpp 重命名为 main_gui_old.cpp (备份)
- [ ] 将 main_new.cpp 重命名为 main_gui.cpp
- [ ] 编译测试所有模块

## 文件大小对比
| 文件 | 原大小 | 新大小 | 减少 |
|------|--------|--------|------|
| main_gui.cpp | 4665行 | → 分散到9个文件 | -95% |
| main_new.cpp | - | 170行 | 新建 |
| GlobalState.cpp | - | 120行 | 新建 |
| Config.cpp | - | 30行 | 新建 |
| MainWindow.cpp | - | ~600行 | 待创建 |
| NetworkHelper.cpp | - | ~400行 | 待创建 |
| BuilderDialog.cpp | - | ~400行 | 待创建 |

## 好处
1. **防止编码错误** - 单文件小于500行，保存时不易损坏
2. **易于维护** - 功能分离，职责清晰
3. **并行开发** - 多人可同时编辑不同模块
4. **编译加速** - 修改单个模块只重编译该文件
5. **代码复用** - 各模块可独立测试

## 编码保护措施
- 所有新文件使用 UTF-8 BOM 编码
- 每个文件头部注释声明编码格式
- 使用 Git 追踪每次修改
- 保留 main_gui_old.cpp 作为完整备份
