/* ============================================================
 * md_handler.h - Markdown Format Handler
 * md_handler.h - Markdown 格式处理器
 *
 * Handles Markdown files by interpreting heading levels
 * (# through ######) as tree hierarchy levels. Body text
 * between headings is accumulated as node content.
 * 通过将标题级别（# 到 ######）解释为树层级来处理 Markdown 文件。
 * 标题之间的正文累积为节点内容。
 * ============================================================ */

#ifndef MD_HANDLER_H
#define MD_HANDLER_H

#include "format_handler.h"  /* FormatHandler, HandlerStatus */

/* Options for the MD handler */
typedef struct {
    bool lists_as_children;   /* Treat list items as child nodes */
    int  max_heading_level;   /* 1-6, default 6 */
} MdOptions;

extern const FormatHandler MD_HANDLER;

HandlerStatus md_parse(const char* filepath, struct Tree* tree,
                        void* options,
                        UserQueryCallback query_cb, void* cb_data);

HandlerStatus md_serialize(const char* filepath,
                            const struct Tree* tree, void* options);

bool md_detect(const char* filepath);

void* md_create_default_options(void);

void md_free_options(void* options);

#endif /* MD_HANDLER_H */
