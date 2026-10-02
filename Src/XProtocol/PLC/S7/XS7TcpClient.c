#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7TcpClient.h"
#include "XS7TcpClient_Protected.h"
#include "XPlcClient_Protected.h"
#include "XPlcReply_Protected.h"
#include "XPlcDevice_Protected.h"
#include "XS7Tpkt.h"
#include "XS7Cotp.h"
#include "XS7Pdu.h"
#include "XS7Address.h"
#include "XS7Value.h"
#include "XS7Control.h"
#include "XS7Block.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include "XHashMap.h"
#include "XQueue.h"
#include "XTcpSocket.h"
#include "XTimer.h"
#include <string.h>

/* ==========================================================================
 * XS7TcpClient —— 西门子 S7（ISO-on-TCP）客户端实现
 *
 * 架构要点（对齐 _w1_design.md §3.4 / 风险清单 §7.3）：
 * - AmQ=1 串行发送：同一时刻仅一个 in-flight 分片（timerMap 非空即有
 *   in-flight），后续分片在 m_sendQueue 排队，完成/超时后由 pumpSendQueue
 *   自动发下一个；
 * - 分片读写：读按 XS7Pdu_maxReadBytesPerItem 切片、写按 XS7Pdu_maxWriteBytes
 *   切片，每片独立 pending/超时/重试；分片按序完成（AmQ=1 保序），
 *   响应数据按完成顺序追加进 Reply 的 rawResult，末片完成后统一
 *   XS7Value_decode 回填 result；
 * - 握手（COTP CR→CC→SetupCommunication）由 XS7Session 状态机推进，
 *   超时复用基类 m_timeoutTimer；业务分片各自持有独立定时器；
 * - 唯一触碰字节流的组件是 XS7Session_feed（本类只 readAll 后整体喂入）。
 * ========================================================================== */

// =============== 内部辅助结构体（先于前置声明定义） ===============

/**
 * @brief 分片/步骤类别（XS7PendingRequest.kind，内部使用）
 * @details 区分一个 in-flight PDU 的应答处理分支：
 *          读写分片、Run/Stop、块服务各序列步、通用原始透传
 */
typedef enum {
    XS7Pending_Read = 1,        ///< 读分片（数据追加 rawResult，末片解码 result）
    XS7Pending_Raw,             ///< 原始透传（rawResult = 完整应答 PDU，单步）
    XS7Pending_Write,           ///< 写分片（校验各项返回码）
    XS7Pending_Control,         ///< PLC Run/Stop（解析状态码）
    XS7Pending_BlockList,       ///< 块列表（解析 XVector<XS7BlockInfo>）
    XS7Pending_BlockUpStart,    ///< 块上传序列第一步（StartUpload）
    XS7Pending_BlockUpData,     ///< 块上传序列数据步（Upload，可多包）
    XS7Pending_BlockUpEnd,      ///< 块上传序列结束步（EndUpload）
    XS7Pending_BlockDlStart,    ///< 块下载序列第一步（RequestDownload）
    XS7Pending_BlockDlData      ///< 块下载序列数据步（Download，末片带 last 标志）
} XS7PendingKind;

/**
 * @brief S7 待处理请求结构体（AmQ=1：单 in-flight + m_sendQueue 排队）
 * @details pduRef → Reply 关联；chunkIndex/chunkCount 支撑一个 Reply 对应多 PDU；
 *          frame 保存本片完整帧（TPKT+COTP+S7），重试时仅改写帧内 pduRef
 *          （偏移 XS7_PDU_REF_FRAME_OFFSET）后原帧重发；
 *          ackFinished 供块上传应答跨槽传递"序列是否结束"标志。
 */
typedef struct XS7PendingRequest {
    XPlcReply* reply;            ///< 关联的Reply对象
    uint16_t pduRef;             ///< S7 PDU 引用号
    uint8_t retryCount;          ///< 当前重试次数
    uint8_t kind;                ///< 分片/步骤类别（XS7PendingKind）
    uint8_t ackFinished;         ///< 块上传应答：序列是否已结束（0/1）
    XTimerId timeoutTimer;       ///< 超时定时器ID（未发送为 XTIMER_INVALID_ID）
    uint16_t chunkIndex;         ///< 分片序号（上传序列=步序；下载序列=分片索引）
    uint16_t chunkCount;         ///< 分片总数（序列型步数未知填 0，完成由 kind 决定）
    XByteArray* frame;           ///< 本片完整帧（TPKT+COTP+S7，对象拥有，重试复用）
} XS7PendingRequest;

/* pduRef 在 pending->frame 内的字节偏移：TPKT(4) + COTP DT(3) 之后，S7 头内
 * 协议ID[0] + ROSCTR[1] + 冗余标识[2..3]，pduRef 居 S7 头 [4..5]
 * （与 XS7Pdu_parseHeader 同点：xs7PduReadBe16(s7+4)，4+3+4=11） */
#define XS7_PDU_REF_FRAME_OFFSET   11
/* pduRef 字段宽度（大端 2 字节） */
#define XS7_PDU_REF_FRAME_SIZE     2
/* COTP DT 头长度（02 F0 80） */
#define XS7_COTP_DT_HEADER_SIZE    3
/* 协商 PDU 合法下限：小于该值视为 PLC 协商异常（设计§3.2：直接信协商值，
 * 异常小报 ProtocolError），此时回退兜底值不做业务 */
#define XS7_MIN_VALID_PDU_LEN      64
/* 本端 TSAP 缺省值（Logo/S200 类可经 LocalTsapParameter 覆盖） */
#define XS7_DEFAULT_LOCAL_TSAP     0x0100
/* TSAP 连接类型首字节（S7NetPlus TsapPair 首期缺省 0x01） */
#define XS7_DEFAULT_CONN_TYPE      0x01

// =============== 虚函数前置声明 ===============
static bool VXPlcDevice_open_s7(XPlcDevice* device);
static void VXPlcDevice_close_s7(XPlcDevice* device);
static void VXPlcClient_deinit_s7(XS7TcpClient* client);
static void VXPlcClient_timerEvent_s7(XObject* obj, XTimerEvent* event);
static bool VXPlcClient_sendRequest_s7(XPlcClient* client, const XByteArray* payload, XPlcReply* reply);
static bool VXPlcClient_processResponse_s7(XPlcClient* client, const XByteArray* payload, XPlcReply* reply);

// =============== 槽函数前置声明 ===============
static void XS7TcpClient_onReadyRead(XObject* receiver, XVarList* args);
static void XS7TcpClient_onConnected(XObject* receiver, XVarList* args);
static void XS7TcpClient_onDisconnected(XObject* receiver, XVarList* args);
static void XS7TcpClient_onErrorOccurred(XObject* receiver, XVarList* args);

// =============== 内部函数前置声明 ===============
static void XS7TcpClient_attemptReconnect(XS7TcpClient* client);
static void XS7TcpClient_scheduleReconnectIfAllowed(XS7TcpClient* client);
static bool XS7TcpClient_startHandshake(XS7TcpClient* client);
static void XS7TcpClient_handshakeFailed(XS7TcpClient* client, XPlcDevice_Error error, const char* msg);
static void XS7TcpClient_handleHandshakeTimeout(XS7TcpClient* client);
static bool XS7TcpClient_enqueuePending(XS7TcpClient* client, XS7PendingRequest* pending);
static bool XS7TcpClient_sendPendingNow(XS7TcpClient* client, XS7PendingRequest* pending);
static bool XS7TcpClient_pumpSendQueue(XS7TcpClient* client);
static void XS7TcpClient_freePending(XS7PendingRequest* pending);
static void XS7TcpClient_abortReplyQueuedChunks(XS7TcpClient* client, XPlcReply* reply);
static void XS7TcpClient_handleRequestTimeout(XS7TcpClient* client, uint16_t pduRef);
static void XS7TcpClient_processFrame(XS7TcpClient* client, const uint8_t* s7, size_t len);
static void XS7TcpClient_finishReply(XS7TcpClient* client, XPlcReply* reply, uint8_t kind);
static bool XS7TcpClient_buildChunkAddress(const XS7Address* base, XS7ValueType type,
                                           size_t chunkByteOffset, size_t chunkElems, XS7Address* out);
static size_t XS7TcpClient_valueTotalBytes(XS7ValueType type, int count);
static size_t XS7TcpClient_elemStepBytes(XS7ValueType type);
static XS7PendingRequest* XS7TcpClient_buildChunkPending(XS7TcpClient* client, XPlcReply* reply,
                                                         uint8_t kind, const XS7Address* addr,
                                                         const uint8_t* data, uint16_t bitCount,
                                                         uint16_t chunkIndex, uint16_t chunkCount);

/* =============== 请求回带（reply->m_request）内部布局 ===============
 * 回带同时承担"分片解码参数随行"职责：分片完成时据此解码回填 result。
 * 字段全部大端（XMemory_write_data BIG_ENDIAN）：
 *   [0]      u8   kind（XS7_ECHO_KIND_*）
 *   [1]      u8   valueType（XS7ValueType）
 *   [2..3]   u16  count（元素个数；String 为容量 n）
 *   [4..7]   u32  bitOffset（基址位偏移；T/C 区为元件序号）
 *   [8..9]   u16  dbNumber
 *   [10]     u8   area（XS7Area 线码）
 *   [11..14] u32  totalBytes（读写总字节；下载为块总长）
 *   [15..16] u16  chunkCount（分片总数；序列型 0）
 *   [17]     u8   nameLen（块名长度，仅块服务有效）
 *   [18..]   name + payload（下载：块字节；写入：编码值字节）
 */
#define XS7_ECHO_OFF_KIND        0
#define XS7_ECHO_OFF_TYPE        1
#define XS7_ECHO_OFF_COUNT       2
#define XS7_ECHO_OFF_BITOFFSET   4
#define XS7_ECHO_OFF_DBNUMBER    8
#define XS7_ECHO_OFF_AREA        10
#define XS7_ECHO_OFF_TOTALBYTES  11
#define XS7_ECHO_OFF_CHUNKCOUNT  15
#define XS7_ECHO_OFF_NAMELEN     17
#define XS7_ECHO_OFF_NAME        18
#define XS7_ECHO_HEADER_SIZE     18

#define XS7_ECHO_KIND_READ        1
#define XS7_ECHO_KIND_WRITE       2
#define XS7_ECHO_KIND_CONTROL     3
#define XS7_ECHO_KIND_BLOCK_LIST  4
#define XS7_ECHO_KIND_BLOCK_UP    5
#define XS7_ECHO_KIND_BLOCK_DL    6
#define XS7_ECHO_KIND_RAW         7

// =============== 字节序辅助函数 ===============
/**
 * @brief 从大端序数据读取 uint16
 */
static inline uint16_t s7ReadU16BE(const uint8_t* data, size_t offset)
{
    uint16_t value;
    XMemory_read_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&value, sizeof(uint16_t));
    return value;
}

/**
 * @brief 写入 uint16 到大端序数据
 */
static inline void s7WriteU16BE(uint8_t* data, size_t offset, uint16_t value)
{
    XMemory_write_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&value, sizeof(uint16_t));
}

/**
 * @brief 写入 uint32 到大端序数据
 */
static inline void s7WriteU32BE(uint8_t* data, size_t offset, uint32_t value)
{
    XMemory_write_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&value, sizeof(uint32_t));
}

// =============== 零警告容器转发（派生→基类显式上行转换） ===============
/* 库内容器接口以基类指针（XClass/XContainer/XVector/XMapBase/XQueueBase）
 * 为参，C 无继承语法，派生指针直传触发 MSVC C4133 隐式转换告警；
 * 本文件按"所属文件零警告"要求经下列内联转发做显式上行转换，
 * 布局兼容性与母版直传语义完全一致（首成员继承链）。 */
static inline size_t s7ByteArraySize(const XByteArray* b)
{
    return XContainer_size_base((const XContainer*)b);
}

static inline const uint8_t* s7ByteArrayDataC(const XByteArray* b)
{
    return (const uint8_t*)XContainerDataAddr((const XContainer*)b);
}

static inline uint8_t* s7ByteArrayData(XByteArray* b)
{
    return (uint8_t*)XContainerDataAddr((XContainer*)b);
}

static inline bool s7ByteArrayAppend(XByteArray* b, const void* data, size_t n)
{
    return XVector_push_back_2((XVector*)b, data, n);
}

static inline void s7ByteArrayClear(XByteArray* b)
{
    XContainer_clear_base((XContainer*)b);
}

static inline void s7ByteArrayDelete(XByteArray* b)
{
    XClass_delete_base((XClass*)b);
}

static inline bool s7MapInsert(XHashMap* m, const void* key, const void* val)
{
    return XMapBase_insert_base((XMapBase*)m, key, val);
}

static inline bool s7MapRemoveKey(XHashMap* m, const void* key)
{
    return XMapBase_remove_base((XMapBase*)m, key);
}

static inline void* s7MapValue(XHashMap* m, const void* key)
{
    return XMapBase_value_base((XMapBase*)m, key);
}

static inline void s7MapClear(XHashMap* m)
{
    XContainer_clear_base((XContainer*)m);
}

static inline size_t s7MapSize(const XHashMap* m)
{
    return XContainer_size_base((const XContainer*)m);
}

static inline void s7MapDelete(XHashMap* m)
{
    XClass_delete_base((XClass*)m);
}

static inline bool s7QueuePush(XQueue* q, void* val)
{
    return XQueueBase_push_base((XQueueBase*)q, val);
}

static inline bool s7QueueTake(XQueue* q, void* out)
{
    return XQueueBase_receive_base((XQueueBase*)q, out);
}

static inline void s7QueuePopHead(XQueue* q)
{
    XQueueBase_pop_base((XQueueBase*)q);
}

static inline void* s7QueueHead(XQueue* q)
{
    return XQueueBase_top_base((XQueueBase*)q);
}

static inline size_t s7QueueSize(const XQueue* q)
{
    return XContainer_size_base((const XContainer*)q);
}

static inline void s7QueueDelete(XQueue* q)
{
    XClass_delete_base((XClass*)q);
}

/** @brief 有界字符串长度（替代 strlen，遵守 string.h 白名单：memcpy/memset/memmove/memcmp） */
static size_t s7CStrLen(const char* s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

/** @brief 有界字符串拷贝（替代 strncpy，消除 C4996 弃用告警） */
static void s7CopyBounded(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t n = s7CStrLen(src);
    if (n > dstSize - 1) n = dstSize - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// =============== 零警告键比较（精确匹配 XCompare 签名） ===============
/**
 * @brief uint16_t 键比较（pduRef → pending 表用）
 * @note 库内生成的 uint16_t_compare 签名为 (const uint16_t*)，与 XCompare
 *       (const void*) 不一致会触发告警；此处按规范手写等价实现
 */
static int32_t s7PduRefCompare(const void* lhs, const void* rhs)
{
    uint16_t a = *(const uint16_t*)lhs;
    uint16_t b = *(const uint16_t*)rhs;
    if (a < b) return XCompare_Less;
    if (a > b) return XCompare_Greater;
    return XCompare_Equality;
}

/**
 * @brief size_t 键比较（XTimerId → pduRef 反查表用）
 */
static int32_t s7TimerIdCompare(const void* lhs, const void* rhs)
{
    size_t a = (size_t)(*(const XTimerId*)lhs);
    size_t b = (size_t)(*(const XTimerId*)rhs);
    if (a < b) return XCompare_Less;
    if (a > b) return XCompare_Greater;
    return XCompare_Equality;
}

// =============== S7 数据区返回码 → 错误描述 ===============
/**
 * @brief 数据区单项返回码 → 错误描述文本
 * @param returnCode 应答项返回码（XS7_RETURN_*，0xFF 成功）
 * @return 固定描述文本（静态存储，无需释放）
 */
static const char* s7ReturnCodeText(uint8_t returnCode)
{
    switch (returnCode) {
    case XS7_RETURN_DATA_ERROR:   return "S7 data error 0x05 (address out of range)";
    case XS7_RETURN_ACCESS_ERROR: return "S7 access error 0x06 (access denied)";
    case 0x07:                    return "S7 data error 0x07 (data type not consistent)";
    case XS7_RETURN_RANGE_ERROR:  return "S7 range error 0x0A (out of range)";
    default:                      return "S7 data error";
    }
}

// =============== 值尺寸/分片规划辅助 ===============
/**
 * @brief 计算一个值读写所需的传输总字节数
 * @param type 值类型
 * @param count 元素个数（String 时为容量 n）
 * @return 传输总字节数（响应侧/编码侧一致口径）
 * @note Bool 位传输：响应每 bit 独占 1 字节（s7comm 通行实现），按 count 字节
 *       预算；String 为 S7 STRING(n) 结构 = n+2 字节（[max][cur][data]，偶对齐
 *       在编码器侧处理，此处按最坏 n+2 预算）
 */
static size_t XS7TcpClient_valueTotalBytes(XS7ValueType type, int count)
{
    size_t n = (count > 0) ? (size_t)count : 1;
    switch (type) {
    case XS7Value_Bool:  return n;        ///< 每 bit 一响应字节
    case XS7Value_Byte:  return n;
    case XS7Value_Word:  return n * 2;
    case XS7Value_DWord: return n * 4;
    case XS7Value_Real:  return n * 4;
    case XS7Value_String: return n + 2;   ///< [max][cur][data...]
    default:             return n;
    }
}

/**
 * @brief 由基址与分片偏移构造分片地址
 * @param base 解析好的基址（type/count 已按调用参数覆写）
 * @param type 值类型（取自 base->type，透传保参）
 * @param chunkByteOffset 本片在数据流内的字节偏移（Bool 为位偏移字节数）
 * @param chunkElems 本片元素个数
 * @param out 输出分片地址
 * @return 构造成功返回true
 * @note 普通区偏移 = byte*8；T/C 字访问区偏移 = 元件序号（每元件 2 字节，
 *       故字节偏移除 2）；Bool 区 bitOffset 直接按位递增
 */
static bool XS7TcpClient_buildChunkAddress(const XS7Address* base, XS7ValueType type,
                                           size_t chunkByteOffset, size_t chunkElems, XS7Address* out)
{
    if (!base || !out) return false;
    *out = *base;
    out->type = type;
    out->count = (uint16_t)chunkElems;
    if (type == XS7Value_Bool) {
        out->bitOffset = base->bitOffset + (uint32_t)chunkByteOffset;
        return true;
    }
    if (type == XS7Value_String) {
        /* String 首片：chunkElems 以线上字节计（n+2 = [max][cur][data]，步长 1），
         * 而 ANY 项 count 语义为容量 n（xs7PduItemTransport 对 String 再 +2），
         * 此处折回容量使 item 宣称字节数与数据区一致；续片由调用方改传
         * Byte 类型，不走此分支 */
        out->count = (chunkElems >= 2) ? (uint16_t)(chunkElems - 2) : 0;
    }
    if (base->area == XS7Area_T || base->area == XS7Area_C) {
        /* T/C 字访问：bitOffset 字段承载元件序号，每元件 2 字节 */
        out->bitOffset = base->bitOffset + (uint32_t)(chunkByteOffset / 2);
    }
    else {
        out->bitOffset = base->bitOffset + (uint32_t)(chunkByteOffset * 8);
    }
    return true;
}

/**
 * @brief 单元素的步进字节数（分片字节偏移折算用）
 * @note Bool 位传输响应每 bit 1 字节；String 按结构字节流线性切（步长 1）
 */
static size_t XS7TcpClient_elemStepBytes(XS7ValueType type)
{
    switch (type) {
    case XS7Value_Word:  return 2;
    case XS7Value_DWord: return 4;
    case XS7Value_Real:  return 4;
    default:             return 1;   /* Bool/Byte/String */
    }
}

// =============== pending 生命周期 ===============
/**
 * @brief 分配并预填一个 pending（frame 由调用方构建后挂入）
 */
static XS7PendingRequest* XS7TcpClient_createPending(XPlcReply* reply, uint16_t pduRef,
                                                     uint8_t kind, uint16_t chunkIndex, uint16_t chunkCount)
{
    XS7PendingRequest* pending =
        (XS7PendingRequest*)XMemory_malloc(sizeof(XS7PendingRequest), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!pending) return NULL;
    pending->reply = reply;
    pending->pduRef = pduRef;
    pending->retryCount = 0;
    pending->kind = kind;
    pending->ackFinished = 0;
    pending->timeoutTimer = XTIMER_INVALID_ID;
    pending->chunkIndex = chunkIndex;
    pending->chunkCount = chunkCount;
    pending->frame = NULL;
    return pending;
}

/**
 * @brief 释放一个 pending（含其持有帧）
 */
static void XS7TcpClient_freePending(XS7PendingRequest* pending)
{
    if (!pending) return;
    if (pending->frame) {
        s7ByteArrayDelete(pending->frame);
        pending->frame = NULL;
    }
    XMemory_free(pending, XCLASS_DEFAULT_MEMORY_TYPE);
}

// =============== 请求回带（echo）构建/读取 ===============
/**
 * @brief 构建"请求回带"头（分片解码参数随行）
 * @param kind 回带类别（XS7_ECHO_KIND_*）
 * @param type 值类型（无类型语义时填 Byte）
 * @param count 元素个数
 * @param addr 基址（可为NULL：控制/块类无地址语义）
 * @param totalBytes 传输总字节数
 * @param chunkCount 分片总数（序列型 0）
 * @param out 输出缓冲（至少 XS7_ECHO_HEADER_SIZE 字节）
 */
static void XS7TcpClient_buildEchoHeader(uint8_t kind, XS7ValueType type, int count,
                                         const XS7Address* addr, uint32_t totalBytes,
                                         uint16_t chunkCount, uint8_t* out)
{
    memset(out, 0, XS7_ECHO_HEADER_SIZE);
    out[XS7_ECHO_OFF_KIND] = kind;
    out[XS7_ECHO_OFF_TYPE] = (uint8_t)type;
    s7WriteU16BE(out, XS7_ECHO_OFF_COUNT, (uint16_t)count);
    if (addr) {
        s7WriteU32BE(out, XS7_ECHO_OFF_BITOFFSET, addr->bitOffset);
        s7WriteU16BE(out, XS7_ECHO_OFF_DBNUMBER, addr->dbNumber);
        out[XS7_ECHO_OFF_AREA] = addr->area;
    }
    s7WriteU32BE(out, XS7_ECHO_OFF_TOTALBYTES, totalBytes);
    s7WriteU16BE(out, XS7_ECHO_OFF_CHUNKCOUNT, chunkCount);
}

/**
 * @brief 读取回带字段（大端）
 */
static XS7ValueType echoValueType(const XByteArray* request)
{
    const uint8_t* d = s7ByteArrayDataC(request);
    return d ? (XS7ValueType)d[XS7_ECHO_OFF_TYPE] : XS7Value_Byte;
}

static int echoCount(const XByteArray* request)
{
    const uint8_t* d = s7ByteArrayDataC(request);
    return d ? (int)s7ReadU16BE(d, XS7_ECHO_OFF_COUNT) : 0;
}

/**
 * @brief 读取回带尾部的块名（NUL 结尾拷贝到栈缓冲）
 * @param request 请求回带
 * @param nameBuf 输出缓冲（调用者提供，建议 32 字节）
 * @param bufSize 缓冲大小
 * @return 块名指针（nameBuf），无块名返回 NULL
 */
static const char* echoBlockName(const XByteArray* request, char* nameBuf, size_t bufSize)
{
    const uint8_t* d = s7ByteArrayDataC(request);
    if (!d || !nameBuf || bufSize == 0) return NULL;
    size_t bufLen = s7ByteArraySize(request);
    if (bufLen < XS7_ECHO_HEADER_SIZE + 1) return NULL;
    size_t nameLen = d[XS7_ECHO_OFF_NAMELEN];
    if (nameLen == 0 || nameLen >= bufSize) return NULL;
    if (bufLen < XS7_ECHO_HEADER_SIZE + 1 + nameLen) return NULL;
    memcpy(nameBuf, d + XS7_ECHO_OFF_NAME, nameLen);
    nameBuf[nameLen] = '\0';
    return nameBuf;
}

/**
 * @brief 读取回带尾部载荷指针/长度（下载块字节 / 写入编码值）
 */
static const uint8_t* echoPayload(const XByteArray* request, size_t* outLen)
{
    const uint8_t* d = s7ByteArrayDataC(request);
    if (!d) { if (outLen) *outLen = 0; return NULL; }
    size_t bufLen = s7ByteArraySize(request);
    if (bufLen <= XS7_ECHO_HEADER_SIZE) { if (outLen) *outLen = 0; return NULL; }
    if (outLen) *outLen = bufLen - XS7_ECHO_HEADER_SIZE;
    return d + XS7_ECHO_HEADER_SIZE;
}

// =============== 帧构建辅助 ===============
/**
 * @brief 将一段 S7 PDU 封装为完整待发帧（COTP DT 头 + TPKT）
 * @param pdu S7 PDU 字节（不含 TPKT/COTP）
 * @param pduLen PDU 长度
 * @return 新建帧字节数组（调用者持有），失败返回NULL
 * @note pduLen 上限为 XS7_REQUESTED_PDU_LEN（栈缓冲防溢出保护）
 */
static XByteArray* XS7TcpClient_wrapDtFrame(const uint8_t* pdu, size_t pduLen)
{
    uint8_t dt[XS7_COTP_DT_HEADER_SIZE + XS7_REQUESTED_PDU_LEN];
    if (!pdu || pduLen == 0 || pduLen > XS7_REQUESTED_PDU_LEN) return NULL;

    dt[0] = 0x02;
    dt[1] = XS7COTP_PDU_DT;
    dt[2] = 0x80;
    memcpy(dt + XS7_COTP_DT_HEADER_SIZE, pdu, pduLen);

    XByteArray* frame = XByteArray_create();
    if (!frame) return NULL;
    if (XS7Tpkt_wrap(frame, dt, XS7_COTP_DT_HEADER_SIZE + pduLen) == 0) {
        s7ByteArrayDelete(frame);
        return NULL;
    }
    return frame;
}

// =============== 类初始化 ===============
XVtable* XS7TcpClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XS7TcpClient)
        // 先继承（顺序不可颠倒）
        XVTABLE_INHERIT_XCLASS(XPlcClient);

    // 后重载：Open/Close/Deinit/TimerEvent/SendRequest/ProcessResponse
    XVTABLE_OVERLOAD_DEFAULT(EXPlcDevice_Open, VXPlcDevice_open_s7);
    XVTABLE_OVERLOAD_DEFAULT(EXPlcDevice_Close, VXPlcDevice_close_s7);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlcClient_deinit_s7);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXPlcClient_timerEvent_s7);
    XVTABLE_OVERLOAD_DEFAULT(EXPlcClient_SendRequest, VXPlcClient_sendRequest_s7);
    XVTABLE_OVERLOAD_DEFAULT(EXPlcClient_ProcessResponse, VXPlcClient_processResponse_s7);

    XCLASS_SHOW_SIZE_DEFAULT(XS7TcpClient);
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化/析构 ===============
XS7TcpClient* XS7TcpClient_create_ex(XMemoryType memory)
{
    XS7TcpClient* client = (XS7TcpClient*)XMemory_malloc(sizeof(XS7TcpClient), memory);
    if (!client) return NULL;
    XS7TcpClient_init(client);
    Set_Class_Memory(client, memory); Set_Class_IsHeap(client, true);
    return client;
}

void XS7TcpClient_init(XS7TcpClient* client)
{
    if (!client) return;

    // 初始化基类
    XPlcClient_init(&client->m_base);
    XClassGetVtable(client) = XS7TcpClient_class_init();

    // 初始化成员
    client->m_session = XS7Session_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (client->m_session) {
        XObject_setParent((XObject*)client->m_session, (XObject*)client);
    }
    client->m_pendingRequests = XHashMap_Create(uint16_t, XS7PendingRequest*, s7PduRefCompare);
    client->m_timerMap = XHashMap_Create(XTimerId, uint16_t, s7TimerIdCompare);
    client->m_sendQueue = XQueue_Create(XS7PendingRequest*);
    client->m_requestData = XByteArray_create();
    client->m_negotiatedPduLen = XS7_FALLBACK_PDU_LEN;
    client->m_pumping = false;
    client->m_clearingAll = false;
}

static void VXPlcClient_deinit_s7(XS7TcpClient* client)
{
    if (!client) return;

    XPlcDevice_disconnectDevice((XPlcDevice*)client);

    if (client->m_session) {
        XS7Session_deleteLater((XObject*)client->m_session);
        client->m_session = NULL;
    }

    if (client->m_requestData) {
        s7ByteArrayDelete(client->m_requestData);
        client->m_requestData = NULL;
    }

    /* 停定时器、对各 Reply 置错、清 pending 表/timerMap 并释放 pending
     *（含 m_sendQueue 内排队项） */
    XS7TcpClient_clearAllPendingRequests(client, XPlcDevice_ConnectionError, "Connection lost");

    if (client->m_pendingRequests) {
        s7MapDelete(client->m_pendingRequests);
        client->m_pendingRequests = NULL;
    }
    if (client->m_timerMap) {
        s7MapDelete(client->m_timerMap);
        client->m_timerMap = NULL;
    }
    if (client->m_sendQueue) {
        s7QueueDelete(client->m_sendQueue);
        client->m_sendQueue = NULL;
    }

    // 释放子类创建的 TCP 套接字
    XIODevice* io = ((XPlcDevice*)client)->m_ioDevice;
    if (io) {
        XObject_deleteLater((XObject*)io);
        ((XPlcDevice*)client)->m_ioDevice = NULL;
    }

    client->m_negotiatedPduLen = XS7_FALLBACK_PDU_LEN;

    // 调用基类析构
    XClass_Deinit_Parent(XPlcClient, (XPlcClient*)client);
}

// =============== 受保护 API ===============
XTcpSocket* XS7TcpClient_socket(const XS7TcpClient* client)
{
    if (!client) return NULL;
    XIODevice* io = ((const XPlcDevice*)client)->m_ioDevice;
    if (!io) return NULL;
    return (XTcpSocket*)io;
}

bool XS7TcpClient_hasPendingRequests(const XS7TcpClient* client)
{
    if (!client || !client->m_pendingRequests) return false;
    return s7MapSize(client->m_pendingRequests) > 0;
}

size_t XS7TcpClient_pendingRequestCount(const XS7TcpClient* client)
{
    if (!client || !client->m_pendingRequests) return 0;
    return s7MapSize(client->m_pendingRequests);
}

void XS7TcpClient_clearAllPendingRequests(XS7TcpClient* client, XPlcDevice_Error error, const char* errorMsg)
{
    if (!client || !client->m_pendingRequests) return;
    if (client->m_clearingAll) {
        /* 重入防护（真机第 1 轮 0xC0000005 直接根因）：本函数对 Reply 置错
         * 会同步触发调用方 finished 槽，槽内可能走到 disconnectDevice →
         * close → 本函数重入，对同一批 pending 二次快照二次释放。
         * 首次调用已覆盖全部清理，重入直接返回。 */
        return;
    }
    client->m_clearingAll = true;

    /* 三段式防双释放（真机第 1 轮 0xC0000005 根因防御）：
     * 1) 只把表内 pending 的"值指针"快照下来（不解引用）；若迭代出的节点数
     *    超过表 size（容器 clear/remove 与桶数组可失步），判表已不可信：
     *    整表废弃重建、一个都不碰；
     * 2) 快照内按指针去重后逐个（停定时器→Reply 置错→释放）；
     * 3) 串行队列排空（与快照去重）；
     * 4) 尾部不信任 clear——直接废弃重建 pending 表与定时器反查表，
     *    杜绝"clear 后桶数组仍可达旧节点"导致的二次遍历二次释放。 */
    {
        size_t total = s7MapSize(client->m_pendingRequests);
        size_t iterated = 0;
        size_t count = 0;
        XS7PendingRequest** snapshot = NULL;
        size_t i, j;
        if (total > 0) {
            snapshot = (XS7PendingRequest**)XMemory_malloc(
                sizeof(XS7PendingRequest*) * total, XCLASS_DEFAULT_MEMORY_TYPE);
        }
        for_each_iterator(client->m_pendingRequests, XHashMap, it)
        {
            XPair* pair = XHashMap_iterator_data(&it);
            const void* valuePtr = (pair) ? XPair_second(pair) : NULL;
            ++iterated;
            if (!valuePtr) continue;
            if (!snapshot || count >= total) continue;   /* 超量：只计数，不解引用 */
            snapshot[count++] = *(XS7PendingRequest**)valuePtr;
        }
        if (iterated > total) {
            /* 容器失步（clear/remove 残留节点）：废弃重建，不释放任何
             * pending（残留节点值可能已悬垂），防解引用已释放内存 */
            XERROR_PRINTF("[XS7TcpClient] pending map desync (iterated=%u size=%u), "
                          "rebuild map\n", (unsigned)iterated, (unsigned)total);
            s7MapDelete(client->m_pendingRequests);
            client->m_pendingRequests =
                XHashMap_Create(uint16_t, XS7PendingRequest*, s7PduRefCompare);
            if (client->m_timerMap) {
                s7MapDelete(client->m_timerMap);
                client->m_timerMap = XHashMap_Create(XTimerId, uint16_t, s7TimerIdCompare);
            }
            if (snapshot) {
                XMemory_free(snapshot, XCLASS_DEFAULT_MEMORY_TYPE);
            }
            client->m_clearingAll = false;
            return;
        }
        for (i = 0; i < count; ++i) {
            XS7PendingRequest* pending = snapshot[i];
            if (pending->timeoutTimer != XTIMER_INVALID_ID) {
                s7MapRemoveKey(client->m_timerMap, &pending->timeoutTimer);
                XObject_killTimer((XObject*)client, pending->timeoutTimer);
                pending->timeoutTimer = XTIMER_INVALID_ID;
            }
            if (pending->reply && error != XPlcDevice_NoError) {
                XPlcReply_setError(pending->reply, error, errorMsg);
                XPlcReply_setState(pending->reply, XPlcReply_State_No_Started);
            }
            XS7TcpClient_freePending(pending);
        }

        // 排空串行队列：排队项一并释放（与快照去重，防跨结构二次释放）
        if (client->m_sendQueue) {
            while (s7QueueSize(client->m_sendQueue) > 0) {
                XS7PendingRequest* queued = NULL;
                bool seen = false;
                if (!s7QueueTake(client->m_sendQueue, &queued)) break;
                if (!queued) continue;
                for (j = 0; j < count; ++j) {
                    if (snapshot[j] == queued) { seen = true; break; }
                }
                if (seen) continue;
                if (queued->reply && error != XPlcDevice_NoError) {
                    XPlcReply_setError(queued->reply, error, errorMsg);
                    XPlcReply_setState(queued->reply, XPlcReply_State_No_Started);
                }
                XS7TcpClient_freePending(queued);
            }
        }
        if (snapshot) {
            XMemory_free(snapshot, XCLASS_DEFAULT_MEMORY_TYPE);
        }
    }
    /* 尾部：废弃重建两张表（不信任容器 clear 与桶数组的同步性） */
    s7MapDelete(client->m_pendingRequests);
    client->m_pendingRequests = XHashMap_Create(uint16_t, XS7PendingRequest*, s7PduRefCompare);
    if (client->m_timerMap) {
        s7MapDelete(client->m_timerMap);
        client->m_timerMap = XHashMap_Create(XTimerId, uint16_t, s7TimerIdCompare);
    }
    client->m_clearingAll = false;
}

// =============== 发送管线（AmQ=1 串行队列） ===============
/**
 * @brief 将一个 pending 加入串行队列
 * @return 队列非法或 reply 已完成返回 false（pending 由调用方释放），否则 true
 * @note 仅入队不发送；实际发送由 pumpSendQueue 统一驱动
 */
static bool XS7TcpClient_enqueuePending(XS7TcpClient* client, XS7PendingRequest* pending)
{
    if (!client || !client->m_sendQueue || !pending) return false;
    if (pending->reply && XPlcReply_isFinished(pending->reply)) return false;
    {
        {
            size_t before = s7QueueSize(client->m_sendQueue);
            if (!s7QueuePush(client->m_sendQueue, &pending)) {
                /* 容器虚槽经 bool 截断的返回值不可信（真机第 1 轮 UAF 根因）：
                 * 以队列长度实变为准确认入队，长度未增才判失败，
                 * 严防"半成功"状态下释放 pending 造成队列悬垂指针 */
                if (s7QueueSize(client->m_sendQueue) == before) {
                    return false;
                }
            }
        }
        return true;
    }
}

/**
 * @brief 立即发送一个 pending（登记 pending 表 + 起超时定时器）
 * @return 发送成功返回true
 * @note AmQ=1 语义：调用前必须确认无 in-flight（timerMap 为空），
 *       由 pumpSendQueue 保证
 */
static bool XS7TcpClient_sendPendingNow(XS7TcpClient* client, XS7PendingRequest* pending)
{
    if (!client || !pending || !pending->frame || !pending->reply) return false;

    XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
    if (!socket || !XTcpSocket_isOpen((const XIODevice*)socket)) return false;

    int64_t sent = XIODevice_write_2((XIODevice*)socket, pending->frame);
    if (sent <= 0) return false;

    // 起该片独立超时定时器并登记两级映射
    int timeout = (int)XPlcClient_timeout(&client->m_base);
    pending->timeoutTimer = XObject_startTimer_ms((XObject*)client, timeout, XTimerType_CoarseTimer);
    s7MapInsert(client->m_pendingRequests, &pending->pduRef, &pending);
    s7MapInsert(client->m_timerMap, &pending->timeoutTimer, &pending->pduRef);

    XPlcReply_setState(pending->reply, XPlcReply_State_Waiting);
    return true;
}

/**
 * @brief 驱动串行队列：无 in-flight 时持续发队头直至队列空或有请求在途
 * @return 本次驱动至少发出一片、或仍有在途/待发请求返回true
 * @note 重入防护（真机第 1 轮 0xC0000005 根因）：发送失败分支对 Reply
 *       setError 会同步触发调用方的 finished 槽，槽内可能再次 send* →
 *       commitReply → 本函数重入，嵌套排空队列使外层迭代悬挂的 pending
 *       指针失配（use-after-free）。故迭代期间 m_pumping=true，重入调用
 *       直接返回 true（"有待发工作"），由外层 while 继续排空新入队项。
 */
static bool XS7TcpClient_pumpSendQueue(XS7TcpClient* client)
{
    if (!client || !client->m_sendQueue || !client->m_timerMap) return false;
    if (XPlcDevice_state((XPlcDevice*)client) != XPlcDevice_ConnectedState) return false;
    if (client->m_pumping) {
        return true;   /* 重入：外层循环收尾前会驱动新入队项，勿嵌套排空 */
    }

    client->m_pumping = true;
    while (s7MapSize(client->m_timerMap) == 0 &&
           s7QueueSize(client->m_sendQueue) > 0) {
        XS7PendingRequest* pending = *(XS7PendingRequest**)s7QueueHead(client->m_sendQueue);
        s7QueuePopHead(client->m_sendQueue);
        if (!pending) continue;
        /* Reply 已完成/被中止的排队项：静默丢弃 */
        if (!pending->reply || XPlcReply_isFinished(pending->reply)) {
            XS7TcpClient_freePending(pending);
            continue;
        }
        if (!XS7TcpClient_sendPendingNow(client, pending)) {
            /* 发送失败：置错并丢弃该 Reply 的全部排队分片（setError 可能
             * 同步重入本函数，m_pumping 已挡住嵌套排空） */
            XPlcReply_setError(pending->reply, XPlcDevice_WriteError, "failed to send request chunk");
            XS7TcpClient_abortReplyQueuedChunks(client, pending->reply);
            XS7TcpClient_freePending(pending);
            continue;
        }
    }
    client->m_pumping = false;
    return s7MapSize(client->m_timerMap) > 0;
}

/**
 * @brief 丢弃某 Reply 仍排队的全部分片（出错中止分片序列时调用）
 * @note 只处理 m_sendQueue 内项；in-flight 项由调用方先行处置
 */
static void XS7TcpClient_abortReplyQueuedChunks(XS7TcpClient* client, XPlcReply* reply)
{
    if (!client || !client->m_sendQueue || !reply) return;

    size_t n = s7QueueSize(client->m_sendQueue);
    while (n-- > 0) {
        XS7PendingRequest* queued = NULL;
        if (!s7QueueTake(client->m_sendQueue, &queued)) break;
        if (!queued) continue;
        if (queued->reply == reply) {
            XS7TcpClient_freePending(queued);
        }
        else {
            s7QueuePush(client->m_sendQueue, &queued);
        }
    }
}

// =============== 超时处理 ===============
/**
 * @brief 从 pending 表摘除一个 pending：摘除成功即释放
 * @warning 摘除失败（容器残留同键/异键节点）时**不得释放**：把 pending
 *          游离化（frame 释放、reply 指针置空、保留结构体内存），
 *          节点值指针仍指向该结构体，由 clearAllPendingRequests 的
 *          快照去重统一回收——任何路径都不会二次释放或解引用已释放
 *          pending（真机第 1 轮 0xC0000005 根因防御）。
 */
static void XS7TcpClient_removePendingFromMap(XS7TcpClient* client, XS7PendingRequest* pending)
{
    if (!client || !pending) return;
    if (s7MapRemoveKey(client->m_pendingRequests, &pending->pduRef)) {
        XS7TcpClient_freePending(pending);
        return;
    }
    XERROR_PRINTF("[XS7TcpClient] pending map remove failed ref=%u, detach pending=%p\n",
                  (unsigned)pending->pduRef, (void*)pending);
    if (pending->frame) {
        s7ByteArrayDelete(pending->frame);
        pending->frame = NULL;
    }
    pending->reply = NULL;
    pending->timeoutTimer = XTIMER_INVALID_ID;
}

/**
 * @brief 单片超时：按 numberOfRetries 重发该片（改写帧内 pduRef），用尽置 TimeoutError
 */
static void XS7TcpClient_handleRequestTimeout(XS7TcpClient* client, uint16_t pduRef)
{
    if (!client || !client->m_pendingRequests) return;

    XS7PendingRequest** slot = (XS7PendingRequest**)s7MapValue(client->m_pendingRequests, &pduRef);
    XS7PendingRequest* pending = slot ? *slot : NULL;
    if (!pending) return;

    /* 定时器已由 TimerEvent 反查路径从 timerMap 移除，这里清字段 */
    pending->timeoutTimer = XTIMER_INVALID_ID;

    XPlcReply* reply = pending->reply;
    uint8_t retryCount = pending->retryCount;
    int maxRetries = XPlcClient_numberOfRetries(&client->m_base);

    if (reply && retryCount < (uint8_t)maxRetries && pending->frame) {
        XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
        if (socket && XTcpSocket_isOpen((const XIODevice*)socket) &&
            s7ByteArraySize(pending->frame) >= XS7_PDU_REF_FRAME_OFFSET + XS7_PDU_REF_FRAME_SIZE) {
            /* 原帧重发：改写帧内 pduRef（TPKT4+COTP3+ROSCTR1 之后 2 字节）并换表键。
             * 注意：表键为插入时的键拷贝，重试必须确认旧键摘除成功后再改
             * pending->pduRef 并以新键重插——否则残留旧键节点会形成同一
             * pending 的双表项，clearAll 处按指针去重兜底（真机第 1 轮实测
             * 曾因此双释放）。 */
            uint8_t* frameData = s7ByteArrayData(pending->frame);
            uint16_t newRef = XS7Session_nextPduRef(client->m_session);
            s7WriteU16BE(frameData, XS7_PDU_REF_FRAME_OFFSET, newRef);

            if (!s7MapRemoveKey(client->m_pendingRequests, &pduRef)) {
                /* 旧键摘除失败：不改键、不再重插（防双表项），直接走超时收尾 */
                XERROR_PRINTF("[XS7TcpClient] timeout retry: stale key %u removal failed, "
                              "fall through to timeout\n", (unsigned)pduRef);
            }
            else {
                pending->pduRef = newRef;
                if (XS7TcpClient_sendPendingNow(client, pending)) {
                    pending->retryCount = (uint8_t)(retryCount + 1);
                    return;   /* 重试已发出，等待新一轮应答/超时 */
                }
                /* 重发失败：表键已移除，走下方超时收尾 */
            }
        }
    }

    /* 重试用尽或重发失败：该 Reply 结束，剩余排队分片一并中止。
     * setError 会同步触发调用方 finished 槽（槽内可能发起新请求），故
     * 本 pending 的摘表与释放在槽返回后执行。 */
    if (reply) {
        XPlcReply_setError(reply, XPlcDevice_TimeoutError, "Request timeout");
        XPlcReply_setState(reply, XPlcReply_State_Timeout);
    }
    pending->pduRef = pduRef;   /* 与表键对齐后再摘（重试失败路径未改键） */
    XS7TcpClient_removePendingFromMap(client, pending);
    XS7TcpClient_abortReplyQueuedChunks(client, reply);
    XS7TcpClient_pumpSendQueue(client);
}

/**
 * @brief 握手步超时（CR/Setup 任一步无应答）
 * @note 必须 XS7Session_reset 后再重连，防残留半帧污染二次握手（设计§7.10）
 */
static void XS7TcpClient_handleHandshakeTimeout(XS7TcpClient* client)
{
    if (!client) return;
    XPlcClient_timeoutTimerStop(&client->m_base);
    if (client->m_session) XS7Session_reset(client->m_session);
    XPlcDevice_setError((XPlcDevice*)client, XPlcDevice_TimeoutError, "S7 handshake timeout");

    /* 重连安排先于断开：断开信号里 session 已 Idle，不会重复调度 */
    XS7TcpClient_scheduleReconnectIfAllowed(client);

    XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
    if (socket) {
        XAbstractSocket_disconnectFromHost_base(socket);
    }
    XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_UnconnectedState);
}

/**
 * @brief 握手失败通用收尾（CC 拒绝/协商异常/帧发送失败）
 */
static void XS7TcpClient_handshakeFailed(XS7TcpClient* client, XPlcDevice_Error error, const char* msg)
{
    if (!client) return;
    XPlcClient_timeoutTimerStop(&client->m_base);
    if (client->m_session) XS7Session_reset(client->m_session);
    XPlcDevice_setError((XPlcDevice*)client, error, msg);
    XS7TcpClient_scheduleReconnectIfAllowed(client);

    XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
    if (socket && XTcpSocket_isOpen((const XIODevice*)socket)) {
        XAbstractSocket_disconnectFromHost_base(socket);
    }
    XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_UnconnectedState);
}

// =============== 握手推进 ===============
/**
 * @brief 启动/重启 S7 应用层握手：产出下一步待发帧并发出
 * @return 帧产出并发送成功返回true
 * @note CR 由 Idle 态产出、Setup 由 CcAccepted 后产出；每步重置握手超时
 */
static bool XS7TcpClient_startHandshake(XS7TcpClient* client)
{
    if (!client || !client->m_session) return false;

    XByteArray* frame = client->m_requestData;
    if (!frame) return false;

    if (!XS7Session_buildNextHandshake(client->m_session, frame)) {
        return false;
    }

    XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
    if (!socket || !XTcpSocket_isOpen((const XIODevice*)socket)) return false;

    int64_t sent = XIODevice_write_2((XIODevice*)socket, frame);
    if (sent <= 0) return false;

    /* 每个握手步独立计时（复用基类 m_timeoutTimer） */
    XPlcClient_timeoutTimerStart(&client->m_base);
    return true;
}

// =============== 重连处理 ===============
/**
 * @brief 按 autoReconnect/最大次数安排一次重连定时
 */
static void XS7TcpClient_scheduleReconnectIfAllowed(XS7TcpClient* client)
{
    if (!client) return;
    XPlcClient* baseClient = &client->m_base;
    if (!baseClient->m_autoReconnect) return;
    if (baseClient->m_maxReconnectAttempts >= 0 &&
        baseClient->m_reconnectAttempts >= baseClient->m_maxReconnectAttempts) {
        return;
    }
    XPlcClient_reconnectTimerStart(baseClient);
}

/**
 * @brief 重连定时触发：重读连接参数并发起连接
 */
static void XS7TcpClient_attemptReconnect(XS7TcpClient* client)
{
    if (!client) return;

    XPlcClient* baseClient = &client->m_base;
    XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_ConnectingState);
    baseClient->m_reconnectAttempts++;

    /* 经虚表走 open：地址/端口/TSAP 参数重读，socket 复用 */
    XPlcDevice_connectDevice((XPlcDevice*)client);
}

// =============== 应答处理 ===============
/**
 * @brief 完成收尾：末片成功后按回带类别回填最终结果并置 Finished
 * @param kind pending 类别（决定是否需要解码/如何收尾）
 */
static void XS7TcpClient_finishReply(XS7TcpClient* client, XPlcReply* reply, uint8_t kind)
{
    (void)client;
    if (!reply) return;

    /* 读分片（结构化类型）：rawResult 已按序拼齐，统一解码回填 result */
    if (kind == XS7Pending_Read && reply->m_type == XPlcReply_Common &&
        reply->m_request && reply->m_rawResult) {
        XS7ValueType type = echoValueType(reply->m_request);
        int count = echoCount(reply->m_request);

        const uint8_t* raw = s7ByteArrayDataC(reply->m_rawResult);
        size_t rawLen = s7ByteArraySize(reply->m_rawResult);

        XVariant* decoded = XVariant_create_int(0);
        if (decoded) {
            if (XS7Value_decode(type, count, raw, rawLen, decoded)) {
                XPlcReply_setResult(reply, decoded);
            }
            else {
                XPlcReply_setError(reply, XPlcDevice_InvalidResponseError,
                                   "S7 value decode failed");
            }
            XVariant_delete_base((XClass*)decoded);
        }
    }

    XPlcReply_setState(reply, XPlcReply_State_Finished);
}

/**
 * @brief 处理一帧完整 S7 PDU（readyRead 事件循环内调用）
 * @details parseHeader 取 pduRef → 查 pending 表 → ProcessResponse 回填 →
 *          按类别推进（末片完成/序列下一步/出错中止）→ pump 发下一片；
 *          ref 不在表中：丢弃并 XERROR_PRINTF（防错包污染）
 */
static void XS7TcpClient_processFrame(XS7TcpClient* client, const uint8_t* s7, size_t len)
{
    if (!client || !s7 || len == 0) return;

    XS7PduHeader header;
    memset(&header, 0, sizeof(header));
    if (!XS7Pdu_parseHeader(s7, len, &header)) {
        XERROR_PRINTF("[XS7TcpClient] bad S7 PDU header, drop frame (%u bytes)\n", (unsigned)len);
        return;
    }

    XS7PendingRequest** slot =
        (XS7PendingRequest**)s7MapValue(client->m_pendingRequests, &header.pduRef);
    XS7PendingRequest* pending = slot ? *slot : NULL;
    if (!pending || !pending->reply) {
        XERROR_PRINTF("[XS7TcpClient] response ref %u not in pending table, drop\n",
                      (unsigned)header.pduRef);
        return;
    }
    XPlcReply* reply = pending->reply;

    /* 停该片超时定时器（先摘反查表再杀定时器） */
    if (pending->timeoutTimer != XTIMER_INVALID_ID) {
        s7MapRemoveKey(client->m_timerMap, &pending->timeoutTimer);
        XObject_killTimer((XObject*)client, pending->timeoutTimer);
        pending->timeoutTimer = XTIMER_INVALID_ID;
    }
    XPlcReply_setState(reply, XPlcReply_State_Responding);

    bool ok = false;
    /* 借用帧拷入临时字节数组交虚槽解析回填（栈对象 init/deinit 成对） */
    {
        XByteArray pdu;
        XByteArray_init(&pdu, true);
        s7ByteArrayAppend(&pdu, s7, len);
        ok = XPlcClient_processResponse_base(&client->m_base, &pdu, reply);
        XClass_deinit_base((XClass*)&pdu);
    }

    /* 摘除该片 pending（无论成败均不再复用）；推进所需字段先拷出 */
    uint8_t kind = pending->kind;
    uint8_t ackFinished = pending->ackFinished;
    uint16_t chunkIndex = pending->chunkIndex;
    uint16_t chunkCount = pending->chunkCount;
    XS7TcpClient_removePendingFromMap(client, pending);
    pending = NULL;

    if (!ok) {
        /* 解析/数据错误：槽内已 setError 的（如 S7 data 0x05/0x07、负 ACK 类
         * 码）保留精确错误；仅对槽内未置错的静默失败兜底，防误报 InvalidResponse */
        if (XPlcReply_error(reply) == XPlcDevice_NoError) {
            XPlcReply_setError(reply, XPlcDevice_InvalidResponseError, "S7 response parse failed");
        }
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XS7TcpClient_pumpSendQueue(client);
        return;
    }

    /* 按类别推进序列 */
    switch (kind) {
    case XS7Pending_Read:
    case XS7Pending_Write:
    case XS7Pending_Raw:
        if (chunkIndex + 1 >= chunkCount) {
            XS7TcpClient_finishReply(client, reply, kind);
        }
        break;

    case XS7Pending_Control:
    case XS7Pending_BlockList:
        XS7TcpClient_finishReply(client, reply, kind);
        break;

#if XS7_BLOCK_ON
    case XS7Pending_BlockUpStart:
    case XS7Pending_BlockUpData: {
        /* 上传序列：未结束继续 Upload，结束转 EndUpload（EndUpload 应答
         * 无专用解析器，按通用头错误域判定） */
        uint8_t pdu[64];
        uint16_t nextRef = XS7Session_nextPduRef(client->m_session);
        size_t pduLen;
        uint8_t nextKind;
        if (ackFinished) {
            char nameBuf[32];
            const char* name = echoBlockName(reply->m_request, nameBuf, sizeof(nameBuf));
            pduLen = XS7Block_buildEndUpload(pdu, nextRef, name ? name : "");
            nextKind = XS7Pending_BlockUpEnd;
        }
        else {
            pduLen = XS7Block_buildUpload(pdu, nextRef);
            nextKind = XS7Pending_BlockUpData;
        }
        if (pduLen > 0 && pduLen <= XS7_REQUESTED_PDU_LEN) {
            XS7PendingRequest* next = XS7TcpClient_createPending(reply, nextRef, nextKind,
                                                                 (uint16_t)(chunkIndex + 1), 0);
            if (next) {
                next->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
                if (next->frame && XS7TcpClient_enqueuePending(client, next)) {
                    break;
                }
                XS7TcpClient_freePending(next);
            }
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, "failed to build upload sequence step");
        }
        else {
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, "upload sequence step build failed");
        }
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        break;
    }

    case XS7Pending_BlockUpEnd:
        XS7TcpClient_finishReply(client, reply, kind);
        break;

    case XS7Pending_BlockDlStart: {
        /* 下载序列：按片预算切载荷并发出第一片（chunkIndex=分片索引） */
        size_t payloadLen = 0;
        const uint8_t* payload = echoPayload(reply->m_request, &payloadLen);
        uint16_t pieceSize = (uint16_t)XS7Pdu_maxWriteBytes(client->m_negotiatedPduLen);
        if (pieceSize == 0) pieceSize = 1;
        uint16_t pieceCount = (uint16_t)((payloadLen + pieceSize - 1) / pieceSize);
        if (pieceCount == 0) pieceCount = 1;

        size_t offset = 0;
        size_t thisLen = (payloadLen < (size_t)pieceSize) ? payloadLen : (size_t)pieceSize;
        uint8_t* chunk = (uint8_t*)XMemory_malloc(XS7_REQUESTED_PDU_LEN + 32, XCLASS_DEFAULT_MEMORY_TYPE);
        if (payload && chunk) {
            size_t cap = XS7_REQUESTED_PDU_LEN + 32;
            uint16_t nextRef = XS7Session_nextPduRef(client->m_session);
            bool last = (pieceCount == 1);
            size_t pduLen = XS7Block_buildDownload(chunk, nextRef, (uint32_t)offset,
                                                   payload + offset, thisLen, last);
            if (pduLen > 0 && pduLen <= cap) {
                XS7PendingRequest* next = XS7TcpClient_createPending(reply, nextRef,
                                                                     XS7Pending_BlockDlData,
                                                                     1, pieceCount);
                if (next) {
                    next->frame = XS7TcpClient_wrapDtFrame(chunk, pduLen);
                    if (next->frame && XS7TcpClient_enqueuePending(client, next)) {
                        XMemory_free(chunk, XCLASS_DEFAULT_MEMORY_TYPE);
                        break;
                    }
                    XS7TcpClient_freePending(next);
                }
            }
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, "failed to build download data step");
        }
        else {
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, "download payload unavailable");
        }
        if (chunk) XMemory_free(chunk, XCLASS_DEFAULT_MEMORY_TYPE);
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        break;
    }

    case XS7Pending_BlockDlData: {
        /* 下载序列：还有剩余分片则发下一片（chunkIndex+1），否则完成 */
        size_t payloadLen = 0;
        const uint8_t* payload = echoPayload(reply->m_request, &payloadLen);
        uint16_t pieceSize = (uint16_t)XS7Pdu_maxWriteBytes(client->m_negotiatedPduLen);
        if (pieceSize == 0) pieceSize = 1;

        size_t offset = (size_t)(chunkIndex + 1) * pieceSize;
        if (payload && offset < payloadLen) {
            size_t remain = payloadLen - offset;
            size_t thisLen = (remain < (size_t)pieceSize) ? remain : (size_t)pieceSize;
            bool last = (offset + thisLen >= payloadLen);
            uint8_t* chunk = (uint8_t*)XMemory_malloc(XS7_REQUESTED_PDU_LEN + 32, XCLASS_DEFAULT_MEMORY_TYPE);
            if (chunk) {
                uint16_t nextRef = XS7Session_nextPduRef(client->m_session);
                size_t pduLen = XS7Block_buildDownload(chunk, nextRef, (uint32_t)offset,
                                                       payload + offset, thisLen, last);
                bool enqueued = false;
                if (pduLen > 0 && pduLen <= XS7_REQUESTED_PDU_LEN + 32) {
                    XS7PendingRequest* next = XS7TcpClient_createPending(reply, nextRef,
                                                                         XS7Pending_BlockDlData,
                                                                         (uint16_t)(chunkIndex + 1),
                                                                         chunkCount);
                    if (next) {
                        next->frame = XS7TcpClient_wrapDtFrame(chunk, pduLen);
                        if (next->frame && XS7TcpClient_enqueuePending(client, next)) {
                            enqueued = true;
                        }
                        else {
                            XS7TcpClient_freePending(next);
                        }
                    }
                }
                XMemory_free(chunk, XCLASS_DEFAULT_MEMORY_TYPE);
                if (enqueued) break;
            }
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, "failed to build download data step");
        }
        else {
            /* 载荷已发完（防御：无 payload 视为完成） */
            XS7TcpClient_finishReply(client, reply, kind);
            break;
        }
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        break;
    }
#endif /* XS7_BLOCK_ON */

    default:
        XS7TcpClient_finishReply(client, reply, kind);
        break;
    }

    /* 序列推进完毕，发下一片（含刚入队的后续步） */
    XS7TcpClient_pumpSendQueue(client);
}

// =============== 虚函数实现 ===============

/**
 * @brief TSAP 参数计算：本端缺省 0x0100；目标 -1/未配置时按 rack/slot 换算
 */
static void XS7TcpClient_computeTsaps(XS7TcpClient* client, uint16_t* localTsap, uint16_t* remoteTsap)
{
    XPlcDevice* dev = (XPlcDevice*)client;
    uint16_t local = XS7_DEFAULT_LOCAL_TSAP;
    uint16_t remote = 0;

    const XVariant* v = XPlcDevice_connectionParameter_const(dev, XPlcDevice_LocalTsapParameter);
    if (v) local = (uint16_t)XVariant_toInt(v);

    v = XPlcDevice_connectionParameter_const(dev, XPlcDevice_RemoteTsapParameter);
    int remoteParam = v ? XVariant_toInt(v) : -1;
    if (remoteParam >= 0) {
        remote = (uint16_t)remoteParam;
    }
    else {
        int rack = 0;
        int slot = 0;
        v = XPlcDevice_connectionParameter_const(dev, XPlcDevice_RackParameter);
        if (v) rack = XVariant_toInt(v);
        v = XPlcDevice_connectionParameter_const(dev, XPlcDevice_SlotParameter);
        if (v) slot = XVariant_toInt(v);
        remote = XS7Cotp_remoteTsapFromRackSlot(XS7_DEFAULT_CONN_TYPE, (uint8_t)rack, (uint8_t)slot);
    }
    if (localTsap) *localTsap = local;
    if (remoteTsap) *remoteTsap = remote;
}

/**
 * @brief 打开设备：懒建 XTcpSocket、四信号接线、会话参数下发并发起 TCP 连接
 * @return 连接发起成功返回true（可收发需等握手完成 ConnectedState）
 */
static bool VXPlcDevice_open_s7(XPlcDevice* device)
{
    if (!device) return false;

    XS7TcpClient* client = (XS7TcpClient*)device;
    XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
    if (!socket)
    {
        socket = (XAbstractSocket*)XTcpSocket_create();
        if (socket) {
            XObject_setParent((XObject*)socket, (XObject*)client);
            ((XPlcDevice*)client)->m_ioDevice = (XIODevice*)socket;

            // 连接四信号（readyRead/connected/disconnected/errorOccurred）
            XObject_connect_1((XObject*)socket,
                XSignal(XIODevice_readyRead_signal),
                (XObject*)client,
                XS7TcpClient_onReadyRead,
                XConnectionType_Auto);

            XObject_connect_1((XObject*)socket,
                XSignal(XTcpSocket_connected_signal),
                (XObject*)client,
                XS7TcpClient_onConnected,
                XConnectionType_Auto);

            XObject_connect_1((XObject*)socket,
                XSignal(XTcpSocket_disconnected_signal),
                (XObject*)client,
                XS7TcpClient_onDisconnected,
                XConnectionType_Auto);

            XObject_connect_1((XObject*)socket,
                XSignal(XAbstractSocket_errorOccurred_signal),
                (XObject*)client,
                XS7TcpClient_onErrorOccurred,
                XConnectionType_Auto);
        }
    }
    if (!socket) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "TCP socket not available");
        return false;
    }

    // 获取连接参数（IP）
    const char* hostName = NULL;
    char hostNameBuf[256] = { 0 };

    const XVariant* addrVar = XPlcDevice_connectionParameter_const(device, XPlcDevice_NetworkAddressParameter);
    if (addrVar) {
        const XString* str = XVariant_toString_const(addrVar);
        if (str) {
            const char* utf8 = XString_toUtf8(str);
            if (utf8) {
                s7CopyBounded(hostNameBuf, sizeof(hostNameBuf), utf8);
                hostName = hostNameBuf;
            }
        }
    }

    if (!hostName || hostName[0] == '\0') {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "PLC address not configured");
        return false;
    }

    uint16_t port = 102;
    const XVariant* portVar = XPlcDevice_connectionParameter_const(device, XPlcDevice_NetworkPortParameter);
    if (portVar) {
        port = (uint16_t)XVariant_toInt(portVar);
    }

    // 会话参数下发（每次连接前重置，防上次残留帧污染握手）
    if (!client->m_session) {
        client->m_session = XS7Session_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (client->m_session) {
            XObject_setParent((XObject*)client->m_session, (XObject*)client);
        }
    }
    if (client->m_session) {
        uint16_t localTsap = XS7_DEFAULT_LOCAL_TSAP;
        uint16_t remoteTsap = 0;
        XS7TcpClient_computeTsaps(client, &localTsap, &remoteTsap);
        XS7Session_reset(client->m_session);
        XS7Session_setParameters(client->m_session, localTsap, remoteTsap,
                                 (uint16_t)XS7_REQUESTED_PDU_LEN, (uint16_t)XS7_AMQ);
    }

    XPlcDevice_setState(device, XPlcDevice_ConnectingState);
    XPlcDevice_setError(device, XPlcDevice_NoError, NULL);

    XAbstractSocket_connectToHost_base(socket, hostName, port, XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    return true;
}

static void VXPlcDevice_close_s7(XPlcDevice* device)
{
    if (!device) return;

    XS7TcpClient* client = (XS7TcpClient*)device;
    XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);

    /* 先停握手定时器，再复位会话（Idle 标记让 onDisconnected 视为主动关闭，
     * 不触发自动重连） */
    XPlcClient_timeoutTimerStop(&client->m_base);
    if (client->m_session) {
        XS7Session_reset(client->m_session);
    }
    XS7TcpClient_clearAllPendingRequests(client, XPlcDevice_ConnectionError, "Device closed");

    if (socket) {
        XAbstractSocket_disconnectFromHost_base(socket);
    }

    XPlcDevice_setState(device, XPlcDevice_UnconnectedState);
}

/**
 * @brief 通用单 PDU 发送虚槽：封 TPKT/COTP 帧、登记 pending、入串行队列
 * @param payload 完整 S7 PDU（不含 TPKT/COTP；pduRef 须已写入 PDU 头）
 * @param reply 关联回复对象
 * @return 受理（发送或排队）返回true
 * @note 分片读写 API 走同一管线的内部快速路径（带 kind/分片元数据）；
 *       本槽为泛化单 PDU 入口，类别由 PDU 功能码推导：
 *       0x04→Read、0x05→Write、0x28/0x29→Control，其余→Raw 透传
 */
static bool VXPlcClient_sendRequest_s7(XPlcClient* baseClient, const XByteArray* payload, XPlcReply* reply)
{
    if (!baseClient || !payload || !reply) return false;

    XS7TcpClient* client = (XS7TcpClient*)baseClient;
    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return false;
    }

    size_t pduLen = s7ByteArraySize(payload);
    const uint8_t* pduData = s7ByteArrayDataC(payload);
    if (!pduData || pduLen < 11 || pduLen > XS7_REQUESTED_PDU_LEN) return false;

    /* pduRef 取自 S7 头 [4..5]（协议ID[0]/ROSCTR[1]/冗余[2..3] 之后，
     * 与 XS7Pdu_parseHeader 同点）；类别由参数段首字节（Job 头 10 字节后）推导 */
    uint16_t pduRef = s7ReadU16BE(pduData, 4);
    uint8_t funcCode = pduData[10];
    uint8_t kind = XS7Pending_Raw;
    if (funcCode == XS7_FUNC_READ) kind = XS7Pending_Read;
    else if (funcCode == XS7_FUNC_WRITE) kind = XS7Pending_Write;
    else if (funcCode == XS7_FUNC_RUN || funcCode == XS7_FUNC_STOP) kind = XS7Pending_Control;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, kind, 0, 1);
    if (!pending) return false;
    pending->frame = XS7TcpClient_wrapDtFrame(pduData, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        return false;
    }

    XPlcReply_setPduRef(reply, pduRef);
    XPlcReply_setState(reply, XPlcReply_State_Requesting);
    if (!XS7TcpClient_enqueuePending(client, pending)) {
        XS7TcpClient_freePending(pending);
        return false;
    }
    XS7TcpClient_pumpSendQueue(client);
    return true;
}

/**
 * @brief 应答解析虚槽：按 pending 类别解析 PDU 并回填 Reply
 * @return 本片处理成功返回true（末片完成由调用方推进）
 * @note 读写数据错误（0x05/0x06/0x0A）不属于解析失败：置 Reply 错误并返回
 *       false，由调用方中止该 Reply 的剩余分片
 */
static bool VXPlcClient_processResponse_s7(XPlcClient* baseClient, const XByteArray* payload, XPlcReply* reply)
{
    if (!baseClient || !payload || !reply) return false;

    XS7TcpClient* client = (XS7TcpClient*)baseClient;

    size_t len = s7ByteArraySize(payload);
    const uint8_t* s7 = s7ByteArrayDataC(payload);
    if (!s7 || len == 0) return false;

    /* 定位 pending 取类别（调用方 processFrame 保证此时仍在表中） */
    XS7PduHeader header;
    memset(&header, 0, sizeof(header));
    if (!XS7Pdu_parseHeader(s7, len, &header)) return false;

    XS7PendingRequest** slot =
        (XS7PendingRequest**)s7MapValue(client->m_pendingRequests, &header.pduRef);
    XS7PendingRequest* pending = slot ? *slot : NULL;
    uint8_t kind = pending ? pending->kind : XS7Pending_Raw;

    /* Ack/Ack_Data 头错误域：非零即协议层失败（CPU 拒绝该 Job）。
     * 真机实测（第 1 轮，S7-1200）：PLC Stop/Run 被拒时 CPU 回 rosctr=0x02
     * （负 ACK，非 Ack_Data）+ errorClass=0x81 errorCode=0x04 —— 此分支必须
     * 同时覆盖 rosctr=ACK 与 ACK_DATA，否则落入业务解析得到误导性错误码。 */
    if ((header.rosctr == XS7_ROSCTR_ACK_DATA || header.rosctr == XS7_ROSCTR_ACK) &&
        (header.errorClass != 0 || header.errorCode != 0)) {
        char msg[80];
        XString* msgStr = XString_create_fmt_utf8(
            "S7 negative ack: error class=0x%02X code=0x%02X (CPU refused request)",
            (unsigned)header.errorClass, (unsigned)header.errorCode);
        if (msgStr) {
            s7CopyBounded(msg, sizeof(msg), XString_toUtf8(msgStr));
            XString_delete_base((XClass*)msgStr);
        }
        else {
            s7CopyBounded(msg, sizeof(msg), "S7 negative ack (header error)");
        }
        XERROR_PRINTF("[XS7TcpClient] %s ref=%u\n", msg, (unsigned)header.pduRef);
        XPlcReply_setError(reply, XPlcDevice_ProtocolError, msg);
        return false;
    }

    switch (kind) {
    case XS7Pending_Read: {
        XS7ReadItem item;
        memset(&item, 0, sizeof(item));
        if (!XS7Pdu_parseReadAck(s7, len, &item, 1)) {
            return false;
        }
        if (item.returnCode != XS7_RETURN_OK) {
            XERROR_PRINTF("[XS7TcpClient] read item return code 0x%02X ref=%u\n",
                          (unsigned)item.returnCode, (unsigned)header.pduRef);
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, s7ReturnCodeText(item.returnCode));
            return false;
        }
        /* 分片按序完成（AmQ=1 保序）：直接追加拼装 */
        if (item.data && item.dataLen > 0) {
            if (!reply->m_rawResult) {
                reply->m_rawResult = XByteArray_create();
            }
            if (!reply->m_rawResult) return false;
            if (!s7ByteArrayAppend(reply->m_rawResult, item.data, item.dataLen)) {
                return false;
            }
        }
        return true;
    }

    case XS7Pending_Write: {
        uint8_t returnCode = XS7_RETURN_OK;
        if (!XS7Pdu_parseWriteAck(s7, len, 1, &returnCode)) {
            return false;
        }
        if (returnCode != XS7_RETURN_OK) {
            XERROR_PRINTF("[XS7TcpClient] write item return code 0x%02X ref=%u\n",
                          (unsigned)returnCode, (unsigned)header.pduRef);
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, s7ReturnCodeText(returnCode));
            return false;
        }
        return true;
    }

    case XS7Pending_Control: {
#if XS7_CONTROL_ON
        uint8_t status = 0;
        if (!XS7Control_parseAck(s7, len, &status)) {
            /* 诊断转储：控制应答结构非预期时打印前缀字节（真机定位用） */
            char hex[24 * 3 + 1];
            size_t dumpLen = (len < 24) ? len : 24;
            size_t di, pos = 0;
            static const char kHexTab[] = "0123456789ABCDEF";
            for (di = 0; di < dumpLen; ++di) {
                hex[pos++] = kHexTab[(s7[di] >> 4) & 0xF];
                hex[pos++] = kHexTab[s7[di] & 0xF];
                hex[pos++] = ' ';
            }
            hex[pos] = '\0';
            XERROR_PRINTF("[XS7TcpClient] control ack parse failed ref=%u len=%u: %s\n",
                          (unsigned)header.pduRef, (unsigned)len, hex);
            return false;
        }
        if (!XS7Control_statusIsSuccess(status)) {
            XPlcReply_setError(reply, XPlcDevice_ProtocolError,
                               "PLC control request rejected");
            return false;
        }
        /* 已运行/已停止视为成功，写提示防操作者重复触发（设计§7.11） */
        if (status == XS7_CONTROL_STATUS_ALREADY_RUNNING) {
            if (reply->m_errorString) {
                XString_assign_fmt_utf8(reply->m_errorString, "%s", "PLC already running");
            }
            else {
                reply->m_errorString = XString_create_fmt_utf8("%s", "PLC already running");
            }
        }
        else if (status == XS7_CONTROL_STATUS_ALREADY_STOPPED) {
            if (reply->m_errorString) {
                XString_assign_fmt_utf8(reply->m_errorString, "%s", "PLC already stopped");
            }
            else {
                reply->m_errorString = XString_create_fmt_utf8("%s", "PLC already stopped");
            }
        }
        XVariant* result = XVariant_create_int((int)status);
        if (result) {
            XPlcReply_setResult(reply, result);
            XVariant_delete_base((XClass*)result);
        }
        return true;
#else
        /* XS7_CONTROL_ON 裁剪时无 Run/Stop 报文可解析 */
        return false;
#endif /* XS7_CONTROL_ON */
    }

#if XS7_BLOCK_ON
    case XS7Pending_BlockList: {
        XVector* blocks = XVector_Create(XS7BlockInfo);
        if (!blocks) return false;
        if (!XS7Block_parseListBlocksAck(s7, len, blocks)) {
            XVector_delete_base((XClass*)blocks);
            return false;
        }
        /* 结果经 ptr 型 XVariant 随行；向量所有权移交调用方
         *（取用后 XVector_delete_base 释放，见 XS7TcpClient.h 契约） */
        XVariant* result = XVariant_create_ptr(blocks);
        if (!result) {
            XVector_delete_base((XClass*)blocks);
            return false;
        }
        XPlcReply_setResult(reply, result);
        XVariant_delete_base((XClass*)result);
        return true;
    }

    case XS7Pending_BlockUpStart:
    case XS7Pending_BlockUpData: {
        bool finished = false;
        if (!reply->m_rawResult) {
            reply->m_rawResult = XByteArray_create();
        }
        if (!reply->m_rawResult) return false;
        if (!XS7Block_parseUploadAck(s7, len, reply->m_rawResult, &finished)) {
            return false;
        }
        if (pending) pending->ackFinished = finished ? 1 : 0;
        return true;
    }

    case XS7Pending_BlockUpEnd:
    case XS7Pending_BlockDlStart:
    case XS7Pending_BlockDlData:
        return XS7Block_parseDownloadAck(s7, len);
#endif /* XS7_BLOCK_ON */

    case XS7Pending_Raw:
    default:
        /* 原始透传：rawResult = 完整应答 PDU */
        if (!reply->m_rawResult) {
            reply->m_rawResult = XByteArray_create();
        }
        if (!reply->m_rawResult) return false;
        return s7ByteArrayAppend(reply->m_rawResult, s7, len);
    }
}

// =============== 定时器事件处理 ===============
static void VXPlcClient_timerEvent_s7(XObject* obj, XTimerEvent* event)
{
    if (!obj || !event) goto parent_call;

    XEvent_accept((XEvent*)event);
    XS7TcpClient* client = (XS7TcpClient*)obj;
    XTimerId timerId = XTimerEvent_timerId(event);
    XPlcDevice_State state = XPlcDevice_state((XPlcDevice*)client);

    if (state == XPlcDevice_ConnectedState && client->m_timerMap) {
        /* 业务片超时：经反查表 O(1) 定位 pduRef */
        uint16_t* pRef = (uint16_t*)s7MapValue(client->m_timerMap, &timerId);
        if (pRef) {
            uint16_t pduRef = *pRef;
            s7MapRemoveKey(client->m_timerMap, &timerId);
            XS7TcpClient_handleRequestTimeout(client, pduRef);
            return;
        }
    }

    if (state == XPlcDevice_HandshakingState &&
        timerId == client->m_base.m_timeoutTimer) {
        XS7TcpClient_handleHandshakeTimeout(client);
        return;
    }

    if (timerId == client->m_base.m_reconnectTimer) {
        /* 重连定时器只触发一次 */
        XPlcClient_reconnectTimerStop(&client->m_base);
        XS7TcpClient_attemptReconnect(client);
        return;
    }

parent_call:
    XClass_Parent(XPlcClient, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

// =============== 槽函数实现 ===============

/**
 * @brief readyRead：读净字节流整体喂会话，取事件循环驱动握手/业务
 */
static void XS7TcpClient_onReadyRead(XObject* receiver, XVarList* args)
{
    (void)args;
    XS7TcpClient* client = (XS7TcpClient*)receiver;
    if (!client || !client->m_session || !client->m_requestData) return;

    XIODevice* io = ((XPlcDevice*)client)->m_ioDevice;
    if (!io) return;

    /* 读净本次字节（覆盖写，临时借用作scratch）并整体喂入会话分帧 */
    XIODevice_readAll_2(io, client->m_requestData, false);
    size_t len = s7ByteArraySize(client->m_requestData);
    if (len == 0) return;

    if (!XS7Session_feed(client->m_session, s7ByteArrayData(client->m_requestData), len)) {
        return;
    }

    /* 事件循环：CcAccepted → 发 Setup；HandshakeDone → Connected；
     * S7Frame → processFrame；Error → 握手失败/链路协议错误 */
    for (;;) {
        XS7Session_EventType ev = XS7Session_takeEvent(client->m_session);
        if (ev == XS7SessionEv_None) break;
        switch (ev) {
        case XS7SessionEv_CcAccepted:
            if (!XS7TcpClient_startHandshake(client)) {
                XS7TcpClient_handshakeFailed(client, XPlcDevice_ProtocolError,
                                             "failed to send SetupCommunication");
            }
            break;

        case XS7SessionEv_HandshakeDone: {
            uint16_t negotiated = XS7Session_negotiatedPduSize(client->m_session);
            XPlcClient_timeoutTimerStop(&client->m_base);
            if (negotiated < XS7_MIN_VALID_PDU_LEN) {
                XS7TcpClient_handshakeFailed(client, XPlcDevice_ProtocolError,
                                             "negotiated PDU length abnormally small");
                break;
            }
            client->m_negotiatedPduLen = negotiated;
            XPlcDevice_setError((XPlcDevice*)client, XPlcDevice_NoError, NULL);
            XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_ConnectedState);
            XS7TcpClient_pumpSendQueue(client);
            break;
        }

        case XS7SessionEv_S7Frame: {
            size_t frameLen = 0;
            const uint8_t* frame = XS7Session_frame(client->m_session, &frameLen);
            if (frame && frameLen > 0) {
                XS7TcpClient_processFrame(client, frame, frameLen);
            }
            break;
        }

        case XS7SessionEv_Error:
            if (XPlcDevice_state((XPlcDevice*)client) == XPlcDevice_HandshakingState) {
                XS7TcpClient_handshakeFailed(client, XPlcDevice_ProtocolError,
                                             "S7 handshake protocol error");
            }
            else {
                /* 业务期协议/分帧错误：清在途请求并按断线重连处理 */
                XPlcDevice_setError((XPlcDevice*)client, XPlcDevice_ProtocolError,
                                    "S7 session protocol error");
                XS7TcpClient_clearAllPendingRequests(client, XPlcDevice_ProtocolError,
                                                     "S7 session protocol error");
                XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
                if (socket) {
                    XAbstractSocket_disconnectFromHost_base(socket);
                }
            }
            break;

        case XS7SessionEv_None:
        default:
            break;
        }
    }
}

/**
 * @brief connected：TCP 已连 → 进入握手态并发 CR
 */
static void XS7TcpClient_onConnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XS7TcpClient* client = (XS7TcpClient*)receiver;
    if (!client) return;

    // 连接成功，重置重连计数器并停止重连定时器
    XPlcClient* baseClient = &client->m_base;
    baseClient->m_reconnectAttempts = 0;
    XPlcClient_reconnectTimerStop(baseClient);

    XPlcDevice_setError((XPlcDevice*)client, XPlcDevice_NoError, NULL);
    XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_HandshakingState);

    if (!XS7TcpClient_startHandshake(client)) {
        XS7TcpClient_handshakeFailed(client, XPlcDevice_ConnectionError,
                                     "failed to send COTP connection request");
    }
}

/**
 * @brief disconnected：清在途请求；主动关闭不重连，异常断线按需重连
 * @note 主动/异常判据：close() 与握手超时路径已先把会话复位为 Idle
 */
static void XS7TcpClient_onDisconnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XS7TcpClient* client = (XS7TcpClient*)receiver;
    if (!client) return;

    bool deliberate = (client->m_session &&
                       XS7Session_state(client->m_session) == XS7Session_Idle);

    XS7TcpClient_clearAllPendingRequests(client, XPlcDevice_ConnectionError, "Connection lost");
    if (client->m_session) {
        XS7Session_reset(client->m_session);
    }
    client->m_negotiatedPduLen = XS7_FALLBACK_PDU_LEN;

    XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_UnconnectedState);

    if (!deliberate) {
        XS7TcpClient_scheduleReconnectIfAllowed(client);
    }
}

/**
 * @brief errorOccurred：连接/通信层错误上报；未建立期直接走断线收尾
 */
static void XS7TcpClient_onErrorOccurred(XObject* receiver, XVarList* args)
{
    (void)args;
    XS7TcpClient* client = (XS7TcpClient*)receiver;
    if (!client) return;

    XPlcDevice_setError((XPlcDevice*)client, XPlcDevice_ConnectionError, "Socket error");

    XPlcDevice_State state = XPlcDevice_state((XPlcDevice*)client);
    if (state == XPlcDevice_ConnectingState || state == XPlcDevice_HandshakingState) {
        /* 连接未建立/握手中失败：会话复位 + 收尾（disconnected 信号可能不来） */
        XPlcClient_timeoutTimerStop(&client->m_base);
        if (client->m_session) {
            XS7Session_reset(client->m_session);
        }
        XS7TcpClient_scheduleReconnectIfAllowed(client);
        XAbstractSocket* socket = (XAbstractSocket*)XS7TcpClient_socket(client);
        if (socket) {
            XAbstractSocket_disconnectFromHost_base(socket);
        }
        XPlcDevice_setState((XPlcDevice*)client, XPlcDevice_UnconnectedState);
    }
}

// =============== 分片构建辅助（读写 API 共用） ===============
/**
 * @brief 构建一个分片 pending：组 S7 PDU → 封 DT/TPKT 帧
 * @param addr 分片地址（偏移/计数已折算到本片）
 * @param data 写入数据（读分片传NULL）
 * @param bitCount 写入位计数（读分片传0）
 * @return 待入队 pending（失败返回NULL）
 */
static XS7PendingRequest* XS7TcpClient_buildChunkPending(XS7TcpClient* client, XPlcReply* reply,
                                                         uint8_t kind, const XS7Address* addr,
                                                         const uint8_t* data, uint16_t bitCount,
                                                         uint16_t chunkIndex, uint16_t chunkCount)
{
    if (!client || !client->m_session || !reply || !addr) return NULL;

    uint16_t pduRef = XS7Session_nextPduRef(client->m_session);
    uint8_t pdu[XS7_REQUESTED_PDU_LEN];
    size_t pduLen = 0;

    if (kind == XS7Pending_Read) {
        pduLen = XS7Pdu_buildRead(pdu, pduRef, addr, 1);
    }
    else if (kind == XS7Pending_Write) {
        const uint8_t* datas[1];
        uint16_t bitCounts[1];
        datas[0] = data;
        bitCounts[0] = bitCount;
        pduLen = XS7Pdu_buildWrite(pdu, pduRef, addr, 1, datas, bitCounts);
    }
    else {
        return NULL;
    }
    if (pduLen == 0 || pduLen > XS7_REQUESTED_PDU_LEN) return NULL;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, kind,
                                                            chunkIndex, chunkCount);
    if (!pending) return NULL;
    pending->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        return NULL;
    }
    return pending;
}

/**
 * @brief 通用受理收尾：入队全部 pending 后驱动队列；首个未发出则失败收尾
 * @return 全部受理且首个分片已发出（或本就有在途）返回true
 */
static bool XS7TcpClient_commitReply(XS7TcpClient* client, XPlcReply* reply,
                                     XS7PendingRequest** pendings, int count)
{
    if (!client || !reply || !pendings || count <= 0) return false;

    XPlcReply_setState(reply, XPlcReply_State_Requesting);
    for (int i = 0; i < count; i++) {
        if (!pendings[i]) continue;
        if (!XS7TcpClient_enqueuePending(client, pendings[i])) {
            /* 回带 Reply 中止：丢弃本批全部 pending */
            for (int j = i; j < count; j++) {
                if (pendings[j]) XS7TcpClient_freePending(pendings[j]);
            }
            XPlcReply_setError(reply, XPlcDevice_ProtocolError, "failed to queue request");
            return false;
        }
    }
    if (!XS7TcpClient_pumpSendQueue(client)) {
        /* 首片未能发出（队列驱动失败）：清理并判失败 */
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XPlcReply_setError(reply, XPlcDevice_WriteError, "failed to send request");
        return false;
    }
    return true;
}

// =============== 读写 API ===============
XPlcReply* XS7TcpClient_sendRead(XS7TcpClient* client, const XString* address, XS7ValueType type, int count)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!address || count <= 0) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid read arguments");
        return NULL;
    }

    XS7Address addr;
    memset(&addr, 0, sizeof(addr));
    if (!XS7Address_parse(&addr, address)) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid S7 address");
        return NULL;
    }
    addr.type = type;
    addr.count = (uint16_t)count;

    /* 分片预算：读按响应侧字节预算切（设计§3.4） */
    int limit = XS7Pdu_maxReadBytesPerItem(client->m_negotiatedPduLen);
    if (limit <= 0) limit = 1;

    size_t totalBytes = XS7TcpClient_valueTotalBytes(type, count);
    int elemsPerChunk = limit;                       /* Bool: 位/片；Byte: 字节/片 */
    if (type == XS7Value_Word)  elemsPerChunk = (limit >= 2) ? limit / 2 : 1;
    if (type == XS7Value_DWord) elemsPerChunk = (limit >= 4) ? limit / 4 : 1;
    if (type == XS7Value_Real)  elemsPerChunk = (limit >= 4) ? limit / 4 : 1;
    if (type == XS7Value_String && elemsPerChunk < 3) {
        /* STRING 首片必须容得下 [max][cur] 两字节头 + ≥1 数据字节 */
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "pdu too small for string read");
        return NULL;
    }
    /* String 的 totalElems/elemsPerChunk 均为线上字节口径（n+2 结构线性切） */
    int totalElems = (type == XS7Value_String) ? (int)totalBytes : count;
    int chunkCount = (totalElems + elemsPerChunk - 1) / elemsPerChunk;
    if (chunkCount < 1) chunkCount = 1;
    size_t elemBytes = XS7TcpClient_elemStepBytes(type);

    /* 回带：kind + 类型/个数/基址 + 总字节 + 分片数 */
    uint8_t echo[XS7_ECHO_HEADER_SIZE];
    XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_READ, type, count, &addr,
                                 (uint32_t)totalBytes, (uint16_t)chunkCount, echo);
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    /* 逐片构建（首片 ref 回填 Reply 供外部定位） */
    XS7PendingRequest** pendings =
        (XS7PendingRequest**)XMemory_malloc(sizeof(XS7PendingRequest*) * (size_t)chunkCount,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!pendings) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    memset(pendings, 0, sizeof(XS7PendingRequest*) * (size_t)chunkCount);

    bool buildOk = true;
    for (int i = 0; i < chunkCount && buildOk; i++) {
        int chunkElems = totalElems - i * elemsPerChunk;
        if (chunkElems > elemsPerChunk) chunkElems = elemsPerChunk;
        size_t chunkByteOffset = (size_t)i * (size_t)elemsPerChunk * elemBytes;

        /* String 续片为裸数据字节（[max][cur] 头只在首片），按 Byte 传输
         * 组 ANY 项（count=裸字节数，xs7PduItemTransport 不再加 +2）；
         * 拼装完成后仍按 String 整体解码，回带类型不受影响 */
        XS7ValueType chunkType = type;
        if (type == XS7Value_String && i > 0) chunkType = XS7Value_Byte;

        XS7Address chunkAddr;
        if (!XS7TcpClient_buildChunkAddress(&addr, chunkType, chunkByteOffset, (size_t)chunkElems, &chunkAddr)) {
            buildOk = false;
            break;
        }
        pendings[i] = XS7TcpClient_buildChunkPending(client, reply, XS7Pending_Read,
                                                     &chunkAddr, NULL, 0,
                                                     (uint16_t)i, (uint16_t)chunkCount);
        if (!pendings[i]) buildOk = false;
    }
    if (!buildOk) {
        for (int i = 0; i < chunkCount; i++) {
            if (pendings[i]) XS7TcpClient_freePending(pendings[i]);
        }
        XMemory_free(pendings, XCLASS_DEFAULT_MEMORY_TYPE);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (pendings[0]) {
        XPlcReply_setPduRef(reply, pendings[0]->pduRef);
    }

    bool committed = XS7TcpClient_commitReply(client, reply, pendings, chunkCount);
    XMemory_free(pendings, XCLASS_DEFAULT_MEMORY_TYPE);
    if (!committed) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}

XPlcReply* XS7TcpClient_sendRead_2(XS7TcpClient* client, const char* addressUtf8, XS7ValueType type, int count)
{
    if (!client || !addressUtf8) return NULL;
    XString* address = XString_create_utf8(addressUtf8);
    if (!address) return NULL;
    XPlcReply* reply = XS7TcpClient_sendRead(client, address, type, count);
    XString_delete_base((XClass*)address);
    return reply;
}

XPlcReply* XS7TcpClient_sendWrite(XS7TcpClient* client, const XString* address, XS7ValueType type,
                                  int count, const XVariant* value)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!address || !value || count <= 0) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid write arguments");
        return NULL;
    }

    XS7Address addr;
    memset(&addr, 0, sizeof(addr));
    if (!XS7Address_parse(&addr, address)) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid S7 address");
        return NULL;
    }
    addr.type = type;
    addr.count = (uint16_t)count;

    /* 值编码（大端字节；Bool 位写为打包位，见 XS7Value 契约） */
    XByteArray* encoded = XByteArray_create();
    if (!encoded) return NULL;
    if (!XS7Value_encode(type, count, value, encoded) || s7ByteArraySize(encoded) == 0) {
        s7ByteArrayDelete(encoded);
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "value encode failed");
        return NULL;
    }
    size_t totalBytes = s7ByteArraySize(encoded);
    const uint8_t* encodedData = s7ByteArrayDataC(encoded);

    /* 分片预算：写按请求侧字节预算切（设计§3.4）；Bool 位写整单单片 */
    int limit = XS7Pdu_maxWriteBytes(client->m_negotiatedPduLen);
    if (limit <= 0) limit = 1;

    int elemsPerChunk = limit;
    if (type == XS7Value_Word)  elemsPerChunk = (limit >= 2) ? limit / 2 : 1;
    if (type == XS7Value_DWord || type == XS7Value_Real) elemsPerChunk = (limit >= 4) ? limit / 4 : 1;
    if (type == XS7Value_Bool || type == XS7Value_String) {
        elemsPerChunk = (int)totalBytes;   /* 位写/串结构整写，不跨片 */
    }
    if (elemsPerChunk < 1) elemsPerChunk = 1;

    size_t elemStep = XS7TcpClient_elemStepBytes(type);
    int totalElems = (type == XS7Value_Bool)
        ? count
        : (int)((totalBytes + elemStep - 1) / elemStep);
    int chunkCount = (totalElems + elemsPerChunk - 1) / elemsPerChunk;
    if (chunkCount < 1) chunkCount = 1;
    size_t elemBytes = elemStep;

    /* 回带：kind + 类型/个数/基址 + 编码值（分片推进时随行取数） */
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    {
        uint8_t echo[XS7_ECHO_HEADER_SIZE];
        XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_WRITE, type, count, &addr,
                                     (uint32_t)totalBytes, (uint16_t)chunkCount, echo);
        s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);
        s7ByteArrayAppend(&echoBuf, encodedData, totalBytes);
    }

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) {
        s7ByteArrayDelete(encoded);
        return NULL;
    }

    XS7PendingRequest** pendings =
        (XS7PendingRequest**)XMemory_malloc(sizeof(XS7PendingRequest*) * (size_t)chunkCount,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!pendings) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    memset(pendings, 0, sizeof(XS7PendingRequest*) * (size_t)chunkCount);

    bool buildOk = true;
    for (int i = 0; i < chunkCount && buildOk; i++) {
        int chunkElems = totalElems - i * elemsPerChunk;
        if (chunkElems > elemsPerChunk) chunkElems = elemsPerChunk;
        size_t chunkByteOffset = (size_t)i * (size_t)elemsPerChunk * elemBytes;
        if (chunkByteOffset >= totalBytes) { buildOk = false; break; }
        /* 本片数据字节数按 元素数×单位字节 推导：elemsPerChunk 以元素计，
         * 直接拿它截断字节数会让 item ANY 宣称的字节数与数据区实际携带
         * 不一致、尾部数据被静默丢弃（Word/DWord/Real 多元素写） */
        size_t thisBytes = (size_t)chunkElems * elemBytes;
        uint16_t bitCount = (type == XS7Value_Bool)
            ? (uint16_t)count
            : (uint16_t)(thisBytes * 8);

        XS7Address chunkAddr;
        if (!XS7TcpClient_buildChunkAddress(&addr, type, chunkByteOffset,
                                            (size_t)chunkElems, &chunkAddr)) {
            buildOk = false;
            break;
        }
        pendings[i] = XS7TcpClient_buildChunkPending(client, reply, XS7Pending_Write,
                                                     &chunkAddr, encodedData + chunkByteOffset,
                                                     bitCount, (uint16_t)i, (uint16_t)chunkCount);
        if (!pendings[i]) buildOk = false;
    }
    if (!buildOk) {
        for (int i = 0; i < chunkCount; i++) {
            if (pendings[i]) XS7TcpClient_freePending(pendings[i]);
        }
        XMemory_free(pendings, XCLASS_DEFAULT_MEMORY_TYPE);
        s7ByteArrayDelete(encoded);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (pendings[0]) {
        XPlcReply_setPduRef(reply, pendings[0]->pduRef);
    }

    bool committed = XS7TcpClient_commitReply(client, reply, pendings, chunkCount);
    XMemory_free(pendings, XCLASS_DEFAULT_MEMORY_TYPE);
    /* 编码缓冲在全部 pending 组帧完成后才可释放：encodedData 指针被
     * buildChunkPending→XS7Pdu_buildWrite 同步拷贝进 PDU（先删后用会把
     * 释放堆填充当数据发出） */
    s7ByteArrayDelete(encoded);
    if (!committed) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}

XPlcReply* XS7TcpClient_sendWrite_2(XS7TcpClient* client, const char* addressUtf8, XS7ValueType type,
                                    int count, const XVariant* value)
{
    if (!client || !addressUtf8) return NULL;
    XString* address = XString_create_utf8(addressUtf8);
    if (!address) return NULL;
    XPlcReply* reply = XS7TcpClient_sendWrite(client, address, type, count, value);
    XString_delete_base((XClass*)address);
    return reply;
}

XPlcReply* XS7TcpClient_sendReadRaw(XS7TcpClient* client, const XString* address, int byteCount)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!address || byteCount <= 0) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid raw read arguments");
        return NULL;
    }

    XS7Address addr;
    memset(&addr, 0, sizeof(addr));
    if (!XS7Address_parse(&addr, address)) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid S7 address");
        return NULL;
    }
    addr.type = XS7Value_Byte;
    addr.count = (uint16_t)byteCount;

    int limit = XS7Pdu_maxReadBytesPerItem(client->m_negotiatedPduLen);
    if (limit <= 0) limit = 1;
    int chunkCount = (byteCount + limit - 1) / limit;
    if (chunkCount < 1) chunkCount = 1;

    uint8_t echo[XS7_ECHO_HEADER_SIZE];
    XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_RAW, XS7Value_Byte, byteCount, &addr,
                                 (uint32_t)byteCount, (uint16_t)chunkCount, echo);
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);

    /* Raw 语义：结果只落 rawResult（完整拼接字节块），不做类型解码 */
    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Raw);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    XS7PendingRequest** pendings =
        (XS7PendingRequest**)XMemory_malloc(sizeof(XS7PendingRequest*) * (size_t)chunkCount,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!pendings) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    memset(pendings, 0, sizeof(XS7PendingRequest*) * (size_t)chunkCount);

    bool buildOk = true;
    for (int i = 0; i < chunkCount && buildOk; i++) {
        size_t offset = (size_t)i * (size_t)limit;
        int thisLen = byteCount - (int)offset;
        if (thisLen > limit) thisLen = limit;

        XS7Address chunkAddr;
        if (!XS7TcpClient_buildChunkAddress(&addr, XS7Value_Byte, offset,
                                            (size_t)thisLen, &chunkAddr)) {
            buildOk = false;
            break;
        }
        pendings[i] = XS7TcpClient_buildChunkPending(client, reply, XS7Pending_Read,
                                                     &chunkAddr, NULL, 0,
                                                     (uint16_t)i, (uint16_t)chunkCount);
        if (!pendings[i]) buildOk = false;
    }
    if (!buildOk) {
        for (int i = 0; i < chunkCount; i++) {
            if (pendings[i]) XS7TcpClient_freePending(pendings[i]);
        }
        XMemory_free(pendings, XCLASS_DEFAULT_MEMORY_TYPE);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (pendings[0]) {
        XPlcReply_setPduRef(reply, pendings[0]->pduRef);
    }

    bool committed = XS7TcpClient_commitReply(client, reply, pendings, chunkCount);
    XMemory_free(pendings, XCLASS_DEFAULT_MEMORY_TYPE);
    if (!committed) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}

// =============== 运维 API（XS7_CONTROL_ON 门控） ===============
#if XS7_CONTROL_ON
XPlcReply* XS7TcpClient_sendPlcRun(XS7TcpClient* client, XS7RunMode mode)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!client->m_session) return NULL;

    uint8_t pdu[64];
    uint16_t pduRef = XS7Session_nextPduRef(client->m_session);
    size_t pduLen = XS7Control_buildRun(pdu, pduRef, mode);
    if (pduLen == 0 || pduLen > sizeof(pdu)) {
        XPlcDevice_setError(device, XPlcDevice_ProtocolError, "build run request failed");
        return NULL;
    }

    /* 回带仅记类别（模式经 PDU 功能码/参数已随行） */
    uint8_t echo[XS7_ECHO_HEADER_SIZE];
    XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_CONTROL, XS7Value_Byte, 1, NULL, (uint32_t)pduLen, 1, echo);
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, XS7Pending_Control, 0, 1);
    if (!pending) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    pending->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    XPlcReply_setPduRef(reply, pduRef);
    XPlcReply_setState(reply, XPlcReply_State_Requesting);

    if (!XS7TcpClient_enqueuePending(client, pending)) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (!XS7TcpClient_pumpSendQueue(client)) {
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}

XPlcReply* XS7TcpClient_sendPlcStop(XS7TcpClient* client)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!client->m_session) return NULL;

    uint8_t pdu[64];
    uint16_t pduRef = XS7Session_nextPduRef(client->m_session);
    size_t pduLen = XS7Control_buildStop(pdu, pduRef);
    if (pduLen == 0 || pduLen > sizeof(pdu)) {
        XPlcDevice_setError(device, XPlcDevice_ProtocolError, "build stop request failed");
        return NULL;
    }

    uint8_t echo[XS7_ECHO_HEADER_SIZE];
    XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_CONTROL, XS7Value_Byte, 1, NULL, (uint32_t)pduLen, 1, echo);
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, XS7Pending_Control, 0, 1);
    if (!pending) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    pending->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    XPlcReply_setPduRef(reply, pduRef);
    XPlcReply_setState(reply, XPlcReply_State_Requesting);

    if (!XS7TcpClient_enqueuePending(client, pending)) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (!XS7TcpClient_pumpSendQueue(client)) {
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}
#endif /* XS7_CONTROL_ON */

// =============== 块传输 API（XS7_BLOCK_ON 门控） ===============
#if XS7_BLOCK_ON
XPlcReply* XS7TcpClient_sendListBlocks(XS7TcpClient* client)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!client->m_session) return NULL;

    uint8_t pdu[64];
    uint16_t pduRef = XS7Session_nextPduRef(client->m_session);
    size_t pduLen = XS7Block_buildListBlocks(pdu, pduRef);
    if (pduLen == 0 || pduLen > sizeof(pdu)) {
        XPlcDevice_setError(device, XPlcDevice_ProtocolError, "build list-blocks request failed");
        return NULL;
    }

    uint8_t echo[XS7_ECHO_HEADER_SIZE];
    XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_BLOCK_LIST, XS7Value_Byte, 1, NULL, (uint32_t)pduLen, 1, echo);
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, XS7Pending_BlockList, 0, 1);
    if (!pending) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    pending->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    XPlcReply_setPduRef(reply, pduRef);
    XPlcReply_setState(reply, XPlcReply_State_Requesting);

    if (!XS7TcpClient_enqueuePending(client, pending)) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (!XS7TcpClient_pumpSendQueue(client)) {
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}

XPlcReply* XS7TcpClient_sendUploadBlock(XS7TcpClient* client, const XString* blockName)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!blockName || !client->m_session) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid upload arguments");
        return NULL;
    }

    XString* nameStr = XString_create_copy(blockName);
    if (!nameStr) return NULL;
    const char* name = XString_toUtf8(nameStr);
    char nameBuf[32] = { 0 };
    if (name) {
        s7CopyBounded(nameBuf, sizeof(nameBuf), name);
    }
    XString_delete_base((XClass*)nameStr);
    if (nameBuf[0] == '\0') {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid block name");
        return NULL;
    }

    uint8_t pdu[64];
    uint16_t pduRef = XS7Session_nextPduRef(client->m_session);
    size_t pduLen = XS7Block_buildStartUpload(pdu, pduRef, nameBuf);
    if (pduLen == 0 || pduLen > sizeof(pdu)) {
        XPlcDevice_setError(device, XPlcDevice_ProtocolError, "build start-upload request failed");
        return NULL;
    }

    /* 回带携带块名（EndUpload 步需复用） */
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    {
        uint8_t echo[XS7_ECHO_HEADER_SIZE];
        XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_BLOCK_UP, XS7Value_Byte, 1, NULL,
                                     0, 0, echo);
        echo[XS7_ECHO_OFF_NAMELEN] = (uint8_t)s7CStrLen(nameBuf);
        s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);
        s7ByteArrayAppend(&echoBuf, nameBuf, s7CStrLen(nameBuf));
    }

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, XS7Pending_BlockUpStart, 0, 0);
    if (!pending) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    pending->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    XPlcReply_setPduRef(reply, pduRef);
    XPlcReply_setState(reply, XPlcReply_State_Requesting);

    if (!XS7TcpClient_enqueuePending(client, pending)) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (!XS7TcpClient_pumpSendQueue(client)) {
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}

XPlcReply* XS7TcpClient_sendDownloadBlock(XS7TcpClient* client, const XString* blockName,
                                          const XByteArray* payload)
{
    if (!client) return NULL;

    XPlcDevice* device = (XPlcDevice*)client;
    if (XPlcDevice_state(device) != XPlcDevice_ConnectedState) {
        XPlcDevice_setError(device, XPlcDevice_ConnectionError, "Not connected");
        return NULL;
    }
    if (!blockName || !payload || s7ByteArraySize(payload) == 0 || !client->m_session) {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid download arguments");
        return NULL;
    }

    XString* nameStr = XString_create_copy(blockName);
    if (!nameStr) return NULL;
    const char* name = XString_toUtf8(nameStr);
    char nameBuf[32] = { 0 };
    if (name) {
        s7CopyBounded(nameBuf, sizeof(nameBuf), name);
    }
    XString_delete_base((XClass*)nameStr);
    if (nameBuf[0] == '\0') {
        XPlcDevice_setError(device, XPlcDevice_ConfigurationError, "invalid block name");
        return NULL;
    }

    size_t payloadLen = s7ByteArraySize(payload);
    const uint8_t* payloadData = s7ByteArrayDataC(payload);

    uint8_t pdu[64];
    uint16_t pduRef = XS7Session_nextPduRef(client->m_session);
    size_t pduLen = XS7Block_buildRequestDownload(pdu, pduRef, nameBuf, (uint32_t)payloadLen);
    if (pduLen == 0 || pduLen > sizeof(pdu)) {
        XPlcDevice_setError(device, XPlcDevice_ProtocolError, "build request-download failed");
        return NULL;
    }

    /* 回带携带块名 + 块字节（各下载步随行取数） */
    XByteArray echoBuf;
    XByteArray_init(&echoBuf, true);
    {
        uint8_t echo[XS7_ECHO_HEADER_SIZE];
        XS7TcpClient_buildEchoHeader(XS7_ECHO_KIND_BLOCK_DL, XS7Value_Byte, 1, NULL,
                                     (uint32_t)payloadLen, 0, echo);
        echo[XS7_ECHO_OFF_NAMELEN] = (uint8_t)s7CStrLen(nameBuf);
        s7ByteArrayAppend(&echoBuf, echo, XS7_ECHO_HEADER_SIZE);
        s7ByteArrayAppend(&echoBuf, nameBuf, s7CStrLen(nameBuf));
        s7ByteArrayAppend(&echoBuf, payloadData, payloadLen);
    }

    XPlcReply* reply = XPlcClient_createReply(&client->m_base, &echoBuf, XPlcReply_Common);
    XClass_deinit_base((XClass*)&echoBuf);
    if (!reply) return NULL;

    XS7PendingRequest* pending = XS7TcpClient_createPending(reply, pduRef, XS7Pending_BlockDlStart, 0, 0);
    if (!pending) {
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    pending->frame = XS7TcpClient_wrapDtFrame(pdu, pduLen);
    if (!pending->frame) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    XPlcReply_setPduRef(reply, pduRef);
    XPlcReply_setState(reply, XPlcReply_State_Requesting);

    if (!XS7TcpClient_enqueuePending(client, pending)) {
        XS7TcpClient_freePending(pending);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    if (!XS7TcpClient_pumpSendQueue(client)) {
        XS7TcpClient_abortReplyQueuedChunks(client, reply);
        XPlcReply_deleteLater((XObject*)reply);
        return NULL;
    }
    return reply;
}
#endif /* XS7_BLOCK_ON */

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
