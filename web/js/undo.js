/* ============================================================
 * undo.js - Undo/Redo System
 * undo.js - 撤销/重做系统
 *
 * Simple undo/redo using tree JSON snapshots. Each snapshot
 * stores the full tree state. On undo/redo, the tree is
 * replaced with the stored snapshot.
 * 使用树 JSON 快照实现简单撤销/重做。每个快照存储完整树状态。
 * 撤销/重做时用存储的快照替换树。
 * ============================================================ */

const UndoManager = {
    stack: [],        /* Array of { tree: json, addr: selected } */
    index: -1,        /* Current position in stack */
    maxSize: 256,     /* Maximum undo depth 最大撤销深度 */

    /* Push a snapshot to the undo stack. Clears redo entries
     * beyond current position. 推入快照到撤销栈。清除当前位置
     * 之后的重做条目。                                           */
    push(treeData) {
        /* Remove any redo entries beyond current index */
        this.stack.splice(this.index + 1);

        /* Deep clone the tree data for the snapshot */
        const snapshot = JSON.parse(JSON.stringify({
            tree: treeData,
            selectedAddr: State.selectedAddr
        }));

        this.stack.push(snapshot);

        /* Trim to max size from the beginning */
        if (this.stack.length > this.maxSize) {
            this.stack.shift();
        } else {
            this.index++;
        }

        this.updateButtons();
    },

    /* Undo: revert to previous snapshot. 撤销：回退到上一快照。   */
    async undo() {
        if (this.index <= 0) return;
        this.index--;
        await this.restore();
    },

    /* Redo: advance to next snapshot. 重做：前进到下一快照。      */
    async redo() {
        if (this.index >= this.stack.length - 1) return;
        this.index++;
        await this.restore();
    },

    /* Restore tree from current snapshot. 从当前快照恢复树。      */
    async restore() {
        const snapshot = this.stack[this.index];
        if (!snapshot) return;

        State.treeData = JSON.parse(JSON.stringify(snapshot.tree));
        State.selectedAddr = snapshot.selectedAddr;

        /* Update backend 更新后端 */
        try {
            await API.replaceTree(State.treeData);
        } catch (err) {
            console.warn('Backend sync failed during undo:', err);
        }

        State.isDirty = true;
        updateStats();
        render();

        /* Update preview panel */
        if (State.selectedAddr) {
            updatePreviewPanel(State.selectedAddr);
            document.getElementById('preview-panel').classList.remove('collapsed');
        }

        this.updateButtons();
    },

    /* Update undo/redo button states. 更新撤销/重做按钮状态。    */
    updateButtons() {
        document.getElementById('btn-undo').disabled = (this.index <= 0);
        document.getElementById('btn-redo').disabled =
            (this.index >= this.stack.length - 1);
    },

    /* Clear the entire undo stack. 清除整个撤销栈。               */
    clear() {
        this.stack = [];
        this.index = -1;
        this.updateButtons();
    }
};

/* --- Global undo/redo functions (called from app.js) --- */
function onUndo() {
    UndoManager.undo();
}

function onRedo() {
    UndoManager.redo();
}

/* Hook: push snapshot whenever the tree changes significantly.
 * This is called from app.js after operations like add/delete/move.
 * 钩子：每当树有显著变化时推入快照。
 * 在 app.js 中的增/删/移操作后调用此函数。                      */
function pushUndoSnapshot() {
    if (State.treeData) {
        UndoManager.push(State.treeData);
    }
}
