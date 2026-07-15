/* ============================================================
 * lxmm_handler.c - .lxmm Binary Format Handler Implementation
 * lxmm_handler.c - .lxmm 二进制格式处理器实现
 *
 * Implements the binary .lxmm mind map file format for reading
 * and writing tree structures with full metadata (colors, sizes,
 * expanded states, format types, and file-level configuration).
 * 实现二进制 .lxmm 思维导图文件格式的读写，包含完整元数据
 *（颜色、尺寸、展开状态、格式类型和文件级配置）。
 *
 * Binary format specification 二进制格式规范:
 *
 *   FILE HEADER (32 bytes 字节):
 *     Offset 0:  Magic "LXMM" (4 bytes, 0x4C584D4D)
 *     Offset 4:  Version uint16 (2 bytes, initial = 1)
 *     Offset 6:  Flags uint16 (2 bytes, reserved)
 *     Offset 8:  NodeCount uint32 (4 bytes)
 *     Offset 12: ConfigOff uint32 (4 bytes, offset to config block)
 *     Offset 16: RootOff uint32 (4 bytes, offset to first node)
 *     Offset 20: Reserved (12 bytes, zero-filled)
 *
 *   CONFIG BLOCK (variable length 变长):
 *     CanvasColor:  uint32 (4 bytes, ARGB)
 *     DefaultWidth:  uint16 (2 bytes, 0 = auto)
 *     DefaultHeight: uint16 (2 bytes, 0 = auto)
 *     DefaultFormat: uint8  (1 byte, 0=plain, 1=latex, 2=html)
 *     DefaultZoom:   float32 (4 bytes)
 *     EncodingLen:   uint8  (1 byte, length of encoding name)
 *     Encoding:      char[EncodingLen] (UTF-8 encoding name)
 *
 *   NODE BLOCK (per node, variable length 每节点，变长):
 *     Flags:      uint8  (1 byte)
 *       bit0: expanded (1 = showing children)
 *       bit1: has_content (1 = content follows)
 *       bit2: has_custom_color (1 = color field present)
 *       bit3: has_custom_size (1 = width/height fields present)
 *       bit4-7: reserved (must be 0)
 *     ChildCount: uint16 (2 bytes, number of direct children)
 *     ContentLen: uint16 (2 bytes, 0 if no content or has_content=0)
 *     Content:    char[ContentLen] (UTF-8 text, only if has_content)
 *     Color:      uint32 (4 bytes, ARGB, only if has_custom_color)
 *     Width:      uint16 (2 bytes, only if has_custom_size)
 *     Height:     uint16 (2 bytes, only if has_custom_size)
 *     FormatType: uint8  (1 byte, only if has_content)
 *
 *   Nodes are written in pre-order traversal. Children immediately
 *   follow their parent's node block. No separate child offset
 *   table is needed — the reader recursively reads child_count
 *   nodes after each node.
 *   节点以前序遍历顺序写入。子节点紧跟父节点块。
 *   不需要单独的偏移表 — 读取器在每个节点后递归读取
 *   child_count 个节点。
 *
 * ============================================================ */

#include "lxmm_handler.h"
#include "tree.h"            /* TreeNode, Tree, TreeConfig, NodeFormatType */
#include "utils.h"           /* mem_alloc, SAFE_FREE */
#include "encoding.h"        /* encoding_read_file_utf8 */

#include <stdlib.h>          /* free, NULL */
#include <string.h>          /* strlen, memcpy, memcmp */
#include <stdio.h>           /* FILE, fopen, fread, fwrite, fclose, SEEK_* */
#include <stdint.h>          /* uint32_t, uint16_t, uint8_t */

#ifdef _WIN32
#include <windows.h>         /* MultiByteToWideChar, _wfopen (Unicode paths) */
#endif

/* ================================================================
 * Binary Format Constants  二进制格式常量
 * ================================================================ */

#define LXMM_MAGIC            0x4C584D4D  /* "LXMM" in little-endian */
#define LXMM_VERSION          1           /* Initial version 初版 */
#define LXMM_HEADER_SIZE      32          /* Fixed header size 固定头大小 */

/* Node flag bits 节点标志位 */
#define LXMM_FLAG_EXPANDED         0x01  /* bit0: expanded 展开 */
#define LXMM_FLAG_HAS_CONTENT      0x02  /* bit1: has text content 有文本内容 */
#define LXMM_FLAG_HAS_CUSTOM_COLOR 0x04  /* bit2: custom color present 有自定义颜色 */
#define LXMM_FLAG_HAS_CUSTOM_SIZE  0x08  /* bit3: custom size present 有自定义尺寸 */
/* bits 4-7 reserved 保留 */

/* ================================================================
 * File I/O helpers (Unicode-safe paths) 文件 I/O 辅助函数
 * ================================================================ */

/* 用 Unicode 安全的方式打开二进制文件以读取。
 * 在 Windows 上使用 _wfopen 支持中文路径。                       */
static FILE* lxmm_fopen_read(const char* filepath) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    WCHAR* wpath = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    if (wpath == NULL) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);
    FILE* fp = _wfopen(wpath, L"rb");
    free(wpath);
    return fp;
#else
    return fopen(filepath, "rb");
#endif
}

/* 用 Unicode 安全的方式打开二进制文件以写入。
 * 在 Windows 上使用 _wfopen 支持中文路径。                       */
static FILE* lxmm_fopen_write(const char* filepath) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    WCHAR* wpath = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    if (wpath == NULL) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);
    FILE* fp = _wfopen(wpath, L"wb");
    free(wpath);
    return fp;
#else
    return fopen(filepath, "wb");
#endif
}

/* ================================================================
 * Binary read/write helpers  二进制读写辅助函数
 * ================================================================ */

/* 安全地从文件读取指定字节数。失败返回 false。                    */
static bool read_bytes(FILE* fp, void* buf, size_t count) {
    return fread(buf, 1, count, fp) == count;
}

/* 安全地向文件写入指定字节数。失败返回 false。                    */
static bool write_bytes(FILE* fp, const void* buf, size_t count) {
    return fwrite(buf, 1, count, fp) == count;
}

/* 读取 uint8（1 字节无符号整数）。                                */
static bool read_u8(FILE* fp, uint8_t* out) {
    return read_bytes(fp, out, 1);
}

/* 写入 uint8。                                                    */
static bool write_u8(FILE* fp, uint8_t val) {
    return write_bytes(fp, &val, 1);
}

/* 读取 uint16（2 字节无符号整数，小端序）。                       */
static bool read_u16(FILE* fp, uint16_t* out) {
    uint8_t bytes[2];
    if (!read_bytes(fp, bytes, 2)) return false;
    *out = (uint16_t)(bytes[0] | (bytes[1] << 8));
    return true;
}

/* 写入 uint16（小端序）。                                         */
static bool write_u16(FILE* fp, uint16_t val) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)((val >> 8) & 0xFF);
    return write_bytes(fp, bytes, 2);
}

/* 读取 uint32（4 字节无符号整数，小端序）。                       */
static bool read_u32(FILE* fp, uint32_t* out) {
    uint8_t bytes[4];
    if (!read_bytes(fp, bytes, 4)) return false;
    *out = ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    return true;
}

/* 写入 uint32（小端序）。                                         */
static bool write_u32(FILE* fp, uint32_t val) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)((val >> 8) & 0xFF);
    bytes[2] = (uint8_t)((val >> 16) & 0xFF);
    bytes[3] = (uint8_t)((val >> 24) & 0xFF);
    return write_bytes(fp, bytes, 4);
}

/* 写入 float32（小端序，IEEE 754）。                              */
static bool write_float(FILE* fp, float val) {
    uint32_t bits;
    /* 通过 union 安全地将 float 转为 uint32_t 进行序列化 */
    memcpy(&bits, &val, sizeof(float));
    return write_u32(fp, bits);
}

/* 读取 float32（小端序）。                                        */
static bool read_float(FILE* fp, float* out) {
    uint32_t bits;
    if (!read_u32(fp, &bits)) return false;
    memcpy(out, &bits, sizeof(float));
    return true;
}

/* ================================================================
 * Serialization — Tree → .lxmm file  序列化 — Tree → .lxmm 文件
 * ================================================================ */

/* 前向声明，因为递归序列化函数互相引用。                          */
static bool serialize_node(FILE* fp, TreeNode* node);

/* 写入文件头（32 字节）。
 * 注意：NodeCount、ConfigOff、RootOff 在最终遍历后回填。          */
static bool write_header(FILE* fp, uint32_t node_count,
                          uint32_t config_off, uint32_t root_off) {
    /* Magic "LXMM" */
    uint8_t magic[4] = { 'L', 'X', 'M', 'M' };
    if (!write_bytes(fp, magic, 4)) return false;

    /* Version = 1 */
    if (!write_u16(fp, LXMM_VERSION)) return false;

    /* Flags (reserved) */
    if (!write_u16(fp, 0)) return false;

    /* NodeCount */
    if (!write_u32(fp, node_count)) return false;

    /* ConfigOff */
    if (!write_u32(fp, config_off)) return false;

    /* RootOff */
    if (!write_u32(fp, root_off)) return false;

    /* Reserved (12 bytes of zeros) */
    uint8_t reserved[12] = { 0 };
    if (!write_bytes(fp, reserved, 12)) return false;

    return true;
}

/* 写入配置块。返回写入的字节数（用于计算偏移）。                    */
static bool write_config_block(FILE* fp, const TreeConfig* cfg) {
    /* CanvasColor: uint32 ARGB */
    if (!write_u32(fp, cfg->canvas_color)) return false;

    /* DefaultWidth: uint16 */
    uint16_t dw = (uint16_t)((cfg->default_width > 65535)
                   ? 65535 : (cfg->default_width < 0 ? 0 : cfg->default_width));
    if (!write_u16(fp, dw)) return false;

    /* DefaultHeight: uint16 */
    uint16_t dh = (uint16_t)((cfg->default_height > 65535)
                   ? 65535 : (cfg->default_height < 0 ? 0 : cfg->default_height));
    if (!write_u16(fp, dh)) return false;

    /* DefaultFormat: uint8 */
    if (!write_u8(fp, (uint8_t)cfg->default_format)) return false;

    /* DefaultZoom: float32 */
    if (!write_float(fp, cfg->default_zoom)) return false;

    /* EncodingLen: uint8 + Encoding string */
    size_t enc_len = strlen(cfg->default_encoding);
    if (enc_len > 255) enc_len = 255;  /* 长度限制1字节 */
    if (!write_u8(fp, (uint8_t)enc_len)) return false;
    if (enc_len > 0) {
        if (!write_bytes(fp, cfg->default_encoding, enc_len)) return false;
    }

    return true;
}

/* 计算配置块的大小（在写入前需要知道）。                          */
static int config_block_size(const TreeConfig* cfg) {
    /* CanvasColor(4) + DefaultWidth(2) + DefaultHeight(2) +
     * DefaultFormat(1) + DefaultZoom(4) + EncodingLen(1) + Encoding */
    size_t enc_len = strlen(cfg->default_encoding);
    if (enc_len > 255) enc_len = 255;
    return (int)(4 + 2 + 2 + 1 + 4 + 1 + enc_len);
}

/* 递归序列化单个节点及其所有子节点（前序遍历）。
 * 这是 .lxmm 格式的核心写入逻辑。
 *
 * 写入流程：
 *   1. 构建 flags 字节（根据节点元数据）
 *   2. 写入 flags
 *   3. 写入 child_count
 *   4. 如果 has_content，写入 content_len 和 content 文本
 *   5. 如果 has_custom_color，写入颜色
 *   6. 如果 has_custom_size，写入宽高
 *   7. 如果 has_content，写入 format_type
 *   8. 递归序列化所有子节点                                      */
static bool serialize_node(FILE* fp, TreeNode* node) {
    if (node == NULL) return true;  /* 防御性：NULL 节点跳过 */

    /* 构建 flags 字节 */
    uint8_t flags = 0;
    if (node->expanded)         flags |= LXMM_FLAG_EXPANDED;
    if (node->content != NULL &&
        node->content[0] != '\0') flags |= LXMM_FLAG_HAS_CONTENT;
    if (node->custom_color != 0) flags |= LXMM_FLAG_HAS_CUSTOM_COLOR;
    if (node->custom_width != 0 ||
        node->custom_height != 0) flags |= LXMM_FLAG_HAS_CUSTOM_SIZE;

    /* 写入 flags */
    if (!write_u8(fp, flags)) return false;

    /* 写入 child_count */
    uint16_t cc = (uint16_t)((node->child_count > 65535)
                   ? 65535 : node->child_count);
    if (!write_u16(fp, cc)) return false;

    /* 写入 content（如果有） */
    if (flags & LXMM_FLAG_HAS_CONTENT) {
        size_t clen = strlen(node->content);
        if (clen > 65535) clen = 65535;
        if (!write_u16(fp, (uint16_t)clen)) return false;
        if (clen > 0) {
            if (!write_bytes(fp, node->content, clen)) return false;
        }

        /* 写入 format_type（内容存在时才写） */
        if (!write_u8(fp, (uint8_t)node->format_type)) return false;
    } else {
        /* 无内容：content_len = 0 */
        if (!write_u16(fp, 0)) return false;
    }

    /* 写入 custom_color（如果有） */
    if (flags & LXMM_FLAG_HAS_CUSTOM_COLOR) {
        if (!write_u32(fp, node->custom_color)) return false;
    }

    /* 写入 custom_size（如果有） */
    if (flags & LXMM_FLAG_HAS_CUSTOM_SIZE) {
        uint16_t cw = (uint16_t)((node->custom_width > 65535)
                       ? 65535 : node->custom_width);
        uint16_t ch = (uint16_t)((node->custom_height > 65535)
                       ? 65535 : node->custom_height);
        if (!write_u16(fp, cw)) return false;
        if (!write_u16(fp, ch)) return false;
    }

    /* 递归序列化所有子节点（前序遍历） */
    for (int i = 0; i < node->child_count; i++) {
        if (!serialize_node(fp, node->children[i])) return false;
    }

    return true;
}

/* ================================================================
 * Deserialization — .lxmm file → Tree  反序列化 — .lxmm 文件 → Tree
 * ================================================================ */

/* 前向声明 */
static TreeNode* deserialize_node(FILE* fp, HandlerStatus* status);

/* 读取并验证文件头。                                               */
static bool read_header(FILE* fp, uint32_t* node_count,
                         uint32_t* config_off, uint32_t* root_off,
                         HandlerStatus* status) {
    /* 验证魔数 */
    uint8_t magic[4];
    if (!read_bytes(fp, magic, 4)) {
        status->result_code = RESULT_ERROR_INVALID_FORMAT;
        handler_status_add_warning(status, RESULT_ERROR_INVALID_FORMAT,
            "Failed to read file header magic");
        return false;
    }
    if (magic[0] != 'L' || magic[1] != 'X' ||
        magic[2] != 'M' || magic[3] != 'M') {
        status->result_code = RESULT_ERROR_INVALID_FORMAT;
        handler_status_add_warning(status, RESULT_ERROR_INVALID_FORMAT,
            "Invalid LXMM magic: expected 'LXMM', got '%c%c%c%c'",
            magic[0], magic[1], magic[2], magic[3]);
        return false;
    }

    /* 版本号 */
    uint16_t version;
    if (!read_u16(fp, &version)) return false;
    if (version != LXMM_VERSION) {
        handler_status_add_warning(status, RESULT_WARN_CONTENT_TRUNC,
            "LXMM version mismatch: file=%d, supported=%d. "
            "Reading may produce incomplete results.",
            (int)version, LXMM_VERSION);
    }

    /* 标志（保留） */
    uint16_t flags;
    if (!read_u16(fp, &flags)) return false;
    (void)flags;  /* 忽略保留字段 */

    /* 节点数量、配置偏移、Root偏移 */
    if (!read_u32(fp, node_count)) return false;
    if (!read_u32(fp, config_off)) return false;
    if (!read_u32(fp, root_off)) return false;

    /* 跳过保留字节 */
    uint8_t reserved[12];
    if (!read_bytes(fp, reserved, 12)) return false;

    return true;
}

/* 读取配置块并填充 TreeConfig。                                    */
static bool read_config_block(FILE* fp, TreeConfig* cfg,
                               HandlerStatus* status) {
    uint32_t canvas_color;
    uint16_t dw, dh;
    uint8_t  fmt, enc_len;

    if (!read_u32(fp, &canvas_color)) return false;
    cfg->canvas_color = canvas_color;

    if (!read_u16(fp, &dw)) return false;
    cfg->default_width = (int)dw;

    if (!read_u16(fp, &dh)) return false;
    cfg->default_height = (int)dh;

    if (!read_u8(fp, &fmt)) return false;
    cfg->default_format = (NodeFormatType)fmt;

    if (!read_float(fp, &cfg->default_zoom)) return false;

    if (!read_u8(fp, &enc_len)) return false;
    if (enc_len > 0) {
        char enc_buf[256];
        if (!read_bytes(fp, enc_buf, enc_len)) return false;
        enc_buf[enc_len] = '\0';
        /* 安全复制到 cfg */
        size_t copy_len = (enc_len < 31) ? enc_len : 31;
        memcpy(cfg->default_encoding, enc_buf, copy_len);
        cfg->default_encoding[copy_len] = '\0';
    } else {
        cfg->default_encoding[0] = '\0';
    }

    (void)status;  /* 配置读取通常不产生警告 */
    return true;
}

/* 递归读取并重建单个节点及其所有子节点。
 * 这是 .lxmm 格式的核心读取逻辑。
 *
 * 读取流程：
 *   1. 读取 flags 字节
 *   2. 读取 child_count
 *   3. 读取 content_len，如果 has_content，读取 content 文本
 *   4. 如果 has_content，读取 format_type
 *   5. 如果 has_custom_color，读取颜色
 *   6. 如果 has_custom_size，读取宽高
 *   7. 设置节点元数据
 *   8. 递归读取 child_count 个子节点                         */
static TreeNode* deserialize_node(FILE* fp, HandlerStatus* status) {
    uint8_t flags;
    if (!read_u8(fp, &flags)) {
        handler_status_add_warning(status, RESULT_ERROR_PARSE,
            "Failed to read node flags");
        return NULL;
    }

    /* 解析标志位 */
    bool has_content      = (flags & LXMM_FLAG_HAS_CONTENT) != 0;
    bool has_custom_color = (flags & LXMM_FLAG_HAS_CUSTOM_COLOR) != 0;
    bool has_custom_size  = (flags & LXMM_FLAG_HAS_CUSTOM_SIZE) != 0;

    /* 读取 child_count */
    uint16_t child_count;
    if (!read_u16(fp, &child_count)) {
        handler_status_add_warning(status, RESULT_ERROR_PARSE,
            "Failed to read child count");
        return NULL;
    }

    /* 读取 content */
    char* content_str = NULL;
    uint16_t content_len;
    if (!read_u16(fp, &content_len)) {
        handler_status_add_warning(status, RESULT_ERROR_PARSE,
            "Failed to read content length");
        return NULL;
    }

    uint8_t format_byte = 0;
    if (has_content && content_len > 0) {
        content_str = (char*)mem_alloc(content_len + 1);
        if (content_len > 0) {
            if (!read_bytes(fp, content_str, content_len)) {
                free(content_str);
                handler_status_add_warning(status, RESULT_ERROR_PARSE,
                    "Failed to read node content (%d bytes)", content_len);
                return NULL;
            }
        }
        content_str[content_len] = '\0';

        /* 读取 format_type */
        if (!read_u8(fp, &format_byte)) {
            free(content_str);
            handler_status_add_warning(status, RESULT_ERROR_PARSE,
                "Failed to read format type");
            return NULL;
        }
    }

    /* 创建节点 */
    TreeNode* node = tree_node_create(content_str);
    free(content_str);  /* tree_node_create 已 strdup 副本 */

    /* 设置展开状态 */
    node->expanded = (flags & LXMM_FLAG_EXPANDED) != 0;

    /* 设置格式类型 */
    node->format_type = (NodeFormatType)format_byte;

    /* 读取自定义颜色（如果有） */
    if (has_custom_color) {
        uint32_t color;
        if (!read_u32(fp, &color)) {
            tree_node_free_subtree(node);
            handler_status_add_warning(status, RESULT_ERROR_PARSE,
                "Failed to read custom color");
            return NULL;
        }
        node->custom_color = color;
    }

    /* 读取自定义尺寸（如果有） */
    if (has_custom_size) {
        uint16_t cw, ch;
        if (!read_u16(fp, &cw)) {
            tree_node_free_subtree(node);
            handler_status_add_warning(status, RESULT_ERROR_PARSE,
                "Failed to read custom width");
            return NULL;
        }
        if (!read_u16(fp, &ch)) {
            tree_node_free_subtree(node);
            handler_status_add_warning(status, RESULT_ERROR_PARSE,
                "Failed to read custom height");
            return NULL;
        }
        node->custom_width  = (int)cw;
        node->custom_height = (int)ch;
    }

    /* 递归读取子节点 */
    for (uint16_t i = 0; i < child_count; i++) {
        TreeNode* child = deserialize_node(fp, status);
        if (child == NULL) {
            /* 子节点读取失败 —— 释放当前节点并返回 NULL */
            tree_node_free_subtree(node);
            return NULL;
        }
        /* 将子节点添加到当前节点 */
        if (node->child_count >= node->child_capacity) {
            int new_cap = (node->child_capacity == 0)
                           ? 4 : node->child_capacity * 2;
            node->children = (TreeNode**)mem_realloc(
                node->children, new_cap * sizeof(TreeNode*));
            for (int j = node->child_count; j < new_cap; j++) {
                node->children[j] = NULL;
            }
            node->child_capacity = new_cap;
        }
        child->parent = node;
        node->children[node->child_count] = child;
        node->child_count++;
    }

    return node;
}

/* ================================================================
 * Public API Implementation  公开 API 实现
 * ================================================================ */

/* 将 .lxmm 文件解析为 Tree。
 *
 * 读取流程：
 *   1. 打开文件（Unicode 安全路径）
 *   2. 读取并验证文件头
 *   3. Seek 到配置偏移，读取配置块
 *   4. Seek 到 Root 偏移，递归读取节点树
 *   5. 计算树深度和元数据                                       */
HandlerStatus lxmm_parse(const char* filepath, struct Tree* tree,
                          void* options,
                          UserQueryCallback query_cb, void* cb_data) {
    HandlerStatus status;
    handler_status_init(&status);
    (void)options;
    (void)query_cb;
    (void)cb_data;

    if (filepath == NULL || tree == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* 打开文件 */
    FILE* fp = lxmm_fopen_read(filepath);
    if (fp == NULL) {
        status.result_code = RESULT_ERROR_FILE_OPEN;
        handler_status_add_warning(&status, RESULT_ERROR_FILE_OPEN,
            "Could not open file: %s", filepath);
        return status;
    }

    /* 读取文件头 */
    uint32_t node_count, config_off, root_off;
    if (!read_header(fp, &node_count, &config_off, &root_off, &status)) {
        fclose(fp);
        return status;
    }

    /* 读取配置块 */
    if (fseek(fp, (long)config_off, SEEK_SET) != 0) {
        status.result_code = RESULT_ERROR_FILE_READ;
        handler_status_add_warning(&status, RESULT_ERROR_FILE_READ,
            "Could not seek to config block at offset %u", config_off);
        fclose(fp);
        return status;
    }
    if (!read_config_block(fp, &tree->config, &status)) {
        status.result_code = RESULT_ERROR_PARSE;
        fclose(fp);
        return status;
    }

    /* 读取 Root 节点树 */
    if (fseek(fp, (long)root_off, SEEK_SET) != 0) {
        status.result_code = RESULT_ERROR_FILE_READ;
        handler_status_add_warning(&status, RESULT_ERROR_FILE_READ,
            "Could not seek to root node at offset %u", root_off);
        fclose(fp);
        return status;
    }

    /* 释放默认的 root，使用从文件读取的 root */
    TreeNode* parsed_root = deserialize_node(fp, &status);
    fclose(fp);

    if (parsed_root == NULL) {
        status.result_code = RESULT_ERROR_PARSE;
        handler_status_add_warning(&status, RESULT_ERROR_PARSE,
            "Failed to read root node tree");
        return status;
    }

    /* 替换 tree 的 root */
    tree_node_free_subtree(tree->root);
    tree->root = parsed_root;
    parsed_root->parent = NULL;
    parsed_root->depth = 0;

    /* 更新元数据 */
    tree->source_file   = str_dup(filepath);  /* utils.h: 安全 strdup */
    tree->source_format = str_dup("lxmm");
    tree_recalculate_depths(tree);
    tree->total_nodes = tree_count_nodes(tree);

    /* 验证节点计数 */
    if (tree->total_nodes != (int)node_count && node_count > 0) {
        handler_status_add_warning(&status, RESULT_WARN_CONTENT_TRUNC,
            "Node count mismatch: header says %u, parsed %d nodes",
            node_count, tree->total_nodes);
    }

    return status;
}

/* 将 Tree 序列化为 .lxmm 文件。
 *
 * 写入流程：
 *   1. 计算偏移量
 *   2. 写入文件头（NodeCount、ConfigOff、RootOff 先写占位值）
 *   3. 写入配置块
 *   4. 写入 root 节点树（前序遍历）
 *   5. Seek 回文件头，回填实际的 NodeCount、ConfigOff、RootOff   */
HandlerStatus lxmm_serialize(const char* filepath,
                              const struct Tree* tree,
                              void* options) {
    HandlerStatus status;
    handler_status_init(&status);
    (void)options;

    if (filepath == NULL || tree == NULL || tree->root == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        return status;
    }

    /* 打开文件 */
    FILE* fp = lxmm_fopen_write(filepath);
    if (fp == NULL) {
        status.result_code = RESULT_ERROR_FILE_WRITE;
        handler_status_add_warning(&status, RESULT_ERROR_FILE_WRITE,
            "Could not open file for writing: %s", filepath);
        return status;
    }

    /* 计算偏移量和计数 */
    uint32_t node_count = (uint32_t)tree->total_nodes;
    uint32_t config_off = LXMM_HEADER_SIZE;  /* 配置块紧接头之后 */
    int cfg_size = config_block_size(&tree->config);
    uint32_t root_off = LXMM_HEADER_SIZE + (uint32_t)cfg_size;

    /* 写入文件头 */
    if (!write_header(fp, node_count, config_off, root_off)) {
        status.result_code = RESULT_ERROR_FILE_WRITE;
        fclose(fp);
        return status;
    }

    /* 写入配置块 */
    if (!write_config_block(fp, &tree->config)) {
        status.result_code = RESULT_ERROR_FILE_WRITE;
        fclose(fp);
        return status;
    }

    /* 递归写入 root 节点树（root 的每个子节点作为顶级节点） */
    for (int i = 0; i < tree->root->child_count; i++) {
        if (!serialize_node(fp, tree->root->children[i])) {
            status.result_code = RESULT_ERROR_FILE_WRITE;
            handler_status_add_warning(&status, RESULT_ERROR_FILE_WRITE,
                "Failed to serialize node tree");
            fclose(fp);
            return status;
        }
    }

    fclose(fp);
    return status;
}

/* 检测 .lxmm 文件：先检查扩展名，再检查魔数。
 * 如果文件存在但魔数不匹配，返回 false（不是有效的 .lxmm 文件）。
 * 优先级：扩展名匹配 AND 魔数匹配 → true。                       */
bool lxmm_detect(const char* filepath) {
    if (filepath == NULL) return false;

    /* 检查扩展名 ".lxmm"（不区分大小写） */
    const char* dot = strrchr(filepath, '.');
    if (dot == NULL) return false;
    const char* ext = dot, *exp = ".lxmm";
    while (*ext && *exp) {
        char e = *ext, x = *exp;
        if (e >= 'A' && e <= 'Z') e += (char)('a' - 'A');
        if (x >= 'A' && x <= 'Z') x += (char)('a' - 'A');
        if (e != x) return false;
        ext++; exp++;
    }
    if (*ext != '\0' || *exp != '\0') return false;

    /* 扩展名匹配，检查魔数（如果文件存在） */
    FILE* fp = lxmm_fopen_read(filepath);
    if (fp == NULL) {
        /* 文件还不存在（例如新建文件时）—— 仅凭扩展名判断 */
        return true;
    }

    uint8_t magic[4];
    bool ok = read_bytes(fp, magic, 4);
    fclose(fp);

    if (!ok) return true;  /* 空文件：凭扩展名接受 */
    return (magic[0] == 'L' && magic[1] == 'X' &&
            magic[2] == 'M' && magic[3] == 'M');
}

/* 创建默认 LXMM 选项（当前为最小占位符）。                       */
void* lxmm_create_default_options(void) {
    LxmmOptions* opts = (LxmmOptions*)mem_alloc(sizeof(LxmmOptions));
    opts->dummy = false;
    return opts;
}

/* 释放 LXMM 选项。                                               */
void lxmm_free_options(void* options) {
    if (options != NULL) free(options);
}

/* ================================================================
 * Handler Singleton Definition  处理器单例定义
 * ================================================================ */

/* LXMM 格式处理器单例。在 main.c 中通过 format_register 注册。
 * 这是 Phase 2 第一个新格式处理器，标志着程序从"格式转换工具"
 * 进化为"思维导图编辑器"。                                      */
const FormatHandler LXMM_HANDLER = {
    .format_name            = "lxmm",
    .extension              = ".lxmm",
    .description            = "LXMM (Mind Map Binary Format)",
    .parse                  = lxmm_parse,
    .serialize              = lxmm_serialize,
    .detect                 = lxmm_detect,
    .create_default_options = lxmm_create_default_options,
    .free_options           = lxmm_free_options
};
