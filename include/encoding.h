/* ============================================================
 * encoding.h - Character Encoding Detection and Conversion
 * encoding.h - 字符编码检测与转换
 *
 * Handles reading and writing text files with proper encoding
 * detection and conversion. Uses Windows MultiByteToWideChar
 * and WideCharToMultiByte API for reliable conversion between
 * UTF-8, UTF-16, and GBK (Chinese Windows code page).
 * 处理文本文件的正确编码检测与转换读写。使用 Windows
 * MultiByteToWideChar 和 WideCharToMultiByte API 在 UTF-8、
 * UTF-16 和 GBK 之间进行可靠转换。
 *
 * This module is Windows-only (uses Win32 API for encoding
 * conversion). 本模块仅限 Windows（使用 Win32 API 进行编码转换）。
 * ============================================================ */

#ifndef ENCODING_H
#define ENCODING_H

#include <stdbool.h>   /* bool, true, false */

/* ================================================================
 * Encoding Types 编码类型
 * ================================================================ */

/* Supported encoding types detected from file headers (BOM)
 * or heuristics. 通过文件头（BOM）或启发式方法检测到的编码类型。
 *
 * ENC_UTF8:     UTF-8 without BOM (assumed default on non-Windows)
 *               不带 BOM 的 UTF-8（非 Windows 的默认假设）
 * ENC_UTF8_BOM: UTF-8 with BOM (bytes: EF BB BF)
 *               带 BOM 的 UTF-8（字节：EF BB BF）
 * ENC_UTF16_LE: UTF-16 Little Endian (BOM: FF FE)
 *               UTF-16 小端序（BOM：FF FE）
 * ENC_UTF16_BE: UTF-16 Big Endian (BOM: FE FF)
 *               UTF-16 大端序（BOM：FE FF）
 * ENC_GBK:      Chinese GBK/GB2312 (detected heuristically on
 *               Chinese Windows when no BOM is present)
 *               中文 GBK/GB2312（中文 Windows 无 BOM 时启发式检测）
 * ENC_UNKNOWN:  Could not determine encoding
 *               无法确定编码                                        */
typedef enum {
    ENC_UTF8      = 0,  /* UTF-8 without BOM */
    ENC_UTF8_BOM  = 1,  /* UTF-8 with BOM (EF BB BF) */
    ENC_UTF16_LE  = 2,  /* UTF-16 Little Endian (FF FE BOM) */
    ENC_UTF16_BE  = 3,  /* UTF-16 Big Endian (FE FF BOM) */
    ENC_GBK       = 4,  /* Chinese GBK/GB2312 code page 936 */
    ENC_UNKNOWN   = 5   /* Unknown / could not detect */
} EncodingType;

/* ================================================================
 * Encoding Detection 编码检测
 * ================================================================ */

/* Detect the encoding of a file by reading its first few bytes.
 * 通过读取文件的前几个字节来检测编码。
 * Uses BOM (Byte Order Mark) detection for UTF-8/UTF-16.
 * Falls back to heuristics (GBK on Chinese locale systems).
 * 使用 BOM（字节序标记）检测 UTF-8/UTF-16。
 * 回退到启发式方法（中文系统上检测 GBK）。
 * Parameters 参数:
 *   filepath - Path to the file to analyze 要分析的文件路径
 * Returns: detected encoding type. 返回值：检测到的编码类型。       */
EncodingType encoding_detect(const char* filepath);

/* ================================================================
 * Encoding Conversion 编码转换
 * ================================================================ */

/* Read an entire file into memory as a UTF-8 string.
 * 将整个文件读入内存并作为 UTF-8 字符串返回。
 * Handles encoding detection and conversion automatically.
 * 自动处理编码检测和转换。
 * Parameters 参数:
 *   filepath     - Path to the input file 输入文件路径
 *   out_encoding - [out, optional] Receives the detected encoding
 *                  [输出, 可选] 接收检测到的编码类型
 * Returns: heap-allocated UTF-8 string (caller must free()),
 *          or NULL on error. 返回值：堆分配的 UTF-8 字符串
 *         （调用者必须 free()），出错返回 NULL。                    */
char* encoding_read_file_utf8(const char* filepath,
                               EncodingType* out_encoding);

/* Write a UTF-8 string to a file with the specified encoding.
 * 将 UTF-8 字符串以指定编码写入文件。
 * Parameters 参数:
 *   filepath      - Path to the output file 输出文件路径
 *   utf8_content  - The UTF-8 text to write 要写入的 UTF-8 文本
 *   target_enc    - Desired output encoding 目标编码
 *                   (use ENC_UTF8_BOM for UTF-8 with BOM)
 *                   （对于带 BOM 的 UTF-8 使用 ENC_UTF8_BOM）
 * Returns: true on success, false on error.                        */
bool encoding_write_file_utf8(const char* filepath,
                               const char* utf8_content,
                               EncodingType target_enc);

/* Get a human-readable name for an encoding type.
 * 获取编码类型的人类可读名称。
 * Example: ENC_UTF8_BOM -> "UTF-8 with BOM"
 * Returns: static string (do not free). 返回静态字符串（不要释放）。*/
const char* encoding_type_name(EncodingType enc);

#endif /* ENCODING_H */
