/**
 * Formidable2026 - RegistryManager Module (DLL)
 * Encoding: UTF-8 BOM
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include <sstream>
#include <vector>
#include "../../Common/Config.h"
#include "../../Common/Utils.h"

using namespace Formidable;

void SendResponse(SOCKET s, uint32_t cmd, uint32_t arg1, uint32_t arg2, const void* data, int len) {
    PkgHeader header;
    memcpy(header.flag, "FRMD26?", 7);
    header.originLen = sizeof(CommandPkg) - 1 + len;
    header.totalLen = sizeof(PkgHeader) + header.originLen;
    
    std::vector<char> buffer(header.totalLen);
    memcpy(buffer.data(), &header, sizeof(PkgHeader));
    
    CommandPkg* pkg = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    pkg->cmd = cmd;
    pkg->arg1 = arg1;
    pkg->arg2 = arg2;
    if (len > 0 && data) {
        memcpy(pkg->data, data, len);
    }
    
    send(s, buffer.data(), (int)buffer.size(), 0);
}

std::string ListRegistryKeys(HKEY hRoot, const char* subKey) {
    std::stringstream ss;
    HKEY hKey;
    std::wstring wSubKey = UTF8ToWide(subKey);
    if (RegOpenKeyExW(hRoot, wSubKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return "";
    }

    wchar_t name[256];
    DWORD nameSize;
    DWORD index = 0;
    
    // 枚举子项 (格式: K|键名)
    while (true) {
        nameSize = sizeof(name) / sizeof(wchar_t);
        if (RegEnumKeyExW(hKey, index, name, &nameSize, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        ss << "K|" << WideToUTF8(name) << "\n";
        index++;
    }

    RegCloseKey(hKey);
    return ss.str();
}

std::string ListRegistryValues(HKEY hRoot, const char* subKey) {
    std::stringstream ss;
    HKEY hKey;
    std::wstring wSubKey = UTF8ToWide(subKey);
    if (RegOpenKeyExW(hRoot, wSubKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return "";
    }

    wchar_t name[256];
    DWORD nameSize;
    DWORD index = 0;
    
    // 枚举值 (格式: V|值名|类型字符串|数据)
    while (true) {
        nameSize = sizeof(name) / sizeof(wchar_t);
        DWORD type;
        BYTE data[8192];
        DWORD dataSize = sizeof(data);
        if (RegEnumValueW(hKey, index, name, &nameSize, NULL, &type, data, &dataSize) != ERROR_SUCCESS) break;
        
        std::string typeStr;
        std::string valData;
        
        switch (type) {
            case REG_SZ:
                typeStr = "REG_SZ";
                valData = WideToUTF8((wchar_t*)data);
                break;
            case REG_EXPAND_SZ:
                typeStr = "REG_EXPAND_SZ";
                valData = WideToUTF8((wchar_t*)data);
                break;
            case REG_DWORD:
                typeStr = "REG_DWORD";
                {
                    char buf[32];
                    sprintf_s(buf, "0x%08X", *(DWORD*)data);
                    valData = buf;
                }
                break;
            case REG_DWORD_BIG_ENDIAN:
                typeStr = "REG_DWORD_BIG_ENDIAN";
                valData = WideToUTF8(L"(DWORD_BIG_ENDIAN)");
                break;
            case REG_MULTI_SZ:
                typeStr = "REG_MULTI_SZ";
                valData = WideToUTF8(L"(多字符串)");
                break;
            case REG_BINARY:
                typeStr = "REG_BINARY";
                {
                    char buf[16];
                    sprintf_s(buf, "(%u 字节)", dataSize);
                    valData = buf;
                }
                break;
            case REG_QWORD:
                typeStr = "REG_QWORD";
                {
                    char buf[32];
                    sprintf_s(buf, "0x%016llX", *(unsigned long long*)data);
                    valData = buf;
                }
                break;
            default:
                typeStr = "未知类型";
                valData = WideToUTF8(L"(未知数据)");
                break;
        }
        
        ss << "V|" << WideToUTF8(name) << "|" << typeStr << "|" << valData << "\n";
        index++;
    }

    RegCloseKey(hKey);
    return ss.str();
}

// 删除键
bool DeleteKey(HKEY hRoot, const char* subKey) {
    // 递归删除子键 (XP 以后 RegDeleteTree 更好，这里兼容性处理)
    HKEY hKey;
    std::wstring wSubKey = UTF8ToWide(subKey);
    if (RegOpenKeyExW(hRoot, wSubKey.c_str(), 0, KEY_ENUMERATE_SUB_KEYS | DELETE, &hKey) == ERROR_SUCCESS) {
        wchar_t subKeyName[MAX_PATH];
        DWORD subKeyNameSize = MAX_PATH;
        while (RegEnumKeyExW(hKey, 0, subKeyName, &subKeyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            std::string subSubKey = std::string(subKey) + "\\" + WideToUTF8(subKeyName);
            if (!DeleteKey(hRoot, subSubKey.c_str())) break; // Recursively delete using full path
            subKeyNameSize = MAX_PATH;
        }
        RegCloseKey(hKey);
    }
    return RegDeleteKeyW(hRoot, wSubKey.c_str()) == ERROR_SUCCESS;
}

// 删除值
bool DeleteValue(HKEY hRoot, const char* subKey, const char* valueName) {
    HKEY hKey;
    std::wstring wSubKey = UTF8ToWide(subKey);
    std::wstring wValueName = UTF8ToWide(valueName);
    if (RegOpenKeyExW(hRoot, wSubKey.c_str(), 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return false;
    LSTATUS res = RegDeleteValueW(hKey, wValueName.c_str());
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

// DLL 导出函数
extern "C" __declspec(dllexport) void WINAPI ModuleEntry(SOCKET s, CommandPkg* pkg) {
    if (pkg->cmd == CMD_REGISTRY_CTRL) {
        // arg1: Root Key Index (0-4)
        // arg2: Action (1=ListKeys, 2=ListValues, 3=DeleteKey, 4=DeleteValue)
        // data: Path (for ListKeys/ListValues/DeleteKey) or "Path|ValueName" (for DeleteValue)
        
        HKEY hRoots[] = { HKEY_CLASSES_ROOT, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, HKEY_USERS, HKEY_CURRENT_CONFIG };
        if (pkg->arg1 >= 5) return;
        HKEY hRoot = hRoots[pkg->arg1];
        
        if (pkg->arg2 == 1) { // List Keys
            std::string result = ListRegistryKeys(hRoot, pkg->data);
            SendResponse(s, CMD_REGISTRY_CTRL, (uint32_t)result.size(), 1, result.c_str(), (int)result.size());
        } else if (pkg->arg2 == 2) { // List Values
            std::string result = ListRegistryValues(hRoot, pkg->data);
            SendResponse(s, CMD_REGISTRY_CTRL, (uint32_t)result.size(), 2, result.c_str(), (int)result.size());
        } else if (pkg->arg2 == 3) { // Delete Key
            bool bRet = DeleteKey(hRoot, pkg->data);
            const char* msg = bRet ? "OK_KEY" : "FAIL_KEY";
            SendResponse(s, CMD_REGISTRY_CTRL, (uint32_t)strlen(msg), 3, msg, (int)strlen(msg));
        } else if (pkg->arg2 == 4) { // Delete Value
            std::string data = pkg->data;
            size_t pos = data.find('|');
            if (pos != std::string::npos) {
                std::string path = data.substr(0, pos);
                std::string val = data.substr(pos + 1);
                bool bRet = DeleteValue(hRoot, path.c_str(), val.c_str());
                const char* msg = bRet ? "OK_VAL" : "FAIL_VAL";
                SendResponse(s, CMD_REGISTRY_CTRL, (uint32_t)strlen(msg), 4, msg, (int)strlen(msg));
            }
        }
    }
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
