#include "BehaviorAnalysisProtection.h"

// 只有在非 DLL 模块编译时才包含 Logger，或者在 DLL 模块中禁用日志
#ifndef FORMIDABLE_MODULE_DLL
#include "../Utils/Logger.h"
#define LOG_INFO(msg) Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_INFO, msg)
#define LOG_WARNING(msg) Formidable::Client::Utils::Logger::Log(Formidable::Client::Utils::LogLevel::LL_WARNING, msg)
#else
#define LOG_INFO(msg) 
#define LOG_WARNING(msg)
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <iphlpapi.h>

#pragma comment(lib, "IPHLPAPI.lib")

namespace Formidable {
namespace Client {
namespace Security {

// Helper for Anti-VM
bool IsRunningInVM() {
    // 1. Check CPUID
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 1);
    if ((cpuInfo[2] >> 31) & 1) return true; // Hypervisor present bit

    // 2. Check RAM size (< 2GB is suspicious for modern Windows)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    if (memInfo.ullTotalPhys / 1024 / 1024 < 2048) return true;

    // 3. Check CPU Cores (< 2 is suspicious)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    if (sysInfo.dwNumberOfProcessors < 2) return true;

    return false;
}

// Helper for Anti-Debug
bool IsDebugged() {
    if (IsDebuggerPresent()) return true;
    
    BOOL bRemoteDebug = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &bRemoteDebug);
    if (bRemoteDebug) return true;

    return false;
}

void BehaviorAnalysisProtection::InitializeProtection() {
    LOG_INFO("Initializing Behavior Analysis Protection");
    // Initialize random seed or other helpers
}

void BehaviorAnalysisProtection::ProtectAgainstAnalysis() {
    if (IsDebugged()) {
        LOG_WARNING("Debugger detected! Exiting.");
        // Subtle exit or infinite loop
        ExitProcess(0); 
    }

    if (IsRunningInVM()) {
        LOG_WARNING("VM detected! Continuing with caution.");
        // Decide whether to run or reduce functionality
        // For now, we continue but might log or change behavior
        // ExitProcess(0); // Optional: Exit if VM detected
    }
}

// APIHooking
void BehaviorAnalysisProtection::APIHooking::HookSensitiveAPIs() {
    // Placeholder
}

void BehaviorAnalysisProtection::APIHooking::BypassAPIMonitoring() {
    // Placeholder: Direct Syscalls would go here
}

void BehaviorAnalysisProtection::APIHooking::ApplyAPIObfuscation() {
    // Placeholder
}

// MemoryManipulation
void BehaviorAnalysisProtection::MemoryManipulation::ProtectMemoryRegions() {
    // Placeholder
}

void BehaviorAnalysisProtection::MemoryManipulation::ApplyMemoryObfuscation() {
    // Placeholder
}

void BehaviorAnalysisProtection::MemoryManipulation::BypassMemoryScanning() {
    // Placeholder
}

// ExecutionFlow
void BehaviorAnalysisProtection::ExecutionFlow::ObfuscateExecutionFlow() {
    // Placeholder
}

void BehaviorAnalysisProtection::ExecutionFlow::ApplyControlFlowGuard() {
    // Placeholder
}

void BehaviorAnalysisProtection::ExecutionFlow::BypassExecutionMonitoring() {
    // Placeholder
}

} // namespace Security
} // namespace Client
} // namespace Formidable
