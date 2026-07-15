/* ============================================================
 * api.js - C Backend API Client
 * api.js - C 后端 API 客户端
 *
 * Wraps all fetch() calls to the C HTTP server. Each function
 * maps to an API endpoint. All functions are async and return
 * parsed JSON or throw on error.
 * 封装所有对 C HTTP 服务器的 fetch() 调用。每个函数映射到
 * 一个 API 端点。所有函数均为异步，返回解析后的 JSON 或抛出错误。
 * ============================================================ */

const API = {
    /* Base URL of the C backend.  C 后端的基础 URL。               */
    BASE: 'http://localhost:8080',

    /* ----------------------------------------------------------------
     * Internal helper: fetch + parse JSON + error handling
     * 内部辅助函数：fetch + 解析 JSON + 错误处理                      */
    async _post(path, body) {
        const resp = await fetch(this.BASE + path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        const json = await resp.json();
        if (!resp.ok || json.error) {
            throw new Error(json.error || `HTTP ${resp.status}`);
        }
        return json;
    },

    async _get(path) {
        const resp = await fetch(this.BASE + path);
        const json = await resp.json();
        if (!resp.ok || json.error) {
            throw new Error(json.error || `HTTP ${resp.status}`);
        }
        return json;
    },

    /* ----------------------------------------------------------------
     * File Operations  文件操作                                      */

    /* Open a file and get the full tree JSON.
     * 打开文件并获取完整树 JSON。                                   */
    async openFile(filepath) {
        return this._post('/api/file/open', { path: filepath });
    },

    /* Save the current tree to a file.
     * 将当前树保存到文件。                                         */
    async saveFile(filepath, treeData) {
        return this._post('/api/file/save', {
            path: filepath,
            config: treeData.config,
            root: treeData.root
        });
    },

    /* Import a non-lxmm file (JSON/TXT/MD) and get tree JSON.
     * 导入非 lxmm 文件并获取树 JSON。                              */
    async importFile(filepath) {
        return this._post('/api/file/import', { path: filepath });
    },

    /* Export the tree to another format.
     * 将树导出为其他格式。                                         */
    async exportFile(filepath, format) {
        return this._post('/api/file/export', {
            path: filepath,
            format: format
        });
    },

    /* ----------------------------------------------------------------
     * Tree Operations  树操作                                        */

    /* Get the full tree JSON from the backend.
     * 从后端获取完整树 JSON。                                      */
    async getTree() {
        return this._post('/api/tree', {});
    },

    /* Replace the entire tree with new JSON data.
     * 用新的 JSON 数据替换整棵树。                                 */
    async replaceTree(treeJson) {
        return this._post('/api/tree/replace', { tree_json: treeJson });
    },

    /* Add a child node. 添加子节点。
     * parent_addr: e.g., "1.2" or "" for root
     * content: text for the new node
     * index: insertion index (-1 = append to end)                  */
    async addNode(parentAddr, content, index) {
        return this._post('/api/tree/node/add', {
            parent_addr: parentAddr || '',
            content: content || '',
            index: index >= 0 ? index : -1
        });
    },

    /* Delete a node and its subtree. 删除节点及其子树。             */
    async deleteNode(addr) {
        return this._post('/api/tree/node/delete', { addr: addr });
    },

    /* Update node metadata. 更新节点元数据。
     * fields can include: content, expanded, custom_color,
     * custom_width, custom_height, format_type                     */
    async updateNode(addr, fields) {
        return this._post('/api/tree/node/update',
            Object.assign({ addr: addr }, fields));
    },

    /* Move a node to a new parent and position.
     * 将节点移动到新父节点和位置。                                 */
    async moveNode(addr, newParent, newIndex) {
        return this._post('/api/tree/node/move', {
            addr: addr,
            new_parent: newParent || '',
            new_index: newIndex
        });
    },

    /* ----------------------------------------------------------------
     * Search  搜索                                                  */

    /* Search nodes by query string. 按查询字符串搜索节点。
     * Returns: { results: [{addr, content, score}, ...] }          */
    async search(query) {
        const enc = encodeURIComponent(query);
        return this._get(`/api/search?q=${enc}`);
    },

    /* ----------------------------------------------------------------
     * Configuration  配置                                           */

    /* Get file-level config. 获取文件级配置。                       */
    async getConfig() {
        return this._post('/api/config/get', {});
    },

    /* Set file-level config. 设置文件级配置。                       */
    async setConfig(config) {
        return this._post('/api/config/set', config);
    },

    /* ----------------------------------------------------------------
     * New Tree  新建树                                              */

    /* Create a new empty tree. 创建新的空树。                       */
    async newTree() {
        return this._post('/api/new', {});
    }
};
