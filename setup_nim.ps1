# Formidable2026 Nim Environment Setup Script
# Automatically downloads and configures Nim and Winim

$ErrorActionPreference = "Stop"

# Configuration
$NIM_VERSION = "2.2.0"
# Use explicit string formatting to avoid interpolation issues
$NIM_URL = "https://nim-lang.org/download/nim-{0}_x64.zip" -f $NIM_VERSION

# Use absolute paths based on the current script location or current directory
$ROOT_DIR = $PSScriptRoot
if ([string]::IsNullOrEmpty($ROOT_DIR)) { $ROOT_DIR = Get-Location }

$TOOLS_DIR = Join-Path $ROOT_DIR "tools"
$NIM_DIR = Join-Path $TOOLS_DIR "nim-$NIM_VERSION"
$NIM_BIN = Join-Path $NIM_DIR "bin"
$ZIP_PATH = Join-Path $TOOLS_DIR "nim.zip"

Write-Host "=========================================="
Write-Host "Formidable2026 Nim Setup"
Write-Host "=========================================="
Write-Host "Debug: Tools Dir: $TOOLS_DIR"
Write-Host "Debug: Zip Path:  $ZIP_PATH"
Write-Host "Debug: Nim URL:   $NIM_URL"

# 1. Create tools directory
if (-not (Test-Path $TOOLS_DIR)) {
    Write-Host "Creating tools directory..."
    New-Item -ItemType Directory -Path $TOOLS_DIR | Out-Null
}

# 2. Download and Install Nim
if (-not (Test-Path $NIM_BIN)) {
    # Download
    if (-not (Test-Path $ZIP_PATH)) {
        Write-Host "Downloading Nim $NIM_VERSION..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        try {
            Invoke-WebRequest -Uri $NIM_URL -OutFile $ZIP_PATH
        } catch {
            Write-Error "Download failed: $_"
            exit 1
        }
    } else {
        Write-Host "Found existing nim.zip"
    }
    
    # Extract
    Write-Host "Extracting Nim..."
    
    # Try using tar (Windows 10/11 built-in) first as it's often more reliable
    $tarPath = Get-Command "tar.exe" -ErrorAction SilentlyContinue
    if ($tarPath) {
        Write-Host "Using system tar..."
        try {
            # tar -xf nim.zip -C tools/
            Push-Location $TOOLS_DIR
            tar -xf "nim.zip"
            Pop-Location
        } catch {
            Write-Warning "Tar extraction failed, falling back to Expand-Archive..."
            Expand-Archive -Path $ZIP_PATH -DestinationPath $TOOLS_DIR -Force
        }
    } else {
        Write-Host "Using Expand-Archive..."
        Expand-Archive -Path $ZIP_PATH -DestinationPath $TOOLS_DIR -Force
    }

    # Verify extraction
    if (-not (Test-Path $NIM_BIN)) {
        Write-Error "Extraction failed: bin directory not found at $NIM_BIN"
        Write-Host "Please check if the zip file was downloaded correctly."
        exit 1
    }
    
    # Cleanup
    # Remove-Item $ZIP_PATH -Force
    Write-Host "Nim installed successfully."
} else {
    Write-Host "Nim already installed at $NIM_DIR"
}

# 3. Add to PATH (Current Session)
if ($env:PATH -notlike "*$NIM_BIN*") {
    Write-Host "Adding Nim to PATH (Current Session)..."
    $env:PATH = "$NIM_BIN;$env:PATH"
}

# 4. Verify Nim
try {
    $nimExe = Join-Path $NIM_BIN "nim.exe"
    $version = & $nimExe --version
    Write-Host "Nim Version Check: OK"
} catch {
    Write-Error "Failed to run Nim. Please check installation."
    exit 1
}

# 5. Install Winim
Write-Host "Installing/Updating Winim library..."
try {
    $nimbleExe = Join-Path $NIM_BIN "nimble.exe"
    & $nimbleExe install winim -y
} catch {
    Write-Warning "Failed to install winim automatically: $_"
    Write-Host "You may need to run: nimble install winim -y manually."
}

Write-Host "=========================================="
Write-Host "Setup Complete!"
Write-Host "You can now compile FormidableNim."
Write-Host "=========================================="
