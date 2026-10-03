/**
 * @file       XGuiRemoteProto.h
 * @brief      XGuiRemote 帧协议/会话层契约(冻结头, 只声明 API 不含实现)。
 * @details    XGuiServer/XGuiClient 远程窗口功能共用的线上协议定义:
 *             - 传输无关: 会话跑在任意 XIODevice* 上(本头仅前置声明
 *               XIODevice, 不引入其定义, 依赖保持叶子化);
 *             - 长度前缀帧 + 消息编解码 + banner/版本/能力协商;
 *             - 全部序列化函数为纯函数(无全局状态, 线程安全);
 *             - 线上字节序一律小端(LE), 经 put/get 辅助显式序列化,
 *               严禁把结构体整块 cast 上网。
 *             协议规范全文见仓库根 XGuiRemote.md §3。
 * @note       模块编译开关在本文件以 #ifndef 形式登记(不改 XGuiConfig.h;
 *             先例: XGUI_BACKINGSTORE_TILE_BATCHING_ON 于
 *             XPlatformBackingStore.h)。=0 时本头除开关外整体裁空,
 *             XGuiServer.h/XGuiClient.h 随之裁空。
 * @author     XinYueC 团队
 */
#ifndef XGUIREMOTEPROTO_H
#define XGUIREMOTEPROTO_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ==================== 模块编译开关(不归 XGuiConfig.h 所有) ==================== */

/**
 * @brief 模块总开关(默认 1)。=0 时 XGuiRemote 全家(协议/编解码/服务器/
 *        客户端/回环设备)整体从头文件与编译产物中剔除。
 */
#ifndef XGUI_REMOTE_ON
#define XGUI_REMOTE_ON 1
#endif

/**
 * @brief zlib tile 编解码开关(默认 1)。zlib 由 Library/zlib 内嵌并链接全部
 *        目标(CMakeLists.txt add_subdirectory(Library/zlib)); 极端裁剪场景
 *        置 0 时仅剔除 ZLIB 编解码器, RLE/RAW 不受影响。
 */
#ifndef XGUI_REMOTE_ZLIB_ON
#define XGUI_REMOTE_ZLIB_ON 1
#endif

/**
 * @brief TLS 传输档开关(默认 1, 依赖内嵌 mbedTLS 的 XSslSocket)。置 0 时
 *        XGuiClient_connectToHostEncrypted 与服务端 TLS 升级路径整体剔除,
 *        协议与明文 TCP/自定义传输不受任何影响(协议不感知加密)。
 */
#ifndef XGUI_REMOTE_TLS_ON
#define XGUI_REMOTE_TLS_ON 1
#endif

#if XGUI_REMOTE_ON

/* ==================== 依赖前置声明 ==================== */

/** @brief XIODevice 前置声明(传输抽象; 完整定义见 Src/XIO/XIODevice)。 */
typedef struct XIODevice XIODevice;

/* ==================== 协议常量 ==================== */

/** @brief 协议版本(横幅与 HELLO 携带; 协商取双方较小值)。 */
#define XGUI_REMOTE_PROTOCOL_VERSION   1

/** @brief 横幅字节数: 魔数 4B('X','G','R','1') + u16 版本 + u16 保留。 */
#define XGUI_REMOTE_BANNER_BYTES       8

/** @brief 单帧负载字节硬上限(不含 5 字节帧头); 超限即协议错误。 */
#define XGUI_REMOTE_MAX_FRAME_BYTES    (16u * 1024u * 1024u)

/** @brief 帧头字节数: [u32 payloadLength][u8 msgType]。 */
#define XGUI_REMOTE_FRAME_HEADER_BYTES 5

/** @brief FB_UPDATE 帧级头字节数(u32 sequence + u16 tileCount + u8 format
 *         + u8 flags)。帧泵交付的 payload 已剥离 5 字节传输帧头, 故 FB_UPDATE
 *         负载内首条 tile 的起始偏移即本值(encFbUpdate 恒返回 8)。 */
#define XGUI_REMOTE_FB_UPDATE_HEADER_BYTES 8

/** @brief 客户端/服务端名称与窗口标题内联缓冲上限(字节, 不含终止符)。 */
#define XGUI_REMOTE_MAX_NAME_BYTES     64
/** @brief IME preedit/commit 文本内联缓冲上限(字节)。 */
#define XGUI_REMOTE_MAX_TEXT_BYTES     1024
/** @brief BYE/AUTH_RESULT/ERROR 等短文本内联缓冲上限(字节)。 */
#define XGUI_REMOTE_MAX_MSG_BYTES      256
/** @brief 单条触摸事件触点数上限(与 XWidget per-id 抓取表容量一致)。 */
#define XGUI_REMOTE_MAX_TOUCH_POINTS   8
/** @brief 认证挑战 nonce 与应答字节数(SHA-256 输出宽度)。 */
#define XGUI_REMOTE_AUTH_NONCE_BYTES   32
#define XGUI_REMOTE_AUTH_RESPONSE_BYTES 32

/* ==================== 线上枚举(数值冻结, 互操作依据) ==================== */

/**
 * @brief 消息类型(帧内 u8 msgType)。
 */
typedef enum XGuiRemoteMsgType {
    XGUI_REMOTE_MSG_HELLO           = 1,  /**< C→S 客户端问候(版本/能力/认证建议) */
    XGUI_REMOTE_MSG_HELLO_ACK       = 2,  /**< S→C 服务端应答(定版/定认证法) */
    XGUI_REMOTE_MSG_AUTH_CHALLENGE  = 3,  /**< S→C 认证挑战(nonce) */
    XGUI_REMOTE_MSG_AUTH_RESPONSE   = 4,  /**< C→S 认证应答 */
    XGUI_REMOTE_MSG_AUTH_RESULT     = 5,  /**< S→C 认证结果 */
    XGUI_REMOTE_MSG_FB_META         = 6,  /**< S→C 画面元信息(尺寸/格式/tile/档位/标题) */
    XGUI_REMOTE_MSG_FB_REQUEST      = 7,  /**< C→S 画面请求(全量刷新/续增量) */
    XGUI_REMOTE_MSG_FB_UPDATE       = 8,  /**< S→C 画面增量(tile 批) */
    XGUI_REMOTE_MSG_FB_ACK          = 9,  /**< C→S 画面确认(V2 流控预留, V1 不发不收) */
    XGUI_REMOTE_MSG_INPUT_KEY       = 10, /**< C→S 键盘按下/释放 */
    XGUI_REMOTE_MSG_INPUT_POINTER   = 11, /**< C→S 鼠标移动/按下/释放/双击 */
    XGUI_REMOTE_MSG_INPUT_WHEEL     = 12, /**< C→S 滚轮 */
    XGUI_REMOTE_MSG_INPUT_TOUCH     = 13, /**< C→S 多点触摸(per-id) */
    XGUI_REMOTE_MSG_INPUT_IME       = 14, /**< C→S 输入法 preedit/commit 文本 */
    XGUI_REMOTE_MSG_PROFILE_SET     = 15, /**< C→S 档位切换请求 */
    XGUI_REMOTE_MSG_PROFILE_RESULT  = 16, /**< S→C 档位切换应答 */
    XGUI_REMOTE_MSG_PING            = 17, /**< 双向 保活探测 */
    XGUI_REMOTE_MSG_PONG            = 18, /**< 双向 保活回显 */
    XGUI_REMOTE_MSG_BYE             = 19, /**< 双向 优雅断开 */
    XGUI_REMOTE_MSG_ERROR           = 20  /**< 双向 错误通告 */
} XGuiRemoteMsgType;

/**
 * @brief 线上像素格式。ARGB32 = 0xAARRGGBB, 内存字节序 B,G,R,A;
 *        RGB565 = 16 位 5:6:5 小端。tile 载荷行主序、无行填充。
 */
typedef enum XGuiRemotePixelFormat {
    XGUI_REMOTE_PF_ARGB32 = 0,
    XGUI_REMOTE_PF_RGB565 = 1
} XGuiRemotePixelFormat;

/**
 * @brief tile 编解码器 id(FB_UPDATE tile 记录携带)。
 */
typedef enum XGuiRemoteCodecId {
    XGUI_REMOTE_CODEC_RAW  = 0, /**< 不压缩(基线兜底, 恒可用) */
    XGUI_REMOTE_CODEC_RLE  = 1, /**< 像素对齐 PackBits(零依赖, 恒可用) */
    XGUI_REMOTE_CODEC_ZLIB = 2  /**< deflate(XGUI_REMOTE_ZLIB_ON 时可用) */
} XGuiRemoteCodecId;

/**
 * @brief 认证方法。强度注记: SHA256_CHALLENGE 防口令明文过网与重放,
 *        但无服务器身份验证与信道绑定, 非强安全; 真实安全选项是 TLS 档。
 */
typedef enum XGuiRemoteAuthMethod {
    XGUI_REMOTE_AUTH_NONE             = 0,
    XGUI_REMOTE_AUTH_SHA256_CHALLENGE = 1
} XGuiRemoteAuthMethod;

/**
 * @brief 档位 id(Custom=0xFF 表示 PROFILE_SET 携带自定义参数块)。
 */
typedef enum XGuiRemoteProfileId {
    XGUI_REMOTE_PROFILE_PERFORMANCE = 0,
    XGUI_REMOTE_PROFILE_RESOURCE    = 1,
    XGUI_REMOTE_PROFILE_AUTO        = 2,  /**< V2 预留: V1 行为等价 RESOURCE */
    XGUI_REMOTE_PROFILE_CUSTOM      = 0xFF
} XGuiRemoteProfileId;

/**
 * @brief 会话状态(客户端对外状态机; 服务端内部复用)。
 */
typedef enum XGuiRemoteSessionState {
    XGUI_REMOTE_STATE_DISCONNECTED   = 0,
    XGUI_REMOTE_STATE_BANNER_WAIT    = 1,
    XGUI_REMOTE_STATE_HANDSHAKING    = 2,
    XGUI_REMOTE_STATE_AUTHENTICATING = 3,
    XGUI_REMOTE_STATE_STREAMING      = 4
} XGuiRemoteSessionState;

/**
 * @brief 断开原因(BYE 消息)。
 */
typedef enum XGuiRemoteByeReason {
    XGUI_REMOTE_BYE_NORMAL          = 0,
    XGUI_REMOTE_BYE_PROTOCOL_ERROR  = 1,
    XGUI_REMOTE_BYE_AUTH_FAILED     = 2,
    XGUI_REMOTE_BYE_VERSION         = 3,
    XGUI_REMOTE_BYE_SESSION_LIMIT   = 4,
    XGUI_REMOTE_BYE_TIMEOUT         = 5,
    XGUI_REMOTE_BYE_SERVER_SHUTDOWN = 6
} XGuiRemoteByeReason;

/**
 * @brief 协议错误码(ERROR 消息与 API 负返回值共用数值)。
 */
typedef enum XGuiRemoteError {
    XGUI_REMOTE_ERR_NONE           = 0,
    XGUI_REMOTE_ERR_PROTOCOL       = 1,
    XGUI_REMOTE_ERR_AUTH           = 2,
    XGUI_REMOTE_ERR_VERSION        = 3,
    XGUI_REMOTE_ERR_SESSION_LIMIT  = 4,
    XGUI_REMOTE_ERR_INTERNAL       = 5,
    XGUI_REMOTE_ERR_FRAME_TOO_LARGE = 6,
    XGUI_REMOTE_ERR_BUFFER_OVERFLOW = 7,
    XGUI_REMOTE_ERR_TIMEOUT        = 8
} XGuiRemoteError;

/** @name 能力协商位(HELLO/HELLO_ACK 的 u32 capabilities) */
/** @{ */
#define XGUI_REMOTE_CAP_ZLIB        (1u << 0) /**< 支持 ZLIB tile 编解码 */
#define XGUI_REMOTE_CAP_RGB565      (1u << 1) /**< 支持 RGB565 线上格式 */
#define XGUI_REMOTE_CAP_TOUCH       (1u << 2) /**< 支持触摸输入转发 */
#define XGUI_REMOTE_CAP_IME         (1u << 3) /**< 支持 IME 文本转发 */
#define XGUI_REMOTE_CAP_TLS         (1u << 4) /**< 传输层为 TLS */
#define XGUI_REMOTE_CAP_PROFILE_SET (1u << 5) /**< 支持运行期档位切换 */
#define XGUI_REMOTE_CAP_FB_ACK      (1u << 6) /**< 支持 FB_ACK 流控(V2 预留) */
/** @} */

/** @name 输入子动作枚举值(INPUT_* 消息内 u8) */
/** @{ */
#define XGUI_REMOTE_PTR_MOVE     0
#define XGUI_REMOTE_PTR_PRESS    1
#define XGUI_REMOTE_PTR_RELEASE  2
#define XGUI_REMOTE_PTR_DBL_CLICK 3
#define XGUI_REMOTE_KEY_PRESS    1
#define XGUI_REMOTE_KEY_RELEASE  2
#define XGUI_REMOTE_TOUCH_BEGIN  0
#define XGUI_REMOTE_TOUCH_UPDATE 1
#define XGUI_REMOTE_TOUCH_END    2
#define XGUI_REMOTE_TOUCH_CANCEL 3
/** 触点状态(对齐 XTouchEvent::XTouchPoint m_state 取值)。 */
#define XGUI_REMOTE_TP_PRESSED    0
#define XGUI_REMOTE_TP_UPDATED    1
#define XGUI_REMOTE_TP_STATIONARY 2
#define XGUI_REMOTE_TP_RELEASED   3
/** @} */

/** @name 修饰键掩码(对齐 XEvent.h XKeyboardModifiers 位定义, 原值透传) */
/** @{ */
#define XGUI_REMOTE_MOD_SHIFT   0x01u
#define XGUI_REMOTE_MOD_CTRL    0x02u
#define XGUI_REMOTE_MOD_ALT     0x04u
#define XGUI_REMOTE_MOD_META    0x08u
#define XGUI_REMOTE_MOD_KEYPAD  0x10u
/** @} */

/** @name 鼠标键掩码(对齐 XEvent.h 鼠标键位定义, 原值透传) */
/** @{ */
#define XGUI_REMOTE_BTN_LEFT    0x01u
#define XGUI_REMOTE_BTN_RIGHT   0x02u
#define XGUI_REMOTE_BTN_MIDDLE  0x04u
#define XGUI_REMOTE_BTN_BACK    0x08u
#define XGUI_REMOTE_BTN_FORWARD 0x10u
/** @} */

/** @brief FB_UPDATE / tile 记录 flags(保留位, V1 恒 0)。 */
#define XGUI_REMOTE_TILE_FLAG_NONE 0x00u

/* ==================== 小端序列化辅助(纯函数) ==================== */

/** @brief 写 u16 小端。 */
void XGuiRemoteProto_putU16(uint8_t* out, uint16_t v);
/** @brief 写 u32 小端。 */
void XGuiRemoteProto_putU32(uint8_t* out, uint32_t v);
/** @brief 写 u64 小端。 */
void XGuiRemoteProto_putU64(uint8_t* out, uint64_t v);
/** @brief 读 u16 小端。 */
uint16_t XGuiRemoteProto_getU16(const uint8_t* in);
/** @brief 读 u32 小端。 */
uint32_t XGuiRemoteProto_getU32(const uint8_t* in);
/** @brief 读 u64 小端。 */
uint64_t XGuiRemoteProto_getU64(const uint8_t* in);

/* ==================== 横幅与帧基元 ==================== */

/**
 * @brief      组 8 字节横幅(魔数 'X','G','R','1' + 协议版本 + 保留 0)。
 * @param      out 输出缓冲; 不得为 NULL, 容量至少 XGUI_REMOTE_BANNER_BYTES。
 */
void XGuiRemoteProto_makeBanner(uint8_t* out);

/**
 * @brief      校验对端横幅魔数(冻结实现口径: memcmp(bytes, "XGR1", 4)
 *             字节串比较; 文档中的 LE 整数标注仅为助记, 不得用于比较)。
 * @param      bytes 对端发来的前 XGUI_REMOTE_BANNER_BYTES 字节。
 * @return     魔数匹配返回 true; 否则应立即断开(协议错误)。
 */
bool XGuiRemoteProto_bannerIsValid(const uint8_t* bytes);

/**
 * @brief      从对端横幅中取协议版本(u16 小端, 偏移 4)。
 */
uint16_t XGuiRemoteProto_bannerVersion(const uint8_t* bytes);

/**
 * @brief      组帧并写入设备(负载 + 5 字节帧头; 单次写, 不内部循环补写)。
 * @details    线程约定: 只能在该设备的属主线程调用(XGuiRemote 会话中即
 *             GUI 线程)。真实传输是非阻塞直写: XAbstractSocket 的
 *             WriteData 直接对 fd XDevice_write(XAbstractSocket.c:631-635),
 *             XIODevice_write_1 原样透传其返回值(XIODevice.c:283-303),
 *             TCP 发送缓冲一满即短写——短写是背压, 不是链路错误。
 *             调用方(会话层)纪律(冻结): 返回值 < 总帧长(5+payloadBytes)
 *             时, 把未写出余量存入会话"帧尾待写缓冲", 由事件循环 poll
 *             回调按档位 txBudgetBytes 逐圈用 XIODevice_write_1 直写续传;
 *             待写缓冲清空前不得组装新帧。任意慢链路由此保持有界且
 *             不会被误判断链。
 * @param      device 已连通的 XIODevice(借用); 不得为 NULL。
 * @param      type   消息类型。
 * @param      payload 负载(借用); 可为 NULL(此时 payloadBytes 必须为 0)。
 * @param      payloadBytes 负载字节数; 超过 XGUI_REMOTE_MAX_FRAME_BYTES
 *             直接拒绝(一个字节都不写)。
 * @return     ≥0 已被设备实际接受的字节数(可为部分写; 0=设备暂不可写);
 *             -1 参数非法/超上限/设备硬错误(负返回)。
 */
int64_t XGuiRemoteProto_writeFrame(XIODevice* device, XGuiRemoteMsgType type,
                                   const void* payload, size_t payloadBytes);

/* ==================== 增量帧泵(读侧状态机) ==================== */

/**
 * @brief      增量帧读取器: 把任意长度的新到字节喂进来, 凑满一帧交一帧。
 * @details    典型用法(GUI 线程 poll 回调内):
 *               n = XIODevice_read_1(dev, buf, sizeof(buf));
 *               r = XGuiRemoteFrameReader_feed(&reader, buf, n);
 *               while (r > 0) { 处理 reader.type/reader.payload;
 *                               r = XGuiRemoteFrameReader_feed(&reader, NULL, 0); }
 *             payload 指向 reader 内部缓冲(有效至下一次 feed 返回 >0 或
 *             deinit), 调用方不得保存该指针。内部缓冲按已见最大帧长增长,
 *             硬上限 XGUI_REMOTE_MAX_FRAME_BYTES——超限帧返回 -1(协议错误,
 *             必须断链), 以此防御长度字段伪造的内存放大。
 *             线程约定: 同一 reader 只能被创建它的线程使用(会话中=GUI 线程)。
 */
typedef struct XGuiRemoteFrameReader {
    uint8_t  headerBuf[XGUI_REMOTE_FRAME_HEADER_BYTES]; /**< 帧头攒字节缓冲。 */
    int      headerGot;      /**< 已攒帧头字节数(0..5)。 */
    uint8_t* payload;        /**< 当前帧负载缓冲(内部拥有; 可能随帧长增长)。 */
    size_t   payloadCap;     /**< 负载缓冲容量。 */
    size_t   payloadGot;     /**< 已收负载数。 */
    size_t   payloadLen;     /**< 当前帧负载总长。 */
    XGuiRemoteMsgType type;  /**< 当前完成帧的类型(仅在 feed 返回 >0 后有效)。 */
} XGuiRemoteFrameReader;

/** @brief 初始化帧读取器(零分配; 首帧到来时才按需分配负载缓冲)。 */
void XGuiRemoteFrameReader_init(XGuiRemoteFrameReader* reader);

/** @brief 释放帧读取器内部缓冲; 之后可安全重新 init 复用。 */
void XGuiRemoteFrameReader_deinit(XGuiRemoteFrameReader* reader);

/**
 * @brief      喂入字节推进状态机。
 * @param      data  新到字节(借用); 可为 NULL(dataBytes=0, 用于取走已完成帧
 *             之后继续读——见典型用法; 亦可对半途帧重复喂 0 探询)。
 * @param      dataBytes 字节数。
 * @return     1  完成一帧: reader.type/payload/payloadLen 有效(已自动复位
 *             准备下一帧, 但 payload 内容保持可读到下次 feed 喂入新数据);
 *             0  需要更多字节;
 *             -1 协议错误(帧超限/长度字段非法), 会话必须断链。
 */
int XGuiRemoteFrameReader_feed(XGuiRemoteFrameReader* reader,
                               const uint8_t* data, size_t dataBytes);

/* ==================== 档位参数(两预设 + 自动预留, 见 XGuiRemote.md §5) ==================== */

/**
 * @brief      档位参数结构: 纯数据, 服务器/客户端各自持有副本。
 * @details    运行期切换在帧边界生效; 服务端换档伴随 FB_META 广播, 客户端
 *             收 FB_META 重建本地缓冲。字段夹取范围见 _sanitize。
 */
typedef struct XGuiRemoteProfile {
    XGuiRemotePixelFormat wireFormat;    /**< 线上像素格式。 */
    XGuiRemoteCodecId     codec;         /**< 首选编码(RAW/RLE/ZLIB)。 */
    int                   zlibLevel;     /**< codec==ZLIB 时 1..9, 其它忽略。 */
    int                   tileWidth;     /**< tile 宽(16..512, 建议偶数)。 */
    int                   tileHeight;    /**< tile 高(16..512)。 */
    int                   maxFps;        /**< 服务端推送帧率上限(1..240)。 */
    size_t                encodeQueueBytes; /**< 编码线程→GUI 有界队列字节预算。 */
    size_t                txBudgetBytes; /**< GUI 线程每圈 poll 最大写出字节。 */
    uint32_t              mouseMoveThrottleMs; /**< 鼠标移动合并窗口(按下/释放
                                                永不合并; 0=直传)。 */
    uint32_t              pingIntervalMs; /**< 保活 PING 间隔。 */
    uint32_t              pingTimeoutMs;  /**< 无对端任何帧即判死链的超时。 */
} XGuiRemoteProfile;

/** @brief 性能模式预设(ARGB32/zlib L1/128×128 tile/60fps/大队列)。 */
void XGuiRemoteProfile_initPerformance(XGuiRemoteProfile* out);
/** @brief 资源(嵌入式)模式预设(RGB565/RLE/32×32 tile/15fps/小缓冲)。 */
void XGuiRemoteProfile_initResource(XGuiRemoteProfile* out);
/** @brief 自动模式预设(V2 预留; V1 行为等价 resource 预设)。 */
void XGuiRemoteProfile_initAuto(XGuiRemoteProfile* out);
/**
 * @brief      把档位各字段夹取到合法范围(越界取边界值; 非法枚举回退
 *             线上 ARGB32/编码 RLE)。入库/上网前必须调用。
 */
void XGuiRemoteProfile_sanitize(XGuiRemoteProfile* profile);
/** @brief 校验档位是否在合法范围内(不修改入参)。 */
bool XGuiRemoteProfile_isValid(const XGuiRemoteProfile* profile);

/**
 * @brief      把档位序列化为 PROFILE_SET 自定义块(Custom 档上网用)。
 * @return     写入字节数; 缓冲不足返回 0。
 */
size_t XGuiRemoteProfile_enc(uint8_t* out, size_t cap,
                             const XGuiRemoteProfile* profile);
/** @brief 反序列化 PROFILE_SET 自定义块; 成功返回 true(已 sanitize)。 */
bool XGuiRemoteProfile_dec(const uint8_t* payload, size_t len,
                           XGuiRemoteProfile* out);

/* ==================== 消息结构(定长内联缓冲, 无堆) ==================== */

/** @brief HELLO / HELLO_ACK 共用结构。 */
typedef struct XGuiRemoteMsgHello {
    uint16_t protocolVersion;                 /**< 协议版本。 */
    uint32_t capabilities;                    /**< 能力位(XGUI_REMOTE_CAP_*)。 */
    uint8_t  authMethod;                      /**< 建议值(HELLO)/选定值(ACK)。 */
    uint16_t nameBytes;                       /**< 名称字节数(≤MAX_NAME)。 */
    char     name[XGUI_REMOTE_MAX_NAME_BYTES];/**< 端名(UTF-8, 非终止符保证)。 */
} XGuiRemoteMsgHello;

/** @brief AUTH_CHALLENGE。 */
typedef struct XGuiRemoteMsgAuthChallenge {
    uint8_t  method;                          /**< 认证方法。 */
    uint16_t nonceBytes;                      /**< 恒 AUTH_NONCE_BYTES。 */
    uint8_t  nonce[XGUI_REMOTE_AUTH_NONCE_BYTES];
} XGuiRemoteMsgAuthChallenge;

/** @brief AUTH_RESPONSE。 */
typedef struct XGuiRemoteMsgAuthResponse {
    uint16_t responseBytes;                   /**< 恒 AUTH_RESPONSE_BYTES。 */
    uint8_t  response[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
} XGuiRemoteMsgAuthResponse;

/** @brief AUTH_RESULT。 */
typedef struct XGuiRemoteMsgAuthResult {
    uint8_t  ok;                              /**< 1 通过 / 0 拒绝(拒绝后 BYE)。 */
    uint16_t textBytes;                       /**< 文本字节数(≤MAX_MSG)。 */
    char     text[XGUI_REMOTE_MAX_MSG_BYTES];
} XGuiRemoteMsgAuthResult;

/** @brief FB_META(画面元信息; 尺寸/格式/tile 变化时重发)。 */
typedef struct XGuiRemoteMsgFbMeta {
    uint16_t width;                           /**< 远端窗口宽(逻辑像素)。 */
    uint16_t height;                          /**< 远端窗口高。 */
    uint8_t  format;                          /**< XGuiRemotePixelFormat。 */
    uint16_t tileWidth;                       /**< tile 宽。 */
    uint16_t tileHeight;                      /**< tile 高。 */
    uint8_t  profileId;                       /**< 当前档位 id。 */
    uint8_t  flags;                           /**< bit0=标题字段有效。 */
    uint16_t titleBytes;                      /**< 标题字节数(≤MAX_NAME)。 */
    char     title[XGUI_REMOTE_MAX_NAME_BYTES];
} XGuiRemoteMsgFbMeta;

/** @brief FB_REQUEST。 */
typedef struct XGuiRemoteMsgFbRequest {
    uint8_t  mode;                            /**< 0=续增量, 1=全量刷新。 */
    uint16_t x, y, w, h;                      /**< 区域(预留, 0=整幅)。 */
} XGuiRemoteMsgFbRequest;

/** @brief FB_UPDATE 帧级头(其后随 tileCount 个 tile 记录)。 */
typedef struct XGuiRemoteMsgFbUpdate {
    uint32_t sequence;                        /**< 单调递增序号(允许回绕)。 */
    uint16_t tileCount;                       /**< 本帧 tile 记录数。 */
    uint8_t  format;                          /**< XGuiRemotePixelFormat。 */
    uint8_t  flags;                           /**< 保留(恒 0)。 */
} XGuiRemoteMsgFbUpdate;

/** @brief FB_UPDATE 内单条 tile 记录。 */
typedef struct XGuiRemoteMsgFbTile {
    uint16_t x, y;                            /**< tile 左上角(窗口坐标)。 */
    uint16_t w, h;                            /**< tile 尺寸。 */
    uint8_t  codec;                           /**< XGuiRemoteCodecId。 */
    uint8_t  flags;                           /**< XGUI_REMOTE_TILE_FLAG_*。 */
    const uint8_t* payload;                   /**< 编码像素(借用, 指向读缓冲)。 */
    uint32_t payloadBytes;                    /**< 编码后字节数。 */
} XGuiRemoteMsgFbTile;

/** @brief FB_ACK(V2 流控预留, V1 不发不收)。 */
typedef struct XGuiRemoteMsgFbAck {
    uint32_t ackSequence;                     /**< 已完整收到的最大序号。 */
    uint32_t windowBytes;                     /**< 接收窗口剩余字节。 */
} XGuiRemoteMsgFbAck;

/** @brief INPUT_KEY。 */
typedef struct XGuiRemoteMsgInputKey {
    uint8_t  action;                          /**< KEY_PRESS / KEY_RELEASE。 */
    uint32_t key;                             /**< XKey 枚举/ASCII 码位原值。 */
    uint32_t modifiers;                       /**< 修饰掩码(原值透传)。 */
    uint32_t nativeScanCode;                  /**< 平台扫描码(未知 0)。 */
    uint32_t timestampMs;                     /**< 事件时间戳(未知 0)。 */
} XGuiRemoteMsgInputKey;

/** @brief INPUT_POINTER。坐标 i16: 按下抓取期间允许越界(负值/超窗)。 */
typedef struct XGuiRemoteMsgInputPointer {
    uint8_t  action;                          /**< PTR_MOVE/PRESS/RELEASE/DBL_CLICK。 */
    uint8_t  button;                          /**< 触发键(XGUI_REMOTE_BTN_*)。 */
    uint16_t buttons;                         /**< 按下键集合掩码。 */
    uint32_t modifiers;                       /**< 修饰掩码。 */
    int16_t  x, y;                            /**< 远端窗口客户区局部坐标。 */
    uint32_t timestampMs;                     /**< 事件时间戳。 */
} XGuiRemoteMsgInputPointer;

/** @brief INPUT_WHEEL(angleDelta 遵循 ±120/格 Qt 约定)。 */
typedef struct XGuiRemoteMsgInputWheel {
    int16_t  angleX, angleY;                  /**< 角度增量。 */
    int16_t  pixelX, pixelY;                  /**< 像素增量(未知 0)。 */
    uint16_t buttons;                         /**< 按下键集合掩码。 */
    uint32_t modifiers;                       /**< 修饰掩码。 */
    int16_t  x, y;                            /**< 远端窗口局部坐标。 */
    uint32_t timestampMs;                     /**< 事件时间戳。 */
} XGuiRemoteMsgInputWheel;

/** @brief 触摸单点(per-id; id 整条序列稳定, 压力 Q8=pressure×255)。 */
typedef struct XGuiRemoteMsgTouchPoint {
    uint32_t id;                              /**< 触点 id。 */
    uint8_t  state;                           /**< TP_PRESSED/.../RELEASED。 */
    int16_t  x, y;                            /**< 远端窗口局部坐标。 */
    uint8_t  pressureQ8;                      /**< 压力(0..255; 无压力 255)。 */
    uint8_t  reserved;                        /**< 对齐保留(恒 0)。 */
} XGuiRemoteMsgTouchPoint;

/** @brief INPUT_TOUCH(多点; 主点必须放 points[0], 对齐注入契约)。 */
typedef struct XGuiRemoteMsgInputTouch {
    uint8_t  action;                          /**< TOUCH_BEGIN/UPDATE/END/CANCEL。 */
    uint8_t  pointCount;                      /**< ≤MAX_TOUCH_POINTS。 */
    uint32_t timestampMs;                     /**< 事件时间戳。 */
    XGuiRemoteMsgTouchPoint points[XGUI_REMOTE_MAX_TOUCH_POINTS];
} XGuiRemoteMsgInputTouch;

/** @brief INPUT_IME(preedit/commit 为 UTF-8 原文; 坐标类字段为 preedit
 *         内部量, 语义对齐 XInputMethodEvent)。 */
typedef struct XGuiRemoteMsgInputIme {
    uint16_t preeditBytes;                    /**< 组合文本字节数(≤MAX_TEXT)。 */
    char     preedit[XGUI_REMOTE_MAX_TEXT_BYTES];
    uint16_t commitBytes;                     /**< 提交文本字节数(≤MAX_TEXT)。 */
    char     commit[XGUI_REMOTE_MAX_TEXT_BYTES];
    int32_t  replacementStart;                /**< 相对光标的替换起点。 */
    int32_t  replacementLength;               /**< 替换长度。 */
    int32_t  cursorPosition;                  /**< -1 未知。 */
    int32_t  anchorPosition;                  /**< -1 未知。 */
} XGuiRemoteMsgInputIme;

/** @brief PROFILE_SET(自定义档时 profile 字段有效)。 */
typedef struct XGuiRemoteMsgProfileSet {
    uint8_t  profileId;                       /**< XGuiRemoteProfileId。 */
    uint8_t  hasCustomProfile;                /**< 1 时 profile 有效。 */
    XGuiRemoteProfile profile;                /**< 自定义参数(hasCustom=1)。 */
} XGuiRemoteMsgProfileSet;

/** @brief PROFILE_RESULT。 */
typedef struct XGuiRemoteMsgProfileResult {
    uint8_t  accepted;                        /**< 1 接受 / 0 拒绝(维持原档)。 */
    uint8_t  profileId;                       /**< 生效档位 id。 */
} XGuiRemoteMsgProfileResult;

/** @brief PING / PONG 共用(u64 毫秒时间戳; PONG 回显对端值)。 */
typedef struct XGuiRemoteMsgPing {
    uint64_t timestampMs;
} XGuiRemoteMsgPing;

/** @brief BYE。 */
typedef struct XGuiRemoteMsgBye {
    uint8_t  reason;                          /**< XGuiRemoteByeReason。 */
    uint16_t textBytes;                       /**< 文本字节数(≤MAX_MSG)。 */
    char     text[XGUI_REMOTE_MAX_MSG_BYTES];
} XGuiRemoteMsgBye;

/** @brief ERROR。 */
typedef struct XGuiRemoteMsgError {
    uint16_t code;                            /**< XGuiRemoteError。 */
    uint16_t textBytes;                       /**< 文本字节数(≤MAX_MSG)。 */
    char     text[XGUI_REMOTE_MAX_MSG_BYTES];
} XGuiRemoteMsgError;

/* ==================== 消息编解码(纯函数, 统一约定) ==================== */
/*
 * enc 系列: 序列化到调用方缓冲。返回写入字节数; 缓冲不足/参数非法返回 0
 *           (不产生半写)。所有字符串字段按"长度前缀+字节"编码, 超长截断。
 * dec 系列: 从负载缓冲解析到结构体。返回 true 成功(结构体字段完整);
 *           false = 负载长度不符/字段越界/枚举值域非法(协议错误,
 *           调用方应断链)。解析器不信任任何长度字段: 逐字段核对剩余
 *           空间后才推进。
 * 枚举值域校验(冻结口径): 封闭枚举字段——format/codec/action(PTR/KEY/
 *           TOUCH)/mode(FB_REQUEST)/method(认证)/profileId——出现协议外
 *           取值时 dec 返回 false, 会话按协议错误断链; 开放字段——flags/
 *           保留位/BYE reason/ERROR code——不校验, 未知值原样透传或归化
 *           为通用值, 为前向兼容留空间(配合帧层"未知名节跳过")。
 */

size_t XGuiRemoteProto_encHello(uint8_t* out, size_t cap,
                                const XGuiRemoteMsgHello* msg);
bool XGuiRemoteProto_decHello(const uint8_t* payload, size_t len,
                              XGuiRemoteMsgHello* out);

size_t XGuiRemoteProto_encAuthChallenge(uint8_t* out, size_t cap,
                                        const XGuiRemoteMsgAuthChallenge* msg);
bool XGuiRemoteProto_decAuthChallenge(const uint8_t* payload, size_t len,
                                      XGuiRemoteMsgAuthChallenge* out);

size_t XGuiRemoteProto_encAuthResponse(uint8_t* out, size_t cap,
                                       const XGuiRemoteMsgAuthResponse* msg);
bool XGuiRemoteProto_decAuthResponse(const uint8_t* payload, size_t len,
                                     XGuiRemoteMsgAuthResponse* out);

size_t XGuiRemoteProto_encAuthResult(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgAuthResult* msg);
bool XGuiRemoteProto_decAuthResult(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgAuthResult* out);

size_t XGuiRemoteProto_encFbMeta(uint8_t* out, size_t cap,
                                 const XGuiRemoteMsgFbMeta* msg);
bool XGuiRemoteProto_decFbMeta(const uint8_t* payload, size_t len,
                               XGuiRemoteMsgFbMeta* out);

size_t XGuiRemoteProto_encFbRequest(uint8_t* out, size_t cap,
                                    const XGuiRemoteMsgFbRequest* msg);
bool XGuiRemoteProto_decFbRequest(const uint8_t* payload, size_t len,
                                  XGuiRemoteMsgFbRequest* out);

/** @brief 序列化 FB_UPDATE 帧级头(恒 XGUI_REMOTE_FB_UPDATE_HEADER_BYTES
 *         字节; 随后由 encFbTile 逐条追加 tile 记录)。 */
size_t XGuiRemoteProto_encFbUpdate(uint8_t* out, size_t cap,
                                   const XGuiRemoteMsgFbUpdate* msg);
bool XGuiRemoteProto_decFbUpdate(const uint8_t* payload, size_t len,
                                 XGuiRemoteMsgFbUpdate* out);

/**
 * @brief       追加一条 tile 记录到 FB_UPDATE 负载(紧跟帧级头或上一条)。
 * @param       encodedPixels 编码像素(借用; 仅被拷贝进 out)。
 * @return      写入字节数; 缓冲不足返回 0。
 */
size_t XGuiRemoteProto_encFbTile(uint8_t* out, size_t cap,
                                 const XGuiRemoteMsgFbTile* tile,
                                 const uint8_t* encodedPixels);

/**
 * @brief       从 FB_UPDATE 负载中按偏移解析一条 tile 记录。
 * @param       offset     起始偏移(相对 FB_UPDATE 负载; 首条传
 *                          XGUI_REMOTE_FB_UPDATE_HEADER_BYTES, 之后传上一次
 *                          返回的 *nextOffset, 直至 tileCount 条解析完)。
 * @param       tileOut    输出记录(payload 指向 payload 内部, 借用)。
 * @param       nextOffset 输出下一条偏移; 可为 NULL。
 * @return      true 解析成功; false 偏移越界/记录截断(协议错误)。
 */
bool XGuiRemoteProto_decFbTile(const uint8_t* payload, size_t len, size_t offset,
                               XGuiRemoteMsgFbTile* tileOut, size_t* nextOffset);

size_t XGuiRemoteProto_encFbAck(uint8_t* out, size_t cap,
                                const XGuiRemoteMsgFbAck* msg);
bool XGuiRemoteProto_decFbAck(const uint8_t* payload, size_t len,
                              XGuiRemoteMsgFbAck* out);

size_t XGuiRemoteProto_encInputKey(uint8_t* out, size_t cap,
                                   const XGuiRemoteMsgInputKey* msg);
bool XGuiRemoteProto_decInputKey(const uint8_t* payload, size_t len,
                                 XGuiRemoteMsgInputKey* out);

size_t XGuiRemoteProto_encInputPointer(uint8_t* out, size_t cap,
                                       const XGuiRemoteMsgInputPointer* msg);
bool XGuiRemoteProto_decInputPointer(const uint8_t* payload, size_t len,
                                     XGuiRemoteMsgInputPointer* out);

size_t XGuiRemoteProto_encInputWheel(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgInputWheel* msg);
bool XGuiRemoteProto_decInputWheel(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgInputWheel* out);

size_t XGuiRemoteProto_encInputTouch(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgInputTouch* msg);
bool XGuiRemoteProto_decInputTouch(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgInputTouch* out);

size_t XGuiRemoteProto_encInputIme(uint8_t* out, size_t cap,
                                   const XGuiRemoteMsgInputIme* msg);
bool XGuiRemoteProto_decInputIme(const uint8_t* payload, size_t len,
                                 XGuiRemoteMsgInputIme* out);

size_t XGuiRemoteProto_encProfileSet(uint8_t* out, size_t cap,
                                     const XGuiRemoteMsgProfileSet* msg);
bool XGuiRemoteProto_decProfileSet(const uint8_t* payload, size_t len,
                                   XGuiRemoteMsgProfileSet* out);

size_t XGuiRemoteProto_encProfileResult(uint8_t* out, size_t cap,
                                        const XGuiRemoteMsgProfileResult* msg);
bool XGuiRemoteProto_decProfileResult(const uint8_t* payload, size_t len,
                                      XGuiRemoteMsgProfileResult* out);

size_t XGuiRemoteProto_encPing(uint8_t* out, size_t cap, uint64_t timestampMs);
bool XGuiRemoteProto_decPing(const uint8_t* payload, size_t len,
                             uint64_t* timestampMsOut);

size_t XGuiRemoteProto_encBye(uint8_t* out, size_t cap,
                              const XGuiRemoteMsgBye* msg);
bool XGuiRemoteProto_decBye(const uint8_t* payload, size_t len,
                            XGuiRemoteMsgBye* out);

size_t XGuiRemoteProto_encError(uint8_t* out, size_t cap,
                                const XGuiRemoteMsgError* msg);
bool XGuiRemoteProto_decError(const uint8_t* payload, size_t len,
                              XGuiRemoteMsgError* out);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUIREMOTEPROTO_H */
