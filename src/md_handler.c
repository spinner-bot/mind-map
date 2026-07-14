/* ============================================================
 * md_handler.c - Markdown Format Handler Implementation
 * md_handler.c - Markdown 格式处理器实现
 *
 * Parses and serializes Markdown files using heading levels
 * (# to ######) as the primary tree structure indicator.
 * 使用标题级别（# 到 ######）作为主要树结构指示器来解析和
 * 序列化 Markdown 文件。
 *
 * Format specification 格式规范:
 *   - Lines matching ^(#{1,6})\s+(.+)$ are heading lines
 *     匹配 ^(#{1,6})\s+(.+)$ 的行为标题行
 *   - Number of # characters (1-6) = tree depth (1-6)
 *     # 字符数 (1-6) = 树深度 (1-6)
 *   - Heading level 1 = top-level (child of root)
 *     标题级别 1 = 顶级（root 的子节点）
 *   - Non-heading text between headings = body text of the
 *     nearest preceding heading
 *     标题之间的非标题文本 = 最近前一个标题的正文
 *   - Level skip (e.g., # then ###) creates empty intermediate nodes
 *     跳级（如 # 后直接 ###）创建空中间节点
 *   - Code blocks (```) are preserved as literal text
 *     代码块 (```) 作为字面文本保留
 * ============================================================ */

#include "md_handler.h"
#include "tree.h"
#include "utils.h"          /* mem_alloc, str_trim, str_dup, SAFE_FREE */
#include "encoding.h"       /* encoding_read/write_file_utf8 */

#include <stdlib.h>         /* free */
#include <string.h>         /* strlen, strchr, strncmp, strcpy, strcat */
#include <stdio.h>          /* snprintf */
#include <ctype.h>          /* isspace */

/* ================================================================
 * MD Parsing  MD 解析
 * ================================================================ */

/* Parse a heading line and extract the level and title.
 * 解析标题行并提取级别和标题。
 * Input: "##  My Title" → level=2, title_start points to "My Title"
 * Returns level (1-6), or 0 if not a heading line.
 * 返回 level (1-6)，非标题行返回 0。                             */
static int parse_heading_line(const char* line,
                               const char** title_start) {
    if (line == NULL || title_start == NULL) {
        return 0;
    }

    /* Count leading # characters */
    int level = 0;
    const char* p = line;
    while (*p == '#') {
        level++;
        p++;
    }

    /* Must have 1-6 #'s followed by whitespace or end */
    if (level < 1 || level > 6) {
        return 0;
    }

    /* Must be followed by a space or tab */
    if (*p != ' ' && *p != '\t') {
        return 0;  /* Not a heading (e.g., "##not-a-heading") */
    }

    /* Skip whitespace after #'s */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    *title_start = p;
    return level;
}

/* Parse a Markdown file into a Tree.
 * 将 Markdown 文件解析为 Tree。
 *
 * Algorithm 算法:
 *   1. Read file line by line
 *   2. Lines starting with # (1-6) are headings → create nodes
 *   3. Other lines are body text → accumulate into current node
 *   4. Handle level skips by creating empty intermediate nodes
 *   5. Handle code blocks (```) as literal text                  */
HandlerStatus md_parse(const char* filepath, struct Tree* tree,
                        void* options,
                        UserQueryCallback query_cb, void* cb_data) {
    HandlerStatus status;
    handler_status_init(&status);
    (void)query_cb;   /* 一期 MD 处理不需要询问用户 */
    (void)cb_data;

    if (filepath == NULL || tree == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* 读取选项 */
    int max_heading = 6;
    if (options != NULL) {
        MdOptions* opts = (MdOptions*)options;
        max_heading = opts->max_heading_level;
        if (max_heading < 1) max_heading = 1;
        if (max_heading > 6) max_heading = 6;
    }

    /* 读取文件 */
    EncodingType enc;
    char* utf8_text = encoding_read_file_utf8(filepath, &enc);
    if (utf8_text == NULL) {
        status.result_code = RESULT_ERROR_FILE_READ;
        return status;
    }

    /* Parse line by line */
    const char* p = utf8_text;
    const char* line_start = p;

    /* Track the current heading node at each level (1-6) */
    TreeNode* heading_stack[7] = { NULL };  /* index 0 unused, 1-6 used */
    heading_stack[0] = tree->root;           /* level 0 = root */

    /* Body text buffer for the current node */
    char* body_buffer = NULL;
    int   body_len    = 0;
    int   body_cap    = 0;
    TreeNode* body_owner = NULL;  /* Which node is accumulating body text */

    bool in_code_block = false;  /* Inside a ``` fenced code block */

    while (*p != '\0') {
        /* Find end of current line */
        if (*p == '\n' || (*p == '\r' && *(p+1) == '\n')) {
            /* Extract line */
            int line_len = (int)(p - line_start);
            if (line_len > 0 || (p > line_start)) {
                char* line = (char*)mem_alloc(line_len + 1);
                memcpy(line, line_start, line_len);
                line[line_len] = '\0';
                /* Trim trailing carriage return */
                if (line_len > 0 && line[line_len-1] == '\r') {
                    line[line_len-1] = '\0';
                    line_len--;
                }

                /* Check for code block fence */
                if (strncmp(line, "```", 3) == 0) {
                    in_code_block = !in_code_block;
                    if (in_code_block && body_owner != NULL) {
                        /* Add code fence to body text */
                        int add_len = line_len + 1;
                        if (body_len + add_len >= body_cap) {
                            body_cap = (body_len + add_len) * 2;
                            body_buffer = (char*)mem_realloc(
                                body_buffer, body_cap);
                        }
                        memcpy(body_buffer + body_len, line, line_len);
                        body_len += line_len;
                        body_buffer[body_len++] = '\n';
                        body_buffer[body_len] = '\0';
                    }
                    free(line);
                    goto next_line_md;
                }

                /* Inside code block: accumulate as literal text */
                if (in_code_block) {
                    if (body_owner != NULL) {
                        int add_len = line_len + 1;
                        if (body_len + add_len >= body_cap) {
                            body_cap = (body_len + add_len) * 2;
                            body_buffer = (char*)mem_realloc(
                                body_buffer, body_cap);
                        }
                        memcpy(body_buffer + body_len, line, line_len);
                        body_len += line_len;
                        body_buffer[body_len++] = '\n';
                        body_buffer[body_len] = '\0';
                    }
                    free(line);
                    goto next_line_md;
                }

                /* Check if this is a heading line */
                const char* title;
                int level = parse_heading_line(line, &title);

                if (level > 0 && level <= max_heading) {
                    /* Flush accumulated body text to the current body_owner */
                    if (body_owner != NULL && body_buffer != NULL &&
                        body_len > 0) {
                        /* Append body text to existing content */
                        char* new_content;
                        if (body_owner->content != NULL) {
                            int old_len = (int)strlen(body_owner->content);
                            new_content = (char*)mem_alloc(
                                old_len + body_len + 2);
                            sprintf(new_content, "%s\n%s",
                                    body_owner->content, body_buffer);
                        } else {
                            new_content = str_dup(body_buffer);
                        }
                        tree_node_set_content(body_owner, new_content);
                        free(new_content);
                        /* Reset body buffer */
                        body_len = 0;
                        if (body_buffer) body_buffer[0] = '\0';
                    }

                    /* Heading line: create/get node at this level */
                    /* Ensure parent at level-1 exists */
                    TreeNode* parent = heading_stack[level - 1];
                    if (parent == NULL) {
                        /* Parent doesn't exist: create empty intermediate nodes
                         * from the nearest existing ancestor up to level-1 */
                        int ancestor_level = level - 2;
                        while (ancestor_level >= 0 &&
                               heading_stack[ancestor_level] == NULL) {
                            ancestor_level--;
                        }
                        TreeNode* ancestor = heading_stack[ancestor_level];

                        /* Create missing levels */
                        for (int l = ancestor_level + 1; l < level; l++) {
                            TreeNode* empty_node = tree_node_add_child(
                                ancestor, NULL);
                            heading_stack[l] = empty_node;
                            ancestor = empty_node;
                            handler_status_add_warning(&status,
                                RESULT_WARN_HEADING_SKIP,
                                "Heading level skip: created empty node "
                                "at level %d", l);
                        }
                        parent = heading_stack[level - 1];
                    }

                    /* Add new heading node */
                    /* Trim trailing whitespace from title */
                    char* trimmed_title = str_dup(title);
                    int tl = (int)strlen(trimmed_title);
                    while (tl > 0 && (trimmed_title[tl-1] == ' ' ||
                           trimmed_title[tl-1] == '\t' ||
                           trimmed_title[tl-1] == '\r')) {
                        trimmed_title[--tl] = '\0';
                    }

                    TreeNode* new_node = tree_node_add_child(
                        parent, trimmed_title);
                    heading_stack[level] = new_node;

                    /* Clear higher levels (this new heading replaces any
                     * previous heading at this level or deeper) */
                    for (int l = level + 1; l <= 6; l++) {
                        heading_stack[l] = NULL;
                    }

                    /* Start accumulating body text for this new node */
                    body_owner = new_node;
                    body_len = 0;
                    if (body_buffer == NULL) {
                        body_cap = 256;
                        body_buffer = (char*)mem_alloc(body_cap);
                    }
                    body_buffer[0] = '\0';

                    free(trimmed_title);
                } else {
                    /* Non-heading line: accumulate as body text */
                    if (body_owner != NULL) {
                        int add_len = line_len + 1;  /* +1 for '\n' */
                        if (body_len + add_len >= body_cap) {
                            body_cap = (body_len + add_len) * 2;
                            body_buffer = (char*)mem_realloc(
                                body_buffer, body_cap);
                        }
                        memcpy(body_buffer + body_len, line, line_len);
                        body_len += line_len;
                        body_buffer[body_len++] = '\n';
                        body_buffer[body_len] = '\0';
                    }
                }

                free(line);
            }

next_line_md:
            /* Skip newline characters */
            if (*p == '\r' && *(p+1) == '\n') {
                p += 2;
            } else {
                p++;
            }
            line_start = p;
        } else {
            p++;
        }
    }

    /* Handle last line (if file doesn't end with newline) */
    if (*line_start != '\0') {
        int line_len = (int)strlen(line_start);
        char* line = (char*)mem_alloc(line_len + 1);
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        if (line_len > 0 && line[line_len-1] == '\r') {
            line[--line_len] = '\0';
        }

        if (!in_code_block) {
            const char* title;
            int level = parse_heading_line(line, &title);
            if (level > 0 && level <= max_heading) {
                /* Flush body text, then add heading */
                if (body_owner != NULL && body_buffer != NULL && body_len > 0) {
                    char* new_content;
                    if (body_owner->content != NULL) {
                        int old_len = (int)strlen(body_owner->content);
                        new_content = (char*)mem_alloc(old_len + body_len + 2);
                        sprintf(new_content, "%s\n%s",
                                body_owner->content, body_buffer);
                    } else {
                        new_content = str_dup(body_buffer);
                    }
                    tree_node_set_content(body_owner, new_content);
                    free(new_content);
                }
                /* Add the heading node */
                TreeNode* parent = heading_stack[level - 1];
                if (parent == NULL) parent = tree->root;
                char* trimmed_title = str_dup(title);
                int tl = (int)strlen(trimmed_title);
                while (tl > 0 && (trimmed_title[tl-1] == ' ' ||
                       trimmed_title[tl-1] == '\r')) {
                    trimmed_title[--tl] = '\0';
                }
                tree_node_add_child(parent, trimmed_title);
                free(trimmed_title);
            } else {
                /* Body text for last line */
                if (body_owner != NULL && body_buffer != NULL) {
                    int add_len = line_len + 1;
                    if (body_len + add_len >= body_cap) {
                        body_cap = (body_len + add_len) * 2;
                        body_buffer = (char*)mem_realloc(
                            body_buffer, body_cap);
                    }
                    memcpy(body_buffer + body_len, line, line_len);
                    body_len += line_len;
                    body_buffer[body_len++] = '\n';
                    body_buffer[body_len] = '\0';
                }
            }
        }
        free(line);
    }

    /* Flush remaining body text */
    if (body_owner != NULL && body_buffer != NULL && body_len > 0) {
        /* Remove trailing newline from body */
        while (body_len > 0 && body_buffer[body_len-1] == '\n') {
            body_buffer[--body_len] = '\0';
        }
        if (body_len > 0) {
            char* new_content;
            if (body_owner->content != NULL) {
                int old_len = (int)strlen(body_owner->content);
                new_content = (char*)mem_alloc(old_len + body_len + 2);
                sprintf(new_content, "%s\n%s",
                        body_owner->content, body_buffer);
            } else {
                new_content = str_dup(body_buffer);
            }
            tree_node_set_content(body_owner, new_content);
            free(new_content);
        }
    }

    free(body_buffer);
    free(utf8_text);

    /* Update tree metadata */
    tree->source_file   = str_dup(filepath);
    tree->source_format = str_dup("md");
    tree_recalculate_depths(tree);
    tree->total_nodes = tree_count_nodes(tree);

    return status;
}

/* ================================================================
 * MD Serialization  MD 序列化
 * ================================================================ */

/* Serialize a Tree to Markdown format.
 * 将 Tree 序列化为 Markdown 格式。
 *
 * Output format 输出格式:
 *   # Node content (depth 1)
 *   ## Node content (depth 2)
 *   ### Node content (depth 3)
 *   ...
 *   ###### Node content (depth 6+)
 *
 * Body text (content after heading title) is emitted as paragraphs.
 * 正文（标题后的内容）作为段落输出。                              */
HandlerStatus md_serialize(const char* filepath,
                            const struct Tree* tree,
                            void* options) {
    HandlerStatus status;
    handler_status_init(&status);
    (void)options;

    if (filepath == NULL || tree == NULL || tree->root == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    int buf_cap = tree->total_nodes * 100 + 512;
    int buf_len = 0;
    char* output = (char*)mem_alloc(buf_cap);
    output[0] = '\0';

    #define APPEND(fmt, ...) do { \
        char _tmp[2048]; \
        int _n = snprintf(_tmp, sizeof(_tmp), fmt, ##__VA_ARGS__); \
        if (_n > 0) { \
            while (buf_len + _n + 1 >= buf_cap) { \
                buf_cap *= 2; \
                output = (char*)mem_realloc(output, buf_cap); \
            } \
            memcpy(output + buf_len, _tmp, _n); \
            buf_len += _n; \
        } \
    } while(0)

    /* DFS pre-order traversal using a stack */
    typedef struct {
        TreeNode* node;
        int       depth;   /* 1-based depth in output (# count) */
    } StackEntry;

    int stack_cap = 64;
    StackEntry* stack = (StackEntry*)mem_alloc(
        stack_cap * sizeof(StackEntry));
    int stack_top = 0;

    /* Push root's children in reverse order */
    for (int i = tree->root->child_count - 1; i >= 0; i--) {
        TreeNode* child = tree->root->children[i];
        if (stack_top >= stack_cap) {
            stack_cap *= 2;
            stack = (StackEntry*)mem_realloc(stack,
                stack_cap * sizeof(StackEntry));
        }
        stack[stack_top].node  = child;
        stack[stack_top].depth = 1;
        stack_top++;
    }

    while (stack_top > 0) {
        stack_top--;
        TreeNode* node = stack[stack_top].node;
        int depth      = stack[stack_top].depth;

        /* Clamp heading level to 6 */
        int heading_level = (depth > 6) ? 6 : depth;

        /* Output heading: "##...## Title" */
        for (int i = 0; i < heading_level; i++) {
            APPEND("#");
        }
        APPEND(" ");
        if (node->content != NULL) {
            APPEND("%s", node->content);
        }
        APPEND("\n\n");

        /* Push children in reverse order */
        for (int i = node->child_count - 1; i >= 0; i--) {
            TreeNode* child = node->children[i];
            if (stack_top >= stack_cap) {
                stack_cap *= 2;
                stack = (StackEntry*)mem_realloc(stack,
                    stack_cap * sizeof(StackEntry));
            }
            stack[stack_top].node  = child;
            stack[stack_top].depth = depth + 1;
            stack_top++;
        }
    }

    free(stack);

    /* Write to file (UTF-8 with BOM) */
    output[buf_len] = '\0';
    bool ok = encoding_write_file_utf8(filepath, output, ENC_UTF8_BOM);
    free(output);

    if (!ok) {
        status.result_code = RESULT_ERROR_FILE_WRITE;
    }

    #undef APPEND
    return status;
}

/* Detect MD files by extension (.md) */
bool md_detect(const char* filepath) {
    if (filepath == NULL) return false;
    const char* dot = strrchr(filepath, '.');
    if (dot == NULL) return false;
    const char* ext = dot, *exp = ".md";
    while (*ext && *exp) {
        char e = *ext, x = *exp;
        if (e >= 'A' && e <= 'Z') e += ('a' - 'A');
        if (x >= 'A' && x <= 'Z') x += ('a' - 'A');
        if (e != x) return false;
        ext++; exp++;
    }
    return (*ext == '\0' && *exp == '\0');
}

void* md_create_default_options(void) {
    MdOptions* opts = (MdOptions*)mem_alloc(sizeof(MdOptions));
    opts->lists_as_children = false;
    opts->max_heading_level = 6;
    return opts;
}

void md_free_options(void* options) {
    if (options != NULL) free(options);
}

const FormatHandler MD_HANDLER = {
    .format_name            = "md",
    .extension              = ".md",
    .description            = "Markdown (Headings)",
    .parse                  = md_parse,
    .serialize              = md_serialize,
    .detect                 = md_detect,
    .create_default_options = md_create_default_options,
    .free_options           = md_free_options
};
