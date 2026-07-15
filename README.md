# Mind Map — 思维导图编辑器 & 多格式转换工具

> I've always regretted that there's no tool out there that automatically scans my thoughts and generates a .mind file the instant I hit "Save". So I built this project to make things just a little better — and maybe it can help you too.

## 概述

一个集**思维导图可视化编辑**与**树状结构多格式互转**于一体的工具。解决了树状信息（学习笔记、PPT大纲、知识图谱等）不易记录、不易直观查看的问题。

**Phase 2** 革命性升级：从批量文件转换器进化为完整的思维导图编辑器，采用 **Web 前端 + C 后端** 架构，兼顾现代 UI 体验和高性能文件处理。

## 功能

### 🧠 思维导图编辑器（二期新增）
- **可视化渲染**：节点圆角卡片 + 贝塞尔曲线弧线，自动配色
- **完整编辑**：悬停四向按钮（展开折叠/加子节点/前后插同级）、双击内联编辑、拖拽移动子树
- **右键菜单**：编辑、插入、删除、颜色/尺寸/格式设置、展开折叠全部
- **撤销/重做**：Ctrl+Z/Y，完整编辑历史
- **模糊搜索**：子串匹配 + 编辑距离评分，地址定位导航
- **缩放平移**：Ctrl+滚轮缩放，拖拽空白平移，适应全部
- **图片导出**：Canvas 渲染导出 PNG/JPG/BMP
- **多窗口**：支持多个浏览器标签页独立编辑

### 📂 格式转换（一期功能保留）
- **JSON** ↔ **TXT** ↔ **MD** ↔ **LXMM** 多格式互转
- .lxmm 专有二进制格式：完整保留元数据（颜色、尺寸、展开状态、格式类型、画布配置）
- 批量转换 + 中文路径支持 + 编码自动检测（UTF-8/UTF-16/GBK）

### 🌐 国际化
- 中英文界面实时切换（前端 + 后端独立 i18n）
- 自动检测浏览器/系统语言

## 架构

```
┌──────────────────────────────────────┐
│        浏览器 (HTML/CSS/JS)           │
│  Canvas 渲染 | 交互 | 右键菜单 | i18n │
└──────────────┬───────────────────────┘
               │  HTTP JSON API
               │  localhost:8080
┌──────────────┴───────────────────────┐
│         C 后端 (WinSock2)             │
│  HTTP Server | Tree Engine | Handlers │
│  .lxmm | JSON | TXT | MD | Encoding  │
└──────────────────────────────────────┘
```

## 支持的格式

| 格式 | 导入 | 导出 | 说明 |
|------|------|------|------|
| .lxmm | ✅ | ✅ | 专有二进制格式，存储完整元数据 |
| .json | ✅ | ✅ | 列表模式 + 索引0枝信息 |
| .txt | ✅ | ✅ | 编号大纲格式（1./1.1./2.） |
| .md | ✅ | ✅ | Markdown 标题层级 |

## 构建

### 依赖
- MSVC 编译器（Visual Studio Build Tools）
- Windows SDK
- Git Bash（运行构建脚本）

### 构建命令
```bash
bash build.sh
```

### 运行
```bash
./mind_map.exe          # 启动 HTTP 服务器模式（默认）
./mind_map.exe --gui    # 启动一期 Win32 GUI 模式
```

启动后浏览器自动打开 `http://localhost:8080`。

## 项目结构

```
mind_map/
├── include/              # C 头文件 (.h)
│   ├── tree.h            # 核心树数据结构 + JSON 桥接
│   ├── format_handler.h  # 格式处理器插件接口
│   ├── lxmm_handler.h    # .lxmm 二进制格式处理器
│   ├── server.h          # HTTP 服务器模块
│   ├── converter.h       # 格式转换编排器
│   ├── json_handler.h    # JSON 处理器
│   ├── txt_handler.h     # TXT 编号大纲处理器
│   ├── md_handler.h      # Markdown 处理器
│   ├── encoding.h        # 编码检测与转换
│   ├── i18n.h            # 国际化
│   ├── gui.h             # Win32 GUI（一期保留）
│   └── utils.h           # 通用工具
├── src/                  # C 源文件 (.c)
│   ├── main.c            # 入口点（HTTP 服务器模式）
│   ├── server.c          # HTTP 服务器 + JSON API 实现
│   ├── tree.c            # 树数据结构 + JSON 序列化/反序列化
│   ├── lxmm_handler.c    # .lxmm 二进制格式解析/序列化
│   ├── json_handler.c    # JSON 解析/序列化（递归下降）
│   ├── txt_handler.c     # TXT 编号大纲解析/序列化
│   ├── md_handler.c      # Markdown 解析/序列化
│   ├── converter.c       # 格式转换编排（单文件+批量）
│   ├── format_handler.c  # 格式处理器注册表
│   ├── encoding.c        # 编码检测与转换（Windows API）
│   ├── i18n.c            # 中英文翻译表
│   ├── gui.c             # Win32 GUI 实现（一期保留）
│   └── utils.c           # 字符串/路径/内存工具
├── web/                  # Web 前端 (HTML/CSS/JS)
│   ├── index.html        # 主页面
│   ├── css/style.css     # 现代样式
│   └── js/
│       ├── i18n.js       # 前端中英文切换
│       ├── api.js        # C 后端 API 客户端
│       ├── renderer.js   # Canvas 思维导图渲染引擎
│       ├── interaction.js# 鼠标/键盘交互
│       ├── menus.js      # 右键菜单 + 剪贴板 + 颜色尺寸
│       ├── undo.js       # 撤销/重做
│       ├── search.js     # 模糊搜索
│       └── app.js        # 主应用控制器
├── obj/                  # 编译产物
├── build.sh              # MSVC 构建脚本
├── Makefile              # GCC/MinGW Makefile
└── README.md
```

## 技术栈

| 层 | 技术 |
|----|------|
| 前端渲染 | HTML5 Canvas API |
| 前端交互 | 原生 JavaScript (ES6) |
| 前端样式 | CSS3 (变量/动画/现代布局) |
| 后端 HTTP | WinSock2 C (零依赖) |
| 文件解析 | 手写递归下降解析器 |
| 编码处理 | Windows MultiByteToWideChar API |
| 构建 | MSVC + Git Bash |

## 未来规划

- [ ] LaTeX 和 HTML 渲染器
- [ ] 合并（merge）功能
- [ ] 主题切换（亮色/暗色）
- [ ] WebView2 嵌入窗口模式
- [ ] 跨平台支持（Linux/macOS）

## 许可证

本项目仅供个人和教育用途。
