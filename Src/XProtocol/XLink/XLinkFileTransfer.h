/** @file       XLinkFileTransfer.h
 * @brief      XLink 文件传输引擎：单会话上的收发状态机（流控/续传/校验）。
 * @details    本模块把 XLinkFile 的线上格式变成可用的传输：
 *               - 一个对象承载**一个方向的一次传输**（发或收），
 *                 由 XLinkFileServer / XLinkFileClient 按需创建；
 *               - 发送侧：按 credit 窗口切片读文件、编码 DATA、推进偏移，
 *                 收 ACK 后补充额度；卡住超时即 Abort；
 *               - 接收侧：按 offset 定位写入 <目标>.part，累计 SHA-256，
 *                 按已消费量回 ACK 授信，Finish 时校验摘要并原子改名；
 *               - 所有 IO 都是增量的：每轮 pump 只做有限工作（受
 *                 chunkBytes / windowBytes 约束），不阻塞事件循环。
 *
 *             线程模型：单线程主循环口径，与会话一致——所有 API 不可阻塞、
 *             不加锁，须在持有者的事件循环内调用（文件引擎的 tick 由
 *             XLinkSession 的 File 回调驱动）。
 * @author     XinYueC 团队
 */
#ifndef XLINKFILETRANSFER_H
#define XLINKFILETRANSFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_FILE_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XLinkProto.h"
#include "XLinkSession.h"
#include "XFile.h"

/**
 * @brief 传输状态。
 */
typedef enum XLinkFileTransferState {
    XLinkFileTransferState_Idle      = 0, /**< 未启动。 */
    XLinkFileTransferState_Opening   = 1, /**< 已发 Open，等 OpenAck。 */
    XLinkFileTransferState_Active    = 2, /**< 正在传数据（受 credit 约束）。 */
    XLinkFileTransferState_Finishing = 3, /**< 已发 Finish，等 FinishAck。 */
    XLinkFileTransferState_Done      = 4, /**< 成功完成。 */
    XLinkFileTransferState_Failed    = 5  /**< 失败/中止（见 m_status）。 */
} XLinkFileTransferState;

/**
 * @brief 传输方向（复用线上枚举，便于直接比较）。
 */
typedef XLinkFileMode XLinkFileTransferDirection;

/**
 * @brief 传输进度回调：每次有效推进后触发（可用于界面进度条）。
 * @param userData 注册时上下文（借用）。
 * @param transferId 传输 id。
 * @param done 已完成字节数（接收侧为已落盘，发送侧为已确认）。
 * @param total 文件总字节数。
 */
typedef void (*XLinkFileProgressCallback)(void* userData, uint32_t transferId,
                                          uint64_t done, uint64_t total);

/**
 * @brief 传输终局回调：成功/失败各触发一次。
 * @param userData 注册时上下文（借用）。
 * @param transferId 传输 id。
 * @param ok 是否成功。
 * @param status 结果码（XLinkFileStatus）。
 * @param text 结果文本（UTF-8，借用；可为空串）。
 */
typedef void (*XLinkFileFinishedCallback)(void* userData, uint32_t transferId,
                                          bool ok, uint8_t status,
                                          const char* text);

/**
 * @brief 文件传输引擎对象。
 * @details 值类型，可嵌入上层对象；init/deinit 配对。
 */
typedef struct XLinkFileTransfer {
    XLinkSession* m_session;   /**< 承载会话（借用）。 */
    uint32_t m_transferId;     /**< 传输 id（发起方分配；接收方回显）。 */
    XLinkFileTransferState m_state; /**< 当前状态。 */

    XLinkFileMode m_mode;      /**< 方向。 */
    bool m_isSender;           /**< 本端是发送方还是接收方。 */

    XFile* m_file;             /**< 本地文件（对象拥有；未打开为 NULL）。 */
    XString* m_localPath;      /**< 本地路径（对象拥有）。 */
    XString* m_remoteName;     /**< 远程文件名（对象拥有）。 */
    XString* m_partPath;       /**< 接收侧临时文件路径（对象拥有）。 */

    uint64_t m_totalSize;      /**< 文件总字节数。 */
    uint64_t m_offset;         /**< 发送侧：已发出偏移；接收侧：已落盘偏移。 */
    uint64_t m_ackOffset;      /**< 发送侧：对端已确认偏移（收 ACK 推进）。 */
    uint32_t m_chunkBytes;     /**< 协商块大小。 */
    uint32_t m_windowBytes;    /**< 协商窗口。 */
    uint32_t m_credit;         /**< 发送侧剩余授信（字节）；接收侧累计待确认量。 */

    uint8_t m_flags;           /**< Open 标志（bit0 允许覆盖）。 */
    uint8_t m_status;          /**< 失败时的结果码（XLinkFileStatus）。 */
    char m_statusText[XLINK_FILE_TEXT_BYTES]; /**< 失败文本。 */

    XLinkFileProgressCallback m_progress;  /**< 进度回调（借用）。 */
    XLinkFileFinishedCallback m_finished;  /**< 终局回调（借用）。 */
    void* m_userData;                      /**< 回调上下文（借用）。 */

    uint32_t m_lastActivityTick; /**< 最近一次有进展的单调毫秒（空闲超时用）。 */
    uint32_t m_retryCount;       /**< 同一偏移重试次数（防御对端不推进）。 */
    uint8_t m_sha256[32];        /**< 发送侧在发起时预算好的整文件摘要：
                                      Finish 直接使用，避免传完再回头读文件
                                      （句柄可能已到末尾/被占用）。 */
    bool m_sha256Valid;          /**< m_sha256 是否有效。 */
} XLinkFileTransfer;

/**
 * @brief      初始化传输引擎（不启动传输）。
 * @param      transfer 对象（调用方提供存储）；NULL 时无操作。
 */
void XLinkFileTransfer_init(XLinkFileTransfer* transfer);

/**
 * @brief      反初始化：关闭文件、释放路径对象。
 * @param      transfer 对象；NULL 时无操作。
 * @note       不发送 Abort（上层负责在销毁前显式中止）。
 */
void XLinkFileTransfer_deinit(XLinkFileTransfer* transfer);

/**
 * @brief      注册进度与终局回调。
 * @param      transfer 对象。
 * @param      progress 进度回调（借用，可为 NULL）。
 * @param      finished 终局回调（借用，可为 NULL）。
 * @param      userData 回调上下文（借用）。
 * @return     成功返回 true；transfer 为 NULL 返回 false。
 */
bool XLinkFileTransfer_setCallbacks(XLinkFileTransfer* transfer,
                                    XLinkFileProgressCallback progress,
                                    XLinkFileFinishedCallback finished,
                                    void* userData);

/**
 * @brief      发送方：发起上传（本地 -> 远端）。
 * @param      transfer 对象。
 * @param      session 承载会话（借用，须已 Ready 且协商了文件能力）。
 * @param      localPath 本地文件路径（UTF-8）。
 * @param      remoteName 远端文件名（相对路径，须通过 XLinkFile_isSafeName）。
 * @param      flags Open 标志（bit0 = 允许覆盖远端）。
 * @param      outTransferId 输出传输 id（可为 NULL）。
 * @return     成功返回 true（Open 已入队）；参数非法/文件打不开返回 false。
 * @note       文件大小在发起时确定；传输期间文件变化不保证一致。
 */
bool XLinkFileTransfer_beginUpload(XLinkFileTransfer* transfer,
                                   XLinkSession* session,
                                   const char* localPath,
                                   const char* remoteName, uint8_t flags,
                                   uint32_t* outTransferId);

/**
 * @brief      发送方：发起下载（远端 -> 本地）。
 * @param      transfer 对象。
 * @param      session 承载会话。
 * @param      remoteName 远端文件名（相对路径）。
 * @param      localPath 本地落盘路径（UTF-8）。
 * @param      flags Open 标志（bit0 = 允许覆盖本地）。
 * @param      outTransferId 输出传输 id（可为 NULL）。
 * @return     成功返回 true。
 */
bool XLinkFileTransfer_beginDownload(XLinkFileTransfer* transfer,
                                     XLinkSession* session,
                                     const char* remoteName,
                                     const char* localPath, uint8_t flags,
                                     uint32_t* outTransferId);

/**
 * @brief      接收方：受理对端 Open 并启动接收。
 * @param      transfer 对象。
 * @param      session 承载会话。
 * @param      open 已解码的 Open 载荷（借用）。
 * @param      localPath 本地落盘路径（UTF-8，已通过沙箱校验）。
 * @param      transferId 对端分配的传输 id。
 * @param      chunkBytes 本端确定的块大小（会回填到 OpenAck）。
 * @param      windowBytes 本端确定的窗口。
 * @return     成功返回 true；文件打不开/大小超限返回 false（调用方回拒绝）。
 * @note       断点续传：若 localPath 已存在，按其大小作为起始偏移。
 */
bool XLinkFileTransfer_beginReceive(XLinkFileTransfer* transfer,
                                    XLinkSession* session,
                                    const XLinkFileOpenPayload* open,
                                    const char* localPath, uint32_t transferId,
                                    uint32_t chunkBytes, uint32_t windowBytes);

/**
 * @brief      服务端：受理对端下载请求并启动**发送**（用于服务端回 OpenAck）。
 * @param      transfer 对象。
 * @param      session 承载会话。
 * @param      localPath 本地待发送文件路径（UTF-8，已通过沙箱校验）。
 * @param      remoteName 对端请求的文件名（用于回显/日志）。
 * @param      remoteNameLen 文件名有效字节数。
 * @param      flags 对端 Open 携带的标志。
 * @param      totalSize 文件大小（本端 stat 所得）。
 * @param      transferId 对端分配的传输 id。
 * @param      chunkBytes 协商块大小。
 * @param      windowBytes 协商窗口。
 * @param      startOffset 对端请求的起始偏移（续传）。
 * @return     成功返回 true；文件打不开/偏移越界返回 false。
 * @note       与 beginUpload 的差别：**不再发 Open**（对端已发），
 *             而是直接发 OpenAck 并进入 Active 状态。
 */
bool XLinkFileTransfer_beginSendFile(XLinkFileTransfer* transfer,
                                     XLinkSession* session,
                                     const char* localPath,
                                     const char* remoteName,
                                     uint16_t remoteNameLen, uint8_t flags,
                                     uint64_t totalSize, uint32_t transferId,
                                     uint32_t chunkBytes, uint32_t windowBytes,
                                     uint64_t startOffset);

/**
 * @brief      推进一步传输（由会话 File 回调/定时节拍驱动）。
 * @param      transfer 对象。
 * @param      nowMs 当前单调毫秒（用于空闲超时）。
 * @return     无。
 * @note       每轮只做一个有界工作单元（最多一个 chunk），保证不阻塞。
 */
void XLinkFileTransfer_pump(XLinkFileTransfer* transfer, uint32_t nowMs);

/**
 * @brief      处理收到的 File 帧（会话钩子直接调用）。
 * @param      transfer 对象。
 * @param      frame 帧头（借用）。
 * @param      payload 载荷（借用）。
 * @param      nowMs 当前单调毫秒。
 * @return     无。
 */
void XLinkFileTransfer_onFrame(XLinkFileTransfer* transfer,
                               const XLinkFrameHeader* frame,
                               const uint8_t* payload, uint32_t nowMs);

/**
 * @brief      主动中止传输（向对端发 Abort）。
 * @param      transfer 对象。
 * @param      status 中止原因（XLinkFileStatus）。
 * @param      text 说明文本（UTF-8，可为 NULL）。
 * @return     无。
 */
void XLinkFileTransfer_abort(XLinkFileTransfer* transfer, uint8_t status,
                             const char* text);

/**
 * @brief      传输是否已终局（Done/Failed）。
 * @param      transfer 对象（借用）；NULL 时返回 true。
 * @return     终局返回 true。
 */
bool XLinkFileTransfer_isFinished(const XLinkFileTransfer* transfer);

/**
 * @brief      获取当前状态。
 * @param      transfer 对象（借用）；NULL 时返回 Idle。
 */
XLinkFileTransferState XLinkFileTransfer_state(const XLinkFileTransfer* transfer);

/**
 * @brief      获取已处理字节数。
 * @param      transfer 对象（借用）；NULL 时返回 0。
 */
uint64_t XLinkFileTransfer_doneBytes(const XLinkFileTransfer* transfer);

/**
 * @brief      获取文件总字节数。
 * @param      transfer 对象（借用）；NULL 时返回 0。
 */
uint64_t XLinkFileTransfer_totalBytes(const XLinkFileTransfer* transfer);

#endif /* XLINK_ON && XLINK_FILE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKFILETRANSFER_H */
