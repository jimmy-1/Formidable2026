@echo off
REM ============================================
REM Formidable2026 DLL 部署脚本
REM 自动复制并重命名第三方 DLL 到输出目录
REM ============================================

set THIRDPARTY=%~dp0thirdparty\Bin
set OUT_X86=%~dp0Formidable2026\x86
set OUT_X64=%~dp0Formidable2026\x64
set MODULE_X86=%~dp0Modules\Multimedia\x86
set MODULE_X64=%~dp0Modules\Multimedia\x64

echo ============================================
echo  Formidable2026 DLL 部署工具
echo ============================================
echo.

REM 创建输出目录（如果不存在）
if not exist "%OUT_X86%" mkdir "%OUT_X86%"
if not exist "%OUT_X64%" mkdir "%OUT_X64%"
if not exist "%MODULE_X86%" mkdir "%MODULE_X86%"
if not exist "%MODULE_X64%" mkdir "%MODULE_X64%"

echo [1/4] 部署主控端 x64 DLL...
REM 主控端 (x64) - FFmpeg DLL
copy /Y "%THIRDPARTY%\avcodec-62.dll" "%OUT_X64%\" >nul 2>&1
copy /Y "%THIRDPARTY%\avutil-60.dll" "%OUT_X64%\" >nul 2>&1
copy /Y "%THIRDPARTY%\swscale-9.dll" "%OUT_X64%\" >nul 2>&1
echo    已复制 FFmpeg DLL 到 %OUT_X64%

echo [2/4] 部署被控端模块 x86 DLL...
REM 被控端模块 (x86) - TurboJPEG + LAME
copy /Y "%THIRDPARTY%\turbojpeg_x86.dll" "%MODULE_X86%\turbojpeg.dll" >nul 2>&1
copy /Y "%THIRDPARTY%\libmp3lame_x86.dll" "%MODULE_X86%\libmp3lame.dll" >nul 2>&1
copy /Y "%THIRDPARTY%\zlibwapi_x86.dll" "%MODULE_X86%\zlibwapi.dll" >nul 2>&1
echo    已复制并重命名 x86 DLL 到 %MODULE_X86%

echo [3/4] 部署被控端模块 x64 DLL...
REM 被控端模块 (x64) - TurboJPEG + LAME
copy /Y "%THIRDPARTY%\turbojpeg_x64.dll" "%MODULE_X64%\turbojpeg.dll" >nul 2>&1
copy /Y "%THIRDPARTY%\libmp3lame_x64.dll" "%MODULE_X64%\libmp3lame.dll" >nul 2>&1
copy /Y "%THIRDPARTY%\zlibwapi_x64.dll" "%MODULE_X64%\zlibwapi.dll" >nul 2>&1
echo    已复制并重命名 x64 DLL 到 %MODULE_X64%

echo [4/4] 复制 FRP 工具...
REM FRP 内网穿透工具
copy /Y "%THIRDPARTY%\frpc.exe" "%OUT_X64%\" >nul 2>&1
copy /Y "%THIRDPARTY%\frpc.toml" "%OUT_X64%\" >nul 2>&1
copy /Y "%THIRDPARTY%\frps.exe" "%OUT_X64%\" >nul 2>&1
copy /Y "%THIRDPARTY%\frps.toml" "%OUT_X64%\" >nul 2>&1
echo    已复制 FRP 工具到 %OUT_X64%

echo.
echo ============================================
echo  部署完成!
echo ============================================
echo.
echo 输出目录:
echo   主控端: %OUT_X64%
echo   模块x86: %MODULE_X86%
echo   模块x64: %MODULE_X64%
echo.
pause
