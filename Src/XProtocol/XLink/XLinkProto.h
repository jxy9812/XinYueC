/** @file       XLinkProto.h
 * @brief      XLink 私有应用层协议——线上格式定义（消息类枚举/帧头/常量）。
 * @details    对齐 MQTT 3.1.1 的消息分类思想（Request/Response/Publish/Subscribe），
 *             但为 XinYueC 自有格式，不兼容任何公开协议规范。实现只依赖 XinYueC
 *             抽象层（XIODevice/XMemory），禁止调用 Win32、POSIX 或其他平台 API。
 *
 *             帧结构（小端线序，定长帧头 12 字节）：
 * @code
 *   偏移  长度  字段        说明
 *   0     4     payloadLen  载荷字节数（不含帧头 12 字节），须 <= XLINK_MAX_PAYLOAD
 *   4     2     msgClass    消息类（XLinkMsgClass），决定语义与应答义务
 *   6     2     msgCode     业务码：Request/Response = 命令号，
 *                            Publish/Subscribe/Unsubscribe = topicId 低 16 位，
 *                            Session = XLinkSessionCode
 *   8     4     requestId   Request/Response 关联 id（两端各自独立自增）；其余类填 0
 *   12    N     payload     载荷（按字段逐个小端序列化，禁止 memcpy 结构体上线）
 * @endcode
 *
 *             【topicId 承载规则】msgCode 线上仅 u16，而业务 topicId 区间自
 *             0x10000 起（已越出 u16 表示范围），故 Publish/Subscribe/Unsubscribe
 *             三类的完整 u32 topicId 一律另置于载荷首 4 字节（小端），msgCode
 *             只承载其低 16 位供快速匹配与统计；解析侧一律以载荷中的 u32 为准。
 *
 *             设计要点：
 *             - 消息类正交而非逐条枚举业务语义。Request/Response 管"我问你要答"
 *               （双向均可发起），Publish 管"我单方面告诉你"，Subscribe/Unsubscribe
 *               管"以后这类推给我"。新增业务形态只表现为新增 msgCode/topicId，
 *               协议层与编解码层零改动。
 *             - requestId 两端各自自增（互不干扰），Response 原样回带对端的 id
 *               做应答关联；待应答由请求侧本地表跟踪与超时（见 XLinkSession.h）。
 */
#ifndef XLINKPROTO_H
#define XLINKPROTO_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入本协议配置，确保 XLINK_ON 已定义（必须早于下方条件编译判定） */
#include "XLink_config.h"

#if XLINK_ON

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*                        协议常量                                           */
/* ========================================================================== */
/* 【2026-10-31 重复宏合并】XLINK_VERSION / XLINK_HEADER_SIZE /
 * XLINK_TOPIC_USER_BASE 唯一权威 = XLink_config.h（上方 :40 已先包含——
 * 原先本文件与配置文件双侧定义，HEADER_SIZE/TOPIC_USER_BASE 两侧均无
 * 守卫，值一旦单侧漂移即 C4005/冲突；含 1.1 变更说明的版本契约文档
 * 已随迁 XLink_config.h）。 */

/* ========================================================================== */
/*                        消息类                                             */
/* ========================================================================== */

/**
 * @brief 消息类：帧的语义类别（线上 msgClass 字段取值）。
 * @details 数值即为线上字节值，0 起连续编号，追加新类只能往后加，不得改动既有值。
 *          非位域，不可按位组合；一帧有且只有一个消息类。
 */
typedef enum XLinkMsgClass {
    XLinkMsgClass_Session     = 0, /**< 会话管理帧（双向对称）：Hello/HelloAck/Heartbeat/Goodbye，
                                       子类型见 XLinkSessionCode。 */
    XLinkMsgClass_Request     = 1, /**< 请求帧（双向对称）：发出后必须收到同 requestId 的
                                       Response 才算闭环；待应答跟踪与超时由请求侧负责。 */
    XLinkMsgClass_Response    = 2, /**< 应答帧（双向对称）：回带对端的 requestId 与相同 msgCode，
                                       载荷承载返回值或错误码。 */
    XLinkMsgClass_Publish     = 3, /**< 推送帧（双向对称）：单方面通知，不等应答，产生即发。 */
    XLinkMsgClass_Subscribe   = 4, /**< 订阅帧（双向对称）：订阅方请求对端按 topicId 推送。 */
    XLinkMsgClass_Unsubscribe = 5, /**< 退订帧（双向对称）：取消此前建立的订阅关系。 */
    XLinkMsgClass_File        = 6  /**< 文件传输帧（双向对称，XLINK_FILE_ON）：
                                       msgCode = XLinkFileCode，requestId = transferId。
                                       由文件传输引擎消费，不投递给业务分发回调。 */
} XLinkMsgClass;

/* ========================================================================== */
/*                        会话子类型                                         */
/* ========================================================================== */

/**
 * @brief 会话管理子类型：线上 msgClass=XLinkMsgClass_Session 时的 msgCode 取值。
 * @details 数值即为线上字节值。非位域，不可按位组合。
 */
typedef enum XLinkSessionCode {
    XLinkSessionCode_Hello     = 1, /**< 建链后首帧（双向对称）：携版本与能力位。 */
    XLinkSessionCode_HelloAck  = 2, /**< 对 Hello 的应答；能力位交集在此确定，
                                        约定可携带全量快照（可选载荷）。 */
    XLinkSessionCode_Heartbeat = 3, /**< 心跳（双向对称）：静默超时即认定链路断开。 */
    XLinkSessionCode_Goodbye   = 4, /**< 主动断链通知（双向对称）：正常关闭前发出，便于对端快速感知。
                                        可选载荷首字节为原因（XLinkGoodbyeReason）。 */
    XLinkSessionCode_AuthRequest   = 5, /**< 登录请求（客户端 → 服务端，XLINK_AUTH_ON）：
                                             载荷首字节为用户名长度，其后为 UTF-8 用户名。 */
    XLinkSessionCode_AuthChallenge = 6, /**< 登录挑战（服务端 → 客户端，XLINK_AUTH_ON）：
                                             盐、迭代次数与 nonce。 */
    XLinkSessionCode_AuthResponse  = 7, /**< 登录应答（客户端 → 服务端，XLINK_AUTH_ON）：
                                             挑战应答摘要。 */
    XLinkSessionCode_AuthResult    = 8  /**< 登录结果（服务端 → 客户端，XLINK_AUTH_ON）：
                                             结果码、角色与服务端证明。 */
} XLinkSessionCode;

/**
 * @brief Goodbye 载荷首字节的断链原因（可选；无载荷等价于 Normal）。
 * @details 追加式扩展：老对端忽略附加载荷，行为与无原因时一致。
 */
typedef enum XLinkGoodbyeReason {
    XLinkGoodbyeReason_Normal      = 0, /**< 正常关闭。 */
    XLinkGoodbyeReason_Protocol    = 1, /**< 协议违例。 */
    XLinkGoodbyeReason_AuthFailed  = 2, /**< 认证失败（口令错误/失败次数达上限）。 */
    XLinkGoodbyeReason_AuthRequired = 3, /**< 对端要求认证但本端无凭据。 */
    XLinkGoodbyeReason_Timeout     = 4, /**< 握手/认证超时。 */
    XLinkGoodbyeReason_ServerShutdown = 5 /**< 服务端关闭（全员下线）。 */
} XLinkGoodbyeReason;

/* ========================================================================== */
/*                        能力位                                             */
/* ========================================================================== */

/**
 * @brief 能力位：Hello/HelloAck 载荷中的 m_capability（线上 u16），按位或组合。
 * @details 双方各自声明己身能力，握手后取按位与作为协商结果；未置位的特性对端
 *           不得依赖（如未协商 XLINK_CAP_Zlib 则载荷必须为原始字节）。
 */
typedef enum XLinkCapability {
    XLinkCapability_None           = 0x0000u, /**< 无任何可选能力（等价于未置任何位）。 */
    XLinkCapability_Zlib           = 0x0001u, /**< 支持 zlib 载荷压缩（协商后生效）。 */
    XLinkCapability_Tls            = 0x0002u, /**< 链路已运行在 TLS 传输之上
                                                 （由传输层保证，非本层实现）。 */
    XLinkCapability_Snapshot       = 0x0004u, /**< HelloAck 载荷内携带全量快照（重连即恢复画面）。 */
    XLinkCapability_RequestTimeout = 0x0008u, /**< 支持请求超时通知（对端待应答表到期上报）。 */
    XLinkCapability_Auth           = 0x0010u, /**< 支持登录认证（XLINK_AUTH_ON）。
                                                   服务端仅在确实配置了口令/账户库时置位；
                                                   双方交集含本位时，握手后进入认证流程。 */
    XLinkCapability_FileTransfer   = 0x0020u  /**< 支持文件传输（XLINK_FILE_ON）。
                                                   交集含本位才允许收发 File 消息类。 */
} XLinkCapability;

/**
 * @brief 登录角色：认证成功后由服务端在 AuthResult 中下发。
 * @details 角色决定业务层权限；协议层只负责传递与保存，不解释权限范围。
 */
typedef enum XLinkAuthRole {
    XLinkAuthRole_Anonymous = 0, /**< 匿名（未启用认证时的默认角色）。 */
    XLinkAuthRole_User      = 1, /**< 普通用户。 */
    XLinkAuthRole_Admin     = 2  /**< 管理员。 */
} XLinkAuthRole;

/**
 * @brief 登录结果码（AuthResult 载荷首字节）。
 */
typedef enum XLinkAuthStatus {
    XLinkAuthStatus_Ok          = 0, /**< 认证通过。 */
    XLinkAuthStatus_BadUser     = 1, /**< 用户不存在。 */
    XLinkAuthStatus_BadPassword = 2, /**< 口令错误。 */
    XLinkAuthStatus_Locked      = 3, /**< 账户已锁定。 */
    XLinkAuthStatus_Rejected    = 4, /**< 通用拒绝（失败次数达上限等）。 */
    XLinkAuthStatus_NoCredential = 5, /**< 服务端未配置认证（不应发生）。 */
    XLinkAuthStatus_Timeout     = 6, /**< 认证超时。 */
    XLinkAuthStatus_Protocol    = 7  /**< 认证帧畸形。 */
} XLinkAuthStatus;

/* ========================================================================== */
/*                        载荷结构                                           */
/* ========================================================================== */

/**
 * @brief Hello / HelloAck 载荷。
 * @details 线上为 version(2) + capability(2) 共 4 字节小端；可选在其后追加
 *          快照字节（长度由帧头 payloadLen 推导）。禁止 memcpy 本结构体上线。
 */
typedef struct XLinkHelloPayload {
    uint16_t m_version;    /**< 协议版本，见 XLINK_VERSION。 */
    uint16_t m_capability; /**< 己身能力位，按位或，见 XLinkCapability。 */
} XLinkHelloPayload;

/**
 * @brief 解析/构造后的帧头（已脱离线上字节序，编解码见 XLinkCodec.h）。
 */
typedef struct XLinkFrameHeader {
    uint32_t m_payloadLen; /**< 载荷字节数；须 <= XLINK_MAX_PAYLOAD，解码时校验。 */
    uint16_t m_msgClass;   /**< 消息类，见 XLinkMsgClass。 */
    uint16_t m_msgCode;    /**< 业务码：Request/Response 为命令号，Publish/Subscribe
                                为 topicId，Session 为 XLinkSessionCode，File 为
                                XLinkFileCode。 */
    uint32_t m_requestId;  /**< Request/Response 关联 id；File 类为 transferId；
                                其余消息类填 0。 */
} XLinkFrameHeader;

/* ========================================================================== */
/*                  登录认证扩展（XLINK_AUTH_ON）                            */
/* ========================================================================== */

#if XLINK_AUTH_ON

/**
 * @brief 登录认证方法（AuthChallenge 载荷首字节）。
 * @details 预留扩展位：未来可加 SCRAM 等更强方法，由挑战方选定。
 */
typedef enum XLinkAuthMethod {
    XLinkAuthMethod_IteratedHmac = 1 /**< H = 迭代 HMAC-SHA256(口令, 盐)；
                                          response = HMAC-SHA256(H, nonce)；
                                          serverProof = HMAC-SHA256(H, nonce||0x01)。 */
} XLinkAuthMethod;

/**
 * @brief AuthRequest 解析结果（用户名走内联缓冲，无堆）。
 * @details 线上布局：[u8 nameLen][name...]（UTF-8，非终止符保证）。
 */
typedef struct XLinkAuthRequestPayload {
    uint8_t m_nameLen;                        /**< 用户名有效字节数。 */
    char    m_name[XLINK_AUTH_NAME_BYTES];    /**< 用户名（UTF-8，非终止符保证）。 */
} XLinkAuthRequestPayload;

/**
 * @brief AuthChallenge 解析结果。
 * @details 线上布局（小端）：
 *          [u8 method][u8 saltLen][u16 reserved][u32 iterations]
 *          [salt...][nonce 32]
 */
typedef struct XLinkAuthChallengePayload {
    uint8_t  m_method;                        /**< 认证方法，见 XLinkAuthMethod。 */
    uint8_t  m_saltLen;                       /**< 盐字节数（须 == XLINK_AUTH_SALT_BYTES）。 */
    uint16_t m_reserved;                      /**< 保留位，必须为 0。 */
    uint32_t m_iterations;                    /**< 口令派生迭代次数（>= 1）。 */
    uint8_t  m_salt[XLINK_AUTH_SALT_BYTES];   /**< 随机盐。 */
    uint8_t  m_nonce[XLINK_AUTH_NONCE_BYTES]; /**< 每连接随机挑战数（防重放）。 */
} XLinkAuthChallengePayload;

/**
 * @brief AuthResponse 解析结果。
 * @details 线上布局：[u8 method][u8 reserved][u16 len][response...]。
 */
typedef struct XLinkAuthResponsePayload {
    uint8_t m_method;                          /**< 回显挑战方法。 */
    uint8_t m_reserved;                        /**< 保留位，必须为 0。 */
    uint16_t m_len;                            /**< 摘要字节数（须 == XLINK_AUTH_NONCE_BYTES）。 */
    uint8_t m_response[XLINK_AUTH_NONCE_BYTES];/**< HMAC-SHA256(H, nonce)。 */
} XLinkAuthResponsePayload;

/**
 * @brief AuthResult 解析结果。
 * @details 线上布局：
 *          [u8 status][u8 role][u16 textLen][text...][serverProof 32]
 *          serverProof 仅 status==Ok 时有效（失败时为全 0）。
 */
typedef struct XLinkAuthResultPayload {
    uint8_t  m_status;                          /**< 结果码，见 XLinkAuthStatus。 */
    uint8_t  m_role;                            /**< 角色，见 XLinkAuthRole。 */
    uint16_t m_textLen;                         /**< 结果文本字节数（<= XLINK_AUTH_TEXT_BYTES）。 */
    char     m_text[XLINK_AUTH_TEXT_BYTES];     /**< 结果文本（UTF-8，非终止符保证）。 */
    uint8_t  m_serverProof[XLINK_AUTH_NONCE_BYTES]; /**< 服务端证明（status==Ok 有效）。 */
} XLinkAuthResultPayload;

#endif /* XLINK_AUTH_ON */

/* ========================================================================== */
/*                  文件传输扩展（XLINK_FILE_ON）                            */
/* ========================================================================== */

#if XLINK_FILE_ON

/**
 * @brief 文件传输操作码（XLinkMsgClass_File 的 msgCode）。
 * @details 数值即线上字节值，追加式扩展只能往后加。
 */
typedef enum XLinkFileCode {
    XLinkFileCode_Open      = 1, /**< 发起方 → 接收方：请求开启传输（含文件名/大小/方向）。 */
    XLinkFileCode_OpenAck   = 2, /**< 接收方 → 发起方：受理结果 + 协商块大小/窗口/起始偏移。 */
    XLinkFileCode_Data      = 3, /**< 发起方 → 接收方：数据块（[u64 offset][bytes...]）。 */
    XLinkFileCode_Ack       = 4, /**< 接收方 → 发起方：确认进度 + 新授信额度。 */
    XLinkFileCode_Finish    = 5, /**< 发起方 → 接收方：传输完成 + 整文件 SHA-256。 */
    XLinkFileCode_FinishAck = 6, /**< 接收方 → 发起方：校验与落盘结果。 */
    XLinkFileCode_Abort     = 7  /**< 双向：中止传输（含原因）。 */
} XLinkFileCode;

/**
 * @brief 传输方向（Open 载荷 m_mode）。
 */
typedef enum XLinkFileMode {
    XLinkFileMode_Upload   = 0, /**< 发起方发送文件，接收方落盘。 */
    XLinkFileMode_Download = 1  /**< 发起方请求接收方发送文件（接收方落盘到发起方）。 */
} XLinkFileMode;

/**
 * @brief 传输结果/中止原因码。
 */
typedef enum XLinkFileStatus {
    XLinkFileStatus_Ok            = 0, /**< 成功。 */
    XLinkFileStatus_Rejected      = 1, /**< 接收方策略拒绝（未授权/方向禁用）。 */
    XLinkFileStatus_NotFound      = 2, /**< 文件不存在（下载）。 */
    XLinkFileStatus_AlreadyExists = 3, /**< 目标已存在且策略不允许覆盖。 */
    XLinkFileStatus_TooLarge      = 4, /**< 超过大小上限。 */
    XLinkFileStatus_NoSpace       = 5, /**< 磁盘空间不足/写入失败。 */
    XLinkFileStatus_Checksum      = 6, /**< 整文件 SHA-256 不符。 */
    XLinkFileStatus_Protocol      = 7, /**< 协议违例（畸形载荷/越界偏移）。 */
    XLinkFileStatus_Aborted       = 8, /**< 对端主动中止。 */
    XLinkFileStatus_Timeout       = 9, /**< 空闲超时。 */
    XLinkFileStatus_RoleDenied    = 10 /**< 当前角色无文件传输权限。 */
} XLinkFileStatus;

/**
 * @brief Open 载荷解析结果。
 * @details 线上布局（小端）：
 *          [u8 mode][u8 flags][u16 nameLen][u32 chunkBytes][u32 windowBytes]
 *          [u64 totalSize][u64 startOffset][name...]
 *          flags bit0 = 允许覆盖已有文件。
 */
typedef struct XLinkFileOpenPayload {
    uint8_t  m_mode;                          /**< 传输方向，见 XLinkFileMode。 */
    uint8_t  m_flags;                         /**< bit0 = 允许覆盖。 */
    uint16_t m_nameLen;                       /**< 文件名有效字节数。 */
    uint32_t m_chunkBytes;                    /**< 发起方建议的块大小（接收方可下调）。 */
    uint32_t m_windowBytes;                   /**< 发起方建议的流控窗口。 */
    uint64_t m_totalSize;                     /**< 文件总字节数。 */
    uint64_t m_startOffset;                   /**< 断点续传起始偏移（0 = 从头）。 */
    char     m_name[XLINK_FILE_NAME_BYTES];   /**< 远程文件名（相对路径，UTF-8，非终止符保证）。 */
} XLinkFileOpenPayload;

/**
 * @brief OpenAck 载荷解析结果。
 * @details 线上布局（小端）：
 *          [u8 accepted][u8 status][u16 textLen][u32 chunkBytes]
 *          [u32 windowBytes][u64 startOffset][u64 totalSize][text...]
 * @note   totalSize 由接收方在下载方向填入（发起方 Open 时并不知道文件
 *         多大），上传方向可置 0（发起方本已知晓）。
 */
typedef struct XLinkFileOpenAckPayload {
    uint8_t  m_accepted;                      /**< 1 = 受理；0 = 拒绝（看 status）。 */
    uint8_t  m_status;                        /**< 结果码，见 XLinkFileStatus。 */
    uint16_t m_textLen;                       /**< 文本字节数。 */
    uint32_t m_chunkBytes;                    /**< 协商后的块大小。 */
    uint32_t m_windowBytes;                   /**< 协商后的流控窗口。 */
    uint64_t m_startOffset;                   /**< 接收方接受的起始偏移（断点续传）。 */
    uint64_t m_totalSize;                     /**< 文件实际总字节数（下载方向必填）。 */
    char     m_text[XLINK_FILE_TEXT_BYTES];   /**< 结果文本（UTF-8，非终止符保证）。 */
} XLinkFileOpenAckPayload;

/**
 * @brief Ack 载荷解析结果。
 * @details 线上布局（小端）：[u64 nextOffset][u32 creditBytes]。
 */
typedef struct XLinkFileAckPayload {
    uint64_t m_nextOffset;                    /**< 接收方期望的下一个字节偏移。 */
    uint32_t m_creditBytes;                   /**< 新授予的窗口额度（字节）。 */
} XLinkFileAckPayload;

/**
 * @brief Finish 载荷解析结果：整文件 SHA-256 摘要。
 */
typedef struct XLinkFileFinishPayload {
    uint64_t m_totalSize;                     /**< 文件总字节数（校验一致性）。 */
    uint8_t  m_sha256[XLINK_AUTH_NONCE_BYTES];/**< 整文件 SHA-256。 */
} XLinkFileFinishPayload;

/**
 * @brief FinishAck 载荷解析结果。
 * @details 线上布局：[u8 ok][u8 status][u16 textLen][text...]。
 */
typedef struct XLinkFileFinishAckPayload {
    uint8_t  m_ok;                            /**< 1 = 校验并落盘成功。 */
    uint8_t  m_status;                        /**< 结果码，见 XLinkFileStatus。 */
    uint16_t m_textLen;                       /**< 文本字节数。 */
    char     m_text[XLINK_FILE_TEXT_BYTES];   /**< 结果文本（UTF-8，非终止符保证）。 */
} XLinkFileFinishAckPayload;

/**
 * @brief Abort 载荷解析结果。
 * @details 线上布局：[u8 status][u8 flag][u16 textLen][text...]。
 */
typedef struct XLinkFileAbortPayload {
    uint8_t  m_status;                        /**< 中止原因，见 XLinkFileStatus。 */
    uint8_t  m_byPeer;                        /**< 1 = 对端发起的中止（本地回调区分用）。 */
    uint16_t m_textLen;                       /**< 文本字节数。 */
    char     m_text[XLINK_FILE_TEXT_BYTES];   /**< 中止文本（UTF-8，非终止符保证）。 */
} XLinkFileAbortPayload;

#endif /* XLINK_FILE_ON */

#endif /* XLINK_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKPROTO_H */
