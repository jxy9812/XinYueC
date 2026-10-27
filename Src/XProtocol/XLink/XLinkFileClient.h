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

#if XLINK_ON && XLINK_FILE_ON && XLINK_CLIENT_ON

#include <stdint.h>
#include <stdbool.h>
#include "XLinkFileTransfer.h"
#include "XLinkClient.h"
#include "XObject.h"

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XLinkFileClient)
/* 定时器事件槽（与 XLinkClient 同口径）。 */
XCLASS_DEFINE_ENUM(XLinkFileClient, timerEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XLinkFileClient)

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
 * @brief      推进一步传输（手动驱动；无事件分发器时必须调用）。
 * @param      obj 对象。
 * @param      nowMs 当前单调毫秒（可传 0 由内部取时）。
 * @return     无。
 * @note       异步环境（有事件分发器）下由内建定时器自动驱动，本函数可选；
 *             手动/测试环境（无分发器，定时器不生效）必须按圈调用，
 *             否则发送侧不会续发数据块。
 */
void XLinkFileClient_pump(XLinkFileClient* obj, uint32_t nowMs);

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

#endif /* XLINK_ON && XLINK_FILE_ON && XLINK_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKFILECLIENT_H */
