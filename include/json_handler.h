/* ============================================================
 * json_handler.h - JSON Format Handler
 * json_handler.h - JSON 格式处理器
 *
 * Declares the JSON format handler for parsing and serializing
 * JSON files. Phase 1 handles lists (arrays) only; dictionaries
 * are converted to lists with sorted keys and a warning.
 * 声明 JSON 格式处理器，用于解析和序列化 JSON 文件。
 * 一期仅处理列表（数组）；字典被转换为按键排序的列表并产生警告。
 *
 * Index-0 Convention 索引0约定:
 *   To solve the "branch info" problem (a node having both content
 *   and children), JSON arrays use index 0 to store branch content
 *   when in INDEX0_BRANCH mode. See format_handler.h for details.
 *   为解决"枝信息"问题（节点同时有内容和子节点），
 *   JSON 数组在 INDEX0_BRANCH 模式下使用索引0存储枝内容。
 * ============================================================ */

#ifndef JSON_HANDLER_H
#define JSON_HANDLER_H

#include "format_handler.h"  /* FormatHandler, HandlerStatus, etc. */

/* ================================================================
 * JSON Handler Options  JSON 处理器选项
 * ================================================================ */

/* Options specific to the JSON handler.
 * JSON 处理器特有的选项。
 *
 * index_mode: Controls how index 0 of arrays is interpreted.
 *             控制数组索引 0 的解释方式。
 *             INDEX0_BRANCH:  index0 = node content, children at 1+
 *                             index0 = 节点内容，子节点从 1+ 开始
 *             INDEX0_SIBLING: all indices are siblings
 *                             所有索引都是兄弟节点
 * ask_index_mode: If true, the handler will invoke the user query
 *                 callback to ask which mode to use. If false,
 *                 index_mode is used directly without asking.
 *                 如果为 true，处理器将调用用户查询回调询问使用哪种模式。
 *                 如果为 false，直接使用 index_mode 的值。         */
typedef struct {
    JsonIndexMode index_mode;       /* Selected index-0 mode 选择的索引0模式 */
    bool          ask_index_mode;   /* Whether to ask user on import/export
                                       导入/导出时是否询问用户 */
} JsonOptions;

/* ================================================================
 * Handler Singleton  处理器单例
 * ================================================================ */

/* The singleton JSON handler instance.
 * JSON 处理器的单例实例。
 * Declared extern here, defined in json_handler.c.
 * 在此声明为 extern，在 json_handler.c 中定义。                  */
extern const FormatHandler JSON_HANDLER;

/* ================================================================
 * Public API (for direct use or testing)
 * 公开 API（用于直接使用或测试）
 * ================================================================ */

/* Parse a JSON file into a Tree. 将 JSON 文件解析为 Tree。
 * See FormatHandler.parse for parameter documentation.
 * 参数文档参见 FormatHandler.parse。                             */
HandlerStatus json_parse(const char* filepath, struct Tree* tree,
                          void* options,
                          UserQueryCallback query_cb,
                          void* cb_data);

/* Serialize a Tree to a JSON file. 将 Tree 序列化为 JSON 文件。
 * See FormatHandler.serialize for parameter documentation.
 * 参数文档参见 FormatHandler.serialize。                         */
HandlerStatus json_serialize(const char* filepath,
                              const struct Tree* tree,
                              void* options);

/* Detect if a file is JSON (by extension). 通过扩展名检测文件是否为 JSON。*/
bool json_detect(const char* filepath);

/* Create default JSON handler options. 创建默认 JSON 处理器选项。
 * Default: index_mode = INDEX0_BRANCH, ask_index_mode = true.
 * 默认：index_mode = INDEX0_BRANCH，ask_index_mode = true。       */
void* json_create_default_options(void);

/* Free JSON handler options. 释放 JSON 处理器选项。              */
void json_free_options(void* options);

#endif /* JSON_HANDLER_H */
