/* ============================================================
 * search.js - Fuzzy Search & Address Locate
 * search.js - 模糊搜索与地址定位
 *
 * Implements fuzzy search with substring matching and scoring.
 * Also provides address-based node navigation.
 * 实现模糊搜索（子串匹配 + 评分）和地址定位导航。
 * ============================================================ */

/* --- Levenshtein Distance / 编辑距离 --- */
function levenshtein(a, b) {
    const alen = a.length, blen = b.length;
    if (alen === 0) return blen;
    if (blen === 0) return alen;

    /* Use single-row optimization for memory efficiency */
    let prev = new Array(blen + 1);
    let curr = new Array(blen + 1);
    for (let j = 0; j <= blen; j++) prev[j] = j;

    for (let i = 1; i <= alen; i++) {
        curr[0] = i;
        for (let j = 1; j <= blen; j++) {
            const cost = a[i - 1] === b[j - 1] ? 0 : 1;
            curr[j] = Math.min(
                prev[j] + 1,      /* deletion */
                curr[j - 1] + 1,  /* insertion */
                prev[j - 1] + cost /* substitution */
            );
        }
        const tmp = prev; prev = curr; curr = tmp;
    }
    return prev[blen];
}

/* Fuzzy match score: higher is better match.
 * 模糊匹配得分：越高越匹配。                                      */
function fuzzyScore(text, query) {
    if (!text || !query) return 0;

    const t = text.toLowerCase();
    const q = query.toLowerCase();

    /* Exact match bonus */
    if (t === q) return 2000;

    /* Starts-with bonus */
    if (t.startsWith(q)) return 1500 + Math.max(0, 500 - t.length);

    /* Contains substring: higher score for earlier match */
    const idx = t.indexOf(q);
    if (idx >= 0) return 1000 + Math.max(0, 500 - idx);

    /* Word boundary match (after space, dash, etc.) */
    const wordPattern = new RegExp('(^|[\\s\\-_.])' + q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i');
    if (wordPattern.test(t)) return 800;

    /* Levenshtein-based fuzzy match for short queries */
    if (q.length >= 2 && q.length <= t.length) {
        const dist = levenshtein(t.substring(0, Math.min(t.length, q.length + 5)), q);
        const maxLen = Math.max(t.length, q.length);
        const similarity = 1 - dist / maxLen;
        if (similarity > 0.4) return Math.round(similarity * 600);
    }

    return 0;
}

/* Client-side search: scan all nodes and rank by score.
 * This is a fallback — the backend /api/search is preferred.
 * 客户端搜索：扫描所有节点并按得分排序。
 * 这是回退方案 — 优先使用后端 /api/search。                      */
function searchNodes(query) {
    const results = [];
    if (!State.treeData?.root) return results;

    function scan(node, path) {
        if (!node) return;
        if (node.content && node.content.length > 0) {
            const score = fuzzyScore(node.content, query);
            if (score > 0) {
                results.push({
                    addr: path || '1',
                    content: node.content.substring(0, 80),
                    score: score
                });
            }
        }
        if (node.children) {
            for (let i = 0; i < node.children.length; i++) {
                const childPath = path ? path + '.' + (i + 1) : '' + (i + 1);
                scan(node.children[i], childPath);
            }
        }
    }
    scan(State.treeData.root, '');
    results.sort((a, b) => b.score - a.score);
    return results;
}
