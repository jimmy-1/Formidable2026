@echo off
REM Build HP-Socket Library
echo Building HP-Socket...

call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64

cd /d "%~dp0"
msbuild thirdparty\HP-Socket\Windows\HPSocketLIB.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /v:minimal /nologo

if %ERRORLEVEL% EQU 0 (
    echo.
    echo HP-Socket build SUCCESS!
) else (
    echo.
    echo HP-Socket build FAILED!
)

pause
