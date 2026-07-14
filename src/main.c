/* ============================================================
 * main.c - Application Entry Point
 * main.c - 应用程序入口点
 *
 * WinMain entry point for the Windows GUI application.
 * Initializes the format handler registry, registers all
 * built-in format handlers, and launches the GUI.
 * Windows GUI 应用程序的 WinMain 入口点。
 * 初始化格式处理器注册表，注册所有内置格式处理器，并启动 GUI。
 * ============================================================ */

#include "format_handler.h"  /* format_registry_init, format_register, etc. */
#include "json_handler.h"    /* JSON_HANDLER */
#include "txt_handler.h"     /* TXT_HANDLER */
#include "md_handler.h"      /* MD_HANDLER */
#include "gui.h"             /* gui_run */

#include <windows.h>         /* WinMain, HINSTANCE */

/* Application entry point for Windows GUI subsystem.
 * Windows GUI 子系统的应用程序入口点。
 *
 * The linker uses -mwindows flag to indicate this is a GUI app
 * (no console window). WinMain is the standard entry point
 * for such applications.
 * 链接器使用 -mwindows 标志表示这是一个 GUI 应用（无控制台窗口）。
 * WinMain 是此类应用的标准入口点。
 *
 * Startup sequence 启动顺序:
 *   1. Initialize the format handler registry 初始化格式处理器注册表
 *   2. Register JSON, TXT, MD format handlers 注册 JSON、TXT、MD 处理器
 *   3. Launch the GUI main loop 启动 GUI 主循环
 *   4. Cleanup on exit 退出时清理                                  */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    /* 抑制未使用参数的编译器警告 */
    (void)hPrevInstance;
    (void)lpCmdLine;

    /* --- Step 1: Initialize format handler registry ---
     * 步骤 1：初始化格式处理器注册表                          */
    if (!format_registry_init()) {
        /* 注册表初始化失败 —— 这是一个致命错误。
         * 在 GUI 应用中无法显示错误消息（因为窗口还没创建），
         * 使用 MessageBoxA 进行早期错误报告。                   */
        MessageBoxA(NULL,
                    "Failed to initialize format handler registry.",
                    "Mind Map Tool - Fatal Error",
                    MB_OK | MB_ICONERROR);
        return 1;  /* 非零退出码表示错误 */
    }

    /* --- Step 2: Register all built-in format handlers ---
     * 步骤 2：注册所有内置格式处理器                          */

    /* 注册 JSON 格式处理器（支持 .json 文件） */
    if (!format_register(&JSON_HANDLER)) {
        MessageBoxA(NULL,
                    "Failed to register JSON format handler.",
                    "Mind Map Tool - Fatal Error",
                    MB_OK | MB_ICONERROR);
        format_registry_shutdown();
        return 1;
    }

    /* 注册 TXT 格式处理器（支持 .txt 编号大纲文件） */
    if (!format_register(&TXT_HANDLER)) {
        MessageBoxA(NULL,
                    "Failed to register TXT format handler.",
                    "Mind Map Tool - Fatal Error",
                    MB_OK | MB_ICONERROR);
        format_registry_shutdown();
        return 1;
    }

    /* 注册 MD 格式处理器（支持 .md Markdown 文件） */
    if (!format_register(&MD_HANDLER)) {
        MessageBoxA(NULL,
                    "Failed to register MD format handler.",
                    "Mind Map Tool - Fatal Error",
                    MB_OK | MB_ICONERROR);
        format_registry_shutdown();
        return 1;
    }

    /* --- Step 3: Launch the GUI ---
     * 步骤 3：启动 GUI                                        */
    /* gui_run 包含主消息循环，直到用户关闭窗口后才返回。
     * gui_run 的返回值是退出码。                              */
    int exit_code = gui_run(hInstance, nCmdShow);

    /* --- Step 4: Cleanup ---
     * 步骤 4：清理                                            */
    /* 关闭注册表，释放所有内部资源 */
    format_registry_shutdown();

    /* 返回退出码（0 表示正常退出） */
    return exit_code;
}
