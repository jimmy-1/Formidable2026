import os
import re

def extract_stub(py_file, header_file):
    print(f"Reading {py_file}...")
    with open(py_file, "r", encoding="utf-8") as f:
        content = f.read()
    
    lines = content.splitlines()
    print(f"Total lines: {len(lines)}")
    
    rdi64 = None
    rdi32 = None

    for line in lines:
        stripped_line = line.strip()
        if "rdiShellcode64 =" in line:
            print(f"Found line with rdiShellcode64 (len: {len(line)})")
            local_scope = {}
            try:
                exec(stripped_line, {}, local_scope)
                rdi64 = local_scope.get("rdiShellcode64")
                if rdi64:
                    print(f"Extracted {len(rdi64)} bytes of 64-bit shellcode.")
            except Exception as e:
                print(f"[-] Exec failed for 64-bit: {e}")
        
        elif "rdiShellcode32 =" in line:
            print(f"Found line with rdiShellcode32 (len: {len(line)})")
            local_scope = {}
            try:
                exec(stripped_line, {}, local_scope)
                rdi32 = local_scope.get("rdiShellcode32")
                if rdi32:
                    print(f"Extracted {len(rdi32)} bytes of 32-bit shellcode.")
            except Exception as e:
                print(f"[-] Exec failed for 32-bit: {e}")

    if rdi64 and rdi32:
        write_header(rdi64, rdi32, header_file)
    else:
        print("[-] Failed to find both shellcodes.")

def write_header(data64, data32, filename):
    print(f"Writing to {filename}...")
    try:
        with open(filename, "w") as f:
            f.write("#pragma once\n\n")
            
            # Write 64-bit
            f.write("static const unsigned char rdiShellcode64[] = {\n")
            for i, byte in enumerate(data64):
                f.write(f"0x{byte:02X}, ")
                if (i + 1) % 16 == 0:
                    f.write("\n")
            f.write("\n};\n")
            f.write(f"static const unsigned int rdiShellcode64_len = {len(data64)};\n\n")

            # Write 32-bit
            f.write("static const unsigned char rdiShellcode32[] = {\n")
            for i, byte in enumerate(data32):
                f.write(f"0x{byte:02X}, ")
                if (i + 1) % 16 == 0:
                    f.write("\n")
            f.write("\n};\n")
            f.write(f"static const unsigned int rdiShellcode32_len = {len(data32)};\n")

        print("Write successful.")
    except Exception as e:
        print(f"[-] Write failed: {e}")

if __name__ == "__main__":
    if not os.path.exists("tools"):
        os.makedirs("tools")
    
    py_path = os.path.join("utils", "sRDI", "Python", "ShellcodeRDI.py")
    if not os.path.exists(py_path):
        print(f"[-] {py_path} does not exist!")
    else:
        extract_stub(py_path, "tools/ShellcodeRDI.h")
