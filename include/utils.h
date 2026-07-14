/* ============================================================
 * utils.h - General Utility Functions
 * utils.h - 通用工具函数
 *
 * Provides string manipulation, path handling, and memory
 * management helpers used across the entire project.
 * 提供整个项目通用的字符串操作、路径处理和内存管理辅助函数。
 *
 * All functions in this module are stateless and thread-safe
 * (no global variables). 本模块所有函数都是无状态且线程安全的。
 * ============================================================ */

#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>   /* bool, true, false */
#include <stddef.h>    /* size_t */

/* ================================================================
 * String Operations 字符串操作
 * ================================================================ */

/* Trim leading and trailing whitespace from a string in-place.
 * 原地去除字符串首尾的空白字符。
 * Whitespace includes: space, tab, carriage return, newline.
 * 空白字符包括：空格、制表符、回车、换行。
 * Returns the same pointer (now pointing to the trimmed start).
 * 返回同一个指针（现在指向修剪后的起始位置）。
 * Example: "  hello \n" becomes "hello"                             */
char* str_trim(char* str);

/* Duplicate a string safely (NULL-safe wrapper around strdup).
 * 安全地复制字符串（strdup 的 NULL 安全封装）。
 * Returns NULL when input is NULL, otherwise a heap-allocated copy.
 * 输入为 NULL 时返回 NULL，否则返回堆分配的副本。
 * Caller must free() the returned string. 调用者必须 free() 返回值。*/
char* str_dup(const char* src);

/* Check if str starts with the given prefix.
 * 检查字符串是否以指定前缀开头。
 * Returns false if either argument is NULL. 任一参数为 NULL 返回 false。*/
bool str_startswith(const char* str, const char* prefix);

/* Check if str ends with the given suffix.
 * 检查字符串是否以指定后缀结尾。
 * Returns false if either argument is NULL. 任一参数为 NULL 返回 false。*/
bool str_endswith(const char* str, const char* suffix);

/* Count occurrences of a character in a string.
 * 统计字符串中某个字符的出现次数。
 * Returns 0 for NULL input. NULL 输入返回 0。                        */
int str_count_char(const char* str, char ch);

/* Split a string by a delimiter character.
 * 按分隔符拆分字符串。
 * Parameters 参数:
 *   str       - String to split (must not be NULL) 要拆分的字符串
 *   delimiter - Character to split on 分隔符
 *   out_count - [out] Receives number of parts 接收拆分后的片段数
 * Returns: heap-allocated array of heap-allocated strings.
 * 返回值：堆分配的字符串数组（每个元素也是堆分配的）。
 * Caller must free each part AND the array via str_split_free().
 * 调用者须通过 str_split_free() 释放每个片段和数组。
 * Leading/trailing delimiters produce empty strings.
 * 首尾的分隔符会产生空字符串。                                    */
char** str_split(const char* str, char delimiter, int* out_count);

/* Free the result of str_split(). 释放 str_split() 的结果。
 * parts: array from str_split() str_split() 返回的数组
 * count: number of parts 片段数量                                 */
void str_split_free(char** parts, int count);

/* ================================================================
 * Path Operations 路径操作
 * ================================================================ */

/* Extract file extension from a path (always lowercase).
 * 从路径中提取文件扩展名（始终为小写）。
 * ext buffer receives e.g. ".txt", ".json", ".md".
 * ext 缓冲区接收如 ".txt"、".json"、".md" 等。
 * If there is no extension, ext receives empty string.
 * 如果没有扩展名，ext 接收空字符串。
 * Returns: true on success, false on error (buffer too small).
 * 返回值：成功返回 true，错误返回 false（缓冲区太小）。            */
bool path_get_extension(const char* filepath, char* ext, int ext_size);

/* Change the file extension of a path.
 * 更改路径的文件扩展名。
 * Example: ("file.txt", ".json", ...) produces "file.json"
 * 示例：("file.txt", ".json", ...) 生成 "file.json"
 * If the original has no extension, the new one is appended.
 * 如果原始没有扩展名，则追加新的。
 * Returns: true on success, false on error.                        */
bool path_change_extension(const char* filepath, const char* new_ext,
                           char* out_path, int out_size);

/* Extract the directory portion of a file path.
 * 提取文件路径中的目录部分。
 * Example: "C:\dir\file.txt" returns "C:\\dir"
 * The result does NOT include a trailing backslash.
 * 结果不包含尾部的反斜杠。
 * Returns: true on success, false on error.                        */
bool path_get_directory(const char* filepath, char* dir, int dir_size);

/* Join a directory path and a filename into a full path.
 * 将目录路径和文件名拼接为完整路径。
 * Handles the separator between dir and filename automatically.
 * 自动处理 dir 和 filename 之间的分隔符。
 * Returns: true on success, false on error.                        */
bool path_join(const char* dir, const char* filename,
               char* out, int out_size);

/* Check if a file or directory exists at the given path.
 * 检查给定路径是否存在文件或目录。
 * Returns true if accessible (file or directory). 可访问返回 true。  */
bool path_exists(const char* filepath);

/* Check if the given path is a regular file (not a directory).
 * 检查给定路径是否为普通文件（而非目录）。
 * Returns false for directories and non-existent paths.
 * 目录和不存在的路径返回 false。                                  */
bool path_is_file(const char* filepath);

/* Get the filename portion (without directory) from a path.
 * 从路径中获取文件名部分（不含目录）。
 * Example: "C:\dir\file.txt" -> "file.txt"
 * Returns pointer into the original string (no allocation).
 * 返回指向原始字符串的指针（不分配新内存）。
 * Returns the input as-is if there is no directory separator.
 * 如果没有目录分隔符，原样返回输入。                              */
const char* path_get_filename(const char* filepath);

/* ================================================================
 * Memory Helpers 内存辅助函数
 * ================================================================ */

/* Allocate memory with NULL check. Aborts on failure.
 * 分配内存并进行 NULL 检查。失败时中止程序。
 * For an interactive GUI tool, OOM is extremely unlikely;
 * aborting with a clear message is safer than NULL-pointer bugs.
 * 对于交互式 GUI 工具，OOM 极不可能发生；
 * 明确中止比空指针错误更安全。                                    */
void* mem_alloc(size_t size);

/* Reallocate memory with NULL check. Aborts on failure.
 * 重新分配内存并进行 NULL 检查。失败时中止程序。                    */
void* mem_realloc(void* ptr, size_t size);

/* Safe free macro: frees ptr and sets it to NULL to prevent UAF.
 * 安全释放宏：释放 ptr 并将其设为 NULL 以防止悬空指针。
 * Usage 用法: SAFE_FREE(my_ptr);
 * Note: evaluates ptr twice, so don't use with expressions.
 * 注意：会对 ptr 求值两次，不要用于表达式。                        */
#define SAFE_FREE(ptr) do { free(ptr); (ptr) = NULL; } while(0)

#endif /* UTILS_H */
