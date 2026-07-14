/* ============================================================
 * i18n.c - Translation Tables and Language Switching
 * i18n.c - 翻译表与语言切换
 *
 * Holds all user-visible strings in English and Chinese.
 * Supports runtime switching between languages.
 * 包含所有用户可见字符串的英文和中文版本。
 * 支持运行时在语言之间切换。
 * ============================================================ */

#include "i18n.h"
#include <stddef.h>  /* NULL */

#ifdef _WIN32
#include <windows.h> /* GetUserDefaultUILanguage, GetSystemDefaultLangID */
#endif

/* Current language. Defaults to English; i18n_init() may
 * auto-detect Chinese locale. 当前语言。默认英语；
 * i18n_init() 可能根据系统区域设置自动检测中文。                 */
static I18nLanguage g_current_lang = LANG_EN;

/* ================================================================
 * English String Table  英语字符串表
 * ================================================================ */

static const char* g_en_strings[STR_COUNT] = {
    /* Window 窗口 */
    [STR_WINDOW_TITLE]        = "Mind Map Conversion Tool",

    /* Buttons 按钮 */
    [STR_BTN_ADD_FILES]       = "Add File(s)",
    [STR_BTN_REMOVE]          = "Remove",
    [STR_BTN_CLEAR_LIST]      = "Clear List",
    [STR_BTN_BROWSE]          = "Browse...",
    [STR_BTN_CONVERT_ALL]     = "Convert All",
    [STR_BTN_CONVERT_SEL]     = "Convert Selected",

    /* Labels 标签 */
    [STR_LABEL_INPUT_FILES]   = "Input Files:",
    [STR_LABEL_OUTPUT_FORMAT] = "Output Format:",
    [STR_LABEL_OUTPUT_DIR]    = "Output Directory:",
    [STR_LABEL_LANGUAGE]      = "Language:",

    /* ListView columns */
    [STR_COL_INDEX]           = "#",
    [STR_COL_PATH]            = "File Path",
    [STR_COL_FORMAT]          = "Format",

    /* File dialog */
    [STR_FILE_FILTER_ALL]     = "All Supported (*.json;*.txt;*.md)",
    [STR_FILE_FILTER_JSON]    = "JSON (*.json)",
    [STR_FILE_FILTER_TXT]     = "Text Outline (*.txt)",
    [STR_FILE_FILTER_MD]      = "Markdown (*.md)",
    [STR_FILE_FILTER_ANY]     = "All Files (*.*)",

    /* Browse dialog */
    [STR_BROWSE_TITLE]        = "Select Output Directory",

    /* User query dialogs */
    [STR_QUERY_JSON_IMPORT_TITLE] = "JSON Import - Index 0 Usage",
    [STR_QUERY_JSON_IMPORT_MSG]   = "How should index 0 of JSON arrays be interpreted?\n\n"
                                     "\"Branch Info\": index 0 stores the node's own "
                                     "content, children start at index 1.\n"
                                     "\"Sibling\": all indices are siblings. "
                                     "Branch info may be lost.",
    [STR_QUERY_JSON_IMPORT_OPT1]  = "Branch Info (index 0 = node content)",
    [STR_QUERY_JSON_IMPORT_OPT2]  = "Sibling (index 0 = first child)",
    [STR_QUERY_TXT_GAP_TITLE]     = "TXT Import - Numbering Gap Detected",
    [STR_QUERY_TXT_GAP_MSG]       = "Non-contiguous numbering was found. "
                                     "How should it be handled?\n\n"
                                     "\"Auto Reindex\": shift items to fill gaps.\n"
                                     "\"Empty Placeholder\": insert empty items.",
    [STR_QUERY_TXT_GAP_OPT1]      = "Auto Reindex (shift to fill gaps)",
    [STR_QUERY_TXT_GAP_OPT2]      = "Empty Placeholder (keep gaps)",

    /* Log messages */
    [STR_LOG_STARTED]        = "Mind Map Conversion Tool started",
    [STR_LOG_FORMATS]        = "Registered %d output format(s)",
    [STR_LOG_ADDED]          = "Added: %s [%s]",
    [STR_LOG_DUPLICATE]      = "File already in list: %s",
    [STR_LOG_REMOVED]        = "Removed file at position %d",
    [STR_LOG_CLEARED]        = "File list cleared",
    [STR_LOG_DIR_SET]        = "Output directory: %s",
    [STR_LOG_BATCH_START]    = "==== Starting batch conversion: %d file(s) ====",
    [STR_LOG_BATCH_DONE]     = "==== Batch complete: %d success, %d warning(s), %d error(s) ====",
    [STR_LOG_FILE_OK]        = "[OK] %s -> %s",
    [STR_LOG_FILE_ERROR]     = "[ERROR] %s: %s",
    [STR_LOG_FILE_WARN]      = "[WARN] %s: %d warning(s)",
    [STR_LOG_MAX_FILES]      = "Cannot add more files (max %d reached)",
    [STR_LOG_CONVERT_ERR]    = "[ERROR] Batch conversion failed to start",
    [STR_LOG_BATCH_ERR]      = "Could not determine input format (unknown extension)",

    /* Warnings/Errors */
    [STR_WARN_NO_FILES]      = "No input files in the list.",
    [STR_WARN_NO_FORMAT]     = "Please select an output format.",
    [STR_WARN_KEY_LOST]      = "Key info lost (dict->list)",
    [STR_WARN_BRANCH_LOST]   = "Branch info lost",
    [STR_WARN_HEADING_SKIP]  = "Heading level skip detected",
    [STR_WARN_NUMBER_GAP]    = "Non-contiguous numbering detected",
    [STR_ERR_FILE_OPEN]      = "Could not open file",
    [STR_ERR_FILE_READ]      = "Could not read file",
    [STR_ERR_FILE_WRITE]     = "Could not write file",
    [STR_ERR_PARSE]          = "Parse error",
    [STR_ERR_INTERNAL]       = "Internal error",

    /* Language names */
    [STR_LANG_ENGLISH]       = "English",
    [STR_LANG_CHINESE]       = "中文",
};

/* ================================================================
 * Chinese String Table  中文翻译表
 * ================================================================ */

static const char* g_zh_strings[STR_COUNT] = {
    /* Window 窗口 */
    [STR_WINDOW_TITLE]        = "思维导图转换工具",

    /* Buttons 按钮 */
    [STR_BTN_ADD_FILES]       = "添加文件",
    [STR_BTN_REMOVE]          = "移除",
    [STR_BTN_CLEAR_LIST]      = "清空列表",
    [STR_BTN_BROWSE]          = "浏览...",
    [STR_BTN_CONVERT_ALL]     = "全部转换",
    [STR_BTN_CONVERT_SEL]     = "转换选中",

    /* Labels 标签 */
    [STR_LABEL_INPUT_FILES]   = "输入文件：",
    [STR_LABEL_OUTPUT_FORMAT] = "输出格式：",
    [STR_LABEL_OUTPUT_DIR]    = "输出目录：",
    [STR_LABEL_LANGUAGE]      = "语言：",

    /* ListView columns */
    [STR_COL_INDEX]           = "#",
    [STR_COL_PATH]            = "文件路径",
    [STR_COL_FORMAT]          = "格式",

    /* File dialog */
    [STR_FILE_FILTER_ALL]     = "所有支持格式 (*.json;*.txt;*.md)",
    [STR_FILE_FILTER_JSON]    = "JSON (*.json)",
    [STR_FILE_FILTER_TXT]     = "文本大纲 (*.txt)",
    [STR_FILE_FILTER_MD]      = "Markdown (*.md)",
    [STR_FILE_FILTER_ANY]     = "所有文件 (*.*)",

    /* Browse dialog */
    [STR_BROWSE_TITLE]        = "选择输出目录",

    /* User query dialogs */
    [STR_QUERY_JSON_IMPORT_TITLE] = "JSON 导入 - 索引0用途",
    [STR_QUERY_JSON_IMPORT_MSG]   = "如何解释 JSON 数组的索引0？\n\n"
                                     "「枝信息」：索引0存储节点自身内容，"
                                     "子节点从索引1开始。\n"
                                     "「并列」：所有索引都是兄弟节点。"
                                     "枝信息可能丢失。",
    [STR_QUERY_JSON_IMPORT_OPT1]  = "枝信息（索引0 = 节点内容）",
    [STR_QUERY_JSON_IMPORT_OPT2]  = "并列（索引0 = 第一个子节点）",
    [STR_QUERY_TXT_GAP_TITLE]     = "TXT 导入 - 检测到编号间隙",
    [STR_QUERY_TXT_GAP_MSG]       = "发现不连续编号。如何处理？\n\n"
                                     "「自动重排」：将后续项目前移填补空隙。\n"
                                     "「空位占位」：在缺失位置插入空项目。",
    [STR_QUERY_TXT_GAP_OPT1]      = "自动重排（前移填补空隙）",
    [STR_QUERY_TXT_GAP_OPT2]      = "空位占位（保留空隙）",

    /* Log messages */
    [STR_LOG_STARTED]        = "思维导图转换工具已启动",
    [STR_LOG_FORMATS]        = "已注册 %d 种输出格式",
    [STR_LOG_ADDED]          = "已添加：%s [%s]",
    [STR_LOG_DUPLICATE]      = "文件已在列表中：%s",
    [STR_LOG_REMOVED]        = "已移除位置 %d 的文件",
    [STR_LOG_CLEARED]        = "文件列表已清空",
    [STR_LOG_DIR_SET]        = "输出目录：%s",
    [STR_LOG_BATCH_START]    = "==== 开始批量转换：%d 个文件 ====",
    [STR_LOG_BATCH_DONE]     = "==== 批量完成：%d 成功，%d 警告，%d 错误 ====",
    [STR_LOG_FILE_OK]        = "[成功] %s → %s",
    [STR_LOG_FILE_ERROR]     = "[错误] %s：%s",
    [STR_LOG_FILE_WARN]      = "[警告] %s：%d 条警告",
    [STR_LOG_MAX_FILES]      = "无法添加更多文件（已达上限 %d 个）",
    [STR_LOG_CONVERT_ERR]    = "[错误] 批量转换启动失败",
    [STR_LOG_BATCH_ERR]      = "无法确定输入格式（未知扩展名）",

    /* Warnings/Errors */
    [STR_WARN_NO_FILES]      = "文件列表为空。",
    [STR_WARN_NO_FORMAT]     = "请选择输出格式。",
    [STR_WARN_KEY_LOST]      = "键信息丢失（字典→列表）",
    [STR_WARN_BRANCH_LOST]   = "枝信息丢失",
    [STR_WARN_HEADING_SKIP]  = "检测到标题层级跳跃",
    [STR_WARN_NUMBER_GAP]    = "检测到不连续编号",
    [STR_ERR_FILE_OPEN]      = "无法打开文件",
    [STR_ERR_FILE_READ]      = "无法读取文件",
    [STR_ERR_FILE_WRITE]     = "无法写入文件",
    [STR_ERR_PARSE]          = "解析错误",
    [STR_ERR_INTERNAL]       = "内部错误",

    /* Language names */
    [STR_LANG_ENGLISH]       = "English",
    [STR_LANG_CHINESE]       = "中文",
};

/* ================================================================
 * API Implementation  API 实现
 * ================================================================ */

/* Detect system language on Windows. 在 Windows 上检测系统语言。
 * If the user's UI language is Chinese (LANGID 0x0804 = zh-CN,
 * 0x0404 = zh-TW, 0x0c04 = zh-HK), default to Chinese.
 * 如果用户的界面语言是中文，默认为中文。                           */
I18nLanguage i18n_init(void) {
#ifdef _WIN32
    /* GetUserDefaultUILanguage returns the LANGID of the user's
     * preferred UI language (e.g., 0x0804 for Chinese Simplified).
     * 返回用户首选 UI 语言的 LANGID（如简体中文为 0x0804）。      */
    LANGID lang_id = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(lang_id);

    /* Check for Chinese language variants 检查中文语言变体 */
    if (primary == LANG_CHINESE) {
        g_current_lang = LANG_ZH;
    }
#endif
    return g_current_lang;
}

/* Get translated string. Falls back to English for any
 * missing/null entries, and to STR_WINDOW_TITLE for any
 * completely broken state. 获取翻译字符串。对于缺失条目
 * 回退到英语，完全损坏时回退到 STR_WINDOW_TITLE。                */
const char* i18n_get(I18nStringId id) {
    /* Bounds check 边界检查 */
    if (id < 0 || id >= STR_COUNT) {
        return "???";
    }

    const char** table = NULL;

    switch (g_current_lang) {
        case LANG_ZH:
            table = g_zh_strings;
            break;
        case LANG_EN:
        default:
            table = g_en_strings;
            break;
    }

    /* If the string is NULL in the target language,
     * fall back to English. 如果目标语言中字符串为 NULL，回退到英语。*/
    if (table[id] == NULL) {
        table = g_en_strings;
    }

    /* Final safety net 最终安全网 */
    if (table[id] == NULL) {
        return "(missing)";
    }

    return table[id];
}

/* Change the current language. 更改当前语言。                    */
void i18n_set_language(I18nLanguage lang) {
    if (lang == LANG_EN || lang == LANG_ZH) {
        g_current_lang = lang;
    }
}

/* Get the current language. 获取当前语言。                       */
I18nLanguage i18n_get_language(void) {
    return g_current_lang;
}
