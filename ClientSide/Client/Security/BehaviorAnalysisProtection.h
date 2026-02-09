#pragma once

namespace Formidable {
namespace Client {
namespace Security {

class BehaviorAnalysisProtection {
public:
    static void InitializeProtection();
    static void ProtectAgainstAnalysis();
    
    class APIHooking {
    public:
        static void HookSensitiveAPIs();
        static void BypassAPIMonitoring();
        static void ApplyAPIObfuscation();
    };
    
    class MemoryManipulation {
    public:
        static void ProtectMemoryRegions();
        static void ApplyMemoryObfuscation();
        static void BypassMemoryScanning();
    };
    
    class ExecutionFlow {
    public:
        static void ObfuscateExecutionFlow();
        static void ApplyControlFlowGuard();
        static void BypassExecutionMonitoring();
    };
};

} // namespace Security
} // namespace Client
} // namespace Formidable
