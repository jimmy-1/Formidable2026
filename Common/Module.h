#ifndef FORMIDABLE_MODULE_H
#define FORMIDABLE_MODULE_H
#include <string>
#include <vector>
#include <winsock2.h>
#include "Config.h"
namespace Formidable {
    // 模块导出函数原型
    typedef void (WINAPI* PFN_MODULE_ENTRY)(SOCKET s, CommandPkg* pkg);
    class IModule {
    public:
        virtual ~IModule() {}
        virtual std::string GetModuleName() = 0;
        virtual void Execute(SOCKET s, CommandPkg* pkg) = 0;
    };
}
#endif // FORMIDABLE_MODULE_H
