@echo off
setlocal EnableDelayedExpansion

:: 检查是否以管理员权限运行
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ========================================================
    echo [错误] 请右键点击本脚本，选择“以管理员身份运行”！
    echo ========================================================
    pause
    exit /b 1
)

echo ========================================================
echo Formidable2026 开发环境一键配置工具
echo ========================================================
echo.

:: 获取当前脚本所在目录作为项目根目录
set "PROJECT_ROOT=%~dp0"
:: 去除末尾的反斜杠
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

echo [INFO] 项目根目录: %PROJECT_ROOT%

:: 设置 Nim 编译器路径
set "NIM_PATH=%PROJECT_ROOT%\tools\nim-2.2.0\bin"

echo [INFO] 正在配置 Nim 环境变量...
echo        目标路径: %NIM_PATH%

:: 检查路径是否存在
if not exist "%NIM_PATH%\nim.exe" (
    echo [ERROR] 无法找到 Nim 编译器！
    echo         请确保 tools\nim-2.2.0\bin\nim.exe 存在。
    echo         如果你刚克隆了项目，请确保 git lfs pull (如果有) 或检查文件完整性。
    pause
    exit /b 1
)

:: 添加到系统环境变量 Path (永久生效)
:: 使用 PowerShell 处理路径添加，避免重复和长度限制问题
powershell -Command "$oldPath = [Environment]::GetEnvironmentVariable('Path', 'Machine'); if ($oldPath -notlike '*%NIM_PATH%*') { [Environment]::SetEnvironmentVariable('Path', $oldPath + ';%NIM_PATH%', 'Machine'); Write-Host '[SUCCESS] 已成功将 Nim 添加到系统 Path 环境变量。' } else { Write-Host '[INFO] Nim 路径已存在于环境变量中，无需重复添加。' }"

:: 验证 Nim 是否可用
echo.
echo [INFO] 正在验证环境...
:: 刷新当前会话的环境变量以便测试
set "PATH=%PATH%;%NIM_PATH%"

nim --version >nul 2>&1
if %errorLevel% equ 0 (
    nim --version | findstr "Nim Compiler"
    echo.
    echo ========================================================
    echo [SUCCESS] 环境配置成功！
    echo ========================================================
    echo 你现在可以直接编译项目了。
    echo 注意：你可能需要重启 Visual Studio 或命令行窗口以使环境变量生效。
) else (
    echo [ERROR] 环境变量设置后验证失败。请尝试重启电脑。
)

echo.
pause
