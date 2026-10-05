@echo off

setlocal
cd /d "%~dp0"
chcp 65001 >nul

set compile_x64=1
set compile_x86=1
set compile_res=1

where x86_64-w64-mingw32-g++ >nul 2>nul
if errorlevel 1 (
    echo [信息] 未找到 x86_64-w64-mingw32-g++ 跳过64位编译。
    set compile_x64=0
)

where i686-w64-mingw32-g++ >nul 2>nul
if errorlevel 1 (
    echo [信息] 未找到 i686-w64-mingw32-g++ 跳过32位编译。
    set compile_x86=0
)

where windres >nul 2>nul
if errorlevel 1 (
    echo [信息] 未找到 windres 跳过资源编译。
    set compile_res=0
    set "RES_X86_ARG="
    set "RES_X64_ARG="
)

echo [1/3] 编译资源文件 windres -i res\ruime_tsf.rc ...
if %compile_res% equ 1 (
    windres -i res\ruime_tsf.rc -O coff -o res\ruime_tsf_x64.res -F pe-x86-64
    windres -i res\ruime_tsf.rc -O coff -o res\ruime_tsf_x86.res -F pe-i386
    if errorlevel 1 (
        echo [提示] 资源编译失败，将跳过图标继续编译（不影响功能）。
        set "RES_X86_ARG="
        set "RES_X64_ARG="
    ) else (
        set "RES_X86_ARG=res\ruime_tsf_x86.res"
        set "RES_X64_ARG=res\ruime_tsf_x64.res"
    )
) else (
    echo 跳过
)

echo [2/3] 编译 RuIME_x64.dll ...

if %compile_x64% equ 1 (
    x86_64-w64-mingw32-g++ -O2 -std=c++17 -Wall -shared -DUNICODE -D_UNICODE -finput-charset=UTF-8 src\ruime_tsf.cpp src\RuIME_x64.def %RES_X64_ARG% -o RuIME_x64.dll -luuid -lole32 -loleaut32 -ladvapi32 -static-libgcc -static-libstdc++

    if errorlevel 1 (
        echo.
        echo [错误] 编译失败，请检查上方错误信息。
        pause
        exit /b 1
    )
) else (
    echo 跳过
)


echo [3/3] 编译 RuIME_x86.dll ...
if %compile_x86% equ 1 (
    i686-w64-mingw32-g++ -O2 -std=c++17 -Wall -shared -DUNICODE -D_UNICODE -finput-charset=UTF-8 src\ruime_tsf.cpp src\RuIME_x86.def %RES_X86_ARG% -o RuIME_x86.dll -luuid -lole32 -loleaut32 -ladvapi32 -static-libgcc -static-libstdc++

    if errorlevel 1 (
        echo.
        echo [错误] 编译失败，请检查上方错误信息。
        pause
        exit /b 1
    )
) else (
    echo 跳过
)

echo.
echo [完成]
if %compile_x64% equ 1 (
    echo 生成 RuIME_x64.dll
)
if %compile_x86% equ 1 (
    echo 生成 RuIME_x86.dll
)
echo.
echo 下一步（需要管理员权限）：以管理员权限运行 install.bat
pause
