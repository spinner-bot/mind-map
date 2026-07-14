/* ============================================================
 * converter.h - Conversion Orchestrator
 * converter.h - 转换编排器
 *
 * Coordinates single and batch file conversions between
 * supported formats. Uses the format handler registry to
 * find appropriate parsers and serializers.
 * 协调支持格式之间的单文件和批量文件转换。
 * 使用格式处理器注册表来找到合适的解析器和序列化器。
 * ============================================================ */

#ifndef CONVERTER_H
#define CONVERTER_H

#include "format_handler.h"  /* FormatHandler, HandlerStatus, UserQueryCallback */
#include <stdbool.h>

/* Maximum path length (Windows MAX_PATH is 260) */
#define MAX_PATH_LEN 512

/* ================================================================
 * Batch Conversion Types  批量转换类型
 * ================================================================ */

/* Callback for reporting batch progress to the GUI.
 * 向 GUI 报告批量进度的回调。
 * current:      which file is being processed (1-based)
 *               正在处理第几个文件（从1开始）
 * total:        total number of files in the batch
 *               批量中的文件总数
 * current_file: path of the file being processed
 *               正在处理的文件路径
 * user_data:    opaque pointer from the caller
 *               调用者传入的不透明指针                             */
typedef void (*ConvertProgressCallback)(int current, int total,
                                         const char* current_file,
                                         void* user_data);

/* Configuration for a batch conversion operation.
 * 批量转换操作的配置。                                            */
typedef struct {
    char**            input_paths;         /* Array of input file paths */
    int               input_count;         /* Number of input files */
    FormatHandler*    output_handler;      /* Target output format */
    char              output_dir[MAX_PATH_LEN]; /* Output directory */
    bool              auto_detect_input;   /* Auto-detect input format by extension */
    FormatHandler**   input_handlers;      /* Per-file handlers (if not auto) */
    UserQueryCallback query_cb;            /* For user decisions during conversion */
    void*             query_cb_data;       /* Opaque data for query_cb */
    ConvertProgressCallback progress_cb;   /* Progress reporting callback */
    void*             progress_cb_data;    /* Opaque data for progress_cb */
} BatchConfig;

/* Results of a batch conversion operation.
 * 批量转换操作的结果。                                            */
typedef struct {
    int     total_files;
    int     success_count;      /* Converted successfully (may include warnings) */
    int     warning_count;      /* Files with warnings */
    int     error_count;        /* Files that failed to convert */
    char**  output_paths;       /* output_paths[i] = result path or NULL on error */
    char**  error_messages;     /* error_messages[i] = message or NULL */
    int*    warning_counts;     /* warning_counts[i] = number of warnings per file */
} BatchResult;

/* ================================================================
 * Conversion Functions  转换函数
 * ================================================================ */

/* Convert a single file from one format to another.
 * 将一个文件从一种格式转换为另一种格式。
 *
 * Workflow: open → detect encoding → parse → validate → serialize → save
 * 工作流：打开 → 检测编码 → 解析 → 校验 → 序列化 → 保存
 *
 * Parameters 参数:
 *   input_path   - Source file path 源文件路径
 *   input_handler - Format handler for the input file 输入文件格式处理器
 *   output_handler - Format handler for the output 输出格式处理器
 *   output_path  - Destination file path 目标文件路径
 *   input_opts   - Options for the input handler (may be NULL)
 *                  输入处理器的选项（可为 NULL）
 *   output_opts  - Options for the output handler (may be NULL)
 *                  输出处理器的选项（可为 NULL）
 *   query_cb     - User decision callback 用户决策回调
 *   cb_data      - Opaque data for query_cb query_cb 的不透明数据
 *
 * Returns 返回值:
 *   HandlerStatus with result and warnings. 含结果和警告的 HandlerStatus。
 *   Positive result_code = success with warnings, 0 = clean success,
 *   negative = error.                                              */
HandlerStatus convert_file(const char* input_path,
                           FormatHandler* input_handler,
                           FormatHandler* output_handler,
                           const char* output_path,
                           void* input_opts,
                           void* output_opts,
                           UserQueryCallback query_cb,
                           void* cb_data);

/* Run a batch conversion. 运行批量转换。
 * Allocates and returns BatchResult (caller must free via
 * batch_result_free()). 分配并返回 BatchResult
 * （调用者必须通过 batch_result_free() 释放）。                  */
BatchResult* convert_batch(const BatchConfig* config);

/* Free a BatchResult allocated by convert_batch().
 * 释放 convert_batch() 分配的 BatchResult。                     */
void batch_result_free(BatchResult* result);

/* Generate an output file path by changing the extension.
 * 通过更改扩展名生成输出文件路径。
 * Example: "notes.txt" + JSON handler → "notes.json"
 * Places result in out_path buffer. 将结果放入 out_path 缓冲区。
 * Returns true on success. 成功返回 true。                       */
bool converter_make_output_path(const char* input_path,
                                 FormatHandler* output_handler,
                                 const char* output_dir,
                                 char* out_path, int out_size);

#endif /* CONVERTER_H */
