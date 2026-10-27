/** @file       XLinkFileTransfer.c
 * @brief      XLink 文件传输引擎实现（收发状态机/流控/续传/校验）。
 * @details    只依赖 XinYueC 抽象层（XFile/XString/XCryptographicHash/
 *             XMemory/XDateTime），禁止平台 API 与 stdio。
 *
 *             状态机（发送方）：
 *               Idle -(beginUpload)-> Opening -(OpenAck OK)-> Active
 *                    -(全部数据发完)-> Finishing -(FinishAck OK)-> Done
 *             状态机（接收方）：
 *               Idle -(beginReceive)-> Active -(Finish 校验通过)-> Done
 *
 *             流控：发送方维护 m_credit（接收方授予的未确认字节额度）。
 *             接收方每消费一定量（半个窗口）回一次 ACK 并补充额度。
 *
 *             断点续传：接收方若发现 <目标>.part 已存在且小于总大小，
 *             以其大小为起始偏移写进 OpenAck；发送方从该偏移继续。
 *
 *             完整性：双方各自累计整文件 SHA-256；发送方在 Finish 中给出，
 *             接收方复算比对，不符即 Abort 并保留 .part（便于排查）。
 * @author     XinYueC 团队
 */
#include "XLinkFileTransfer.h"

#if XLINK_ON && XLINK_FILE_ON

#include "XLinkFile.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XCryptographicHash.h"
#include "XByteArray.h"
#include "XPrintf.h"
#include "XDateTime.h"
#include <string.h>

/** @brief 接收方每消费满「窗口的多少分之一」回一次 ACK（越小越频繁）。 */
#define XLINK_FILE_ACK_DIVISOR 2u

/** @brief 同一偏移最多重试次数（对端反复不推进时判定失败）。 */
#define XLINK_FILE_MAX_STALL 8u

/* ========================================================================== */
/*                        内部工具                                           */
/* ========================================================================== */

/** @brief 设置失败终局并清理文件句柄（不发帧，由调用方决定是否 Abort）。 */
static void xft_fail(XLinkFileTransfer* t, uint8_t status, const char* text)
{
    if (!t)
        return;
    if (t->m_state == XLinkFileTransferState_Done
        || t->m_state == XLinkFileTransferState_Failed)
        return;
    t->m_state = XLinkFileTransferState_Failed;
    t->m_status = status;
    if (text) {
        size_t n = XStrlen(text);
        if (n >= sizeof(t->m_statusText))
            n = sizeof(t->m_statusText) - 1u;
        memcpy(t->m_statusText, text, n);
        t->m_statusText[n] = '\0';
    }
    else {
        t->m_statusText[0] = '\0';
    }
    if (t->m_file) {
        XClassDelete((XClass*)t->m_file);
        t->m_file = NULL;
    }
    if (t->m_finished)
        t->m_finished(t->m_userData, t->m_transferId, false, status,
                      t->m_statusText);
}

/** @brief 设置成功终局（保留文件句柄的关闭由调用方负责）。 */
static void xft_succeed(XLinkFileTransfer* t)
{
    if (!t || t->m_state == XLinkFileTransferState_Done
        || t->m_state == XLinkFileTransferState_Failed)
        return;
    t->m_state = XLinkFileTransferState_Done;
    t->m_status = XLinkFileStatus_Ok;
    t->m_statusText[0] = '\0';
    if (t->m_file) {
        XFile_flush(t->m_file);
        XClassDelete((XClass*)t->m_file);
        t->m_file = NULL;
    }
    if (t->m_finished)
        t->m_finished(t->m_userData, t->m_transferId, true,
                      XLinkFileStatus_Ok, "");
}

/** @brief 进度上报（无回调时为空操作）。 */
static void xft_progress(XLinkFileTransfer* t)
{
    if (t && t->m_progress)
        t->m_progress(t->m_userData, t->m_transferId, t->m_offset,
                      t->m_totalSize);
}

/** @brief 发送一个 File 帧（通道未就绪时视为失败）。 */
static bool xft_send(XLinkFileTransfer* t, uint16_t code,
                     const uint8_t* payload, uint32_t length)
{
    XLinkFrameHeader frame;
    if (!t || !t->m_session)
        return false;
    frame.m_payloadLen = length;
    frame.m_msgClass = XLinkMsgClass_File;
    frame.m_msgCode = code;
    frame.m_requestId = t->m_transferId;
    return XLinkSession_send(t->m_session, &frame, payload);
}

/** @brief 发送 OpenAck（受理/拒绝共用）。 */
static void xft_sendOpenAck(XLinkFileTransfer* t, bool accepted, uint8_t status,
                            const char* text, uint64_t startOffset)
{
    XLinkFileOpenAckPayload ack;
    uint8_t buf[XLINK_FILE_OPEN_ACK_HEADER + XLINK_FILE_TEXT_BYTES];
    size_t len = 0;
    size_t textLen = text ? XStrlen(text) : 0;

    if (!t)
        return;
    if (textLen > XLINK_FILE_TEXT_BYTES)
        textLen = XLINK_FILE_TEXT_BYTES;
    memset(&ack, 0, sizeof(ack));
    ack.m_accepted = accepted ? 1u : 0u;
    ack.m_status = status;
    ack.m_textLen = (uint16_t)textLen;
    ack.m_chunkBytes = t->m_chunkBytes;
    ack.m_windowBytes = t->m_windowBytes;
    ack.m_startOffset = startOffset;
    ack.m_totalSize = t->m_totalSize;   /* 下载方向由本端（接收方）告知真实大小 */
    if (textLen > 0)
        memcpy(ack.m_text, text, textLen);
    if (XLinkFile_encodeOpenAck(&ack, buf, sizeof(buf), &len))
        (void)xft_send(t, XLinkFileCode_OpenAck, buf, (uint32_t)len);
}

/** @brief 发送 Ack（进度 + 授信）。 */
static void xft_sendAck(XLinkFileTransfer* t, uint32_t credit)
{
    XLinkFileAckPayload ack;
    uint8_t buf[12];
    size_t len = 0;
    if (!t)
        return;
    ack.m_nextOffset = t->m_offset;
    ack.m_creditBytes = credit;
    if (XLinkFile_encodeAck(&ack, buf, sizeof(buf), &len))
        (void)xft_send(t, XLinkFileCode_Ack, buf, (uint32_t)len);
}

/** @brief 发送 Abort 并本地置失败。 */
static void xft_sendAbortAndFail(XLinkFileTransfer* t, uint8_t status,
                                 const char* text)
{
    XLinkFileAbortPayload abortMsg;
    uint8_t buf[4 + XLINK_FILE_TEXT_BYTES];
    size_t len = 0;
    size_t textLen = text ? XStrlen(text) : 0;

    if (!t)
        return;
    if (textLen > XLINK_FILE_TEXT_BYTES)
        textLen = XLINK_FILE_TEXT_BYTES;
    memset(&abortMsg, 0, sizeof(abortMsg));
    abortMsg.m_status = status;
    abortMsg.m_byPeer = 0;
    abortMsg.m_textLen = (uint16_t)textLen;
    if (textLen > 0)
        memcpy(abortMsg.m_text, text, textLen);
    if (XLinkFile_encodeAbort(&abortMsg, buf, sizeof(buf), &len))
        (void)xft_send(t, XLinkFileCode_Abort, buf, (uint32_t)len);
    xft_fail(t, status, text);
}

/** @brief 计算**已打开**文件的整文件 SHA-256（发送侧用）。
 *  @details 用已打开的句柄：先记下当前位置，seek 回 0 全量读，再 seek 回
 *           原位。避免二次打开同一文件在 Windows 共享模式下失败。
 *           一次性读入后调用一次性哈希接口（分块流式接口在本仓库 XFile
 *           组合下存在未定位的问题，一次性路径更确定）。 */
static bool xft_hashOpenFile(XFile* file, uint8_t outHash[32])
{
    int64_t size;
    int64_t savedPos;
    uint8_t* buffer;
    int64_t got;
    XByteArray* digest;
    const uint8_t* data;
    int64_t digestSize;
    bool ok = false;

    if (!file || !outHash)
        return false;
    size = (int64_t)XFile_size_base(file);
    if (size < 0)
        return false;
    savedPos = (int64_t)XIODevice_pos_base((XIODevice*)file);
    if (!XIODevice_seek_base((XIODevice*)file, 0))
        return false;

    buffer = (uint8_t*)XMalloc_System((size_t)(size ? size : 1));
    if (!buffer) {
        (void)XIODevice_seek_base((XIODevice*)file, savedPos);
        return false;
    }
    got = XIODevice_read_1((XIODevice*)file, (char*)buffer, size);
    (void)XIODevice_seek_base((XIODevice*)file, savedPos);
    if (got != size) {
        XFree_System(buffer);
        return false;
    }
    digest = XCryptographicHash_hash((const char*)buffer, (size_t)size,
                                     XCryptographicHash_Sha256);
    XFree_System(buffer);
    if (!digest)
        return false;
    data = XByteArray_constData(digest);
    digestSize = (int64_t)XByteArray_size_base(digest);
    if (data && digestSize == 32) {
        memcpy(outHash, data, 32);
        ok = true;
    }
    XClassDelete((XClass*)digest);
    return ok;
}

/** @brief 计算当前本地文件整文件 SHA-256。
 *  @details 一次性读入整个文件后调用一次性哈希接口：分块流式接口在本仓库
 *           的 XFile 组合下存在未定位的重入问题（实测段错误），一次性接口
 *           路径更短、行为确定。文件大小受 XLINK_FILE_MAX_BYTES 限制
 *           （默认 128MB），内存占用可接受；嵌入式可下调该上限。 */
static bool xft_hashFile(const char* path, uint8_t outHash[32])
{
    XString* name;
    XFile* file;
    int64_t size;
    uint8_t* buffer;
    int64_t got;
    XByteArray* digest;
    const uint8_t* data;
    int64_t digestSize;
    bool ok = false;

    if (!path || !outHash)
        return false;
    name = XString_create_utf8(path);
    if (!name)
        return false;
    file = XFile_create_2(name);
    XClassDelete((XClass*)name);
    if (!file)
        return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    size = (int64_t)XFile_size_base(file);
    if (size < 0) {
        XClassDelete((XClass*)file);
        return false;
    }
    buffer = (uint8_t*)XMalloc_System((size_t)(size ? size : 1));
    if (!buffer) {
        XClassDelete((XClass*)file);
        return false;
    }
    got = XIODevice_read_1((XIODevice*)file, (char*)buffer, size);
    XClassDelete((XClass*)file);
    if (got != size) {
        XFree_System(buffer);
        return false;
    }
    digest = XCryptographicHash_hash((const char*)buffer, (size_t)size,
                                     XCryptographicHash_Sha256);
    XFree_System(buffer);
    if (!digest)
        return false;
    data = XByteArray_constData(digest);
    digestSize = (int64_t)XByteArray_size_base(digest);
    if (data && digestSize == 32) {
        memcpy(outHash, data, 32);
        ok = true;
    }
    XClassDelete((XClass*)digest);
    return ok;
}

/** @brief 打开本地文件（收发共用）。 */
static XFile* xft_openLocal(const char* path, XIODeviceBaseMode mode)
{
    XString* name;
    XFile* file;

    if (!path || !path[0])
        return NULL;
    name = XString_create_utf8(path);
    if (!name)
        return NULL;
    file = XFile_create_2(name);
    XClassDelete((XClass*)name);
    if (!file)
        return NULL;
    if (!XFile_open_2(file, mode, XFile_ReadOwner | XFile_WriteOwner)) {
        XClassDelete((XClass*)file);
        return NULL;
    }
    return file;
}

/* ========================================================================== */
/*                        生命周期                                           */
/* ========================================================================== */

void XLinkFileTransfer_init(XLinkFileTransfer* transfer)
{
    if (!transfer)
        return;
    memset(transfer, 0, sizeof(*transfer));
    transfer->m_state = XLinkFileTransferState_Idle;
}

void XLinkFileTransfer_deinit(XLinkFileTransfer* transfer)
{
    if (!transfer)
        return;
    if (transfer->m_file) {
        XClassDelete((XClass*)transfer->m_file);
        transfer->m_file = NULL;
    }
    if (transfer->m_localPath) {
        XClassDelete((XClass*)transfer->m_localPath);
        transfer->m_localPath = NULL;
    }
    if (transfer->m_remoteName) {
        XClassDelete((XClass*)transfer->m_remoteName);
        transfer->m_remoteName = NULL;
    }
    if (transfer->m_partPath) {
        XClassDelete((XClass*)transfer->m_partPath);
        transfer->m_partPath = NULL;
    }
    /* 会话是借用指针：解除引用，避免残留传输在下一轮 pump 里访问
       已销毁/更换的会话。回调**保留**（同一对象跨多次传输复用），
       由 setCallbacks 显式替换。 */
    transfer->m_session = NULL;
    transfer->m_state = XLinkFileTransferState_Idle;
}

bool XLinkFileTransfer_setCallbacks(XLinkFileTransfer* transfer,
                                    XLinkFileProgressCallback progress,
                                    XLinkFileFinishedCallback finished,
                                    void* userData)
{
    if (!transfer)
        return false;
    transfer->m_progress = progress;
    transfer->m_finished = finished;
    transfer->m_userData = userData;
    return true;
}

/* ========================================================================== */
/*                        发起（发送方）                                     */
/* ========================================================================== */

/** @brief 发送 Open（上传/下载共用）。 */
static bool xft_beginSender(XLinkFileTransfer* t, XLinkSession* session,
                            XLinkFileMode mode, const char* localPath,
                            const char* remoteName, uint8_t flags,
                            uint32_t* outTransferId)
{
    XLinkFileOpenPayload open;
    uint8_t buf[XLINK_FILE_OPEN_HEADER + XLINK_FILE_NAME_BYTES];
    size_t len = 0;
    size_t nameLen;
    uint64_t totalSize = 0;

    if (!t || !session || !localPath || !remoteName)
        return false;
    if (!XLinkSession_isReady(session))
        return false;
    if (!XLinkFile_isSafeName(remoteName, 0))
        return false;
    nameLen = XStrlen(remoteName);
    if (nameLen == 0 || nameLen >= XLINK_FILE_NAME_BYTES)
        return false;

    /* 上传：本地文件必须存在且可读，大小即总长。
       下载：本地目标可以不存在（新建），总长由对端 OpenAck 告知；
       这里先用 0 占位，收到 OpenAck 后回填。 */
    if (mode == XLinkFileMode_Upload) {
        XFile* probe = xft_openLocal(localPath, XIODevice_ReadOnly);
        if (!probe)
            return false;
        totalSize = (uint64_t)XFile_size_base(probe);
        XClassDelete((XClass*)probe); /* 先量尺寸，正式打开在状态清理之后 */
    }

    /* 重置传输上下文；回调与 transferId 由调用方预设，重置时保留。 */
    {
        uint32_t savedId = t->m_transferId;
        XLinkFileProgressCallback savedProgress = t->m_progress;
        XLinkFileFinishedCallback savedFinished = t->m_finished;
        void* savedUser = t->m_userData;
        XLinkFileTransfer_deinit(t);
        XLinkFileTransfer_init(t);
        t->m_transferId = savedId;
        t->m_progress = savedProgress;
        t->m_finished = savedFinished;
        t->m_userData = savedUser;
    }
    t->m_session = session;
    t->m_isSender = true;
    t->m_mode = mode;
    t->m_totalSize = totalSize;
    t->m_offset = 0;
    t->m_chunkBytes = XLINK_FILE_CHUNK_BYTES;
    t->m_windowBytes = XLINK_FILE_WINDOW_BYTES;
    t->m_credit = 0;
    t->m_flags = flags;
    t->m_localPath = XString_create_utf8(localPath);
    t->m_remoteName = XString_create_utf8(remoteName);
    if (!t->m_localPath || !t->m_remoteName) {
        xft_fail(t, XLinkFileStatus_Rejected, "out of memory");
        return false;
    }
    if (mode == XLinkFileMode_Upload && !t->m_file) {
        t->m_file = xft_openLocal(localPath, XIODevice_ReadOnly);
        if (!t->m_file) {
            xft_fail(t, XLinkFileStatus_NotFound, "cannot open local file");
            return false;
        }
    }

    /* 发起时就把整文件摘要算好：传输途中文件可能被改写、句柄也可能
       已读到末尾，末尾再回头读容易失败（实测服务端下载路径即如此）。
       摘要只用于 Finish 校验，必须在数据发送前确定内容。 */
    if (mode == XLinkFileMode_Upload) {
        t->m_sha256Valid = xft_hashFile(localPath, t->m_sha256);
    }

    memset(&open, 0, sizeof(open));
    open.m_mode = (uint8_t)mode;
    open.m_flags = flags;
    open.m_nameLen = (uint16_t)nameLen;
    open.m_chunkBytes = t->m_chunkBytes;
    open.m_windowBytes = t->m_windowBytes;
    open.m_totalSize = totalSize;
    open.m_startOffset = 0;
    memcpy(open.m_name, remoteName, nameLen);
    if (!XLinkFile_encodeOpen(&open, buf, sizeof(buf), &len)) {
        xft_fail(t, XLinkFileStatus_Protocol, "encode open failed");
        return false;
    }
    t->m_state = XLinkFileTransferState_Opening;
    if (!xft_send(t, XLinkFileCode_Open, buf, (uint32_t)len)) {
        xft_fail(t, XLinkFileStatus_Aborted, "send open failed");
        return false;
    }
    if (outTransferId)
        *outTransferId = t->m_transferId;
    return true;
}

bool XLinkFileTransfer_beginUpload(XLinkFileTransfer* transfer,
                                   XLinkSession* session,
                                   const char* localPath,
                                   const char* remoteName, uint8_t flags,
                                   uint32_t* outTransferId)
{
    bool ok;
    if (!transfer)
        return false;
    /* transferId 由调用方在发起前设置（0 表示未设，这里补一个非 0 值）。 */
    if (transfer->m_transferId == 0)
        transfer->m_transferId = (uint32_t)XDateTime_currentMSecsSinceEpoch();
    ok = xft_beginSender(transfer, session, XLinkFileMode_Upload, localPath,
                         remoteName, flags, outTransferId);
    if (!ok)
        return false;
    if (outTransferId)
        *outTransferId = transfer->m_transferId;
    return true;
}

bool XLinkFileTransfer_beginDownload(XLinkFileTransfer* transfer,
                                     XLinkSession* session,
                                     const char* remoteName,
                                     const char* localPath, uint8_t flags,
                                     uint32_t* outTransferId)
{
    bool ok;
    if (!transfer)
        return false;
    if (transfer->m_transferId == 0)
        transfer->m_transferId = (uint32_t)XDateTime_currentMSecsSinceEpoch();
    ok = xft_beginSender(transfer, session, XLinkFileMode_Download, localPath,
                         remoteName, flags, outTransferId);
    if (!ok)
        return false;
    if (outTransferId)
        *outTransferId = transfer->m_transferId;
    return true;
}

/* ========================================================================== */
/*                        服务端受理下载（发送侧）                           */
/* ========================================================================== */

bool XLinkFileTransfer_beginSendFile(XLinkFileTransfer* transfer,
                                     XLinkSession* session,
                                     const char* localPath,
                                     const char* remoteName,
                                     uint16_t remoteNameLen, uint8_t flags,
                                     uint64_t totalSize, uint32_t transferId,
                                     uint32_t chunkBytes, uint32_t windowBytes,
                                     uint64_t startOffset)
{
    XFile* file;

    (void)remoteNameLen;
    if (!transfer || !session || !localPath || !remoteName)
        return false;
    if (startOffset > totalSize)
        return false;

    XLinkFileTransfer_deinit(transfer);
    XLinkFileTransfer_init(transfer);
    transfer->m_session = session;
    transfer->m_isSender = true;
    transfer->m_mode = XLinkFileMode_Download;
    transfer->m_transferId = transferId;
    transfer->m_totalSize = totalSize;
    transfer->m_offset = startOffset;
    transfer->m_ackOffset = startOffset;
    transfer->m_chunkBytes = chunkBytes ? chunkBytes : XLINK_FILE_CHUNK_BYTES;
    transfer->m_windowBytes = windowBytes ? windowBytes
                                          : XLINK_FILE_WINDOW_BYTES;
    transfer->m_credit = transfer->m_windowBytes;
    transfer->m_flags = flags;
    transfer->m_localPath = XString_create_utf8(localPath);
    transfer->m_remoteName = XString_create_utf8(remoteName);
    if (!transfer->m_localPath || !transfer->m_remoteName) {
        xft_fail(transfer, XLinkFileStatus_Rejected, "out of memory");
        return false;
    }

    file = xft_openLocal(localPath, XIODevice_ReadOnly);
    if (!file) {
        xft_fail(transfer, XLinkFileStatus_NotFound, "cannot open local file");
        return false;
    }
    if (startOffset > 0
        && !XIODevice_seek_base((XIODevice*)file, (int64_t)startOffset)) {
        XClassDelete((XClass*)file);
        xft_fail(transfer, XLinkFileStatus_NoSpace, "seek failed");
        return false;
    }
    transfer->m_file = file;

    /* 发起时预算整文件摘要（见 xft_beginSender 同名说明）。 */
    transfer->m_sha256Valid = xft_hashOpenFile(file, transfer->m_sha256);

    /* 直接回 OpenAck（受理），随后进入 Active 由 pump 发数据。 */
    transfer->m_state = XLinkFileTransferState_Active;
    xft_sendOpenAck(transfer, true, XLinkFileStatus_Ok, "", startOffset);
    return true;
}

/* ========================================================================== */
/*                        接收（接收方）                                     */
/* ========================================================================== */

bool XLinkFileTransfer_beginReceive(XLinkFileTransfer* transfer,
                                    XLinkSession* session,
                                    const XLinkFileOpenPayload* open,
                                    const char* localPath, uint32_t transferId,
                                    uint32_t chunkBytes, uint32_t windowBytes)
{
    XFile* file;
    uint64_t existing = 0;

    if (!transfer || !session || !open || !localPath)
        return false;

    XLinkFileTransfer_deinit(transfer);
    XLinkFileTransfer_init(transfer);
    transfer->m_session = session;
    transfer->m_isSender = false;
    transfer->m_mode = (XLinkFileMode)open->m_mode;
    transfer->m_transferId = transferId;
    transfer->m_totalSize = open->m_totalSize;
    transfer->m_chunkBytes = chunkBytes ? chunkBytes : XLINK_FILE_CHUNK_BYTES;
    transfer->m_windowBytes = windowBytes ? windowBytes
                                          : XLINK_FILE_WINDOW_BYTES;
    transfer->m_credit = 0;
    transfer->m_flags = open->m_flags;
    transfer->m_localPath = XString_create_utf8(localPath);
    transfer->m_remoteName = XString_create_utf8(open->m_name);
    if (!transfer->m_localPath || !transfer->m_remoteName) {
        xft_fail(transfer, XLinkFileStatus_Rejected, "out of memory");
        return false;
    }

    /* 接收侧要写文件：上传模式（本端接收）用 localPath 直接建；
       下载模式（本端发送）不需要在这里打开。 */
    if (open->m_mode != XLinkFileMode_Upload) {
        /* 本端是发送方，beginReceiver 不该被调用。 */
        xft_fail(transfer, XLinkFileStatus_Protocol, "wrong direction");
        return false;
    }

    /* 断点续传：已有 .part 就续写（文件大小即已收字节数）。 */
    file = xft_openLocal(localPath, XIODevice_ReadWrite | XIODevice_Create);
    if (file) {
        existing = (uint64_t)XFile_size_base(file);
        if (existing > 0 && existing <= open->m_totalSize) {
            if (!XIODevice_seek_base((XIODevice*)file, (int64_t)existing)) {
                XClassDelete((XClass*)file);
                xft_fail(transfer, XLinkFileStatus_NoSpace,
                         "seek part file failed");
                return false;
            }
            transfer->m_offset = existing;
        }
        else if (existing > open->m_totalSize) {
            /* 已有比目标大：无法续传，要求从头（截断）。 */
            XClassDelete((XClass*)file);
            file = xft_openLocal(localPath, XIODevice_ReadWrite
                                               | XIODevice_Create
                                               | XIODevice_Truncate);
            if (!file) {
                xft_fail(transfer, XLinkFileStatus_NoSpace,
                         "truncate part file failed");
                return false;
            }
            transfer->m_offset = 0;
        }
    }
    else {
        file = xft_openLocal(localPath, XIODevice_ReadWrite
                                           | XIODevice_Create
                                           | XIODevice_Truncate);
        if (!file) {
            xft_fail(transfer, XLinkFileStatus_NoSpace,
                     "cannot create local file");
            return false;
        }
        transfer->m_offset = 0;
    }
    transfer->m_file = file;
    transfer->m_state = XLinkFileTransferState_Active;
    xft_sendOpenAck(transfer, true, XLinkFileStatus_Ok, "",
                    transfer->m_offset);
    return true;
}

/* ========================================================================== */
/*                        收帧处理                                           */
/* ========================================================================== */

/** @brief 发送方处理 OpenAck。 */
static void xft_onOpenAck(XLinkFileTransfer* t, const uint8_t* payload,
                          uint32_t length)
{
    XLinkFileOpenAckPayload ack;

    if (t->m_state != XLinkFileTransferState_Opening)
        return;
    if (!XLinkFile_decodeOpenAck(payload, length, &ack)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "bad open ack");
        return;
    }
    if (!ack.m_accepted) {
        xft_fail(t, ack.m_status ? ack.m_status : XLinkFileStatus_Rejected,
                 ack.m_textLen ? ack.m_text : "rejected by peer");
        return;
    }

    /* 采纳对端协商的块大小/窗口，并从对端指定的偏移继续。 */
    if (ack.m_chunkBytes > 0
        && ack.m_chunkBytes <= XLINK_MAX_PAYLOAD - XLINK_FILE_DATA_OFFSET_BYTES)
        t->m_chunkBytes = ack.m_chunkBytes;
    if (ack.m_windowBytes > 0)
        t->m_windowBytes = ack.m_windowBytes;
    t->m_offset = ack.m_startOffset;
    t->m_credit = ack.m_windowBytes;
    /* 下载方向：本端在 Open 时不知道文件大小，由对端 OpenAck 告知。 */
    if (t->m_mode == XLinkFileMode_Download && ack.m_totalSize > 0)
        t->m_totalSize = ack.m_totalSize;
    /* 角色切换：下载方向发起 Open 时本端是"请求方"，OpenAck 受理后
       本端转为**接收方**（数据由对端发来），不能再走发送泵。 */
    if (t->m_mode == XLinkFileMode_Download)
        t->m_isSender = false;

    if (t->m_mode == XLinkFileMode_Download) {
        /* 下载：对端在 OpenAck 里告知文件总大小（上传则是本地已知）。 */
        if (ack.m_startOffset <= ack.m_windowBytes)
            ;
    }

    /* 定位本地文件到起始偏移（上传续传 / 下载新建覆盖）。 */
    if (t->m_file) {
        if (t->m_mode == XLinkFileMode_Upload) {
            (void)XIODevice_seek_base((XIODevice*)t->m_file,
                                      (int64_t)t->m_offset);
        }
    }
    else if (t->m_mode == XLinkFileMode_Download) {
        XIODeviceBaseMode mode = XIODevice_ReadWrite | XIODevice_Create;
        if (t->m_offset == 0)
            mode = XIODevice_ReadWrite | XIODevice_Create | XIODevice_Truncate;
        t->m_file = xft_openLocal(XString_toUtf8(t->m_localPath), mode);
        if (!t->m_file) {
            xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace,
                                 "cannot open local target");
            return;
        }
        if (t->m_offset > 0)
            (void)XIODevice_seek_base((XIODevice*)t->m_file,
                                      (int64_t)t->m_offset);
    }

    t->m_state = XLinkFileTransferState_Active;
    t->m_lastActivityTick = 0;
    t->m_retryCount = 0;
}

/** @brief 接收方处理 DATA。 */
static void xft_onData(XLinkFileTransfer* t, const uint8_t* payload,
                       uint32_t length)
{
    uint64_t offset = 0;
    const uint8_t* data = NULL;
    uint32_t dataLen = 0;
    int64_t written;

    if (t->m_state != XLinkFileTransferState_Active || t->m_isSender)
        return;
    if (!XLinkFile_decodeData(payload, length, &offset, &data, &dataLen)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "bad data frame");
        return;
    }
    /* 只接受期望偏移的数据；重复/乱序按已收处理（幂等，回 ACK 拉回正轨）。 */
    if (offset < t->m_offset) {
        xft_sendAck(t, t->m_windowBytes);
        return;
    }
    if (offset > t->m_offset) {
        /* 跳块：不可能发生（TCP 有序），按协议违例中止。 */
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "data gap");
        return;
    }
    if ((uint64_t)dataLen > t->m_totalSize - t->m_offset) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "data overflow");
        return;
    }
    if (!t->m_file) {
        xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace, "no local file");
        return;
    }

    written = XIODevice_write_1((XIODevice*)t->m_file, (const char*)data,
                                (int64_t)dataLen);
    if (written != (int64_t)dataLen) {
        xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace, "write failed");
        return;
    }
    t->m_offset += dataLen;
    /* 每块必回 ACK：把 ACK 当作「已落盘水位」的即时通告。
       攒够半个窗口再回会在「发送方额度刚好耗尽」时形成一次往返停顿，
       且末块 ACK 的边界条件容易让双方互等（实测下载方向会卡住）。
       每块一 ACK 的额外开销只有 12 字节帧头 + 8 + 4 字节载荷，
       相对 4KB 数据块可忽略。 */
    XFile_flush(t->m_file);
    xft_sendAck(t, t->m_windowBytes);
    t->m_credit = 0;
    xft_progress(t);
}

/** @brief 发送方处理 ACK。 */
static void xft_onAck(XLinkFileTransfer* t, const uint8_t* payload,
                      uint32_t length)
{
    XLinkFileAckPayload ack;
    uint64_t confirmed;

    if (t->m_state != XLinkFileTransferState_Active || !t->m_isSender)
        return;
    if (!XLinkFile_decodeAck(payload, length, &ack)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "bad ack");
        return;
    }
    confirmed = ack.m_nextOffset;
    if (confirmed > t->m_offset) {
        /* 对端收到比本端记录更多的数据：不可能（本端是发送方）。 */
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "ack beyond sent");
        return;
    }
    if (confirmed > t->m_totalSize) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "ack beyond size");
        return;
    }
    /* 对端确认进度 + 新授信。窗口以对端为准。 */
    t->m_ackOffset = confirmed;
    t->m_credit = ack.m_creditBytes;
    t->m_lastActivityTick = 0;
    t->m_retryCount = 0;
    xft_progress(t);
}

/** @brief 接收方处理 Finish。 */
static void xft_onFinish(XLinkFileTransfer* t, const uint8_t* payload,
                         uint32_t length)
{
    XLinkFileFinishPayload finish;
    XLinkFileFinishAckPayload ackResult;
    uint8_t buf[4 + XLINK_FILE_TEXT_BYTES];
    size_t len = 0;
    uint8_t actual[32];

    if (t->m_state != XLinkFileTransferState_Active || t->m_isSender)
        return;
    if (!XLinkFile_decodeFinish(payload, length, &finish)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "bad finish");
        return;
    }
    if (finish.m_totalSize != t->m_totalSize || t->m_offset != t->m_totalSize) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol,
                             "finish size mismatch");
        return;
    }
    if (t->m_file) {
        XFile_flush(t->m_file);
        XClassDelete((XClass*)t->m_file);
        t->m_file = NULL;
    }
    if (!xft_hashFile(XString_toUtf8(t->m_localPath), actual)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace,
                             "cannot verify local file");
        return;
    }
    if (memcmp(actual, finish.m_sha256, 32) != 0) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Checksum,
                             "sha256 mismatch");
        return;
    }

    memset(&ackResult, 0, sizeof(ackResult));
    ackResult.m_ok = 1;
    ackResult.m_status = XLinkFileStatus_Ok;
    ackResult.m_textLen = 0;
    if (XLinkFile_encodeFinishAck(&ackResult, buf, sizeof(buf), &len))
        (void)xft_send(t, XLinkFileCode_FinishAck, buf, (uint32_t)len);
    xft_succeed(t);
}

/** @brief 发送方处理 FinishAck。 */
static void xft_onFinishAck(XLinkFileTransfer* t, const uint8_t* payload,
                            uint32_t length)
{
    XLinkFileFinishAckPayload result;

    if (t->m_state != XLinkFileTransferState_Finishing || !t->m_isSender)
        return;
    if (!XLinkFile_decodeFinishAck(payload, length, &result)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "bad finish ack");
        return;
    }
    if (!result.m_ok) {
        xft_fail(t, result.m_status ? result.m_status
                                    : XLinkFileStatus_Rejected,
                 result.m_textLen ? result.m_text : "peer rejected finish");
        return;
    }
    xft_succeed(t);
}

/** @brief 双方处理 Abort。 */
static void xft_onAbort(XLinkFileTransfer* t, const uint8_t* payload,
                        uint32_t length)
{
    XLinkFileAbortPayload abortMsg;

    if (XLinkFileTransfer_isFinished(t))
        return;
    if (!XLinkFile_decodeAbort(payload, length, &abortMsg)) {
        xft_fail(t, XLinkFileStatus_Protocol, "bad abort");
        return;
    }
    xft_fail(t, abortMsg.m_status ? abortMsg.m_status
                                  : XLinkFileStatus_Aborted,
             abortMsg.m_textLen ? abortMsg.m_text : "aborted by peer");
}

void XLinkFileTransfer_onFrame(XLinkFileTransfer* transfer,
                               const XLinkFrameHeader* frame,
                               const uint8_t* payload, uint32_t nowMs)
{
    if (!transfer || !frame)
        return;
    transfer->m_lastActivityTick = nowMs;
    switch ((XLinkFileCode)frame->m_msgCode) {
    case XLinkFileCode_OpenAck:
        xft_onOpenAck(transfer, payload, frame->m_payloadLen);
        break;
    case XLinkFileCode_Data:
        xft_onData(transfer, payload, frame->m_payloadLen);
        break;
    case XLinkFileCode_Ack:
        xft_onAck(transfer, payload, frame->m_payloadLen);
        break;
    case XLinkFileCode_Finish:
        xft_onFinish(transfer, payload, frame->m_payloadLen);
        break;
    case XLinkFileCode_FinishAck:
        xft_onFinishAck(transfer, payload, frame->m_payloadLen);
        break;
    case XLinkFileCode_Abort:
        xft_onAbort(transfer, payload, frame->m_payloadLen);
        break;
    default:
        /* Open/Data 等发送方向操作由上层状态机发起，接收方向无效帧忽略。 */
        break;
    }
}

/* ========================================================================== */
/*                        驱动                                               */
/* ========================================================================== */

/** @brief 发送方：在授信范围内发一块数据。 */
static void xft_senderPump(XLinkFileTransfer* t)
{
    uint8_t* buf;
    uint8_t data[XLINK_FILE_CHUNK_BYTES];
    uint32_t limit;
    int64_t got;
    size_t len = 0;

    if (!t->m_file)
        return;
    if (t->m_offset >= t->m_totalSize)
        return;
    /* 发送窗口：未确认字节达到窗口即停发，等对端 ACK 推进 m_ackOffset。
       窗口必须保留——去掉后发送端一次把整文件灌进队列，接收方的
       ACK 反馈与发送进度失去耦合，实测两端会在「各自等对方」处停滞。 */
    if (t->m_offset - t->m_ackOffset >= t->m_windowBytes)
        return;

    limit = t->m_chunkBytes;
    if (limit > XLINK_FILE_CHUNK_BYTES)
        limit = XLINK_FILE_CHUNK_BYTES;
    if (limit > (uint64_t)(t->m_totalSize - t->m_offset))
        limit = (uint32_t)(t->m_totalSize - t->m_offset);
    if (limit > XLINK_MAX_PAYLOAD - XLINK_FILE_DATA_OFFSET_BYTES)
        limit = XLINK_MAX_PAYLOAD - XLINK_FILE_DATA_OFFSET_BYTES;

    got = XIODevice_read_1((XIODevice*)t->m_file, (char*)data,
                           (int64_t)limit);
    if (got <= 0) {
        xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace, "local read failed");
        return;
    }

    buf = (uint8_t*)XMalloc_System(XLINK_FILE_DATA_OFFSET_BYTES + (size_t)got);
    if (!buf) {
        xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace, "out of memory");
        return;
    }
    if (!XLinkFile_encodeData(t->m_offset, data, (uint32_t)got, buf,
                              XLINK_FILE_DATA_OFFSET_BYTES + (size_t)got,
                              &len)) {
        XFree_System(buf);
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "encode data failed");
        return;
    }
    if (!xft_send(t, XLinkFileCode_Data, buf, (uint32_t)len)) {
        XFree_System(buf);
        /* 发送队列满属于背压：不失败，下一轮再试。 */
        return;
    }
    XFree_System(buf);
    t->m_offset += (uint64_t)got;
    xft_progress(t);
}

/** @brief 发送方：全部数据发完则发 Finish。 */
static void xft_senderFinish(XLinkFileTransfer* t)
{
    XLinkFileFinishPayload finish;
    uint8_t buf[40];
    size_t len = 0;
    uint8_t hash[32];

    if (t->m_offset < t->m_totalSize)
        return;
    /* 不再要求 m_ackOffset 追平总量：TCP 顺序保证对端会按序收到全部
       DATA，等最后一次 ACK 只会让传输在「数据发完但末 ACK 丢失」时
       永久卡住（实测发生过）。Finish 本身携带总长度与 SHA-256，接收侧
       会先校验长度再校验摘要——数据没到齐就必然失败，安全性不打折。 */
    /* 对端指定了起始偏移（续传）：发送侧需要 hash 整个文件，与文件是否
       已读到末尾无关；这里直接用已打开的句柄重算，避免二次打开同一文件
       在部分后端（Windows 共享模式）下失败。 */
    if (!t->m_sha256Valid) {
        if (!xft_hashOpenFile(t->m_file, hash)) {
            xft_sendAbortAndFail(t, XLinkFileStatus_NoSpace,
                                 "cannot hash local file");
            return;
        }
        memcpy(t->m_sha256, hash, 32);
        t->m_sha256Valid = true;
    }
    memcpy(hash, t->m_sha256, 32);
    memset(&finish, 0, sizeof(finish));
    finish.m_totalSize = t->m_totalSize;
    memcpy(finish.m_sha256, hash, 32);
    if (!XLinkFile_encodeFinish(&finish, buf, sizeof(buf), &len)) {
        xft_sendAbortAndFail(t, XLinkFileStatus_Protocol, "encode finish");
        return;
    }
    if (!xft_send(t, XLinkFileCode_Finish, buf, (uint32_t)len)) {
        return; /* 背压，下一轮再试 */
    }
    t->m_state = XLinkFileTransferState_Finishing;
}

void XLinkFileTransfer_pump(XLinkFileTransfer* transfer, uint32_t nowMs)
{
    if (!transfer || XLinkFileTransfer_isFinished(transfer))
        return;
    if (!transfer->m_session || !XLinkSession_isReady(transfer->m_session))
        return;

    if (transfer->m_lastActivityTick == 0)
        transfer->m_lastActivityTick = nowMs;
    /* Opening 停滞重发：Open 是下载方向的"触发帧"——对端不回 OpenAck
       就永远不会有数据流动，而静默判线要 3 秒才会断链。为不把一次
       丢帧/竞态放大成整次传输失败，这里每秒重发一次 Open；对端按
       "重置槽位并重新受理"处理（幂等）。 */
    if (transfer->m_state == XLinkFileTransferState_Opening) {
        if (transfer->m_lastActivityTick != 0
            && nowMs - transfer->m_lastActivityTick >= 1000u) {
            XLinkFileOpenPayload open;
            uint8_t buf[XLINK_FILE_OPEN_HEADER + XLINK_FILE_NAME_BYTES];
            size_t len = 0;
            size_t nameLen = 0;
            const char* name = transfer->m_remoteName
                                   ? XString_toUtf8(transfer->m_remoteName)
                                   : NULL;
            if (name)
                nameLen = strlen(name);
            memset(&open, 0, sizeof(open));
            open.m_mode = (uint8_t)transfer->m_mode;
            open.m_flags = transfer->m_flags;
            open.m_nameLen = (uint16_t)nameLen;
            open.m_chunkBytes = transfer->m_chunkBytes;
            open.m_windowBytes = transfer->m_windowBytes;
            open.m_totalSize = transfer->m_totalSize;
            open.m_startOffset = 0;
            if (name && nameLen > 0 && nameLen < XLINK_FILE_NAME_BYTES
                && XLinkFile_encodeOpen(&open, buf, sizeof(buf), &len)
                && xft_send(transfer, XLinkFileCode_Open, buf, (uint32_t)len)) {
                transfer->m_lastActivityTick = nowMs;
            }
        }
        return;
    }
    /* 活跃传输期间续命：文件传输是长耗时单向流量，对端 ACK/数据间隔可能
       超过会话静默阈值（默认 3s），届时会话会被误判断链（实测下载方向
       在等待对端确认时被回收）。只在传输未终局时续命，空闲超时判定仍由
       下面的 XLINK_FILE_IDLE_TIMEOUT_MS 独立负责。 */
    (void)XLinkSession_keepAlive(transfer->m_session, nowMs);
    if (XLINK_FILE_IDLE_TIMEOUT_MS > 0 && nowMs >= transfer->m_lastActivityTick
        && (nowMs - transfer->m_lastActivityTick)
               > XLINK_FILE_IDLE_TIMEOUT_MS) {
        xft_sendAbortAndFail(transfer, XLinkFileStatus_Timeout, "idle timeout");
        return;
    }

    if (!transfer->m_isSender)
        return; /* 接收方纯被动，收帧时推进 */

    switch (transfer->m_state) {
    case XLinkFileTransferState_Active:
        /* 单轮多块推进：受会话发送队列背压自然限流（send 返回 false 即停），
           避免"每拍只发一块"在低频节拍下把大文件传成龟速。 */
        while (transfer->m_offset < transfer->m_totalSize) {
            uint64_t before = transfer->m_offset;
            xft_senderPump(transfer);
            if (XLinkFileTransfer_isFinished(transfer))
                return;
            if (transfer->m_offset == before)
                break; /* 队列满/读失败：本轮到此为止 */
        }
        if (!XLinkFileTransfer_isFinished(transfer)
            && transfer->m_offset >= transfer->m_totalSize)
            xft_senderFinish(transfer);
        break;
    default:
        break;
    }
}

void XLinkFileTransfer_abort(XLinkFileTransfer* transfer, uint8_t status,
                             const char* text)
{
    if (!transfer || XLinkFileTransfer_isFinished(transfer))
        return;
    xft_sendAbortAndFail(transfer, status ? status : XLinkFileStatus_Aborted,
                         text ? text : "aborted");
}

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

bool XLinkFileTransfer_isFinished(const XLinkFileTransfer* transfer)
{
    if (!transfer)
        return true;
    return transfer->m_state == XLinkFileTransferState_Done
           || transfer->m_state == XLinkFileTransferState_Failed;
}

XLinkFileTransferState XLinkFileTransfer_state(
    const XLinkFileTransfer* transfer)
{
    return transfer ? transfer->m_state : XLinkFileTransferState_Idle;
}

uint64_t XLinkFileTransfer_doneBytes(const XLinkFileTransfer* transfer)
{
    return transfer ? transfer->m_offset : 0;
}

uint64_t XLinkFileTransfer_totalBytes(const XLinkFileTransfer* transfer)
{
    return transfer ? transfer->m_totalSize : 0;
}

#endif /* XLINK_ON && XLINK_FILE_ON */
