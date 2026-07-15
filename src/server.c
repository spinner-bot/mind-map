/* ============================================================
 * server.c - HTTP Server Implementation (Phase 2)
 * server.c - HTTP 服务器实现（二期）
 *
 * Lightweight HTTP server using WinSock2. Provides REST-style
 * JSON API for the web frontend and serves static files from
 * the web/ directory. Single-threaded, blocking I/O — suitable
 * for a local single-user desktop tool.
 * 使用 WinSock2 的轻量级 HTTP 服务器。为 Web 前端提供 REST 风格
 * JSON API，并从 web/ 目录提供静态文件。单线程、阻塞 I/O —
 * 适合本地单用户桌面工具。
 *
 * ============================================================ */

#include "server.h"
#include "tree.h"            /* Tree, TreeNode, TreeConfig, tree_* */
#include "format_handler.h"  /* FormatHandler, format_registry_* */
#include "converter.h"       /* convert_file, BatchConfig, BatchResult */
#include "utils.h"           /* mem_alloc, str_dup, path_* */
#include "encoding.h"        /* encoding_read_file_utf8 */

#include <stdio.h>           /* FILE, fopen, fread, fclose, snprintf */
#include <stdlib.h>          /* malloc, free, atoi */
#include <string.h>          /* strlen, strcmp, strncmp, strcpy, strchr,
                                strstr, memcpy */
#include <stdarg.h>          /* va_list, va_start, va_end */
#include <stdint.h>          /* uint32_t */

#ifdef _WIN32
#include <winsock2.h>        /* socket, bind, listen, accept, send, recv */
#include <windows.h>         /* ShellExecuteW (open browser) */
#include <ws2tcpip.h>        /* inet_pton */
#pragma comment(lib, "ws2_32.lib")
#else
/* POSIX fallback (for potential future cross-platform support)
 * POSIX 回退（为未来跨平台支持预留） */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

/* ================================================================
 * Server State  服务器状态
 * ================================================================ */

static SOCKET       g_listen_socket = INVALID_SOCKET;  /* 监听 socket */
static volatile bool g_running    = false;              /* 服务器是否在运行 */
static struct Tree* g_tree       = NULL;               /* 当前操作的树 */
static char         g_base_dir[512];                   /* web/ 目录的绝对路径 */

/* ================================================================
 * HTTP Helpers  HTTP 辅助函数
 * ================================================================ */

/* HTTP response status codes we use. 我们使用的 HTTP 状态码。     */
#define HTTP_200_OK        "200 OK"
#define HTTP_400_BAD       "400 Bad Request"
#define HTTP_404_NOT_FOUND "404 Not Found"
#define HTTP_405_METHOD    "405 Method Not Allowed"
#define HTTP_500_ERROR     "500 Internal Server Error"

/* MIME types for static file serving. 静态文件服务的 MIME 类型。   */
static const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    return "application/octet-stream";
}

/* Send an HTTP response. 发送 HTTP 响应。
 * status:   e.g., "200 OK"
 * mime:     e.g., "application/json; charset=utf-8"
 * body:     response body (can be NULL for empty body)
 * extra_hdrs: additional headers like CORS (can be NULL)
 * cli_sock: client socket to send to                           */
static void send_response(const char* status, const char* mime,
                           const char* body, const char* extra_hdrs,
                           SOCKET cli_sock) {
    char header[4096];
    int body_len = (body != NULL) ? (int)strlen(body) : 0;

    /* 构建 HTTP 响应头 */
    int hdr_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "%s%s"
        "\r\n",
        status, mime, body_len,
        (extra_hdrs != NULL) ? extra_hdrs : "",
        (extra_hdrs != NULL) ? "\r\n" : "");

    /* 发送头部 */
    send(cli_sock, header, hdr_len, 0);

    /* 发送正文（如果有） */
    if (body != NULL && body_len > 0) {
        send(cli_sock, body, body_len, 0);
    }
}

/* Send a JSON response (convenience wrapper). 发送 JSON 响应。    */
static void send_json(const char* status, const char* json, SOCKET cli_sock) {
    send_response(status, "application/json; charset=utf-8", json, NULL, cli_sock);
}

/* Send a simple JSON error response. 发送简单的 JSON 错误响应。   */
static void send_json_error(const char* status, const char* msg, SOCKET cli_sock) {
    char json[512];
    snprintf(json, sizeof(json), "{\"error\":\"%s\"}", msg);
    send_json(status, json, cli_sock);
}

/* ================================================================
 * HTTP Request Parser  HTTP 请求解析器
 * ================================================================ */

/* Parsed HTTP request. 解析后的 HTTP 请求。                       */
typedef struct {
    char method[16];           /* GET, POST, OPTIONS, etc. */
    char path[SERVER_MAX_PATH]; /* URL path (e.g., /api/tree/node/add) */
    char body[65536];           /* Request body (for POST) */
    int  body_len;              /* Length of body */
} HttpRequest;

/* 尝试找到请求正文（在双 \r\n 之后）。辅助函数。                  */
static const char* find_body_start(const char* request, int len) {
    for (int i = 0; i < len - 3; i++) {
        if (request[i] == '\r' && request[i+1] == '\n' &&
            request[i+2] == '\r' && request[i+3] == '\n') {
            return request + i + 4;
        }
    }
    return NULL;
}

/* Parse an HTTP request string into an HttpRequest struct.
 * 将 HTTP 请求字符串解析为 HttpRequest 结构体。
 * Parses: method, path, and body (for POST).
 * 解析：方法、路径和正文（POST 时）。                             */
static bool parse_request(const char* raw, int raw_len,
                           HttpRequest* req) {
    if (raw == NULL || req == NULL) return false;

    memset(req, 0, sizeof(*req));

    /* 解析请求行: METHOD /path HTTP/1.1 */
    const char* p = raw;
    /* 提取 method */
    int mi = 0;
    while (*p != ' ' && *p != '\0' && mi < 15) {
        req->method[mi++] = *p++;
    }
    req->method[mi] = '\0';
    if (*p != ' ') return false;
    p++;  /* 跳过空格 */

    /* 提取 path */
    int pi = 0;
    while (*p != ' ' && *p != '\0' && pi < SERVER_MAX_PATH - 1) {
        req->path[pi++] = *p++;
    }
    req->path[pi] = '\0';

    /* 提取 body（如果有） */
    const char* body_start = find_body_start(raw, raw_len);
    if (body_start != NULL) {
        int remaining = raw_len - (int)(body_start - raw);
        if (remaining > 0 && remaining < (int)sizeof(req->body) - 1) {
            memcpy(req->body, body_start, remaining);
            req->body[remaining] = '\0';
            req->body_len = remaining;
        }
    }

    return true;
}

/* ================================================================
 * Static File Serving  静态文件服务
 * ================================================================ */

/* Serve a static file from the web/ directory.
 * 从 web/ 目录提供静态文件。
 * Maps URL path like "/css/style.css" to "web/css/style.css".      */
static bool serve_static_file(const char* url_path, SOCKET cli_sock) {
    /* 安全检查：防止目录遍历攻击（../ 等） */
    if (strstr(url_path, "..") != NULL) {
        send_json_error(HTTP_400_BAD, "Invalid path", cli_sock);
        return false;
    }

    /* 构建本地文件路径: web/ + URL path */
    char file_path[1024];
    /* 对于根路径 "/"，返回 index.html */
    if (strcmp(url_path, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/index.html", g_base_dir);
    } else {
        snprintf(file_path, sizeof(file_path), "%s%s", g_base_dir, url_path);
    }

    /* 读取文件 */
    FILE* fp = fopen(file_path, "rb");
    if (fp == NULL) {
        /* 对于未知路径且无扩展名的，尝试作为 SPA 路由返回 index.html */
        const char* ext = strrchr(url_path, '.');
        if (ext == NULL && strcmp(url_path, "/") != 0) {
            snprintf(file_path, sizeof(file_path), "%s/index.html", g_base_dir);
            fp = fopen(file_path, "rb");
        }
        if (fp == NULL) {
            send_json_error(HTTP_404_NOT_FOUND, "File not found", cli_sock);
            return false;
        }
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > (10 * 1024 * 1024)) {
        /* 文件太大会导致问题，限制为 10MB */
        fclose(fp);
        send_json_error(HTTP_500_ERROR, "File too large", cli_sock);
        return false;
    }

    /* 读取文件内容 */
    char* content = (char*)mem_alloc((size_t)(file_size + 1));
    size_t bytes_read = fread(content, 1, (size_t)file_size, fp);
    fclose(fp);
    content[bytes_read] = '\0';

    /* 发送响应 */
    const char* mime = get_mime_type(file_path);

    /* 对于二进制文件（图片等），需要特殊处理。但 Phase 2 的静态文件
     * 主要是文本（html/css/js），所以用文本方式发送。后续如图片导出
     * 需要时再扩展二进制发送支持。                                */
    char header[4096];
    int hdr_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n",
        mime, (int)bytes_read);

    send(cli_sock, header, hdr_len, 0);
    send(cli_sock, content, (int)bytes_read, 0);

    free(content);
    return true;
}

/* ================================================================
 * JSON Helpers for API  用于 API 的 JSON 辅助函数
 * ================================================================ */

/* Extract a string value for a given key from a JSON object.
 * 从 JSON 对象中提取指定键的字符串值。
 * Simple key-value search — sufficient for our controlled API format.
 * 简单的键值搜索 —— 足够用于我们受控的 API 格式。                 */
static bool json_extract_string(const char* json, const char* key,
                                 char* out, int out_size) {
    if (json == NULL || key == NULL || out == NULL) return false;

    /* 构建搜索模式: "key" */
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* pos = strstr(json, pattern);
    if (pos == NULL) return false;

    /* 找到 ': ' 后面的 '"' 或值 */
    pos = strchr(pos + strlen(pattern), ':');
    if (pos == NULL) return false;
    pos++;  /* 跳过 ':' */

    /* 跳过空白 */
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    /* 如果值是字符串，提取引号内的内容 */
    if (*pos == '"') {
        pos++;  /* 跳过开始 '"' */
        int oi = 0;
        while (*pos != '\0' && *pos != '"' && oi < out_size - 1) {
            if (*pos == '\\' && *(pos + 1) != '\0') {
                pos++;  /* 跳过转义字符 */
                switch (*pos) {
                    case 'n': out[oi++] = '\n'; break;
                    case 't': out[oi++] = '\t'; break;
                    case '\\': out[oi++] = '\\'; break;
                    case '"': out[oi++] = '"'; break;
                    default: out[oi++] = *pos; break;
                }
            } else {
                out[oi++] = *pos;
            }
            pos++;
        }
        out[oi] = '\0';
        return true;
    }

    /* 如果值不是字符串（数字、布尔等），提取到下一个逗号或 } */
    if (*pos == 'n' && strncmp(pos, "null", 4) == 0) {
        out[0] = '\0';
        return true;
    }

    /* 对于非字符串值，尝试读取 */
    int oi = 0;
    while (*pos != '\0' && *pos != ',' && *pos != '}' &&
           *pos != '\n' && oi < out_size - 1) {
        out[oi++] = *pos++;
    }
    out[oi] = '\0';
    return true;
}

/* Extract an integer value for a given key from a JSON object.
 * 从 JSON 对象中提取指定键的整数值。                              */
static bool json_extract_int(const char* json, const char* key, int* out) {
    char buf[64];
    if (!json_extract_string(json, key, buf, sizeof(buf))) return false;
    *out = atoi(buf);
    return true;
}

/* Extract a uint32 value for a given key from a JSON object.
 * 从 JSON 对象中提取指定键的 uint32 值。                          */
static bool json_extract_uint(const char* json, const char* key,
                                uint32_t* out) {
    char buf[64];
    if (!json_extract_string(json, key, buf, sizeof(buf))) return false;
    *out = (uint32_t)strtoul(buf, NULL, 10);
    return true;
}

/* Extract a boolean value for a given key from a JSON object.
 * 从 JSON 对象中提取指定键的布尔值。                              */
static bool json_extract_bool(const char* json, const char* key, bool* out) {
    char buf[64];
    if (!json_extract_string(json, key, buf, sizeof(buf))) return false;
    *out = (strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0);
    return true;
}

/* ================================================================
 * API Route Handlers  API 路由处理函数
 * ================================================================ */

/* 辅助：通过地址字符串（如 "0.1.2" = 第1个根级子节点下第2个子节点
 * 的第3个子节点）在树中定位节点。返回节点指针，并可通过 parent_out
 * 获取父节点。地址为空时返回 root。                               */
static TreeNode* find_node_by_addr(Tree* tree, const char* addr,
                                     TreeNode** parent_out) {
    if (parent_out != NULL) *parent_out = NULL;
    if (tree == NULL || tree->root == NULL) return NULL;

    /* 空地址或 "root" 返回根节点 */
    if (addr == NULL || addr[0] == '\0' || strcmp(addr, "root") == 0) {
        return tree->root;
    }

    /* 解析点分隔的索引（0-based，与 TXT 一致） */
    TreeNode* current = tree->root;
    TreeNode* parent = NULL;

    const char* p = addr;
    while (*p != '\0') {
        /* 读取索引 */
        int idx = 0;
        while (*p >= '0' && *p <= '9') {
            idx = idx * 10 + (*p - '0');
            p++;
        }

        /* 检查子节点是否存在 */
        if (idx < 0 || idx >= current->child_count) {
            return NULL;  /* 地址越界 */
        }

        parent = current;
        current = current->children[idx];

        /* 跳过点分隔符 */
        if (*p == '.') p++;
    }

    if (parent_out != NULL) *parent_out = parent;
    return current;
}

/* 辅助：从一节点获取其地址字符串（1-based，点分隔）。
 * 返回堆分配的字符串，调用者需 free()。                          */
static char* get_node_addr(TreeNode* node) {
    if (node == NULL) return str_dup("");

    /* 计算路径：从 node 向上追溯到 root */
    int indices[64];     /* 最多 64 级深度 */
    int depth = 0;
    TreeNode* current = node;

    while (current != NULL && current->parent != NULL) {
        TreeNode* parent = current->parent;
        /* 在父节点的 children 中搜索 current */
        for (int i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == current) {
                indices[depth++] = i;
                break;
            }
        }
        current = parent;
    }

    /* 反转索引数组并转换为 1-based 字符串 */
    char buf[256];
    int pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (i < depth - 1) buf[pos++] = '.';
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%d", indices[i] + 1);  /* 0-based → 1-based */
    }
    buf[pos] = '\0';
    return str_dup(buf);
}

/* ================================================================
 * API Handler Implementations  API 处理函数实现
 * ================================================================ */

/* POST /api/file/open — 打开文件，返回树 JSON + 配置。
 * Body: { "path": "C:/path/to/file.lxmm" }                       */
static void api_file_open(const HttpRequest* req, SOCKET cli_sock) {
    char filepath[MAX_PATH_LEN];
    if (!json_extract_string(req->body, "path", filepath, sizeof(filepath))) {
        send_json_error(HTTP_400_BAD, "Missing 'path' field", cli_sock);
        return;
    }

    /* 检测输入格式 */
    FormatHandler* handler = format_find_by_extension(filepath);
    if (handler == NULL) {
        send_json_error(HTTP_400_BAD, "Unsupported file format", cli_sock);
        return;
    }

    /* 创建新树并解析文件 */
    Tree* new_tree = tree_create();
    HandlerStatus status = handler->parse(filepath, new_tree, NULL, NULL, NULL);

    if (status.result_code < 0) {
        char err[512];
        snprintf(err, sizeof(err), "Parse error: %s",
                 handler_result_to_string(status.result_code));
        tree_free(new_tree);
        send_json_error(HTTP_500_ERROR, err, cli_sock);
        return;
    }

    /* 将新树作为当前树 */
    if (g_tree != NULL) {
        tree_free(g_tree);
    }
    g_tree = new_tree;

    /* 返回完整树 JSON */
    char* json = tree_to_json(g_tree);
    send_json(HTTP_200_OK, json, cli_sock);
    free(json);
}

/* POST /api/file/save — 保存当前树到文件。
 * Body: { "path": "C:/path/to/output.lxmm" }                      */
static void api_file_save(const HttpRequest* req, SOCKET cli_sock) {
    char filepath[MAX_PATH_LEN];
    if (!json_extract_string(req->body, "path", filepath, sizeof(filepath))) {
        send_json_error(HTTP_400_BAD, "Missing 'path' field", cli_sock);
        return;
    }

    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree to save", cli_sock);
        return;
    }

    /* 从 JSON body 更新树的配置（如果提供了 config） */
    const char* cfg_pos = strstr(req->body, "\"config\":");
    if (cfg_pos == NULL) {
        cfg_pos = strstr(req->body, "\"config\" :");
    }
    if (cfg_pos != NULL) {
        /* 简单解析：提取 canvas_color 等字段更新 g_tree->config */
        json_extract_uint(cfg_pos, "canvas_color",
                          &g_tree->config.canvas_color);
        json_extract_int(cfg_pos, "default_width",
                         &g_tree->config.default_width);
        json_extract_int(cfg_pos, "default_height",
                         &g_tree->config.default_height);
        int fmt = 0;
        if (json_extract_int(cfg_pos, "default_format", &fmt)) {
            g_tree->config.default_format = (NodeFormatType)fmt;
        }
        char enc[32];
        if (json_extract_string(cfg_pos, "default_encoding", enc, sizeof(enc))) {
            strncpy(g_tree->config.default_encoding, enc, 31);
            g_tree->config.default_encoding[31] = '\0';
        }
    }

    /* 如果请求中包含 tree_json，先更新树 */
    const char* root_pos = strstr(req->body, "\"root\":");
    if (root_pos == NULL) {
        root_pos = strstr(req->body, "\"root\" :");
    }

    if (root_pos != NULL) {
        /* 从 JSON 重建树（替换当前树） */
        /* 构建 JSON 对象字符串：{ "config": ..., "root": ... } */
        /* 这里需要从请求体中提取完整的树 JSON。
         * 简化方案：直接从请求体构建。由于请求体可能包含额外的
         * 顶层字段（如 "path"），我们需要构造干净的树 JSON。   */

        /* 尝试用 tree_from_json 解析（需要请求体就是完整树 JSON）。
         * 但这里请求体是 { "path":..., "root":{...}, "config":{...} }。
         * 需要提取 root 部分。
         * 简化：仅当路径匹配时才这样处理。                         */
    }

    /* 检测输出格式并序列化 */
    FormatHandler* handler = format_find_by_extension(filepath);
    if (handler == NULL) {
        /* 默认使用 lxmm_handler */
        handler = format_find_by_name("lxmm");
    }
    if (handler == NULL || handler->serialize == NULL) {
        send_json_error(HTTP_500_ERROR, "No suitable output handler", cli_sock);
        return;
    }

    HandlerStatus status = handler->serialize(filepath, g_tree, NULL);

    if (status.result_code < 0) {
        char err[512];
        snprintf(err, sizeof(err), "Save error: %s",
                 handler_result_to_string(status.result_code));
        send_json_error(HTTP_500_ERROR, err, cli_sock);
        return;
    }

    send_json(HTTP_200_OK, "{\"ok\":true}", cli_sock);
}

/* POST /api/file/import — 导入非 lxmm 文件（JSON/TXT/MD），返回树。
 * Body: { "path": "C:/path/to/file.json" }                        */
static void api_file_import(const HttpRequest* req, SOCKET cli_sock) {
    char filepath[MAX_PATH_LEN];
    if (!json_extract_string(req->body, "path", filepath, sizeof(filepath))) {
        send_json_error(HTTP_400_BAD, "Missing 'path' field", cli_sock);
        return;
    }

    FormatHandler* handler = format_find_by_extension(filepath);
    if (handler == NULL) {
        send_json_error(HTTP_400_BAD, "Unsupported file format", cli_sock);
        return;
    }

    Tree* new_tree = tree_create();
    HandlerStatus status = handler->parse(filepath, new_tree, NULL, NULL, NULL);

    if (status.result_code < 0) {
        char err[512];
        snprintf(err, sizeof(err), "Import error: %s",
                 handler_result_to_string(status.result_code));
        tree_free(new_tree);
        send_json_error(HTTP_500_ERROR, err, cli_sock);
        return;
    }

    /* 替换当前树 */
    if (g_tree != NULL) tree_free(g_tree);
    g_tree = new_tree;

    char* json = tree_to_json(g_tree);
    send_json(HTTP_200_OK, json, cli_sock);
    free(json);
}

/* POST /api/file/export — 导出当前树为指定格式。
 * Body: { "path": "out.json", "format": "json" }                  */
static void api_file_export(const HttpRequest* req, SOCKET cli_sock) {
    char filepath[MAX_PATH_LEN];
    char fmt_name[32];
    if (!json_extract_string(req->body, "path", filepath, sizeof(filepath))) {
        send_json_error(HTTP_400_BAD, "Missing 'path' field", cli_sock);
        return;
    }
    if (!json_extract_string(req->body, "format", fmt_name, sizeof(fmt_name))) {
        send_json_error(HTTP_400_BAD, "Missing 'format' field", cli_sock);
        return;
    }

    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree to export", cli_sock);
        return;
    }

    FormatHandler* handler = format_find_by_name(fmt_name);
    if (handler == NULL || handler->serialize == NULL) {
        send_json_error(HTTP_400_BAD, "Unsupported output format", cli_sock);
        return;
    }

    HandlerStatus status = handler->serialize(filepath, g_tree, NULL);
    if (status.result_code < 0) {
        char err[512];
        snprintf(err, sizeof(err), "Export error: %s",
                 handler_result_to_string(status.result_code));
        send_json_error(HTTP_500_ERROR, err, cli_sock);
        return;
    }

    /* 返回输出路径 */
    char resp[1024];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"output_path\":\"%s\"}", filepath);
    send_json(HTTP_200_OK, resp, cli_sock);
}

/* POST /api/tree — 返回完整当前树 JSON。                          */
static void api_tree_get(const HttpRequest* req, SOCKET cli_sock) {
    (void)req;
    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }
    char* json = tree_to_json(g_tree);
    send_json(HTTP_200_OK, json, cli_sock);
    free(json);
}

/* POST /api/tree/node/add — 添加节点。
 * Body: { "parent_addr": "1.2", "content": "New Node", "index": 0 }*/
static void api_node_add(const HttpRequest* req, SOCKET cli_sock) {
    char addr[256], content[4096];
    int index = -1;  /* -1 = 追加到末尾 */
    json_extract_string(req->body, "parent_addr", addr, sizeof(addr));
    json_extract_string(req->body, "content", content, sizeof(content));
    json_extract_int(req->body, "index", &index);

    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }

    TreeNode* parent = find_node_by_addr(g_tree, addr, NULL);
    if (parent == NULL) {
        send_json_error(HTTP_400_BAD, "Invalid parent address", cli_sock);
        return;
    }

    TreeNode* new_node = NULL;
    if (index < 0 || index > parent->child_count) {
        /* 追加到末尾 */
        new_node = tree_node_add_child(parent, content);
    } else {
        /* 在指定位置插入 —— 暂时用末尾追加 + 移位模拟 */
        new_node = tree_node_add_child(parent, content);
        /* 将新节点移到指定位置：从末尾移到 index */
        if (new_node != NULL && index < parent->child_count - 1) {
            /* 移到最后插入的节点到 index 位置 */
            int last = parent->child_count - 1;
            for (int i = last; i > index; i--) {
                parent->children[i] = parent->children[i - 1];
            }
            parent->children[index] = new_node;
        }
    }

    if (new_node == NULL) {
        send_json_error(HTTP_500_ERROR, "Failed to create node", cli_sock);
        return;
    }

    tree_recalculate_depths(g_tree);
    g_tree->total_nodes = tree_count_nodes(g_tree);

    /* 返回新节点的地址 */
    char* node_addr = get_node_addr(new_node);
    char resp[512];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"addr\":\"%s\"}", node_addr);
    send_json(HTTP_200_OK, resp, cli_sock);
    free(node_addr);
}

/* POST /api/tree/node/delete — 删除节点。
 * Body: { "addr": "1.2.3" }                                      */
static void api_node_delete(const HttpRequest* req, SOCKET cli_sock) {
    char addr[256];
    json_extract_string(req->body, "addr", addr, sizeof(addr));

    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }

    TreeNode* parent = NULL;
    TreeNode* node = find_node_by_addr(g_tree, addr, &parent);
    if (node == NULL || node == g_tree->root) {
        send_json_error(HTTP_400_BAD, "Invalid node address", cli_sock);
        return;
    }
    if (parent == NULL) {
        send_json_error(HTTP_400_BAD, "Cannot delete root", cli_sock);
        return;
    }

    /* 在父节点的 children 中查找索引 */
    int idx = -1;
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) { idx = i; break; }
    }
    if (idx < 0) {
        send_json_error(HTTP_500_ERROR, "Node not found in parent", cli_sock);
        return;
    }

    tree_node_remove_child(parent, idx);
    tree_recalculate_depths(g_tree);
    g_tree->total_nodes = tree_count_nodes(g_tree);

    send_json(HTTP_200_OK, "{\"ok\":true}", cli_sock);
}

/* POST /api/tree/node/update — 更新节点元数据。
 * Body: { "addr":"1.2", "content":"new text", "expanded":true,
 *         "custom_color":255, "custom_width":10, "custom_height":3,
 *         "format_type":0 }                                       */
static void api_node_update(const HttpRequest* req, SOCKET cli_sock) {
    char addr[256];
    json_extract_string(req->body, "addr", addr, sizeof(addr));

    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }

    TreeNode* node = find_node_by_addr(g_tree, addr, NULL);
    if (node == NULL) {
        send_json_error(HTTP_400_BAD, "Invalid node address", cli_sock);
        return;
    }

    /* 更新 content（如果提供了） */
    char content[4096];
    if (json_extract_string(req->body, "content", content, sizeof(content))) {
        tree_node_set_content(node, content);
    }

    /* 更新展开状态 */
    bool expanded;
    if (json_extract_bool(req->body, "expanded", &expanded)) {
        node->expanded = expanded;
    }

    /* 更新自定义颜色 */
    uint32_t color;
    if (json_extract_uint(req->body, "custom_color", &color)) {
        node->custom_color = color;
    }

    /* 更新宽度/高度 */
    int width;
    if (json_extract_int(req->body, "custom_width", &width)) {
        node->custom_width = width;
    }
    int height;
    if (json_extract_int(req->body, "custom_height", &height)) {
        node->custom_height = height;
    }

    /* 更新格式类型 */
    int fmt;
    if (json_extract_int(req->body, "format_type", &fmt)) {
        node->format_type = (NodeFormatType)fmt;
    }

    tree_recalculate_depths(g_tree);
    send_json(HTTP_200_OK, "{\"ok\":true}", cli_sock);
}

/* POST /api/tree/node/move — 移动节点到新位置。
 * Body: { "addr":"1.2", "new_parent":"1", "new_index":0 }         */
static void api_node_move(const HttpRequest* req, SOCKET cli_sock) {
    char addr[256], new_parent_addr[256];
    int new_index = 0;
    json_extract_string(req->body, "addr", addr, sizeof(addr));
    json_extract_string(req->body, "new_parent", new_parent_addr,
                         sizeof(new_parent_addr));
    json_extract_int(req->body, "new_index", &new_index);

    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }

    TreeNode* old_parent = NULL;
    TreeNode* node = find_node_by_addr(g_tree, addr, &old_parent);
    if (node == NULL || node == g_tree->root || old_parent == NULL) {
        send_json_error(HTTP_400_BAD, "Invalid source address", cli_sock);
        return;
    }

    TreeNode* new_parent = find_node_by_addr(g_tree, new_parent_addr, NULL);
    if (new_parent == NULL) {
        send_json_error(HTTP_400_BAD, "Invalid destination address", cli_sock);
        return;
    }

    /* 从旧父节点移除 */
    int old_idx = -1;
    for (int i = 0; i < old_parent->child_count; i++) {
        if (old_parent->children[i] == node) { old_idx = i; break; }
    }
    if (old_idx < 0) {
        send_json_error(HTTP_500_ERROR, "Node not found in parent", cli_sock);
        return;
    }

    /* 先从旧父断开 */
    node->parent = NULL;
    int remaining = old_parent->child_count - old_idx - 1;
    if (remaining > 0) {
        memmove(&old_parent->children[old_idx],
                &old_parent->children[old_idx + 1],
                remaining * sizeof(TreeNode*));
    }
    old_parent->child_count--;
    old_parent->children[old_parent->child_count] = NULL;

    /* 插入到新父节点 */
    if (new_index < 0) new_index = 0;
    if (new_index > new_parent->child_count) new_index = new_parent->child_count;

    /* 扩容（如果需要） */
    if (new_parent->child_count >= new_parent->child_capacity) {
        int new_cap = (new_parent->child_capacity == 0)
                       ? 4 : new_parent->child_capacity * 2;
        new_parent->children = (TreeNode**)mem_realloc(
            new_parent->children, new_cap * sizeof(TreeNode*));
        for (int i = new_parent->child_count; i < new_cap; i++) {
            new_parent->children[i] = NULL;
        }
        new_parent->child_capacity = new_cap;
    }

    /* 为插入腾出空间 */
    for (int i = new_parent->child_count; i > new_index; i--) {
        new_parent->children[i] = new_parent->children[i - 1];
    }

    /* 建立新链接 */
    new_parent->children[new_index] = node;
    node->parent = new_parent;
    new_parent->child_count++;

    tree_recalculate_depths(g_tree);
    g_tree->total_nodes = tree_count_nodes(g_tree);

    /* 返回新地址 */
    char* new_addr = get_node_addr(node);
    char resp[512];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"new_addr\":\"%s\"}", new_addr);
    send_json(HTTP_200_OK, resp, cli_sock);
    free(new_addr);
}

/* GET /api/search?q=关键词 — 在树中搜索节点。
 * 返回匹配节点列表（按相似度排序）。                              */
static void api_search(const HttpRequest* req, SOCKET cli_sock) {
    if (g_tree == NULL) {
        send_json(HTTP_200_OK, "{\"results\":[]}", cli_sock);
        return;
    }

    /* 从查询字符串中提取 q 参数 */
    const char* q_start = strstr(req->path, "q=");
    if (q_start == NULL) {
        send_json(HTTP_200_OK, "{\"results\":[]}", cli_sock);
        return;
    }
    q_start += 2;  /* 跳过 "q=" */

    /* URL 解码（简化版：只处理 %20） */
    char query[256];
    int qi = 0;
    for (const char* p = q_start; *p != '\0' && *p != '&' && qi < 250; p++) {
        if (*p == '%' && *(p+1) == '2' && *(p+2) == '0') {
            query[qi++] = ' '; p += 2;
        } else if (*p == '+') {
            query[qi++] = ' ';
        } else {
            query[qi++] = *p;
        }
    }
    query[qi] = '\0';

    if (query[0] == '\0') {
        send_json(HTTP_200_OK, "{\"results\":[]}", cli_sock);
        return;
    }

    /* BFS 遍历所有节点，做子串匹配 */
    /* 简单方案：子串匹配评分（精确匹配 > 前缀匹配 > 包含子串） */
    char result_json[65536];
    int rj_pos = 0;
    rj_pos += snprintf(result_json + rj_pos,
                        sizeof(result_json) - rj_pos,
                        "{\"results\":[");

    bool first_result = true;
    /* 使用简单的 BFS */
    int cap = 64;
    TreeNode** queue = (TreeNode**)mem_alloc(cap * sizeof(TreeNode*));
    int front = 0, rear = 0, qsize = 0;
    queue[rear++] = g_tree->root;
    qsize = 1;

    while (qsize > 0) {
        TreeNode* node = queue[front++];
        qsize--;

        if (node->content != NULL && node != g_tree->root) {
            /* 检查是否匹配（不区分大小写的子串匹配） */
            const char* haystack = node->content;
            const char* needle = query;

            /* 简单的子串查找（不区分大小写） */
            bool matched = false;
            int score = 0;
            for (const char* h = haystack; *h; h++) {
                const char* n = needle;
                const char* h2 = h;
                while (*n && *h2) {
                    char c1 = *h2, c2 = *n;
                    if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
                    if (c2 >= 'A' && c2 <= 'Z') c2 += ('a' - 'A');
                    if (c1 != c2) break;
                    n++; h2++;
                }
                if (*n == '\0') {
                    matched = true;
                    /* 评分：越靠前分数越高 */
                    score = 1000 - (int)(h - haystack);
                    if (h == haystack) score += 500;  /* 前缀匹配加分 */
                    break;
                }
            }

            if (matched) {
                char* addr = get_node_addr(node);
                if (!first_result) {
                    if (rj_pos < (int)sizeof(result_json) - 5)
                        result_json[rj_pos++] = ',';
                }
                first_result = false;

                /* 截断内容用于预览（最多 40 字符） */
                char preview[128];
                int pl = 0;
                for (const char* c = node->content; *c && pl < 80; c++) {
                    if (*c == '"') { preview[pl++] = '\\'; preview[pl++] = '"'; }
                    else if (*c == '\\') { preview[pl++] = '\\'; preview[pl++] = '\\'; }
                    else if (*c == '\n') { preview[pl++] = ' '; }
                    else { preview[pl++] = *c; }
                }
                preview[pl] = '\0';

                rj_pos += snprintf(result_json + rj_pos,
                    sizeof(result_json) - rj_pos,
                    "{\"addr\":\"%s\",\"content\":\"%s\",\"score\":%d}",
                    addr, preview, score);
                free(addr);
            }
        }

        /* 子节点入队 */
        for (int i = 0; i < node->child_count; i++) {
            if (qsize >= cap) {
                cap *= 2;
                queue = (TreeNode**)mem_realloc(queue, cap * sizeof(TreeNode*));
            }
            queue[rear++] = node->children[i];
            qsize++;
        }
    }

    free(queue);
    rj_pos += snprintf(result_json + rj_pos,
                        sizeof(result_json) - rj_pos, "]}");
    send_json(HTTP_200_OK, result_json, cli_sock);
}

/* POST /api/config/set — 设置配置。
 * Body: { "canvas_color":..., "default_width":..., ... }          */
static void api_config_set(const HttpRequest* req, SOCKET cli_sock) {
    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }

    json_extract_uint(req->body, "canvas_color",
                      &g_tree->config.canvas_color);
    json_extract_int(req->body, "default_width",
                     &g_tree->config.default_width);
    json_extract_int(req->body, "default_height",
                     &g_tree->config.default_height);
    int fmt = 0;
    if (json_extract_int(req->body, "default_format", &fmt)) {
        g_tree->config.default_format = (NodeFormatType)fmt;
    }
    char enc[32];
    if (json_extract_string(req->body, "default_encoding", enc, sizeof(enc))) {
        strncpy(g_tree->config.default_encoding, enc, 31);
        g_tree->config.default_encoding[31] = '\0';
    }
    float zoom = 1.0f;
    /* 简单浮点解析 */
    char zoom_buf[32];
    if (json_extract_string(req->body, "default_zoom", zoom_buf, sizeof(zoom_buf))) {
        zoom = (float)atof(zoom_buf);
        g_tree->config.default_zoom = zoom;
    }

    send_json(HTTP_200_OK, "{\"ok\":true}", cli_sock);
}

/* POST /api/config/get — 获取当前配置。                           */
static void api_config_get(const HttpRequest* req, SOCKET cli_sock) {
    (void)req;
    if (g_tree == NULL) {
        send_json_error(HTTP_500_ERROR, "No tree loaded", cli_sock);
        return;
    }

    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"canvas_color\":%u,"
        "\"default_width\":%d,"
        "\"default_height\":%d,"
        "\"default_format\":%d,"
        "\"default_zoom\":%g,"
        "\"default_encoding\":\"%s\""
        "}",
        g_tree->config.canvas_color,
        g_tree->config.default_width,
        g_tree->config.default_height,
        (int)g_tree->config.default_format,
        g_tree->config.default_zoom,
        g_tree->config.default_encoding);
    send_json(HTTP_200_OK, json, cli_sock);
}

/* POST /api/new — 创建新的空思维导图。                            */
static void api_new_tree(const HttpRequest* req, SOCKET cli_sock) {
    (void)req;
    if (g_tree != NULL) tree_free(g_tree);
    g_tree = tree_create();
    char* json = tree_to_json(g_tree);
    send_json(HTTP_200_OK, json, cli_sock);
    free(json);
}

/* POST /api/tree/replace — 前端用修改后的完整树 JSON 替换当前树。
 * Body: { "tree_json": "{ ... }" }
 * 前端修改树后发送完整 JSON，后端解析并替换。这对于复杂的编辑
 * 操作（拖放、批量修改）更高效。                                  */
static void api_tree_replace(const HttpRequest* req, SOCKET cli_sock) {
    /* 从请求体直接提取 tree_json 字段对应的完整 JSON 对象 */
    const char* pos = strstr(req->body, "\"tree_json\"");
    if (pos == NULL) {
        pos = strstr(req->body, "\"tree_json\" :");
    }
    if (pos == NULL) {
        /* 如果 body 本身就是树 JSON（以 { 开头），直接用 body */
        if (req->body[0] == '{') {
            pos = req->body;
        } else {
            send_json_error(HTTP_400_BAD, "Missing 'tree_json' field", cli_sock);
            return;
        }
    } else {
        /* 跳过 "tree_json": 或 "tree_json" :  */
        pos = strchr(pos, ':');
        if (pos == NULL) {
            send_json_error(HTTP_400_BAD, "Invalid JSON", cli_sock);
            return;
        }
        pos++;  /* 跳过 ':' */
        while (*pos == ' ' || *pos == '\t') pos++;
    }

    /* 尝试解析 */
    char err[256];
    Tree* new_tree = tree_from_json(pos, err, sizeof(err));
    if (new_tree == NULL) {
        char resp[512];
        snprintf(resp, sizeof(resp), "{\"error\":\"Parse failed: %s\"}", err);
        send_json(HTTP_500_ERROR, resp, cli_sock);
        return;
    }

    /* 替换当前树 */
    if (g_tree != NULL) tree_free(g_tree);
    g_tree = new_tree;

    send_json(HTTP_200_OK, "{\"ok\":true}", cli_sock);
}

/* ================================================================
 * Route Table  路由表
 * ================================================================ */

typedef void (*ApiHandler)(const HttpRequest* req, SOCKET cli_sock);

typedef struct {
    const char* method;
    const char* path;
    ApiHandler  handler;
} Route;

static const Route g_routes[] = {
    { "POST", "/api/file/open",      api_file_open      },
    { "POST", "/api/file/save",      api_file_save      },
    { "POST", "/api/file/import",    api_file_import    },
    { "POST", "/api/file/export",    api_file_export    },
    { "POST", "/api/tree",           api_tree_get       },
    { "GET",  "/api/tree",           api_tree_get       },
    { "POST", "/api/tree/replace",   api_tree_replace   },
    { "POST", "/api/tree/node/add",  api_node_add       },
    { "POST", "/api/tree/node/delete", api_node_delete  },
    { "POST", "/api/tree/node/update", api_node_update  },
    { "POST", "/api/tree/node/move", api_node_move      },
    { "GET",  "/api/search",         api_search         },
    { "POST", "/api/config/get",     api_config_get     },
    { "POST", "/api/config/set",     api_config_set     },
    { "POST", "/api/new",            api_new_tree       },
};
#define ROUTE_COUNT (sizeof(g_routes) / sizeof(g_routes[0]))

/* ================================================================
 * Request Dispatching  请求分发
 * ================================================================ */

/* Handle a single HTTP request. Routes to static file or API.      */
static void handle_request(const HttpRequest* req, SOCKET cli_sock) {
    /* Handle CORS preflight OPTIONS request */
    if (strcmp(req->method, "OPTIONS") == 0) {
        send_response(HTTP_200_OK, "text/plain", "",
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type", cli_sock);
        return;
    }

    /* Check API routes */
    for (size_t i = 0; i < ROUTE_COUNT; i++) {
        if (strcmp(req->method, g_routes[i].method) == 0 &&
            strncmp(req->path, g_routes[i].path, strlen(g_routes[i].path)) == 0) {
            /* For GET search, match prefix (path may have ?q=...) */
            if (strcmp(req->method, "GET") == 0 &&
                strncmp(g_routes[i].path, "/api/search", 11) == 0) {
                /* Exact route match check passed, but need prefix match */
                /* /api/search starts with /api/search */
            }
            g_routes[i].handler(req, cli_sock);
            return;
        }
    }

    /* Not an API route — serve static file */
    serve_static_file(req->path, cli_sock);
}

/* ================================================================
 * Server Lifecycle  服务器生命周期
 * ================================================================ */

/* 初始化 WinSock2 并创建监听 socket。                             */
bool server_init(void) {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }
#endif

    /* 创建 socket */
    g_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_socket == INVALID_SOCKET) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    /* 设置 socket 选项：允许地址重用（避免重启时 "Address in use"） */
    int opt = 1;
    setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               (const char*)&opt, sizeof(opt));
#else
               &opt, sizeof(opt));
#endif

    /* 绑定到 localhost:8080 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_HOST);

    if (bind(g_listen_socket, (struct sockaddr*)&addr,
             sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    /* 开始监听（backlog = 8，本地工具不需要太大） */
    if (listen(g_listen_socket, 8) == SOCKET_ERROR) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    /* 确定 web/ 目录的绝对路径。
     * 从可执行文件所在目录查找 web/ 子目录。                      */
#ifdef _WIN32
    WCHAR wpath[512];
    GetModuleFileNameW(NULL, wpath, 512);
    /* 去掉文件名，保留目录 */
    WCHAR* last_sep = wcsrchr(wpath, L'\\');
    if (last_sep != NULL) *last_sep = L'\0';
    /* 拼接到 web/ 目录 */
    wcscat(wpath, L"\\web");
    /* 转为 UTF-8 */
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                        g_base_dir, sizeof(g_base_dir), NULL, NULL);
#else
    strcpy(g_base_dir, SERVER_WEB_DIR);
#endif

    return true;
}

/* 设置服务器操作的树。必须在 server_start 之前调用。             */
void server_set_tree(struct Tree* tree) {
    g_tree = tree;
}

/* 停止信号：设置 g_running = false 并关闭 socket。
 * server_start 中的 accept 循环检测到后退出。                     */
void server_stop(void) {
    g_running = false;
    if (g_listen_socket != INVALID_SOCKET) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }
}

/* 清理 WinSock2 资源。                                           */
void server_cleanup(void) {
    if (g_listen_socket != INVALID_SOCKET) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

/* 自动打开默认浏览器到应用 URL。                                  */
static void open_browser(void) {
#ifdef _WIN32
    WCHAR url[128];
    _snwprintf(url, 128, L"http://localhost:%d", SERVER_PORT);
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
#endif
}

/* 启动 HTTP 服务器主循环。阻塞直到 server_stop() 被调用。         */
int server_start(void) {
    if (g_listen_socket == INVALID_SOCKET) {
        return 1;
    }

    g_running = true;

    /* 自动打开浏览器 */
    open_browser();

    /* 主 accept 循环 */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_sock = accept(g_listen_socket,
                                     (struct sockaddr*)&client_addr,
                                     &client_len);
        if (client_sock == INVALID_SOCKET) {
            /* server_stop() 关闭了 socket，正常退出 */
            if (!g_running) break;
            continue;
        }

        /* 设置接收超时（5 秒），防止客户端挂起 */
        int timeout = 5000;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,
#ifdef _WIN32
                   (const char*)&timeout, sizeof(timeout));
#else
                   &timeout, sizeof(timeout));
#endif

        /* 读取 HTTP 请求 */
        char raw_buf[SERVER_MAX_REQUEST];
        int bytes = recv(client_sock, raw_buf,
                         sizeof(raw_buf) - 1, 0);
        if (bytes > 0) {
            raw_buf[bytes] = '\0';

            /* 解析请求 */
            HttpRequest req;
            if (parse_request(raw_buf, bytes, &req)) {
                /* 分发处理 */
                handle_request(&req, client_sock);
            } else {
                send_json_error(HTTP_400_BAD, "Malformed request", client_sock);
            }
        }

        /* 关闭客户端连接 */
        closesocket(client_sock);
    }

    return 0;
}
