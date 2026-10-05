@echo off
chcp 65001 >nul

net session >nul 2>nul
if errorlevel 1 (
    echo 请以管理员身份运行
    pause
    exit /b 1
)

setlocal EnableDelayedExpansion
cd /d "%~dp0"
set "PF64=C:\Program Files"
set "PF86=C:\Program Files (x86)"
set "OK=0"
set "FAIL=0"

if EXIST "RuIME_x64.dll" (
    echo 正在安装 RuIME x64 ...

    if NOT EXIST "!PF64!\RuIME" (
        mkdir "!PF64!\RuIME"
        if errorlevel 1 (
            echo [错误] 无法创建目录 !PF64!\RuIME
            set "FAIL=1"
        )
    )

    copy /y "RuIME_x64.dll" "!PF64!\RuIME\RuIME_x64.dll" >nul
    if errorlevel 1 (
        echo [错误] 复制 RuIME_x64.dll 失败（可能文件被占用？）
        set "FAIL=1"
    ) else (
        "C:\Windows\System32\regsvr32.exe" /s "!PF64!\RuIME\RuIME_x64.dll"
        if errorlevel 1 (
            echo [错误] regsvr32 注册 RuIME_x64.dll 失败
            set "FAIL=1"
        ) else (
            echo [成功] RuIME x64 安装完成
            set "OK=1"
        )
    )
)

if EXIST "RuIME_x86.dll" (
    echo 正在安装 RuIME x86 ...

    if NOT EXIST "!PF86!\RuIME" (
        mkdir "!PF86!\RuIME"
        if errorlevel 1 (
            echo [错误] 无法创建目录 !PF86!\RuIME
            set "FAIL=1"
        )
    )

    copy /y "RuIME_x86.dll" "!PF86!\RuIME\RuIME_x86.dll" >nul
    if errorlevel 1 (
        echo [错误] 复制 RuIME_x86.dll 失败（可能文件被占用？）
        set "FAIL=1"
    ) else (
        "C:\Windows\SysWOW64\regsvr32.exe" /s "!PF86!\RuIME\RuIME_x86.dll"
        if errorlevel 1 (
            echo [错误] regsvr32 注册 RuIME_x86.dll 失败
            set "FAIL=1"
        ) else (
            echo [成功] RuIME x86 安装完成
            set "OK=1"
        )
    )
)

echo.
if "%FAIL%"=="1" (
    echo [结果] 部分步骤失败，请检查上面的 [错误] 行。
) else if "%OK%"=="1" (
    echo [结果] 安装完成。
) else (
    echo [结果] 未找到任何 DLL，什么都没做。
)

pause