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
    std::vector<char> buffer;
    HKEY hKey;
    std::wstring wSubKey = UTF8ToWide(subKey);
    if (RegOpenKeyExW(hRoot, wSubKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        uint32_t count = 0;
        buffer.resize(4);
        memcpy(buffer.data(), &count, 4);
        return std::string(buffer.begin(), buffer.end());
    }

    wchar_t name[256];
    DWORD nameSize;
    DWORD index = 0;
    
    std::vector<std::string> keys;
    
    // 枚举子项
    while (true) {
        nameSize = sizeof(name) / sizeof(wchar_t);
        if (RegEnumKeyExW(hKey, index, name, &nameSize, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        keys.push_back(WideToUTF8(name));
        index++;
    }

    RegCloseKey(hKey);

    // 序列化: [Count:4] ([Len:4][Name])...
    uint32_t count = (uint32_t)keys.size();
    size_t totalSize = 4;
    for (const auto& k : keys) {
        totalSize += 4 + k.size();
    }
    
    buffer.resize(totalSize);
    char* p = buffer.data();
    
    memcpy(p, &count, 4); p += 4;
    for (const auto& k : keys) {
        uint32_t len = (uint32_t)k.size();
        memcpy(p, &len, 4); p += 4;
        memcpy(p, k.c_str(), len); p += len;
    }
    
    return std::string(buffer.begin(), buffer.end());
}

std::string ListRegistryValues(HKEY hRoot, const char* subKey) {
    std::vector<char> buffer;
    HKEY hKey;
    std::wstring wSubKey = UTF8ToWide(subKey);
    if (RegOpenKeyExW(hRoot, wSubKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        uint32_t count = 0;
        buffer.resize(4);
        memcpy(buffer.data(), &count, 4);
        return std::string(buffer.begin(), buffer.end());
    }

    wchar_t name[16383]; // Max value name length
    DWORD nameSize;
    DWORD index = 0;
    
    struct ValInfo {
        std::string name;
        uint32_t type;
        std::string data;
    };
    std::vector<ValInfo> values;
    
    // 枚举值
    while (true) {
        nameSize = sizeof(name) / sizeof(wchar_t);
        DWORD type;
        // 动态获取数据大小
        DWORD dataSize = 0;
        LSTATUS status = RegEnumValueW(hKey, index, name, &nameSize, NULL, &type, NULL, &dataSize);
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) break;
        
        std::vector<BYTE> dataBuf(dataSize);
        nameSize = sizeof(name) / sizeof(wchar_t); // Reset nameSize
        if (RegEnumValueW(hKey, index, name, &nameSize, NULL, &type, dataBuf.data(), &dataSize) != ERROR_SUCCESS) {
            index++;
            continue;
        }
        
        ValInfo val;
        val.name = WideToUTF8(name);
        val.type = type;
        
        switch (type) {
            case REG_SZ:
            case REG_EXPAND_SZ:
                if (dataSize > 0) {
                    // 确保以 null 结尾
                    if (dataSize >= 2 && dataBuf[dataSize-2] == 0 && dataBuf[dataSize-1] == 0) {
                        val.data = WideToUTF8((wchar_t*)dataBuf.data());
                    } else {
                         // 有些畸形数据可能没有null结尾
                         std::wstring ws((wchar_t*)dataBuf.data(), dataSize / 2);
                         val.data = WideToUTF8(ws.c_str());
                    }
                }
                break;
            case REG_DWORD:
            case REG_DWORD_BIG_ENDIAN:
                if (dataSize >= 4) {
                    char buf[32];
                    sprintf_s(buf, "0x%08X (%u)", *(DWORD*)dataBuf.data(), *(DWORD*)dataBuf.data());
                    val.data = buf;
                }
                break;
            case REG_QWORD:
                if (dataSize >= 8) {
                    char buf[64];
                    sprintf_s(buf, "0x%016llX", *(unsigned long long*)dataBuf.data());
                    val.data = buf;
                }
                break;
            case REG_MULTI_SZ:
                // 处理多字符串
                if (dataSize > 0) {
                    std::wstring ws;
                    wchar_t* p = (wchar_t*)dataBuf.data();
                    wchar_t* end = (wchar_t*)(dataBuf.data() + dataSize);
                    while (p < end && *p) {
                        if (!ws.empty()) ws += L" ";
                        ws += p;
                        p += wcslen(p) + 1;
                    }
                    val.data = WideToUTF8(ws.c_str());
                }
                break;
            case REG_BINARY:
            default:
                // 二进制数据转 hex 字符串 (前 256 字节)
                {
                    std::stringstream ss;
                    DWORD showLen = dataSize > 256 ? 256 : dataSize;
                    for (DWORD i = 0; i < showLen; ++i) {
                        char hex[4];
                        sprintf_s(hex, "%02X ", dataBuf[i]);
                        ss << hex;
                    }
                    if (dataSize > 256) ss << "...";
                    val.data = ss.str();
                }
                break;
        }
        
        values.push_back(val);
        index++;
    }

    RegCloseKey(hKey);

    // 序列化: [Count:4] ([Type:4][NameLen:4][Name][DataLen:4][Data])...
    uint32_t count = (uint32_t)values.size();
    size_t totalSize = 4;
    for (const auto& v : values) {
        totalSize += 4 + 4 + v.name.size() + 4 + v.data.size();
    }
    
    buffer.resize(totalSize);
    char* p = buffer.data();
    
    memcpy(p, &count, 4); p += 4;
    for (const auto& v : values) {
        memcpy(p, &v.type, 4); p += 4;
        
        uint32_t nameLen = (uint32_t)v.name.size();
        memcpy(p, &nameLen, 4); p += 4;
        memcpy(p, v.name.c_str(), nameLen); p += nameLen;
        
        uint32_t dataLen = (uint32_t)v.data.size();
        memcpy(p, &dataLen, 4); p += 4;
        memcpy(p, v.data.c_str(), dataLen); p += dataLen;
    }
    
    return std::string(buffer.begin(), buffer.end());
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
