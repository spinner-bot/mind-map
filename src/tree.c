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
#include <stdio.h>    /* snprintf, sprintf */

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
