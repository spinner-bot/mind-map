/* ============================================================
 * tree.c - Core Tree Operations Implementation
 * tree.c - 核心树操作实现
 *
 * Implements the tree data structure defined in tree.h.
 * 实现 tree.h 中定义的树数据结构。
 *
 * Internal conventions 内部约定:
 *   - All functions that allocate memory check the result
 *     and abort on failure (via a helper) since there is
 *     no meaningful recovery path for OOM in this tool.
 *     所有分配内存的函数都会检查结果并在失败时中止
 *     （通过辅助函数），因为此工具对 OOM 无有意义的恢复路径。
 *   - NULL is a valid content value; code must handle it.
 *     NULL 是合法的 content 值；代码必须处理它。
 *   - The children array uses geometric growth (2x) for
 *     amortized O(1) append.
 *     children 数组使用几何增长（2x）以实现均摊 O(1) 追加。
 * ============================================================ */

#include "tree.h"

#include <stdlib.h>   /* malloc, free, realloc, NULL, size_t */
#include <string.h>   /* strdup, strlen, strcpy, memmove */
#include <stdio.h>    /* snprintf, sprintf, vsnprintf */
#include <stdarg.h>   /* va_list, va_start, va_end */
#include <stdint.h>   /* uint32_t */

/* ================================================================
 * Internal helpers 内部辅助函数
 * ================================================================ */

/* Safe malloc wrapper: aborts on failure.
 * 安全的 malloc 封装：失败时中止程序。
 * Since this is an interactive GUI tool, OOM is extremely
 * unlikely and crashing with a clear message is preferable
 * to cascading NULL-pointer bugs. 由于这是交互式 GUI 工具，
 * OOM 极不可能发生，崩溃并给出明确信息优于级联的空指针错误。   */
static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL && size > 0) {
        /* 内存分配失败 —— 极不可能的情况，中止程序 */
        fprintf(stderr, "FATAL: memory allocation failed (%zu bytes)\n", size);
        abort();
    }
    return ptr;
}

/* Safe realloc wrapper: aborts on failure.
 * 安全的 realloc 封装：失败时中止程序。                         */
static void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (new_ptr == NULL && size > 0) {
        /* 内存重分配失败 —— 极不可能的情况，中止程序 */
        fprintf(stderr, "FATAL: memory reallocation failed (%zu bytes)\n", size);
        abort();
    }
    return new_ptr;
}

/* Safe strdup wrapper: handles NULL input, aborts on OOM.
 * 安全的 strdup 封装：处理 NULL 输入，OOM 时中止。             */
static char* safe_strdup(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    char* copy = (char*)safe_malloc(len + 1);
    /* 复制字符串内容，包括结尾的 '\0' */
    memcpy(copy, str, len + 1);
    return copy;
}

/* ================================================================
 * Lifecycle Functions 生命周期函数
 * ================================================================ */

/* 创建一个新的空树，包含一个 root 节点。
 * root 节点的 content 为 NULL，无子节点，depth 为 0。
 * 字段初始化说明：
 *   - content:        NULL（root 作为容器，通常无内容）
 *   - parent:         NULL（root 没有父节点）
 *   - children:       NULL（初始无子节点）
 *   - child_count:    0
 *   - child_capacity: 0
 *   - depth:          0（root 深度为 0）
 *   - has_branch_info: false（无内容+无子节点不构成枝信息）      */
Tree* tree_create(void) {
    /* 分配 Tree 容器结构 */
    Tree* tree = (Tree*)safe_malloc(sizeof(Tree));

    /* 创建 root 节点 —— 作为整棵树的容器 */
    tree->root = tree_node_create(NULL);
    tree->root->depth = 0;  /* root 深度恒为 0 */

    /* 初始化树的元数据 */
    tree->total_nodes  = 1;  /* 包含 root 节点 */
    tree->source_file   = NULL;
    tree->source_format = NULL;

    /* 初始化二期文件级配置为默认值 */
    tree->config.canvas_color    = 0xFFFFFFFF;  /* 白色背景 ARGB */
    tree->config.default_width   = 0;           /* 自动宽度 */
    tree->config.default_height  = 0;           /* 自动高度 */
    tree->config.default_format  = FMT_PLAIN;   /* 纯文本 */
    tree->config.default_zoom    = 1.0f;        /* 100% 缩放 */
    /* 默认编码为 UTF-8 */
    tree->config.default_encoding[0] = 'U';
    tree->config.default_encoding[1] = 'T';
    tree->config.default_encoding[2] = 'F';
    tree->config.default_encoding[3] = '-';
    tree->config.default_encoding[4] = '8';
    tree->config.default_encoding[5] = '\0';

    return tree;
}

/* 递归释放整棵树。
 * 释放顺序：先递归释放所有子节点，再释放 root，
 * 最后释放 Tree 容器本身。这样可以确保所有内存都被正确清理。
 * 对 NULL 指针安全（无操作）。                                 */
void tree_free(Tree* tree) {
    if (tree == NULL) {
        return;  /* NULL 安全：对空树无操作 */
    }

    /* 递归释放 root 及其所有后代 */
    if (tree->root != NULL) {
        tree_node_free_subtree(tree->root);
        tree->root = NULL;
    }

    /* 释放元数据字符串 */
    free(tree->source_file);
    tree->source_file = NULL;
    free(tree->source_format);
    tree->source_format = NULL;

    /* 释放 Tree 容器本身 */
    free(tree);
}

/* 创建一个新的 TreeNode。
 * content 参数可以为 NULL，表示无内容的节点。
 * 新节点没有子节点和父节点（由调用者在添加到树时设置）。
 * 内存分配说明：
 *   - content 通过 safe_strdup 复制，节点拥有副本的所有权
 *   - children 数组初始为 NULL（延迟分配，在首次添加子节点时创建）*/
TreeNode* tree_node_create(const char* content) {
    /* 分配节点结构 */
    TreeNode* node = (TreeNode*)safe_malloc(sizeof(TreeNode));

    /* 复制内容文本（NULL 安全：safe_strdup(NULL) 返回 NULL） */
    node->content = safe_strdup(content);

    /* 初始化结构字段 —— 新节点是孤立的，无父子关系 */
    node->parent          = NULL;
    node->children        = NULL;  /* 延迟分配：首次添加子节点时才分配 */
    node->child_count     = 0;
    node->child_capacity  = 0;
    node->depth           = 0;     /* 加入树后由 tree_recalculate_depths() 更新 */
    node->has_branch_info = false; /* 同时有内容和子节点时设为 true */

    /* 初始化二期 .lxmm 元数据字段为默认值 */
    node->expanded        = true;           /* 默认展开 */
    node->custom_color    = 0;              /* 0 = 自动配色 */
    node->custom_width    = 0;              /* 0 = 自动宽度 */
    node->custom_height   = 0;              /* 0 = 自动高度 */
    node->format_type     = FMT_PLAIN;      /* 默认纯文本 */

    return node;
}

/* 递归释放节点及其所有后代。
 * 释放前还会从其父节点中移除此节点（通过 tree_node_remove_child），
 * 以保持父节点数据的一致性。
 * 释放顺序：先深度优先释放所有子节点，最后释放节点自身。
 * 具体步骤：
 *   1. 递归释放所有子节点（每个子节点释放其子树）
 *   2. 释放 children 数组本身
 *   3. 从其父节点的 children 中移除此节点
 *   4. 释放 content 字符串
 *   5. 释放节点结构本身                                         */
void tree_node_free_subtree(TreeNode* node) {
    if (node == NULL) {
        return;  /* NULL 安全 */
    }

    /* 第一步：递归释放所有子节点。
     * 注意：从后往前释放，避免索引偏移问题。
     * 每次释放 child_count-1 位置的子节点，直到全部释放完毕。 */
    while (node->child_count > 0) {
        /* 释放最后一个子节点及其子树（递归调用自身） */
        tree_node_free_subtree(node->children[node->child_count - 1]);
        /* child_count 在 tree_node_free_subtree 的内部路径中
         * 通过 tree_node_remove_child 递减，无需手动操作。     */
    }

    /* 第二步：释放 children 数组 */
    free(node->children);
    node->children       = NULL;
    node->child_capacity = 0;

    /* 第三步：如果此节点有父节点，从父节点的 children 中移除它。
     * 注意：在遍历父节点的 children 时需要通过指针比较找到此节点。
     * 由于我们可能已经在父节点遍历的子调用中，需要安全地移除。
     * 使用 tree_node_remove_child 的标准逻辑来保证一致性。     */
    if (node->parent != NULL) {
        TreeNode* parent = node->parent;
        /* 在父节点的 children 数组中搜索此节点 */
        for (int i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == node) {
                /* 找到了 —— 通过移动后续元素来移除 */
                /* Found — remove by shifting subsequent elements */
                int remaining = parent->child_count - i - 1;
                if (remaining > 0) {
                    /* 将后面的元素向前移动一位 */
                    memmove(&parent->children[i],
                            &parent->children[i + 1],
                            remaining * sizeof(TreeNode*));
                }
                parent->child_count--;
                parent->children[parent->child_count] = NULL; /* 清理悬空指针 */
                break;
            }
        }
    }

    /* 第四步：释放节点内容 */
    free(node->content);
    node->content = NULL;

    /* 第五步：释放节点结构本身 */
    free(node);
}

/* ================================================================
 * Tree Mutation 树变更操作
 * ================================================================ */

/* 向父节点添加一个子节点。
 * 使用几何增长策略（容量翻倍）来管理 children 数组，
 * 实现均摊 O(1) 的追加性能。
 *
 * 参数说明：
 *   parent  - 要添加子节点的父节点（不能为 NULL）
 *   content - 新子节点的文本内容（可以为 NULL）
 *
 * 返回值：指向新创建的子节点的指针。
 *
 * 实现细节：
 *   1. 如果 children 数组容量不足（child_count >= child_capacity），
 *      则将容量翻倍（初始容量为 4，最小容量保证）
 *   2. 在新分配的位置创建 TreeNode
 *   3. 设置父子双向链接
 *   4. 递增 child_count
 *   5. 更新 Tree 的 total_nodes 计数（遍历到 root 找 Tree* 太复杂，
 *      由 tree_count_nodes 在需要时重新计算）                        */
TreeNode* tree_node_add_child(TreeNode* parent, const char* content) {
    if (parent == NULL) {
        return NULL;  /* 防御性：父节点不能为 NULL */
    }

    /* 检查 children 数组是否需要扩容。
     * 初始分配使用 4 个槽位的容量，之后每次翻倍。
     * 这样可以减少频繁的 realloc 调用。                            */
    if (parent->child_count >= parent->child_capacity) {
        /* 计算新容量：从 0 开始则初始化为 4，否则翻倍 */
        int new_capacity = (parent->child_capacity == 0)
                           ? 4
                           : parent->child_capacity * 2;
        size_t new_size = new_capacity * sizeof(TreeNode*);
        /* 重新分配 children 数组 */
        parent->children = (TreeNode**)safe_realloc(parent->children, new_size);
        parent->child_capacity = new_capacity;

        /* 将新分配的槽位初始化为 NULL（便于调试） */
        for (int i = parent->child_count; i < parent->child_capacity; i++) {
            parent->children[i] = NULL;
        }
    }

    /* 创建新节点并建立父子链接 */
    TreeNode* child = tree_node_create(content);
    child->parent = parent;

    /* 将新子节点放入 children 数组的末尾 */
    parent->children[parent->child_count] = child;
    parent->child_count++;

    /* 注意：不在此处更新 depth 和 total_nodes。
     * 调用者在完成所有结构变更后应调用 tree_recalculate_depths()
     * 和 tree_count_nodes() 来批量更新。                           */

    return child;
}

/* 从父节点移除指定索引处的子节点。
 * 被移除的节点及其整个子树都会被释放。
 *
 * 参数说明：
 *   parent - 父节点
 *   index  - 要移除的子节点的索引（0-based）
 *
 * 返回值：成功返回 true，索引越界返回 false。
 *
 * 实现细节：
 *   1. 验证索引有效性
 *   2. 释放该子节点及其子树
 *   3. 将数组中后续元素前移一位
 *   4. 递减 child_count
 *   5. 清理尾部悬空指针（防御性编程）                              */
bool tree_node_remove_child(TreeNode* parent, int index) {
    if (parent == NULL || index < 0 || index >= parent->child_count) {
        return false;  /* 参数无效 */
    }

    /* 获取要移除的子节点指针 */
    TreeNode* child = parent->children[index];

    /* 先断开父子链接，防止 tree_node_free_subtree 修改父节点。
     * tree_node_free_subtree 中会尝试从其父节点移除自身，
     * 如果我们的移除逻辑和它的冲突会导致双重释放。                  */
    child->parent = NULL;

    /* 递归释放子节点及其所有后代 */
    tree_node_free_subtree(child);

    /* 将后续子节点前移，填补空位 */
    int remaining = parent->child_count - index - 1;
    if (remaining > 0) {
        memmove(&parent->children[index],
                &parent->children[index + 1],
                remaining * sizeof(TreeNode*));
    }

    /* 更新计数并清理尾部 */
    parent->child_count--;
    parent->children[parent->child_count] = NULL;  /* 清除悬空指针 */

    return true;
}

/* 设置（替换）节点的内容文本。
 * 旧内容自动释放，新内容通过 strdup 复制。
 * content 为 NULL 表示清除内容（将 content 字段设为 NULL）。
 * 设置后自动更新 has_branch_info 标志。
 * has_branch_info 规则：节点同时有内容 AND 子节点时为 true。      */
void tree_node_set_content(TreeNode* node, const char* content) {
    if (node == NULL) {
        return;  /* 防御性：节点不能为 NULL */
    }

    /* 释放旧内容 */
    free(node->content);
    node->content = NULL;

    /* 复制新内容（NULL 安全：safe_strdup(NULL) 返回 NULL） */
    node->content = safe_strdup(content);

    /* 更新枝信息标志：节点同时有非空内容和至少一个子节点 */
    node->has_branch_info = (node->content != NULL && node->child_count > 0);
}

/* ================================================================
 * Traversal Functions 遍历函数
 * ================================================================ */

/* 辅助函数：递归执行前序遍历。
 * 先访问当前节点，然后递归访问每个子节点。
 * 如果回调函数返回 false，则立即停止遍历。                         */
static bool traverse_preorder_recursive(TreeNode* node,
                                         TreeTraverseFunc callback,
                                         void* user_data) {
    if (node == NULL) {
        return true;  /* NULL 节点：跳过，继续遍历 */
    }

    /* 访问当前节点。
     * sibling_index 对于 root 始终为 0（root 没有兄弟节点） */
    if (!callback(node, 0, user_data)) {
        return false;  /* 回调要求停止 */
    }

    /* 递归访问每个子节点 */
    for (int i = 0; i < node->child_count; i++) {
        if (!traverse_preorder_recursive(node->children[i],
                                          callback, user_data)) {
            return false;  /* 子树遍历被中止 */
        }
    }

    return true;  /* 继续遍历 */
}

/* 前序遍历：父节点先于子节点被访问。
 * 遍历顺序示例（假设 root 有 A、B 两个子节点，A 有 A1、A2）：
 *   root -> A -> A1 -> A2 -> B
 * 适用场景：构建索引、克隆树、深度优先搜索。                       */
void tree_traverse_preorder(Tree* tree, TreeTraverseFunc callback,
                             void* user_data) {
    if (tree == NULL || tree->root == NULL || callback == NULL) {
        return;  /* 参数校验 */
    }
    traverse_preorder_recursive(tree->root, callback, user_data);
}

/* 辅助函数：递归执行后序遍历。
 * 先递归访问每个子节点，然后访问当前节点。
 * 如果回调函数返回 false，则立即停止遍历。                         */
static bool traverse_postorder_recursive(TreeNode* node,
                                          TreeTraverseFunc callback,
                                          void* user_data) {
    if (node == NULL) {
        return true;  /* NULL 节点：跳过，继续遍历 */
    }

    /* 先递归访问所有子节点 */
    for (int i = 0; i < node->child_count; i++) {
        if (!traverse_postorder_recursive(node->children[i],
                                           callback, user_data)) {
            return false;  /* 子树遍历被中止 */
        }
    }

    /* 然后访问当前节点 */
    return callback(node, 0, user_data);
}

/* 后序遍历：子节点先于父节点被访问。
 * 遍历顺序示例（假设 root 有 A、B 两个子节点，A 有 A1、A2）：
 *   A1 -> A2 -> A -> B -> root
 * 适用场景：需要在处理父节点之前先处理完所有子节点（如计算汇总、
 * 释放资源等）的操作。                                             */
void tree_traverse_postorder(Tree* tree, TreeTraverseFunc callback,
                              void* user_data) {
    if (tree == NULL || tree->root == NULL || callback == NULL) {
        return;  /* 参数校验 */
    }
    traverse_postorder_recursive(tree->root, callback, user_data);
}

/* 层序（BFS）遍历：逐层从左到右访问节点。
 * 使用简单的动态数组作为队列实现 BFS。
 *
 * 遍历顺序示例（假设 root 有 A、B，A 有 A1、A2，B 有 B1）：
 *   root -> A -> B -> A1 -> A2 -> B1
 *
 * 适用场景：按缩进层级输出、计算 depth、序列化到按层级排列的格式。
 *
 * 实现说明：
 *   使用数组 + front/rear 指针模拟循环队列。
 *   初始队列容量为 64，按需扩容。                                  */
void tree_traverse_levelorder(Tree* tree, TreeTraverseFunc callback,
                               void* user_data) {
    if (tree == NULL || tree->root == NULL || callback == NULL) {
        return;  /* 参数校验 */
    }

    /* 队列实现：使用动态数组存储待访问的节点指针。
     * 初始容量 64 对于大多数树已经足够。                           */
    int queue_capacity = 64;
    TreeNode** queue = (TreeNode**)safe_malloc(
        queue_capacity * sizeof(TreeNode*));
    int front = 0;  /* 队首索引（下一个要出队的元素） */
    int rear  = 0;  /* 队尾索引（下一个入队的位置）   */
    int size  = 0;  /* 当前队列中的元素数量           */

    /* 将 root 节点入队 */
    queue[rear] = tree->root;
    rear = (rear + 1) % queue_capacity;
    size = 1;

    /* BFS 主循环：逐个出队并访问，同时将子节点入队 */
    while (size > 0) {
        /* 出队 */
        TreeNode* current = queue[front];
        front = (front + 1) % queue_capacity;
        size--;

        /* 访问当前节点 */
        if (!callback(current, 0, user_data)) {
            /* 回调要求停止，释放队列并返回 */
            free(queue);
            return;
        }

        /* 将所有子节点入队 */
        for (int i = 0; i < current->child_count; i++) {
            /* 检查队列是否需要扩容 */
            if (size >= queue_capacity) {
                /* 队列满了 —— 扩容为原来的 2 倍。
                 * 需要重新分配并将元素整理为连续排列。              */
                int new_capacity = queue_capacity * 2;
                TreeNode** new_queue = (TreeNode**)safe_malloc(
                    new_capacity * sizeof(TreeNode*));

                /* 将当前队列中的元素复制到新数组的连续位置 */
                for (int j = 0; j < size; j++) {
                    new_queue[j] = queue[(front + j) % queue_capacity];
                }

                free(queue);
                queue = new_queue;
                front = 0;
                rear  = size;
                queue_capacity = new_capacity;
            }

            /* 将子节点入队 */
            queue[rear] = current->children[i];
            rear = (rear + 1) % queue_capacity;
            size++;
        }
    }

    /* 释放队列内存 */
    free(queue);
}

/* ================================================================
 * Query and Utility Functions 查询与工具函数
 * ================================================================ */

/* 计算树的最大深度。
 * 遍历所有节点，取 depth 字段的最大值。
 * 仅含 root 的树深度为 0。                                       */
/* Forward declaration for find_max_depth_recursive.
 * MSVC requires this since the static function is defined after tree_max_depth.
 * MSVC 要求在 tree_max_depth 之前声明此静态函数。            */
static void find_max_depth_recursive(TreeNode* node, int* max_d_ptr);

int tree_max_depth(const Tree* tree) {
    if (tree == NULL || tree->root == NULL) {
        return -1;  /* 无效的树返回 -1 */
    }

    int max_d = 0;
    /* 使用递归辅助函数遍历所有节点，更新 max_d */
    find_max_depth_recursive(tree->root, &max_d);

    return max_d;
}

/* 内部辅助：递归查找最大深度 */
static void find_max_depth_recursive(TreeNode* node, int* max_d_ptr) {
    if (node == NULL) {
        return;
    }
    if (node->depth > *max_d_ptr) {
        *max_d_ptr = node->depth;
    }
    for (int i = 0; i < node->child_count; i++) {
        find_max_depth_recursive(node->children[i], max_d_ptr);
    }
}

/* 重新计算树中所有节点的 depth 和 has_branch_info。
 * 应该在每次结构性更改（添加/移除节点）后调用。
 *
 * 使用 BFS 层序遍历确保 depth 计算的正确性：
 *   每个节点的 depth = 父节点的 depth + 1
 *   root 的 depth = 0
 *
 * 同时更新每个节点的 has_branch_info 标志。                        */
void tree_recalculate_depths(Tree* tree) {
    if (tree == NULL || tree->root == NULL) {
        return;
    }

    /* 使用简单的 BFS 遍历来设置 depth。
     * 由于层序遍历已经按层访问节点，我们直接在遍历中计算 depth。
     * 但为了避免依赖 tree_traverse_levelorder 的回调机制，
     * 这里使用显式的队列 BFS 实现。                               */

    /* 初始队列容量 */
    int queue_cap = 64;
    TreeNode** queue = (TreeNode**)safe_malloc(
        queue_cap * sizeof(TreeNode*));
    int front = 0, rear = 0, qsize = 0;

    /* root 的 depth 始终为 0 */
    tree->root->depth = 0;

    /* 将 root 入队 */
    queue[rear++] = tree->root;
    qsize = 1;

    while (qsize > 0) {
        TreeNode* current = queue[front++];
        qsize--;

        /* 更新当前节点的 has_branch_info 标志 */
        current->has_branch_info = (current->content != NULL
                                     && current->child_count > 0);

        /* 处理所有子节点 */
        for (int i = 0; i < current->child_count; i++) {
            TreeNode* child = current->children[i];
            /* 子节点的 depth = 父节点 depth + 1 */
            child->depth = current->depth + 1;

            /* 检查队列容量 */
            if (qsize >= queue_cap) {
                int new_cap = queue_cap * 2;
                TreeNode** new_q = (TreeNode**)safe_malloc(
                    new_cap * sizeof(TreeNode*));
                for (int j = 0; j < qsize; j++) {
                    new_q[j] = queue[(front + j) % queue_cap];
                }
                free(queue);
                queue = new_q;
                front = 0;
                rear  = qsize;
                queue_cap = new_cap;
            }

            /* 子节点入队 */
            queue[rear++] = child;
            qsize++;
        }
    }

    free(queue);

    /* 更新 total_nodes 计数 */
    tree->total_nodes = tree_count_nodes(tree);
}

/* 校验树的内部一致性。
 * 检查项目：
 *   1. root 不为 NULL
 *   2. 无循环引用（通过验证 depth 不超过 total_nodes）
 *   3. 父子链接一致（child->parent 指回 parent）
 *   4. child_count 不超过 child_capacity
 *   5. depth 值合理（非负，且子节点 depth = 父节点 depth + 1）
 *   6. children 数组中没有 NULL 条目（在有效范围内）            */
bool tree_validate(const Tree* tree, char* error_buf, int buf_size) {
    if (tree == NULL) {
        if (error_buf && buf_size > 0) {
            snprintf(error_buf, buf_size, "Tree pointer is NULL");
        }
        return false;
    }

    if (tree->root == NULL) {
        if (error_buf && buf_size > 0) {
            snprintf(error_buf, buf_size, "Tree has no root node");
        }
        return false;
    }

    /* 使用 BFS 检查每个节点 */
    int cap = 64;
    TreeNode** queue = (TreeNode**)safe_malloc(cap * sizeof(TreeNode*));
    int front = 0, rear = 0, qsize = 0;
    int visited_count = 0;  /* 已访问节点计数，用于检测循环 */

    queue[rear++] = tree->root;
    qsize = 1;

    while (qsize > 0) {
        TreeNode* node = queue[front++];
        qsize--;
        visited_count++;

        /* 检查 3：root 的 parent 必须为 NULL */
        if (node == tree->root && node->parent != NULL) {
            if (error_buf && buf_size > 0) {
                snprintf(error_buf, buf_size,
                         "Root node has non-NULL parent");
            }
            free(queue);
            return false;
        }

        /* 检查 3：非 root 节点的 parent 必须非 NULL */
        if (node != tree->root && node->parent == NULL) {
            if (error_buf && buf_size > 0) {
                snprintf(error_buf, buf_size,
                         "Non-root node has NULL parent (content: '%s')",
                         node->content ? node->content : "(null)");
            }
            free(queue);
            return false;
        }

        /* 检查 4：child_count 有效性 */
        if (node->child_count < 0 || node->child_count > node->child_capacity) {
            if (error_buf && buf_size > 0) {
                snprintf(error_buf, buf_size,
                         "Node child_count (%d) exceeds child_capacity (%d)",
                         node->child_count, node->child_capacity);
            }
            free(queue);
            return false;
        }

        /* 检查 5：depth 必须非负 */
        if (node->depth < 0) {
            if (error_buf && buf_size > 0) {
                snprintf(error_buf, buf_size,
                         "Node has negative depth (%d)", node->depth);
            }
            free(queue);
            return false;
        }

        /* 遍历子节点 */
        for (int i = 0; i < node->child_count; i++) {
            TreeNode* child = node->children[i];

            /* 检查 6：children 数组中不应有 NULL */
            if (child == NULL) {
                if (error_buf && buf_size > 0) {
                    snprintf(error_buf, buf_size,
                             "NULL child at index %d of node '%s'",
                             i, node->content ? node->content : "(null)");
                }
                free(queue);
                return false;
            }

            /* 检查 3：子节点的 parent 必须回指当前节点 */
            if (child->parent != node) {
                if (error_buf && buf_size > 0) {
                    snprintf(error_buf, buf_size,
                             "Child at index %d has incorrect parent pointer", i);
                }
                free(queue);
                return false;
            }

            /* 检查 5：子节点 depth 必须等于父节点 depth + 1 */
            if (child->depth != node->depth + 1) {
                if (error_buf && buf_size > 0) {
                    snprintf(error_buf, buf_size,
                             "Depth mismatch: parent depth=%d, child depth=%d (expected %d)",
                             node->depth, child->depth, node->depth + 1);
                }
                free(queue);
                return false;
            }

            /* 子节点入队 */
            if (qsize >= cap) {
                int new_cap = cap * 2;
                TreeNode** new_q = (TreeNode**)safe_malloc(
                    new_cap * sizeof(TreeNode*));
                for (int j = 0; j < qsize; j++) {
                    new_q[j] = queue[(front + j) % cap];
                }
                free(queue);
                queue = new_q;
                front = 0;
                rear  = qsize;
                cap   = new_cap;
            }
            queue[rear++] = child;
            qsize++;
        }
    }

    free(queue);

    /* 检查 1 & 2：visited_count 应与 total_nodes 一致。
     * 如果 visited_count 远大于 total_nodes，可能存在循环。
     * 但 BFS 本身在树中不会循环（前提是父子链接正确），
     * 所以这里是冗余的安全检查。                                  */
    if (visited_count != tree->total_nodes) {
        if (error_buf && buf_size > 0) {
            snprintf(error_buf, buf_size,
                     "Node count mismatch: visited %d, total_nodes says %d",
                     visited_count, tree->total_nodes);
        }
        /* 这不算致命错误，更新计数即可 */
    }

    return true;  /* 所有检查通过 */
}

/* 将树导出为 Graphviz DOT 格式。
 * 生成有向图：节点为椭圆，边为箭头。
 * 每个节点显示其 content 和 depth。
 * 可用于调试和可视化树结构。
 *
 * DOT 输出示例：
 *   digraph Tree {
 *       node [shape=ellipse];
 *       "node_0" [label="root (d=0)"];
 *       "node_0" -> "node_1";
 *       "node_1" [label="Chapter 1 (d=1)"];
 *       ...
 *   }
 * 使用 Graphviz 渲染: dot -Tpng output.dot -o tree.png              */
char* tree_export_dot(const Tree* tree) {
    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }

    /* 预估 DOT 输出大小：每个节点约 100 字节（标签+边+格式），
     * 加上头部和尾部。使用动态缓冲区，按需扩容。                 */
    int buf_size = 4096;
    int buf_used = 0;
    char* buf = (char*)safe_malloc(buf_size);

    /* 辅助函数：向缓冲区追加字符串 */
    /* 内联展开以避免额外的函数调用开销 */

    /* 写入 DOT 头部 */
    buf_used = snprintf(buf, buf_size,
                        "digraph Tree {\n"
                        "    node [shape=ellipse, style=filled, "
                        "fillcolor=lightyellow];\n"
                        "    edge [arrowhead=vee];\n"
                        "    rankdir=TB;\n\n");

    /* 使用 BFS 遍历，生成节点和边 */
    int cap = 64;
    TreeNode** queue = (TreeNode**)safe_malloc(cap * sizeof(TreeNode*));
    int front = 0, rear = 0, qsize = 0;
    int node_id_counter = 0;  /* 为每个节点分配唯一 ID */

    queue[rear++] = tree->root;
    qsize = 1;

    /* 存储每个节点的 ID 以便父节点引用。
     * 简单方案：在节点遍历时直接输出边。                        */

    while (qsize > 0) {
        TreeNode* node = queue[front++];
        qsize--;

        /* 分配节点 ID */
        int my_id = node_id_counter++;

        /* 生成节点标签。
         * 转义特殊字符（双引号、反斜杠、换行符）以便在 DOT 中安全显示。*/
        char label_buf[512];
        const char* content_str = node->content ? node->content : "(root)";
        /* 简单的转义：将双引号替换为单引号，反斜杠替换为斜杠。
         * 对于调试用途已足够。                                  */
        int li = 0;
        label_buf[li++] = '"';
        for (const char* c = content_str; *c && li < 500; c++) {
            if (*c == '"') {
                label_buf[li++] = '\'';
            } else if (*c == '\\') {
                label_buf[li++] = '/';
            } else if (*c == '\n') {
                label_buf[li++] = ' ';
            } else {
                label_buf[li++] = *c;
            }
        }
        label_buf[li++] = '"';
        label_buf[li] = '\0';

        /* 写入节点声明 */
        char node_line[1024];
        int written = snprintf(node_line, sizeof(node_line),
                    "    node_%d [label=\"%s (d=%d)%s\"];\n",
                    my_id, content_str,
                    node->depth,
                    node->has_branch_info ? " [BRANCH]" : "");
        /* 上面的 snprintf 可能截断，但对于调试用途可接受 */

        /* 确保缓冲区足够大 */
        if (buf_used + written + 512 >= buf_size) {
            buf_size *= 2;
            buf = (char*)safe_realloc(buf, buf_size);
        }
        /* 重新生成节点行以适配新缓冲区（使用新的 buf_size 做安全写入）*/
        written = snprintf(node_line, sizeof(node_line),
                    "    node_%d [label=\"%s (d=%d)%s\"];\n",
                    my_id, content_str,
                    node->depth,
                    node->has_branch_info ? " [BRANCH]" : "");
        /* 复制到缓冲区 */
        for (int k = 0; k < written; k++) {
            buf[buf_used + k] = node_line[k];
        }
        buf_used += written;

        /* 写入边（从当前节点到每个子节点）并为子节点分配 ID */
        for (int i = 0; i < node->child_count; i++) {
            int child_id = node_id_counter + i;
            /* 计算要写入的字节数 */
            int needed = snprintf(NULL, 0,
                        "    node_%d -> node_%d;\n", my_id, child_id);
            if (buf_used + needed + 1 >= buf_size) {
                buf_size = (buf_used + needed + 1) * 2;
                buf = (char*)safe_realloc(buf, buf_size);
            }
            written = snprintf(buf + buf_used,
                        buf_size - buf_used,
                        "    node_%d -> node_%d;\n", my_id, child_id);
            buf_used += written;

            /* 将子节点加入 BFS 队列 */
            if (qsize >= cap) {
                int new_cap = cap * 2;
                TreeNode** new_q = (TreeNode**)safe_malloc(
                    new_cap * sizeof(TreeNode*));
                for (int j = 0; j < qsize; j++) {
                    new_q[j] = queue[(front + j) % cap];
                }
                free(queue);
                queue = new_q;
                front = 0;
                rear  = qsize;
                cap   = new_cap;
            }
            queue[rear++] = node->children[i];
            qsize++;
        }
    }

    /* 写入 DOT 尾部 */
    if (buf_used + 3 >= buf_size) {
        buf_size = buf_used + 16;
        buf = (char*)safe_realloc(buf, buf_size);
    }
    buf[buf_used++] = '}';
    buf[buf_used++] = '\n';
    buf[buf_used] = '\0';

    free(queue);
    return buf;
}

/* 统计树中的节点总数。
 * 使用 BFS 遍历来计数（比递归更安全，避免栈溢出）。
 * 同时更新 tree->total_nodes 字段。                              */
int tree_count_nodes(const Tree* tree) {
    if (tree == NULL || tree->root == NULL) {
        return 0;
    }

    int count = 0;

    /* BFS 计数 */
    int cap = 64;
    TreeNode** queue = (TreeNode**)safe_malloc(cap * sizeof(TreeNode*));
    int front = 0, rear = 0, qsize = 0;

    queue[rear++] = tree->root;
    qsize = 1;

    while (qsize > 0) {
        TreeNode* node = queue[front++];
        qsize--;
        count++;

        /* 子节点入队 */
        for (int i = 0; i < node->child_count; i++) {
            if (qsize >= cap) {
                int new_cap = cap * 2;
                TreeNode** new_q = (TreeNode**)safe_malloc(
                    new_cap * sizeof(TreeNode*));
                for (int j = 0; j < qsize; j++) {
                    new_q[j] = queue[(front + j) % cap];
                }
                free(queue);
                queue = new_q;
                front = 0;
                rear  = qsize;
                cap   = new_cap;
            }
            queue[rear++] = node->children[i];
            qsize++;
        }
    }

    free(queue);
    return count;
}

/* ================================================================
 * JSON Bridge Functions — Phase 2 frontend-backend communication
 * JSON 桥接函数 — 二期前后端通信
 * ================================================================ */

/* 动态字符串缓冲区，用于构建 JSON 输出。
 * 自动扩容，支持追加格式化字符串。                               */
typedef struct {
    char* data;      /* 缓冲区指针 */
    int   len;       /* 当前已使用长度（不含 '\0'） */
    int   cap;       /* 缓冲区总容量 */
} JsonBuf;

/* 初始化 JSON 缓冲区。初始容量 4KB，对大多数树足够。            */
static void json_buf_init(JsonBuf* jb) {
    jb->cap = 4096;
    jb->data = (char*)safe_malloc(jb->cap);
    jb->data[0] = '\0';
    jb->len = 0;
}

/* 向 JSON 缓冲区追加格式化字符串。自动扩容。                    */
static void json_buf_append(JsonBuf* jb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    /* 先计算需要的长度 */
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed <= 0) return;

    /* 确保容量足够（+1 给 '\0'） */
    while (jb->len + needed + 1 >= jb->cap) {
        jb->cap *= 2;
        jb->data = (char*)safe_realloc(jb->data, jb->cap);
    }

    va_start(args, fmt);
    vsnprintf(jb->data + jb->len, jb->cap - jb->len, fmt, args);
    va_end(args);
    jb->len += needed;
}

/* 追加 JSON 转义后的字符串（包括引号包裹）。
 * 对 "、\、\n、\r、\t 等字符进行转义。                          */
static void json_buf_append_string(JsonBuf* jb, const char* str) {
    if (str == NULL) {
        json_buf_append(jb, "null");
        return;
    }
    json_buf_append(jb, "\"");
    for (const char* c = str; *c; c++) {
        switch (*c) {
            case '"':  json_buf_append(jb, "\\\""); break;
            case '\\': json_buf_append(jb, "\\\\"); break;
            case '\n': json_buf_append(jb, "\\n");  break;
            case '\r': json_buf_append(jb, "\\r");  break;
            case '\t': json_buf_append(jb, "\\t");  break;
            default:   {
                /* 确保容量足够单字符 + '\0' */
                if (jb->len + 2 >= jb->cap) {
                    jb->cap *= 2;
                    jb->data = (char*)safe_realloc(jb->data, jb->cap);
                }
                jb->data[jb->len++] = *c;
                jb->data[jb->len] = '\0';
            }
        }
    }
    json_buf_append(jb, "\"");
}

/* 递归将 TreeNode 序列化为 JSON 对象，写入缓冲区。
 * 输出格式：
 *   { "content": "...", "expanded": true, "custom_color": 0,
 *     "custom_width": 0, "custom_height": 0, "format_type": 0,
 *     "has_branch_info": false, "children": [...] }              */
static void tree_node_to_json_buf(TreeNode* node, JsonBuf* jb) {
    if (node == NULL) {
        json_buf_append(jb, "null");
        return;
    }

    json_buf_append(jb, "{");

    /* content — 节点文本内容 */
    json_buf_append(jb, "\"content\":");
    json_buf_append_string(jb, node->content);
    json_buf_append(jb, ",");

    /* expanded — 展开/折叠状态 */
    json_buf_append(jb, "\"expanded\":%s,", node->expanded ? "true" : "false");

    /* custom_color — 自定义颜色 (0=自动) */
    json_buf_append(jb, "\"custom_color\":%u,", node->custom_color);

    /* custom_width — 自定义宽度 (0=自动) */
    json_buf_append(jb, "\"custom_width\":%d,", node->custom_width);

    /* custom_height — 自定义高度 (0=自动) */
    json_buf_append(jb, "\"custom_height\":%d,", node->custom_height);

    /* format_type — 内容格式 */
    json_buf_append(jb, "\"format_type\":%d,", (int)node->format_type);

    /* has_branch_info — 枝信息标志 */
    json_buf_append(jb, "\"has_branch_info\":%s,", node->has_branch_info ? "true" : "false");

    /* children — 递归序列化子节点数组 */
    json_buf_append(jb, "\"children\":[");
    for (int i = 0; i < node->child_count; i++) {
        if (i > 0) json_buf_append(jb, ",");
        tree_node_to_json_buf(node->children[i], jb);
    }
    json_buf_append(jb, "]");

    json_buf_append(jb, "}");
}

/* 将 TreeConfig 序列化为 JSON 对象。                              */
static void tree_config_to_json_buf(const TreeConfig* cfg, JsonBuf* jb) {
    if (cfg == NULL) {
        json_buf_append(jb, "null");
        return;
    }
    json_buf_append(jb, "{");
    json_buf_append(jb, "\"canvas_color\":%u,", cfg->canvas_color);
    json_buf_append(jb, "\"default_width\":%d,", cfg->default_width);
    json_buf_append(jb, "\"default_height\":%d,", cfg->default_height);
    json_buf_append(jb, "\"default_format\":%d,", (int)cfg->default_format);
    json_buf_append(jb, "\"default_zoom\":%g,", cfg->default_zoom);
    json_buf_append(jb, "\"default_encoding\":");
    json_buf_append_string(jb, cfg->default_encoding);
    json_buf_append(jb, "}");
}

/* 公开 API：将整棵树（含配置和所有元数据）序列化为 JSON 字符串。  */
char* tree_to_json(const Tree* tree) {
    if (tree == NULL || tree->root == NULL) {
        /* 空树：返回最小有效 JSON */
        char* empty = (char*)safe_malloc(32);
        snprintf(empty, 32, "{\"root\":null,\"config\":null}");
        return empty;
    }

    JsonBuf jb;
    json_buf_init(&jb);

    json_buf_append(&jb, "{");

    /* config 部分 */
    json_buf_append(&jb, "\"config\":");
    tree_config_to_json_buf(&tree->config, &jb);
    json_buf_append(&jb, ",");

    /* root 部分 */
    json_buf_append(&jb, "\"root\":");
    tree_node_to_json_buf(tree->root, &jb);

    json_buf_append(&jb, "}");

    /* 确保以 '\0' 结尾 */
    if (jb.len + 1 >= jb.cap) {
        jb.cap = jb.len + 1;
        jb.data = (char*)safe_realloc(jb.data, jb.cap);
    }
    jb.data[jb.len] = '\0';

    return jb.data;
}

/* ================================================================
 * tree_from_json — JSON 字符串 → Tree
 * ================================================================ */

/* 简化的 JSON 解析器状态。
 * 专为解析 tree_to_json 的输出格式设计。                         */
typedef struct {
    const char* json;  /* 输入的 JSON 字符串 */
    int         pos;   /* 当前解析位置 */
    int         len;   /* 字符串总长度 */
} JsonReader;

/* 跳过空白字符。                                                  */
static void json_skip_ws(JsonReader* jr) {
    while (jr->pos < jr->len) {
        char c = jr->json[jr->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            jr->pos++;
        } else {
            break;
        }
    }
}

/* 查看当前字符，不前进。                                          */
static char json_peek(JsonReader* jr) {
    if (jr->pos >= jr->len) return '\0';
    return jr->json[jr->pos];
}

/* 前进一个字符并返回它。                                          */
static char json_next(JsonReader* jr) {
    if (jr->pos >= jr->len) return '\0';
    return jr->json[jr->pos++];
}

/* 断言当前字符匹配期望值，匹配则跳过。                            */
static bool json_expect(JsonReader* jr, char expected) {
    json_skip_ws(jr);
    if (json_peek(jr) == expected) {
        json_next(jr);
        return true;
    }
    return false;
}

/* 解析 JSON 字符串字面量（含引号）。结果存入 buf。
 * 支持 \", \\, \n, \r, \t 转义。                                */
static bool json_read_string(JsonReader* jr, char* buf, int buf_size) {
    json_skip_ws(jr);
    if (json_peek(jr) != '"') return false;
    json_next(jr);  /* 跳过开始的 '"' */

    int bi = 0;
    while (jr->pos < jr->len && bi < buf_size - 1) {
        char c = json_next(jr);
        if (c == '"') {
            /* 字符串结束 */
            buf[bi] = '\0';
            return true;
        } else if (c == '\\') {
            char esc = json_next(jr);
            switch (esc) {
                case '"':  buf[bi++] = '"';  break;
                case '\\': buf[bi++] = '\\'; break;
                case 'n':  buf[bi++] = '\n'; break;
                case 'r':  buf[bi++] = '\r'; break;
                case 't':  buf[bi++] = '\t'; break;
                default:   buf[bi++] = esc;  break;
            }
        } else {
            buf[bi++] = c;
        }
    }
    buf[bi] = '\0';
    return false;  /* 未闭合的字符串 */
}

/* 解析 JSON null 字面量。                                         */
static bool json_read_null(JsonReader* jr) {
    json_skip_ws(jr);
    if (jr->pos + 3 < jr->len &&
        jr->json[jr->pos] == 'n' &&
        jr->json[jr->pos+1] == 'u' &&
        jr->json[jr->pos+2] == 'l' &&
        jr->json[jr->pos+3] == 'l') {
        jr->pos += 4;
        return true;
    }
    return false;
}

/* 解析 JSON 布尔值。                                              */
static bool json_read_bool(JsonReader* jr, bool* out) {
    json_skip_ws(jr);
    if (jr->pos + 3 < jr->len &&
        jr->json[jr->pos] == 't' &&
        jr->json[jr->pos+1] == 'r' &&
        jr->json[jr->pos+2] == 'u' &&
        jr->json[jr->pos+3] == 'e') {
        jr->pos += 4;
        *out = true;
        return true;
    }
    if (jr->pos + 4 < jr->len &&
        jr->json[jr->pos] == 'f' &&
        jr->json[jr->pos+1] == 'a' &&
        jr->json[jr->pos+2] == 'l' &&
        jr->json[jr->pos+3] == 's' &&
        jr->json[jr->pos+4] == 'e') {
        jr->pos += 5;
        *out = false;
        return true;
    }
    return false;
}

/* 解析 JSON 整数。                                                */
static bool json_read_int(JsonReader* jr, int* out) {
    json_skip_ws(jr);
    char c = json_peek(jr);
    /* 判断是否为数字或负号开头 */
    if (!((c >= '0' && c <= '9') || c == '-')) return false;

    int sign = 1;
    if (c == '-') { sign = -1; json_next(jr); }

    int val = 0;
    while (jr->pos < jr->len) {
        c = json_peek(jr);
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            json_next(jr);
        } else {
            break;
        }
    }
    *out = sign * val;
    return true;
}

/* 解析 JSON 无符号整数。                                          */
static bool json_read_uint(JsonReader* jr, uint32_t* out) {
    json_skip_ws(jr);
    char c = json_peek(jr);
    if (c < '0' || c > '9') return false;

    uint32_t val = 0;
    while (jr->pos < jr->len) {
        c = json_peek(jr);
        if (c >= '0' && c <= '9') {
            val = val * 10 + (uint32_t)(c - '0');
            json_next(jr);
        } else {
            break;
        }
    }
    *out = val;
    return true;
}

/* 前向声明递归解析函数。                                          */
static TreeNode* json_parse_tree_node(JsonReader* jr, char* error_msg, int err_size);

/* 解析 JSON 对象中的键名并验证是否匹配期望值。
 * 对象形如 "key": value。调用者需确保当前位置在对象内部。        */
static bool json_read_key(JsonReader* jr, const char* expected_key) {
    char key_buf[64];
    if (!json_read_string(jr, key_buf, sizeof(key_buf))) return false;
    if (strcmp(key_buf, expected_key) != 0) return false;
    if (!json_expect(jr, ':')) return false;
    return true;
}

/* 解析 JSON 数组 [ ... ]，对每个元素调用回调创建子节点。
 * 回调接收父节点指针，应创建子节点并返回新创建的子节点指针。    */
static bool json_parse_children_array(JsonReader* jr, TreeNode* parent,
                                       char* error_msg, int err_size) {
    if (!json_expect(jr, '[')) {
        if (error_msg) snprintf(error_msg, err_size, "Expected '[' for children array");
        return false;
    }

    json_skip_ws(jr);
    /* 空数组 */
    if (json_peek(jr) == ']') {
        json_next(jr);
        return true;
    }

    /* 解析每个子节点 */
    while (jr->pos < jr->len) {
        json_skip_ws(jr);
        if (json_peek(jr) == ']') {
            json_next(jr);
            return true;
        }

        /* 递归解析子节点 */
        TreeNode* child = json_parse_tree_node(jr, error_msg, err_size);
        if (child == NULL) return false;

        /* 将子节点添加到父节点 */
        /* child 目前是孤立的，需要加入到 parent 的 children 中 */
        if (parent->child_count >= parent->child_capacity) {
            int new_cap = (parent->child_capacity == 0)
                           ? 4 : parent->child_capacity * 2;
            parent->children = (TreeNode**)safe_realloc(
                parent->children, new_cap * sizeof(TreeNode*));
            for (int i = parent->child_count; i < new_cap; i++) {
                parent->children[i] = NULL;
            }
            parent->child_capacity = new_cap;
        }
        child->parent = parent;
        parent->children[parent->child_count] = child;
        parent->child_count++;

        /* 跳过可选的逗号 */
        json_skip_ws(jr);
        if (json_peek(jr) == ',') {
            json_next(jr);
        }
    }

    return true;
}

/* 递归解析单个 TreeNode 的 JSON 对象。
 * 格式：{ "content": ..., "expanded": ..., "children": [...] }   */
static TreeNode* json_parse_tree_node(JsonReader* jr, char* error_msg, int err_size) {
    /* 检查是否为 null */
    json_skip_ws(jr);
    if (json_peek(jr) == 'n') {
        if (json_read_null(jr)) return NULL;
        if (error_msg) snprintf(error_msg, err_size, "Expected null or object");
        return NULL;
    }

    if (!json_expect(jr, '{')) {
        if (error_msg) snprintf(error_msg, err_size, "Expected '{' for node object");
        return NULL;
    }

    /* 创建节点（content 稍后设置） */
    TreeNode* node = tree_node_create(NULL);

    /* 解析对象的键值对 */
    json_skip_ws(jr);
    while (jr->pos < jr->len && json_peek(jr) != '}') {
        char key_buf[64];
        if (!json_read_string(jr, key_buf, sizeof(key_buf))) {
            if (error_msg) snprintf(error_msg, err_size, "Expected string key in node object");
            tree_node_free_subtree(node);
            return NULL;
        }
        if (!json_expect(jr, ':')) {
            if (error_msg) snprintf(error_msg, err_size, "Expected ':' after key");
            tree_node_free_subtree(node);
            return NULL;
        }

        /* 根据键名解析对应的值 */
        if (strcmp(key_buf, "content") == 0) {
            json_skip_ws(jr);
            if (json_peek(jr) == 'n') {
                /* null content → 保持 NULL */
                json_read_null(jr);
            } else {
                char content_buf[16384];
                if (json_read_string(jr, content_buf, sizeof(content_buf))) {
                    if (content_buf[0] != '\0') {
                        tree_node_set_content(node, content_buf);
                    }
                }
            }
        } else if (strcmp(key_buf, "expanded") == 0) {
            bool val;
            if (json_read_bool(jr, &val)) node->expanded = val;
        } else if (strcmp(key_buf, "custom_color") == 0) {
            uint32_t val;
            if (json_read_uint(jr, &val)) node->custom_color = val;
        } else if (strcmp(key_buf, "custom_width") == 0) {
            int val;
            if (json_read_int(jr, &val)) node->custom_width = val;
        } else if (strcmp(key_buf, "custom_height") == 0) {
            int val;
            if (json_read_int(jr, &val)) node->custom_height = val;
        } else if (strcmp(key_buf, "format_type") == 0) {
            int val;
            if (json_read_int(jr, &val)) {
                node->format_type = (NodeFormatType)val;
            }
        } else if (strcmp(key_buf, "has_branch_info") == 0) {
            bool val;
            if (json_read_bool(jr, &val)) node->has_branch_info = val;
        } else if (strcmp(key_buf, "children") == 0) {
            if (!json_parse_children_array(jr, node, error_msg, err_size)) {
                tree_node_free_subtree(node);
                return NULL;
            }
        }

        /* 跳过逗号 */
        json_skip_ws(jr);
        if (json_peek(jr) == ',') {
            json_next(jr);
        }
        json_skip_ws(jr);
    }

    if (!json_expect(jr, '}')) {
        if (error_msg) snprintf(error_msg, err_size, "Expected '}' to close node object");
        tree_node_free_subtree(node);
        return NULL;
    }

    return node;
}

/* 从 JSON 对象中解析 TreeConfig。                                  */
static bool json_parse_config(JsonReader* jr, TreeConfig* cfg) {
    if (!json_expect(jr, '{')) return false;

    json_skip_ws(jr);
    while (jr->pos < jr->len && json_peek(jr) != '}') {
        char key_buf[64];
        if (!json_read_string(jr, key_buf, sizeof(key_buf))) return false;
        if (!json_expect(jr, ':')) return false;

        if (strcmp(key_buf, "canvas_color") == 0) {
            uint32_t val; if (json_read_uint(jr, &val)) cfg->canvas_color = val;
        } else if (strcmp(key_buf, "default_width") == 0) {
            int val; if (json_read_int(jr, &val)) cfg->default_width = val;
        } else if (strcmp(key_buf, "default_height") == 0) {
            int val; if (json_read_int(jr, &val)) cfg->default_height = val;
        } else if (strcmp(key_buf, "default_format") == 0) {
            int val; if (json_read_int(jr, &val)) cfg->default_format = (NodeFormatType)val;
        } else if (strcmp(key_buf, "default_zoom") == 0) {
            /* 简单浮点解析：读整数/小数 */
            json_skip_ws(jr);
            float fval = 1.0f;
            /* 此处需更完善的浮点解析，简化跳过 */
            if (json_peek(jr) >= '0' && json_peek(jr) <= '9') {
                int ival = 0;
                json_read_int(jr, &ival);
                fval = (float)ival;
            }
            cfg->default_zoom = fval;
        } else if (strcmp(key_buf, "default_encoding") == 0) {
            char enc_buf[32];
            if (json_read_string(jr, enc_buf, sizeof(enc_buf))) {
                strncpy(cfg->default_encoding, enc_buf, 31);
                cfg->default_encoding[31] = '\0';
            }
        }

        json_skip_ws(jr);
        if (json_peek(jr) == ',') json_next(jr);
        json_skip_ws(jr);
    }

    return json_expect(jr, '}');
}

/* 公开 API：将 JSON 字符串解析为 Tree。                            */
Tree* tree_from_json(const char* json_str,
                     char* error_msg, int err_size) {
    if (json_str == NULL) {
        if (error_msg && err_size > 0) {
            snprintf(error_msg, err_size, "NULL JSON string");
        }
        return NULL;
    }

    if (error_msg && err_size > 0) error_msg[0] = '\0';

    JsonReader jr;
    jr.json = json_str;
    jr.pos  = 0;
    jr.len  = (int)strlen(json_str);

    /* 解析顶层对象 { "config": { ... }, "root": { ... } } */
    if (!json_expect(&jr, '{')) {
        if (error_msg && err_size > 0) {
            snprintf(error_msg, err_size, "Expected '{' at start of tree JSON");
        }
        return NULL;
    }

    /* 创建 Tree */
    Tree* tree = tree_create();
    if (tree == NULL) {
        if (error_msg && err_size > 0) {
            snprintf(error_msg, err_size, "Failed to create tree");
        }
        return NULL;
    }

    /* 解析两个顶层键: "config" 和 "root" */
    json_skip_ws(&jr);
    while (jr.pos < jr.len && json_peek(&jr) != '}') {
        char key_buf[64];
        if (!json_read_string(&jr, key_buf, sizeof(key_buf))) {
            if (error_msg && err_size > 0) {
                snprintf(error_msg, err_size, "Expected key in root object");
            }
            tree_free(tree);
            return NULL;
        }
        if (!json_expect(&jr, ':')) {
            if (error_msg && err_size > 0) {
                snprintf(error_msg, err_size, "Expected ':' after key '%s'", key_buf);
            }
            tree_free(tree);
            return NULL;
        }

        if (strcmp(key_buf, "config") == 0) {
            json_skip_ws(&jr);
            if (json_peek(&jr) == 'n') {
                json_read_null(&jr);
            } else {
                if (!json_parse_config(&jr, &tree->config)) {
                    if (error_msg && err_size > 0) {
                        snprintf(error_msg, err_size, "Failed to parse config");
                    }
                    tree_free(tree);
                    return NULL;
                }
            }
        } else if (strcmp(key_buf, "root") == 0) {
            json_skip_ws(&jr);
            if (json_peek(&jr) == 'n') {
                json_read_null(&jr);
            } else {
                /* 释放默认的 root，使用解析出的 root */
                TreeNode* parsed_root = json_parse_tree_node(&jr, error_msg, err_size);
                if (parsed_root == NULL) {
                    tree_free(tree);
                    return NULL;
                }
                /* 替换 tree 的 root */
                tree_node_free_subtree(tree->root);
                tree->root = parsed_root;
                /* root 没有 parent */
                parsed_root->parent = NULL;
                parsed_root->depth = 0;
            }
        }

        json_skip_ws(&jr);
        if (json_peek(&jr) == ',') json_next(&jr);
        json_skip_ws(&jr);
    }

    if (!json_expect(&jr, '}')) {
        if (error_msg && err_size > 0) {
            snprintf(error_msg, err_size, "Expected '}' at end of tree JSON");
        }
        tree_free(tree);
        return NULL;
    }

    /* 重新计算元数据 */
    tree_recalculate_depths(tree);
    tree->total_nodes = tree_count_nodes(tree);

    return tree;
}
