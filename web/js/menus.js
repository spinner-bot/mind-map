/* ============================================================
 * menus.js - Context Menus, Clipboard, Color & Size Settings
 * menus.js - 右键菜单、剪贴板、颜色尺寸设置
 *
 * Implements right-click context menus for nodes and canvas,
 * clipboard operations (copy/cut/paste), color picker,
 * and custom node size configuration.
 * 实现节点和画布的右键上下文菜单、剪贴板操作（复制/剪切/粘贴）、
 * 颜色选择器和自定义节点尺寸配置。
 * ============================================================ */

/* Override the stubs from interaction.js.
 * 覆盖 interaction.js 中的桩实现。                                */

function showNodeContextMenu(x, y, node) {
    removeMenu();
    const menu = createMenu();

    const t = I18N.t.bind(I18N);
    const addr = getNodeAddrString(node);

    addMenuItem(menu, t('menu.edit'), () => startInlineEdit(node));
    addMenuItem(menu, t('menu.addChild'), () => addChildAndRefresh(addr));
    addSeparator(menu);
    addMenuItem(menu, t('menu.insertBefore'), () => insertSibling(addr, 'before'));
    addMenuItem(menu, t('menu.insertAfter'), () => insertSibling(addr, 'after'));
    addSeparator(menu);
    addMenuItem(menu, t('menu.cut'), () => cutNode(addr));
    addMenuItem(menu, t('menu.copy'), () => copyNode(node));
    addMenuItem(menu, t('menu.paste'), () => pasteNode(addr));
    addSeparator(menu);
    addMenuItem(menu, t('menu.color'), null, 'disabled');  /* Submenu placeholder */
    addMenuItem(menu, t('menu.color.auto'), () => setNodeColor(addr, 0));
    addMenuItem(menu, t('menu.color.custom'), () => showColorPicker(addr));
    addSeparator(menu);
    addMenuItem(menu, t('menu.size'), null, 'disabled');
    addMenuItem(menu, t('menu.size.auto'), () => setNodeSize(addr, 0, 0));
    addMenuItem(menu, t('menu.size.custom'), () => showSizeDialog(addr));
    addSeparator(menu);
    addMenuItem(menu, t('menu.format'), null, 'disabled');
    addMenuItem(menu, t('menu.format.plain'), () => setNodeFormat(addr, 0));
    addMenuItem(menu, t('menu.format.latex'), () => setNodeFormat(addr, 1), 'disabled');
    addMenuItem(menu, t('menu.format.html'), () => setNodeFormat(addr, 2), 'disabled');
    addSeparator(menu);
    if (node.children && node.children.length > 0) {
        addMenuItem(menu, t('menu.expandAll'), () => expandAll(addr, true));
        addMenuItem(menu, t('menu.collapseAll'), () => expandAll(addr, false));
    }
    addSeparator(menu);
    addMenuItem(menu, t('menu.delete'), () => {
        if (confirm(I18N.t('dialog.confirmDelete'))) deleteNode(addr);
    }, '', 'danger');

    positionMenu(menu, x, y);
}

function showCanvasContextMenu(x, y) {
    removeMenu();
    const menu = createMenu();
    const t = I18N.t.bind(I18N);

    addMenuItem(menu, t('canvas.color'), () => showCanvasColorPicker());
    addMenuItem(menu, t('canvas.defaultSize'), () => showDefaultSizeDialog());
    addMenuItem(menu, t('canvas.defaultFormat'), () => showDefaultFormatDialog());
    addMenuItem(menu, t('canvas.defaultEnc'), () => showDefaultEncDialog());

    positionMenu(menu, x, y);
}

/* --- Menu Helpers / 菜单辅助 --- */
function createMenu() {
    const menu = document.createElement('div');
    menu.className = 'context-menu';
    document.body.appendChild(menu);
    return menu;
}

function addMenuItem(menu, text, onClick, extraClass) {
    const item = document.createElement('div');
    item.className = 'menu-item' + (extraClass ? ' ' + extraClass : '');
    item.textContent = text;
    if (onClick) {
        item.onclick = () => { onClick(); removeMenu(); };
    }
    menu.appendChild(item);
}

function addSeparator(menu) {
    const sep = document.createElement('div');
    sep.className = 'menu-separator';
    menu.appendChild(sep);
}

function positionMenu(menu, x, y) {
    /* Keep menu within viewport 使菜单保持在视口内 */
    const mw = 220, mh = menu.children.length * 34 + 12;
    menu.style.left = Math.min(x, window.innerWidth - mw - 5) + 'px';
    menu.style.top = Math.min(y, window.innerHeight - mh - 5) + 'px';
}

function removeMenu() {
    const existing = document.querySelector('.context-menu');
    if (existing) existing.remove();
}

/* --- Clipboard / 剪贴板 --- */
function copyNode(node) {
    State.clipboard = JSON.parse(JSON.stringify(node));  /* Deep clone */
    console.log('Copied:', node.content);
}

async function cutNode(addr) {
    const node = findNodeByAddr(addr);
    if (node) copyNode(node);
    await deleteNode(addr);
}

async function pasteNode(targetAddr) {
    if (!State.clipboard) return;
    try {
        /* Add as child of target */
        await API.addNode(targetAddr, State.clipboard.content || '', -1);
        State.treeData = await API.getTree();
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Paste failed:', err);
    }
}

async function deleteNode(addr) {
    try {
        await API.deleteNode(addr);
        State.treeData = await API.getTree();
        State.selectedAddr = null;
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Delete failed:', err);
    }
}

/* --- Color / 颜色 --- */
async function setNodeColor(addr, color) {
    try {
        await API.updateNode(addr, { custom_color: color });
        State.treeData = await API.getTree();
        State.isDirty = true;
        render();
    } catch (err) {
        console.error('Set color failed:', err);
    }
}

function showColorPicker(addr) {
    const input = document.createElement('input');
    input.type = 'color';
    input.value = '#4A90D9';
    input.onchange = async () => {
        /* Convert #rrggbb to ARGB uint32 */
        const hex = input.value.replace('#', '');
        const r = parseInt(hex.slice(0, 2), 16);
        const g = parseInt(hex.slice(2, 4), 16);
        const b = parseInt(hex.slice(4, 6), 16);
        const argb = (0xFF << 24) | (r << 16) | (g << 8) | b;
        await setNodeColor(addr, argb);
    };
    input.click();
}

function showCanvasColorPicker() {
    const input = document.createElement('input');
    input.type = 'color';
    input.value = '#FAFAFA';
    input.onchange = async () => {
        const hex = input.value.replace('#', '');
        const r = parseInt(hex.slice(0, 2), 16);
        const g = parseInt(hex.slice(2, 4), 16);
        const b = parseInt(hex.slice(4, 6), 16);
        const argb = (0xFF << 24) | (r << 16) | (g << 8) | b;
        if (State.treeData?.config) State.treeData.config.canvas_color = argb;
        State.isDirty = true;
        render();
    };
    input.click();
}

/* --- Size / 尺寸 --- */
async function setNodeSize(addr, width, height) {
    try {
        await API.updateNode(addr, { custom_width: width, custom_height: height });
        State.treeData = await API.getTree();
        State.isDirty = true;
        render();
    } catch (err) {
        console.error('Set size failed:', err);
    }
}

function showSizeDialog(addr) {
    const w = prompt('Width (chars, 0=auto):', '0');
    if (w === null) return;
    const h = prompt('Height (chars, 0=auto):', '0');
    if (h === null) return;
    setNodeSize(addr, parseInt(w) || 0, parseInt(h) || 0);
}

function showDefaultSizeDialog() {
    const w = prompt('Default Width (chars, 0=auto):',
                     State.treeData?.config?.default_width || '0');
    if (w === null) return;
    const h = prompt('Default Height (chars, 0=auto):',
                     State.treeData?.config?.default_height || '0');
    if (h === null) return;
    if (State.treeData?.config) {
        State.treeData.config.default_width = parseInt(w) || 0;
        State.treeData.config.default_height = parseInt(h) || 0;
        State.isDirty = true;
    }
}

/* --- Format / 格式 --- */
async function setNodeFormat(addr, fmt) {
    try {
        await API.updateNode(addr, { format_type: fmt });
        State.treeData = await API.getTree();
        State.isDirty = true;
        render();
    } catch (err) {
        console.error('Set format failed:', err);
    }
}

function showDefaultFormatDialog() {
    const fmt = prompt('Default Format (0=Plain Text, 1=LaTeX, 2=HTML):',
                       State.treeData?.config?.default_format || '0');
    if (fmt === null) return;
    const val = parseInt(fmt) || 0;
    if (State.treeData?.config) {
        State.treeData.config.default_format = Math.min(2, Math.max(0, val));
        State.isDirty = true;
    }
}

function showDefaultEncDialog() {
    const enc = prompt('Default Encoding:',
                       State.treeData?.config?.default_encoding || 'UTF-8');
    if (enc === null) return;
    if (State.treeData?.config) {
        State.treeData.config.default_encoding = enc || 'UTF-8';
        State.isDirty = true;
    }
}

/* --- Expand/Collapse All / 展开/折叠全部 --- */
async function expandAll(addr, expand) {
    /* Recursively set expanded state for entire subtree */
    function walkAndSet(node, val) {
        node.expanded = val;
        if (node.children) {
            for (const child of node.children) walkAndSet(child, val);
        }
    }
    const node = findNodeByAddr(addr);
    if (node) {
        walkAndSet(node, expand);
        try {
            /* Update entire tree since we changed many nodes */
            await API.replaceTree(State.treeData);
            State.isDirty = true;
            render();
        } catch (err) {
            console.error('Expand all failed:', err);
        }
    }
}
