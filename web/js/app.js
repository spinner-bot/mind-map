/* ============================================================
 * app.js - Main Application Controller
 * app.js - 主应用控制器
 *
 * Orchestrates the mind map editor: initializes the UI, manages
 * global state (tree data, selection, viewport), and coordinates
 * between renderer, interaction, and API modules.
 * 协调思维导图编辑器：初始化 UI、管理全局状态（树数据、选中、
 * 视口）、协调渲染器、交互和 API 模块之间的通信。
 * ============================================================ */

/* --- Global State / 全局状态 --- */
const State = {
    /* Tree data from backend. 来自后端的树数据。                    */
    treeData: null,       /* Full tree JSON: { config, root } */

    /* Currently selected node address. 当前选中节点地址。          */
    selectedAddr: null,

    /* Viewport state. 视口状态。                                   */
    viewport: {
        offsetX: 0,       /* Pan offset X 平移偏移 */
        offsetY: 0,       /* Pan offset Y */
        zoom: 1.0,        /* Zoom level 缩放级别 (0.1 ~ 5.0) */
    },

    /* Edit state. 编辑状态。                                       */
    isDirty: false,       /* True if tree has unsaved changes */
    currentFile: null,    /* Currently open file path */
    clipboard: null,      /* Copied/cut subtree data */

    /* Interaction state. 交互状态。                                */
    hoveredNode: null,    /* Node under mouse 鼠标下的节点 */
    dragging: false,      /* Is a drag operation in progress? */

    /* Stats cache. 统计缓存。                                      */
    stats: {
        layers: 0,
        nodes: 0,
        sizeKB: 0,
        totalChars: 0
    }
};

/* --- Initialization / 初始化 --- */
async function initApp() {
    /* Init i18n first 先初始化 i18n */
    I18N.init();
    document.getElementById('lang-select').value = I18N.current;

    /* Reflect i18n strings 应用翻译 */
    reflectI18N();

    /* Load initial tree from backend (new empty tree) 从后端加载初始空树 */
    try {
        State.treeData = await API.newTree();
        console.log('App initialized: new empty tree created');
    } catch (err) {
        console.warn('Failed to create initial tree, using offline stub:', err);
        /* Offline fallback: create empty tree in-memory */
        State.treeData = {
            config: {
                canvas_color: 4294967295, default_width: 0, default_height: 0,
                default_format: 0, default_encoding: 'UTF-8', default_zoom: 1.0
            },
            root: { content: null, expanded: true, custom_color: 0, custom_width: 0,
                    custom_height: 0, format_type: 0, has_branch_info: false, children: [] }
        };
    }

    /* Initialize the mind map renderer. 初始化思维导图渲染器。      */
    initRenderer();

    /* Initialize interaction handlers. 初始化交互处理器。           */
    initInteraction();

    /* Update UI. 更新 UI。                                          */
    updateStats();
    updateZoomLabel();
    render();

    /* Wire up toolbar buttons. 绑定工具栏按钮。                     */
    wireToolbar();

    /* Wire up keyboard shortcuts. 绑定键盘快捷键。                  */
    wireKeyboard();

    console.log('Mind Map Editor ready');
}

/* --- Toolbar Event Wiring / 工具栏事件绑定 --- */
function wireToolbar() {
    const $ = (id) => document.getElementById(id);

    $('btn-new').onclick = onNewFile;
    $('btn-open').onclick = onOpenFile;
    $('btn-save').onclick = onSaveFile;
    $('btn-undo').onclick = onUndo;
    $('btn-redo').onclick = onRedo;
    $('btn-zoom-out').onclick = () => adjustZoom(-0.1);
    $('btn-zoom-in').onclick = () => adjustZoom(0.1);
    $('btn-zoom-fit').onclick = fitAll;
    $('btn-export').onclick = onExport;
    $('btn-config').onclick = onConfig;
    $('btn-close-panel').onclick = () => deselectNode();

    $('search-input').oninput = onSearchInput;
    $('locate-input').onkeydown = onLocateKey;
    $('lang-select').onchange = (e) => {
        I18N.setLang(e.target.value);
        reflectI18N();
    };

    $('btn-edit-content').onclick = onEditSelected;
    $('btn-add-child').onclick = onAddChildToSelected;

    /* 空画布时"创建初始节点"按钮 */
    $('btn-init-node').onclick = async () => {
        try {
            await API.addNode('', '', -1);
            State.treeData = await API.getTree();
            State.isDirty = true;
            updateStats();
            render();
        } catch (err) {
            showToast('Failed: ' + err.message);
        }
    };
}

/* --- Keyboard Shortcuts / 键盘快捷键 --- */
function wireKeyboard() {
    document.addEventListener('keydown', (e) => {
        /* Ctrl+Z: Undo */
        if (e.ctrlKey && !e.shiftKey && e.key === 'z') { e.preventDefault(); onUndo(); }
        /* Ctrl+Y or Ctrl+Shift+Z: Redo */
        if ((e.ctrlKey && e.key === 'y') || (e.ctrlKey && e.shiftKey && e.key === 'z')) {
            e.preventDefault(); onRedo();
        }
        /* Ctrl+S: Save */
        if (e.ctrlKey && e.key === 's') { e.preventDefault(); onSaveFile(); }
        /* Ctrl+N: New */
        if (e.ctrlKey && e.key === 'n') { e.preventDefault(); onNewFile(); }
        /* Ctrl+O: Open */
        if (e.ctrlKey && e.key === 'o') { e.preventDefault(); onOpenFile(); }
        /* Ctrl+Plus: Zoom in */
        if (e.ctrlKey && (e.key === '=' || e.key === '+')) { e.preventDefault(); adjustZoom(0.1); }
        /* Ctrl+Minus: Zoom out */
        if (e.ctrlKey && e.key === '-') { e.preventDefault(); adjustZoom(-0.1); }
        /* Ctrl+0: Fit all */
        if (e.ctrlKey && e.key === '0') { e.preventDefault(); fitAll(); }
        /* Escape: Deselect */
        if (e.key === 'Escape') { deselectNode(); closeContextMenu(); }
        /* Delete: Delete selected node */
        if (e.key === 'Delete' && State.selectedAddr) {
            e.preventDefault(); onDeleteSelected();
        }
    });
}

/* --- I18N Reflection / 翻译应用 --- */
function reflectI18N() {
    const t = I18N.t.bind(I18N);
    /* Toolbar */
    document.getElementById('btn-new').textContent = t('btn.new');
    document.getElementById('btn-open').textContent = t('btn.open');
    document.getElementById('btn-save').textContent = t('btn.save');
    document.getElementById('btn-export').textContent = t('btn.export');
    document.getElementById('btn-config').textContent = t('btn.config');
    document.getElementById('search-input').placeholder = t('search.placeholder');
    document.getElementById('locate-input').placeholder = t('locate.placeholder');
    /* Panel */
    document.getElementById('panel-title').textContent = t('panel.title');
    document.getElementById('btn-edit-content').textContent = t('panel.edit');
    document.getElementById('btn-add-child').textContent = t('panel.addchild');
}

/* --- Stats Update / 统计更新 --- */
function updateStats() {
    const root = State.treeData?.root;
    if (!root) return;

    /* Count nodes and compute depth 计数节点和深度 */
    function countNodes(node, depth) {
        let count = 1, maxD = depth, totalChars = 0;
        if (node.content) totalChars += node.content.length;
        for (const child of (node.children || [])) {
            const [c, d, ch] = countNodes(child, depth + 1);
            count += c;
            if (d > maxD) maxD = d;
            totalChars += ch;
        }
        return [count, maxD, totalChars];
    }

    const [totalNodes, maxDepth, totalChars] = countNodes(root, 0);
    const layers = maxDepth;  /* Excluding root depth 0 */
    const nodes = totalNodes - 1;  /* Excluding root */

    State.stats = {
        layers: layers,
        nodes: nodes,
        sizeKB: Math.max(0.1, Math.round(totalChars / 102.4) / 10),
        totalChars: totalChars
    };

    const $ = (id) => document.getElementById(id);
    $('status-stats').textContent =
        `${layers} ${I18N.t('status.layers')}, ${nodes} ${I18N.t('status.nodes')}`;
    $('status-size').textContent = `${State.stats.sizeKB} KB`;
    $('status-chars').textContent = `${totalChars} ${I18N.t('status.chars')}`;
}

/* --- Toast Notification / 提示通知 --- */
function showToast(msg, duration) {
    duration = duration || 2000;
    /* 移除已有 toast */
    const existing = document.querySelector('.mindmap-toast');
    if (existing) existing.remove();
    /* 创建新 toast */
    const toast = document.createElement('div');
    toast.className = 'mindmap-toast';
    toast.textContent = msg;
    toast.style.cssText = 'position:fixed;bottom:40px;left:50%;transform:translateX(-50%);'
        + 'background:#2c3e50;color:#fff;padding:10px 24px;border-radius:20px;'
        + 'font-size:14px;z-index:9999;opacity:0;transition:opacity 0.3s;pointer-events:none;';
    document.body.appendChild(toast);
    requestAnimationFrame(() => { toast.style.opacity = '1'; });
    setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
    }, duration);
}

/* --- Toolbar Actions / 工具栏操作 --- */
async function onNewFile() {
    try {
        State.treeData = await API.newTree();
        State.selectedAddr = null;
        State.currentFile = null;
        State.isDirty = false;
        updateStats();
        render();
        showToast('New mind map created / 已创建新思维导图');
    } catch (err) {
        console.error('New failed:', err);
        showToast('Failed: ' + err.message);
    }
}

async function onOpenFile() {
    const input = document.getElementById('file-input');
    input.onchange = async () => {
        const file = input.files[0];
        if (!file) return;
        try {
            /* 浏览器安全限制：无法获取文件完整路径，只能拿到文件名和内容。
             * 因此直接传文件内容给后端，由后端写临时文件再解析。       */
            const text = await file.text();
            State.treeData = await API.importContent(file.name, text);
            State.currentFile = null;
            State.selectedAddr = null;
            State.isDirty = true;
            updateStats();
            render();
        } catch (err) {
            console.error('Open failed:', err);
            alert('Failed to open file: ' + err.message);
        }
        input.value = '';
    };
    input.click();
}

async function onSaveFile() {
    if (!State.treeData) {
        showToast('Nothing to save / 没有可保存的内容');
        return;
    }
    try {
        const fname = State.currentFile || 'untitled.lxmm';
        await API.saveFile(fname, State.treeData);
        State.isDirty = false;
        showToast('Saved: ' + fname + ' / 已保存');
    } catch (err) {
        console.error('Save failed:', err);
        showToast('Save failed: ' + err.message);
    }
}

function onUndo() { UndoManager.undo(); showToast('Undo / 撤销'); }
function onRedo() { UndoManager.redo(); showToast('Redo / 重做'); }

function adjustZoom(delta) {
    State.viewport.zoom = Math.max(0.1, Math.min(5.0, State.viewport.zoom + delta));
    updateZoomLabel();
    render();
}

function fitAll() { /* Placeholder — implemented with renderer */ }

function updateZoomLabel() {
    document.getElementById('zoom-label').textContent =
        Math.round(State.viewport.zoom * 100) + '%';
}

async function onExport() {
    /* 导出为 PNG 图片 */
    try {
        const canvas = getCanvas();
        canvas.toBlob(async (blob) => {
            if (!blob) { showToast('Export failed / 导出失败'); return; }
            /* 触发浏览器下载 */
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url; a.download = 'mindmap.png'; a.click();
            URL.revokeObjectURL(url);
            showToast('Exported: mindmap.png / 已导出');
        }, 'image/png');
    } catch (err) {
        showToast('Export failed: ' + err.message);
    }
}

function onConfig() {
    /* 打开画布设置对话框 */
    const c = State.treeData?.config;
    if (!c) { showToast('No config / 无配置'); return; }
    const bg = prompt('Canvas color (ARGB, e.g. 4294967295=white):', c.canvas_color || 4294967295);
    if (bg === null) return;
    const dw = prompt('Default node width (chars, 0=auto):', c.default_width || 0);
    if (dw === null) return;
    const dh = prompt('Default node height (chars, 0=auto):', c.default_height || 0);
    if (dh === null) return;
    c.canvas_color = parseInt(bg) || 4294967295;
    c.default_width = parseInt(dw) || 0;
    c.default_height = parseInt(dh) || 0;
    State.isDirty = true;
    render();
    showToast('Config updated / 配置已更新');
}

async function onEditSelected() {
    if (!State.selectedAddr) return;
    /* Trigger inline edit on the selected node */
    if (typeof startInlineEdit === 'function') {
        startInlineEdit(State.selectedAddr);
    }
}

async function onAddChildToSelected() {
    if (!State.selectedAddr) return;
    try {
        await API.addNode(State.selectedAddr, '', -1);
        State.treeData = await API.getTree();
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Add child failed:', err);
    }
}

async function onDeleteSelected() {
    if (!State.selectedAddr) return;
    if (!confirm(I18N.t('dialog.confirmDelete'))) return;
    try {
        await API.deleteNode(State.selectedAddr);
        State.treeData = await API.getTree();
        State.selectedAddr = null;
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Delete failed:', err);
    }
}

/* --- Search / 搜索 --- */
let searchTimer = null;
async function onSearchInput() {
    clearTimeout(searchTimer);
    const q = document.getElementById('search-input').value.trim();
    if (!q) {
        document.getElementById('search-results').classList.add('hidden');
        return;
    }
    searchTimer = setTimeout(async () => {
        try {
            const resp = await API.search(q);
            showSearchResults(resp.results || []);
        } catch (err) {
            console.error('Search failed:', err);
        }
    }, 300);
}

function showSearchResults(results) {
    const container = document.getElementById('search-results');
    if (!results.length) {
        container.innerHTML = '<div class="menu-item disabled">No results</div>';
    } else {
        container.innerHTML = results.map(r =>
            `<div class="menu-item" data-addr="${r.addr}">
                <span>📍${r.addr}</span>
                <span style="flex:1;margin:0 8px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${escapeHtml(r.content)}</span>
                <span style="color:var(--color-text-secondary);font-size:11px;">${r.score}</span>
            </div>`
        ).join('');
        /* Click handler */
        container.querySelectorAll('.menu-item').forEach(el => {
            el.onclick = () => {
                const addr = el.dataset.addr;
                State.selectedAddr = addr;
                render();
                container.classList.add('hidden');
                document.getElementById('search-input').value = '';
            };
        });
    }
    container.classList.remove('hidden');
}

/* --- Locate / 定位 --- */
function onLocateKey(e) {
    if (e.key !== 'Enter') return;
    const addr = document.getElementById('locate-input').value.trim();
    if (!addr) return;
    State.selectedAddr = addr;
    render();
}

/* --- Selection / 选中 --- */
function selectNode(addr) {
    State.selectedAddr = addr;
    updatePreviewPanel(addr);
    document.getElementById('preview-panel').classList.remove('collapsed');
}

function deselectNode() {
    State.selectedAddr = null;
    document.getElementById('preview-panel').classList.add('collapsed');
    render();
}

function updatePreviewPanel(addr) {
    const node = findNodeByAddr(addr);
    if (!node) return;
    const $ = (id) => document.getElementById(id);
    $('info-addr').textContent = addr || '(root)';
    $('info-content').textContent = node.content || I18N.t('panel.none');
    $('info-children').textContent = (node.children || []).length;
    $('info-chars').textContent = node.content ? node.content.length : 0;
    const fmtNames = ['format.plain', 'format.latex', 'format.html'];
    $('info-format').textContent = I18N.t(fmtNames[node.format_type || 0]);
}

/* Find a node by address string (e.g., "0.1") in tree data.
 * 按地址字符串在树数据中查找节点。                                 */
function findNodeByAddr(addr) {
    if (!State.treeData?.root) return null;
    if (!addr || addr === 'root') return State.treeData.root;

    let node = State.treeData.root;
    const parts = addr.split('.');
    for (const p of parts) {
        const idx = parseInt(p, 10) - 1;  /* 1-based → 0-based */
        if (isNaN(idx) || idx < 0 || !node.children || idx >= node.children.length) {
            return null;
        }
        node = node.children[idx];
    }
    return node;
}

/* Get address for a node object (reverse lookup).
 * 获取节点对象的地址字符串（反向查找）。                           */
function getNodeAddrString(targetNode) {
    if (!State.treeData?.root) return '';
    if (targetNode === State.treeData.root) return '';

    /* DFS to find the path */
    function dfs(node, path) {
        if (node === targetNode) return path;
        if (!node.children) return null;
        for (let i = 0; i < node.children.length; i++) {
            const childPath = path ? path + '.' + (i + 1) : '' + (i + 1);
            const result = dfs(node.children[i], childPath);
            if (result) return result;
        }
        return null;
    }
    return dfs(State.treeData.root, '') || '';
}

/* --- HTML Escaping / HTML 转义 --- */
function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;')
              .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

/* --- Context Menu Close / 关闭右键菜单 --- */
function closeContextMenu() {
    const menu = document.querySelector('.context-menu');
    if (menu) menu.remove();
}

/* --- Startup --- */
document.addEventListener('DOMContentLoaded', initApp);
