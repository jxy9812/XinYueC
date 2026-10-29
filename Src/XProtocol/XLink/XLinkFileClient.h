/** @file       XLinkFileClient.h
 * @brief      XLink 文件传输客户端：upload / download / abort + 进度信号。
 * @details    在 XLinkClient 之上提供文件传输便捷接口：
 *               - upload/download 发起传输（自动选 transferId、自动挂回调）；
 *               - 进度与终局经信号上报（对齐 XLink 家族 XEmitSignal 风格）；
 *               - 同一时刻只允许一个活跃传输（简化状态与内存）；
 *               - 客户端会话重建（重连）时自动中止旧传输，避免悬挂。
 *
 *             线程模型：单线程主循环，与 XLinkClient 一致——所有 API 不可
 *             阻塞，须在持有者的事件循环内调用。
 * @author     XinYueC 团队
 */
#ifndef XLINKFILECLIENT_H
#define XLINKFILECLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_FILE_ON && XLINK_CLIENT_ON

#include <stdint.h>
#include <stdbool.h>
#include "XLinkFileTransfer.h"
#include "XLinkClient.h"
#include "XObject.h"

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XLinkFileClient)
XCLASS_DEFINE_EXTEND_END(XLinkFileClient, XObject)

/**
 * @brief 会话驱动模式（pump 是否代泵承载会话）。
 * @details 单驱动口径：会话挂载传输时已自动接好异步驱动（内建定时器 +
 *          readyRead，见 XLinkSession attachAsync），异步环境下本类再泵
 *          会话即成「顺序双泵」。默认 Auto 按本线程分发器在位与否门控，
 *          心跳拍编译为 0（XLINK_HEARTBEAT_TICK_MS == 0）时无条件代泵
 *          （会话契约要求手动 pump）。注意真实 TCP 的读写均为事件驱动
 *          （见 XAbstractSocket），"分发器在位却不转事件"的宿主即便
 *          Force 也无法搬运数据——Force 面向环回等同步传输的手动驱动
 *          与诊断对比，不是真实 TCP 无事件宿主的救生出口。
 */
typedef enum XLinkFileClientSessionDrive {
    XLinkFileClientSessionDrive_Auto = 0,        /**< 默认：无分发器或心跳拍为 0（手动环境）才代泵会话。 */
    XLinkFileClientSessionDrive_ForceSessionPump = 1 /**< 强制：每圈 pump 都代泵会话（旧版行为）。 */
} XLinkFileClientSessionDrive;

/**
 * @brief 文件传输客户端对象。
 * @details 拥有一个传输引擎；自身是 XObject，可接收节拍驱动。
 */
typedef struct XLinkFileClient {
    XObject m_class;          /**< 基类；第一个成员、由 XObject 管理。 */
    XLinkClient* m_client;    /**< 关联客户端（借用，不取得所有权）。 */
    XLinkFileTransfer m_transfer; /**< 传输引擎（对象拥有）。 */
    uint32_t m_nextTransferId;/**< transferId 发生器（客户端用奇数）。 */
    uint32_t m_tickIntervalMs;/**< 节拍周期（默认 XLINK_FILE_TICK_MS）。 */
    XTimerId m_timerId;       /**< 内建节拍定时器（仅活跃传输期间运行）。 */
    int m_sessionDrive;       /**< 会话驱动模式（XLinkFileClientSessionDrive）。 */
} XLinkFileClient;

/* ========================================================================== */
/*                        构造与生命周期                                     */
/* ========================================================================== */

/**
 * @brief      初始化文件传输客户端（不关联 XLinkClient）。
 * @param      obj 对象（调用方提供存储）；NULL 时无操作。
 */
void XLinkFileClient_init(XLinkFileClient* obj);

/**
 * @brief      创建堆对象。
 * @return     成功返回新对象；失败返回 NULL。须用 XClassDelete 释放。
 */
XLinkFileClient* XLinkFileClient_create(void);

/**
 * @brief      关联到 XLinkClient（借用）。
 * @param      obj 对象。
 * @param      client 客户端（借用，调用方保证其存活期覆盖本对象）。
 * @return     成功返回 true；参数非法返回 false。
 * @note       关联后本对象会向该客户端的会话注册 File 回调（新会话由
 *             XLinkFileClient 在每次发起传输时重新挂接）。
 */
bool XLinkFileClient_attach(XLinkFileClient* obj, XLinkClient* client);

/* ========================================================================== */
/*                        发起传输                                           */
/* ========================================================================== */

/**
 * @brief      上传本地文件到服务端。
 * @param      obj 对象。
 * @param      localPath 本地文件路径（UTF-8）。
 * @param      remoteName 远端文件名（相对路径）。
 * @param      flags Open 标志（bit0 = 允许覆盖远端）。
 * @param      outTransferId 输出传输 id（可为 NULL）。
 * @return     成功返回 true（Open 已入队）。
 */
bool XLinkFileClient_upload(XLinkFileClient* obj, const char* localPath,
                            const char* remoteName, uint8_t flags,
                            uint32_t* outTransferId);

/**
 * @brief      从服务端下载文件。
 * @param      obj 对象。
 * @param      remoteName 远端文件名（相对路径）。
 * @param      localPath 本地落盘路径（UTF-8）。
 * @param      flags Open 标志（bit0 = 允许覆盖本地）。
 * @param      outTransferId 输出传输 id（可为 NULL）。
 * @return     成功返回 true。
 */
bool XLinkFileClient_download(XLinkFileClient* obj, const char* remoteName,
                              const char* localPath, uint8_t flags,
                              uint32_t* outTransferId);

/**
 * @brief      推进一步传输（手动驱动口；传输引擎的唯一通用推进通道）。
 * @param      obj 对象。
 * @param      nowMs 当前单调毫秒（可传 0 由内部取时）。
 * @return     无。
 * @note       驱动契约（单驱动原则，双泵禁令的边界在这里）：
 *               - 传输引擎本函数每圈必泵——它无任何异步驱动，
 *                 不调用则发送侧不续发数据块；
 *               - 承载会话默认**不**代泵：异步环境（本线程有事件分发器
 *                 且心跳拍 > 0，会话内建定时器与 readyRead 在位）会话已
 *                 由框架驱动，再泵一次即成双驱动；手动环境（无分发器，
 *                 定时器必然缺席，见 XObject_startTimer_ms；或心跳拍
 *                 编译为 0，会话契约要求手动 pump）才由本函数代泵会话；
 *               - nowMs 会注入会话时间基（m_nowTick/静默计时/心跳判定，
 *                 传输活跃期经 keepAlive 直达，门控不隔离它），异步环境
 *                 下请传 0（内部取实时），勿注入异构时钟；
 *               - "分发器在位却不转事件"的宿主 Auto 拒泵（可用
 *                 XLinkFileClient_setSessionDrive 切 Force），但真实 TCP
 *                 的读写均为事件驱动，此类宿主即便 Force 也无法搬运数据，
 *                 Force 只对环回等同步传输的手动驱动有意义。
 */
void XLinkFileClient_pump(XLinkFileClient* obj, uint32_t nowMs);

/**
 * @brief      设置会话驱动模式（缺省 Auto）。
 * @param      obj 对象。
 * @param      mode XLinkFileClientSessionDrive。
 * @return     设置成功返回 true；参数非法返回 false。
 * @note       仅影响 XLinkFileClient_pump 是否代泵会话，不影响本类内建
 *             节拍定时器；传输活跃中可随时切换，下一圈生效。
 */
bool XLinkFileClient_setSessionDrive(XLinkFileClient* obj,
                                     XLinkFileClientSessionDrive mode);

/**
 * @brief      读取当前会话驱动模式。
 * @param      obj 对象（借用）；NULL 时返回 Auto。
 */
XLinkFileClientSessionDrive XLinkFileClient_sessionDrive(
    const XLinkFileClient* obj);

/**
 * @brief      查询下一圈 pump 是否会代泵承载会话。
 * @param      obj 对象（借用）；NULL 时返回 false。
 * @return     与 XLinkFileClient_pump 的门控判定同源（单一事实源）：
 *             Force 恒 true；Auto 在无分发器或心跳拍为 0 时 true，
 *             异步环境 false。
 * @note       自定义宿主可据此决定是否自行驱动承载会话（查询动态求值，
 *             分发器创建/销毁后结果随之变化）。
 */
bool XLinkFileClient_willPumpSession(const XLinkFileClient* obj);

/**
 * @brief      中止当前传输。
 * @param      obj 对象。
 * @param      status 中止原因（XLinkFileStatus）；0 归一为 Aborted。
 * @param      text 说明文本（可为 NULL）。
 * @return     无。
 */
void XLinkFileClient_abort(XLinkFileClient* obj, uint8_t status,
                           const char* text);

/* ========================================================================== */
/*                        状态查询                                           */
/* ========================================================================== */

/**
 * @brief      是否有活跃传输（未终局）。
 * @param      obj 对象（借用）；NULL 时返回 false。
 */
bool XLinkFileClient_isBusy(const XLinkFileClient* obj);

/**
 * @brief      获取当前传输状态。
 * @param      obj 对象（借用）。
 */
XLinkFileTransferState XLinkFileClient_state(const XLinkFileClient* obj);

/**
 * @brief      已传输字节数。
 * @param      obj 对象（借用）。
 */
uint64_t XLinkFileClient_doneBytes(const XLinkFileClient* obj);

/**
 * @brief      文件总字节数。
 * @param      obj 对象（借用）。
 */
uint64_t XLinkFileClient_totalBytes(const XLinkFileClient* obj);

/**
 * @brief      获取传输引擎（高级用法，可再挂回调）。
 * @param      obj 对象（借用）；NULL 时返回 NULL。
 */
XLinkFileTransfer* XLinkFileClient_transfer(XLinkFileClient* obj);

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

/**
 * @brief 触发 progress 信号（传输推进时由内部发射）。
 * @param self 对象实例指针（非NULL）。
 * @param transferId 传输 id。
 * @param done 已完成字节数。
 * @param total 总字节数。
 * @return 信号标识。
 * @note 槽函数原型：void slot(XObject* sender, XVarList* args)，参数依次为
 *       uint32_t transferId、uint64_t done、uint64_t total。
 */
void* XLinkFileClient_progress_signal(XLinkFileClient* self,
                                      uint32_t transferId, uint64_t done,
                                      uint64_t total);

/**
 * @brief 触发 finished 信号（传输终局时由内部发射，成功/失败各一次）。
 * @param self 对象实例指针（非NULL）。
 * @param transferId 传输 id。
 * @param ok 是否成功。
 * @param status 结果码（XLinkFileStatus）。
 * @param text 结果文本（借用，可为 NULL）。
 * @return 信号标识。
 * @note 槽函数原型：void slot(XObject* sender, XVarList* args)，参数依次为
 *       uint32_t transferId、bool ok、uint8_t status、const char* text。
 */
void* XLinkFileClient_finished_signal(XLinkFileClient* self,
                                      uint32_t transferId, bool ok,
                                      uint8_t status, const char* text);

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_FILE_ON && XLINK_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKFILECLIENT_H */
