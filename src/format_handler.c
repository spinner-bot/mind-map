/* ============================================================
 * format_handler.c - Format Handler Registry Implementation
 * format_handler.c - 格式处理器注册表实现
 *
 * Implements the format handler registry: a dynamic array of
 * registered FormatHandler pointers with lookup functions.
 * 实现格式处理器注册表：一个注册的 FormatHandler 指针的动态数组，
 * 附带查找函数。
 *
 * The registry is a global singleton (not thread-safe — this
 * is a single-threaded desktop GUI application).
 * 注册表是全局单例（非线程安全 —— 这是单线程桌面 GUI 应用程序）。
 * ============================================================ */

#include "format_handler.h"
#include "utils.h"     /* mem_alloc, mem_realloc, str_endswith */

#include <stdlib.h>    /* free */
#include <string.h>    /* strchr, strcmp, strlen, strcpy */
#include <stdio.h>     /* vsnprintf */
#include <stdarg.h>    /* va_start, va_end */

/* ================================================================
 * Registry State 注册表状态
 * ================================================================ */

/* Maximum number of registered handlers. The registry grows
 * dynamically, but this is the initial capacity.
 * 最大注册处理器数量。注册表动态增长，这是初始容量。              */
#define REGISTRY_INITIAL_CAPACITY 8

/* The global registry array. 全局注册表数组。
 * Static to this file — all access is through the public API.
 * 文件内静态 —— 所有访问通过公开 API。                           */
static FormatHandler** registry = NULL;
static int registry_count    = 0;   /* Currently registered handlers */
static int registry_capacity = 0;   /* Allocated capacity */

/* ================================================================
 * Registry Functions 注册表函数
 * ================================================================ */

/* 初始化注册表。分配初始容量。
 * 必须在使用其他注册表函数之前调用。
 * 可以安全地多次调用（幂等）。                                  */
bool format_registry_init(void) {
    if (registry != NULL) {
        /* 已经初始化 —— 幂等操作 */
        return true;
    }

    registry = (FormatHandler**)mem_alloc(
        REGISTRY_INITIAL_CAPACITY * sizeof(FormatHandler*));
    registry_count    = 0;
    registry_capacity = REGISTRY_INITIAL_CAPACITY;

    /* 初始化所有指针为 NULL */
    for (int i = 0; i < registry_capacity; i++) {
        registry[i] = NULL;
    }

    return true;
}

/* 关闭注册表并释放内存。
 * 注意：这不释放各个处理器（它们通常是静态单例）。
 * 只释放注册表数组本身。                                      */
void format_registry_shutdown(void) {
    if (registry == NULL) {
        return;  /* 未初始化或已关闭 */
    }

    /* 清空所有条目（处理器本身是静态的，不需要释放） */
    for (int i = 0; i < registry_count; i++) {
        registry[i] = NULL;
    }

    free(registry);
    registry         = NULL;
    registry_count    = 0;
    registry_capacity = 0;
}

/* 注册一个格式处理器。
 * 处理器通过其 format_name 唯一标识。
 * 如果同名处理器已注册，返回 false 并保留原处理器。            */
bool format_register(const FormatHandler* handler) {
    if (handler == NULL || handler->format_name == NULL) {
        return false;  /* 无效的处理器 */
    }

    if (registry == NULL) {
        /* 注册表未初始化 —— 自动初始化 */
        if (!format_registry_init()) {
            return false;
        }
    }

    /* 检查是否已经注册了同名处理器 */
    for (int i = 0; i < registry_count; i++) {
        if (registry[i] != NULL &&
            strcmp(registry[i]->format_name, handler->format_name) == 0) {
            /* 同名处理器已存在 —— 拒绝重复注册 */
            return false;
        }
    }

    /* 检查容量是否足够，不够则扩容 */
    if (registry_count >= registry_capacity) {
        int new_capacity = registry_capacity * 2;
        registry = (FormatHandler**)mem_realloc(
            registry, new_capacity * sizeof(FormatHandler*));
        /* 初始化新槽位 */
        for (int i = registry_capacity; i < new_capacity; i++) {
            registry[i] = NULL;
        }
        registry_capacity = new_capacity;
    }

    /* 添加处理器 */
    registry[registry_count] = (FormatHandler*)handler;  /* 去除 const */
    registry_count++;

    return true;
}

/* 从注册表中移除处理器。
 * 通过名称查找处理器，找到后移除。
 * 使用前移后续元素的方式保持数组紧凑。                          */
bool format_unregister(const char* format_name) {
    if (format_name == NULL || registry == NULL) {
        return false;
    }

    /* 查找处理器 */
    for (int i = 0; i < registry_count; i++) {
        if (registry[i] != NULL &&
            strcmp(registry[i]->format_name, format_name) == 0) {
            /* 找到了 —— 将后续元素前移 */
            int remaining = registry_count - i - 1;
            if (remaining > 0) {
                /* 移动后续元素 */
                for (int j = i; j < registry_count - 1; j++) {
                    registry[j] = registry[j + 1];
                }
            }
            /* 清理最后一个槽位 */
            registry[registry_count - 1] = NULL;
            registry_count--;
            return true;
        }
    }

    return false;  /* 未找到 */
}

/* 按文件扩展名查找处理器。
 * 从文件路径中提取扩展名，然后与每个注册处理器的 extension
 * 字段进行不区分大小写的比较。
 * 返回值：匹配的处理器，未找到返回 NULL。                       */
FormatHandler* format_find_by_extension(const char* filepath) {
    if (filepath == NULL || registry == NULL) {
        return NULL;
    }

    /* 提取扩展名：查找最后一个 '.' */
    const char* dot = strrchr(filepath, '.');
    if (dot == NULL) {
        return NULL;  /* 没有扩展名 */
    }

    /* 验证 '.' 是文件名的一部分（不在目录名中） */
    const char* last_sep = strrchr(filepath, '\\');
    if (strrchr(filepath, '/') > last_sep) {
        last_sep = strrchr(filepath, '/');
    }
    if (last_sep != NULL && dot < last_sep) {
        return NULL;  /* '.' 在目录中，不是文件扩展名 */
    }

    /* 遍历注册的处理器，进行不区分大小写的扩展名比较 */
    for (int i = 0; i < registry_count; i++) {
        if (registry[i] == NULL || registry[i]->extension == NULL) {
            continue;
        }

        /* 比较扩展名（不区分大小写）。
         * 由于扩展名以 '.' 开头（如 ".json"），
         * 直接比较从 dot 开始的字符串即可。                      */
        const char* ext = registry[i]->extension;
        const char* file_ext = dot;
        bool matches = true;
        while (*ext != '\0' && *file_ext != '\0') {
            /* 不区分大小写比较 */
            char e = *ext;
            char f = *file_ext;
            if (e >= 'A' && e <= 'Z') e += ('a' - 'A');
            if (f >= 'A' && f <= 'Z') f += ('a' - 'A');
            if (e != f) {
                matches = false;
                break;
            }
            ext++;
            file_ext++;
        }
        /* 两者必须同时到达末尾才算完全匹配 */
        if (matches && *ext == '\0' && *file_ext == '\0') {
            return registry[i];
        }
    }

    return NULL;  /* 没有匹配的处理器 */
}

/* 按格式名称查找处理器。
 * 对 format_name 字段进行精确（区分大小写）比较。               */
FormatHandler* format_find_by_name(const char* format_name) {
    if (format_name == NULL || registry == NULL) {
        return NULL;
    }

    for (int i = 0; i < registry_count; i++) {
        if (registry[i] != NULL &&
            strcmp(registry[i]->format_name, format_name) == 0) {
            return registry[i];
        }
    }

    return NULL;
}

/* 列出所有已注册的处理器。
 * 将处理器指针复制到调用者提供的数组中。
 * 返回复制的数量（如果超出 max_count 则会被截断）。             */
int format_list_all(FormatHandler** out_array, int max_count) {
    if (out_array == NULL || max_count <= 0 || registry == NULL) {
        return 0;
    }

    int count = (registry_count < max_count) ? registry_count : max_count;
    for (int i = 0; i < count; i++) {
        out_array[i] = registry[i];
    }

    return registry_count;  /* 返回总数，即使超出 max_count */
}

/* ================================================================
 * Status Helpers 状态辅助函数
 * ================================================================ */

/* 将 HandlerStatus 初始化为干净状态（OK，无警告）。
 * 零填充所有字段是最简单的实现方式。                            */
void handler_status_init(HandlerStatus* status) {
    if (status == NULL) {
        return;
    }

    /* 零填充整个结构 */
    status->result_code   = RESULT_OK;
    status->warning_count = 0;

    /* warnings 数组在零填充后自动包含空字符串 */
    for (int i = 0; i < MAX_WARNINGS; i++) {
        status->warnings[i][0] = '\0';
    }
}

/* 向状态中添加警告。
 * 使用 printf 风格的格式化来构建警告消息。
 * 更新 result_code 为第一个警告码（或保持 OK）。
 * 如果已累积 MAX_WARNINGS 条警告，新警告被静默丢弃。           */
void handler_status_add_warning(HandlerStatus* status,
                                 HandlerResult code,
                                 const char* fmt, ...) {
    if (status == NULL || fmt == NULL) {
        return;
    }

    /* 如果已经达到最大警告数，静默丢弃 */
    if (status->warning_count >= MAX_WARNINGS) {
        return;
    }

    /* 格式化警告消息 */
    va_list args;
    va_start(args, fmt);
    /* 使用 vsnprintf 安全格式化到警告槽位 */
    int idx = status->warning_count;
    vsnprintf(status->warnings[idx], MAX_WARN_LEN, fmt, args);
    /* 确保以 NUL 终止（vsnprintf 总是以 NUL 终止，但作为安全措施） */
    status->warnings[idx][MAX_WARN_LEN - 1] = '\0';
    va_end(args);

    status->warning_count++;

    /* 设置 result_code：第一次警告时改为 warning code，
     * 后续警告保持第一个非零 code（让调用者知道发生了什么）。    */
    if (status->result_code == RESULT_OK) {
        status->result_code = code;
    }
}

/* 获取结果码的人类可读描述。
 * 返回指向静态字符串的指针（不要释放）。                        */
const char* handler_result_to_string(HandlerResult code) {
    switch (code) {
        /* 成功 */
        case RESULT_OK:
            return "OK";

        /* 警告 */
        case RESULT_WARN_KEY_LOST:
            return "Key info lost (dict→list conversion)";
        case RESULT_WARN_BRANCH_LOST:
            return "Branch info lost";
        case RESULT_WARN_HEADING_SKIP:
            return "Heading level skip detected";
        case RESULT_WARN_NUMBER_GAP:
            return "Non-contiguous numbering detected";
        case RESULT_WARN_CONTENT_TRUNC:
            return "Content truncated";
        case RESULT_WARN_INDEX_ZERO_MIXED:
            return "Index-0 mode mismatch";

        /* 错误 */
        case RESULT_ERROR_FILE_OPEN:
            return "Could not open file";
        case RESULT_ERROR_FILE_READ:
            return "Could not read file";
        case RESULT_ERROR_FILE_WRITE:
            return "Could not write file";
        case RESULT_ERROR_INVALID_FORMAT:
            return "Invalid format";
        case RESULT_ERROR_PARSE:
            return "Parse error";
        case RESULT_ERROR_MEMORY:
            return "Memory allocation failure";
        case RESULT_ERROR_ENCODING:
            return "Encoding error";
        case RESULT_ERROR_EMPTY_TREE:
            return "Empty tree (nothing to serialize)";
        case RESULT_ERROR_INTERNAL:
            return "Internal error";
        case RESULT_ERROR_USER_CANCEL:
            return "Operation cancelled by user";

        default:
            return "Unknown result code";
    }
}
