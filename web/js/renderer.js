/* ============================================================
 * renderer.js - Mind Map Canvas Renderer
 * renderer.js - 思维导图 Canvas 渲染引擎
 *
 * Renders the tree structure as a visual mind map on an HTML5
 * Canvas. Uses a recursive layout algorithm to position nodes
 * and draws rounded rectangles with bezier curve arcs.
 * 在 HTML5 Canvas 上将树结构渲染为可视化思维导图。
 * 使用递归布局算法定位节点，绘制圆角矩形和贝塞尔曲线弧线。
 * ============================================================ */

/* --- Constants / 常量 --- */
const NODE_PADDING_X = 16;    /* Horizontal padding inside node 节点内水平内边距 */
const NODE_PADDING_Y = 8;     /* Vertical padding inside node */
const NODE_MIN_WIDTH  = 80;   /* Minimum node width 最小节点宽度 */
const NODE_MIN_HEIGHT = 36;   /* Minimum node height 最小节点高度 */
const H_SPACING = 100;        /* Horizontal spacing between levels 层级间水平间距 */
const V_GAP = 10;             /* Vertical gap between siblings 兄弟节点间垂直间隙 */
const ARC_CTRL_OFFSET = 50;   /* Bezier control point offset 贝塞尔控制点偏移 */
const HOVER_BTN_SIZE = 16;    /* Size of hover direction buttons 悬停按钮尺寸 */

/* Auto-color palette: 6 harmonious colors for depth levels.
 * 自动调色板：6 种和谐色用于深度层级。                            */
const DEPTH_COLORS = [
    '#4A90D9',  /* Blue 蓝 */
    '#27AE60',  /* Green 绿 */
    '#E67E22',  /* Orange 橙 */
    '#9B59B6',  /* Purple 紫 */
    '#E74C3C',  /* Red 红 */
    '#1ABC9C',  /* Teal 青 */
];

/* Rendered node cache: node → { x, y, w, h, color, arcPoints }
 * 渲染节点缓存：节点对象 → 位置/尺寸/颜色/弧线点                  */
let _nodeCache = new Map();  /* WeakMap would be better but Map is simpler */

/* --- Canvas Setup / 画布设置 --- */
function initRenderer() {
    const canvas = getCanvas();
    resizeCanvas();
    window.addEventListener('resize', () => {
        resizeCanvas();
        render();
    });
}

function getCanvas() {
    return document.getElementById('mindmap-canvas');
}

function getCtx() {
    return getCanvas().getContext('2d');
}

function resizeCanvas() {
    const container = document.getElementById('canvas-container');
    const canvas = getCanvas();
    const dpr = window.devicePixelRatio || 1;
    canvas.width  = container.clientWidth * dpr;
    canvas.height = container.clientHeight * dpr;
    canvas.style.width  = container.clientWidth + 'px';
    canvas.style.height = container.clientHeight + 'px';
    getCtx().setTransform(dpr, 0, 0, dpr, 0, 0);
}

/* --- Layout Algorithm / 布局算法 --- */
/* Recursively calculate positions for all nodes.
 * 递归计算所有节点的位置。
 * Returns: total height of this subtree.
 * 返回：此子树的总高度。                                          */
function layoutSubtree(node, depth, x, y) {
    if (!node) return 0;

    /* Calculate this node's dimensions 计算此节点的尺寸 */
    const text = node.content || '(empty)';
    const ctx = getCtx();
    ctx.font = '13px ' + getComputedStyle(document.body).fontFamily;
    const textMetrics = ctx.measureText(text);
    const textW = textMetrics.width;

    /* Node width: max of min_width, text_width, custom_width */
    let nodeW = Math.max(NODE_MIN_WIDTH, textW + NODE_PADDING_X * 2);
    if (node.custom_width > 0) nodeW = node.custom_width * 8;  /* chars→px approx */

    /* Node height */
    let nodeH = NODE_MIN_HEIGHT;
    if (node.custom_height > 0) nodeH = node.custom_height * 18; /* chars→px approx */

    const nodeX = x;
    let nodeY = y;

    /* If node has children and is expanded, layout them first */
    const hasVisibleChildren = node.children && node.children.length > 0 && node.expanded !== false;
    let totalHeight = nodeH;

    if (hasVisibleChildren) {
        let childY = y;
        const childX = x + nodeW + H_SPACING;

        /* Layout each child recursively */
        for (let i = 0; i < node.children.length; i++) {
            const childH = layoutSubtree(node.children[i], depth + 1, childX, childY);
            childY += childH + V_GAP;
        }
        childY -= V_GAP;  /* Remove trailing gap */

        totalHeight = Math.max(nodeH, childY - y);

        /* Center parent vertically among children */
        if (node.children.length > 0) {
            const firstChild = _nodeCache.get(node.children[0]);
            const lastChild = _nodeCache.get(node.children[node.children.length - 1]);
            if (firstChild && lastChild) {
                const childrenMidY = (firstChild.y + (lastChild.y + lastChild.h)) / 2;
                nodeY = childrenMidY - nodeH / 2;
                if (nodeY < y) nodeY = y;
                totalHeight = Math.max(totalHeight, (nodeY + nodeH) - y);
            }
        }
    }

    /* Cache node position and size */
    _nodeCache.set(node, {
        x: nodeX, y: nodeY, w: nodeW, h: nodeH,
        depth: depth,
        color: getNodeColor(node, depth),
        hasVisibleChildren: hasVisibleChildren
    });

    return totalHeight;
}

/* Get the display color for a node. 获取节点的显示颜色。           */
function getNodeColor(node, depth) {
    if (node.custom_color && node.custom_color !== 0) {
        /* ARGB → CSS: convert uint32 to #rrggbb */
        const r = (node.custom_color >> 16) & 0xFF;
        const g = (node.custom_color >> 8) & 0xFF;
        const b = node.custom_color & 0xFF;
        return `rgb(${r},${g},${b})`;
    }
    return DEPTH_COLORS[depth % DEPTH_COLORS.length];
}

/* --- Main Render / 主渲染 --- */
function render() {
    const canvas = getCanvas();
    const ctx = getCtx();
    const vp = State.viewport;

    /* Clear canvas 清除画布 */
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    /* Draw canvas background 绘制画布背景 */
    const bgColor = State.treeData?.config?.canvas_color;
    if (bgColor) {
        const r = (bgColor >> 16) & 0xFF;
        const g = (bgColor >> 8) & 0xFF;
        const b = bgColor & 0xFF;
        ctx.fillStyle = `rgb(${r},${g},${b})`;
    } else {
        ctx.fillStyle = '#fafafa';
    }
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    /* Apply viewport transform 应用视口变换 */
    ctx.save();
    ctx.translate(vp.offsetX, vp.offsetY);
    ctx.scale(vp.zoom, vp.zoom);

    /* Layout and render tree from root 从 root 布局和渲染 */
    if (State.treeData?.root) {
        _nodeCache.clear();

        /* Start layout with some margin 从边距开始布局 */
        const marginX = 40;
        const marginY = canvas.height / (2 * vp.zoom) - vp.offsetY / vp.zoom;
        layoutSubtree(State.treeData.root, 0, marginX, Math.max(40, marginY));

        /* Draw arcs first (below nodes) 先绘制弧线（在节点下层） */
        drawAllArcs(ctx, State.treeData.root);

        /* Draw nodes on top 然后绘制节点 */
        drawAllNodes(ctx, State.treeData.root);
    }

    ctx.restore();

    /* 如果 root 没有可见子节点，显示"创建初始节点"按钮 */
    const root = State.treeData?.root;
    const hasVisibleNodes = root && root.children && root.children.length > 0;
    const btn = document.getElementById('btn-init-node');
    if (btn) {
        if (hasVisibleNodes) {
            btn.classList.add('hidden');
        } else {
            btn.classList.remove('hidden');
        }
    }
}

/* --- Node Drawing / 节点绘制 --- */
function drawAllNodes(ctx, node) {
    if (!node) return;

    const cached = _nodeCache.get(node);
    if (!cached) return;

    /* Skip root node if it has no content (container only)  */
    const isRoot = (node === State.treeData.root);
    if (!isRoot || node.content) {
        drawNode(ctx, node, cached);
    }

    /* Draw children (only if expanded)  */
    if (cached.hasVisibleChildren && node.children) {
        for (const child of node.children) {
            drawAllNodes(ctx, child);
        }
    }
}

function drawNode(ctx, node, cached) {
    const { x, y, w, h, color, depth } = cached;
    const isSelected = (State.selectedAddr &&
        State.selectedAddr === getNodeAddrString(node));
    const isHovered = (State.hoveredNode === node);

    const radius = 8;  /* Corner radius 圆角半径 */

    /* Shadow (if not root) 阴影（非 root） */
    if (node !== State.treeData.root) {
        ctx.save();
        ctx.shadowColor = 'rgba(0,0,0,0.12)';
        ctx.shadowBlur = 6;
        ctx.shadowOffsetX = 1;
        ctx.shadowOffsetY = 2;
    }

    /* Node fill 节点填充 */
    ctx.fillStyle = color;
    ctx.globalAlpha = 0.92;
    roundedRect(ctx, x, y, w, h, radius);
    ctx.fill();

    /* Restore shadow 恢复阴影设置 */
    if (node !== State.treeData.root) {
        ctx.restore();
    }
    ctx.globalAlpha = 1.0;

    /* Node border 节点边框 */
    ctx.strokeStyle = isSelected ? '#2c3e50' :
                      isHovered ? '#2980b9' : 'rgba(0,0,0,0.15)';
    ctx.lineWidth = isSelected ? 2.5 : isHovered ? 2 : 1;
    roundedRect(ctx, x, y, w, h, radius);
    ctx.stroke();

    /* Selection glow 选中高亮 */
    if (isSelected) {
        ctx.save();
        ctx.strokeStyle = 'rgba(74, 144, 217, 0.4)';
        ctx.lineWidth = 6;
        roundedRect(ctx, x, y, w, h, radius);
        ctx.stroke();
        ctx.restore();
    }

    /* Text 文本 */
    const text = node.content || '(empty)';
    ctx.fillStyle = isLightColor(color) ? '#333' : '#fff';
    ctx.font = '13px ' + getComputedStyle(document.body).fontFamily;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'center';

    /* Truncate text if too long 文本过长则截断 */
    let displayText = text;
    const maxTextW = w - NODE_PADDING_X * 2;
    if (ctx.measureText(text).width > maxTextW) {
        let truncated = text;
        while (ctx.measureText(truncated + '…').width > maxTextW && truncated.length > 1) {
            truncated = truncated.slice(0, -1);
        }
        displayText = truncated + '…';
    }
    ctx.fillText(displayText, x + w / 2, y + h / 2);

    /* Expansion indicator 展开/折叠指示器 */
    if (cached.hasVisibleChildren) {
        ctx.fillStyle = 'rgba(255,255,255,0.8)';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText('▼', x + w - 6, y + h - 6);
    } else if (node.children && node.children.length > 0 && node.expanded === false) {
        /* Collapsed indicator 折叠指示器 */
        ctx.fillStyle = 'rgba(255,255,255,0.8)';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText('▶', x + w - 6, y + h - 6);
    }

    /* Draw hover buttons 绘制悬停按钮 */
    if (isHovered && !State.dragging) {
        drawHoverButtons(ctx, cached);
    }
}

/* Draw 4 directional hover buttons around a node.
 * 在节点周围绘制 4 个方向悬停按钮。                               */
function drawHoverButtons(ctx, cached) {
    const { x, y, w, h } = cached;
    const bs = HOVER_BTN_SIZE;
    const half = bs / 2;

    /* Button positions: top, bottom, left, right */
    const positions = [
        { cx: x + w/2, cy: y - half - 4, label: '↑' },  /* Top: insert before */
        { cx: x + w/2, cy: y + h + half + 4, label: '↓' }, /* Bottom: insert after */
        { cx: x - half - 4, cy: y + h/2, label: '◀' },  /* Left: toggle expand */
        { cx: x + w + half + 4, cy: y + h/2, label: '▶' }, /* Right: add child */
    ];

    for (const pos of positions) {
        /* Button circle 按钮圆形 */
        ctx.fillStyle = '#4A90D9';
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(pos.cx, pos.cy, half, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();

        /* Button arrow/symbol 按钮符号 */
        ctx.fillStyle = '#fff';
        ctx.font = 'bold 10px sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(pos.label, pos.cx, pos.cy);
    }
}

/* --- Arc Drawing / 弧线绘制 --- */
function drawAllArcs(ctx, node) {
    if (!node || !node.children) return;

    const cached = _nodeCache.get(node);
    if (!cached) return;

    /* Only draw arcs if node is expanded 仅在展开时绘制弧线 */
    if (node.expanded === false) return;

    for (const child of node.children) {
        const childCache = _nodeCache.get(child);
        if (childCache) {
            drawArc(ctx, cached, childCache);
        }
        /* Recursively draw arcs for children */
        drawAllArcs(ctx, child);
    }
}

/* Draw a cubic bezier arc from parent to child node.
 * 绘制从父节点到子节点的三次贝塞尔曲线弧线。                      */
function drawArc(ctx, parent, child) {
    /* Start from right edge of parent 从父节点右边缘出发 */
    const x1 = parent.x + parent.w;
    const y1 = parent.y + parent.h / 2;

    /* End at left edge of child 到达子节点左边缘 */
    const x2 = child.x;
    const y2 = child.y + child.h / 2;

    /* Control points for smooth S-curve 控制点形成平滑 S 曲线 */
    const ctrlOffset = Math.min(ARC_CTRL_OFFSET, (x2 - x1) * 0.6);
    const cp1x = x1 + ctrlOffset;
    const cp1y = y1;
    const cp2x = x2 - ctrlOffset;
    const cp2y = y2;

    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x2, y2);
    ctx.strokeStyle = parent.color || '#b0bec5';
    ctx.lineWidth = 1.8;
    ctx.globalAlpha = 0.7;
    ctx.stroke();
    ctx.globalAlpha = 1.0;

    /* Small dot at connection points 连接点小圆点 */
    ctx.fillStyle = parent.color || '#b0bec5';
    ctx.beginPath();
    ctx.arc(x2, y2, 3, 0, Math.PI * 2);
    ctx.fill();
}

/* --- Helper Functions / 辅助函数 --- */

/* Draw a rounded rectangle path. 绘制圆角矩形路径。               */
function roundedRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.arcTo(x + w, y, x + w, y + r, r);
    ctx.lineTo(x + w, y + h - r);
    ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
    ctx.lineTo(x + r, y + h);
    ctx.arcTo(x, y + h, x, y + h - r, r);
    ctx.lineTo(x, y + r);
    ctx.arcTo(x, y, x + r, y, r);
    ctx.closePath();
}

/* Check if a color is light (for text contrast).
 * 检查颜色是否为浅色（用于文本对比度）。                         */
function isLightColor(color) {
    /* Parse rgb(r,g,b) or #rrggbb format */
    let r = 128, g = 128, b = 128;  /* Default: medium */
    if (color.startsWith('rgb')) {
        const m = color.match(/(\d+),\s*(\d+),\s*(\d+)/);
        if (m) { r = +m[1]; g = +m[2]; b = +m[3]; }
    } else if (color.startsWith('#')) {
        r = parseInt(color.slice(1, 3), 16);
        g = parseInt(color.slice(3, 5), 16);
        b = parseInt(color.slice(5, 7), 16);
    }
    /* Relative luminance approximation 相对亮度近似 */
    const luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
    return luminance > 0.55;
}

/* Hit-test: find node at given canvas coordinates.
 * 命中测试：在给定画布坐标处查找节点。                            */
function hitTestNode(canvasX, canvasY) {
    const vp = State.viewport;
    /* Convert canvas coords to world coords 将画布坐标转为世界坐标 */
    const wx = (canvasX - vp.offsetX) / vp.zoom;
    const wy = (canvasY - vp.offsetY) / vp.zoom;

    /* Search all cached nodes in reverse (children drawn last = on top) */
    let bestNode = null;
    for (const [node, cached] of _nodeCache) {
        if (wx >= cached.x && wx <= cached.x + cached.w &&
            wy >= cached.y && wy <= cached.y + cached.h) {
            /* Skip root if it has no content 跳过无内容的 root */
            if (node === State.treeData.root && !node.content) continue;
            bestNode = node;
            /* Return the deepest (most specific) match */
        }
    }
    return bestNode;
}

/* Hit-test for hover buttons. Returns button direction or null.
 * 悬停按钮命中测试。返回按钮方向或 null。                         */
function hitTestHoverButton(canvasX, canvasY) {
    const node = State.hoveredNode;
    if (!node || State.dragging) return null;

    const cached = _nodeCache.get(node);
    if (!cached) return null;

    const vp = State.viewport;
    const wx = (canvasX - vp.offsetX) / vp.zoom;
    const wy = (canvasY - vp.offsetY) / vp.zoom;

    const { x, y, w, h } = cached;
    const bs = HOVER_BTN_SIZE;
    const half = bs / 2;

    /* Check each button position */
    const buttons = [
        { dir: 'top',    cx: x + w/2, cy: y - half - 4 },
        { dir: 'bottom', cx: x + w/2, cy: y + h + half + 4 },
        { dir: 'left',   cx: x - half - 4, cy: y + h/2 },
        { dir: 'right',  cx: x + w + half + 4, cy: y + h/2 },
    ];

    for (const btn of buttons) {
        const dx = wx - btn.cx, dy = wy - btn.cy;
        if (Math.sqrt(dx * dx + dy * dy) <= half + 2) {
            return btn.dir;
        }
    }
    return null;
}

/* --- Zoom Controls / 缩放控制 --- */
function fitAll() {
    /* Calculate bounding box of all nodes 计算所有节点的包围盒 */
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const [node, cached] of _nodeCache) {
        if (node === State.treeData.root && !node.content) continue;
        if (cached.x < minX) minX = cached.x;
        if (cached.y < minY) minY = cached.y;
        if (cached.x + cached.w > maxX) maxX = cached.x + cached.w;
        if (cached.y + cached.h > maxY) maxY = cached.y + cached.h;
    }

    if (minX === Infinity) { State.viewport.zoom = 1.0; return; }

    const canvas = getCanvas();
    const contentW = maxX - minX + 80;  /* margin */
    const contentH = maxY - minY + 80;
    const zoomX = canvas.width / contentW;
    const zoomY = canvas.height / contentH;
    State.viewport.zoom = Math.min(zoomX, zoomY, 1.5);  /* Cap at 1.5x */
    State.viewport.offsetX = -(minX - 40) * State.viewport.zoom;
    State.viewport.offsetY = -(minY - 40) * State.viewport.zoom;
    updateZoomLabel();
    render();
}

/* Pan canvas by delta. 按偏移量平移画布。                         */
function panCanvas(dx, dy) {
    State.viewport.offsetX += dx;
    State.viewport.offsetY += dy;
    render();
}
