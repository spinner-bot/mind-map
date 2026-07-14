#!/bin/bash
# ============================================================
# build.sh - Mind Map Tool build script for Git Bash + MSVC
# 在 Git Bash 中使用 MSVC 编译器构建项目
# ============================================================

set -e

# --- Paths ---
VCINSTALLDIR="D:/VisualStudio/Packages/VC/Tools/MSVC/14.44.35207"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKVER="10.0.26100.0"

CL="$VCINSTALLDIR/bin/Hostx64/x64/cl.exe"
LINK="$VCINSTALLDIR/bin/Hostx64/x64/link.exe"

# --- Prevent MSYS from mangling /flag arguments ---
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

# --- Set include and lib paths ---
export INCLUDE="$VCINSTALLDIR/include;$SDK/Include/$SDKVER/um;$SDK/Include/$SDKVER/ucrt;$SDK/Include/$SDKVER/shared"
export LIB="$VCINSTALLDIR/lib/x64;$SDK/Lib/$SDKVER/um/x64;$SDK/Lib/$SDKVER/ucrt/x64"

# --- Directories ---
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_DIR"
mkdir -p obj

# --- Common compile flags ---
CFLAGS="/nologo /utf-8 /c /Iinclude /Foobj/"

# --- Common link libraries ---
LIBS="ole32.lib comctl32.lib comdlg32.lib shell32.lib gdi32.lib user32.lib kernel32.lib shlwapi.lib"

echo "============================================================"
echo "  Building Mind Map Conversion Tool"
echo "  构建 Mind Map 转换工具"
echo "============================================================"

# --- Compile ---
echo ""
echo "[1/2] Compiling... 编译中..."
"$CL" $CFLAGS \
    src/main.c \
    src/tree.c \
    src/utils.c \
    src/encoding.c \
    src/format_handler.c \
    src/json_handler.c \
    src/txt_handler.c \
    src/md_handler.c \
    src/converter.c \
    src/gui.c \
    src/i18n.c 2>&1 | grep -E "error|warning|$" || true

# Check if obj files exist
OBJFILES=""
for f in main tree utils encoding format_handler json_handler txt_handler md_handler converter gui i18n; do
    if [ ! -f "obj/${f}.obj" ]; then
        echo "[ERROR] Compilation failed: obj/${f}.obj not found"
        exit 1
    fi
    OBJFILES="$OBJFILES obj/${f}.obj"
done

# --- Link ---
echo ""
echo "[2/2] Linking... 链接中..."
"$LINK" /nologo /SUBSYSTEM:WINDOWS $OBJFILES $LIBS /OUT:mind_map.exe 2>&1

# --- Done ---
if [ -f "mind_map.exe" ]; then
    echo ""
    echo "============================================================"
    echo "  Build successful! 构建成功！"
    echo "  Output: mind_map.exe ($(du -h mind_map.exe | cut -f1))"
    echo "============================================================"
else
    echo ""
    echo "[ERROR] Linking failed - mind_map.exe not created"
    exit 1
fi
