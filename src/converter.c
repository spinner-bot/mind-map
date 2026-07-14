/* ============================================================
 * converter.c - Conversion Orchestrator Implementation
 * converter.c - 转换编排器实现
 *
 * Implements single and batch file conversion between formats
 * using the format handler registry and tree data structure.
 * 使用格式处理器注册表和树数据结构实现单文件和批量格式转换。
 * ============================================================ */

#include "converter.h"
#include "tree.h"            /* tree_create, tree_free, tree_validate */
#include "utils.h"           /* path_get_filename, path_change_extension,
                                path_join, str_dup, mem_alloc */
#include "format_handler.h"  /* FormatHandler, HandlerStatus */

#include <stdlib.h>          /* free, NULL */
#include <string.h>          /* strlen, strcpy, strrchr */
#include <stdio.h>           /* snprintf */

/* ================================================================
 * Single File Conversion  单文件转换
 * ================================================================ */

/* 将单个文件从一种格式转换为另一种格式。
 * 完整的转换流水线：创建树 → 解析 → 校验 → 序列化 → 清理。     */
HandlerStatus convert_file(const char* input_path,
                           FormatHandler* input_handler,
                           FormatHandler* output_handler,
                           const char* output_path,
                           void* input_opts,
                           void* output_opts,
                           UserQueryCallback query_cb,
                           void* cb_data) {
    HandlerStatus status;
    handler_status_init(&status);

    /* 输入验证 */
    if (input_path == NULL || input_handler == NULL ||
        output_handler == NULL || output_path == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        handler_status_add_warning(&status,
            RESULT_ERROR_INTERNAL,
            "convert_file: invalid parameters (NULL pointer)");
        return status;
    }

    /* 验证输入处理器 */
    if (input_handler->parse == NULL ||
        output_handler->serialize == NULL) {
        status.result_code = RESULT_ERROR_INTERNAL;
        handler_status_add_warning(&status,
            RESULT_ERROR_INTERNAL,
            "Handler is missing required parse/serialize functions");
        return status;
    }

    /* --- Step 1: Create intermediate tree ---
     * 步骤1：创建中间树                                        */
    Tree* tree = tree_create();
    if (tree == NULL) {
        status.result_code = RESULT_ERROR_MEMORY;
        return status;
    }

    /* --- Step 2: Parse input file ---
     * 步骤2：解析输入文件                                      */
    HandlerStatus parse_status = input_handler->parse(
        input_path, tree, input_opts, query_cb, cb_data);

    /* 检查致命错误 */
    if (parse_status.result_code < 0) {
        /* 解析失败 —— 返回错误状态 */
        status = parse_status;
        tree_free(tree);
        return status;
    }

    /* 复制解析警告到最终状态 */
    for (int i = 0; i < parse_status.warning_count; i++) {
        handler_status_add_warning(&status,
            parse_status.result_code,
            "%s", parse_status.warnings[i]);
    }

    /* --- Step 3: Validate tree ---
     * 步骤3：校验树结构                                        */
    char error_buf[512];
    if (!tree_validate(tree, error_buf, sizeof(error_buf))) {
        status.result_code = RESULT_ERROR_INTERNAL;
        handler_status_add_warning(&status,
            RESULT_ERROR_INTERNAL,
            "Tree validation failed after parsing: %s", error_buf);
        tree_free(tree);
        return status;
    }

    /* 重新计算深度和枝信息，确保元数据一致 */
    tree_recalculate_depths(tree);

    /* --- Step 4: Serialize to output file ---
     * 步骤4：序列化到输出文件                                  */
    HandlerStatus serialize_status = output_handler->serialize(
        output_path, tree, output_opts);

    if (serialize_status.result_code < 0) {
        /* 序列化失败 */
        status = serialize_status;
        tree_free(tree);
        return status;
    }

    /* 复制序列化警告 */
    for (int i = 0; i < serialize_status.warning_count; i++) {
        handler_status_add_warning(&status,
            serialize_status.result_code,
            "%s", serialize_status.warnings[i]);
    }

    /* --- Step 5: Cleanup ---
     * 步骤5：清理                                              */
    tree_free(tree);

    /* If we had warnings but no error, set a positive result code */
    if (status.warning_count > 0 && status.result_code == RESULT_OK) {
        /* Keep whatever the first warning code is */
    }

    return status;
}

/* ================================================================
 * Batch Conversion  批量转换
 * ================================================================ */

/* 运行批量文件转换。
 * 每个文件独立转换，结果汇总在一个 BatchResult 中。             */
BatchResult* convert_batch(const BatchConfig* config) {
    if (config == NULL || config->input_paths == NULL ||
        config->input_count <= 0 || config->output_handler == NULL) {
        return NULL;
    }

    /* 分配结果结构 */
    BatchResult* result = (BatchResult*)mem_alloc(sizeof(BatchResult));
    result->total_files    = config->input_count;
    result->success_count  = 0;
    result->warning_count  = 0;
    result->error_count    = 0;

    /* 分配每个文件的结果数组 */
    result->output_paths   = (char**)mem_alloc(
        config->input_count * sizeof(char*));
    result->error_messages = (char**)mem_alloc(
        config->input_count * sizeof(char*));
    result->warning_counts = (int*)mem_alloc(
        config->input_count * sizeof(int));

    /* 初始化为 NULL */
    for (int i = 0; i < config->input_count; i++) {
        result->output_paths[i]   = NULL;
        result->error_messages[i] = NULL;
        result->warning_counts[i] = 0;
    }

    /* 逐文件转换 */
    for (int i = 0; i < config->input_count; i++) {
        const char* input_path = config->input_paths[i];
        if (input_path == NULL) {
            result->error_count++;
            result->error_messages[i] = str_dup("NULL input path");
            continue;
        }

        /* 报告进度 */
        if (config->progress_cb != NULL) {
            config->progress_cb(i + 1, config->input_count,
                                input_path, config->progress_cb_data);
        }

        /* 确定输入格式处理器 */
        FormatHandler* input_handler = NULL;
        if (config->auto_detect_input) {
            input_handler = format_find_by_extension(input_path);
        } else if (config->input_handlers != NULL) {
            input_handler = config->input_handlers[i];
        }

        if (input_handler == NULL) {
            result->error_count++;
            result->error_messages[i] = str_dup(
                "Could not determine input format (unknown extension)");
            continue;
        }

        /* 生成输出路径 */
        char output_path[MAX_PATH_LEN];
        if (!converter_make_output_path(input_path,
                                         config->output_handler,
                                         config->output_dir,
                                         output_path,
                                         sizeof(output_path))) {
            result->error_count++;
            result->error_messages[i] = str_dup(
                "Could not generate output path");
            continue;
        }

        /* 执行转换 */
        /* 为每个处理器创建默认选项 */
        void* in_opts  = (input_handler->create_default_options)
                         ? input_handler->create_default_options()
                         : NULL;
        void* out_opts = (config->output_handler->create_default_options)
                         ? config->output_handler->create_default_options()
                         : NULL;

        HandlerStatus file_status = convert_file(
            input_path, input_handler, config->output_handler,
            output_path, in_opts, out_opts,
            config->query_cb, config->query_cb_data);

        /* 释放选项 */
        if (input_handler->free_options && in_opts) {
            input_handler->free_options(in_opts);
        }
        if (config->output_handler->free_options && out_opts) {
            config->output_handler->free_options(out_opts);
        }

        /* 记录结果 */
        if (file_status.result_code < 0) {
            /* 错误 */
            result->error_count++;
            result->error_messages[i] = str_dup(
                handler_result_to_string(file_status.result_code));
        } else {
            /* 成功（可能有警告） */
            result->success_count++;
            result->output_paths[i] = str_dup(output_path);
            result->warning_counts[i] = file_status.warning_count;
            if (file_status.warning_count > 0) {
                result->warning_count++;
            }
        }
    }

    return result;
}

/* 释放批量转换结果。 */
void batch_result_free(BatchResult* result) {
    if (result == NULL) {
        return;
    }

    if (result->output_paths != NULL) {
        for (int i = 0; i < result->total_files; i++) {
            free(result->output_paths[i]);
        }
        free(result->output_paths);
    }

    if (result->error_messages != NULL) {
        for (int i = 0; i < result->total_files; i++) {
            free(result->error_messages[i]);
        }
        free(result->error_messages);
    }

    free(result->warning_counts);
    free(result);
}

/* 生成输出文件路径：输入路径 + 新扩展名，放到输出目录中。
 * 示例："D:\notes\chapter.txt" + JSON handler + "D:\output"
 *       → "D:\output\chapter.json"                              */
bool converter_make_output_path(const char* input_path,
                                 FormatHandler* output_handler,
                                 const char* output_dir,
                                 char* out_path, int out_size) {
    if (input_path == NULL || output_handler == NULL ||
        out_path == NULL || out_size <= 0) {
        return false;
    }

    /* 获取输入文件名（不含目录） */
    const char* filename = path_get_filename(input_path);
    if (filename == NULL) {
        return false;
    }

    /* 更改扩展名 */
    char new_filename[MAX_PATH_LEN];
    /* 找到输入文件名的扩展名 */
    const char* dot = strrchr(filename, '.');
    if (dot != NULL) {
        /* 截取文件名（不含扩展名） */
        int base_len = (int)(dot - filename);
        if (base_len >= MAX_PATH_LEN) base_len = MAX_PATH_LEN - 10;
        memcpy(new_filename, filename, base_len);
        /* 追加新扩展名 */
        strcpy(new_filename + base_len, output_handler->extension);
    } else {
        /* 没有扩展名：直接追加 */
        snprintf(new_filename, sizeof(new_filename),
                 "%s%s", filename, output_handler->extension);
    }

    /* 拼接到输出目录 */
    const char* dir = (output_dir != NULL && output_dir[0] != '\0')
                      ? output_dir : ".";

    return path_join(dir, new_filename, out_path, out_size);
}
