#include <iostream>
#include <string>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: Pe2Bin.exe <input.dll> <output.bin>" << std::endl;
        return 1;
    }

    std::string inputDll = argv[1];
    std::string outputBin = argv[2];
    
    // Path to the sRDI script relative to project root
    // Assuming Pe2Bin.exe is run from the project root
    std::string scriptPath = "ClientSide\\FormidableNim\\utils\\sRDI\\PowerShell\\ConvertTo-Shellcode.ps1";
    
    // Construct PowerShell command
    // We use [IO.File]::WriteAllBytes for better compatibility across PowerShell versions than Set-Content -Encoding Byte
    std::string command = "powershell -NoProfile -ExecutionPolicy Bypass -Command \"";
    command += "$ErrorActionPreference = 'Stop'; ";
    command += "try { ";
    command += ". .\\" + scriptPath + "; ";
    command += "[IO.File]::WriteAllBytes('" + outputBin + "', (ConvertTo-Shellcode -File '" + inputDll + "')); ";
    command += "} catch { Write-Error $_; exit 1 }";
    command += "\"";

    std::cout << "[Pe2Bin] Converting " << inputDll << " to shellcode " << outputBin << " using sRDI..." << std::endl;

    int result = system(command.c_str());

    if (result != 0) {
        std::cerr << "[Pe2Bin] Error: Conversion failed with exit code " << result << std::endl;
        return 1;
    }

    std::cout << "[Pe2Bin] Success!" << std::endl;
    return 0;
}
