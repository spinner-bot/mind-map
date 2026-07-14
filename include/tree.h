/* ============================================================
 * tree.h - Core Tree Data Structures and Operations
 * tree.h - 核心树数据结构与操作
 *
 * This module defines the universal intermediate tree representation
 * used by all format handlers. A Tree consists of TreeNode nodes
 * arranged in a parent-child hierarchy. Each node may hold text
 * content and an arbitrary number of child nodes.
 * 本模块定义了所有格式处理器共用的通用中间树表示。
 * 一棵树由 TreeNode 节点按父子层级组成。每个节点可持有
 * 文本内容和任意数量的子节点。
 *
 * Design principle 设计原则:
 *   - Memory ownership is explicit: content and children arrays
 *     are always heap-allocated and owned by their parent node.
 *     内存所有权明确：content 和 children 数组始终从堆分配，
 *     由其父节点拥有。
 *   - NULL content is valid and means "no text" (not "empty string").
 *     NULL content 是合法的，表示"无文本"（而非空字符串）。
 *   - The root node is a container: its content is typically NULL
 *     but may hold a document title.
 *     root 节点是容器：其 content 通常为 NULL，但可持有文档标题。
 * ============================================================ */

#ifndef TREE_H
#define TREE_H

#include <stdbool.h>   /* bool, true, false */
#include <stddef.h>    /* size_t */

/* ----------------------------------------------------------------
 * Data Structures 数据结构
 * ---------------------------------------------------------------- */

/* TreeNode: A single node in the intermediate tree representation.
 * TreeNode: 中间树表示中的单个节点。
 *
 * A node represents one "item" in the tree (a heading, a list item,
 * an outline entry, etc.). It has text content and may have children.
 * 一个节点代表树中的一个"条目"（标题、列表项、大纲条目等）。
 * 它有文本内容，并可以有子节点。
 *
 * When content != NULL and child_count > 0 simultaneously,
 * has_branch_info is set to TRUE — this is critical for detecting
 * information that could be lost in formats that don't support
 * branch-attached data (e.g., some JSON representations).
 * 当 content != NULL 且 child_count > 0 同时成立时，
 * has_branch_info 设为 TRUE — 这对检测可能在
 * 不支持枝干数据的格式中丢失的信息至关重要。                */
typedef struct TreeNode {
    /* Node's text content. Heap-allocated string, may be NULL.
     * 节点的文本内容。堆分配的字符串，可以为 NULL。           */
    char*   content;

    /* Pointer to parent node. NULL only for the root node.
     * 指向父节点的指针。仅 root 节点为 NULL。                */
    struct TreeNode* parent;

    /* Dynamic array of child node pointers.
     * 子节点指针的动态数组。                                 */
    struct TreeNode** children;

    /* Number of valid child pointers in the children array.
     * children 数组中有效子节点指针的数量。                   */
    int     child_count;

    /* Allocated capacity of the children array.
     * children 数组的已分配容量。                             */
    int     child_capacity;

    /* Depth from root (root = 0). Maintained automatically
     * by tree_recalculate_depths(). 从 root 开始的深度
     * (root = 0)。由 tree_recalculate_depths() 自动维护。    */
    int     depth;

    /* TRUE when this node has BOTH non-NULL content AND at
     * least one child. Set during parsing; used by serializers
     * to decide whether branch-info-loss warnings are needed.
     * 当节点同时有非 NULL 的 content 和至少一个子节点时为 TRUE。
     * 在解析时设置；序列化器用它判断是否需要枝信息丢失警告。  */
    bool    has_branch_info;
} TreeNode;

/* Tree: Container holding the entire tree plus metadata.
 * Tree: 容纳整棵树及其元数据的容器。                          */
typedef struct Tree {
    /* Root node. Never NULL after tree_create().
     * Root 节点。在 tree_create() 之后永远不为 NULL。        */
    TreeNode* root;

    /* Total count of all nodes in the tree, including root.
     * 树中所有节点的总数，包括 root。                        */
    int       total_nodes;

    /* Originating file path (set after parse from file),
     * or NULL if the tree was created programmatically.
     * 来源文件路径（从文件解析后设置），
     * 如果是通过编程创建的树则为 NULL。                      */
    char*     source_file;

    /* Originating format name (e.g., "json", "txt", "md"),
     * or NULL. 来源格式名称（如 "json"、"txt"、"md"），
     * 或为 NULL。                                          */
    char*     source_format;
} Tree;

/* ----------------------------------------------------------------
 * Traversal callback type 遍历回调类型
 * ---------------------------------------------------------------- */

/* Callback function type for tree traversal.
 * 树遍历的回调函数类型。
 *
 * Parameters 参数:
 *   node          - Current node being visited 当前访问的节点
 *   sibling_index - Index of this node among its siblings (0-based)
 *                   此节点在兄弟节点中的索引（从0开始）
 *   user_data     - Opaque pointer passed through from the caller
 *                   调用者传入的不透明指针
 *
 * Returns 返回值:
 *   true  - Continue traversal 继续遍历
 *   false - Halt traversal immediately 立即停止遍历               */
typedef bool (*TreeTraverseFunc)(TreeNode* node, int sibling_index,
                                  void* user_data);

/* ----------------------------------------------------------------
 * Lifecycle Functions 生命周期函数
 * ---------------------------------------------------------------- */

/* Create a new empty Tree with a root node.
 * 创建一个新的空树，包含一个 root 节点。
 * Returns: heap-allocated Tree pointer. Never NULL.
 * 返回值：堆分配的 Tree 指针。永远不为 NULL。
 * The root node has content=NULL, no children, depth=0.
 * root 节点的 content=NULL，无子节点，depth=0。                   */
Tree* tree_create(void);

/* Free an entire Tree and all its nodes recursively.
 * 递归释放整棵树及其所有节点。
 * After this call, the tree pointer is invalid (set to NULL
 * is the caller's responsibility if desired).
 * 调用后 tree 指针无效（如需设为 NULL 由调用者负责）。
 * Safe to call with NULL (no-op). 传入 NULL 安全（无操作）。     */
void tree_free(Tree* tree);

/* ----------------------------------------------------------------
 * Node Creation and Destruction 节点创建与销毁
 * ---------------------------------------------------------------- */

/* Create a new TreeNode with the given content.
 * 用给定的内容创建一个新的 TreeNode。
 * content: text to copy into the node (strdup'd internally).
 *          节点的文本内容（内部使用 strdup 复制）。
 *          Pass NULL for a content-less node.
 *          传入 NULL 表示无内容节点。
 * Returns: heap-allocated TreeNode pointer. Never NULL.
 * 返回值：堆分配的 TreeNode 指针。永远不为 NULL。                 */
TreeNode* tree_node_create(const char* content);

/* Free a node and all its descendants recursively.
 * 递归释放节点及其所有后代。
 * Also removes the node from its parent's children array
 * if it has a parent. 如果节点有父节点，还会从父节点的
 * children 数组中移除它。
 * Safe to call with NULL (no-op). 传入 NULL 安全（无操作）。     */
void tree_node_free_subtree(TreeNode* node);

/* ----------------------------------------------------------------
 * Tree Mutation 树变更操作
 * ---------------------------------------------------------------- */

/* Add a child node to the given parent.
 * 向给定父节点添加一个子节点。
 * parent:  the parent to add to (must not be NULL)
 *          要添加到的父节点（不能为 NULL）
 * content: text for the new child (strdup'd), may be NULL
 *          新子节点的文本（内部 strdup），可以为 NULL
 * Returns: pointer to the newly created child node.
 * 返回值：指向新创建子节点的指针。
 * The children array is grown dynamically as needed.
 * children 数组会根据需要动态增长。                               */
TreeNode* tree_node_add_child(TreeNode* parent, const char* content);

/* Remove a child node from its parent at the given index.
 * 从父节点中移除指定索引处的子节点。
 * The removed node is freed (along with its subtree).
 * 被移除的节点（及其子树）会被释放。
 * Returns: true on success, false if index is out of range.
 * 返回值：成功返回 true，索引越界返回 false。                     */
bool tree_node_remove_child(TreeNode* parent, int index);

/* Set (replace) the content text of a node.
 * 设置（替换）节点的内容文本。
 * node:    the node to modify (must not be NULL)
 *          要修改的节点（不能为 NULL）
 * content: new text (strdup'd), may be NULL to clear
 *          新文本（内部 strdup），可以为 NULL 以清除内容
 * Old content is freed automatically. 旧内容自动释放。
 * Updates has_branch_info after setting.
 * 设置后更新 has_branch_info。                                   */
void tree_node_set_content(TreeNode* node, const char* content);

/* ----------------------------------------------------------------
 * Traversal Functions 遍历函数
 * ---------------------------------------------------------------- */

/* Pre-order traversal: visit parent before children.
 * 前序遍历：先访问父节点，再访问子节点。
 * Root is visited first, then recursively each child subtree.
 * 先访问 root，然后递归访问每个子子树。                            */
void tree_traverse_preorder(Tree* tree, TreeTraverseFunc callback,
                             void* user_data);

/* Post-order traversal: visit children before parent.
 * 后序遍历：先访问子节点，再访问父节点。
 * Useful for cleanup-like operations where children must be
 * processed before their parent. 适用于必须先处理子节点
 * 再处理父节点的清理类操作。                                      */
void tree_traverse_postorder(Tree* tree, TreeTraverseFunc callback,
                              void* user_data);

/* Level-order (BFS) traversal: visit nodes level by level.
 * 层序（广度优先）遍历：逐层访问节点。
 * Useful for serialization where depth order matters.
 * 适用于需要按深度顺序序列化的场景。                              */
void tree_traverse_levelorder(Tree* tree, TreeTraverseFunc callback,
                               void* user_data);

/* ----------------------------------------------------------------
 * Query and Utility Functions 查询与工具函数
 * ---------------------------------------------------------------- */

/* Return the maximum depth of the tree (longest path from root).
 * 返回树的最大深度（从 root 出发的最长路径）。
 * An empty tree (root only) has max depth 0.
 * 空树（仅有 root）的最大深度为 0。                                */
int tree_max_depth(const Tree* tree);

/* Recalculate depth and has_branch_info for all nodes.
 * 重新计算所有节点的 depth 和 has_branch_info。
 * Call this after any structural change (add/remove/move nodes).
 * 在任何结构性变更（添加/移除/移动节点）后调用此函数。            */
void tree_recalculate_depths(Tree* tree);

/* Validate the internal consistency of a tree.
 * 校验树的内部一致性。
 * Checks: no cycles, parent-child backlinks correct,
 * child_count <= child_capacity, depth consistency, etc.
 * 检查：无循环、父子反向链接正确、child_count <= child_capacity、
 * depth 一致性等。
 * If error_buf is non-NULL, a human-readable error message
 * is written there on failure (max buf_size bytes).
 * 如果 error_buf 非 NULL，失败时会写入人类可读的错误信息
 * （最多 buf_size 字节）。
 * Returns: true if the tree passes all checks. 通过所有检查返回 true。*/
bool tree_validate(const Tree* tree, char* error_buf, int buf_size);

/* Export the tree to Graphviz DOT format for visual debugging.
 * 将树导出为 Graphviz DOT 格式用于可视化调试。
 * Returns: heap-allocated DOT string (caller must free()).
 * 返回值：堆分配的 DOT 字符串（调用者必须 free()）。
 * Render with: dot -Tpng tree.dot -o tree.png
 * 渲染命令: dot -Tpng tree.dot -o tree.png                          */
char* tree_export_dot(const Tree* tree);

/* Count total nodes in a tree (recalculates total_nodes).
 * 统计树中的节点总数（重新计算 total_nodes）。                     */
int tree_count_nodes(const Tree* tree);

#endif /* TREE_H */
