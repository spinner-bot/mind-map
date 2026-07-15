/* ============================================================
 * interaction.js - Node Interaction Handlers
 * interaction.js - 节点交互处理器
 *
 * Mouse and touch interaction handlers for the mind map canvas:
 * hover detection, hover buttons, click-to-select, double-click
 * edit, drag-to-move, drag-to-pan, edge auto-scroll.
 * 思维导图画布的鼠标和触摸交互处理器：悬停检测、悬停按钮、
 * 点击选中、双击编辑、拖拽移动、拖拽平移、边缘自动滚动。
 * ============================================================ */

/* --- Mouse State / 鼠标状态 --- */
const Mouse = {
    x: 0, y: 0,               /* Current position 当前位置 */
    down: false,               /* Mouse button held 鼠标按下 */
    dragStartX: 0, dragStartY: 0,  /* Drag start position 拖拽起始位置 */
    dragNode: null,            /* Node being dragged 拖拽中的节点 */
    dragPreviewX: 0, dragPreviewY: 0, /* Drag preview position 拖拽预览位置 */
    isDragging: false,         /* True = drag in progress 拖拽进行中 */
    isPanning: false,          /* True = panning canvas 平移画布中 */
    autoScrollTimer: null,     /* Edge auto-scroll timer 边缘自动滚动定时器 */
    lastClickTime: 0,          /* For double-click detection 双击检测 */
};

/* --- Initialize / 初始化 --- */
function initInteraction() {
    const canvas = getCanvas();
    canvas.addEventListener('mousedown', onMouseDown);
    canvas.addEventListener('mousemove', onMouseMove);
    canvas.addEventListener('mouseup', onMouseUp);
    canvas.addEventListener('dblclick', onDoubleClick);
    canvas.addEventListener('contextmenu', onContextMenu);
    canvas.addEventListener('wheel', onWheel, { passive: false });
    canvas.addEventListener('mouseleave', onMouseLeave);

    /* Close context menu and search results on outside click */
    document.addEventListener('click', (e) => {
        if (!e.target.closest('.context-menu')) closeContextMenu();
        if (!e.target.closest('#search-results') &&
            e.target !== document.getElementById('search-input')) {
            document.getElementById('search-results').classList.add('hidden');
        }
    });
}

/* --- Mouse Handlers / 鼠标处理器 --- */
function onMouseDown(e) {
    const canvas = getCanvas();
    const rect = canvas.getBoundingClientRect();
    Mouse.x = e.clientX - rect.left;
    Mouse.y = e.clientY - rect.top;
    Mouse.down = true;
    Mouse.dragStartX = Mouse.x;
    Mouse.dragStartY = Mouse.y;

    /* Check if clicking a hover button first 先检查是否点击悬停按钮 */
    if (State.hoveredNode) {
        const btnDir = hitTestHoverButton(Mouse.x, Mouse.y);
        if (btnDir) {
            handleHoverButtonClick(State.hoveredNode, btnDir);
            Mouse.down = false;  /* Don't start drag for button clicks */
            return;
        }
    }

    /* Check if clicking a node 检查是否点击节点 */
    const hitNode = hitTestNode(Mouse.x, Mouse.y);
    if (hitNode) {
        /* Start potential drag on node 开始可能的节点拖拽 */
        Mouse.dragNode = hitNode;
        Mouse.dragPreviewX = Mouse.x;
        Mouse.dragPreviewY = Mouse.y;
        canvas.style.cursor = 'grabbing';
    } else {
        /* Start potential canvas pan 开始可能的画布平移 */
        Mouse.isPanning = true;
        canvas.style.cursor = 'grabbing';
    }
}

function onMouseMove(e) {
    const canvas = getCanvas();
    const rect = canvas.getBoundingClientRect();
    const prevX = Mouse.x, prevY = Mouse.y;
    Mouse.x = e.clientX - rect.left;
    Mouse.y = e.clientY - rect.top;

    /* Drag logic 拖拽逻辑 */
    if (Mouse.down) {
        const dx = Mouse.x - Mouse.dragStartX;
        const dy = Mouse.y - Mouse.dragStartY;

        if (Mouse.dragNode && (Math.abs(dx) > 3 || Math.abs(dy) > 3)) {
            /* Start dragging the node 开始拖拽节点 */
            Mouse.isDragging = true;
            State.dragging = true;
            Mouse.dragPreviewX = Mouse.x;
            Mouse.dragPreviewY = Mouse.y;
            canvas.style.cursor = 'grabbing';
            render();  /* Redraw with drag preview */
            startAutoScroll();
        } else if (Mouse.isPanning && (Math.abs(dx) > 3 || Math.abs(dy) > 3)) {
            /* Pan the canvas 平移画布 */
            panCanvas(dx, dy);
            Mouse.dragStartX = Mouse.x;
            Mouse.dragStartY = Mouse.y;
        }
        return;
    }

    /* Hover detection 悬停检测。
     * 关键：鼠标移到节点外围的悬停按钮上时，hitTestNode 返回 null（按钮在节点矩形外），
     * 导致 hoveredNode 被清除、按钮消失、无法点击。
     * 修复：如果当前有 hoveredNode，先检查鼠标是否仍在它的按钮区域内；
     * 如果是，保持 hoveredNode 不变。                                 */
    const hitNode = hitTestNode(Mouse.x, Mouse.y);
    if (hitNode !== null) {
        /* 鼠标在节点上 → 更新 hoveredNode */
        if (hitNode !== State.hoveredNode) {
            State.hoveredNode = hitNode;
            canvas.style.cursor = 'pointer';
            render();
        }
    } else if (State.hoveredNode != null) {
        /* 鼠标不在任何节点上，但检查是否在 hoveredNode 的按钮区域内 */
        const btnDir = hitTestHoverButton(Mouse.x, Mouse.y);
        if (btnDir === null) {
            /* 既不在节点上也不在按钮上 → 清除悬停 */
            State.hoveredNode = null;
            canvas.style.cursor = 'grab';
            render();
        }
        /* 如果 btnDir 非 null → 鼠标在按钮上，保持 hoveredNode 不变 */
    } else {
        canvas.style.cursor = 'grab';
    }

    /* 悬停按钮光标指示 */
    if (State.hoveredNode && !State.dragging) {
        const btnDir = hitTestHoverButton(Mouse.x, Mouse.y);
        canvas.style.cursor = btnDir ? 'pointer' : 'pointer';
    }
}

function onMouseUp(e) {
    Mouse.down = false;
    const canvas = getCanvas();

    /* End drag 结束拖拽 */
    if (Mouse.isDragging && Mouse.dragNode) {
        endDragNode(Mouse.x, Mouse.y);
    }

    /* End pan 结束平移 */
    if (Mouse.isPanning) {
        Mouse.isPanning = false;
    }

    /* Click select (no significant drag) 点击选中（无明显拖拽） */
    if (!Mouse.isDragging && !Mouse.isPanning) {
        const hitNode = hitTestNode(Mouse.x, Mouse.y);
        if (hitNode) {
            const addr = getNodeAddrString(hitNode);
            selectNode(addr);
            render();
        } else {
            /* Click on empty canvas → deselect 点击空白 → 取消选中 */
            deselectNode();
        }
    }

    /* Reset drag state 重置拖拽状态 */
    Mouse.isDragging = false;
    Mouse.dragNode = null;
    State.dragging = false;
    stopAutoScroll();
    canvas.style.cursor = State.hoveredNode ? 'pointer' : 'grab';
    render();
}

function onDoubleClick(e) {
    /* Double-click on node → inline edit 双击节点 → 内联编辑 */
    const hitNode = hitTestNode(Mouse.x, Mouse.y);
    if (hitNode && hitNode !== State.treeData.root) {
        startInlineEdit(hitNode);
    }
}

function onContextMenu(e) {
    e.preventDefault();
    const hitNode = hitTestNode(
        e.clientX - getCanvas().getBoundingClientRect().left,
        e.clientY - getCanvas().getBoundingClientRect().top
    );
    if (hitNode && hitNode !== State.treeData.root) {
        /* Select the right-clicked node first 先选中右键的节点 */
        const addr = getNodeAddrString(hitNode);
        selectNode(addr);
        render();
        showNodeContextMenu(e.clientX, e.clientY, hitNode);
    } else {
        showCanvasContextMenu(e.clientX, e.clientY);
    }
}

function onWheel(e) {
    e.preventDefault();
    if (e.ctrlKey) {
        /* Ctrl+wheel: zoom 缩放 */
        const delta = -e.deltaY * 0.001;
        adjustZoom(delta);
    } else {
        /* Normal wheel: pan vertically 普通滚轮：垂直平移 */
        panCanvas(-e.deltaX, -e.deltaY);
    }
}

function onMouseLeave() {
    Mouse.down = false;
    Mouse.isDragging = false;
    Mouse.isPanning = false;
    State.hoveredNode = null;
    stopAutoScroll();
    render();
}

/* --- Hover Button Actions / 悬停按钮操作 --- */
function handleHoverButtonClick(node, direction) {
    const addr = getNodeAddrString(node);
    switch (direction) {
        case 'left':  /* Toggle expand/collapse 切换展开折叠 */
            toggleExpand(node, addr);
            break;
        case 'right':  /* Add child node 添加子节点 */
            addChildAndRefresh(addr);
            break;
        case 'top':  /* Insert sibling before 在前插入同级 */
            insertSibling(addr, 'before');
            break;
        case 'bottom':  /* Insert sibling after 在后插入同级 */
            insertSibling(addr, 'after');
            break;
    }
}

async function toggleExpand(node, addr) {
    const expanded = node.expanded !== false ? false : true;
    try {
        await API.updateNode(addr, { expanded: expanded });
        State.treeData = await API.getTree();
        State.isDirty = true;
        render();
    } catch (err) {
        console.error('Toggle expand failed:', err);
    }
}

async function addChildAndRefresh(addr) {
    try {
        await API.addNode(addr, '', -1);
        State.treeData = await API.getTree();
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Add child failed:', err);
    }
}

async function insertSibling(addr, position) {
    try {
        /* Find parent address and index */
        if (!addr) return;
        const parts = addr.split('.');
        const lastIdx = parseInt(parts[parts.length - 1], 10);
        const parentAddr = parts.length > 1
            ? parts.slice(0, -1).join('.') : '';
        const insertIdx = position === 'before' ? lastIdx - 1 : lastIdx;
        await API.addNode(parentAddr, '', insertIdx);
        State.treeData = await API.getTree();
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Insert sibling failed:', err);
    }
}

/* --- Drag Node / 拖拽节点 --- */
function endDragNode(canvasX, canvasY) {
    const dragNode = Mouse.dragNode;
    const dragAddr = getNodeAddrString(dragNode);
    if (!dragAddr) return;

    /* Find target parent under mouse 查找鼠标下的目标父节点 */
    const targetNode = hitTestNode(canvasX, canvasY);

    if (targetNode && targetNode !== dragNode) {
        /* Check target is not a descendant of dragNode  */
        if (!isDescendantOf(targetNode, dragNode)) {
            moveNodeToTarget(dragAddr, targetNode);
            return;
        }
    }

    /* Drop on canvas background → move to root level 放到空白处 → 移到根级 */
    moveNodeToParent(dragAddr, '');
}

function isDescendantOf(possibleAncestor, node) {
    let current = node;
    while (current) {
        if (current === possibleAncestor) return true;
        /* Navigate up via parent (not directly available in cached data) */
        /* This is a simplified check — full implementation would use tree */
        break;
    }
    return false;
}

async function moveNodeToTarget(addr, targetNode) {
    const targetAddr = getNodeAddrString(targetNode);
    try {
        await API.moveNode(addr, targetAddr, 0);
        State.treeData = await API.getTree();
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Move failed:', err);
    }
}

async function moveNodeToParent(addr, parentAddr) {
    try {
        await API.moveNode(addr, parentAddr, -1);
        State.treeData = await API.getTree();
        State.isDirty = true;
        updateStats();
        render();
    } catch (err) {
        console.error('Move to root failed:', err);
    }
}

/* --- Edge Auto-Scroll / 边缘自动滚动 --- */
function startAutoScroll() {
    stopAutoScroll();
    Mouse.autoScrollTimer = setInterval(() => {
        if (!Mouse.isDragging) { stopAutoScroll(); return; }
        const canvas = getCanvas();
        const margin = 40;  /* Pixels from edge to trigger scroll */
        const speed = 8;    /* Scroll speed 滚动速度 */

        let dx = 0, dy = 0;
        if (Mouse.x < margin) dx = -speed;
        else if (Mouse.x > canvas.width / (window.devicePixelRatio || 1) - margin) dx = speed;
        if (Mouse.y < margin) dy = -speed;
        else if (Mouse.y > canvas.height / (window.devicePixelRatio || 1) - margin) dy = speed;

        if (dx !== 0 || dy !== 0) {
            panCanvas(dx, dy);
        }
    }, 16);  /* ~60fps */
}

function stopAutoScroll() {
    if (Mouse.autoScrollTimer) {
        clearInterval(Mouse.autoScrollTimer);
        Mouse.autoScrollTimer = null;
    }
}

/* --- Inline Edit / 内联编辑 --- */
function startInlineEdit(node) {
    const cached = _nodeCache.get(node);
    if (!cached) return;

    const vp = State.viewport;
    const canvas = getCanvas();
    const rect = canvas.getBoundingClientRect();

    /* Calculate screen position 计算屏幕位置 */
    const screenX = rect.left + (cached.x * vp.zoom + vp.offsetX);
    const screenY = rect.top + (cached.y * vp.zoom + vp.offsetY);

    /* Remove existing inline edit 移除已有内联编辑 */
    const existing = document.querySelector('.inline-edit');
    if (existing) existing.remove();

    /* Create textarea for editing 创建编辑用 textarea */
    const textarea = document.createElement('textarea');
    textarea.className = 'inline-edit';
    textarea.style.left = screenX + 'px';
    textarea.style.top = screenY + 'px';
    textarea.style.width = Math.max(120, cached.w * vp.zoom) + 'px';
    textarea.style.minHeight = Math.max(30, cached.h * vp.zoom) + 'px';
    textarea.value = node.content || '';
    document.body.appendChild(textarea);
    textarea.focus();
    textarea.select();

    /* Save on Enter, cancel on Escape 回车保存，Esc 取消 */
    textarea.addEventListener('keydown', async (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            const newContent = textarea.value;
            textarea.remove();
            const addr = getNodeAddrString(node);
            if (addr) {
                try {
                    await API.updateNode(addr, { content: newContent });
                    State.treeData = await API.getTree();
                    State.isDirty = true;
                    updateStats();
                    render();
                } catch (err) {
                    console.error('Edit failed:', err);
                }
            }
        } else if (e.key === 'Escape') {
            textarea.remove();
        }
    });

    /* Save on blur 失焦保存 */
    textarea.addEventListener('blur', async () => {
        if (!document.body.contains(textarea)) return;  /* Already removed */
        const newContent = textarea.value;
        textarea.remove();
        const addr = getNodeAddrString(node);
        if (addr) {
            try {
                await API.updateNode(addr, { content: newContent });
                State.treeData = await API.getTree();
                State.isDirty = true;
                updateStats();
                render();
            } catch (err) {
                console.error('Edit failed:', err);
            }
        }
    });
}

/* --- Context Menus / 右键菜单 --- */
/* Forward-declared in menus.js; stub implementations here.
 * 在 menus.js 中声明；此处提供桩实现。                            */
function showNodeContextMenu(x, y, node) {
    /* Stub — will be overridden by menus.js when loaded */
    console.log('Node context menu at', x, y, 'for node:', node.content);
}

function showCanvasContextMenu(x, y) {
    /* Stub — will be overridden by menus.js when loaded */
    console.log('Canvas context menu at', x, y);
}
