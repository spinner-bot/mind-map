/* ============================================================
 * i18n.h - Internationalization / Multi-Language Support
 * i18n.h - 国际化 / 多语言支持
 *
 * Provides a simple translation system for EN ↔ ZH switching.
 * 提供简单的中英文翻译切换系统。
 *
 * Usage 用法:
 *   #include "i18n.h"
 *   const char* text = _(STR_WINDOW_TITLE);
 *   // Returns "Mind Map Conversion Tool" or "思维导图转换工具"
 *
 * Adding new strings 添加新字符串:
 *   1. Add a STR_xxx entry in the I18nStringId enum
 *   2. Add English text to g_en_strings[] in i18n.c
 *   3. Add Chinese text to g_zh_strings[] in i18n.c
 * ============================================================ */

#ifndef I18N_H
#define I18N_H

/* Language enum 语言枚举 */
typedef enum {
    LANG_EN = 0,  /* English 英语 */
    LANG_ZH = 1   /* Chinese 中文 */
} I18nLanguage;

/* ================================================================
 * String IDs 字符串标识符
 * ================================================================ */

typedef enum {
    /* Window 窗口 */
    STR_WINDOW_TITLE,

    /* Buttons 按钮 */
    STR_BTN_ADD_FILES,
    STR_BTN_REMOVE,
    STR_BTN_CLEAR_LIST,
    STR_BTN_BROWSE,
    STR_BTN_CONVERT_ALL,
    STR_BTN_CONVERT_SEL,

    /* Labels 标签 */
    STR_LABEL_INPUT_FILES,
    STR_LABEL_OUTPUT_FORMAT,
    STR_LABEL_OUTPUT_DIR,
    STR_LABEL_LANGUAGE,

    /* ListView columns ListView 列 */
    STR_COL_INDEX,
    STR_COL_PATH,
    STR_COL_FORMAT,

    /* File dialog 文件对话框 */
    STR_FILE_FILTER_ALL,
    STR_FILE_FILTER_JSON,
    STR_FILE_FILTER_TXT,
    STR_FILE_FILTER_MD,
    STR_FILE_FILTER_ANY,

    /* Browse dialog 浏览对话框 */
    STR_BROWSE_TITLE,

    /* User query dialogs 用户询问对话框 */
    STR_QUERY_JSON_IMPORT_TITLE,
    STR_QUERY_JSON_IMPORT_MSG,
    STR_QUERY_JSON_IMPORT_OPT1,
    STR_QUERY_JSON_IMPORT_OPT2,
    STR_QUERY_TXT_GAP_TITLE,
    STR_QUERY_TXT_GAP_MSG,
    STR_QUERY_TXT_GAP_OPT1,
    STR_QUERY_TXT_GAP_OPT2,

    /* Log messages 日志消息 */
    STR_LOG_STARTED,
    STR_LOG_FORMATS,
    STR_LOG_ADDED,
    STR_LOG_DUPLICATE,
    STR_LOG_REMOVED,
    STR_LOG_CLEARED,
    STR_LOG_DIR_SET,
    STR_LOG_BATCH_START,
    STR_LOG_BATCH_DONE,
    STR_LOG_FILE_OK,
    STR_LOG_FILE_ERROR,
    STR_LOG_FILE_WARN,
    STR_LOG_MAX_FILES,
    STR_LOG_CONVERT_ERR,
    STR_LOG_BATCH_ERR,

    /* Warnings/Errors 警告/错误 */
    STR_WARN_NO_FILES,
    STR_WARN_NO_FORMAT,
    STR_WARN_KEY_LOST,
    STR_WARN_BRANCH_LOST,
    STR_WARN_HEADING_SKIP,
    STR_WARN_NUMBER_GAP,
    STR_ERR_FILE_OPEN,
    STR_ERR_FILE_READ,
    STR_ERR_FILE_WRITE,
    STR_ERR_PARSE,
    STR_ERR_INTERNAL,

    /* Language names 语言名称 */
    STR_LANG_ENGLISH,
    STR_LANG_CHINESE,

    /* --- Sentinel 哨兵值 --- */
    STR_COUNT  /* Must be last 必须放在最后 */
} I18nStringId;

/* ================================================================
 * API Functions  API 函数
 * ================================================================ */

/* Initialize the i18n system. 初始化国际化系统。
 * Loads the last-used language from config, or defaults to
 * system locale. 从配置加载上次使用的语言，或默认为系统语言。
 * Returns the initial language. 返回初始语言。                   */
I18nLanguage i18n_init(void);

/* Get a translated string by its ID. 通过 ID 获取翻译后的字符串。
 * Returns a static string (do not free). 返回静态字符串（不要释放）。
 * Never returns NULL. 永不返回 NULL。                            */
const char* i18n_get(I18nStringId id);

/* Set the current language. 设置当前语言。
 * If the GUI has already created controls, caller must
 * refresh all displayed strings after changing language.
 * 如果 GUI 已创建控件，调用者必须在更改语言后刷新所有显示的字符串。*/
void i18n_set_language(I18nLanguage lang);

/* Get the current language. 获取当前语言。                       */
I18nLanguage i18n_get_language(void);

/* Shortcut macro for i18n_get(). Usage: _(STR_xxx)
 * i18n_get() 的快捷宏。用法：_(STR_xxx)                         */
#define _(id) i18n_get(id)

#endif /* I18N_H */
