/* ============================================================
 * txt_handler.c - TXT Numbered Outline Format Implementation
 * txt_handler.c - TXT 编号大纲格式实现
 *
 * Parses and serializes the numbered outline format where each
 * line has a numeric prefix indicating the hierarchy.
 * 解析和序列化编号大纲格式，每行有数字前缀指示层级。
 *
 * Format specification 格式规范:
 *   - Each line: "<numbers.> <content>"
 *     每行："<数字.> <内容>"
 *   - Numbers separated by dots (e.g., "1.", "1.1.", "2.3.1.")
 *     数字用点分隔（如 "1."、"1.1."、"2.3.1."）
 *   - The number of dot-separated parts = depth level (1-based)
 *     点分隔的段数 = 深度层级（从1开始）
 *   - Identifiers are only valid at the start of a line (after newline)
 *     标识符仅在行首（换行符后）有效
 *   - Decimal numbers mid-line (like "3.14") are NOT identifiers
 *     行中的小数（如 "3.14"）不是标识符
 *
 * Index conversion 索引转换:
 *   External (file): 1-based numbering ("1.", "2.", "1.1.")
 *   Internal (tree):  0-based indices (children[0], children[1])
 *
 * Branch info 枝信息:
 *   When both "2.3." (own content) and "2.3.1." (child) exist,
 *   the node at path [2,3] has both content and children.
 *   This is detected and marked as has_branch_info = true.
 *   当 "2.3."（自身内容）和 "2.3.1."（子节点）同时存在时，
 *   路径 [2,3] 处的节点同时有内容和子节点。
 *   这会被检测并标记为 has_branch_info = true。
 * ============================================================ */

#include "txt_handler.h"
#include "tree.h"
#include "utils.h"          /* mem_alloc, str_trim, str_dup, SAFE_FREE */
#include "encoding.h"       /* encoding_read_file_utf8, encoding_write_file_utf8 */

#include <stdlib.h>         /* free, atoi */
#include <string.h>         /* strlen, strchr, strcmp, strcpy */
#include <stdio.h>          /* snprintf, sscanf */
#include <ctype.h>          /* isdigit */

/* ================================================================
 * Internal helpers 内部辅助函数
 * ================================================================ */

/* Parse a TXT identifier like "1.2.3." into an array of integers.
 * 将 TXT 标识符（如 "1.2.3."）解析为整数数组。
 *
 * Parameters 参数:
 *   line       - The input line starting with the identifier
 *                以标识符开头的输入行
 *   indices    - Output array to fill with 0-based indices
 *                输出的 0-based 索引数组
 *   max_depth  - Maximum number of indices to parse
 *                要解析的最大索引数
 *
 * Returns 返回值:
 *   Number of indices parsed (depth), or -1 on error.
 *   解析的索引数（深度），出错返回 -1。
 *
 * The function converts 1-based TXT numbering to 0-based internal
 * indices. For example, "1.2.3." produces [0, 1, 2].
 * 此函数将 1-based 的 TXT 编号转换为 0-based 的内部索引。
 * 例如 "1.2.3." 产生 [0, 1, 2]。                                 */
static int parse_txt_identifier(const char* line, int* indices,
                                 int max_depth) {
    if (line == NULL || indices == NULL || max_depth <= 0) {
        return -1;
    }

    const char* p = line;
    int depth = 0;

    while (*p != '\0' && depth < max_depth) {
        /* 跳过行首的空白（仅在第一次迭代时） */
        if (depth == 0) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
        }

        /* 检查是否是数字（标识符部分） */
        if (!isdigit((unsigned char)*p)) {
            /* 不是数字 —— 标识符结束，或者这不是一个标识符行 */
            if (depth == 0) {
                return -1;  /* 行不以标识符开头 */
            }
            break;
        }

        /* 读取数字 */
        int num = 0;
        while (isdigit((unsigned char)*p)) {
            num = num * 10 + (*p - '0');
            p++;
        }

        /* 验证数字不为 0（文档规定：不存在 0 索引） */
        if (num <= 0) {
            return -1;  /* 编号不能为 0 */
        }

        /* 转换为 0-based 索引存储 */
        indices[depth] = num - 1;
        depth++;

        /* 期望 '.' 作为分隔符（或标识符结束符） */
        if (*p == '.') {
            p++;  /* 跳过 '.' */
            /* 检查 '.' 后面是否是空格或行尾（标识符结束）
             * 或是否是数字（多级编号的下一级） */
            if (*p == ' ' || *p == '\t' || *p == '\0' || *p == '\r' || *p == '\n') {
                /* 标识符结束 —— 后面是内容文本 */
                break;
            } else if (isdigit((unsigned char)*p)) {
                /* 下一级编号 —— 继续循环 */
                continue;
            } else {
                /* 非数字也非空白 —— 可能不是合法的标识符 */
                break;
            }
        } else {
            /* 数字后不是 '.' —— 标识符格式不完整 */
            break;
        }
    }

    return depth;  /* 返回解析出的层级深度 */
}

/* Extract the content part of a TXT line (everything after the identifier).
 * 提取 TXT 行的内容部分（标识符之后的所有内容）。
 *
 * Returns pointer into the original string (no allocation).
 * 返回指向原始字符串的指针（不分配内存）。                      */
static const char* extract_txt_content(const char* line) {
    if (line == NULL) {
        return NULL;
    }

    const char* p = line;

    /* 跳过标识符部分 */
    while (*p != '\0') {
        /* 跳过数字 */
        if (isdigit((unsigned char)*p)) {
            while (isdigit((unsigned char)*p)) p++;
        }
        /* 跳过 '.' */
        if (*p == '.') {
            p++;
        }
        /* 如果下一个字符是数字，继续（多级编号） */
        if (isdigit((unsigned char)*p)) {
            continue;
        }
        /* 否则标识符结束 */
        break;
    }

    /* 跳过空白字符（标识符和内容之间的空格） */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    /* 移除行尾的换行符和回车符 */
    /* 使用原地修改不方便（因为返回的是指针），
     * 调用者应自行处理首尾空白。                               */

    return p;
}

/* ================================================================
 * TXT Parsing  TXT 解析
 * ================================================================ */

/* Parse a TXT file into a Tree.
 * 将 TXT 文件解析为 Tree。
 *
 * Parsing algorithm 解析算法:
 *   1. Read the file line by line
 *   2. For each line, try to parse the identifier pattern
 *   3. Navigate/create the path in the tree
 *   4. Set the node content
 *   5. Pass 2: detect branch info and numbering gaps              */
HandlerStatus txt_parse(const char* filepath, struct Tree* tree,
                         void* options,
                         UserQueryCallback query_cb, void* cb_data) {
    HandlerStatus status;
    handler_status_init(&status);

    if (filepath == NULL || tree == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* 读取选项 */
    TxtReindexMode reindex_mode = TXT_REINDEX_ASK;
    if (options != NULL) {
        TxtOptions* opts = (TxtOptions*)options;
        reindex_mode = opts->reindex_mode;
    }

    /* 读取文件并转换为 UTF-8 */
    EncodingType enc;
    char* utf8_text = encoding_read_file_utf8(filepath, &enc);
    if (utf8_text == NULL) {
        status.result_code = RESULT_ERROR_FILE_READ;
        return status;
    }

    /* ================================================================
     * PASS 1: Parse identifiers and build tree
     * 第一遍：解析标识符并构建树                                     */

    /* Split into lines for processing */
    const char* p = utf8_text;
    const char* line_start = utf8_text;

    while (*p != '\0') {
        /* 找到行尾 */
        if (*p == '\n' || (*p == '\r' && *(p + 1) == '\n')) {
            /* 提取当前行 */
            int line_len = (int)(p - line_start);
            if (line_len > 0) {
                /* 创建行缓冲（临时） */
                char* line = (char*)mem_alloc(line_len + 1);
                memcpy(line, line_start, line_len);
                line[line_len] = '\0';

                /* 解析标识符 */
                int indices[32];  /* 最大支持 32 级深度 */
                int depth = parse_txt_identifier(line, indices, 32);
                if (depth > 0) {
                    /* 有效的标识符行 —— 导航到树中的正确位置 */
                    TreeNode* current = tree->root;

                    /* 沿路径导航，必要时创建中间节点 */
                    for (int d = 0; d < depth; d++) {
                        int target_idx = indices[d];

                        /* 确保父节点有足够的子节点 */
                        while (current->child_count <= target_idx) {
                            /* 创建空的占位节点 */
                            tree_node_add_child(current, NULL);
                        }

                        /* 移动到目标子节点 */
                        current = current->children[target_idx];

                        /* 如果是路径的最后一个索引，设置内容 */
                        if (d == depth - 1) {
                            const char* content = extract_txt_content(line);
                            if (content != NULL && *content != '\0') {
                                /* 去除尾部换行符 */
                                char* trimmed = str_dup(content);
                                /* 去除尾部 \r 和 \n */
                                int tl = (int)strlen(trimmed);
                                while (tl > 0 &&
                                       (trimmed[tl-1] == '\r' ||
                                        trimmed[tl-1] == '\n')) {
                                    trimmed[--tl] = '\0';
                                }
                                if (tl > 0) {
                                    tree_node_set_content(current, trimmed);
                                }
                                free(trimmed);
                            }
                        }
                    }
                }

                free(line);
            }

            /* 跳过换行符 */
            if (*p == '\r' && *(p + 1) == '\n') {
                p += 2;
            } else {
                p++;
            }
            line_start = p;
        } else {
            p++;
        }
    }

    /* 处理最后一行（没有尾部换行符的情况） */
    if (*line_start != '\0') {
        int line_len = (int)strlen(line_start);
        char* line = (char*)mem_alloc(line_len + 1);
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        int indices[32];
        int depth = parse_txt_identifier(line, indices, 32);
        if (depth > 0) {
            TreeNode* current = tree->root;
            for (int d = 0; d < depth; d++) {
                int target_idx = indices[d];
                while (current->child_count <= target_idx) {
                    tree_node_add_child(current, NULL);
                }
                current = current->children[target_idx];
                if (d == depth - 1) {
                    const char* content = extract_txt_content(line);
                    if (content != NULL && *content != '\0') {
                        char* trimmed = str_dup(content);
                        int tl = (int)strlen(trimmed);
                        while (tl > 0 &&
                               (trimmed[tl-1] == '\r' ||
                                trimmed[tl-1] == '\n')) {
                            trimmed[--tl] = '\0';
                        }
                        if (tl > 0) {
                            tree_node_set_content(current, trimmed);
                        }
                        free(trimmed);
                    }
                }
            }
        }
        free(line);
    }

    /* ================================================================
     * PASS 2: Detect gaps and handle reindexing
     * 第二遍：检测空隙并处理重索引                                    */

    /* Detecting gaps: for each parent, check its children.
     * If there are empty placeholder nodes (content==NULL, children==0)
     * between non-empty nodes, it indicates a gap.
     * 检测空隙：对于每个父节点，检查其子节点。
     * 如果在非空节点之间有空的占位节点（content==NULL, children==0），
     * 说明存在空隙。                                                */

    /* Use BFS to visit all nodes */
    {
        int cap = 64;
        TreeNode** queue = (TreeNode**)mem_alloc(cap * sizeof(TreeNode*));
        int front = 0, rear = 0, qsize = 0;
        queue[rear++] = tree->root;
        qsize = 1;

        while (qsize > 0) {
            TreeNode* node = queue[front++];
            qsize--;

            /* Check this node's children for gaps */
            bool found_gap = false;
            bool found_non_empty_after_gap = false;

            for (int i = 0; i < node->child_count; i++) {
                TreeNode* child = node->children[i];
                bool is_empty_placeholder = (child->content == NULL &&
                                              child->child_count == 0);

                if (!is_empty_placeholder && found_gap) {
                    found_non_empty_after_gap = true;
                    break;
                }
                if (is_empty_placeholder && !found_gap) {
                    /* Check if there are non-empty nodes later */
                    for (int j = i + 1; j < node->child_count; j++) {
                        TreeNode* later = node->children[j];
                        if (later->content != NULL ||
                            later->child_count > 0) {
                            found_gap = true;
                            break;
                        }
                    }
                }
            }

            if (found_gap && found_non_empty_after_gap) {
                /* Ask user how to handle gaps (or use preset mode) */
                TxtReindexMode action = reindex_mode;
                if (action == TXT_REINDEX_ASK && query_cb != NULL) {
                    const char* choices[] = {
                        "Auto Reindex (shift to fill gaps)",
                        "Empty Placeholder (keep gaps)"
                    };
                    int choice = query_cb(
                        "TXT Import - Numbering Gap Detected",
                        "Non-contiguous numbering was found in the outline. "
                        "How should it be handled?\n\n"
                        "\"Auto Reindex\": Subsequent items are shifted to "
                        "fill the gaps.\n"
                        "\"Empty Placeholder\": Empty items are inserted at "
                        "the missing positions.",
                        choices, 2, cb_data);
                    if (choice < 0) {
                        status.result_code = RESULT_ERROR_USER_CANCEL;
                        free(queue);
                        free(utf8_text);
                        return status;
                    }
                    action = (choice == 0) ? TXT_REINDEX_AUTO_SHIFT
                                           : TXT_REINDEX_PLACEHOLDER;
                }

                if (action == TXT_REINDEX_AUTO_SHIFT) {
                    /* Compact the children array: remove empty placeholders */
                    int write_idx = 0;
                    for (int i = 0; i < node->child_count; i++) {
                        TreeNode* child = node->children[i];
                        bool is_empty = (child->content == NULL &&
                                          child->child_count == 0);
                        /* Also check if there's a non-empty node after */
                        bool has_content_after = false;
                        for (int j = i + 1; j < node->child_count; j++) {
                            TreeNode* later = node->children[j];
                            if (later->content != NULL ||
                                later->child_count > 0) {
                                has_content_after = true;
                                break;
                            }
                        }

                        if (is_empty && has_content_after) {
                            /* This is a gap placeholder — remove it */
                            child->parent = NULL;  /* Detach */
                            tree_node_free_subtree(child);
                            /* Don't increment write_idx */
                        } else {
                            /* Keep this node */
                            if (write_idx != i) {
                                node->children[write_idx] = child;
                                node->children[i] = NULL;
                            }
                            write_idx++;
                        }
                    }
                    /* Clear trailing entries */
                    for (int i = write_idx; i < node->child_count; i++) {
                        node->children[i] = NULL;
                    }
                    node->child_count = write_idx;

                    handler_status_add_warning(&status,
                        RESULT_WARN_NUMBER_GAP,
                        "Non-contiguous numbering detected and auto-reindexed "
                        "(shifted to fill gaps).");
                } else {
                    /* TXT_REINDEX_PLACEHOLDER: keep gaps as empty nodes */
                    handler_status_add_warning(&status,
                        RESULT_WARN_NUMBER_GAP,
                        "Non-contiguous numbering detected. "
                        "Empty placeholder nodes created at missing positions.");
                }
            }

            /* Enqueue children */
            for (int i = 0; i < node->child_count; i++) {
                if (qsize >= cap) {
                    cap *= 2;
                    queue = (TreeNode**)mem_realloc(queue,
                        cap * sizeof(TreeNode*));
                }
                queue[rear++] = node->children[i];
                qsize++;
            }
        }
        free(queue);
    }

    /* Update tree metadata */
    tree->source_file   = str_dup(filepath);
    tree->source_format = str_dup("txt");
    tree_recalculate_depths(tree);
    tree->total_nodes = tree_count_nodes(tree);

    free(utf8_text);
    return status;
}

/* ================================================================
 * TXT Serialization  TXT 序列化
 * ================================================================ */

/* Serialize a Tree to TXT numbered outline format.
 * 将 Tree 序列化为 TXT 编号大纲格式。
 *
 * Serialization algorithm 序列化算法:
 *   1. BFS/DFS traversal to enumerate nodes in order
 *   2. For each node, compute its 1-based path (breadcrumb)
 *   3. Output: "<path.> <content>\n"                             */
HandlerStatus txt_serialize(const char* filepath,
                             const struct Tree* tree,
                             void* options) {
    HandlerStatus status;
    handler_status_init(&status);
    (void)options;  /* 未使用 */

    if (filepath == NULL || tree == NULL || tree->root == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* Build output string using a dynamic buffer */
    int buf_cap = tree->total_nodes * 80 + 256;
    int buf_len = 0;
    char* output = (char*)mem_alloc(buf_cap);
    output[0] = '\0';

    /* Helper to append to the output buffer */
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

    /* BFS traversal to output nodes in level order.
     * For TXT format, we actually want pre-order (DFS) so that
     * children appear immediately after their parent.            */
    /* Use a stack for DFS pre-order */
    typedef struct {
        TreeNode* node;
        int*      path;     /* 1-based path to this node */
        int       path_len;
    } StackEntry;

    int stack_cap = 64;
    StackEntry* stack = (StackEntry*)mem_alloc(
        stack_cap * sizeof(StackEntry));
    int stack_top = 0;

    /* Start with root's children */
    for (int i = 0; i < tree->root->child_count; i++) {
        /* Push in reverse order so first child is processed first */
        int idx = tree->root->child_count - 1 - i;
        TreeNode* child = tree->root->children[idx];
        if (stack_top >= stack_cap) {
            stack_cap *= 2;
            stack = (StackEntry*)mem_realloc(stack,
                stack_cap * sizeof(StackEntry));
        }
        stack[stack_top].node = child;
        stack[stack_top].path = (int*)mem_alloc(sizeof(int));
        stack[stack_top].path[0] = idx + 1;  /* 0-based → 1-based */
        stack[stack_top].path_len = 1;
        stack_top++;
    }

    while (stack_top > 0) {
        /* Pop */
        stack_top--;
        TreeNode* node = stack[stack_top].node;
        int* path      = stack[stack_top].path;
        int  path_len  = stack[stack_top].path_len;

        /* Generate the numbered prefix: e.g., "1.2.1. " */
        for (int i = 0; i < path_len; i++) {
            APPEND("%d.", path[i]);
        }
        APPEND(" ");

        /* Output content */
        if (node->content != NULL) {
            APPEND("%s", node->content);
        }
        /* 注意：枝信息节点（有内容+子节点）：内容已在上方输出。
         * 子节点将在后续出栈时输出，它们的路径会包含本级编号。
         * 例如：
         *   2.3. 这是枝信息              ← 本节点（有内容）
         *   2.3.1. 这是子节点内容         ← 子节点
         * 这在 TXT 格式中是正确的表示。                         */
        APPEND("\n");

        /* Push children in reverse order (DFS pre-order) */
        for (int i = node->child_count - 1; i >= 0; i--) {
            TreeNode* child = node->children[i];
            if (stack_top >= stack_cap) {
                stack_cap *= 2;
                stack = (StackEntry*)mem_realloc(stack,
                    stack_cap * sizeof(StackEntry));
            }
            stack[stack_top].node = child;
            /* Build child path: parent_path + (i+1) */
            int* child_path = (int*)mem_alloc(
                (path_len + 1) * sizeof(int));
            for (int j = 0; j < path_len; j++) {
                child_path[j] = path[j];
            }
            child_path[path_len] = i + 1;  /* 0-based → 1-based */
            stack[stack_top].path = child_path;
            stack[stack_top].path_len = path_len + 1;
            stack_top++;
        }

        /* Free the path array of the processed node */
        free(path);
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

/* Detect TXT files by extension (.txt) */
bool txt_detect(const char* filepath) {
    if (filepath == NULL) return false;
    const char* dot = strrchr(filepath, '.');
    if (dot == NULL) return false;
    /* Case-insensitive comparison with ".txt" */
    const char* ext = dot, *exp = ".txt";
    while (*ext && *exp) {
        char e = *ext, x = *exp;
        if (e >= 'A' && e <= 'Z') e += ('a' - 'A');
        if (x >= 'A' && x <= 'Z') x += ('a' - 'A');
        if (e != x) return false;
        ext++; exp++;
    }
    return (*ext == '\0' && *exp == '\0');
}

/* Create default TXT options */
void* txt_create_default_options(void) {
    TxtOptions* opts = (TxtOptions*)mem_alloc(sizeof(TxtOptions));
    opts->reindex_mode = TXT_REINDEX_ASK;  /* 默认：询问用户 */
    return opts;
}

/* Free TXT options */
void txt_free_options(void* options) {
    if (options != NULL) free(options);
}

/* TXT handler singleton */
const FormatHandler TXT_HANDLER = {
    .format_name            = "txt",
    .extension              = ".txt",
    .description            = "TXT (Numbered Outline)",
    .parse                  = txt_parse,
    .serialize              = txt_serialize,
    .detect                 = txt_detect,
    .create_default_options = txt_create_default_options,
    .free_options           = txt_free_options
};
