@echo off
REM ============================================================
REM build.bat - Mind Map Tool Build Script for MSVC
REM 自动检测 VS 环境，无需手动打开 Developer Command Prompt
REM ============================================================

setlocal enabledelayedexpansion

REM --- Auto-detect and set up VC environment 自动检测 VS 环境 ---
if not defined VCINSTALLDIR (
    echo [INFO] Setting up Visual Studio environment...
    echo [信息] 正在配置 Visual Studio 环境...

    REM Try to find vcvars64.bat via vswhere
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do (
        set "VS_PATH=%%i"
    )

    REM Also check the known path
    if not defined VS_PATH (
        if exist "D:\VisualStudio\Packages\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_PATH=D:\VisualStudio\Packages"
        )
    )

    if defined VS_PATH (
        set "VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!VCVARS!" (
            call "!VCVARS!" >nul 2>&1
            echo [INFO] VS environment loaded from !VS_PATH!
        ) else (
            echo [WARN] vcvars64.bat not found at !VCVARS!
            echo [警告] 未找到 vcvars64.bat，请手动打开 Developer Command Prompt for VS
        )
    ) else (
        echo [WARN] Visual Studio not detected.
        echo [警告] 未检测到 Visual Studio，请手动打开 Developer Command Prompt for VS
    )
)

REM --- Configuration 配置 ---
set "TARGET=mind_map.exe"
set "SRCDIR=src"
set "INCDIR=include"
set "OBJDIR=obj"

REM --- Create obj directory 创建 obj 目录 ---
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM --- Compiler flags 编译器标志 ---
REM /TC        : 强制所有文件按 C 编译 (不是 C++)
REM /nologo    : 不显示版权信息
REM /W4        : 高警告级别
REM /I         : 包含路径
REM /D_CRT_SECURE_NO_WARNINGS : 关闭安全函数警告
set "CFLAGS=/nologo /W4 /I%INCDIR% /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE"

REM --- Linker flags 链接器标志 ---
REM /SUBSYSTEM:WINDOWS : GUI 应用（无控制台窗口）
REM comctl32.lib       : ListView、ProgressBar 等通用控件
REM comdlg32.lib       : 文件打开/保存对话框
REM shell32.lib        : 浏览文件夹对话框
set "LDFLAGS=/SUBSYSTEM:WINDOWS comctl32.lib comdlg32.lib shell32.lib gdi32.lib user32.lib kernel32.lib shlwapi.lib"

REM --- Source files 源文件列表 ---
set "SOURCES=%SRCDIR%\main.c %SRCDIR%\tree.c %SRCDIR%\utils.c %SRCDIR%\encoding.c %SRCDIR%\format_handler.c %SRCDIR%\json_handler.c %SRCDIR%\txt_handler.c %SRCDIR%\md_handler.c %SRCDIR%\converter.c %SRCDIR%\gui.c"

echo ============================================================
echo  Building Mind Map Conversion Tool
echo  构建 Mind Map 转换工具
echo ============================================================

REM --- Compile step: each .c -> .obj ---
echo.
echo [1/2] Compiling source files...
echo [1/2] 编译源文件...

set "OBJFILES="
for %%f in (%SOURCES%) do (
    set "OBJFILE=%OBJDIR%\%%~nf.obj"
    set "OBJFILES=!OBJFILES! !OBJFILE!"
    echo   Compiling %%~nxf...
    cl /c %CFLAGS% /Fo"!OBJFILE!" "%%f" 2>&1
    if errorlevel 1 (
        echo.
        echo [ERROR] Compilation failed for %%~nxf
        echo [错误] 编译失败: %%~nxf
        goto :error
    )
)

REM --- Link step: all .obj -> .exe ---
echo.
echo [2/2] Linking...
echo [2/2] 链接...

link /nologo %OBJFILES% /OUT:"%TARGET%" %LDFLAGS%
if errorlevel 1 (
    echo.
    echo [ERROR] Linking failed
    echo [错误] 链接失败
    goto :error
)

REM --- Success ---
echo.
echo ============================================================
echo   Build successful! 构建成功！
echo   Output 输出: %TARGET%
echo ============================================================
goto :end

:error
echo.
echo ============================================================
echo   Build FAILED! 构建失败！
echo ============================================================
exit /b 1

:end
endlocal
