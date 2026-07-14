/* ============================================================
 * utils.c - General Utility Functions Implementation
 * utils.c - 通用工具函数实现
 *
 * Implements string manipulation, path handling, and memory
 * management utilities defined in utils.h.
 * 实现 utils.h 中定义的字符串操作、路径处理和内存管理工具函数。
 * ============================================================ */

#include "utils.h"

#include <stdlib.h>   /* malloc, free, realloc, abort */
#include <string.h>   /* strlen, strcpy, strcmp, memcpy, memmove */
#include <stdio.h>    /* fprintf, snprintf */
#include <ctype.h>    /* isspace */
#include <sys/stat.h> /* stat */

#ifdef _WIN32
/* Windows specific: for PathFindFileNameA, GetFileAttributesA etc.
 * 仅限 Windows：PathFindFileNameA、GetFileAttributesA 等 */
#include <windows.h>
#include <shlwapi.h>  /* PathFindFileNameA, PathFindExtensionA */
#endif

/* ================================================================
 * Memory Helpers 内存辅助函数
 * ================================================================ */

/* 分配内存并在失败时中止程序。
 * 这保证了调用者永远不需要检查 NULL 返回值，
 * 从而简化了所有调用方的代码。                                 */
void* mem_alloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL && size > 0) {
        /* OOM 在这种交互式桌面工具中极不可能发生。
         * 如果真的发生，立即中止是最好的选择 ——
         * 比让程序在空指针上崩溃并产生难以调试的错误要好。     */
        fprintf(stderr, "FATAL: memory allocation failed (%zu bytes)\n", size);
        abort();
    }
    return ptr;
}

/* 重新分配内存并在失败时中止程序。
 * 行为与 realloc 一致：如果 ptr 为 NULL，等同于 malloc；
 * 如果 size 为 0，等同于 free（但我们直接调用 abort 在这种情况下）。*/
void* mem_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (new_ptr == NULL && size > 0) {
        /* 重分配失败同样意味着系统内存耗尽 —— 中止程序 */
        fprintf(stderr, "FATAL: memory reallocation failed (%zu bytes)\n", size);
        abort();
    }
    return new_ptr;
}

/* ================================================================
 * String Operations 字符串操作
 * ================================================================ */

/* 原地去除字符串首尾的空白字符。
 * 算法：
 *   1. 跳过开头的空白字符（移动 start 指针）
 *   2. 找到结尾的空白字符开始位置（设置 end 标记）
 *   3. 如果 start 在原字符串中间，使用 memmove 将内容移到开头
 *   4. 在 end 位置写入 '\0'
 * 注意：这是原地操作，所以输入字符串必须可写。                  */
char* str_trim(char* str) {
    if (str == NULL) {
        return NULL;  /* NULL 安全 */
    }

    /* 跳过开头的空白字符 */
    char* start = str;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    /* 如果全是空白，返回空字符串 */
    if (*start == '\0') {
        *str = '\0';
        return str;
    }

    /* 找到结尾（最后一个非空白字符之后的位置） */
    char* end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    /* 如果 start 移动了，需要将内容移到字符串开头 */
    if (start != str) {
        size_t len = end - start;  /* 修剪后的长度（不含 '\0'） */
        memmove(str, start, len);
        str[len] = '\0';
    } else {
        /* start 没有移动，直接在 end 处截断 */
        *end = '\0';
    }

    return str;
}

/* 安全复制字符串：NULL 输入返回 NULL。
 * 这是标准 strdup 的 NULL 安全封装。
 * 调用者负责 free() 返回的字符串。                              */
char* str_dup(const char* src) {
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src);
    char* copy = (char*)mem_alloc(len + 1);
    memcpy(copy, src, len + 1);  /* +1 包含 '\0' */
    return copy;
}

/* 检查字符串是否以指定前缀开头。
 * 如果任一参数为 NULL，返回 false。
 * 前缀比字符串长时返回 false。                                 */
bool str_startswith(const char* str, const char* prefix) {
    if (str == NULL || prefix == NULL) {
        return false;
    }
    size_t str_len    = strlen(str);
    size_t prefix_len = strlen(prefix);
    if (prefix_len > str_len) {
        return false;
    }
    /* 比较前 prefix_len 个字符 */
    return (strncmp(str, prefix, prefix_len) == 0);
}

/* 检查字符串是否以指定后缀结尾。
 * 如果任一参数为 NULL，返回 false。
 * 后缀比字符串长时返回 false。                                 */
bool str_endswith(const char* str, const char* suffix) {
    if (str == NULL || suffix == NULL) {
        return false;
    }
    size_t str_len    = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return false;
    }
    /* 比较字符串最后 suffix_len 个字符 */
    return (strcmp(str + str_len - suffix_len, suffix) == 0);
}

/* 统计字符串中某个字符的出现次数。
 * NULL 输入返回 0。                                            */
int str_count_char(const char* str, char ch) {
    if (str == NULL) {
        return 0;
    }
    int count = 0;
    for (const char* p = str; *p != '\0'; p++) {
        if (*p == ch) {
            count++;
        }
    }
    return count;
}

/* 按分隔符拆分字符串。
 * 返回堆分配的字符串数组（每个元素也是堆分配的）。
 * 算法：
 *   1. 遍历一次统计段数（分隔符个数 + 1）
 *   2. 分配数组
 *   3. 遍历第二次，提取每个片段并复制到数组中
 * 时间复杂度：O(n)，空间复杂度：O(n)                             */
char** str_split(const char* str, char delimiter, int* out_count) {
    if (str == NULL || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }

    /* 第一次遍历：统计段数 */
    int count = 1;  /* 至少有一段 */
    for (const char* p = str; *p != '\0'; p++) {
        if (*p == delimiter) {
            count++;
        }
    }

    /* 分配结果数组 */
    char** parts = (char**)mem_alloc(count * sizeof(char*));

    /* 第二次遍历：提取每个片段 */
    const char* segment_start = str;
    int idx = 0;

    for (const char* p = str; ; p++) {
        if (*p == delimiter || *p == '\0') {
            /* 提取从 segment_start 到 p 的片段（不含分隔符） */
            size_t seg_len = p - segment_start;
            parts[idx] = (char*)mem_alloc(seg_len + 1);
            if (seg_len > 0) {
                memcpy(parts[idx], segment_start, seg_len);
            }
            parts[idx][seg_len] = '\0';
            idx++;

            if (*p == '\0') {
                break;  /* 到达字符串末尾 */
            }

            /* 跳过当前分隔符，下一个片段从下一个字符开始 */
            segment_start = p + 1;
        }
    }

    *out_count = count;
    return parts;
}

/* 释放 str_split() 的结果。
 * 先释放每个片段字符串，再释放数组本身。                        */
void str_split_free(char** parts, int count) {
    if (parts == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(parts[i]);
        parts[i] = NULL;
    }
    free(parts);
}

/* ================================================================
 * Path Operations 路径操作
 * ================================================================ */

/* 提取文件扩展名（始终小写）。
 * 使用 Windows API PathFindExtensionA 或手动查找最后一个 '.'。
 * 结果包含 '.'（如 ".txt"），无扩展名时为空字符串。             */
bool path_get_extension(const char* filepath, char* ext, int ext_size) {
    if (filepath == NULL || ext == NULL || ext_size <= 0) {
        return false;
    }

    ext[0] = '\0';  /* 默认：无扩展名 */

    /* 查找路径中最后一个 '.' */
    const char* dot = strrchr(filepath, '.');
    if (dot == NULL) {
        /* 没有扩展名 —— 返回空字符串 */
        return true;
    }

    /* 检查这个 '.' 是否在路径分隔符之后（确保它是文件名中的 '.'，
     * 而不是目录名中的）。例如 "C:\dir.1\file" 不应提取 ".1\file"。*/
    const char* last_sep = strrchr(filepath, '\\');
    if (strrchr(filepath, '/') > last_sep) {
        last_sep = strrchr(filepath, '/');
    }
    if (last_sep != NULL && dot < last_sep) {
        /* '.' 在最后一个路径分隔符之前 —— 这是目录名的一部分 */
        return true;
    }

    /* 复制扩展名并转换为小写 */
    size_t ext_len = strlen(dot);
    if (ext_len >= (size_t)ext_size) {
        ext_len = ext_size - 1;  /* 为 '\0' 预留空间 */
    }

    for (size_t i = 0; i < ext_len; i++) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext[ext_len] = '\0';

    return true;
}

/* 更改路径的文件扩展名。
 * 如果原始路径有扩展名则替换，否则追加新的扩展名。              */
bool path_change_extension(const char* filepath, const char* new_ext,
                           char* out_path, int out_size) {
    if (filepath == NULL || new_ext == NULL || out_path == NULL || out_size <= 0) {
        return false;
    }

    /* 找到原始路径中的最后一个 '.' */
    const char* dot = strrchr(filepath, '.');

    /* 验证这个 '.' 是文件名的一部分 */
    const char* last_sep = strrchr(filepath, '\\');
    if (strrchr(filepath, '/') > last_sep) {
        last_sep = strrchr(filepath, '/');
    }

    size_t base_len;  /* 不含扩展名的部分长度 */
    if (dot != NULL && (last_sep == NULL || dot > last_sep)) {
        /* 有有效的扩展名 —— 截断到 '.' 位置 */
        base_len = dot - filepath;
    } else {
        /* 没有扩展名 —— 使用完整文件名 */
        base_len = strlen(filepath);
    }

    /* 确保输出缓冲区足够大 */
    size_t new_len = base_len + strlen(new_ext);
    if (new_len >= (size_t)out_size) {
        return false;  /* 缓冲区太小 */
    }

    /* 复制基础部分并追加新扩展名 */
    memcpy(out_path, filepath, base_len);
    strcpy(out_path + base_len, new_ext);  /* strcpy 包含 '\0' */

    return true;
}

/* 提取文件路径中的目录部分。
 * 查找最后一个路径分隔符（'\' 或 '/'），
 * 提取其之前的所有内容。如果没有分隔符，返回 "."（当前目录）。  */
bool path_get_directory(const char* filepath, char* dir, int dir_size) {
    if (filepath == NULL || dir == NULL || dir_size <= 0) {
        return false;
    }

    /* 查找最后一个路径分隔符 */
    const char* last_sep = strrchr(filepath, '\\');
    const char* last_fwd = strrchr(filepath, '/');
    if (last_fwd > last_sep) {
        last_sep = last_fwd;
    }

    if (last_sep == NULL) {
        /* 没有目录分隔符 —— 返回 "."（当前目录） */
        if (dir_size < 3) {
            return false;
        }
        dir[0] = '.';
        dir[1] = '\0';
        return true;
    }

    /* 复制分隔符之前的部分 */
    size_t dir_len = last_sep - filepath;
    if (dir_len == 0) {
        /* 路径以分隔符开头（如 "\file.txt"），返回根目录 */
        if (dir_size < 2) {
            return false;
        }
        dir[0] = '\\';
        dir[1] = '\0';
        return true;
    }

    if (dir_len >= (size_t)dir_size) {
        return false;
    }

    memcpy(dir, filepath, dir_len);
    dir[dir_len] = '\0';

    return true;
}

/* 拼接目录路径和文件名。
 * 自动处理分隔符（如果 dir 不以 '\' 或 '/' 结尾，则添加 '\'）。*/
bool path_join(const char* dir, const char* filename,
               char* out, int out_size) {
    if (dir == NULL || filename == NULL || out == NULL || out_size <= 0) {
        return false;
    }

    size_t dir_len  = strlen(dir);
    size_t file_len = strlen(filename);

    /* 判断 dir 是否以路径分隔符结尾 */
    bool dir_has_sep = (dir_len > 0 &&
        (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/'));

    /* 计算所需的总长度 */
    size_t total_len = dir_len + file_len + (dir_has_sep ? 0 : 1);
    if (total_len >= (size_t)out_size) {
        return false;  /* 缓冲区太小 */
    }

    /* 复制目录部分 */
    memcpy(out, dir, dir_len);
    if (!dir_has_sep) {
        out[dir_len] = '\\';  /* 添加 Windows 路径分隔符 */
        dir_len++;
    }

    /* 复制文件名部分 */
    memcpy(out + dir_len, filename, file_len);
    out[dir_len + file_len] = '\0';

    return true;
}

/* 检查给定路径是否存在（文件或目录均可）。
 * 使用 stat() 进行跨平台检查。                                  */
bool path_exists(const char* filepath) {
    if (filepath == NULL) {
        return false;
    }
    struct stat st;
    return (stat(filepath, &st) == 0);
}

/* 检查给定路径是否为普通文件。
 * 使用 stat() + S_ISREG 宏进行检查。
 * 目录和不存在的路径返回 false。                                */
bool path_is_file(const char* filepath) {
    if (filepath == NULL) {
        return false;
    }
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false;  /* 无法访问（不存在、权限不足等） */
    }
    return S_ISREG(st.st_mode);
}

/* 从路径中提取文件名部分。
 * 查找最后一个路径分隔符，返回其后的内容。
 * 如果没有分隔符，返回整个路径。
 * 注意：返回值指向原始字符串内部，不分配新内存。                */
const char* path_get_filename(const char* filepath) {
    if (filepath == NULL) {
        return NULL;
    }

    /* 查找最后一个 '\' 或 '/' */
    const char* last_sep = strrchr(filepath, '\\');
    const char* last_fwd = strrchr(filepath, '/');
    if (last_fwd > last_sep) {
        last_sep = last_fwd;
    }

    if (last_sep == NULL) {
        /* 没有目录分隔符 —— 整个字符串就是文件名 */
        return filepath;
    }

    /* 返回分隔符之后的内容 */
    return last_sep + 1;
}
