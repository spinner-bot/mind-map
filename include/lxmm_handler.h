/* ============================================================
 * lxmm_handler.h - .lxmm Binary Mind Map Format Handler
 * lxmm_handler.h - .lxmm 二进制思维导图格式处理器
 *
 * Declares the LXMM format handler for parsing and serializing
 * the proprietary .lxmm binary mind map format. This format is
 * exclusive to this application — it stores tree structure,
 * per-node metadata (color, size, expanded state, format type),
 * and file-level configuration (canvas color, defaults).
 * 声明 LXMM 格式处理器，用于解析和序列化专有的 .lxmm 二进制
 * 思维导图格式。此格式为本应用专有 — 它存储树结构、
 * 每节点元数据（颜色、尺寸、展开状态、格式类型）和
 * 文件级配置（画布颜色、默认值）。
 *
 * ============================================================ */

#ifndef LXMM_HANDLER_H
#define LXMM_HANDLER_H

#include "format_handler.h"  /* FormatHandler, HandlerStatus, etc. */
#include <stdbool.h>

/* ================================================================
 * LXMM Handler Options  LXMM 处理器选项
 * ================================================================ */

/* Options specific to the LXMM handler (currently minimal).
 * LXMM 处理器特有的选项（目前很少）。                              */
typedef struct {
    bool dummy;  /* Placeholder for future options 未来选项的占位符 */
} LxmmOptions;

/* ================================================================
 * Handler Singleton  处理器单例
 * ================================================================ */

/* The singleton LXMM handler instance. LXMM 处理器的单例实例。    */
extern const FormatHandler LXMM_HANDLER;

/* ================================================================
 * Public API (for direct use or testing)
 * 公开 API（用于直接使用或测试）
 * ================================================================ */

/* Parse a .lxmm file into a Tree. 将 .lxmm 文件解析为 Tree。     */
HandlerStatus lxmm_parse(const char* filepath, struct Tree* tree,
                          void* options,
                          UserQueryCallback query_cb,
                          void* cb_data);

/* Serialize a Tree to a .lxmm file. 将 Tree 序列化为 .lxmm 文件。*/
HandlerStatus lxmm_serialize(const char* filepath,
                              const struct Tree* tree,
                              void* options);

/* Detect if a file is .lxmm (by extension + magic bytes).
 * 检测文件是否为 .lxmm（通过扩展名 + 魔数字节）。                 */
bool lxmm_detect(const char* filepath);

/* Create default LXMM handler options. 创建默认 LXMM 处理器选项。 */
void* lxmm_create_default_options(void);

/* Free LXMM handler options. 释放 LXMM 处理器选项。              */
void lxmm_free_options(void* options);

#endif /* LXMM_HANDLER_H */
