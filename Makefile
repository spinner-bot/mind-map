# ============================================================
# Mind Map Conversion Tool - Build System
# 思维导图多格式转换工具 - 构建系统
#
# Target: mind_map.exe (Windows GUI application, no console window)
# 目标: mind_map.exe (Windows 图形界面程序，无控制台窗口)
#
# Usage 用法:
#   make all   - Build the project 构建项目
#   make clean - Remove build artifacts 清理构建产物
#   make run   - Build and launch 构建并运行
#   make help  - Show this help 显示帮助
# ============================================================

# --- Compiler and flags 编译器与标志 ---
CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -mwindows -I./include
LDFLAGS  = -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lkernel32 -lshell32 -lshlwapi
TARGET   = mind_map.exe

# --- Directories 目录 ---
SRCDIR   = src
OBJDIR   = obj
INCDIR   = include

# --- Source files and object files 源文件与目标文件 ---
SOURCES  = $(wildcard $(SRCDIR)/*.c)
OBJECTS  = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SOURCES))

# --- Default target 默认目标 ---
.PHONY: all clean run help

all: $(TARGET)

# --- Link step: produce the Windows GUI executable 链接步骤：生成 Windows GUI 可执行文件 ---
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# --- Compile step: each .c -> .o 编译步骤：每个 .c 编译为 .o ---
# Create obj/ directory if it doesn't exist 如果 obj/ 目录不存在则创建
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Ensure obj/ directory exists 确保 obj/ 目录存在 ---
$(OBJDIR):
	mkdir -p $(OBJDIR)

# --- Convenience: build and launch 便捷目标：构建并运行 ---
run: $(TARGET)
	./$(TARGET)

# --- Clean build artifacts 清理构建产物 ---
clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)

# --- Show help 显示帮助 ---
help:
	@echo "Mind Map Conversion Tool - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build mind_map.exe (default)"
	@echo "  clean  - Remove build artifacts (obj/ and .exe)"
	@echo "  run    - Build and run the application"
	@echo "  help   - Show this help message"
