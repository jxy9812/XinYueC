/**
 * @file       XGuiRemoteProto.c
 * @brief      XGuiRemote 帧协议/会话层实现(小端序列化 + 20 条消息编解码 +
 *             横幅/组帧 + 增量帧泵 + 档位参数)。
 * @details    契约见冻结头 XGuiRemoteProto.h, 协议规范见仓库根 XGuiRemote.md
 *             §3/§5。实现纪律:
 *               - 全部函数为纯函数, 无全局可变状态, 线程安全;
 *               - 线上多字节整数一律小端, 经 put/get 显式序列化;
 *               - enc 缓冲不足返回 0 不半写; dec 逐字段核对剩余空间,
 *                 负载必须被完整消费(长度不符=协议错误);
 *               - 封闭枚举(format/codec/action/mode/method/profileId)值域
 *                 严格校验, 开放字段(flags/保留位/BYE reason/ERROR code)
 *                 不校验(XGuiRemote.md §3.4 冻结口径)。
 * @author     XinYueC 团队
 */
#include "XGuiRemoteProto.h"
#if XGUI_REMOTE_ON

#include "XIODevice.h"
#include "XMemory.h"
#include <string.h>

/* ==================== 小端序列化辅助 ==================== */

void XGuiRemoteProto_putU16(uint8_t* out, uint16_t v)
{
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
}

void XGuiRemoteProto_putU32(uint8_t* out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
    out[2] = (uint8_t)((v >> 16) & 0xFFu);
    out[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void XGuiRemoteProto_putU64(uint8_t* out, uint64_t v)
{
    XGuiRemoteProto_putU32(out, (uint32_t)(v & 0xFFFFFFFFu));
    XGuiRemoteProto_putU32(out + 4, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

uint16_t XGuiRemoteProto_getU16(const uint8_t* in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

uint32_t XGuiRemoteProto_getU32(const uint8_t* in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

uint64_t XGuiRemoteProto_getU64(const uint8_t* in)
{
    return (uint64_t)XGuiRemoteProto_getU32(in) |
           ((uint64_t)XGuiRemoteProto_getU32(in + 4) << 32);
}

/* ==================== 横幅与帧基元 ==================== */

void XGuiRemoteProto_makeBanner(uint8_t* out)
{
    if (!out) return;
    out[0] = (uint8_t)'X';
    out[1] = (uint8_t)'G';
    out[2] = (uint8_t)'R';
    out[3] = (uint8_t)'1';
    XGuiRemoteProto_putU16(out + 4, XGUI_REMOTE_PROTOCOL_VERSION);
    XGuiRemoteProto_putU16(out + 6, 0);
}

bool XGuiRemoteProto_bannerIsValid(const uint8_t* bytes)
{
    /* 冻结口径: memcmp 字节串比较(XGuiRemote.md §3.3), 不得用整数比较。 */
    if (!bytes) return false;
    return memcmp(bytes, "XGR1", 4) == 0;
}

uint16_t XGuiRemoteProto_bannerVersion(const uint8_t* bytes)
{
    if (!bytes) return 0;
    return XGuiRemoteProto_getU16(bytes + 4);
}

int64_t XGuiRemoteProto_writeFrame(XIODevice* device, XGuiRemoteMsgType type,
                                   const void* payload, size_t payloadBytes)
{
    if (!device) return -1;
    if (!payload && payloadBytes != 0) return -1;
    if (payloadBytes > (size_t)XGUI_REMOTE_MAX_FRAME_BYTES) return -1;

    /* 组整帧后单次写尝试; 不内部循环补写——续传由会话层帧尾待写缓冲
     * 负责(XGuiRemote.md §6.4)。 */
    size_t total = (size_t)XGUI_REMOTE_FRAME_HEADER_BYTES + payloadBytes;
    uint8_t* frame = (uint8_t*)XMemory_malloc(total, XMEMORY_TYPE_SYSTEM);
    if (!frame) return -1;

    XGuiRemoteProto_putU32(frame, (uint32_t)payloadBytes);
    frame[4] = (uint8_t)type;
    if (payloadBytes != 0) {
        memcpy(frame + XGUI_REMOTE_FRAME_HEADER_BYTES, payload, payloadBytes);
    }

    int64_t accepted = XIODevice_write_1(device, (const char*)frame,
                                         (int64_t)total);
    XMemory_free(frame, XMEMORY_TYPE_SYSTEM);

    if (accepted < 0) return -1; /* 设备硬错误; 短写(≥0)是背压, 原样返回 */
    return accepted;
}

/* ==================== 增量帧泵(读侧状态机) ==================== */
/*
 * 实现说明(字段复用): 冻结结构体字段在内部用作"接收字节队列"——
 *   payload[0..payloadCap) 存储区(恒为分配基址, deinit/reserve 据此
 *   释放/搬移); payloadGot=队尾写游标; headerGot=队头解析游标
 *   (≥0; -1 为粘性协议错误标记); payloadLen=当前已解析帧的目标负载长。
 * 每次带新数据的 feed 先把队头已消费部分压实(memmove)再追加——压实只
 * 发生在新数据到来时; feed(NULL,0) 只解析不压实, 可连续取走同批粘包帧。
 * 完成一帧时把负载内容拷贝到缓冲基址交付(reader->payload 恒=基址,
 * 兼作帧负载指针)——拷贝目标区 [0,len) 恒在已消费区间内, 不触碰未解析
 * 字节; 上一帧交付内容按冻结口径"有效至下一次 feed 返回 >0 或 deinit"。
 * headerBuf 为冻结字段, 队列实现下保留清零(解析的"帧头态"由
 * headerGot 游标承担)。
 */

void XGuiRemoteFrameReader_init(XGuiRemoteFrameReader* reader)
{
    if (!reader) return;
    memset(reader, 0, sizeof(*reader));
}

void XGuiRemoteFrameReader_deinit(XGuiRemoteFrameReader* reader)
{
    if (!reader) return;
    if (reader->payload) {
        XMemory_free(reader->payload, XMEMORY_TYPE_SYSTEM);
        reader->payload = NULL;
    }
    reader->payloadCap = 0;
    reader->payloadGot = 0;
    reader->payloadLen = 0;
    reader->headerGot = 0;
    reader->type = (XGuiRemoteMsgType)0;
}

/* 队列存储区绝对上限: 合法流的队列占用 = 一个未完帧(≤硬帽+5) + 一圈
 * 喂入量; 超过该上限的喂入视同协议错误拒绝, 防御长度字段伪造的内存放大。 */
#define FRAME_READER_QUEUE_CAP_MAX \
    ((size_t)XGUI_REMOTE_MAX_FRAME_BYTES * 2u + 64u * 1024u)

/* 扩容队列存储区: 成功返回 true; 超绝对上限拒绝。注意: 不为"已解析的
 * 目标帧长"预分配, 只随实际到包字节增长——伪造的大 len 不会引发分配。 */
static bool frameReaderReserve(XGuiRemoteFrameReader* reader, size_t newCap)
{
    if (newCap <= reader->payloadCap) return true;
    if (newCap > FRAME_READER_QUEUE_CAP_MAX) return false;
    size_t grown = reader->payloadCap ? reader->payloadCap : 64;
    while (grown < newCap) {
        if (grown > FRAME_READER_QUEUE_CAP_MAX / 2u) { grown = newCap; break; }
        grown *= 2;
    }
    uint8_t* p = (uint8_t*)XMemory_malloc(grown, XMEMORY_TYPE_SYSTEM);
    if (!p) return false;
    if (reader->payload) {
        memcpy(p, reader->payload, reader->payloadGot);
        XMemory_free(reader->payload, XMEMORY_TYPE_SYSTEM);
    }
    reader->payload = p;
    reader->payloadCap = grown;
    return true;
}

int XGuiRemoteFrameReader_feed(XGuiRemoteFrameReader* reader,
                               const uint8_t* data, size_t dataBytes)
{
    if (!reader) return -1;
    if (reader->headerGot < 0) return -1; /* 粘性协议错误: 必须断链重建 */

    if (data && dataBytes > 0) {
        if (dataBytes > FRAME_READER_QUEUE_CAP_MAX) {
            reader->headerGot = -1; /* 单圈喂入超绝对上限 */
            return -1;
        }
        /* 压实: 丢弃已消费前缀(此动作使上一帧 payload 借用指针失效,
         * 契约允许——"喂入新数据"即失效点)。 */
        if (reader->headerGot > 0) {
            memmove(reader->payload, reader->payload + reader->headerGot,
                    reader->payloadGot - reader->headerGot);
            reader->payloadGot -= reader->headerGot;
            reader->headerGot = 0;
        }
        if (dataBytes > reader->payloadCap - reader->payloadGot) {
            if (!frameReaderReserve(reader, reader->payloadGot + dataBytes)) {
                reader->headerGot = -1;
                return -1;
            }
        }
        memcpy(reader->payload + reader->payloadGot, data, dataBytes);
        reader->payloadGot += dataBytes;
    }

    /* 解析循环: 帧头态(5 字节)→负载态(按 payloadLen 凑满)。 */
    for (;;) {
        size_t queued = reader->payloadGot - reader->headerGot;
        if (queued < (size_t)XGUI_REMOTE_FRAME_HEADER_BYTES) return 0;

        const uint8_t* head = reader->payload + reader->headerGot;
        uint32_t payloadLen = XGuiRemoteProto_getU32(head);
        if (payloadLen > (uint32_t)XGUI_REMOTE_MAX_FRAME_BYTES) {
            reader->headerGot = -1; /* 协议错误: 会话必须断链 */
            return -1;
        }
        if (queued - (size_t)XGUI_REMOTE_FRAME_HEADER_BYTES < (size_t)payloadLen) {
            reader->payloadLen = (size_t)payloadLen; /* 记录目标长, 继续凑 */
            return 0;
        }

        /* 完成一帧: 负载内容拷到缓冲基址交付(reader->payload 恒=基址,
         * 兼作帧负载借用指针, 有效至下次 feed 返回 >0 或 deinit);
         * 拷贝目标 [0,len) 恒在已消费区间, 不触碰未解析字节。 */
        reader->type = (XGuiRemoteMsgType)head[4];
        memmove(reader->payload, head + XGUI_REMOTE_FRAME_HEADER_BYTES,
                (size_t)payloadLen);
        reader->payloadLen = (size_t)payloadLen;
        reader->headerGot +=
            (int)(XGUI_REMOTE_FRAME_HEADER_BYTES + (size_t)payloadLen);
        return 1;
    }
}

/* ==================== 档位参数 ==================== */

/* 夹取范围(sanitize 即定义, 见 XGuiRemote.md §5.2 两档参数表量级)。 */
#define PROFILE_QUEUE_BYTES_MIN   (64u * 1024u)
#define PROFILE_QUEUE_BYTES_MAX   (256u * 1024u * 1024u)
#define PROFILE_TX_BYTES_MIN      (4u * 1024u)
#define PROFILE_TX_BYTES_MAX      ((size_t)XGUI_REMOTE_MAX_FRAME_BYTES)
#define PROFILE_THROTTLE_MS_MAX   1000u
#define PROFILE_PING_MS_MIN       100u
#define PROFILE_PING_MS_MAX       (60u * 60u * 1000u)

static bool profileFormatValid(int format)
{
    return format == XGUI_REMOTE_PF_ARGB32 || format == XGUI_REMOTE_PF_RGB565;
}

static bool profileCodecValid(int codec)
{
    return codec == XGUI_REMOTE_CODEC_RAW || codec == XGUI_REMOTE_CODEC_RLE ||
           codec == XGUI_REMOTE_CODEC_ZLIB;
}

static bool profileInRange(const XGuiRemoteProfile* p)
{
    if (!profileFormatValid((int)p->wireFormat)) return false;
    if (!profileCodecValid((int)p->codec)) return false;
    if (p->codec == XGUI_REMOTE_CODEC_ZLIB &&
        (p->zlibLevel < 1 || p->zlibLevel > 9)) return false;
    if (p->tileWidth < 16 || p->tileWidth > 512) return false;
    if (p->tileHeight < 16 || p->tileHeight > 512) return false;
    if (p->maxFps < 1 || p->maxFps > 240) return false;
    if (p->encodeQueueBytes < PROFILE_QUEUE_BYTES_MIN ||
        p->encodeQueueBytes > PROFILE_QUEUE_BYTES_MAX) return false;
    if (p->txBudgetBytes < PROFILE_TX_BYTES_MIN ||
        p->txBudgetBytes > PROFILE_TX_BYTES_MAX) return false;
    if (p->mouseMoveThrottleMs > PROFILE_THROTTLE_MS_MAX) return false;
    if (p->pingIntervalMs < PROFILE_PING_MS_MIN ||
        p->pingIntervalMs > PROFILE_PING_MS_MAX) return false;
    if (p->pingTimeoutMs < PROFILE_PING_MS_MIN ||
        p->pingTimeoutMs > PROFILE_PING_MS_MAX) return false;
    return true;
}

static void profileInit(XGuiRemoteProfile* out, XGuiRemotePixelFormat format,
                        XGuiRemoteCodecId codec, int zlibLevel,
                        int tileW, int tileH, int maxFps,
                        size_t queueBytes, size_t txBytes,
                        uint32_t throttleMs, uint32_t pingMs, uint32_t timeoutMs)
{
    if (!out) return;
    out->wireFormat = format;
    out->codec = codec;
    out->zlibLevel = zlibLevel;
    out->tileWidth = tileW;
    out->tileHeight = tileH;
    out->maxFps = maxFps;
    out->encodeQueueBytes = queueBytes;
    out->txBudgetBytes = txBytes;
    out->mouseMoveThrottleMs = throttleMs;
    out->pingIntervalMs = pingMs;
    out->pingTimeoutMs = timeoutMs;
}

void XGuiRemoteProfile_initPerformance(XGuiRemoteProfile* out)
{
    /* XGuiRemote.md §5.2 性能模式: ARGB32/zlib L1/128×128/60fps/大队列。 */
    profileInit(out, XGUI_REMOTE_PF_ARGB32, XGUI_REMOTE_CODEC_ZLIB, 1,
                128, 128, 60,
                2u * 1024u * 1024u, 256u * 1024u,
                0u, 5000u, 15000u);
}

void XGuiRemoteProfile_initResource(XGuiRemoteProfile* out)
{
    /* XGuiRemote.md §5.2 资源模式: RGB565/RLE/32×32/15fps/小缓冲。 */
    profileInit(out, XGUI_REMOTE_PF_RGB565, XGUI_REMOTE_CODEC_RLE, 1,
                32, 32, 15,
                256u * 1024u, 32u * 1024u,
                30u, 10000u, 30000u);
}

void XGuiRemoteProfile_initAuto(XGuiRemoteProfile* out)
{
    /* V2 预留: V1 行为等价 resource 预设(XGuiRemote.md §5.3)。 */
    XGuiRemoteProfile_initResource(out);
}

void XGuiRemoteProfile_sanitize(XGuiRemoteProfile* profile)
{
    if (!profile) return;
    if (!profileFormatValid((int)profile->wireFormat))
        profile->wireFormat = XGUI_REMOTE_PF_ARGB32; /* 非法枚举回退(冻结) */
    if (!profileCodecValid((int)profile->codec))
        profile->codec = XGUI_REMOTE_CODEC_RLE;
    if (profile->zlibLevel < 1) profile->zlibLevel = 1;
    if (profile->zlibLevel > 9) profile->zlibLevel = 9;
    if (profile->tileWidth < 16) profile->tileWidth = 16;
    if (profile->tileWidth > 512) profile->tileWidth = 512;
    if (profile->tileHeight < 16) profile->tileHeight = 16;
    if (profile->tileHeight > 512) profile->tileHeight = 512;
    if (profile->maxFps < 1) profile->maxFps = 1;
    if (profile->maxFps > 240) profile->maxFps = 240;
    if (profile->encodeQueueBytes < PROFILE_QUEUE_BYTES_MIN)
        profile->encodeQueueBytes = PROFILE_QUEUE_BYTES_MIN;
    if (profile->encodeQueueBytes > PROFILE_QUEUE_BYTES_MAX)
        profile->encodeQueueBytes = PROFILE_QUEUE_BYTES_MAX;
    if (profile->txBudgetBytes < PROFILE_TX_BYTES_MIN)
        profile->txBudgetBytes = PROFILE_TX_BYTES_MIN;
    if (profile->txBudgetBytes > PROFILE_TX_BYTES_MAX)
        profile->txBudgetBytes = PROFILE_TX_BYTES_MAX;
    if (profile->mouseMoveThrottleMs > PROFILE_THROTTLE_MS_MAX)
        profile->mouseMoveThrottleMs = PROFILE_THROTTLE_MS_MAX;
    if (profile->pingIntervalMs < PROFILE_PING_MS_MIN)
        profile->pingIntervalMs = PROFILE_PING_MS_MIN;
    if (profile->pingIntervalMs > PROFILE_PING_MS_MAX)
        profile->pingIntervalMs = PROFILE_PING_MS_MAX;
    if (profile->pingTimeoutMs < PROFILE_PING_MS_MIN)
        profile->pingTimeoutMs = PROFILE_PING_MS_MIN;
    if (profile->pingTimeoutMs > PROFILE_PING_MS_MAX)
        profile->pingTimeoutMs = PROFILE_PING_MS_MAX;
}

bool XGuiRemoteProfile_isValid(const XGuiRemoteProfile* profile)
{
    if (!profile) return false;
    return profileInRange(profile);
}

/* ---- 档位自定义块(PROFILE_SET 载荷)序列化 ----
 * 固定布局(32 字节, 顺序=结构体字段序, 宽度显式冻结, 与机器字长无关):
 *   [u8 wireFormat][u8 codec][i32 zlibLevel][u16 tileWidth][u16 tileHeight]
 *   [u16 maxFps][u32 encodeQueueBytes][u32 txBudgetBytes]
 *   [u32 mouseMoveThrottleMs][u32 pingIntervalMs][u32 pingTimeoutMs]
 */
#define PROFILE_BLOCK_BYTES 32u

static size_t profilePutU16Sat(uint8_t* out, size_t v)
{
    if (v > 0xFFFFu) v = 0xFFFFu;
    XGuiRemoteProto_putU16(out, (uint16_t)v);
    return 2;
}

static size_t profilePutU32Sat(uint8_t* out, size_t v)
{
    if (v > 0xFFFFFFFFu) v = 0xFFFFFFFFu;
    XGuiRemoteProto_putU32(out, (uint32_t)v);
    return 4;
}

size_t XGuiRemoteProfile_enc(uint8_t* out, size_t cap,
                             const XGuiRemoteProfile* profile)
{
    if (!out || !profile || cap < PROFILE_BLOCK_BYTES) return 0;

    /* 枚举值按合法值上网(不改写调用方结构体; 非法值用冻结回退值)。 */
    uint8_t format = profileFormatValid((int)profile->wireFormat)
        ? (uint8_t)profile->wireFormat : (uint8_t)XGUI_REMOTE_PF_ARGB32;
    uint8_t codec = profileCodecValid((int)profile->codec)
        ? (uint8_t)profile->codec : (uint8_t)XGUI_REMOTE_CODEC_RLE;

    size_t off = 0;
    out[off++] = format;
    out[off++] = codec;
    XGuiRemoteProto_putU32(out + off, (uint32_t)(int32_t)profile->zlibLevel);
    off += 4;
    off += profilePutU16Sat(out + off, (size_t)profile->tileWidth);
    off += profilePutU16Sat(out + off, (size_t)profile->tileHeight);
    off += profilePutU16Sat(out + off, (size_t)profile->maxFps);
    off += profilePutU32Sat(out + off, profile->encodeQueueBytes);
    off += profilePutU32Sat(out + off, profile->txBudgetBytes);
    off += profilePutU32Sat(out + off, (size_t)profile->mouseMoveThrottleMs);
    off += profilePutU32Sat(out + off, (size_t)profile->pingIntervalMs);
    off += profilePutU32Sat(out + off, (size_t)profile->pingTimeoutMs);
    return off; /* == PROFILE_BLOCK_BYTES */
}

bool XGuiRemoteProfile_dec(const uint8_t* payload, size_t len,
                           XGuiRemoteProfile* out)
{
    if (!payload || !out) return false;
    if (len != PROFILE_BLOCK_BYTES) return false; /* 固定布局 */

    memset(out, 0, sizeof(*out));
    size_t off = 0;
    out->wireFormat = (XGuiRemotePixelFormat)payload[off++];
    out->codec = (XGuiRemoteCodecId)payload[off++];
    out->zlibLevel = (int)(int32_t)XGuiRemoteProto_getU32(payload + off);
    off += 4;
    out->tileWidth = (int)XGuiRemoteProto_getU16(payload + off); off += 2;
    out->tileHeight = (int)XGuiRemoteProto_getU16(payload + off); off += 2;
    out->maxFps = (int)XGuiRemoteProto_getU16(payload + off); off += 2;
    out->encodeQueueBytes = (size_t)XGuiRemoteProto_getU32(payload + off); off += 4;
    out->txBudgetBytes = (size_t)XGuiRemoteProto_getU32(payload + off); off += 4;
    out->mouseMoveThrottleMs = XGuiRemoteProto_getU32(payload + off); off += 4;
    out->pingIntervalMs = XGuiRemoteProto_getU32(payload + off); off += 4;
    out->pingTimeoutMs = XGuiRemoteProto_getU32(payload + off); off += 4;

    XGuiRemoteProfile_sanitize(out); /* 冻结: dec 出口必经 sanitize */
    return true;
}

/* ==================== 消息编解码公共辅助 ==================== */

typedef struct DecCursor {
    const uint8_t* p;
    size_t len;
    size_t off;
} DecCursor;

static bool decTake(DecCursor* c, size_t n, const uint8_t** out)
{
    if (!c || c->len - c->off < n) return false;
    if (out) *out = c->p + c->off;
    c->off += n;
    return true;
}

static bool decU8(DecCursor* c, uint8_t* v)
{
    const uint8_t* p;
    if (!decTake(c, 1, &p)) return false;
    *v = p[0];
    return true;
}

static bool decU16(DecCursor* c, uint16_t* v)
{
    const uint8_t* p;
    if (!decTake(c, 2, &p)) return false;
    *v = XGuiRemoteProto_getU16(p);
    return true;
}

static bool decU32(DecCursor* c, uint32_t* v)
{
    const uint8_t* p;
    if (!decTake(c, 4, &p)) return false;
    *v = XGuiRemoteProto_getU32(p);
    return true;
}

static bool decU64(DecCursor* c, uint64_t* v)
{
    const uint8_t* p;
    if (!decTake(c, 8, &p)) return false;
    *v = XGuiRemoteProto_getU64(p);
    return true;
}

static bool decI32(DecCursor* c, int32_t* v)
{
    uint32_t u;
    if (!decU32(c, &u)) return false;
    *v = (int32_t)u;
    return true;
}

/* u16 长度前缀字符串: 长度越界(超结构缓冲或超剩余负载)=协议错误;
 * 拷贝后保证缓冲内终止(容量允许时)。 */
static bool decString16(DecCursor* c, char* buf, size_t bufCap, uint16_t* lenOut)
{
    uint16_t n;
    const uint8_t* p;
    if (!decU16(c, &n)) return false;
    if ((size_t)n > bufCap) return false;
    if (!decTake(c, n, &p)) return false;
    if (n > 0) memcpy(buf, p, n);
    if ((size_t)n < bufCap) buf[n] = '\0'; /* 缓冲保证终止 */
    *lenOut = n;
    return true;
}

/* enc 侧字符串: 按结构缓冲容量截断(超长截断, 冻结约定)。 */
static size_t encStringLen(const uint16_t* declared, size_t bufCap)
{
    size_t n = *declared;
    if (n > bufCap) n = bufCap;
    return n;
}

static void encString16(uint8_t* out, size_t off, const char* s, size_t n)
{
    XGuiRemoteProto_putU16(out + off, (uint16_t)n);
    if (n > 0) memcpy(out + off + 2, s, n);
}

static bool authMethodValid(uint8_t m)
{
    return m == XGUI_REMOTE_AUTH_NONE || m == XGUI_REMOTE_AUTH_SHA256_CHALLENGE;
}

static bool pixelFormatValid(uint8_t f)
{
    return f == XGUI_REMOTE_PF_ARGB32 || f == XGUI_REMOTE_PF_RGB565;
}

static bool codecIdValid(uint8_t c)
{
    return c == XGUI_REMOTE_CODEC_RAW || c == XGUI_REMOTE_CODEC_RLE ||
           c == XGUI_REMOTE_CODEC_ZLIB;
}

static bool profileIdValid(uint8_t id)
{
    return id == XGUI_REMOTE_PROFILE_PERFORMANCE ||
           id == XGUI_REMOTE_PROFILE_RESOURCE ||
           id == XGUI_REMOTE_PROFILE_AUTO ||
           id == XGUI_REMOTE_PROFILE_CUSTOM;
}

/* ==================== HELLO / HELLO_ACK ==================== */
/* 线上布局: [u16 protocolVersion][u32 capabilities][u8 authMethod]
 *           [u16 nameBytes][name...] */

size_t XGuiRemoteProto_encHello(uint8_t* out, size_t cap,
                                const XGuiRemoteMsgHello* msg)
{
    if (!out || !msg) return 0;
    size_t nameLen = encStringLen(&msg->nameBytes, XGUI_REMOTE_MAX_NAME_BYTES);
    size_t needed = 9 + nameLen;
    if (cap < needed) return 0;
    XGuiRemoteProto_putU16(out, msg->protocolVersion);
    XGuiRemoteProto_putU32(out + 2, msg->capabilities);
    out[6] = msg->authMethod;
    encString16(out + 7, 0, msg->name, nameLen);
    return needed;
}

bool XGuiRemoteProto_decHello(const uint8_t* payload, size_t len,
                              XGuiRemoteMsgHello* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU16(&c, &out->protocolVersion)) return false;
    if (!decU32(&c, &out->capabilities)) return false;
    if (!decU8(&c, &out->authMethod)) return false;
    if (!authMethodValid(out->authMethod)) return false; /* method: 封闭枚举 */
    if (!decString16(&c, out->name, XGUI_REMOTE_MAX_NAME_BYTES,
                     &out->nameBytes)) return false;
    return c.off == c.len; /* 负载必须被完整消费 */
}

/* ==================== AUTH_CHALLENGE ==================== */
/* 线上布局: [u8 method][u16 nonceBytes][nonce...] */

size_t XGuiRemoteProto_encAuthChallenge(uint8_t* out, size_t cap,
                                        const XGuiRemoteMsgAuthChallenge* msg)
{
    if (!out || !msg) return 0;
    size_t nonceLen = encStringLen(&msg->nonceBytes,
                                   XGUI_REMOTE_AUTH_NONCE_BYTES);
    size_t needed = 3 + nonceLen;
    if (cap < needed) return 0;
    out[0] = msg->method;
    XGuiRemoteProto_putU16(out + 1, (uint16_t)nonceLen);
    if (nonceLen > 0) memcpy(out + 3, msg->nonce, nonceLen);
    return needed;
}

bool XGuiRemoteProto_decAuthChallenge(const uint8_t* payload, size_t len,
                                      XGuiRemoteMsgAuthChallenge* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->method)) return false;
    if (!authMethodValid(out->method)) return false; /* method: 封闭枚举 */
    if (!decString16(&c, (char*)out->nonce, XGUI_REMOTE_AUTH_NONCE_BYTES,
                     &out->nonceBytes)) return false;
    return c.off == c.len;
}

/* ==================== AUTH_RESPONSE ==================== */
/* 线上布局: [u16 responseBytes][response...] */

size_t XGuiRemoteProto_encAuthResponse(uint8_t* out, size_t cap,
                                       const XGuiRemoteMsgAuthResponse* msg)
{
    if (!out || !msg) return 0;
    size_t n = encStringLen(&msg->responseBytes,
                            XGUI_REMOTE_AUTH_RESPONSE_BYTES);
    size_t needed = 2 + n;
    if (cap < needed) return 0;
    XGuiRemoteProto_putU16(out, (uint16_t)n);
    if (n > 0) memcpy(out + 2, msg->response, n);
    return needed;
}

bool XGuiRemoteProto_decAuthResponse(const uint8_t* payload, size_t len,
                                     XGuiRemoteMsgAuthResponse* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decString16(&c, (char*)out->response, XGUI_REMOTE_AUTH_RESPONSE_BYTES,
                     &out->responseBytes)) return false;
    return c.off == c.len;
}

/* ==================== AUTH_RESULT ==================== */
/* 线上布局: [u8 ok][u16 textBytes][text...] */

size_t XGuiRemoteProto_encAuthResult(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgAuthResult* msg)
{
    if (!out || !msg) return 0;
    size_t textLen = encStringLen(&msg->textBytes, XGUI_REMOTE_MAX_MSG_BYTES);
    size_t needed = 3 + textLen;
    if (cap < needed) return 0;
    out[0] = msg->ok;
    encString16(out + 1, 0, msg->text, textLen);
    return needed;
}

bool XGuiRemoteProto_decAuthResult(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgAuthResult* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->ok)) return false;
    if (!decString16(&c, out->text, XGUI_REMOTE_MAX_MSG_BYTES,
                     &out->textBytes)) return false;
    return c.off == c.len;
}

/* ==================== FB_META ==================== */
/* 线上布局: [u16 width][u16 height][u8 format][u16 tileWidth][u16 tileHeight]
 *           [u8 profileId][u8 flags][u16 titleBytes][title...] */

size_t XGuiRemoteProto_encFbMeta(uint8_t* out, size_t cap,
                                 const XGuiRemoteMsgFbMeta* msg)
{
    if (!out || !msg) return 0;
    size_t titleLen = encStringLen(&msg->titleBytes,
                                   XGUI_REMOTE_MAX_NAME_BYTES);
    size_t needed = 13 + titleLen;
    if (cap < needed) return 0;
    XGuiRemoteProto_putU16(out, msg->width);
    XGuiRemoteProto_putU16(out + 2, msg->height);
    out[4] = msg->format;
    XGuiRemoteProto_putU16(out + 5, msg->tileWidth);
    XGuiRemoteProto_putU16(out + 7, msg->tileHeight);
    out[9] = msg->profileId;
    out[10] = msg->flags;
    encString16(out + 11, 0, msg->title, titleLen);
    return needed;
}

bool XGuiRemoteProto_decFbMeta(const uint8_t* payload, size_t len,
                               XGuiRemoteMsgFbMeta* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU16(&c, &out->width)) return false;
    if (!decU16(&c, &out->height)) return false;
    if (!decU8(&c, &out->format)) return false;
    if (!pixelFormatValid(out->format)) return false; /* format: 封闭枚举 */
    if (!decU16(&c, &out->tileWidth)) return false;
    if (!decU16(&c, &out->tileHeight)) return false;
    if (!decU8(&c, &out->profileId)) return false;
    if (!profileIdValid(out->profileId)) return false; /* profileId: 封闭枚举 */
    if (!decU8(&c, &out->flags)) return false;         /* flags: 开放, 不校验 */
    if (!decString16(&c, out->title, XGUI_REMOTE_MAX_NAME_BYTES,
                     &out->titleBytes)) return false;
    return c.off == c.len;
}

/* ==================== FB_REQUEST ==================== */
/* 线上布局: [u8 mode][u16 x][u16 y][u16 w][u16 h] */

size_t XGuiRemoteProto_encFbRequest(uint8_t* out, size_t cap,
                                    const XGuiRemoteMsgFbRequest* msg)
{
    if (!out || !msg || cap < 9) return 0;
    out[0] = msg->mode;
    XGuiRemoteProto_putU16(out + 1, msg->x);
    XGuiRemoteProto_putU16(out + 3, msg->y);
    XGuiRemoteProto_putU16(out + 5, msg->w);
    XGuiRemoteProto_putU16(out + 7, msg->h);
    return 9;
}

bool XGuiRemoteProto_decFbRequest(const uint8_t* payload, size_t len,
                                  XGuiRemoteMsgFbRequest* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->mode)) return false;
    if (out->mode > 1) return false; /* mode: 封闭枚举(0=增量,1=全量) */
    if (!decU16(&c, &out->x)) return false;
    if (!decU16(&c, &out->y)) return false;
    if (!decU16(&c, &out->w)) return false;
    if (!decU16(&c, &out->h)) return false;
    return c.off == c.len;
}

/* ==================== FB_UPDATE 帧级头 ==================== */
/* 线上布局: [u32 sequence][u16 tileCount][u8 format][u8 flags] 恒 8 字节 */

size_t XGuiRemoteProto_encFbUpdate(uint8_t* out, size_t cap,
                                   const XGuiRemoteMsgFbUpdate* msg)
{
    if (!out || !msg) return 0;
    if (cap < XGUI_REMOTE_FB_UPDATE_HEADER_BYTES) return 0;
    XGuiRemoteProto_putU32(out, msg->sequence);
    XGuiRemoteProto_putU16(out + 4, msg->tileCount);
    out[6] = msg->format;
    out[7] = msg->flags;
    return XGUI_REMOTE_FB_UPDATE_HEADER_BYTES; /* 恒 8(冻结) */
}

bool XGuiRemoteProto_decFbUpdate(const uint8_t* payload, size_t len,
                                 XGuiRemoteMsgFbUpdate* out)
{
    if (!payload || !out) return false;
    if (len < XGUI_REMOTE_FB_UPDATE_HEADER_BYTES) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU32(&c, &out->sequence)) return false;
    if (!decU16(&c, &out->tileCount)) return false;
    if (!decU8(&c, &out->format)) return false;
    if (!pixelFormatValid(out->format)) return false; /* format: 封闭枚举 */
    if (!decU8(&c, &out->flags)) return false;        /* flags: 开放, 不校验 */
    return true; /* 其余字节为 tile 记录, 由 decFbTile 按偏移解析 */
}

/* ==================== FB_UPDATE tile 记录 ==================== */
/* 线上布局: [u16 x][u16 y][u16 w][u16 h][u8 codec][u8 flags]
 *           [u32 payloadBytes][payload...] */

#define FB_TILE_FIXED_BYTES 14u

size_t XGuiRemoteProto_encFbTile(uint8_t* out, size_t cap,
                                 const XGuiRemoteMsgFbTile* tile,
                                 const uint8_t* encodedPixels)
{
    if (!out || !tile) return 0;
    if (!encodedPixels && tile->payloadBytes != 0) return 0;
    size_t needed = FB_TILE_FIXED_BYTES + tile->payloadBytes;
    if (cap < needed) return 0;
    XGuiRemoteProto_putU16(out, tile->x);
    XGuiRemoteProto_putU16(out + 2, tile->y);
    XGuiRemoteProto_putU16(out + 4, tile->w);
    XGuiRemoteProto_putU16(out + 6, tile->h);
    out[8] = tile->codec;
    out[9] = tile->flags;
    XGuiRemoteProto_putU32(out + 10, tile->payloadBytes);
    if (tile->payloadBytes > 0) {
        memcpy(out + FB_TILE_FIXED_BYTES, encodedPixels, tile->payloadBytes);
    }
    return needed;
}

bool XGuiRemoteProto_decFbTile(const uint8_t* payload, size_t len, size_t offset,
                               XGuiRemoteMsgFbTile* tileOut, size_t* nextOffset)
{
    if (!payload || !tileOut) return false;
    if (offset > len) return false;
    if (len - offset < FB_TILE_FIXED_BYTES) return false;

    memset(tileOut, 0, sizeof(*tileOut));
    DecCursor c = { payload, len, offset };
    if (!decU16(&c, &tileOut->x)) return false;
    if (!decU16(&c, &tileOut->y)) return false;
    if (!decU16(&c, &tileOut->w)) return false;
    if (!decU16(&c, &tileOut->h)) return false;
    if (!decU8(&c, &tileOut->codec)) return false;
    if (!codecIdValid(tileOut->codec)) return false; /* codec: 封闭枚举 */
    if (!decU8(&c, &tileOut->flags)) return false;   /* flags: 开放, 不校验 */
    if (!decU32(&c, &tileOut->payloadBytes)) return false;
    if (c.len - c.off < tileOut->payloadBytes) return false; /* 记录截断 */
    tileOut->payload = c.p + c.off;
    c.off += tileOut->payloadBytes;
    if (nextOffset) *nextOffset = c.off;
    return true;
}

/* ==================== FB_ACK ==================== */
/* 线上布局: [u32 ackSequence][u32 windowBytes] */

size_t XGuiRemoteProto_encFbAck(uint8_t* out, size_t cap,
                                const XGuiRemoteMsgFbAck* msg)
{
    if (!out || !msg || cap < 8) return 0;
    XGuiRemoteProto_putU32(out, msg->ackSequence);
    XGuiRemoteProto_putU32(out + 4, msg->windowBytes);
    return 8;
}

bool XGuiRemoteProto_decFbAck(const uint8_t* payload, size_t len,
                              XGuiRemoteMsgFbAck* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU32(&c, &out->ackSequence)) return false;
    if (!decU32(&c, &out->windowBytes)) return false;
    return c.off == c.len;
}

/* ==================== INPUT_KEY ==================== */
/* 线上布局: [u8 action][u32 key][u32 modifiers][u32 nativeScanCode]
 *           [u32 timestampMs] */

size_t XGuiRemoteProto_encInputKey(uint8_t* out, size_t cap,
                                   const XGuiRemoteMsgInputKey* msg)
{
    if (!out || !msg || cap < 17) return 0;
    out[0] = msg->action;
    XGuiRemoteProto_putU32(out + 1, msg->key);
    XGuiRemoteProto_putU32(out + 5, msg->modifiers);
    XGuiRemoteProto_putU32(out + 9, msg->nativeScanCode);
    XGuiRemoteProto_putU32(out + 13, msg->timestampMs);
    return 17;
}

bool XGuiRemoteProto_decInputKey(const uint8_t* payload, size_t len,
                                 XGuiRemoteMsgInputKey* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->action)) return false;
    if (out->action != XGUI_REMOTE_KEY_PRESS &&
        out->action != XGUI_REMOTE_KEY_RELEASE) return false; /* 封闭枚举 */
    if (!decU32(&c, &out->key)) return false;
    if (!decU32(&c, &out->modifiers)) return false;
    if (!decU32(&c, &out->nativeScanCode)) return false;
    if (!decU32(&c, &out->timestampMs)) return false;
    return c.off == c.len;
}

/* ==================== INPUT_POINTER ==================== */
/* 线上布局: [u8 action][u8 button][u16 buttons][u32 modifiers]
 *           [i16 x][i16 y][u32 timestampMs] */

size_t XGuiRemoteProto_encInputPointer(uint8_t* out, size_t cap,
                                       const XGuiRemoteMsgInputPointer* msg)
{
    if (!out || !msg || cap < 16) return 0;
    out[0] = msg->action;
    out[1] = msg->button;
    XGuiRemoteProto_putU16(out + 2, msg->buttons);
    XGuiRemoteProto_putU32(out + 4, msg->modifiers);
    XGuiRemoteProto_putU16(out + 8, (uint16_t)msg->x);
    XGuiRemoteProto_putU16(out + 10, (uint16_t)msg->y);
    XGuiRemoteProto_putU32(out + 12, msg->timestampMs);
    return 16;
}

bool XGuiRemoteProto_decInputPointer(const uint8_t* payload, size_t len,
                                     XGuiRemoteMsgInputPointer* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->action)) return false;
    if (out->action > XGUI_REMOTE_PTR_DBL_CLICK) return false; /* 封闭枚举 */
    if (!decU8(&c, &out->button)) return false;
    if (!decU16(&c, &out->buttons)) return false;
    if (!decU32(&c, &out->modifiers)) return false;
    uint16_t x, y;
    if (!decU16(&c, &x)) return false;
    if (!decU16(&c, &y)) return false;
    out->x = (int16_t)x;
    out->y = (int16_t)y;
    if (!decU32(&c, &out->timestampMs)) return false;
    return c.off == c.len;
}

/* ==================== INPUT_WHEEL ==================== */
/* 线上布局: [i16 angleX][i16 angleY][i16 pixelX][i16 pixelY][u16 buttons]
 *           [u32 modifiers][i16 x][i16 y][u32 timestampMs] */

size_t XGuiRemoteProto_encInputWheel(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgInputWheel* msg)
{
    if (!out || !msg || cap < 22) return 0;
    XGuiRemoteProto_putU16(out, (uint16_t)msg->angleX);
    XGuiRemoteProto_putU16(out + 2, (uint16_t)msg->angleY);
    XGuiRemoteProto_putU16(out + 4, (uint16_t)msg->pixelX);
    XGuiRemoteProto_putU16(out + 6, (uint16_t)msg->pixelY);
    XGuiRemoteProto_putU16(out + 8, msg->buttons);
    XGuiRemoteProto_putU32(out + 10, msg->modifiers);
    XGuiRemoteProto_putU16(out + 14, (uint16_t)msg->x);
    XGuiRemoteProto_putU16(out + 16, (uint16_t)msg->y);
    XGuiRemoteProto_putU32(out + 18, msg->timestampMs);
    return 22;
}

bool XGuiRemoteProto_decInputWheel(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgInputWheel* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    uint16_t angleX, angleY, pixelX, pixelY, x, y;
    if (!decU16(&c, &angleX)) return false;
    if (!decU16(&c, &angleY)) return false;
    if (!decU16(&c, &pixelX)) return false;
    if (!decU16(&c, &pixelY)) return false;
    if (!decU16(&c, &out->buttons)) return false;
    if (!decU32(&c, &out->modifiers)) return false;
    if (!decU16(&c, &x)) return false;
    if (!decU16(&c, &y)) return false;
    if (!decU32(&c, &out->timestampMs)) return false;
    out->angleX = (int16_t)angleX;
    out->angleY = (int16_t)angleY;
    out->pixelX = (int16_t)pixelX;
    out->pixelY = (int16_t)pixelY;
    out->x = (int16_t)x;
    out->y = (int16_t)y;
    return c.off == c.len;
}

/* ==================== INPUT_TOUCH ==================== */
/* 线上布局: [u8 action][u8 pointCount][u32 timestampMs]
 *           每点: [u32 id][u8 state][i16 x][i16 y][u8 pressureQ8][u8 reserved] */

#define TOUCH_POINT_BYTES 11u

size_t XGuiRemoteProto_encInputTouch(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgInputTouch* msg)
{
    if (!out || !msg) return 0;
    size_t points = msg->pointCount;
    if (points > XGUI_REMOTE_MAX_TOUCH_POINTS) points = XGUI_REMOTE_MAX_TOUCH_POINTS;
    size_t needed = 6 + points * TOUCH_POINT_BYTES;
    if (cap < needed) return 0;
    out[0] = msg->action;
    out[1] = (uint8_t)points;
    XGuiRemoteProto_putU32(out + 2, msg->timestampMs);
    size_t off = 6;
    for (size_t i = 0; i < points; ++i) {
        const XGuiRemoteMsgTouchPoint* pt = &msg->points[i];
        XGuiRemoteProto_putU32(out + off, pt->id);
        out[off + 4] = pt->state;
        XGuiRemoteProto_putU16(out + off + 5, (uint16_t)pt->x);
        XGuiRemoteProto_putU16(out + off + 7, (uint16_t)pt->y);
        out[off + 9] = pt->pressureQ8;
        out[off + 10] = pt->reserved;
        off += TOUCH_POINT_BYTES;
    }
    return needed;
}

bool XGuiRemoteProto_decInputTouch(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgInputTouch* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->action)) return false;
    if (out->action > XGUI_REMOTE_TOUCH_CANCEL) return false; /* 封闭枚举 */
    if (!decU8(&c, &out->pointCount)) return false;
    if (out->pointCount > XGUI_REMOTE_MAX_TOUCH_POINTS) return false; /* 越界 */
    if (!decU32(&c, &out->timestampMs)) return false;
    for (size_t i = 0; i < out->pointCount; ++i) {
        XGuiRemoteMsgTouchPoint* pt = &out->points[i];
        if (!decU32(&c, &pt->id)) return false;
        if (!decU8(&c, &pt->state)) return false;
        uint16_t x, y;
        if (!decU16(&c, &x)) return false;
        if (!decU16(&c, &y)) return false;
        pt->x = (int16_t)x;
        pt->y = (int16_t)y;
        if (!decU8(&c, &pt->pressureQ8)) return false;
        if (!decU8(&c, &pt->reserved)) return false;
    }
    return c.off == c.len;
}

/* ==================== INPUT_IME ==================== */
/* 线上布局: [u16 preeditBytes][preedit...][u16 commitBytes][commit...]
 *           [i32 replacementStart][i32 replacementLength]
 *           [i32 cursorPosition][i32 anchorPosition] */

size_t XGuiRemoteProto_encInputIme(uint8_t* out, size_t cap,
                                   const XGuiRemoteMsgInputIme* msg)
{
    if (!out || !msg) return 0;
    size_t preeditLen = encStringLen(&msg->preeditBytes,
                                     XGUI_REMOTE_MAX_TEXT_BYTES);
    size_t commitLen = encStringLen(&msg->commitBytes,
                                    XGUI_REMOTE_MAX_TEXT_BYTES);
    size_t needed = 20 + preeditLen + commitLen;
    if (cap < needed) return 0;
    size_t off = 0;
    encString16(out, off, msg->preedit, preeditLen);
    off += 2 + preeditLen;
    encString16(out, off, msg->commit, commitLen);
    off += 2 + commitLen;
    XGuiRemoteProto_putU32(out + off, (uint32_t)msg->replacementStart); off += 4;
    XGuiRemoteProto_putU32(out + off, (uint32_t)msg->replacementLength); off += 4;
    XGuiRemoteProto_putU32(out + off, (uint32_t)msg->cursorPosition); off += 4;
    XGuiRemoteProto_putU32(out + off, (uint32_t)msg->anchorPosition); off += 4;
    return needed;
}

bool XGuiRemoteProto_decInputIme(const uint8_t* payload, size_t len,
                                 XGuiRemoteMsgInputIme* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decString16(&c, out->preedit, XGUI_REMOTE_MAX_TEXT_BYTES,
                     &out->preeditBytes)) return false;
    if (!decString16(&c, out->commit, XGUI_REMOTE_MAX_TEXT_BYTES,
                     &out->commitBytes)) return false;
    if (!decI32(&c, &out->replacementStart)) return false;
    if (!decI32(&c, &out->replacementLength)) return false;
    if (!decI32(&c, &out->cursorPosition)) return false;
    if (!decI32(&c, &out->anchorPosition)) return false;
    return c.off == c.len;
}

/* ==================== PROFILE_SET / PROFILE_RESULT ==================== */
/* 线上布局: [u8 profileId][u8 hasCustomProfile][自定义块 32B(可选)] */

size_t XGuiRemoteProto_encProfileSet(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgProfileSet* msg)
{
    if (!out || !msg) return 0;
    size_t needed = 2;
    if (msg->hasCustomProfile) needed += PROFILE_BLOCK_BYTES;
    if (cap < needed) return 0;
    out[0] = msg->profileId;
    out[1] = msg->hasCustomProfile ? 1 : 0;
    if (msg->hasCustomProfile) {
        if (XGuiRemoteProfile_enc(out + 2, cap - 2, &msg->profile) == 0) {
            return 0; /* 不产生半写 */
        }
    }
    return needed;
}

bool XGuiRemoteProto_decProfileSet(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgProfileSet* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->profileId)) return false;
    if (!profileIdValid(out->profileId)) return false; /* 封闭枚举 */
    if (!decU8(&c, &out->hasCustomProfile)) return false;
    if (out->hasCustomProfile) {
        if (c.len - c.off < PROFILE_BLOCK_BYTES) return false;
        if (!XGuiRemoteProfile_dec(c.p + c.off, PROFILE_BLOCK_BYTES,
                                   &out->profile)) return false;
        c.off += PROFILE_BLOCK_BYTES;
    } else {
        XGuiRemoteProfile_initResource(&out->profile); /* 确定性缺省 */
        XGuiRemoteProfile_sanitize(&out->profile);
    }
    return c.off == c.len;
}

/* 线上布局: [u8 accepted][u8 profileId] */

size_t XGuiRemoteProto_encProfileResult(uint8_t* out, size_t cap,
                                        const XGuiRemoteMsgProfileResult* msg)
{
    if (!out || !msg || cap < 2) return 0;
    out[0] = msg->accepted;
    out[1] = msg->profileId;
    return 2;
}

bool XGuiRemoteProto_decProfileResult(const uint8_t* payload, size_t len,
                                      XGuiRemoteMsgProfileResult* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->accepted)) return false;      /* 开放, 不校验 */
    if (!decU8(&c, &out->profileId)) return false;
    if (!profileIdValid(out->profileId)) return false; /* 封闭枚举 */
    return c.off == c.len;
}

/* ==================== PING / PONG ==================== */
/* 线上布局: [u64 timestampMs] */

size_t XGuiRemoteProto_encPing(uint8_t* out, size_t cap, uint64_t timestampMs)
{
    if (!out || cap < 8) return 0;
    XGuiRemoteProto_putU64(out, timestampMs);
    return 8;
}

bool XGuiRemoteProto_decPing(const uint8_t* payload, size_t len,
                             uint64_t* timestampMsOut)
{
    if (!payload) return false;
    DecCursor c = { payload, len, 0 };
    uint64_t ts;
    if (!decU64(&c, &ts)) return false;
    if (timestampMsOut) *timestampMsOut = ts;
    return c.off == c.len;
}

/* ==================== BYE ==================== */
/* 线上布局: [u8 reason][u16 textBytes][text...](reason 开放, 不校验) */

size_t XGuiRemoteProto_encBye(uint8_t* out, size_t cap,
                              const XGuiRemoteMsgBye* msg)
{
    if (!out || !msg) return 0;
    size_t textLen = encStringLen(&msg->textBytes, XGUI_REMOTE_MAX_MSG_BYTES);
    size_t needed = 3 + textLen;
    if (cap < needed) return 0;
    out[0] = msg->reason;
    encString16(out + 1, 0, msg->text, textLen);
    return needed;
}

bool XGuiRemoteProto_decBye(const uint8_t* payload, size_t len,
                            XGuiRemoteMsgBye* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU8(&c, &out->reason)) return false;
    if (!decString16(&c, out->text, XGUI_REMOTE_MAX_MSG_BYTES,
                     &out->textBytes)) return false;
    return c.off == c.len;
}

/* ==================== ERROR ==================== */
/* 线上布局: [u16 code][u16 textBytes][text...](code 开放, 不校验) */

size_t XGuiRemoteProto_encError(uint8_t* out, size_t cap,
                                const XGuiRemoteMsgError* msg)
{
    if (!out || !msg) return 0;
    size_t textLen = encStringLen(&msg->textBytes, XGUI_REMOTE_MAX_MSG_BYTES);
    size_t needed = 4 + textLen;
    if (cap < needed) return 0;
    XGuiRemoteProto_putU16(out, msg->code);
    encString16(out + 2, 0, msg->text, textLen);
    return needed;
}

bool XGuiRemoteProto_decError(const uint8_t* payload, size_t len,
                              XGuiRemoteMsgError* out)
{
    if (!payload || !out) return false;
    memset(out, 0, sizeof(*out));
    DecCursor c = { payload, len, 0 };
    if (!decU16(&c, &out->code)) return false;
    if (!decString16(&c, out->text, XGUI_REMOTE_MAX_MSG_BYTES,
                     &out->textBytes)) return false;
    return c.off == c.len;
}

#endif /* XGUI_REMOTE_ON */
