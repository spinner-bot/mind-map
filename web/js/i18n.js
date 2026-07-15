/* ============================================================
 * i18n.js - Frontend Internationalization
 * i18n.js - 前端国际化
 *
 * Simple EN↔ZH translation for all UI strings. Mirrors the
 * C backend's i18n system but runs entirely in the browser.
 * Stores language preference in localStorage.
 * 简单的 EN↔ZH 翻译，覆盖所有 UI 字符串。与 C 后端 i18n 系统
 * 对应，但完全在浏览器中运行。语言偏好存储在 localStorage 中。
 * ============================================================ */

const I18N = {
    current: 'en',  /* Default language 默认语言 */

    strings: {
        /* Toolbar 工具栏 */
        'btn.new':       { en: '📄 New',              zh: '📄 新建' },
        'btn.open':      { en: '📂 Open',             zh: '📂 打开' },
        'btn.save':      { en: '💾 Save',             zh: '💾 保存' },
        'btn.export':    { en: '📤 Export',           zh: '📤 导出' },
        'btn.config':    { en: '⚙ Settings',         zh: '⚙ 设置' },
        'search.placeholder': { en: 'Search...',      zh: '搜索...' },
        'locate.placeholder': { en: 'Addr e.g. 1.2.3', zh: '地址 如 1.2.3' },

        /* Preview Panel 预览面板 */
        'panel.title':   { en: 'Node Info',            zh: '节点信息' },
        'panel.addr':    { en: 'Address',              zh: '地址' },
        'panel.content': { en: 'Content',              zh: '内容' },
        'panel.children':{ en: 'Children',             zh: '子节点' },
        'panel.chars':   { en: 'Characters',           zh: '字符数' },
        'panel.format':  { en: 'Format',               zh: '格式' },
        'panel.edit':    { en: 'Edit',                 zh: '编辑' },
        'panel.addchild':{ en: 'Add Child',            zh: '加子节点' },
        'panel.close':   { en: 'Close',                zh: '关闭' },
        'panel.none':    { en: '—',                    zh: '—' },

        /* Status Bar 状态栏 */
        'status.layers': { en: 'Layers',               zh: '层' },
        'status.nodes':  { en: 'Nodes',                zh: '节点' },
        'status.chars':  { en: 'chars',                zh: '字符' },

        /* Format names 格式类型 */
        'format.plain':  { en: 'Plain Text',           zh: '纯文本' },
        'format.latex':  { en: 'LaTeX',                zh: 'LaTeX' },
        'format.html':   { en: 'HTML',                 zh: 'HTML' },

        /* Context Menu 右键菜单 */
        'menu.edit':        { en: '✏ Edit Content',      zh: '✏ 编辑内容' },
        'menu.addChild':    { en: '📋 Add Child Node',   zh: '📋 插入子节点' },
        'menu.insertBefore':{ en: '⬆ Insert Before',     zh: '⬆ 在前插入同级' },
        'menu.insertAfter': { en: '⬇ Insert After',      zh: '⬇ 在后插入同级' },
        'menu.delete':      { en: '🗑 Delete Node',      zh: '🗑 删除节点' },
        'menu.cut':         { en: '✂ Cut',               zh: '✂ 剪切' },
        'menu.copy':        { en: '📋 Copy',             zh: '📋 复制' },
        'menu.paste':       { en: '📌 Paste',            zh: '📌 粘贴' },
        'menu.color':       { en: '🎨 Color',            zh: '🎨 颜色' },
        'menu.color.auto':  { en: 'Auto (by depth)',     zh: '自动（按层级）' },
        'menu.color.custom':{ en: 'Custom Color...',     zh: '自定义颜色...' },
        'menu.color.subtree':{ en: 'Apply to Subtree',   zh: '应用到子树' },
        'menu.size':        { en: '📐 Size',             zh: '📐 尺寸' },
        'menu.size.auto':   { en: 'Auto Fit',            zh: '自动适配' },
        'menu.size.custom': { en: 'Custom Size...',      zh: '自定义尺寸...' },
        'menu.format':      { en: '📝 Format',           zh: '📝 格式' },
        'menu.format.plain':{ en: 'Plain Text',          zh: '纯文本' },
        'menu.format.latex':{ en: 'LaTeX (reserved)',    zh: 'LaTeX（预留）' },
        'menu.format.html': { en: 'HTML (reserved)',     zh: 'HTML（预留）' },
        'menu.expandAll':   { en: '🔽 Expand All',       zh: '🔽 展开全部' },
        'menu.collapseAll': { en: '🔼 Collapse All',     zh: '🔼 折叠全部' },

        /* Canvas context menu 画布右键 */
        'canvas.color':     { en: 'Canvas Color...',     zh: '画布颜色...' },
        'canvas.defaultSize':{ en: 'Default Node Size...', zh: '默认节点尺寸...' },
        'canvas.defaultFormat':{ en: 'Default Format...', zh: '默认格式...' },
        'canvas.defaultEnc':{ en: 'Default Encoding...',  zh: '默认编码...' },

        /* Export 导出 */
        'export.title':     { en: 'Export Image',        zh: '导出图片' },
        'export.format':    { en: 'Format',              zh: '格式' },
        'export.zoom':      { en: 'Scale',               zh: '缩放' },
        'export.btn':       { en: 'Export',              zh: '导出' },
        'export.cancel':    { en: 'Cancel',              zh: '取消' },

        /* Dialog buttons */
        'dialog.ok':        { en: 'OK',                  zh: '确定' },
        'dialog.cancel':    { en: 'Cancel',              zh: '取消' },
        'dialog.confirmDelete': { en: 'Delete this node and all its children?', zh: '删除此节点及其所有子节点？' },

        /* Notifications */
        'notify.saved':     { en: 'File saved',          zh: '文件已保存' },
        'notify.exported':  { en: 'Exported successfully', zh: '导出成功' },
        'notify.copied':    { en: 'Copied to clipboard', zh: '已复制到剪贴板' },
    },

    /* Initialize: detect browser language or use stored preference.
     * 初始化：检测浏览器语言或使用存储的偏好。                     */
    init() {
        const stored = localStorage.getItem('mindmap-lang');
        if (stored === 'zh' || stored === 'en') {
            this.current = stored;
        } else {
            /* Auto-detect: if browser lang starts with 'zh', use Chinese */
            const navLang = (navigator.language || navigator.userLanguage || '');
            this.current = navLang.startsWith('zh') ? 'zh' : 'en';
        }
        return this.current;
    },

    /* Get translated string. 获取翻译字符串。                       */
    t(key) {
        const entry = this.strings[key];
        if (!entry) return key;  /* Missing key: return as-is */
        return entry[this.current] || entry.en;
    },

    /* Set language and persist. 设置语言并持久化。                 */
    setLang(lang) {
        if (lang === 'en' || lang === 'zh') {
            this.current = lang;
            localStorage.setItem('mindmap-lang', lang);
        }
    },

    /* Toggle language. 切换语言。                                  */
    toggle() {
        this.setLang(this.current === 'en' ? 'zh' : 'en');
    }
};
