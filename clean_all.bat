@echo off
chcp 65001 >nul
echo ========================================================
echo  Formidable2026 深度清理脚本
echo ========================================================
echo.
echo 注意: 这将删除所有编译生成的文件，包括:
echo   1. Formidable2026\x86 目录
echo   2. Formidable2026\x64 目录
echo   3. 所有中间文件 (Intermediate)
echo.
echo 按任意键继续，或关闭窗口取消...
pause >nul

echo.
echo [1/4] 清理 x86 输出目录...
if exist "Formidable2026\x86" (
    rmdir /s /q "Formidable2026\x86"
    echo    已删除 Formidable2026\x86
) else (
    echo    Formidable2026\x86 不存在，跳过
)

echo.
echo [2/4] 清理 x64 输出目录...
if exist "Formidable2026\x64" (
    rmdir /s /q "Formidable2026\x64"
    echo    已删除 Formidable2026\x64
) else (
    echo    Formidable2026\x64 不存在，跳过
)

echo.
echo [3/4] 清理中间文件...
REM 遍历删除所有 Intermediate 目录
for /d /r %%d in (Intermediate) do (
    if exist "%%d" (
        echo    删除: %%d
        rmdir /s /q "%%d"
    )
)
REM 遍历删除所有 .vs 目录 (Visual Studio 缓存)
if exist ".vs" (
    echo    删除: .vs 缓存
    rmdir /s /q ".vs"
)

echo.
echo [4/4] 清理生成的临时文件...
del /q /s *.aps 2>nul
del /q /s *.idb 2>nul
del /q /s *.pdb 2>nul
del /q /s *.ilk 2>nul

echo.
echo ========================================================
echo  清理完成! 现在可以重新生成解决方案。
echo ========================================================
pause
