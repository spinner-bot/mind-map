/* ============================================================
 * gui.h - Win32 GUI Module
 * gui.h - Win32 GUI 模块
 *
 * Declares the GUI entry point and window constants.
 * 声明 GUI 入口点和窗口常量。
 * ============================================================ */

#ifndef GUI_H
#define GUI_H

#include <windows.h>

/* Window class and title constants 窗口类和标题常量 */
#define GUI_WINDOW_CLASS    L"MindMapToolWindow"
#define GUI_WINDOW_TITLE    L"Mind Map Conversion Tool"
#define GUI_WINDOW_WIDTH    900
#define GUI_WINDOW_HEIGHT   650

/* Control IDs 控件 ID */
#define IDC_FILE_LIST       1001  /* ListView for input files */
#define IDC_ADD_FILES       1002  /* Add Files button */
#define IDC_CLEAR_LIST      1003  /* Clear List button */
#define IDC_REMOVE_FILE     1004  /* Remove selected file button */
#define IDC_OUTPUT_FORMAT   1005  /* Output format ComboBox */
#define IDC_OUTPUT_DIR      1006  /* Output directory Edit */
#define IDC_BROWSE_DIR      1007  /* Browse directory button */
#define IDC_CONVERT_ALL     1008  /* Convert All button */
#define IDC_CONVERT_SEL     1009  /* Convert Selected button */
#define IDC_PROGRESS_BAR    1010  /* Progress bar */
#define IDC_LOG_AREA        1011  /* Log display Edit (read-only) */
#define IDC_FORMAT_LABEL    1012  /* "Output Format:" label */
#define IDC_DIR_LABEL       1013  /* "Output Directory:" label */
#define IDC_LANGUAGE        1014  /* Language ComboBox */

/* Initialize and run the GUI message loop.
 * 初始化并运行 GUI 消息循环。
 * Returns when the window is closed. 窗口关闭时返回。             */
int gui_run(HINSTANCE hInstance, int nCmdShow);

#endif /* GUI_H */
