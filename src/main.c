/* ============================================================
 * main.c - Application Entry Point
 * main.c - 应用程序入口点
 *
 * Phase 2: HTTP server mode (default). The C backend starts a
 * lightweight HTTP server on localhost:8080, serves the web
 * frontend, and provides a JSON API for all operations.
 * Phase 1 Win32 GUI mode is preserved and can be toggled.
 * 二期：HTTP 服务器模式（默认）。C 后端在 localhost:8080 启动
 * 轻量级 HTTP 服务器，提供 Web 前端和 JSON API。
 * 一期 Win32 GUI 模式保留，可通过命令行切换。
 *
 * Startup sequence 启动顺序:
 *   1. Initialize format handler registry 初始化格式处理器注册表
 *   2. Register all format handlers 注册所有格式处理器
 *   3. Initialize and start HTTP server 初始化并启动 HTTP 服务器
 *   4. Cleanup on exit 退出时清理
 * ============================================================ */

#include "format_handler.h"  /* format_registry_init, format_register, etc. */
#include "json_handler.h"    /* JSON_HANDLER */
#include "txt_handler.h"     /* TXT_HANDLER */
#include "md_handler.h"      /* MD_HANDLER */
#include "lxmm_handler.h"    /* LXMM_HANDLER — Phase 2 binary format */
#include "i18n.h"            /* i18n_init, _() */
#include "tree.h"            /* tree_create, tree_free */
#include "server.h"          /* server_init, server_start, server_cleanup */

#include <stdio.h>           /* printf, fprintf */
#include <windows.h>         /* WinMain, HINSTANCE, AllocConsole */

/* Phase 1 GUI entry point (preserved for compatibility).
 * 一期 GUI 入口点（保留以兼容）。                                   */
int gui_run(HINSTANCE hInstance, int nCmdShow);

/* Application entry point. Console subsystem for server mode.
 * 应用程序入口点。控制台子系统，服务器模式。                       */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    /* 设置控制台输出编码为 UTF-8，避免中文乱码。
     * Set console output code page to UTF-8 to prevent garbled text. */
    SetConsoleOutputCP(CP_UTF8);

    printf("Mind Map Tool v2.0 — Phase 2 Server Mode\n");
    printf("思维导图工具 v2.0 — 二期服务器模式\n\n");

    /* --- Step 1: Initialize format handler registry --- */
    if (!format_registry_init()) {
        fprintf(stderr, "FATAL: Failed to initialize format handler registry.\n");
        system("pause");
        return 1;
    }
    printf("[OK] Format handler registry initialized\n");

    /* --- Step 2: Register all format handlers --- */
    if (!format_register(&JSON_HANDLER)) {
        fprintf(stderr, "FATAL: Failed to register JSON handler.\n");
        format_registry_shutdown();
        system("pause");
        return 1;
    }
    printf("[OK] Registered: JSON handler\n");

    if (!format_register(&TXT_HANDLER)) {
        fprintf(stderr, "FATAL: Failed to register TXT handler.\n");
        format_registry_shutdown();
        system("pause");
        return 1;
    }
    printf("[OK] Registered: TXT handler\n");

    if (!format_register(&MD_HANDLER)) {
        fprintf(stderr, "FATAL: Failed to register MD handler.\n");
        format_registry_shutdown();
        system("pause");
        return 1;
    }
    printf("[OK] Registered: MD handler\n");

    if (!format_register(&LXMM_HANDLER)) {
        fprintf(stderr, "FATAL: Failed to register LXMM handler.\n");
        format_registry_shutdown();
        system("pause");
        return 1;
    }
    printf("[OK] Registered: LXMM handler\n");

    /* Check for --gui flag to launch Phase 1 GUI mode.
     * 检查 --gui 标志启动一期 GUI 模式。                          */
    {
        int gui_mode = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--gui") == 0) { gui_mode = 1; break; }
        }
        if (gui_mode) {
            printf("\nLaunching Phase 1 GUI mode...\n");
            i18n_init();
            int exit_code = gui_run(GetModuleHandle(NULL), SW_SHOWDEFAULT);
            format_registry_shutdown();
            return exit_code;
        }
    }

    /* --- Step 3: Initialize and start HTTP server --- */
    printf("\n--- Starting HTTP Server ---\n");
    printf("Initializing server on http://localhost:%d ...\n", SERVER_PORT);

    if (!server_init()) {
        fprintf(stderr, "FATAL: Failed to initialize HTTP server.\n");
        fprintf(stderr, "       Port %d may be in use. Close other instances.\n",
                SERVER_PORT);
        format_registry_shutdown();
        system("pause");
        return 1;
    }
    printf("[OK] Server socket created\n");

    /* Create a default empty tree for the editor */
    Tree* initial_tree = tree_create();
    server_set_tree(initial_tree);
    printf("[OK] Initial mind map tree created\n");

    printf("\n========================================\n");
    printf("  Opening browser...\n");
    printf("  URL: http://localhost:%d\n", SERVER_PORT);
    printf("  Press Ctrl+C or close this window\n");
    printf("  to stop the server.\n");
    printf("========================================\n\n");

    /* Start the server (blocks until stopped) */
    int exit_code = server_start();

    /* --- Step 4: Cleanup --- */
    printf("\nShutting down...\n");
    tree_free(initial_tree);
    server_cleanup();
    format_registry_shutdown();
    printf("Goodbye!\n");

    return exit_code;
}
