#ifndef XMENUTEST_H
#define XMENUTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
XMenu* XMenuTest_create();
int XMenuTest_show(XMenu* menu,int column);
int XMenuTest_run();
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
/** @brief 远程菜单处理结果；会话仍活动时返回 Active，输入 q 后返回 Finished。 */
typedef enum XMenuTestRemoteResult {
    XMenuTestRemoteResult_Finished = 0,
    XMenuTestRemoteResult_Active = 1,
    XMenuTestRemoteResult_Error = -1
} XMenuTestRemoteResult;

/**
 * @brief 远程可增量驱动的旧测试菜单状态。
 * @details
 * 该状态复用 XMenuTest_run 使用的菜单树和动作，避免远程 Shell 阻塞在
 * stdin。对象由 XMenuTestRemoteSession_create 创建并由 destroy 释放。
 */
typedef struct XMenuTestRemoteSession XMenuTestRemoteSession;

/** @brief 创建远程菜单状态并建立旧测试菜单树；失败返回 NULL。 */
XMenuTestRemoteSession* XMenuTestRemoteSession_create(void);
/** @brief 输出当前菜单页；session 无效时不执行操作。 */
void XMenuTestRemoteSession_show(XMenuTestRemoteSession* session);
/**
 * @brief 处理一行远程菜单输入并输出下一页。
 * @param session 远程菜单状态；不能为空。
 * @param line UTF-8 输入行；不含结尾 NUL，可为空行。
 * @param length line 的字节数；不含结尾 NUL。
 * @return q/Q 返回 Finished；菜单继续运行返回 Active；参数或资源错误返回 Error。
 */
XMenuTestRemoteResult XMenuTestRemoteSession_processLine(
    XMenuTestRemoteSession* session, const char* line, size_t length);
/** @brief 销毁远程菜单状态和其拥有的菜单树；可传 NULL。 */
void XMenuTestRemoteSession_destroy(XMenuTestRemoteSession* session);
#endif
#ifdef __cplusplus
}
#endif	
#endif
