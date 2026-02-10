import winim

# --- Syscall Prototypes ---

proc NtAllocateVirtualMemory(
    processHandle: HANDLE,
    baseAddress: ptr PVOID,
    zeroBits: ULONG,
    regionSize: ptr SIZE_T,
    allocationType: ULONG,
    protect: ULONG
): NTSTATUS {.importc, dynlib: "ntdll", stdcall.}

proc NtWriteVirtualMemory(
    processHandle: HANDLE,
    baseAddress: PVOID,
    buffer: PVOID,
    numberOfBytesToWrite: SIZE_T,
    numberOfBytesWritten: PSIZE_T
): NTSTATUS {.importc, dynlib: "ntdll", stdcall.}

proc NtCreateThreadEx(
    threadHandle: PHANDLE,
    desiredAccess: ACCESS_MASK,
    objectAttributes: POBJECT_ATTRIBUTES,
    processHandle: HANDLE,
    startRoutine: PVOID,
    argument: PVOID,
    createFlags: ULONG,
    zeroBits: ULONG_PTR,
    stackSize: SIZE_T,
    maximumStackSize: SIZE_T,
    attributeList: PVOID
): NTSTATUS {.importc, dynlib: "ntdll", stdcall.}

proc NtOpenProcess(
    processHandle: PHANDLE,
    desiredAccess: ACCESS_MASK,
    objectAttributes: POBJECT_ATTRIBUTES,
    clientId: PCLIENT_ID
): NTSTATUS {.importc, dynlib: "ntdll", stdcall.}

# --- Public Syscall Wrappers ---

proc syscallNtAllocateVirtualMemory*(
  processHandle: HANDLE,
  baseAddress: ptr PVOID,
  zeroBits: ULONG,
  regionSize: ptr SIZE_T,
  allocationType: ULONG,
  protect: ULONG
): NTSTATUS =
  return NtAllocateVirtualMemory(processHandle, baseAddress, zeroBits, regionSize, allocationType, protect)

proc syscallNtWriteVirtualMemory*(
  processHandle: HANDLE,
  baseAddress: PVOID,
  buffer: PVOID,
  numberOfBytesToWrite: SIZE_T,
  numberOfBytesWritten: PSIZE_T
): NTSTATUS =
  return NtWriteVirtualMemory(processHandle, baseAddress, buffer, numberOfBytesToWrite, numberOfBytesWritten)

proc syscallNtCreateThreadEx*(
  threadHandle: PHANDLE,
  desiredAccess: ACCESS_MASK,
  objectAttributes: POBJECT_ATTRIBUTES,
  processHandle: HANDLE,
  startRoutine: PVOID,
  argument: PVOID,
  createFlags: ULONG,
  zeroBits: ULONG_PTR,
  stackSize: SIZE_T,
  maximumStackSize: SIZE_T,
  attributeList: PVOID
): NTSTATUS =
  return NtCreateThreadEx(threadHandle, desiredAccess, objectAttributes, processHandle, startRoutine, argument, createFlags, zeroBits, stackSize, maximumStackSize, attributeList)

proc syscallNtOpenProcess*(
    processHandle: PHANDLE,
    desiredAccess: ACCESS_MASK,
    objectAttributes: POBJECT_ATTRIBUTES,
    clientId: PCLIENT_ID
): NTSTATUS =
    return NtOpenProcess(processHandle, desiredAccess, objectAttributes, clientId)
