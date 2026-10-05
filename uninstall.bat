@echo off
chcp 65001 >nul

net session >nul 2>nul
if errorlevel 1 (
    echo 请以管理员身份运行此脚本。
    pause
    exit /b 1
)

setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "PF64=%ProgramFiles%"
set "PF86=%ProgramFiles(x86)%"
set "CLSID={9A7B3C5D-1E4F-4A68-B2C9-7D3E8F1A6B42}"
set "TIP_KEY=HKLM\SOFTWARE\Microsoft\CTF\TIP\%CLSID%"
set "TIP_KEY_WOW=HKLM\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\%CLSID%"
set "CLSID_KEY=HKCR\CLSID\%CLSID%"

for /f "delims=" %%i in ('powershell -NoProfile -Command "(Get-Date).ToString('"'yyyyMMdd_HHmmss'"')"') do set "TS=%%i"

echo 正在创建备份目录 backups\ ...
if not exist backups mkdir backups

echo.
echo 1) 尝试反注册 DLL（如果存在）...
if exist "!PF64!\RuIME\RuIME_x64.dll" (
    echo 反注册 !PF64!\RuIME\RuIME_x64.dll ...
    regsvr32 /u /s "!PF64!\RuIME\RuIME_x64.dll" || echo 注意：反注册 x64 DLL 返回非零。
) else (
    echo 未在 !PF64! 找到 RuIME_x64.dll，跳过。
)

if exist "!PF86!\RuIME\RuIME_x86.dll" (
    echo 反注册 !PF86!\RuIME\RuIME_x86.dll ...
    regsvr32 /u /s "!PF86!\RuIME\RuIME_x86.dll" || echo 注意：反注册 x86 DLL 返回非零。
) else (
    echo 未在 !PF86! 找到 RuIME_x86.dll，跳过。
)

echo.
echo 2) 导出相关注册表键作为备份（放在 backups\）...
if exist "backups" (
    reg query "%CLSID_KEY%" >nul 2>nul
    if not errorlevel 1 (
        reg export "%CLSID_KEY%" "backups\clsid_%TS%.reg" /y >nul 2>nul && echo 已备份 %CLSID_KEY% -> backups\clsid_%TS%.reg
    ) else echo 未发现 %CLSID_KEY%，无需备份。

    reg query "%TIP_KEY%" >nul 2>nul
    if not errorlevel 1 (
        reg export "%TIP_KEY%" "backups\tip_%TS%.reg" /y >nul 2>nul && echo 已备份 %TIP_KEY% -> backups\tip_%TS%.reg
    ) else echo 未发现 %TIP_KEY%，无需备份。

    reg query "%TIP_KEY_WOW%" >nul 2>nul
    if not errorlevel 1 (
        reg export "%TIP_KEY_WOW%" "backups\tip_wow_%TS%.reg" /y >nul 2>nul && echo 已备份 %TIP_KEY_WOW% -> backups\tip_wow_%TS%.reg
    ) else echo 未发现 %TIP_KEY_WOW%，无需备份。
) else (
    echo 无法创建 backups 目录，跳过备份。
)

echo.
echo 3) 删除注册表键（谨慎操作）...

echo 删除 %CLSID_KEY% ...
reg delete "%CLSID_KEY%" /f >nul 2>nul && echo 已删除 %CLSID_KEY% || echo 未删除（可能不存在或权限不足）

echo 删除 %TIP_KEY% ...
reg delete "%TIP_KEY%" /f >nul 2>nul && echo 已删除 %TIP_KEY% || echo 未删除（可能不存在或权限不足）

echo 删除 %TIP_KEY_WOW% ...
reg delete "%TIP_KEY_WOW%" /f >nul 2>nul && echo 已删除 %TIP_KEY_WOW% || echo 未删除（可能不存在或权限不足）

echo 尝试清理可能的子键（如果存在）...
reg delete "HKLM\SOFTWARE\Microsoft\CTF\TIP\%CLSID%" /f >nul 2>nul
reg delete "HKCR\Interface\{%CLSID%}" /f >nul 2>nul

echo.
echo 4) 删除安装目录...
if exist "!PF64!\RuIME" (
    echo 删除 !PF64!\RuIME ...
    rmdir /s /q "!PF64!\RuIME" && echo 已删除 !PF64!\RuIME || echo 删除失败（请检查权限/文件锁定）
) else echo 未在 !PF64! 找到安装目录，跳过。

if exist "!PF86!\RuIME" (
    echo 删除 !PF86!\RuIME ...
    rmdir /s /q "!PF86!\RuIME" && echo 已删除 !PF86!\RuIME || echo 删除失败（请检查权限/文件锁定）
) else echo 未在 !PF86! 找到安装目录，跳过。

echo.
echo 卸载步骤已完成（尝试）。
echo 建议：重启系统或注销并重新登录以使输入法列表更新。
echo 备份文件（如果有）保存在： %~dp0backups\

echo.
pause
endlocal
