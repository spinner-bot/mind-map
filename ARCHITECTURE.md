# 二期架构说明文档

## 为什么是"Web 前端 + C 后端"？

一期使用纯 C + Win32 API 做 GUI，界面效果受限于 Win32 控件体系，难以做到现代、美观、流畅的用户体验。二期需求（思维导图可视化渲染、拖拽、动画、悬停按钮、右键菜单等）对 UI 要求极高。

因此二期采用 **浏览器做前端，C 做后端** 的架构：

| 层 | 技术 | 职责 |
|----|------|------|
| 前端 | HTML5 Canvas + CSS3 + 原生 JS | 渲染、交互、动画——所有用户看到和操作的部分 |
| 后端 | C（WinSock2 + 一期全部代码） | 文件 I/O、格式解析/序列化、树操作——所有数据处理 |
| 通信 | HTTP JSON API (localhost:8080) | 前后端之间的约定协议 |

**好处**：界面能做到任意美观度（CSS3 阴影/渐变/动画/Canvas 自由绘制），同时一期所有 C 代码（tree、json/txt/md handler、encoding、converter）全部保留复用，一行没浪费。

---

## 启动流程

```
用户双击 mind_map.exe
  │
  ├─ 1. SetConsoleOutputCP(CP_UTF8)    确保中文不乱码
  ├─ 2. format_registry_init()         初始化格式处理器注册表
  ├─ 3. format_register() × 4          注册 JSON / TXT / MD / LXMM 处理器
  ├─ 4. server_init()                  创建 TCP socket，绑定 localhost:8080
  ├─ 5. tree_create()                  创建一棵空树供编辑器初始使用
  ├─ 6. server_set_tree(tree)          把树交给 HTTP 服务器
  ├─ 7. open_browser()                 ShellExecuteW 打开浏览器
  └─ 8. server_start()                进入无限循环，阻塞等待 HTTP 请求
```

之后 exe 一直运行，等待浏览器发来请求。关闭控制台窗口（Ctrl+C）即退出。

---

## 浏览器加载过程

浏览器打开 `http://localhost:8080` 后：

```
浏览器 → GET /                    → 返回 index.html
浏览器 → GET /css/style.css       → 返回样式
浏览器 → GET /js/i18n.js          → 前端翻译模块
浏览器 → GET /js/api.js           → API 客户端封装
浏览器 → GET /js/renderer.js      → Canvas 渲染引擎
浏览器 → GET /js/interaction.js   → 鼠标/键盘交互
浏览器 → GET /js/menus.js         → 右键菜单/剪贴板
浏览器 → GET /js/undo.js          → 撤销/重做
浏览器 → GET /js/search.js        → 模糊搜索
浏览器 → GET /js/app.js           → 主控制器（最后加载，负责初始化）
```

全部 JS 加载完毕后，`app.js` 的 `initApp()` 自动执行，向 C 后端请求初始数据。

---

## 前后端通信协议

所有通信使用 HTTP POST（少数 GET），Body 为 JSON，响应也是 JSON。

### 核心 API 端点一览

| 端点 | 用途 | 请求 Body | 响应 |
|------|------|-----------|------|
| `POST /api/new` | 新建空树 | `{}` | 完整树 JSON |
| `POST /api/tree` | 获取当前树 | `{}` | 完整树 JSON |
| `POST /api/tree/replace` | 替换整个树 | `{"tree_json":"{...}"}` | `{"ok":true}` |
| `POST /api/tree/node/add` | 添加节点 | `{"parent_addr":"1.2","content":"新节点","index":-1}` | `{"ok":true,"addr":"1.2.1"}` |
| `POST /api/tree/node/delete` | 删除节点 | `{"addr":"1.2.3"}` | `{"ok":true}` |
| `POST /api/tree/node/update` | 更新节点 | `{"addr":"1.2","content":"新内容","expanded":true,...}` | `{"ok":true}` |
| `POST /api/tree/node/move` | 移动节点 | `{"addr":"1.2","new_parent":"2","new_index":0}` | `{"ok":true,"new_addr":"2.1"}` |
| `GET /api/search?q=关键词` | 搜索节点 | — | `{"results":[{addr,content,score}]}` |
| `POST /api/file/open` | 打开文件 | `{"path":"C:/test.lxmm"}` | 完整树 JSON |
| `POST /api/file/save` | 保存文件 | `{"path":"C:/test.lxmm","config":{...},"root":{...}}` | `{"ok":true}` |
| `POST /api/file/import` | 导入（JSON/TXT/MD） | `{"path":"C:/test.json"}` | 完整树 JSON |
| `POST /api/file/export` | 导出为其他格式 | `{"path":"out.md","format":"md"}` | `{"ok":true,"output_path":"out.md"}` |
| `POST /api/config/get` | 获取配置 | `{}` | `{canvas_color,default_width,...}` |
| `POST /api/config/set` | 设置配置 | `{"canvas_color":...,...}` | `{"ok":true}` |

### 树的 JSON 格式

API 中传递的"完整树 JSON"格式如下。这是前端和后端之间的核心数据协议：

```json
{
  "config": {
    "canvas_color": 4294967295,
    "default_width": 0,
    "default_height": 0,
    "default_format": 0,
    "default_encoding": "UTF-8",
    "default_zoom": 1.0
  },
  "root": {
    "content": null,
    "expanded": true,
    "custom_color": 0,
    "custom_width": 0,
    "custom_height": 0,
    "format_type": 0,
    "has_branch_info": false,
    "children": [
      {
        "content": "第一章",
        "expanded": true,
        "custom_color": 4282664003,
        "custom_width": 0,
        "custom_height": 0,
        "format_type": 0,
        "has_branch_info": false,
        "children": [
          { "content": "1.1 概述", "children": [], ... },
          { "content": "1.2 详述", "children": [], ... }
        ]
      }
    ]
  }
}
```

每个节点字段说明：

| 字段 | 类型 | 说明 |
|------|------|------|
| `content` | string\|null | 节点文本内容，null 表示无内容 |
| `expanded` | bool | 展开状态，false 时子节点在编辑器中隐藏 |
| `custom_color` | uint32 | ARGB 颜色，0 表示使用自动配色 |
| `custom_width` | int | 自定义宽度（字符数），0 表示自动 |
| `custom_height` | int | 自定义高度（字符数），0 表示自动 |
| `format_type` | int | 0=纯文本，1=LaTeX(预留)，2=HTML(预留) |
| `has_branch_info` | bool | 同时有内容和子节点时为 true |
| `children` | array | 子节点数组，递归嵌套 |

---

## 前端模块职责

### `i18n.js` — 翻译
- 维护中英文对照表
- `I18N.t(key)` 获取翻译字符串
- 语言偏好存入 `localStorage`，刷新不丢失
- 初始化时自动检测浏览器语言

### `api.js` — 通信层
- 封装所有 `fetch()` 调用
- `API.openFile(path)`、`API.addNode(...)`、`API.search(q)` 等
- 自动 JSON 解析和错误处理

### `renderer.js` — 画布渲染
- `layoutSubtree()` — 递归布局算法：计算每个节点的 (x, y, w, h)
  - 每个节点宽度 = max(最小宽度, 文本宽度 + 内边距, 自定义宽度)
  - 父节点垂直居中于子节点范围
  - 水平间距固定，垂直间距根据子树大小计算
- `drawNode()` — 绘制圆角矩形节点 + 文字 + 阴影 + 选中高亮
- `drawArc()` — 绘制三次贝塞尔曲线连接父子节点
- `drawHoverButtons()` — 在悬停节点四周绘制 4 个方向圆形按钮
- `fitAll()` — 计算所有节点包围盒，自动计算缩放比例使全部可见
- `hitTestNode()` — 将画布坐标反算为世界坐标，遍历缓存找到命中节点

### `interaction.js` — 交互
- 鼠标状态机：空闲 → 悬停 → 点选 → 拖拽(节点/画布) → 释放
- 悬停检测每帧 `mousemove`，悬停态变化触发 `render()` 重绘
- 单击空白 → 取消选中；单击节点 → 选中 + 更新预览面板
- 双击节点 → `startInlineEdit()` 在节点位置创建 `<textarea>` 覆盖层
- 拖拽节点 → 边缘自动滚动（`setInterval` 每 16ms 检查）
- 拖拽释放 → 检测目标，防循环（拖到自己子孙），调 `API.moveNode()`
- Ctrl+滚轮 → 缩放；普通滚轮 → 垂直平移

### `menus.js` — 右键菜单
- `showNodeContextMenu()` — 节点右键菜单（编辑/插入/删除/剪切/复制/粘贴/颜色/尺寸/格式/展开折叠）
- `showCanvasContextMenu()` — 画布右键菜单（画布颜色/默认尺寸/格式/编码）
- 剪贴板：`copyNode()` 深拷贝，`pasteNode()` 添加为子节点
- 颜色：`<input type="color">` HTML 原生取色器
- 尺寸：`prompt()` 对话框输入宽高

### `undo.js` — 撤销/重做
- 基于完整树 JSON 快照的实现
- `UndoManager.push(treeData)` — 每次重要编辑操作后调用
- `UndoManager.undo()` / `UndoManager.redo()` — Ctrl+Z / Ctrl+Y
- 最大 256 步，超出从头部丢弃

### `search.js` — 搜索
- `fuzzyScore()` 评分：精确匹配 2000 > 前缀匹配 1500 > 子串 1000 > 词边界 800 > 编辑距离
- Levenshtein 编辑距离算法（单行动态规划优化）
- 前端搜索作为后端 `/api/search` 的补充回退

### `app.js` — 主控制器
- `State` 全局状态对象（treeData, selectedAddr, viewport, dragNode 等）
- `initApp()` 启动序列
- 工具栏按钮绑定 + 键盘快捷键（Ctrl+S/N/O/Z/Y/+/-0/Escape/Delete）
- `reflectI18N()` 切换语言时刷新所有 UI 文字
- `updateStats()` 遍历树统计层数/节点数/字符数，更新状态栏

---

## 后端模块职责

### `server.c` — HTTP 服务器
- 基于 WinSock2，零外部依赖
- `server_init()` 创建 TCP socket → bind → listen
- `server_start()` 进入 `accept` 循环，每个连接：
  1. 读取 HTTP 请求字符串
  2. `parse_request()` 解析 method / path / body
  3. `handle_request()` 匹配路由表 → 调用对应 handler
  4. 如果是 API 路由 → 调 handler 函数
  5. 否则 → `serve_static_file()` 从 `web/` 目录读文件返回
- 15 个 API handler 函数，每个对应一个端点
- JSON 请求解析使用简单的字符串搜索（无需完整 JSON 库）

### `tree.c` — 树数据结构
- 一期已有的核心模块，二期扩展了：
  - `NodeFormatType` 枚举（FMT_PLAIN / FMT_LATEX / FMT_HTML）
  - `TreeConfig` 结构体（画布颜色、默认尺寸、编码、缩放）
  - `TreeNode` 新字段（expanded、custom_color、custom_width、custom_height、format_type）
  - `tree_to_json()` — 完整树序列化为 JSON 字符串（含所有元数据）
  - `tree_from_json()` — JSON 字符串反序列化为 Tree（含简易递归下降 JSON 解析器）

### `lxmm_handler.c` — .lxmm 二进制格式
- 文件格式：32 字节文件头 + 配置块 + 前序遍历节点块
- 每节点变长编码（flags 标记存在哪些可选字段）
- `lxmm_serialize()` — Tree → 二进制文件
- `lxmm_parse()` — 二进制文件 → Tree
- `lxmm_detect()` — 扩展名 + 魔数双重验证

---

## 典型操作的数据流

### 操作：双击编辑节点文字

```
用户双击节点
  → interaction.js: onDoubleClick()
  → 命中检测找到节点 → startInlineEdit(node)
  → 在节点屏幕位置创建 textarea
用户修改文字，按 Enter
  → API.updateNode(addr, {content: "新文字"})
  → POST /api/tree/node/update
C 后端:
  → find_node_by_addr() 定位节点
  → tree_node_set_content() 更新内容
  → 返回 {"ok":true}
前端:
  → API.getTree() 拉取最新树
  → State.treeData 更新
  → updateStats() 刷新状态栏
  → render() 重绘画布
```

### 操作：拖拽移动子树

```
用户按住节点拖拽
  → interaction.js: onMouseDown → Mouse.dragNode = 节点
  → onMouseMove → Mouse.isDragging = true
  → 边缘自动滚动（靠近画布边缘时）
用户松手
  → onMouseUp → hitTestNode() 找到目标
  → 检查不是拖入自己子孙
  → API.moveNode(addr, targetAddr, index)
  → POST /api/tree/node/move
C 后端:
  → 从旧父节点 children 中移除
  → 插入到新父节点 children 的指定位置
  → 重新计算深度
  → 返回新地址
前端:
  → getTree() → render() 重绘
```

### 操作：保存为 .lxmm 文件

```
用户点击 Save
  → API.saveFile(filename, State.treeData)
  → POST /api/file/save
C 后端:
  → format_find_by_extension → 找到 LXMM_HANDLER
  → lxmm_serialize(filepath, tree)
      → 写文件头（"LXMM" 魔数 + 版本号 + 偏移量）
      → 写配置块（canvas_color, default_width...）
      → 递归写节点（flags + child_count + content + color + size）
  → 返回 {"ok":true}
```

---

## 地址系统

节点的"地址"是其在树中的位置标识，格式为点分隔的 1-based 索引。

例如 `"2.1.3"` 表示：root 的第 2 个子节点 → 该节点的第 1 个子节点 → 该节点的第 3 个子节点。

```
root
├─ (1) 第一章          ← 地址: "1"
│   ├─ (1.1) 概述      ← 地址: "1.1"
│   └─ (1.2) 详述      ← 地址: "1.2"
├─ (2) 第二章          ← 地址: "2"
│   ├─ (2.1) 背景      ← 地址: "2.1"
│   └─ (2.2) 方法      ← 地址: "2.2"
│       └─ (2.2.1) 实验 ← 地址: "2.2.1"
```

C 端的 `find_node_by_addr()` 和 JS 端的 `findNodeByAddr()` 都按此规则解析地址并定位节点。

---

## 文件结构总览

```
mind_map/
├── include/               C 头文件
│   ├── tree.h             树结构定义 + JSON 桥接声明
│   ├── format_handler.h   格式处理器插件接口
│   ├── lxmm_handler.h     .lxmm 格式处理器
│   ├── server.h           HTTP 服务器模块
│   ├── converter.h        格式转换编排
│   ├── json/txt/md_handler.h  各格式处理器
│   ├── encoding.h         编码检测/转换
│   ├── i18n.h             后端翻译
│   ├── gui.h              一期 GUI（保留）
│   └── utils.h            通用工具
├── src/                   C 源文件（.c 100%）
│   ├── main.c             入口点
│   ├── server.c           HTTP 服务器 + 全部 API 实现
│   ├── tree.c             树结构 + tree_to/from_json
│   ├── lxmm_handler.c     .lxmm 二进制解析/序列化
│   ├── json/txt/md_handler.c  各格式处理器
│   ├── converter.c        格式转换
│   ├── format_handler.c   处理器注册表
│   ├── encoding.c         编码处理
│   ├── i18n.c             翻译表
│   ├── gui.c              一期 GUI（保留）
│   └── utils.c            工具函数
├── web/                   前端文件
│   ├── index.html         主页面
│   ├── css/style.css      样式
│   └── js/
│       ├── i18n.js        翻译
│       ├── api.js         API 通信
│       ├── renderer.js    Canvas 渲染
│       ├── interaction.js 鼠标/键盘交互
│       ├── menus.js       右键菜单/剪贴板
│       ├── undo.js        撤销/重做
│       ├── search.js      搜索
│       └── app.js         主控制器
├── build.sh               MSVC 构建脚本
├── Makefile               GCC Makefile
└── README.md              项目说明
```
