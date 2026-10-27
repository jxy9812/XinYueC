/** @file       XLinkFileServer.h
 * @brief      XLink 文件传输服务端：根目录沙箱 + 策略 + 会话绑定。
 * @details    把 XLinkFileTransfer 引擎挂到 XLinkServer 上，提供：
 *               - 根目录沙箱：所有落盘/读取都限制在配置的根目录内，
 *                 拒绝绝对路径、目录穿越、隐藏文件、软链接逃逸；
 *               - 策略开关：上传/下载方向、单文件大小上限、覆盖策略、
 *                 是否要求已登录、角色门槛；
 *               - 会话绑定：每个会话最多一个传输；会话回收时自动清理；
 *               - 与认证联动：可要求会话已认证（默认要求）。
 *
 *             安全边界（重要）：
 *               - XLink 协议本身不加密；跨网段请叠加 TLS（XSslSocket）；
 *               - 本模块只做路径与权限校验，不做内容扫描；
 *               - 拒绝符号链接（写入侧）与越界读取（读取侧）。
 * @author     XinYueC 团队
 */
#ifndef XLINKFILESERVER_H
#define XLINKFILESERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_FILE_ON

#include <stdint.h>
#include <stdbool.h>
#include "XLinkFileTransfer.h"
#include "XLinkServer.h"

/**
 * @brief 服务端文件传输策略。
 */
typedef struct XLinkFileServerPolicy {
    XString* m_rootDir;        /**< 沙箱根目录（对象拥有，XString UTF-8）。 */
    uint64_t m_maxFileBytes;   /**< 单文件大小上限（0 = 不限制）。 */
    uint32_t m_chunkBytes;     /**< 下发给对端的块大小。 */
    uint32_t m_windowBytes;    /**< 下发给对端的窗口。 */
    uint8_t  m_flags;          /**< 策略位（见 XLINK_FILESRV_*）。 */
    uint8_t  m_minRole;        /**< 允许传输的最低角色（XLinkAuthRole）。 */
} XLinkFileServerPolicy;

/** @brief 允许上传（客户端 -> 服务端）。 */
#define XLINK_FILESRV_ALLOW_UPLOAD   0x01u
/** @brief 允许下载（服务端 -> 客户端）。 */
#define XLINK_FILESRV_ALLOW_DOWNLOAD 0x02u
/** @brief 允许覆盖已存在文件（否则拒绝）。 */
#define XLINK_FILESRV_ALLOW_OVERWRITE 0x04u
/** @brief 要求会话已通过认证（默认开启）。 */
#define XLINK_FILESRV_REQUIRE_AUTH   0x08u
/** @brief 允许在根目录下创建子目录（默认关闭）。 */
#define XLINK_FILESRV_ALLOW_SUBDIR   0x10u

/** @brief 服务端文件传输会话槽（每会话一个）。 */
typedef struct XLinkFileServerSession {
    XLinkSession* m_session;      /**< 关联会话（借用）。 */
    XLinkFileTransfer m_transfer; /**< 传输引擎。 */
    bool m_used;                  /**< 槽位占用。 */
} XLinkFileServerSession;

/**
 * @brief 文件传输服务端对象。
 * @details 值类型；init/deinit 配对。attach 到 XLinkServer 后由服务端的
 *          tick 回调驱动。
 */
typedef struct XLinkFileServer {
    XLinkServer* m_server;     /**< 挂接的服务端（借用）。 */
    XLinkFileServerPolicy m_policy; /**< 策略。 */
    XLinkFileServerSession m_slots[XLINK_FILE_MAX_TRANSFERS]; /**< 会话槽。 */
    XString* m_resolvedRoot;   /**< 规范化后的根目录（对象拥有；首次使用时解析）。 */
    bool m_attached;           /**< 已挂接。 */
    bool m_inTick;             /**< tick 重入保护：服务端 timerEvent 与手动
                                    pump 并存时，同一轮不重复推进传输。 */
} XLinkFileServer;

/**
 * @brief      初始化文件传输服务端（默认策略：禁止任何方向，要求认证）。
 * @param      fs 对象（调用方提供存储）；NULL 时无操作。
 */
void XLinkFileServer_init(XLinkFileServer* fs);

/**
 * @brief      反初始化：中止并清理全部传输。
 * @param      fs 对象；NULL 时无操作。
 * @note       不修改 XLinkServer 本身（由调用方负责 detach 或销毁顺序）。
 */
void XLinkFileServer_deinit(XLinkFileServer* fs);

/**
 * @brief      设置沙箱根目录。
 * @param      fs 对象。
 * @param      dir UTF-8 目录路径（必须已存在）。
 * @return     成功返回 true；参数非法/目录不存在返回 false。
 * @note       路径深拷贝（XString）；规范化在首次使用时进行。
 */
bool XLinkFileServer_setRootDir(XLinkFileServer* fs, const char* dir);

/**
 * @brief      设置允许的方向与策略位。
 * @param      fs 对象。
 * @param      flags XLINK_FILESRV_* 位组合。
 * @return     成功返回 true。
 */
bool XLinkFileServer_setFlags(XLinkFileServer* fs, uint8_t flags);

/**
 * @brief      设置单文件大小上限。
 * @param      fs 对象；bytes 0 表示不限制。
 * @return     成功返回 true。
 */
bool XLinkFileServer_setMaxFileBytes(XLinkFileServer* fs, uint64_t bytes);

/**
 * @brief      设置允许传输的最低角色。
 * @param      fs 对象；role 见 XLinkAuthRole。
 * @return     成功返回 true。
 */
bool XLinkFileServer_setMinRole(XLinkFileServer* fs, uint8_t role);

/**
 * @brief      挂接到服务端：接管会话的 File 消息与节拍驱动。
 * @param      fs 对象。
 * @param      server 服务端（借用；须已 listen 或即将 listen）。
 * @return     成功返回 true。
 * @note       会安装自己的 dispatch 包装与 tick 回调；若服务端已注册业务
 *             dispatch/tick，本函数用链式方式保留它们（见实现说明）。
 */
bool XLinkFileServer_attach(XLinkFileServer* fs, XLinkServer* server);

/**
 * @brief      从服务端摘除。
 * @param      fs 对象。
 * @return     无。
 */
void XLinkFileServer_detach(XLinkFileServer* fs);

/**
 * @brief      获取根目录（只读借用）。
 * @param      fs 对象（借用）；NULL 时返回 NULL。
 */
const char* XLinkFileServer_rootDir_const(const XLinkFileServer* fs);

#endif /* XLINK_ON && XLINK_FILE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKFILESERVER_H */
