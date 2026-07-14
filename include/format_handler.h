/* ============================================================
 * format_handler.h - Format Handler Plugin Interface
 * format_handler.h - 格式处理器插件接口
 *
 * Defines the plugin architecture for format handlers. Each
 * file format (JSON, TXT, MD, future .mind) is implemented as
 * a FormatHandler that conforms to this interface. New formats
 * can be added by writing a handler and registering it —
 * no changes to the core code are needed.
 * 定义格式处理器的插件架构。每种文件格式（JSON、TXT、MD、
 * 未来的 .mind）都作为符合此接口的 FormatHandler 实现。
 * 新格式只需编写处理器并注册即可添加 —— 无需修改核心代码。
 *
 * Design pattern 设计模式:
 *   Strategy Pattern — each FormatHandler is a strategy for
 *   parsing/serializing a specific file format. The converter
 *   module selects the appropriate handler via the registry.
 *   策略模式 —— 每个 FormatHandler 是特定文件格式的解析/序列化策略。
 *   转换模块通过注册表选择合适处理器。
 * ============================================================ */

#ifndef FORMAT_HANDLER_H
#define FORMAT_HANDLER_H

#include <stdbool.h>   /* bool, true, false */

/* Forward declaration 前向声明 */
struct Tree;
struct FormatHandler;

/* ================================================================
 * Result Codes 结果码
 * ================================================================ */

/* Unified result codes returned by all parse and serialize
 * operations. OK is zero; warnings are positive; errors are
 * negative. 所有解析和序列化操作返回的统一结果码。
 * OK 为零；警告为正数；错误为负数。
 *
 * Convention 约定:
 *   - 0:         Success, no warnings 成功，无警告
 *   - Positive:  Success with warnings 成功但有警告
 *   - Negative:  Error (operation failed) 错误（操作失败）     */
typedef enum {
    /* --- Success 成功 --- */
    RESULT_OK                    =  0,

    /* --- Warnings 警告（正数） --- */
    RESULT_WARN_KEY_LOST         =  1,  /* JSON dict→list: keys discarded */
    RESULT_WARN_BRANCH_LOST      =  2,  /* Branch node info lost in output */
    RESULT_WARN_HEADING_SKIP     =  3,  /* MD heading level gap detected */
    RESULT_WARN_NUMBER_GAP       =  4,  /* TXT non-contiguous numbering detected */
    RESULT_WARN_CONTENT_TRUNC    =  5,  /* Content truncated (depth exceeded, etc.) */
    RESULT_WARN_INDEX_ZERO_MIXED =  6,  /* JSON index-0 mode mismatch between files */

    /* --- Errors 错误（负数） --- */
    RESULT_ERROR_FILE_OPEN       = -1,  /* Could not open file */
    RESULT_ERROR_FILE_READ       = -2,  /* Could not read file */
    RESULT_ERROR_FILE_WRITE      = -3,  /* Could not write file */
    RESULT_ERROR_INVALID_FORMAT  = -4,  /* File does not match expected format */
    RESULT_ERROR_PARSE           = -5,  /* Parse error (malformed content) */
    RESULT_ERROR_MEMORY          = -6,  /* Memory allocation failure */
    RESULT_ERROR_ENCODING        = -7,  /* Encoding detection/conversion failure */
    RESULT_ERROR_EMPTY_TREE      = -8,  /* Cannot serialize an empty tree */
    RESULT_ERROR_INTERNAL        = -9,  /* Internal logic error (should not happen) */
    RESULT_ERROR_USER_CANCEL     = -10  /* User cancelled the operation */
} HandlerResult;

/* ================================================================
 * Warning Accumulation 警告累积
 * ================================================================ */

/* Maximum number of warnings that can be accumulated per operation.
 * 单次操作可累积的最大警告数。                                     */
#define MAX_WARNINGS    32

/* Maximum length of each warning message (including NUL terminator).
 * 每条警告消息的最大长度（包括 NUL 终止符）。                      */
#define MAX_WARN_LEN    512

/* ================================================================
 * HandlerStatus — Operation Result + Warnings
 * HandlerStatus — 操作结果 + 警告
 * ================================================================ */

/* Unified status structure returned by both parse() and serialize().
 * parse() 和 serialize() 返回的统一状态结构。
 *
 * result_code:  Overall result (0=OK, >0=warning, <0=error)
 *               整体结果（0=OK，>0=警告，<0=错误）
 * warning_count: Number of accumulated warnings
 *                累积的警告数量
 * warnings:      Warning message strings (up to MAX_WARNINGS)
 *                警告消息字符串（最多 MAX_WARNINGS 条）             */
typedef struct {
    HandlerResult result_code;
    int           warning_count;
    char          warnings[MAX_WARNINGS][MAX_WARN_LEN];
} HandlerStatus;

/* ================================================================
 * UserQueryCallback — User Decision Interface
 * UserQueryCallback — 用户决策接口
 * ================================================================ */

/* Callback for format handlers to ask the user a question.
 * 格式处理器询问用户问题的回调函数。
 *
 * Handlers call this when they need a decision (e.g., how to
 * handle non-contiguous TXT numbering, or whether to use
 * index-0 branch-info mode for JSON). The callback abstracts
 * the UI: GUI mode shows a dialog, CLI mode reads stdin.
 * 处理器在需要决策时调用此函数（例如，如何处理不连续的 TXT 编号，
 * 或 JSON 是否使用索引0枝信息模式）。回调抽象了 UI：
 * GUI 模式显示对话框，CLI 模式读取标准输入。
 *
 * Parameters 参数:
 *   title       - Short title for the question (dialog title)
 *                 问题的简短标题（对话框标题）
 *   question    - Detailed question text
 *                 详细的问题文本
 *   options     - Array of option strings to present (e.g., ["Yes", "No"])
 *                 要呈现的选项字符串数组（如 ["是", "否"]）
 *   option_count - Number of options (1-4 recommended)
 *                  选项数量（建议 1-4 个）
 *   user_data   - Opaque pointer provided by the caller
 *                 调用者传入的不透明指针
 *
 * Returns 返回值:
 *   0-based index of the selected option, or -1 if cancelled.
 *   所选选项的从 0 开始的索引，如果取消则返回 -1。                   */
typedef int (*UserQueryCallback)(const char* title,
                                  const char* question,
                                  const char** options,
                                  int option_count,
                                  void* user_data);

/* ================================================================
 * JSON Index-0 Mode — 枝信息处理策略
 * ================================================================ */

/* Controls how index 0 of a JSON array is interpreted.
 * 控制 JSON 数组索引 0 的解释方式。
 *
 * INDEX0_BRANCH: Index 0 holds the node's own content (branch info),
 *   children start at index 1.
 *   索引 0 存储节点自身内容（枝信息），子节点从索引 1 开始。
 *   Array format: ["node_content", child1, child2, ...]
 *
 * INDEX0_SIBLING: All indices are siblings (no special meaning for
 *   index 0). Branch info in the tree will be lost with a warning.
 *   所有索引都是兄弟节点（索引 0 无特殊含义）。
 *   树中的枝信息将丢失并产生警告。
 *   Array format: [child1, child2, ...]                               */
typedef enum {
    INDEX0_BRANCH   = 0,  /* 枝信息模式：索引0 = 节点内容 */
    INDEX0_SIBLING  = 1   /* 并列模式：索引0 = 第一个子节点 */
} JsonIndexMode;

/* ================================================================
 * FormatHandler — The Plugin Vtable
 * FormatHandler — 插件虚表
 * ================================================================ */

/* A format handler is a collection of function pointers that
 * implement parsing, serialization, and detection for a specific
 * file format. Each handler is a singleton — there is one
 * instance per supported format, registered at startup.
 * 格式处理器是一组函数指针的集合，实现了特定文件格式的
 * 解析、序列化和检测功能。每个处理器是单例 —— 每种支持的格式
 * 有一个实例，在启动时注册。                                       */

/* Forward declaration of struct Tree */
struct Tree;

typedef struct FormatHandler {
    /* Internal identifier, e.g., "json", "txt", "md".
     * 内部标识符，如 "json"、"txt"、"md"。                       */
    const char* format_name;

    /* File extension including dot, e.g., ".json", ".txt", ".md".
     * 包含点的文件扩展名，如 ".json"、".txt"、".md"。            */
    const char* extension;

    /* Human-readable description for GUI display.
     * 用于 GUI 显示的人类可读描述。                              */
    const char* description;

    /* --- Parse: file on disk → Tree ---
     * 解析：磁盘上的文件 → Tree
     *
     * Parameters 参数:
     *   filepath  - Path to the input file 输入文件路径
     *   tree      - Pre-initialized Tree to populate 预初始化的树
     *   options   - Handler-specific options (may be NULL)
     *               处理器特定选项（可以为 NULL）
     *   query_cb  - Callback for user decisions (may be NULL)
     *               用户决策回调（可以为 NULL）
     *   cb_data   - Opaque user data for the callback
     *               回调的不透明用户数据
     * Returns: HandlerStatus with result and warnings.
     * 返回值：包含结果和警告的 HandlerStatus。                     */
    HandlerStatus (*parse)(const char* filepath,
                           struct Tree* tree,
                           void* options,
                           UserQueryCallback query_cb,
                           void* cb_data);

    /* --- Serialize: Tree → file on disk ---
     * 序列化：Tree → 磁盘上的文件
     *
     * Parameters 参数:
     *   filepath  - Path to the output file 输出文件路径
     *   tree      - The tree to serialize 要序列化的树
     *   options   - Handler-specific options (may be NULL)
     *               处理器特定选项（可以为 NULL）
     * Returns: HandlerStatus with result and warnings.
     * 返回值：包含结果和警告的 HandlerStatus。                     */
    HandlerStatus (*serialize)(const char* filepath,
                               const struct Tree* tree,
                               void* options);

    /* --- Detect: quick format identification ---
     * 检测：快速格式识别
     *
     * In Phase 1, detection is by file extension only.
     * In future phases, could sniff file content (magic bytes).
     * 一期仅通过文件扩展名检测。
     * 后续阶段可嗅探文件内容（魔数）。
     * Returns: true if this handler likely handles this file.
     * 返回值：如果此处理器可能处理此文件则返回 true。              */
    bool (*detect)(const char* filepath);

    /* --- Create default options for this handler ---
     * 为此处理器创建默认选项
     * Returns: heap-allocated options struct (caller must
     *          free via free_options()). 堆分配的选项结构。       */
    void* (*create_default_options)(void);

    /* --- Free options created by create_default_options ---
     * 释放由 create_default_options 创建的选项                    */
    void (*free_options)(void* options);

} FormatHandler;

/* ================================================================
 * Registry Functions 注册表函数
 * ================================================================ */

/* Initialize the global format handler registry.
 * 初始化全局格式处理器注册表。
 * Must be called before any other registry functions.
 * 必须在其他注册表函数之前调用。
 * Returns: true on success. 成功返回 true。                        */
bool format_registry_init(void);

/* Shut down and free the format handler registry.
 * 关闭并释放格式处理器注册表。
 * Unregisters all handlers and frees internal data.
 * 注销所有处理器并释放内部数据。                                  */
void format_registry_shutdown(void);

/* Register a format handler. 注册一个格式处理器。
 * handler: pointer to a static/extern FormatHandler singleton.
 *          pointer to a static/extern FormatHandler 单例指针。
 * Returns: true on success, false if handler is NULL or
 *          a handler with the same name is already registered.
 *          成功返回 true，处理器为 NULL 或同名已注册返回 false。   */
bool format_register(const FormatHandler* handler);

/* Unregister a format handler by name. 按名称注销格式处理器。
 * Returns: true if found and removed. 找到并移除返回 true。        */
bool format_unregister(const char* format_name);

/* Find a handler by file extension (auto-detection).
 * 按文件扩展名查找处理器（自动检测）。
 * Extracts extension from filepath, looks up matching handler.
 * 从 filepath 提取扩展名，查找匹配的处理器。
 * Returns: handler pointer, or NULL if no match.
 * 返回值：处理器指针，无匹配返回 NULL。                            */
FormatHandler* format_find_by_extension(const char* filepath);

/* Find a handler by its format name. 按格式名称查找处理器。
 * Returns: handler pointer, or NULL if not found.
 * 返回值：处理器指针，未找到返回 NULL。                            */
FormatHandler* format_find_by_name(const char* format_name);

/* List all registered handlers. 列出所有已注册的处理器。
 * out_array: caller-provided array to fill with handler pointers
 *            调用者提供的数组，用于填充处理器指针
 * max_count: capacity of out_array out_array 的容量
 * Returns: number of handlers copied (may be > max_count if
 *          truncated). 复制的处理器数量（如果被截断可能 > max_count）。*/
int format_list_all(FormatHandler** out_array, int max_count);

/* ================================================================
 * Status Helpers 状态辅助函数
 * ================================================================ */

/* Initialize a HandlerStatus to OK/no-warnings.
 * 将 HandlerStatus 初始化为 OK/无警告。                           */
void handler_status_init(HandlerStatus* status);

/* Add a warning to a HandlerStatus.
 * 向 HandlerStatus 添加一条警告。
 * If warning_count already at MAX_WARNINGS, the new warning
 * is silently dropped. 如果 warning_count 已达 MAX_WARNINGS，
 * 新警告将被静默丢弃。
 * status:  status struct to modify 要修改的状态结构
 * code:    result code for this warning 此警告的结果码
 * fmt, ...: printf-style format string and args
 *           printf 风格的格式字符串及参数                        */
void handler_status_add_warning(HandlerStatus* status,
                                 HandlerResult code,
                                 const char* fmt, ...);

/* Get a human-readable string for a result code.
 * 获取结果码的人类可读字符串。
 * Returns: static string (do not free). 返回静态字符串（不要释放）。*/
const char* handler_result_to_string(HandlerResult code);

#endif /* FORMAT_HANDLER_H */
