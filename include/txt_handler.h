/* ============================================================
 * txt_handler.h - TXT Numbered Outline Format Handler
 * txt_handler.h - TXT 编号大纲格式处理器
 *
 * Handles the numbered outline format where each line starts
 * with a numeric identifier like "1.", "2.", "1.2.", "1.2.1."
 * that indicates the tree hierarchy level.
 * 处理编号大纲格式，每行以数字标识符（如 "1."、"2."、"1.2."）开头，
 * 指示树的层级结构。
 * ============================================================ */

#ifndef TXT_HANDLER_H
#define TXT_HANDLER_H

#include "format_handler.h"  /* FormatHandler, HandlerStatus, etc. */
#include "tree.h"            /* Tree, TreeNode */

/* ================================================================
 * TXT Handler Options  TXT 处理器选项
 * ================================================================ */

/* How to handle gaps in numbering (e.g., "1.1.", "1.3." but no "1.2.")
 * 如何处理编号中的空隙（如只有 "1.1."、"1.3." 但没有 "1.2."）
 *
 * TXT_REINDEX_ASK:          Ask user via callback
 *                           通过回调询问用户
 * TXT_REINDEX_AUTO_SHIFT:   Silently shift subsequent numbers to fill gaps
 *                           静默将后续编号前移填补空隙
 * TXT_REINDEX_PLACEHOLDER:  Create empty placeholder nodes at missing indices
 *                           在缺失索引处创建空占位节点                */
typedef enum {
    TXT_REINDEX_ASK          = 0,
    TXT_REINDEX_AUTO_SHIFT   = 1,
    TXT_REINDEX_PLACEHOLDER  = 2
} TxtReindexMode;

/* Options for the TXT handler.  TXT 处理器的选项。
 * reindex_mode: How to handle non-contiguous numbering.
 *               如何处理非连续编号。                              */
typedef struct {
    TxtReindexMode reindex_mode;
} TxtOptions;

/* ================================================================
 * Handler Singleton  处理器单例
 * ================================================================ */

extern const FormatHandler TXT_HANDLER;

/* ================================================================
 * Public API  公开 API
 * ================================================================ */

HandlerStatus txt_parse(const char* filepath, struct Tree* tree,
                         void* options,
                         UserQueryCallback query_cb, void* cb_data);

HandlerStatus txt_serialize(const char* filepath,
                             const struct Tree* tree, void* options);

bool txt_detect(const char* filepath);

void* txt_create_default_options(void);

void txt_free_options(void* options);

#endif /* TXT_HANDLER_H */
