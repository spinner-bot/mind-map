/* ============================================================
 * encoding.c - Encoding Detection and Conversion Implementation
 * encoding.c - 编码检测与转换实现
 *
 * Uses Windows MultiByteToWideChar / WideCharToMultiByte APIs
 * for reliable encoding conversion between UTF-8, UTF-16, GBK.
 * 使用 Windows MultiByteToWideChar / WideCharToMultiByte API
 * 在 UTF-8、UTF-16、GBK 之间进行可靠转换。
 *
 * Encoding strategy 编码策略:
 *   - Internal: always UTF-8 (universal intermediate)
 *     内部：始终使用 UTF-8（通用中间格式）
 *   - Input: detect BOM/encoding, convert to UTF-8
 *     输入：检测 BOM/编码，转换为 UTF-8
 *   - Output: UTF-8 with BOM (for broad compatibility)
 *     输出：带 BOM 的 UTF-8（广泛兼容性）
 * ============================================================ */

#include "encoding.h"
#include "utils.h"     /* mem_alloc, path_exists, path_is_file */

#include <stdlib.h>    /* free, NULL */
#include <stdio.h>     /* FILE, fopen, fread, fclose, fwrite, SEEK_END */
#include <string.h>    /* strlen, memcmp */

/* Windows API headers required for encoding conversion.
 * 编码转换所需的 Windows API 头文件。                             */
#ifdef _WIN32
#include <windows.h>   /* MultiByteToWideChar, WideCharToMultiByte,
                          CP_UTF8, CP_ACP, GetACP */
#endif

/* ================================================================
 * Internal helpers 内部辅助函数
 * ================================================================ */

/* Read entire file into a raw byte buffer.
 * 将整个文件读入原始字节缓冲区。
 * Parameters 参数:
 *   filepath - Input file path 输入文件路径
 *   out_size - [out] Receives file size in bytes
 *              [输出] 接收文件大小（字节）
 * Returns: heap-allocated byte buffer, or NULL on error.
 * 返回值：堆分配的字节缓冲区，出错返回 NULL。
 * Caller must free() the buffer. 调用者必须 free() 缓冲区。       */
static unsigned char* read_file_bytes(const char* filepath,
                                       long* out_size) {
    if (filepath == NULL || out_size == NULL) {
        return NULL;
    }

    /* 以二进制模式打开文件 */
    FILE* fp = fopen(filepath, "rb");
    if (fp == NULL) {
        return NULL;
    }

    /* 获取文件大小 */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    /* 分配缓冲区并读取 */
    unsigned char* buffer = (unsigned char*)mem_alloc(
        (size_t)(file_size + 1));  /* +1 用于终止符 */
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    /* 添加 NUL 终止符（作为安全措施，即使对二进制数据也是如此） */
    buffer[file_size] = '\0';
    *out_size = file_size;
    return buffer;
}

/* ================================================================
 * Encoding Detection 编码检测
 * ================================================================ */

/* BOM (Byte Order Mark) signature definitions.
 * BOM（字节序标记）签名定义。                                    */

/* UTF-8 BOM: EF BB BF */
static const unsigned char BOM_UTF8[]    = { 0xEF, 0xBB, 0xBF };
#define BOM_UTF8_LEN 3

/* UTF-16 LE BOM: FF FE */
static const unsigned char BOM_UTF16_LE[] = { 0xFF, 0xFE };
#define BOM_UTF16_LE_LEN 2

/* UTF-16 BE BOM: FE FF */
static const unsigned char BOM_UTF16_BE[] = { 0xFE, 0xFF };
#define BOM_UTF16_BE_LEN 2

/* 检测文件的字符编码。
 * 检测策略（按优先级）：
 *   1. 读取文件前 4 字节检查 BOM
 *   2. UTF-16 LE BOM (FF FE) → UTF-16 LE
 *   3. UTF-16 BE BOM (FE FF) → UTF-16 BE
 *   4. UTF-8 BOM (EF BB BF) → UTF-8 with BOM
 *   5. 无 BOM 但文件内容以 UTF-8 序列开头 → UTF-8
 *   6. 否则检测是否为 GBK（通过验证字节序列是否符合 GBK 编码规则）
 *   7. 回退到 UTF-8（最安全的假设）                             */
EncodingType encoding_detect(const char* filepath) {
    if (filepath == NULL) {
        return ENC_UNKNOWN;
    }

    /* 检查文件是否存在 */
    if (!path_exists(filepath)) {
        return ENC_UNKNOWN;
    }

    /* 读取文件前 4 字节用于 BOM 检测 */
    FILE* fp = fopen(filepath, "rb");
    if (fp == NULL) {
        return ENC_UNKNOWN;
    }

    unsigned char header[4] = { 0 };
    size_t bytes_read = fread(header, 1, sizeof(header), fp);
    fclose(fp);

    if (bytes_read == 0) {
        /* 空文件 —— 默认假设 UTF-8 */
        return ENC_UTF8;
    }

    /* 步 1：检查 UTF-16 LE BOM */
    if (bytes_read >= 2 &&
        memcmp(header, BOM_UTF16_LE, BOM_UTF16_LE_LEN) == 0) {
        return ENC_UTF16_LE;
    }

    /* 步 2：检查 UTF-16 BE BOM */
    if (bytes_read >= 2 &&
        memcmp(header, BOM_UTF16_BE, BOM_UTF16_BE_LEN) == 0) {
        return ENC_UTF16_BE;
    }

    /* 步 3：检查 UTF-8 BOM */
    if (bytes_read >= 3 &&
        memcmp(header, BOM_UTF8, BOM_UTF8_LEN) == 0) {
        return ENC_UTF8_BOM;
    }

    /* 步 4：无 BOM —— 尝试通过字节序列启发式检测。
     * UTF-8 有严格的字节序列规则：
     *   - ASCII (0x00-0x7F): 单字节
     *   - 2字节序列: 110xxxxx 10xxxxxx
     *   - 3字节序列: 1110xxxx 10xxxxxx 10xxxxxx
     *   - 4字节序列: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
     * 如果文件内容不符合 UTF-8 规则，可能是 GBK（中文 Windows 常见）。*/
    size_t check_len = bytes_read;
    bool is_valid_utf8 = true;
    int continuation_bytes = 0;  /* 预期的后续字节数 */

    for (size_t i = 0; i < check_len; i++) {
        unsigned char c = header[i];

        if (continuation_bytes > 0) {
            /* 后续字节必须以 10xxxxxx 开头 */
            if ((c & 0xC0) != 0x80) {
                is_valid_utf8 = false;
                break;
            }
            continuation_bytes--;
        } else {
            if (c <= 0x7F) {
                /* ASCII —— 单字节，无需后续字节 */
                continuation_bytes = 0;
            } else if ((c & 0xE0) == 0xC0) {
                /* 110xxxxx —— 2字节序列 */
                continuation_bytes = 1;
            } else if ((c & 0xF0) == 0xE0) {
                /* 1110xxxx —— 3字节序列 */
                continuation_bytes = 2;
            } else if ((c & 0xF8) == 0xF0) {
                /* 11110xxx —— 4字节序列 */
                continuation_bytes = 3;
            } else {
                /* 无效的起始字节 —— 不是 UTF-8 */
                is_valid_utf8 = false;
                break;
            }
        }
    }

    /* 如果所有后续字节都已正确匹配，则是合法的 UTF-8 */
    if (is_valid_utf8 && continuation_bytes == 0) {
        return ENC_UTF8;
    }

    /* 步 5：不是合法的 UTF-8 —— 检查 Windows 代码页。
     * 在中文 Windows (代码页 936 = GBK) 上，GetACP() 返回 936。
     * 我们假设非 UTF-8 文件使用系统默认代码页（通常是 GBK）。
     * 注意：这是一种启发式方法，对于纯 ASCII 文件会错误识别为 GBK，
     * 但 ASCII 同时也是合法的 UTF-8，所以前面已被检测为 UTF-8。  */
#ifdef _WIN32
    /* 获取系统默认 ANSI 代码页 */
    UINT acp = GetACP();
    if (acp == 936 || acp == 54936) {
        /* 936 = GBK, 54936 = GB18030 */
        return ENC_GBK;
    }
#endif

    /* 最后的回退：假设 UTF-8。
     * 这通常是最安全的默认值，因为大多数现代文本文件都是 UTF-8。*/
    return ENC_UTF8;
}

/* ================================================================
 * Encoding Conversion (using Windows API) 编码转换（使用 Windows API）
 * ================================================================ */

#ifdef _WIN32

/* 将源编码的字节字符串转换为 UTF-8。
 * 内部使用 Windows API：源编码 → UTF-16 (WideChar) → UTF-8。
 * 两步转换虽然略显间接，但 Windows API 对 WideChar 的支持最完善。
 *
 * 参数：
 *   src_data - 源字节数据
 *   src_size - 源数据大小（字节）
 *   src_enc  - 源编码类型
 * 返回值：堆分配的 UTF-8 字符串，或 NULL。                       */
static char* convert_to_utf8(const unsigned char* src_data,
                              long src_size, EncodingType src_enc) {
    if (src_data == NULL || src_size <= 0) {
        /* 空数据：返回空字符串 */
        char* empty = (char*)mem_alloc(1);
        empty[0] = '\0';
        return empty;
    }

    /* 步 1：确定 Windows 代码页。
     * Windows 原生支持：CP_UTF8 (65001)、GBK (936) 等。
     * 对于 UTF-16 LE/BE，需要手动处理 BOM。                      */
    UINT src_codepage;
    const unsigned char* actual_data = src_data;
    long actual_size = src_size;

    switch (src_enc) {
        case ENC_UTF8:
        case ENC_UTF8_BOM:
            /* UTF-8 输入：如果已是 UTF-8 且我们想要 UTF-8，
             * 直接复制即可（跳过可选的 BOM）。                  */
            if (src_enc == ENC_UTF8_BOM && src_size >= 3 &&
                memcmp(src_data, BOM_UTF8, BOM_UTF8_LEN) == 0) {
                /* 跳过 BOM */
                actual_data = src_data + BOM_UTF8_LEN;
                actual_size = src_size - BOM_UTF8_LEN;
            }
            /* 直接返回 UTF-8 副本 */
            char* result = (char*)mem_alloc((size_t)(actual_size + 1));
            memcpy(result, actual_data, (size_t)actual_size);
            result[actual_size] = '\0';
            return result;

        case ENC_UTF16_LE:
            /* UTF-16 LE with BOM: 跳过 BOM，使用 UTF-16 LE 解码 */
            if (actual_size >= 2 &&
                memcmp(actual_data, BOM_UTF16_LE, 2) == 0) {
                actual_data += 2;
                actual_size -= 2;
            }
            /* 将 UTF-16 LE 转换为 wide string，再转为 UTF-8 */
            {
                int wchar_count = MultiByteToWideChar(
                    CP_UTF8,  /* 当源是 UTF-16 LE 时，使用宽字符直接读取 */
                    0,        /* 在 UTF-8 codepage 下不支持原始读取，改用下面的方式 */
                    (LPCCH)actual_data, (int)actual_size,
                    NULL, 0);
                /* 实际上 MultiByteToWideChar 不直接支持 UTF-16。
                 * 替代方法：直接以 WCHAR* 方式解释字节。          */
                /* 简单实现：将 UTF-16 LE 字节转换为 WCHAR 字符串 */
                int wchars = (int)(actual_size / 2);
                WCHAR* wide_str = (WCHAR*)mem_alloc(
                    (wchars + 1) * sizeof(WCHAR));
                /* 直接复制字节，因为 Windows 内部使用 UTF-16 LE */
                memcpy(wide_str, actual_data, (size_t)actual_size);
                wide_str[wchars] = L'\0';

                /* 将 wide string 转换为 UTF-8 */
                int utf8_len = WideCharToMultiByte(
                    CP_UTF8, 0, wide_str, wchars,
                    NULL, 0, NULL, NULL);
                char* utf8_str = (char*)mem_alloc((size_t)(utf8_len + 1));
                WideCharToMultiByte(
                    CP_UTF8, 0, wide_str, wchars,
                    utf8_str, utf8_len, NULL, NULL);
                utf8_str[utf8_len] = '\0';

                free(wide_str);
                return utf8_str;
            }

        case ENC_UTF16_BE:
            /* UTF-16 BE: 跳过 BOM，交换字节序转换为 LE */
            if (actual_size >= 2 &&
                memcmp(actual_data, BOM_UTF16_BE, 2) == 0) {
                actual_data += 2;
                actual_size -= 2;
            }
            {
                /* 将 UTF-16 BE 转换为 WCHAR（LE） */
                int wchars = (int)(actual_size / 2);
                WCHAR* wide_str = (WCHAR*)mem_alloc(
                    (wchars + 1) * sizeof(WCHAR));
                /* 逐字符交换字节序 */
                for (int i = 0; i < wchars; i++) {
                    wide_str[i] = (WCHAR)(
                        (actual_data[i * 2] << 8) |
                         actual_data[i * 2 + 1]);
                }
                wide_str[wchars] = L'\0';

                /* 转换为 UTF-8 */
                int utf8_len = WideCharToMultiByte(
                    CP_UTF8, 0, wide_str, wchars,
                    NULL, 0, NULL, NULL);
                char* utf8_str = (char*)mem_alloc((size_t)(utf8_len + 1));
                WideCharToMultiByte(
                    CP_UTF8, 0, wide_str, wchars,
                    utf8_str, utf8_len, NULL, NULL);
                utf8_str[utf8_len] = '\0';

                free(wide_str);
                return utf8_str;
            }

        case ENC_GBK:
            /* GBK (code page 936) → UTF-8 */
            src_codepage = 936;
            goto convert_codepage;

        default:
        case ENC_UNKNOWN:
            /* 未知编码：当作 UTF-8 处理 */
            {
                char* result = (char*)mem_alloc(
                    (size_t)(actual_size + 1));
                memcpy(result, actual_data, (size_t)actual_size);
                result[actual_size] = '\0';
                return result;
            }
    }

convert_codepage:
    /* 通用代码页转换：CP → WideChar → UTF-8 */
    {
        /* 步 1：代码页 → UTF-16 (WideChar) */
        int wchar_count = MultiByteToWideChar(
            src_codepage, 0,
            (LPCCH)actual_data, (int)actual_size,
            NULL, 0);
        if (wchar_count == 0) {
            /* 转换失败：返回 UTF-8 原始副本 */
            char* result = (char*)mem_alloc((size_t)(actual_size + 1));
            memcpy(result, actual_data, (size_t)actual_size);
            result[actual_size] = '\0';
            return result;
        }

        WCHAR* wide_str = (WCHAR*)mem_alloc(
            (wchar_count + 1) * sizeof(WCHAR));
        MultiByteToWideChar(
            src_codepage, 0,
            (LPCCH)actual_data, (int)actual_size,
            wide_str, wchar_count);
        wide_str[wchar_count] = L'\0';

        /* 步 2：UTF-16 (WideChar) → UTF-8 */
        int utf8_len = WideCharToMultiByte(
            CP_UTF8, 0, wide_str, wchar_count,
            NULL, 0, NULL, NULL);
        char* utf8_str = (char*)mem_alloc((size_t)(utf8_len + 1));
        WideCharToMultiByte(
            CP_UTF8, 0, wide_str, wchar_count,
            utf8_str, utf8_len, NULL, NULL);
        utf8_str[utf8_len] = '\0';

        free(wide_str);
        return utf8_str;
    }
}

/* 将 UTF-8 字符串转换为目标编码的字节并写入文件。
 * 转换流程：
 *   1. UTF-8 → UTF-16 (WideChar)
 *   2. 根据需要添加 BOM
 *   3. UTF-16 → 目标编码（或直接写 UTF-8 字节）                */
static bool write_utf8_as_encoding(const char* utf8_str,
                                    EncodingType target_enc,
                                    FILE* fp) {
    if (utf8_str == NULL || fp == NULL) {
        return false;
    }

    size_t utf8_len = strlen(utf8_str);

    switch (target_enc) {
        case ENC_UTF8:
            /* 纯 UTF-8 无 BOM：直接写入 */
            if (fwrite(utf8_str, 1, utf8_len, fp) != utf8_len) {
                return false;
            }
            return true;

        case ENC_UTF8_BOM:
            /* UTF-8 with BOM: 先写 BOM，再写内容 */
            if (fwrite(BOM_UTF8, 1, BOM_UTF8_LEN, fp) != BOM_UTF8_LEN) {
                return false;
            }
            if (fwrite(utf8_str, 1, utf8_len, fp) != utf8_len) {
                return false;
            }
            return true;

        case ENC_UTF16_LE:
            /* UTF-16 LE with BOM */
            {
                /* 先写 BOM */
                if (fwrite(BOM_UTF16_LE, 1, 2, fp) != 2) {
                    return false;
                }
                /* UTF-8 → UTF-16 LE */
                int wchar_count = MultiByteToWideChar(
                    CP_UTF8, 0, utf8_str, (int)utf8_len,
                    NULL, 0);
                if (wchar_count == 0) {
                    return false;
                }
                WCHAR* wide_str = (WCHAR*)mem_alloc(
                    wchar_count * sizeof(WCHAR));
                MultiByteToWideChar(
                    CP_UTF8, 0, utf8_str, (int)utf8_len,
                    wide_str, wchar_count);
                /* 写入 UTF-16 LE 字节 */
                size_t byte_count = wchar_count * sizeof(WCHAR);
                if (fwrite(wide_str, 1, byte_count, fp) != byte_count) {
                    free(wide_str);
                    return false;
                }
                free(wide_str);
                return true;
            }

        case ENC_UTF16_BE:
            /* UTF-16 BE with BOM */
            {
                if (fwrite(BOM_UTF16_BE, 1, 2, fp) != 2) {
                    return false;
                }
                /* UTF-8 → UTF-16 LE → 交换字节序 → BE */
                int wchar_count = MultiByteToWideChar(
                    CP_UTF8, 0, utf8_str, (int)utf8_len,
                    NULL, 0);
                if (wchar_count == 0) {
                    return false;
                }
                WCHAR* wide_str = (WCHAR*)mem_alloc(
                    wchar_count * sizeof(WCHAR));
                MultiByteToWideChar(
                    CP_UTF8, 0, utf8_str, (int)utf8_len,
                    wide_str, wchar_count);
                /* 交换字节序并写入 */
                for (int i = 0; i < wchar_count; i++) {
                    unsigned char be[2];
                    be[0] = (unsigned char)(wide_str[i] >> 8);   /* 高字节 */
                    be[1] = (unsigned char)(wide_str[i] & 0xFF); /* 低字节 */
                    if (fwrite(be, 1, 2, fp) != 2) {
                        free(wide_str);
                        return false;
                    }
                }
                free(wide_str);
                return true;
            }

        case ENC_GBK:
            /* GBK: UTF-8 → WideChar → GBK (code page 936) */
            {
                int wchar_count = MultiByteToWideChar(
                    CP_UTF8, 0, utf8_str, (int)utf8_len,
                    NULL, 0);
                if (wchar_count == 0) {
                    /* 可能是纯 ASCII —— 直接写 */
                    if (fwrite(utf8_str, 1, utf8_len, fp) != utf8_len) {
                        return false;
                    }
                    return true;
                }
                WCHAR* wide_str = (WCHAR*)mem_alloc(
                    wchar_count * sizeof(WCHAR));
                MultiByteToWideChar(
                    CP_UTF8, 0, utf8_str, (int)utf8_len,
                    wide_str, wchar_count);

                int gbk_len = WideCharToMultiByte(
                    936, 0, wide_str, wchar_count,
                    NULL, 0, NULL, NULL);
                char* gbk_str = (char*)mem_alloc(gbk_len + 1);
                WideCharToMultiByte(
                    936, 0, wide_str, wchar_count,
                    gbk_str, gbk_len, NULL, NULL);

                bool ok = (fwrite(gbk_str, 1, gbk_len, fp) == (size_t)gbk_len);
                free(gbk_str);
                free(wide_str);
                return ok;
            }

        default:
            /* 未知目标编码：按 UTF-8 写入 */
            if (fwrite(utf8_str, 1, utf8_len, fp) != utf8_len) {
                return false;
            }
            return true;
    }
}

#else /* !_WIN32 */

/* 非 Windows 平台的简化实现。
 * 仅支持 UTF-8 编码的读写（不做转换）。
 * 这是因为编码转换依赖平台特定的 API。                           */

static char* convert_to_utf8(const unsigned char* src_data,
                              long src_size, EncodingType src_enc) {
    (void)src_enc;  /* 未使用 —— 在 POSIX 平台上简单假设 UTF-8 */

    if (src_data == NULL || src_size <= 0) {
        char* empty = (char*)mem_alloc(1);
        empty[0] = '\0';
        return empty;
    }

    /* 跳过 UTF-8 BOM（如果存在） */
    if (src_size >= 3 && memcmp(src_data, BOM_UTF8, BOM_UTF8_LEN) == 0) {
        src_data += BOM_UTF8_LEN;
        src_size -= BOM_UTF8_LEN;
    }

    char* result = (char*)mem_alloc((size_t)(src_size + 1));
    memcpy(result, src_data, (size_t)src_size);
    result[src_size] = '\0';
    return result;
}

static bool write_utf8_as_encoding(const char* utf8_str,
                                    EncodingType target_enc,
                                    FILE* fp) {
    if (utf8_str == NULL || fp == NULL) {
        return false;
    }

    size_t utf8_len = strlen(utf8_str);

    /* 如果需要，写入 BOM */
    if (target_enc == ENC_UTF8_BOM) {
        if (fwrite(BOM_UTF8, 1, BOM_UTF8_LEN, fp) != BOM_UTF8_LEN) {
            return false;
        }
    }

    /* 写入 UTF-8 内容 */
    if (fwrite(utf8_str, 1, utf8_len, fp) != utf8_len) {
        return false;
    }
    return true;
}

#endif /* _WIN32 */

/* ================================================================
 * Public API 公开 API
 * ================================================================ */

/* 将整个文件读入内存并以 UTF-8 字符串形式返回。
 * 自动检测编码并转换为 UTF-8。
 * 返回值：堆分配的 UTF-8 字符串（调用者负责 free()）。
 * 失败返回 NULL。                                               */
char* encoding_read_file_utf8(const char* filepath,
                               EncodingType* out_encoding) {
    if (filepath == NULL) {
        if (out_encoding != NULL) {
            *out_encoding = ENC_UNKNOWN;
        }
        return NULL;
    }

    /* 检测文件编码 */
    EncodingType enc = encoding_detect(filepath);
    if (out_encoding != NULL) {
        *out_encoding = enc;
    }

    if (enc == ENC_UNKNOWN) {
        /* 无法检测编码：当作 UTF-8 读取 */
        enc = ENC_UTF8;
    }

    /* 读取原始字节 */
    long file_size = 0;
    unsigned char* raw_bytes = read_file_bytes(filepath, &file_size);
    if (raw_bytes == NULL) {
        return NULL;
    }

    /* 转换为 UTF-8 */
    char* utf8_str = convert_to_utf8(raw_bytes, file_size, enc);
    free(raw_bytes);

    return utf8_str;
}

/* 将 UTF-8 字符串以指定编码写入文件。
 * 自动处理 BOM 写入和编码转换。
 * 返回值：成功返回 true，失败返回 false。                       */
bool encoding_write_file_utf8(const char* filepath,
                               const char* utf8_content,
                               EncodingType target_enc) {
    if (filepath == NULL || utf8_content == NULL) {
        return false;
    }

    /* 以二进制模式打开文件进行写入 */
    FILE* fp = fopen(filepath, "wb");
    if (fp == NULL) {
        return false;
    }

    bool ok = write_utf8_as_encoding(utf8_content, target_enc, fp);
    fclose(fp);

    if (!ok) {
        /* 写入失败：尝试删除不完整的输出文件 */
        remove(filepath);
    }

    return ok;
}

/* 获取编码类型的人类可读名称。
 * 用于日志输出和错误消息。                                    */
const char* encoding_type_name(EncodingType enc) {
    switch (enc) {
        case ENC_UTF8:      return "UTF-8";
        case ENC_UTF8_BOM:  return "UTF-8 with BOM";
        case ENC_UTF16_LE:  return "UTF-16 Little Endian";
        case ENC_UTF16_BE:  return "UTF-16 Big Endian";
        case ENC_GBK:       return "GBK (Chinese)";
        case ENC_UNKNOWN:
        default:            return "Unknown";
    }
}
