/* ============================================================
 * gui.c - Win32 GUI Implementation
 * gui.c - Win32 GUI 实现
 *
 * Implements the main application window with:
 *   - File list (ListView in report mode) with add/remove
 *   - Output format selection (ComboBox)
 *   - Output directory selection with browse button
 *   - Convert buttons and progress bar
 *   - Log message display area
 *   - User decision dialog for format handler callbacks
 * 实现主应用程序窗口，包含文件列表、格式选择、目录选择、
 * 转换按钮、进度条、日志显示和用户决策对话框。
 *
 * Win32 boilerplate is kept to a minimum; the focus is on
 * clean integration with the conversion engine.
 * Win32 样板代码保持最少；重点是干净地集成转换引擎。
 * ============================================================ */

#include "gui.h"
#include "i18n.h"            /* _( ) macro for multi-language strings */
#include "converter.h"       /* convert_file, convert_batch, BatchConfig, BatchResult */
#include "format_handler.h"  /* FormatHandler, format_registry_*, handler_status_* */
#include "utils.h"           /* path_get_filename, str_dup, SAFE_FREE */
#include "tree.h"

#include <windows.h>
#include <commctrl.h>        /* ListView, ProgressBar, ComboBoxEx */
#include <commdlg.h>         /* GetOpenFileNameW */
#include <shlobj.h>          /* SHBrowseForFolderW */
#include <stdio.h>           /* snprintf, vsnprintf */
#include <stdlib.h>          /* malloc, free */
#include <string.h>          /* strlen, wcslen, strcpy */
#include <time.h>            /* time */

/* ================================================================
 * UTF-8 ↔ Wide String Helpers
 * UTF-8 ↔ 宽字符串转换辅助函数
 * ================================================================ */

/* Convert a UTF-8 string to a wide-character string.
 * 将 UTF-8 字符串转换为宽字符字符串。
 * Returns heap-allocated WCHAR* (caller must free()).
 * 返回堆分配的 WCHAR*（调用者必须 free()）。                   */
static WCHAR* utf8_to_wide(const char* utf8) {
    if (utf8 == NULL) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide == NULL) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    return wide;
}

/* Convert a wide-character string to UTF-8.
 * 将宽字符字符串转换为 UTF-8。
 * Returns heap-allocated char* (caller must free()).
 * 返回堆分配的 char*（调用者必须 free()）。                    */
static char* wide_to_utf8(const WCHAR* wide) {
    if (wide == NULL) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char* utf8 = (char*)malloc(len);
    if (utf8 == NULL) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, len, NULL, NULL);
    return utf8;
}

/* ================================================================
 * Global State (per-instance for this single-window app)
 * 全局状态（此单窗口应用的实例状态）
 * ================================================================ */

/* Maximum number of files that can be added to the list.
 * 可添加到列表的最大文件数。                                     */
#define MAX_INPUT_FILES 256

/* Per-file entry in our internal tracking array.
 * 内部跟踪数组中的每个文件条目。                                 */
typedef struct {
    char  path[MAX_PATH_LEN];   /* Full file path 完整文件路径 */
    char  format_name[32];      /* Detected/selected format 检测/选择的格式 */
    FormatHandler* handler;     /* Associated format handler 关联的格式处理器 */
} FileEntry;

/* Main window state — all application data lives here.
 * 主窗口状态 —— 所有应用数据都在这里。                            */
typedef struct {
    HWND hwnd;                     /* Main window handle 主窗口句柄 */
    HWND hFileList;                /* ListView control */
    HWND hAddBtn, hClearBtn, hRemoveBtn;
    HWND hFormatCombo;
    HWND hOutputDir;
    HWND hBrowseBtn;
    HWND hConvertAll, hConvertSel;
    HWND hProgress;
    HWND hLog;

    /* File tracking 文件跟踪 */
    FileEntry files[MAX_INPUT_FILES];
    int        file_count;

    /* Output directory string 输出目录字符串 */
    char output_dir[MAX_PATH_LEN];

    /* Available format handlers for output 可用的输出格式处理器 */
    FormatHandler* output_handlers[8];
    int            output_handler_count;

    /* Conversion state 转换状态 */
    bool is_converting;  /* TRUE during batch conversion */

    /* Log buffer 日志缓冲区 */
    char log_text[65536];
    int  log_len;
} GuiState;

static GuiState g_gui;

/* ================================================================
 * Logging helpers 日志辅助函数
 * ================================================================ */

/* Add a timestamped message to the log area.
 * 向日志区添加带时间戳的消息。                                   */
static void gui_log(const char* fmt, ...) {
    /* Get current time 获取当前时间 */
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    /* Format the message 格式化消息 */
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* Append to log buffer 追加到日志缓冲区 */
    int add_len = snprintf(NULL, 0, "[%s] %s\r\n", time_str, msg);
    if (g_gui.log_len + add_len < (int)sizeof(g_gui.log_text) - 1) {
        g_gui.log_len += snprintf(g_gui.log_text + g_gui.log_len,
                                   sizeof(g_gui.log_text) - g_gui.log_len,
                                   "[%s] %s\r\n", time_str, msg);
    }

    /* Update the Edit control with wide string 用宽字符串更新 Edit */
    if (g_gui.hLog != NULL) {
        WCHAR* wlog = utf8_to_wide(g_gui.log_text);
        if (wlog != NULL) {
            SetWindowTextW(g_gui.hLog, wlog);
            free(wlog);
        }
        /* Scroll to bottom 滚动到底部 */
        int len = GetWindowTextLength(g_gui.hLog);
        SendMessage(g_gui.hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(g_gui.hLog, EM_SCROLLCARET, 0, 0);
    }
}

/* ================================================================
 * ListView helpers  ListView 辅助函数
 * ================================================================ */

/* Add a file to the ListView and internal tracking array.
 * 将文件添加到 ListView 和内部跟踪数组。                         */
static void gui_add_file(const char* filepath) {
    if (g_gui.file_count >= MAX_INPUT_FILES) {
        gui_log("Cannot add more files (max %d reached)", MAX_INPUT_FILES);
        return;
    }

    /* Check for duplicates 检查重复 */
    for (int i = 0; i < g_gui.file_count; i++) {
        if (strcmp(g_gui.files[i].path, filepath) == 0) {
            gui_log("File already in list: %s",
                     path_get_filename(filepath));
            return;
        }
    }

    /* Detect input format 检测输入格式 */
    FormatHandler* handler = format_find_by_extension(filepath);
    const char* format_name = handler ? handler->format_name : "?";

    /* Store in internal array 存入内部数组 */
    FileEntry* entry = &g_gui.files[g_gui.file_count];
    strncpy(entry->path, filepath, MAX_PATH_LEN - 1);
    entry->path[MAX_PATH_LEN - 1] = '\0';
    strncpy(entry->format_name, format_name, sizeof(entry->format_name) - 1);
    entry->handler = handler;
    g_gui.file_count++;

    /* Add to ListView (Unicode API for Chinese path support)
     * 添加到 ListView（Unicode API 支持中文路径） */
    LVITEMW lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = g_gui.file_count - 1;

    /* Column 0: index number 索引号 */
    WCHAR idx_str[16];
    _snwprintf(idx_str, 16, L"%d", g_gui.file_count);
    lvi.pszText = idx_str;
    lvi.iSubItem = 0;
    int pos = (int)SendMessageW(g_gui.hFileList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

    /* Column 1: file path 文件路径 */
    WCHAR* wpath = utf8_to_wide(filepath);
    if (wpath != NULL) {
        lvi.iItem = pos;
        lvi.iSubItem = 1;
        lvi.pszText = wpath;
        SendMessageW(g_gui.hFileList, LVM_SETITEMW, 0, (LPARAM)&lvi);
        free(wpath);
    }

    /* Column 2: format 格式 */
    WCHAR* wfmt = utf8_to_wide(format_name);
    if (wfmt != NULL) {
        lvi.iSubItem = 2;
        lvi.pszText = wfmt;
        SendMessageW(g_gui.hFileList, LVM_SETITEMW, 0, (LPARAM)&lvi);
        free(wfmt);
    }

    gui_log("Added: %s [%s]", path_get_filename(filepath), format_name);
}

/* Remove the selected file from the list. 从列表中移除选中文件。  */
static void gui_remove_selected(void) {
    int sel = (int)SendMessage(g_gui.hFileList, LVM_GETNEXTITEM,
                                (WPARAM)-1, LVNI_SELECTED);
    if (sel < 0 || sel >= g_gui.file_count) {
        return;  /* No selection 无选中项 */
    }

    /* Remove from ListView 从 ListView 移除 */
    SendMessage(g_gui.hFileList, LVM_DELETEITEM, (WPARAM)sel, 0);

    /* Remove from internal array (shift remaining entries)
     * 从内部数组移除（移动后续条目） */
    for (int i = sel; i < g_gui.file_count - 1; i++) {
        g_gui.files[i] = g_gui.files[i + 1];
    }
    g_gui.file_count--;

    /* Update index numbers in ListView (use wide API)
     * 更新 ListView 中的索引号（使用 Unicode API） */
    for (int i = sel; i < g_gui.file_count; i++) {
        WCHAR widx[16];
        _snwprintf(widx, 16, L"%d", i + 1);
        LVITEMW lvi2 = { 0 };
        lvi2.mask = LVIF_TEXT;
        lvi2.iItem = i;
        lvi2.iSubItem = 0;
        lvi2.pszText = widx;
        SendMessageW(g_gui.hFileList, LVM_SETITEMW, 0, (LPARAM)&lvi2);
    }

    gui_log("Removed file at position %d", sel + 1);
}

/* Clear all files from the list. 清除列表中所有文件。             */
static void gui_clear_list(void) {
    SendMessage(g_gui.hFileList, LVM_DELETEALLITEMS, 0, 0);
    g_gui.file_count = 0;
    gui_log("File list cleared");
}

/* ================================================================
 * Dialogs 对话框
 * ================================================================ */

/* Show file open dialog (multi-select). 显示文件打开对话框（多选）。*/
static void gui_show_open_dialog(void) {
    OPENFILENAMEW ofn = { 0 };
    WCHAR szFile[8192] = { 0 };  /* Multi-select buffer 多选缓冲区 */

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = g_gui.hwnd;
    ofn.lpstrFile       = szFile;
    ofn.nMaxFile        = sizeof(szFile) / sizeof(WCHAR);
    ofn.lpstrFilter     = L"All Supported (*.json;*.txt;*.md)\0*.json;*.txt;*.md\0"
                          L"JSON (*.json)\0*.json\0"
                          L"Text Outline (*.txt)\0*.txt\0"
                          L"Markdown (*.md)\0*.md\0"
                          L"All Files (*.*)\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
                          OFN_ALLOWMULTISELECT | OFN_EXPLORER |
                          OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        /* Parse multi-select result.
         * If only one file: szFile = full path.
         * If multiple: szFile = directory path, followed by filenames,
         * each NUL-terminated, double-NUL at end.                    */
        WCHAR* p = ofn.lpstrFile;
        WCHAR  dir[MAX_PATH];

        /* First string is the directory */
        wcscpy(dir, p);
        p += wcslen(p) + 1;  /* Skip past NUL */

        if (*p == L'\0') {
            /* Single file: dir contains the full path 单文件 */
            char utf8_path[MAX_PATH_LEN];
            WideCharToMultiByte(CP_UTF8, 0, dir, -1,
                                utf8_path, sizeof(utf8_path), NULL, NULL);
            gui_add_file(utf8_path);
        } else {
            /* Multiple files: dir = directory, then filenames 多文件 */
            while (*p != L'\0') {
                WCHAR full_path[MAX_PATH];
                /* Build full path: dir + backslash + filename */
                wsprintfW(full_path, L"%s\\%s", dir, p);
                char utf8_path[MAX_PATH_LEN];
                WideCharToMultiByte(CP_UTF8, 0, full_path, -1,
                                    utf8_path, sizeof(utf8_path), NULL, NULL);
                gui_add_file(utf8_path);

                p += wcslen(p) + 1;  /* Skip to next filename */
            }
        }
    }
}

/* Show browse-for-folder dialog. 显示浏览文件夹对话框。           */
static void gui_show_browse_dir(void) {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = g_gui.hwnd;
    bi.lpszTitle = L"Select Output Directory";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        WCHAR wdir[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, wdir)) {
            char utf8_dir[MAX_PATH_LEN];
            WideCharToMultiByte(CP_UTF8, 0, wdir, -1,
                                utf8_dir, sizeof(utf8_dir), NULL, NULL);
            /* Use wide string directly to avoid encoding issues */
            SetWindowTextW(g_gui.hOutputDir, wdir);
            strncpy(g_gui.output_dir, utf8_dir, MAX_PATH_LEN - 1);
            gui_log("Output directory: %s", utf8_dir);
        }
        CoTaskMemFree(pidl);
    }
}

/* ================================================================
 * UserQueryCallback Bridge  用户决策回调桥接
 * ================================================================ */

/* This is the bridge between format handlers and the GUI.
 * When a handler needs a user decision, it calls this function,
 * which shows a Win32 MessageBox with custom buttons.
 * 这是格式处理器和 GUI 之间的桥梁。
 * 当处理器需要用户决策时，它调用此函数，
 * 函数显示带自定义按钮的 Win32 MessageBox。                     */
static int WINAPI gui_query_callback(const char* title,
                                      const char* question,
                                      const char** options,
                                      int option_count,
                                      void* user_data) {
    (void)user_data;

    if (option_count <= 0 || option_count > 4) {
        return -1;  /* Invalid option count */
    }

    /* Build the message box text.
     * We prepend the question and append numbered options.       */
    char msg[2048];
    int pos = snprintf(msg, sizeof(msg), "%s\n\n", question);

    for (int i = 0; i < option_count; i++) {
        pos += snprintf(msg + pos, sizeof(msg) - pos,
                        "  [%d] %s\n", i + 1, options[i]);
    }

    /* Map option count to MessageBox button types.
     * MessageBox only supports a few predefined button combos.
     * For 2 options: MB_YESNO or MB_OKCANCEL
     * For 3 options: MB_YESNOCANCEL or MB_ABORTRETRYIGNORE
     * We'll use a simpler approach: ask via a custom approach.   */

    /* Use MB_YESNO for 2 options, MB_YESNOCANCEL for 3.
     * For other counts, default to MB_OK.                        */
    UINT mb_type = MB_ICONQUESTION;
    int default_ret = -1;

    switch (option_count) {
        case 1:
            mb_type |= MB_OK;
            default_ret = 0;
            break;
        case 2:
            /* Map: option 0 = Yes (IDYES=6), option 1 = No (IDNO=7) */
            mb_type |= MB_YESNO;
            break;
        case 3:
            mb_type |= MB_YESNOCANCEL;
            break;
        default:
            mb_type |= MB_OK;
            default_ret = 0;
            break;
    }

    /* Convert title and message to wide strings for MessageBox */
    WCHAR wtitle[256];
    WCHAR wmsg[2048];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle,
                        sizeof(wtitle) / sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg,
                        sizeof(wmsg) / sizeof(WCHAR));

    int mb_result = MessageBoxW(g_gui.hwnd, wmsg, wtitle, mb_type);

    /* Map MessageBox result back to 0-based option index */
    switch (mb_result) {
        case IDYES:    return 0;  /* Yes → option 0 */
        case IDNO:     return 1;  /* No → option 1 */
        case IDCANCEL: return (option_count >= 3) ? 2 : -1;
        case IDOK:     return 0;
        default:       return default_ret;
    }
}

/* ================================================================
 * UI Refresh (for language switching)  UI 刷新（语言切换用）
 * ================================================================ */

/* Refresh all user-visible text after language change.
 * 语言切换后刷新所有用户可见的文本。                               */
static void gui_refresh_ui(void) {
    WCHAR* wtmp;
    /* Update window title */
    wtmp = utf8_to_wide(_(STR_WINDOW_TITLE));
    if (wtmp) { SetWindowTextW(g_gui.hwnd, wtmp); free(wtmp); }
    /* Update button labels */
    wtmp = utf8_to_wide(_(STR_BTN_ADD_FILES));
    if (wtmp) { SetWindowTextW(g_gui.hAddBtn, wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_BTN_REMOVE));
    if (wtmp) { SetWindowTextW(g_gui.hRemoveBtn, wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_BTN_CLEAR_LIST));
    if (wtmp) { SetWindowTextW(g_gui.hClearBtn, wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_BTN_BROWSE));
    if (wtmp) { SetWindowTextW(g_gui.hBrowseBtn, wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_BTN_CONVERT_ALL));
    if (wtmp) { SetWindowTextW(g_gui.hConvertAll, wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_BTN_CONVERT_SEL));
    if (wtmp) { SetWindowTextW(g_gui.hConvertSel, wtmp); free(wtmp); }
    /* Update labels */
    wtmp = utf8_to_wide(_(STR_LABEL_INPUT_FILES));
    if (wtmp) { SetWindowTextW(GetDlgItem(g_gui.hwnd, 2001), wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_LABEL_OUTPUT_FORMAT));
    if (wtmp) { SetWindowTextW(GetDlgItem(g_gui.hwnd, IDC_FORMAT_LABEL), wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_LABEL_OUTPUT_DIR));
    if (wtmp) { SetWindowTextW(GetDlgItem(g_gui.hwnd, IDC_DIR_LABEL), wtmp); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_LABEL_LANGUAGE));
    if (wtmp) { SetWindowTextW(GetDlgItem(g_gui.hwnd, 2002), wtmp); free(wtmp); }
    /* Update ListView column headers */
    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT;
    wtmp = utf8_to_wide(_(STR_COL_INDEX));
    if (wtmp) { lvc.pszText = wtmp; SendMessageW(g_gui.hFileList, LVM_SETCOLUMNW, 0, (LPARAM)&lvc); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_COL_PATH));
    if (wtmp) { lvc.pszText = wtmp; SendMessageW(g_gui.hFileList, LVM_SETCOLUMNW, 1, (LPARAM)&lvc); free(wtmp); }
    wtmp = utf8_to_wide(_(STR_COL_FORMAT));
    if (wtmp) { lvc.pszText = wtmp; SendMessageW(g_gui.hFileList, LVM_SETCOLUMNW, 2, (LPARAM)&lvc); free(wtmp); }
}

/* ================================================================
 * Conversion Execution  转换执行
 * ================================================================ */

/* Run the batch conversion with current settings.
 * 使用当前设置运行批量转换。                                     */
static void gui_run_conversion(void) {
    if (g_gui.file_count == 0) {
        MessageBoxW(g_gui.hwnd,
                    L"No input files in the list.",
                    L"Mind Map Tool", MB_OK | MB_ICONWARNING);
        return;
    }

    /* Get selected output format 获取选择的输出格式 */
    int fmt_idx = (int)SendMessage(g_gui.hFormatCombo, CB_GETCURSEL, 0, 0);
    if (fmt_idx < 0 || fmt_idx >= g_gui.output_handler_count) {
        MessageBoxW(g_gui.hwnd,
                    L"Please select an output format.",
                    L"Mind Map Tool", MB_OK | MB_ICONWARNING);
        return;
    }
    FormatHandler* out_handler = g_gui.output_handlers[fmt_idx];

    /* Get output directory (Unicode, then convert to UTF-8)
     * 获取输出目录（Unicode，再转为 UTF-8） */
    WCHAR wdir[MAX_PATH];
    GetWindowTextW(g_gui.hOutputDir, wdir, MAX_PATH);
    char out_dir[MAX_PATH_LEN];
    WideCharToMultiByte(CP_UTF8, 0, wdir, -1,
                        out_dir, sizeof(out_dir), NULL, NULL);
    if (out_dir[0] == '\0') {
        strcpy(out_dir, ".");
    }

    /* Disable buttons during conversion 转换期间禁用按钮 */
    g_gui.is_converting = true;
    EnableWindow(g_gui.hConvertAll, FALSE);
    EnableWindow(g_gui.hConvertSel, FALSE);

    /* Set up progress bar 设置进度条 */
    SendMessage(g_gui.hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, g_gui.file_count));
    SendMessage(g_gui.hProgress, PBM_SETPOS, 0, 0);

    gui_log("==== Starting batch conversion: %d file(s) ====", g_gui.file_count);

    /* Build BatchConfig 构建 BatchConfig */
    BatchConfig config;
    config.auto_detect_input = true;
    config.input_handlers     = NULL;
    config.output_handler     = out_handler;
    config.query_cb           = gui_query_callback;
    config.query_cb_data      = NULL;
    config.progress_cb        = NULL;
    config.progress_cb_data   = NULL;

    /* Copy output directory 复制输出目录 */
    strncpy(config.output_dir, out_dir, MAX_PATH_LEN - 1);
    config.output_dir[MAX_PATH_LEN - 1] = '\0';

    /* Build input paths array 构建输入路径数组 */
    config.input_count = g_gui.file_count;
    config.input_paths = (char**)malloc(config.input_count * sizeof(char*));
    for (int i = 0; i < config.input_count; i++) {
        config.input_paths[i] = g_gui.files[i].path;
    }

    /* Run conversion 执行转换 */
    BatchResult* result = convert_batch(&config);

    /* Report results 报告结果 */
    if (result != NULL) {
        gui_log("==== Batch complete: %d success, %d warning(s), %d error(s) ====",
                result->success_count, result->warning_count,
                result->error_count);

        for (int i = 0; i < result->total_files; i++) {
            if (result->output_paths[i] != NULL) {
                gui_log("[OK] %s → %s",
                         path_get_filename(config.input_paths[i]),
                         path_get_filename(result->output_paths[i]));
            }
            if (result->error_messages[i] != NULL) {
                gui_log("[ERROR] %s: %s",
                         path_get_filename(config.input_paths[i]),
                         result->error_messages[i]);
            }
            if (result->warning_counts[i] > 0) {
                gui_log("[WARN] %s: %d warning(s)",
                         path_get_filename(config.input_paths[i]),
                         result->warning_counts[i]);
            }
        }

        batch_result_free(result);
    } else {
        gui_log("[ERROR] Batch conversion failed to start");
    }

    free(config.input_paths);

    /* Re-enable buttons 重新启用按钮 */
    g_gui.is_converting = false;
    EnableWindow(g_gui.hConvertAll, TRUE);
    EnableWindow(g_gui.hConvertSel, TRUE);

    /* Reset progress bar 重置进度条 */
    SendMessage(g_gui.hProgress, PBM_SETPOS, 0, 0);
}

/* ================================================================
 * Window Procedure  窗口过程
 * ================================================================ */

/* Main window procedure. Handles all window messages.
 * 主窗口过程。处理所有窗口消息。                                   */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
        {
            /* Store window handle 保存窗口句柄 */
            g_gui.hwnd = hwnd;

            /* Initialize state 初始化状态 */
            g_gui.file_count = 0;
            g_gui.is_converting = false;
            g_gui.log_len = 0;
            g_gui.log_text[0] = '\0';
            strcpy(g_gui.output_dir, ".");

            /* Get available output handlers 获取可用的输出处理器 */
            g_gui.output_handler_count = format_list_all(
                g_gui.output_handlers, 8);

            /* Create font for controls 创建控件字体 */
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            /* --- File list label --- */
            {
                WCHAR* wl = utf8_to_wide(_(STR_LABEL_INPUT_FILES));
                CreateWindowW(L"STATIC", wl ? wl : L"Input Files:",
                              WS_CHILD | WS_VISIBLE,
                              10, 10, 200, 20,
                              hwnd, (HMENU)2001, NULL, NULL);
                if (wl) free(wl);
            }

            /* --- File ListView (report mode) --- */
            g_gui.hFileList = CreateWindowW(WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER |
                LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                10, 30, 650, 180, hwnd,
                (HMENU)IDC_FILE_LIST, NULL, NULL);
            SendMessage(g_gui.hFileList, WM_SETFONT,
                        (WPARAM)hFont, TRUE);

            /* ListView columns: #, File Path, Format */
            LVCOLUMNW lvc = { 0 };
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            lvc.iSubItem = 0;
            {
                WCHAR* wcol = utf8_to_wide(_(STR_COL_INDEX));
                lvc.pszText = wcol ? wcol : L"#";
                lvc.cx = 40;
                ListView_InsertColumn(g_gui.hFileList, 0, &lvc);
                if (wcol) free(wcol);
            }

            lvc.iSubItem = 1;
            {
                WCHAR* wcol = utf8_to_wide(_(STR_COL_PATH));
                lvc.pszText = wcol ? wcol : L"File Path";
                lvc.cx = 420;
                ListView_InsertColumn(g_gui.hFileList, 1, &lvc);
                if (wcol) free(wcol);
            }

            lvc.iSubItem = 2;
            {
                WCHAR* wcol = utf8_to_wide(_(STR_COL_FORMAT));
                lvc.pszText = wcol ? wcol : L"Format";
                lvc.cx = 160;
                ListView_InsertColumn(g_gui.hFileList, 2, &lvc);
                if (wcol) free(wcol);
            }

            /* Extended ListView styles: full row select */
            ListView_SetExtendedListViewStyle(g_gui.hFileList,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

            /* --- Buttons --- */
            {
                WCHAR* wb = utf8_to_wide(_(STR_BTN_ADD_FILES));
                g_gui.hAddBtn = CreateWindowW(L"BUTTON", wb ? wb : L"Add",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    670, 30, 100, 30, hwnd,
                    (HMENU)IDC_ADD_FILES, NULL, NULL);
                SendMessage(g_gui.hAddBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                if (wb) free(wb);
            }
            {
                WCHAR* wb = utf8_to_wide(_(STR_BTN_REMOVE));
                g_gui.hRemoveBtn = CreateWindowW(L"BUTTON", wb ? wb : L"Remove",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    670, 70, 100, 30, hwnd,
                    (HMENU)IDC_REMOVE_FILE, NULL, NULL);
                SendMessage(g_gui.hRemoveBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                if (wb) free(wb);
            }
            {
                WCHAR* wb = utf8_to_wide(_(STR_BTN_CLEAR_LIST));
                g_gui.hClearBtn = CreateWindowW(L"BUTTON", wb ? wb : L"Clear",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    670, 110, 100, 30, hwnd,
                    (HMENU)IDC_CLEAR_LIST, NULL, NULL);
                SendMessage(g_gui.hClearBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                if (wb) free(wb);
            }

            /* --- Output Format --- */
            {
                WCHAR* wl = utf8_to_wide(_(STR_LABEL_OUTPUT_FORMAT));
                CreateWindowW(L"STATIC", wl ? wl : L"Output Format:",
                    WS_CHILD | WS_VISIBLE,
                    10, 225, 120, 20,
                    hwnd, (HMENU)IDC_FORMAT_LABEL, NULL, NULL);
                if (wl) free(wl);
            }

            g_gui.hFormatCombo = CreateWindowW(L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                140, 222, 200, 200, hwnd,
                (HMENU)IDC_OUTPUT_FORMAT, NULL, NULL);
            SendMessage(g_gui.hFormatCombo, WM_SETFONT,
                        (WPARAM)hFont, TRUE);

            /* Populate format combo 填充格式下拉列表 */
            for (int i = 0; i < g_gui.output_handler_count; i++) {
                /* Convert format description to wide string */
                WCHAR wdesc[128];
                MultiByteToWideChar(CP_UTF8, 0,
                    g_gui.output_handlers[i]->description, -1,
                    wdesc, sizeof(wdesc) / sizeof(WCHAR));
                SendMessageW(g_gui.hFormatCombo, CB_ADDSTRING,
                             0, (LPARAM)wdesc);
            }
            /* Select first format 选择第一个格式 */
            SendMessage(g_gui.hFormatCombo, CB_SETCURSEL, 0, 0);

            /* --- Output Directory --- */
            {
                WCHAR* wl = utf8_to_wide(_(STR_LABEL_OUTPUT_DIR));
                CreateWindowW(L"STATIC", wl ? wl : L"Output Directory:",
                    WS_CHILD | WS_VISIBLE,
                    10, 255, 120, 20,
                    hwnd, (HMENU)IDC_DIR_LABEL, NULL, NULL);
                if (wl) free(wl);
            }

            g_gui.hOutputDir = CreateWindowW(L"EDIT", L".",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                140, 252, 400, 25, hwnd,
                (HMENU)IDC_OUTPUT_DIR, NULL, NULL);
            SendMessage(g_gui.hOutputDir, WM_SETFONT,
                        (WPARAM)hFont, TRUE);

            {
                WCHAR* wb = utf8_to_wide(_(STR_BTN_BROWSE));
                g_gui.hBrowseBtn = CreateWindowW(L"BUTTON", wb ? wb : L"Browse...",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    550, 250, 100, 27, hwnd,
                    (HMENU)IDC_BROWSE_DIR, NULL, NULL);
                SendMessage(g_gui.hBrowseBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                if (wb) free(wb);
            }

            /* --- Language Selector --- */
            {
                WCHAR* wl = utf8_to_wide(_(STR_LABEL_LANGUAGE));
                CreateWindowW(L"STATIC", wl ? wl : L"Language:",
                    WS_CHILD | WS_VISIBLE,
                    10, 285, 60, 20,
                    hwnd, (HMENU)2002, NULL, NULL);
                if (wl) free(wl);
            }
            {
                HWND hLangCombo = CreateWindowW(L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                    75, 282, 100, 200, hwnd,
                    (HMENU)IDC_LANGUAGE, NULL, NULL);
                SendMessage(hLangCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
                /* Add language options */
                WCHAR* wlang;
                wlang = utf8_to_wide(_(STR_LANG_ENGLISH));
                SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)(wlang ? wlang : L"English"));
                if (wlang) free(wlang);
                wlang = utf8_to_wide(_(STR_LANG_CHINESE));
                SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)(wlang ? wlang : L"Chinese"));
                if (wlang) free(wlang);
                /* Select current language */
                SendMessage(hLangCombo, CB_SETCURSEL, (WPARAM)i18n_get_language(), 0);
            }

            /* --- Convert Buttons --- */
            {
                WCHAR* wb = utf8_to_wide(_(STR_BTN_CONVERT_ALL));
                g_gui.hConvertAll = CreateWindowW(L"BUTTON",
                    wb ? wb : L"Convert All",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    10, 315, 150, 35, hwnd,
                    (HMENU)IDC_CONVERT_ALL, NULL, NULL);
                SendMessage(g_gui.hConvertAll, WM_SETFONT,
                            (WPARAM)hFont, TRUE);
                if (wb) free(wb);
            }
            {
                WCHAR* wb = utf8_to_wide(_(STR_BTN_CONVERT_SEL));
                g_gui.hConvertSel = CreateWindowW(L"BUTTON",
                    wb ? wb : L"Convert Selected",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    170, 315, 150, 35, hwnd,
                    (HMENU)IDC_CONVERT_SEL, NULL, NULL);
                SendMessage(g_gui.hConvertSel, WM_SETFONT,
                            (WPARAM)hFont, TRUE);
                if (wb) free(wb);
            }

            /* --- Progress Bar --- */
            g_gui.hProgress = CreateWindowW(PROGRESS_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                330, 320, 330, 25, hwnd,
                (HMENU)IDC_PROGRESS_BAR, NULL, NULL);

            /* --- Log Area --- */
            g_gui.hLog = CreateWindowW(L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER |
                ES_MULTILINE | ES_READONLY | WS_VSCROLL |
                ES_AUTOVSCROLL,
                10, 360, 860, 240, hwnd,
                (HMENU)IDC_LOG_AREA, NULL, NULL);
            SendMessage(g_gui.hLog, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Set default font for all controls */
            /* (already set per-control above) */

            gui_log("%s", _(STR_LOG_STARTED));
            gui_log(_(STR_LOG_FORMATS),
                     g_gui.output_handler_count);

            return 0;
        }

        case WM_SIZE:
        {
            /* Resize log area to fill remaining space
             * 调整日志区大小以填充剩余空间 */
            int width  = LOWORD(lParam);
            int height = HIWORD(lParam);

            if (g_gui.hLog != NULL) {
                MoveWindow(g_gui.hLog, 10, 360,
                           width - 30, height - 370, TRUE);
            }
            /* Resize progress bar 调整进度条大小 */
            if (g_gui.hProgress != NULL) {
                MoveWindow(g_gui.hProgress, 330, 320,
                           width - 350, 25, TRUE);
            }
            return 0;
        }

        case WM_COMMAND:
        {
            WORD id = LOWORD(wParam);

            switch (id) {
                case IDC_ADD_FILES:
                    gui_show_open_dialog();
                    break;

                case IDC_REMOVE_FILE:
                    gui_remove_selected();
                    break;

                case IDC_CLEAR_LIST:
                    gui_clear_list();
                    break;

                case IDC_BROWSE_DIR:
                    gui_show_browse_dir();
                    break;

                case IDC_CONVERT_ALL:
                    gui_run_conversion();
                    break;

                case IDC_LANGUAGE:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int lang_idx = (int)SendMessage(
                            GetDlgItem(hwnd, IDC_LANGUAGE), CB_GETCURSEL, 0, 0);
                        if (lang_idx >= 0) {
                            i18n_set_language((I18nLanguage)lang_idx);
                            gui_refresh_ui();
                            gui_log("Language switched to: %s",
                                    lang_idx == LANG_ZH ? "中文" : "English");
                        }
                    }
                    break;

                case IDC_CONVERT_SEL:
                    /* For now, same as Convert All
                     * (selection filtering not implemented in Phase 1) */
                    gui_run_conversion();
                    break;

                case IDC_OUTPUT_DIR:
                    if (HIWORD(wParam) == EN_CHANGE) {
                        WCHAR wbuf[MAX_PATH];
                        GetWindowTextW(g_gui.hOutputDir, wbuf, MAX_PATH);
                        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1,
                            g_gui.output_dir, sizeof(g_gui.output_dir), NULL, NULL);
                    }
                    break;
            }
            return 0;
        }

        case WM_NOTIFY:
        {
            /* Handle ListView double-click (open file?)
             * For now, no special handling needed */
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

/* ================================================================
 * GUI Entry Point  GUI 入口点
 * ================================================================ */

/* Initialize and run the GUI. 初始化并运行 GUI。
 * Returns exit code (0 = normal). 返回退出码（0 = 正常）。       */
int gui_run(HINSTANCE hInstance, int nCmdShow) {
    /* Initialize common controls (required for ListView, ProgressBar, etc.)
     * 初始化通用控件（ListView、ProgressBar 等需要） */
    INITCOMMONCONTROLSEX icc = { 0 };
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    /* Register window class 注册窗口类 */
    WNDCLASSEXW wc = { 0 };
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = GUI_WINDOW_CLASS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class",
                    L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Create main window 创建主窗口 */
    HWND hwnd = CreateWindowExW(
        0, GUI_WINDOW_CLASS, GUI_WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        MessageBoxW(NULL, L"Failed to create window",
                    L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Center window on screen 窗口居中 */
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - GUI_WINDOW_WIDTH) / 2;
    int y = (screen_h - GUI_WINDOW_HEIGHT) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* Message loop 消息循环 */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
