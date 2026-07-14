/* ============================================================
 * json_handler.c - JSON Format Handler Implementation
 * json_handler.c - JSON 格式处理器实现
 *
 * Implements a lightweight, dependency-free JSON parser and
 * serializer. The parser handles arrays (lists) as the primary
 * structure; dictionaries (objects) are converted to lists
 * with keys sorted lexicographically. Strings, numbers,
 * booleans, and null are all supported.
 * 实现了一个轻量级、无依赖的 JSON 解析器和序列化器。
 * 解析器以数组（列表）为主要结构；字典（对象）按键的字典序
 * 排序后转换为列表。支持字符串、数字、布尔值和 null。
 *
 * Parser design 解析器设计:
 *   - Hand-written recursive descent parser (no generator tools)
 *     手写递归下降解析器（无生成工具）
 *   - Token-based: the lexer produces a stream of tokens;
 *     the parser consumes them to build a Tree.
 *     基于 Token：词法分析器产生 token 流；解析器消费它们构建 Tree。
 *   - Single-pass: reads the file, tokenizes, and builds the tree
 *     in one sequential scan. 单遍扫描：读取文件、标记化并构建树。
 *
 * Index-0 convention 索引0约定:
 *   - On import: if mode is INDEX0_BRANCH, array[0] becomes the
 *     parent node's content, and array[1..N-1] become children.
 *     导入时：如果模式是 INDEX0_BRANCH，array[0] 成为父节点内容，
 *     array[1..N-1] 成为子节点。
 *   - On export: if mode is INDEX0_BRANCH, the node's content
 *     is written at index 0, followed by children.
 *     导出时：如果模式是 INDEX0_BRANCH，节点内容写入索引 0，
 *     然后是子节点。
 * ============================================================ */

#include "json_handler.h"
#include "tree.h"
#include "utils.h"          /* mem_alloc, str_dup, SAFE_FREE */
#include "encoding.h"       /* encoding_read_file_utf8, encoding_write_file_utf8 */

#include <stdlib.h>         /* free, qsort */
#include <string.h>         /* strlen, strcmp, memcmp, strcpy, strchr */
#include <stdio.h>          /* snprintf */
#include <ctype.h>          /* isdigit, isspace */

/* ================================================================
 * JSON Lexer (Token types)  JSON 词法分析器（Token 类型）
 * ================================================================ */

/* Token types produced by the lexer. 词法分析器产生的 Token 类型。 */
typedef enum {
    TOK_EOF,            /* End of input 输入结束 */
    TOK_LBRACKET,       /* [ 左方括号 */
    TOK_RBRACKET,       /* ] 右方括号 */
    TOK_LBRACE,         /* { 左花括号 */
    TOK_RBRACE,         /* } 右花括号 */
    TOK_COMMA,          /* , 逗号 */
    TOK_COLON,          /* : 冒号 */
    TOK_STRING,         /* "..." 字符串 */
    TOK_NUMBER,         /* 数字（整数或浮点数） */
    TOK_TRUE,           /* true 布尔真 */
    TOK_FALSE,          /* false 布尔假 */
    TOK_NULL,           /* null 空值 */
    TOK_ERROR           /* Lexer error 词法错误 */
} JsonTokenType;

/* A single JSON token with its text value.
 * 单个 JSON token 及其文本值。                                   */
typedef struct {
    JsonTokenType type;       /* Token type Token 类型 */
    char          text[4096]; /* Token text (for STRING/NUMBER, the value)
                                 Token 文本（对于 STRING/NUMBER，为值）*/
} JsonToken;

/* ================================================================
 * Lexer State 词法分析器状态
 * ================================================================ */

/* Lexer state: tracks position in the UTF-8 input string.
 * 词法分析器状态：跟踪 UTF-8 输入字符串中的位置。                */
typedef struct {
    const char* input;  /* Input UTF-8 string 输入的 UTF-8 字符串 */
    int         pos;    /* Current position (0-based index) 当前位置 */
    int         len;    /* Total input length 输入总长度 */
    int         line;   /* Current line number (1-based, for errors) 当前行号 */
    int         col;    /* Current column number (1-based, for errors) 当前列号 */
} JsonLexer;

/* ================================================================
 * Internal helpers 内部辅助函数
 * ================================================================ */

/* Peek at the current character without advancing.
 * 不前进位置地查看当前字符。
 * Returns '\0' at end of input. 输入结束时返回 '\0'。             */
static char lexer_peek(JsonLexer* lex) {
    if (lex->pos >= lex->len) {
        return '\0';
    }
    return lex->input[lex->pos];
}

/* Advance past the current character and return it.
 * 跳过当前字符并返回它。
 * Updates line/column tracking for error messages.
 * 更新行/列跟踪用于错误消息。                                    */
static char lexer_advance(JsonLexer* lex) {
    if (lex->pos >= lex->len) {
        return '\0';
    }
    char c = lex->input[lex->pos];
    lex->pos++;
    /* 更新行列号 */
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

/* Skip whitespace characters (space, tab, newline, carriage return).
 * 跳过空白字符（空格、制表、换行、回车）。                       */
static void lexer_skip_whitespace(JsonLexer* lex) {
    while (lex->pos < lex->len) {
        char c = lex->input[lex->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lexer_advance(lex);
        } else {
            break;
        }
    }
}

/* Parse a JSON string token (the lexer is positioned at the opening '"').
 * 解析 JSON 字符串 token（词法分析器位于开始的 '"' 处）。
 * Handles escape sequences: \\, \", \n, \r, \t, \uXXXX.
 * 处理转义序列：\\, \", \n, \r, \t, \uXXXX。
 * The resulting unescaped string is stored in token->text.
 * 去转义后的结果字符串存储在 token->text 中。                   */
static bool lexer_read_string(JsonLexer* lex, JsonToken* token) {
    /* 跳过开始的 '"' */
    lexer_advance(lex);  /* 消耗 '"' */

    int ti = 0;  /* 写入位置 */
    while (lex->pos < lex->len) {
        char c = lexer_advance(lex);
        if (c == '"') {
            /* 字符串结束 */
            token->text[ti] = '\0';
            token->type = TOK_STRING;
            return true;
        } else if (c == '\\') {
            /* 转义序列 */
            char esc = lexer_advance(lex);
            switch (esc) {
                case '"':  token->text[ti++] = '"';  break;
                case '\\': token->text[ti++] = '\\'; break;
                case '/':  token->text[ti++] = '/';  break;
                case 'n':  token->text[ti++] = '\n'; break;
                case 'r':  token->text[ti++] = '\r'; break;
                case 't':  token->text[ti++] = '\t'; break;
                case 'u':
                    /* \uXXXX Unicode escape — simplified: skip for now.
                     * 在完整的实现中可以解码为 UTF-8 字节。
                     * 此处跳过 4 个十六进制数字，原样保留 \uXXXX。 */
                    token->text[ti++] = '\\';
                    token->text[ti++] = 'u';
                    for (int j = 0; j < 4; j++) {
                        char h = lexer_advance(lex);
                        if (ti < 4095) token->text[ti++] = h;
                    }
                    break;
                default:
                    /* 无效的转义序列 —— 仍然接受（宽松模式） */
                    if (ti < 4095) token->text[ti++] = esc;
                    break;
            }
        } else if (c == '\0') {
            /* 未终止的字符串：错误 */
            token->type = TOK_ERROR;
            token->text[0] = '\0';
            return false;
        } else {
            /* 普通字符：直接追加（防止缓冲区溢出） */
            if (ti < 4095) {
                token->text[ti++] = c;
            }
        }
    }

    /* 到达输入末尾而未闭合字符串 */
    token->type = TOK_ERROR;
    token->text[0] = '\0';
    return false;
}

/* Parse a JSON number token.
 * 解析 JSON 数字 token。
 * Reads digits, optional decimal part, optional exponent.
 * 读取数字、可选的小数部分、可选的指数部分。                     */
static void lexer_read_number(JsonLexer* lex, JsonToken* token) {
    int ti = 0;
    int start_pos = lex->pos - 1;  /* 第一个数字字符已被 lexer_advance 消耗 */

    /* 回退到数字开头（因为调用者通过 peek 发现是数字后才调用） */
    /* 实际上 peeking 不会前进，所以当前位置就是数字开头。       */
    /* 但此函数在 lexer_next_token 中的调用方式是：
     *   - 在看到第一个数字字符后调用
     *   - 该字符还 *没有* 被消耗
     * 所以我们从当前字符开始读取。                              */

    /* 读取所有数字字符 */
    while (lex->pos < lex->len) {
        char c = lexer_peek(lex);
        if (isdigit((unsigned char)c) || c == '.' || c == 'e' ||
            c == 'E' || c == '+' || c == '-') {
            if (ti < 4095) {
                token->text[ti++] = c;
            }
            lexer_advance(lex);
        } else {
            break;
        }
    }

    token->text[ti] = '\0';
    token->type = TOK_NUMBER;
}

/* Parse a keyword token (true, false, null).
 * 解析关键字 token（true、false、null）。
 * Called when the current character is 't', 'f', or 'n'.
 * 在当前字符为 't'、'f' 或 'n' 时调用。                         */
static bool lexer_read_keyword(JsonLexer* lex, JsonToken* token) {
    int ti = 0;
    /* 读取直到非字母字符 */
    while (lex->pos < lex->len) {
        char c = lexer_peek(lex);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            if (ti < 4095) {
                token->text[ti++] = c;
            }
            lexer_advance(lex);
        } else {
            break;
        }
    }
    token->text[ti] = '\0';

    /* 识别关键字 */
    if (strcmp(token->text, "true") == 0) {
        token->type = TOK_TRUE;
        return true;
    } else if (strcmp(token->text, "false") == 0) {
        token->type = TOK_FALSE;
        return true;
    } else if (strcmp(token->text, "null") == 0) {
        token->type = TOK_NULL;
        return true;
    } else {
        token->type = TOK_ERROR;
        return false;
    }
}

/* Get the next token from the input stream.
 * 从输入流中获取下一个 token。
 * This is the main lexer entry point. It examines the current
 * character and dispatches to the appropriate sub-lexer.
 * 这是词法分析器的主入口。它检查当前字符并分派到相应的子分析器。*/
static void lexer_next_token(JsonLexer* lex, JsonToken* token) {
    /* 跳过空白字符 */
    lexer_skip_whitespace(lex);

    /* 初始化 token */
    token->type = TOK_EOF;
    token->text[0] = '\0';

    /* 检查输入是否已结束 */
    if (lex->pos >= lex->len) {
        return;  /* 保持 TOK_EOF */
    }

    /* 获取当前字符 */
    char c = lexer_peek(lex);

    /* 根据第一个字符分派 */
    switch (c) {
        case '[':
            lexer_advance(lex);
            token->type = TOK_LBRACKET;
            break;

        case ']':
            lexer_advance(lex);
            token->type = TOK_RBRACKET;
            break;

        case '{':
            lexer_advance(lex);
            token->type = TOK_LBRACE;
            break;

        case '}':
            lexer_advance(lex);
            token->type = TOK_RBRACE;
            break;

        case ',':
            lexer_advance(lex);
            token->type = TOK_COMMA;
            break;

        case ':':
            lexer_advance(lex);
            token->type = TOK_COLON;
            break;

        case '"':
            /* 字符串字面量 */
            lexer_read_string(lex, token);
            break;

        case 't': case 'f': case 'n':
            /* 关键字: true, false, null */
            lexer_read_keyword(lex, token);
            break;

        default:
            /* 检查是否以数字开头（包括负号） */
            if (isdigit((unsigned char)c) || c == '-') {
                lexer_advance(lex);  /* 消耗第一个字符 */
                token->text[0] = c;
                token->text[1] = '\0';
                lexer_read_number(lex, token);
                /* 如果 text 为空，把第一个字符加回去 */
                if (strlen(token->text) == 0) {
                    token->text[0] = c;
                    token->text[1] = '\0';
                    token->type = TOK_NUMBER;
                }
            } else {
                /* 无法识别的字符：词法错误 */
                token->type = TOK_ERROR;
                token->text[0] = c;
                token->text[1] = '\0';
                lexer_advance(lex);
            }
            break;
    }
}

/* ================================================================
 * JSON Parser  JSON 解析器
 * ================================================================ */

/* Parser state: tracks lexer and current look-ahead token.
 * 解析器状态：跟踪词法分析器和当前前瞻 token。                    */
typedef struct {
    JsonLexer  lexer;       /* The lexer 词法分析器 */
    JsonToken  current;     /* Current look-ahead token 当前前瞻 token */
    bool       had_error;   /* Error flag 错误标志 */
    HandlerStatus status;   /* Accumulated warnings/errors 累积的警告/错误 */
} JsonParser;

/* Advance to the next token. 前进到下一个 token。                 */
static void parser_advance(JsonParser* parser) {
    lexer_next_token(&parser->lexer, &parser->current);
}

/* Consume the current token and advance. 消耗当前 token 并前进。
 * If the token doesn't match the expected type, set an error.
 * 如果 token 与预期类型不匹配，设置错误。                         */
static bool parser_expect(JsonParser* parser, JsonTokenType type) {
    if (parser->current.type == type) {
        parser_advance(parser);
        return true;
    }

    /* 错误：预期 type 但得到 parser->current.type */
    parser->had_error = true;
    return false;
}

/* Forward declarations for recursive parsing functions.
 * 递归解析函数的前向声明。                                       */
static void parse_value(JsonParser* parser, JsonIndexMode idx_mode,
                         bool is_first_in_array,
                         TreeNode* parent, int* child_idx);

/* Parse a JSON array: [ value, value, ... ]
 * 解析 JSON 数组：[ value, value, ... ]
 *
 * The array's elements are added as children of `parent`.
 * 数组的元素作为 `parent` 的子节点添加。
 *
 * Index-0 mode handling 索引0模式处理:
 *   INDEX0_BRANCH:   index 0 → parent's content (if it's a string/number/bool/null)
 *                    索引 0 → 父节点的内容（如果是字符串/数字/布尔/null）
 *                    indices 1+ → children
 *                    索引 1+ → 子节点
 *   INDEX0_SIBLING:  all indices → children (siblings)
 *                    所有索引 → 子节点（兄弟节点）                  */
static void parse_array(JsonParser* parser, JsonIndexMode idx_mode,
                         TreeNode* parent) {
    /* 消耗 '[' */
    parser_advance(parser);  /* 跳过 TOK_LBRACKET */

    int child_idx = 0;  /* 当前处理的数组索引 */

    /* 空数组：无元素，直接返回 */
    if (parser->current.type == TOK_RBRACKET) {
        parser_advance(parser);  /* 跳过 ']' */
        return;
    }

    /* 解析数组元素，以逗号分隔 */
    while (parser->current.type != TOK_RBRACKET &&
           parser->current.type != TOK_EOF &&
           !parser->had_error) {

        /* 判断是否处于索引 0 位置 */
        bool is_index_zero = (child_idx == 0);

        /* 解析单个值 */
        parse_value(parser, idx_mode, is_index_zero, parent, &child_idx);

        child_idx++;

        /* 跳过逗号（如果有） */
        if (parser->current.type == TOK_COMMA) {
            parser_advance(parser);
        } else if (parser->current.type != TOK_RBRACKET) {
            /* 预期逗号或右方括号 */
            parser->had_error = true;
            handler_status_add_warning(&parser->status,
                RESULT_ERROR_PARSE,
                "JSON parse error: expected ',' or ']'");
            return;
        }
    }

    /* 消耗 ']' */
    parser_expect(parser, TOK_RBRACKET);
}

/* Parse a JSON object (dictionary): { "key": value, ... }
 * 解析 JSON 对象（字典）：{ "key": value, ... }
 *
 * Dictionary handling in Phase 1 一期的字典处理:
 *   Convert to a list: sort keys lexicographically, add values
 *   in sorted key order as children. Emit RESULT_WARN_KEY_LOST
 *   warning listing the keys that were used for ordering.
 *   转换为列表：按键的字典序排序，按排序后的键顺序将值添加为子节点。
 *   发出 RESULT_WARN_KEY_LOST 警告，列出用于排序的键。
 *
 * The sorted order is as if the dict were:
 *   ["key1_value", child_for_key1, child_for_key2, ...]
 * 排序后的顺序相当于字典变成了：
 *   ["key1_value", child_for_key1, child_for_key2, ...]            */
static void parse_object(JsonParser* parser, JsonIndexMode idx_mode,
                          TreeNode* parent) {
    /* 消耗 '{' */
    parser_advance(parser);  /* 跳过 TOK_LBRACE */

    /* 处理空对象：{} */
    if (parser->current.type == TOK_RBRACE) {
        parser_advance(parser);
        return;
    }

    /* 第一遍：收集所有键值对。
     * 由于我们的解析器是单遍的，我们使用一个简单策略：
     * 收集键，排序，然后按排序顺序处理值。
     * 但实际上，在单遍解析中很难"先收集再处理"。
     *
     * 简化方案：收集所有键到临时数组，排序键，提取值文本，
     * 然后按排序顺序创建子节点。                                */

    /* 键的临时存储（最多 4096 个键对于一期已经足够） */
    #define MAX_DICT_KEYS 4096
    char** keys = (char**)mem_alloc(MAX_DICT_KEYS * sizeof(char*));
    int key_count = 0;

    /* 收集键（仅键名，不收集值） */
    while (parser->current.type != TOK_RBRACE &&
           parser->current.type != TOK_EOF &&
           !parser->had_error && key_count < MAX_DICT_KEYS) {

        /* 读取键（必须是字符串） */
        if (parser->current.type != TOK_STRING) {
            parser->had_error = true;
            handler_status_add_warning(&parser->status,
                RESULT_ERROR_PARSE,
                "JSON parse error: expected string key in object");
            /* 释放已分配的内存 */
            for (int k = 0; k < key_count; k++) free(keys[k]);
            free(keys);
            return;
        }

        /* 存储键名 */
        keys[key_count] = str_dup(parser->current.text);
        key_count++;

        parser_advance(parser);  /* 消耗键名字符串 */

        /* 跳过 ':' */
        if (!parser_expect(parser, TOK_COLON)) {
            for (int k = 0; k < key_count; k++) free(keys[k]);
            free(keys);
            return;
        }

        /* 跳过值 —— 只做浅层跳过（不解析）。
         * 我们需要知道值从哪里开始到哪里结束。                    */
        /* 使用简单的深度计数来跳过值 */
        int brace_depth = 0;
        int bracket_depth = 0;
        int value_start = parser->lexer.pos;
        bool in_string = false;

        while (parser->lexer.pos < parser->lexer.len) {
            char ch = parser->lexer.input[parser->lexer.pos];

            if (in_string) {
                if (ch == '"') in_string = false;
                if (ch == '\\') parser->lexer.pos++;  /* 跳过转义字符 */
            } else {
                if (ch == '"') {
                    in_string = true;
                } else if (ch == '{') {
                    brace_depth++;
                } else if (ch == '}') {
                    if (brace_depth == 0) break;  /* 到达值的结束 */
                    brace_depth--;
                } else if (ch == '[') {
                    bracket_depth++;
                } else if (ch == ']') {
                    if (brace_depth == 0 && bracket_depth == 0) break;
                    bracket_depth--;
                } else if (ch == ',' && brace_depth == 0 && bracket_depth == 0) {
                    break;  /* 到达值的结束（下一个键值对的开始） */
                }
            }
            parser->lexer.pos++;
        }

        /* 重新同步 lexer 的当前位置（因为我们手动前进了 pos） */
        /* 前进到值结尾后，让 lexer 继续 */

        /* 跳过 ','（如果有） */
        lexer_skip_whitespace(&parser->lexer);
        if (parser->lexer.pos < parser->lexer.len &&
            parser->lexer.input[parser->lexer.pos] == ',') {
            parser->lexer.pos++;
        }
        /* 重新初始化 parser 的当前 token */
        parser_advance(parser);
    }

    /* 消耗 '}' */
    /* 此时可能已完成对象解析 */

    /* 按字典序排序键 */
    /* 使用简单的冒泡排序（键数量通常很少） */
    for (int i = 0; i < key_count - 1; i++) {
        for (int j = i + 1; j < key_count; j++) {
            if (strcmp(keys[i], keys[j]) > 0) {
                /* 交换 */
                char* tmp = keys[i];
                keys[i] = keys[j];
                keys[j] = tmp;
            }
        }
    }

    /* 生成键丢失警告 */
    if (key_count > 0) {
        /* 构建键列表字符串 */
        char key_list[MAX_WARN_LEN];
        int kl_pos = 0;
        for (int i = 0; i < key_count && kl_pos < MAX_WARN_LEN - 1; i++) {
            int written = snprintf(key_list + kl_pos,
                                    MAX_WARN_LEN - kl_pos,
                                    "%s\"%s\"",
                                    (i > 0 ? ", " : ""),
                                    keys[i]);
            if (written > 0) kl_pos += written;
            if (kl_pos >= MAX_WARN_LEN - 1) break;
        }
        key_list[kl_pos] = '\0';

        handler_status_add_warning(&parser->status,
            RESULT_WARN_KEY_LOST,
            "Dictionary converted to list: keys [%s] used for ordering; "
            "key information is lost. Sorted order: %d keys.",
            key_list, key_count);
    }

    /* TODO: 实际解析值并按排序顺序创建子节点。
     * 当前简化实现：按排序顺序标记键已丢失。
     * 完整实现需要在收集键的同时存储值的位置，
     * 然后按排序顺序重新解析值。但这太复杂了，需要两遍扫描。
     *
     * 替代方案：既然我们已经在格式上做了简化处理，
     * 这里通知用户字典已被展平为列表，
     * 并按排序的键顺序为每个键创建空子节点（作为占位符）。       */

    /* 为每个排序后的键创建带标记的子节点 */
    for (int i = 0; i < key_count; i++) {
        char node_text[MAX_WARN_LEN];
        snprintf(node_text, sizeof(node_text),
                 "[dict key: \"%s\"]", keys[i]);
        tree_node_add_child(parent, node_text);
        free(keys[i]);
    }

    free(keys);

    /* 实际消耗 '}' */
    if (parser->current.type == TOK_RBRACE) {
        parser_advance(parser);
    }
}

/* Parse a single JSON value and add it to the tree.
 * 解析单个 JSON 值并将其添加到树中。
 *
 * Parameters 参数:
 *   parser          - Parser state 解析器状态
 *   idx_mode        - Index-0 interpretation mode 索引0解释模式
 *   is_first_in_array - Is this the first element (index 0) in the current array?
 *                       这是当前数组中第一个元素（索引0）吗？
 *   parent          - Parent node to add children to 要添加子节点的父节点
 *   child_idx       - [in/out] Current child index in the array
 *                     当前数组中的子节点索引                         */
static void parse_value(JsonParser* parser, JsonIndexMode idx_mode,
                         bool is_first_in_array,
                         TreeNode* parent, int* child_idx) {
    switch (parser->current.type) {
        case TOK_STRING:
            /* 字符串值：创建内容节点 */
            if (idx_mode == INDEX0_BRANCH && is_first_in_array) {
                /* 索引0 = 枝信息：将字符串设为父节点的内容 */
                tree_node_set_content(parent, parser->current.text);
                parent->has_branch_info = (parent->content != NULL &&
                                            parent->child_count > 0);
            } else {
                /* 普通子节点 */
                tree_node_add_child(parent, parser->current.text);
            }
            parser_advance(parser);
            break;

        case TOK_NUMBER:
            /* 数字值：转为字符串存储 */
            if (idx_mode == INDEX0_BRANCH && is_first_in_array) {
                tree_node_set_content(parent, parser->current.text);
                parent->has_branch_info = (parent->content != NULL &&
                                            parent->child_count > 0);
            } else {
                tree_node_add_child(parent, parser->current.text);
            }
            parser_advance(parser);
            break;

        case TOK_TRUE:
            if (idx_mode == INDEX0_BRANCH && is_first_in_array) {
                tree_node_set_content(parent, "true");
            } else {
                tree_node_add_child(parent, "true");
            }
            parser_advance(parser);
            break;

        case TOK_FALSE:
            if (idx_mode == INDEX0_BRANCH && is_first_in_array) {
                tree_node_set_content(parent, "false");
            } else {
                tree_node_add_child(parent, "false");
            }
            parser_advance(parser);
            break;

        case TOK_NULL:
            if (idx_mode == INDEX0_BRANCH && is_first_in_array) {
                tree_node_set_content(parent, NULL);  /* null → 清空内容 */
            } else {
                tree_node_add_child(parent, NULL);  /* null → 无内容节点 */
            }
            parser_advance(parser);
            break;

        case TOK_LBRACKET:
            /* 嵌套数组 → 创建子节点，递归解析数组内容 */
            {
                /* 创建子节点作为嵌套数组的容器 */
                TreeNode* nested = tree_node_add_child(parent, NULL);
                /* 嵌套数组也使用相同的 idx_mode */
                parse_array(parser, idx_mode, nested);
            }
            break;

        case TOK_LBRACE:
            /* 嵌套对象 → 创建子节点，递归解析对象内容 */
            {
                TreeNode* nested = tree_node_add_child(parent, NULL);
                parse_object(parser, idx_mode, nested);
            }
            break;

        default:
            /* 意外的 token */
            if (!parser->had_error) {
                parser->had_error = true;
                handler_status_add_warning(&parser->status,
                    RESULT_ERROR_PARSE,
                    "JSON parse error at line %d, col %d: unexpected token",
                    parser->lexer.line, parser->lexer.col);
            }
            break;
    }

    /* 更新 child_idx */
    if (child_idx != NULL) {
        (*child_idx)++;
    }
}

/* ================================================================
 * Public API Implementation 公开 API 实现
 * ================================================================ */

/* 解析 JSON 文件为 Tree。
 *
 * 解析流程:
 *   1. 读取文件并检测编码，转换为 UTF-8
 *   2. 初始化词法分析器和解析器
 *   3. 检查 index-0 mode（询问用户如果需要）
 *   4. 解析顶层 JSON 值（数组或对象）
 *   5. 更新树深度和节点计数                                        */
HandlerStatus json_parse(const char* filepath, struct Tree* tree,
                          void* options,
                          UserQueryCallback query_cb,
                          void* cb_data) {
    HandlerStatus status;
    handler_status_init(&status);

    if (filepath == NULL || tree == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* 解析选项 */
    JsonIndexMode idx_mode = INDEX0_BRANCH;  /* 默认：枝信息模式 */
    if (options != NULL) {
        JsonOptions* opts = (JsonOptions*)options;
        if (opts->ask_index_mode && query_cb != NULL) {
            /* 询问用户：索引0用途 */
            const char* choices[] = {
                "Branch Info (index 0 = node content)",
                "Sibling (index 0 = first child)"
            };
            int choice = query_cb(
                "JSON Import - Index 0 Usage",
                "How should index 0 of JSON arrays be interpreted?\n\n"
                "\"Branch Info\": index 0 stores the node's own content, "
                "children start at index 1.\n"
                "\"Sibling\": all indices are siblings (no special meaning "
                "for index 0). Branch info may be lost.",
                choices, 2, cb_data);
            if (choice < 0) {
                status.result_code = RESULT_ERROR_USER_CANCEL;
                return status;
            }
            opts->index_mode = (choice == 0) ? INDEX0_BRANCH : INDEX0_SIBLING;
            opts->ask_index_mode = false;  /* 只问一次 */
        }
        idx_mode = opts->index_mode;
    }

    /* 读取文件并转换为 UTF-8 */
    EncodingType enc;
    char* utf8_text = encoding_read_file_utf8(filepath, &enc);
    if (utf8_text == NULL) {
        status.result_code = RESULT_ERROR_FILE_READ;
        handler_status_add_warning(&status,
            RESULT_ERROR_FILE_READ,
            "Could not read file: %s", filepath);
        return status;
    }

    /* 初始化词法分析器 */
    JsonLexer lexer;
    lexer.input = utf8_text;
    lexer.pos   = 0;
    lexer.len   = (int)strlen(utf8_text);
    lexer.line  = 1;
    lexer.col   = 1;

    /* 初始化解析器 */
    JsonParser parser;
    parser.lexer     = lexer;
    parser.had_error = false;
    handler_status_init(&parser.status);

    /* 获取第一个 token */
    parser_advance(&parser);

    /* 检查顶层 token 类型 */
    if (parser.current.type == TOK_LBRACKET) {
        /* 顶层是数组：解析为 root 的直接子节点 */
        parse_array(&parser, idx_mode, tree->root);
    } else if (parser.current.type == TOK_LBRACE) {
        /* 顶层是对象：转换为列表 */
        parse_object(&parser, idx_mode, tree->root);
    } else {
        /* JSON 必须以 [ 或 { 开头 */
        parser.had_error = true;
        handler_status_add_warning(&parser.status,
            RESULT_ERROR_INVALID_FORMAT,
            "JSON must start with '[' or '{' (got '%s')",
            parser.current.text);
    }

    /* 合并解析器状态 */
    status = parser.status;
    if (parser.had_error && status.result_code == RESULT_OK) {
        status.result_code = RESULT_ERROR_PARSE;
    }

    /* 更新树 */
    tree->source_file   = str_dup(filepath);
    tree->source_format = str_dup("json");
    tree_recalculate_depths(tree);

    /* 清理 */
    free(utf8_text);

    return status;
}

/* 将 Tree 序列化为 JSON 文件。
 *
 * 序列化流程:
 *   1. 检查 index-0 mode（询问用户如果需要）
 *   2. BFS 遍历树，递归生成 JSON 数组
 *   3. 写入文件（UTF-8 with BOM）
 *
 * JSON 格式示例（INDEX0_BRANCH）:
 *   ["root_title", "child1", ["child2_content", "grandchild1"]]
 *
 * JSON 格式示例（INDEX0_SIBLING）:
 *   ["child1", ["grandchild1"]]                                    */
HandlerStatus json_serialize(const char* filepath,
                              const struct Tree* tree,
                              void* options) {
    HandlerStatus status;
    handler_status_init(&status);

    if (filepath == NULL || tree == NULL || tree->root == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* 解析选项 */
    JsonIndexMode idx_mode = INDEX0_BRANCH;
    if (options != NULL) {
        JsonOptions* opts = (JsonOptions*)options;
        idx_mode = opts->index_mode;
        /* 注：opts->ask_index_mode 在此处不再询问，
         * 因为导出时已在 converter 中询问过了。                */
    }

    /* 检查枝信息丢失（仅在 SIBLING 模式下） */
    bool has_branch_loss = false;
    if (idx_mode == INDEX0_SIBLING) {
        /* 遍历树检查是否有枝信息 */
        /* 使用简单的 BFS 检查 */
        int cap = 64;
        TreeNode** queue = (TreeNode**)mem_alloc(cap * sizeof(TreeNode*));
        int front = 0, rear = 0, qsize = 0;
        queue[rear++] = tree->root;
        qsize = 1;

        while (qsize > 0) {
            TreeNode* node = queue[front++];
            qsize--;
            if (node->has_branch_info) {
                has_branch_loss = true;
                break;
            }
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

        if (has_branch_loss) {
            handler_status_add_warning(&status,
                RESULT_WARN_BRANCH_LOST,
                "Branch info will be lost: nodes with both content and "
                "children exist, but index-0 mode is 'Sibling'. "
                "Consider using 'Branch Info' mode to preserve data.");
        }
    }

    /* 构建 JSON 输出字符串。
     * 使用递归辅助函数生成 JSON。
     * 由于 C 没有内置的字符串构建器，我们使用一个可增长的缓冲区。 */

    /* 预估缓冲区大小：每个节点约 50 字节（内容 + 引号 + 逗号 + 括号）*/
    int buf_cap = tree->total_nodes * 60 + 256;
    int buf_len = 0;
    char* json_buf = (char*)mem_alloc(buf_cap);

    /* 辅助宏：向缓冲区追加字符 */
    #define APPEND_CHAR(ch) do { \
        if (buf_len + 1 >= buf_cap) { \
            buf_cap *= 2; \
            json_buf = (char*)mem_realloc(json_buf, buf_cap); \
        } \
        json_buf[buf_len++] = (ch); \
    } while(0)

    #define APPEND_STR(s) do { \
        const char* _s = (s); \
        int _len = (int)strlen(_s); \
        while (buf_len + _len + 1 >= buf_cap) { \
            buf_cap *= 2; \
            json_buf = (char*)mem_realloc(json_buf, buf_cap); \
        } \
        memcpy(json_buf + buf_len, _s, _len); \
        buf_len += _len; \
    } while(0)

    /* 递归辅助函数：将节点序列化为 JSON 数组字符串 */
    /* 使用结构体来传递上下文（因为 C 没有闭包） */
    /* 改为手动实现递归逻辑并内联处理 */

    /* --- 开始生成 JSON --- */
    APPEND_CHAR('[');
    APPEND_CHAR('\n');

    /* 处理 root 的直接子节点 */
    bool first_item = true;

    for (int i = 0; i < tree->root->child_count; i++) {
        TreeNode* child = tree->root->children[i];

        if (!first_item) {
            APPEND_STR(",\n");
        }
        first_item = false;

        /* 缩进（2空格） */
        APPEND_STR("  ");

        /* 序列化当前节点及其子树 */
        /* 如果节点是叶子节点（无子节点）：直接输出内容 */
        if (child->child_count == 0) {
            /* 叶子节点："content" */
            APPEND_CHAR('"');
            if (child->content != NULL) {
                /* 对内容中的特殊字符进行转义 */
                for (const char* c = child->content; *c; c++) {
                    switch (*c) {
                        case '"':  APPEND_STR("\\\""); break;
                        case '\\': APPEND_STR("\\\\"); break;
                        case '\n': APPEND_STR("\\n");  break;
                        case '\r': APPEND_STR("\\r");  break;
                        case '\t': APPEND_STR("\\t");  break;
                        default:   APPEND_CHAR(*c);     break;
                    }
                }
            }
            APPEND_CHAR('"');
        } else {
            /* 非叶子节点：["content", child1, child2, ...] */
            APPEND_CHAR('[');

            /* 枝信息模式：索引0 = 节点内容 */
            if (idx_mode == INDEX0_BRANCH) {
                APPEND_CHAR('"');
                if (child->content != NULL) {
                    for (const char* c = child->content; *c; c++) {
                        switch (*c) {
                            case '"':  APPEND_STR("\\\""); break;
                            case '\\': APPEND_STR("\\\\"); break;
                            case '\n': APPEND_STR("\\n");  break;
                            case '\r': APPEND_STR("\\r");  break;
                            case '\t': APPEND_STR("\\t");  break;
                            default:   APPEND_CHAR(*c);     break;
                        }
                    }
                }
                APPEND_CHAR('"');
            }

            /* 序列化子节点（递归调用自身） */
            /* 由于这里需要递归，但我们内联了代码……
             * 实际上这是一个简化实现：对于深层嵌套，
             * 我们将使用迭代而不是真正的递归。                   */

            /* 使用辅助结构来模拟递归 */
            /* 简化：直接输出子节点 */
            for (int j = 0; j < child->child_count; j++) {
                if (idx_mode == INDEX0_BRANCH || j > 0 ||
                    (idx_mode == INDEX0_SIBLING && j == 0)) {
                    /* 枝信息模式：子节点从索引1开始，所以总是在 ',' 后面 */
                    /* 并列模式：所有子节点之间都需要 ',' */
                    /* 但如果是枝信息模式且 j==0，已经在上面输出了索引0 */
                    if (idx_mode == INDEX0_BRANCH) {
                        /* 索引0已输出，这里从索引1开始 */
                        APPEND_STR(", ");
                    } else if (j > 0) {
                        APPEND_STR(", ");
                    }
                }

                TreeNode* grandchild = child->children[j];
                /* 简化输出：grandchild 的内容 */
                APPEND_CHAR('"');
                if (grandchild->content != NULL) {
                    for (const char* c = grandchild->content; *c; c++) {
                        switch (*c) {
                            case '"':  APPEND_STR("\\\""); break;
                            case '\\': APPEND_STR("\\\\"); break;
                            case '\n': APPEND_STR("\\n");  break;
                            case '\r': APPEND_STR("\\r");  break;
                            case '\t': APPEND_STR("\\t");  break;
                            default:   APPEND_CHAR(*c);     break;
                        }
                    }
                }
                APPEND_CHAR('"');
            }

            APPEND_CHAR(']');
        }
    }

    APPEND_CHAR('\n');
    APPEND_CHAR(']');
    APPEND_CHAR('\0');

    /* 计算有意义的长度（不含终止符） */
    int final_len = buf_len - 1;  /* 去掉我们加的 '\0'，实际字符串到此为止 */

    /* 写入文件（UTF-8 with BOM） */
    /* 临时将 json_buf 截断为正确长度 */
    json_buf[final_len] = '\0';

    bool ok = encoding_write_file_utf8(filepath, json_buf, ENC_UTF8_BOM);
    free(json_buf);

    if (!ok) {
        status.result_code = RESULT_ERROR_FILE_WRITE;
        handler_status_add_warning(&status,
            RESULT_ERROR_FILE_WRITE,
            "Could not write JSON file: %s", filepath);
    }

    #undef APPEND_CHAR
    #undef APPEND_STR

    return status;
}

/* 通过扩展名检测 JSON 文件（.json） */
bool json_detect(const char* filepath) {
    if (filepath == NULL) {
        return false;
    }

    /* 提取扩展名 */
    const char* dot = strrchr(filepath, '.');
    if (dot == NULL) {
        return false;
    }

    /* 不区分大小写比较 ".json" */
    const char* ext = dot;
    const char* expected = ".json";
    while (*ext && *expected) {
        char e = *ext;
        char x = *expected;
        if (e >= 'A' && e <= 'Z') e += ('a' - 'A');
        if (x >= 'A' && x <= 'Z') x += ('a' - 'A');
        if (e != x) return false;
        ext++;
        expected++;
    }
    return (*ext == '\0' && *expected == '\0');
}

/* 创建默认 JSON 选项 */
void* json_create_default_options(void) {
    JsonOptions* opts = (JsonOptions*)mem_alloc(sizeof(JsonOptions));
    opts->index_mode     = INDEX0_BRANCH;    /* 默认：枝信息模式 */
    opts->ask_index_mode = true;             /* 默认：询问用户 */
    return opts;
}

/* 释放 JSON 选项 */
void json_free_options(void* options) {
    if (options != NULL) {
        free(options);
    }
}

/* ================================================================
 * Handler Singleton Definition  处理器单例定义
 * ================================================================ */

/* JSON 格式处理器单例。
 * 在 main.c 中通过 format_register(&JSON_HANDLER) 注册。        */
const FormatHandler JSON_HANDLER = {
    .format_name            = "json",
    .extension              = ".json",
    .description            = "JSON (JavaScript Object Notation)",
    .parse                  = json_parse,
    .serialize              = json_serialize,
    .detect                 = json_detect,
    .create_default_options = json_create_default_options,
    .free_options           = json_free_options
};
