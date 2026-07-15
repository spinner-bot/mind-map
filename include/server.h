/* ============================================================
 * server.h - HTTP Server Module (Phase 2)
 * server.h - HTTP 服务器模块（二期）
 *
 * Provides a lightweight HTTP server for the web frontend's
 * JSON API and static file serving. Based on WinSock2 — no
 * external dependencies.
 * 为 Web 前端的 JSON API 和静态文件服务提供轻量级 HTTP 服务器。
 * 基于 WinSock2 — 无外部依赖。
 *
 * Usage 用法:
 *   1. server_init()         — 初始化服务器
 *   2. server_set_tree()     — 设置当前工作的树（用于 API 响应）
 *   3. server_start()        — 启动 HTTP 服务 + 自动打开浏览器
 *   4. server_stop()         — 停止服务
 *   5. server_cleanup()      — 清理 WinSock2 资源
 * ============================================================ */

#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

/* Forward declaration 前向声明 */
struct Tree;

/* Server configuration 服务器配置 */
#define SERVER_PORT          8080
#define SERVER_HOST          "127.0.0.1"  /* localhost only 仅本地 */
#define SERVER_WEB_DIR       "web"        /* Static files directory 静态文件目录 */
#define SERVER_MAX_REQUEST   65536        /* Max HTTP request size 最大请求大小 */
#define SERVER_MAX_PATH      256          /* Max URL path length 最大路径长度 */

/* ================================================================
 * API Functions  API 函数
 * ================================================================ */

/* Initialize the HTTP server (prepare WinSock2, create socket).
 * 初始化 HTTP 服务器（准备 WinSock2、创建 socket）。
 * Returns true on success. 成功返回 true。                        */
bool server_init(void);

/* Set the tree that the server operates on. The server holds a
 * pointer to this tree (no copy is made). All /api/tree/* endpoints
 * operate on this tree. Call this before server_start().
 * 设置服务器操作的树。服务器持有此树的指针（不制作副本）。
 * 所有 /api/tree/* 端点操作此树。在 server_start() 之前调用。    */
void server_set_tree(struct Tree* tree);

/* Start the HTTP server. This function blocks until server_stop()
 * is called from another thread/signal, or an error occurs.
 * Automatically opens the default browser to the app URL.
 * 启动 HTTP 服务器。此函数阻塞直到 server_stop() 被调用或出错。
 * 自动打开默认浏览器访问应用 URL。                               */
int server_start(void);

/* Stop the HTTP server gracefully. Can be called from any thread.
 * 优雅地停止 HTTP 服务器。可从任何线程调用。                     */
void server_stop(void);

/* Cleanup WinSock2 resources. Call at application exit.
 * 清理 WinSock2 资源。在应用程序退出时调用。                     */
void server_cleanup(void);

#endif /* SERVER_H */
