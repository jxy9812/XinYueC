#ifndef XS7SESSION_H
#define XS7SESSION_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XObject.h"
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7Session.h
 * @brief S7 会话状态机（握手推进、pduRef 分配、粘包/半包分帧、事件队列）
 * @details 会话层唯一有状态对象：COTP CR → CC → SetupCommunication 协商；
 *          唯一允许触碰 TCP 字节流的组件（内部 XByteArray 缓冲，
 *          TPKT 定长 → 缓冲 → 逐帧出队），产出事件供 XS7TcpClient 驱动。
 *
 * @par 握手推进（由 XS7TcpClient 在 socket connected 槽驱动）
 * buildNextHandshake → 发 CR → feed 到 CcAccepted → buildNextHandshake →
 * 发 Setup → feed 到应答 → HandshakeDone(协商 PDU) → setState(ConnectedState)。
 * 任一步超时或 CC 拒绝：先 XS7Session_reset（清残留帧防污染二次握手）再重连。
 *
 * @par 坏包处理约定
 * 按 TPKT len 精确推进读指针 + XERROR_PRINTF 记录，禁止清空缓冲了事
 * （否则后续半包错位）；超 XS7_MAX_FRAME 丢弃该帧。
 */

/**
 * @brief 会话状态枚举
 */
typedef enum {
    XS7Session_Idle,       ///< 空闲（未开始握手）
    XS7Session_CrSent,     ///< CR 已发送，等待 CC
    XS7Session_SetupSent,  ///< SetupCommunication 已发送，等待应答
    XS7Session_Ready,      ///< 握手完成，可收发业务
    XS7Session_Error       ///< 错误（需 reset 后重连）
} XS7Session_State;

/**
 * @brief 会话事件类型枚举
 */
typedef enum {
    XS7SessionEv_None,         ///< 无事件
    XS7SessionEv_CcAccepted,   ///< CC 被接受（可发 Setup）
    XS7SessionEv_HandshakeDone,///< 握手完成（协商出 PDU 长度）
    XS7SessionEv_S7Frame,      ///< 收到完整 S7 PDU（经 XS7Session_frame() 取）
    XS7SessionEv_Error         ///< 协议/分帧错误
} XS7Session_EventType;

/**
 * @brief S7 会话对象（继承自XObject，对象拥有缓冲）
 */
XCLASS_DEFINE_BEGING(XS7Session)
XCLASS_DEFINE_EXTEND_END(XS7Session, XObject)

typedef struct XS7Session {
    XObject m_class;                     ///< 继承自XObject基类，第一位，禁手工修改
    uint16_t/*XS7Session_State*/ m_state;          ///< 会话状态
    uint16_t m_localTsap;                ///< 本端 TSAP
    uint16_t m_remoteTsap;               ///< 目标 TSAP
    uint16_t m_requestedPduLen;          ///< 请求的 PDU 长度（首期 960）
    uint16_t m_amq;                      ///< 请求的最大并行任务数（首期 1）
    uint16_t m_negotiatedPduLen;         ///< 协商出的 PDU 长度（未握手为 0）
    uint16_t m_nextPduRef;               ///< PDU 引用号分配游标（1..65535 循环）
    uint8_t  m_pendingEvent;             ///< 待取事件（XS7Session_EventType）
    XByteArray* m_buffer;                ///< 粘包/半包缓冲（对象拥有）
    XByteArray* m_frame;                 ///< 最近解析出的完整帧（S7 PDU 借用视图）
} XS7Session;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XS7Session的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XS7Session_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XS7Session实例
 * @param memory 内存分配类型
 * @return 成功返回新实例指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 */
XS7Session* XS7Session_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XS7Session实例
 * @param session 待初始化的实例指针（非NULL）
 * @note 状态置 Idle、分配游标从 1 起、创建内部缓冲
 */
void XS7Session_init(XS7Session* session);

/******************************************************************************************
 * 会话 API
 ******************************************************************************************/

 /**
  * @brief 配置握手参数（connectDevice 前调用）
  * @param session 会话指针（非NULL）
  * @param localTsap 本端 TSAP
  * @param remoteTsap 目标 TSAP（-1=按 Rack/Slot 自动已换算）
  * @param requestedPduLen 请求的 PDU 长度（首期 960）
  * @param amq 请求的最大并行任务数（首期 1）
  */
void XS7Session_setParameters(XS7Session* session, uint16_t localTsap, uint16_t remoteTsap,
                              uint16_t requestedPduLen, uint16_t amq);

/**
 * @brief 获取会话状态
 * @param session 会话指针
 * @return 会话状态，session为NULL时返回XS7Session_Idle
 */
XS7Session_State XS7Session_state(const XS7Session* session);

/**
 * @brief 获取协商出的 PDU 长度
 * @param session 会话指针
 * @return 协商长度，未握手或session为NULL返回0
 */
uint16_t XS7Session_negotiatedPduSize(const XS7Session* session);

/**
 * @brief 分配下一个 PDU 引用号
 * @param session 会话指针（非NULL）
 * @return 1..65535 循环递增的 pduRef，session为NULL返回0
 */
uint16_t XS7Session_nextPduRef(XS7Session* session);

/**
 * @brief 产出下一步待发帧（CR 或 SetupCommunication 请求）
 * @param session 会话指针（非NULL）
 * @param outFrame 输出帧字节数组（清空后写入，含 TPKT）
 * @return 有待发帧返回true；无待发或状态非法返回false
 */
bool XS7Session_buildNextHandshake(XS7Session* session, XByteArray* outFrame);

/**
 * @brief 喂入 TCP 原始字节（可半包/粘包/多帧/坏包）
 * @param session 会话指针（非NULL）
 * @param data 原始字节
 * @param len 字节数
 * @return 全部接受返回true；缓冲非法等致命错误返回false
 * @note 内部按 TPKT 定长切帧入 m_buffer，逐帧出队转事件；
 *       坏包按 TPKT len 精确推进读指针并 XERROR_PRINTF，不得清空缓冲了事
 */
bool XS7Session_feed(XS7Session* session, const uint8_t* data, size_t len);

/**
 * @brief 取一个事件（出队）
 * @param session 会话指针（非NULL）
 * @return 事件类型；无事件返回XS7SessionEv_None
 * @note S7Frame 事件经 XS7Session_frame() 取完整 S7 PDU（借用，下次 take 前有效）
 */
XS7Session_EventType XS7Session_takeEvent(XS7Session* session);

/**
 * @brief 取最近解析出的完整 S7 PDU
 * @param session 会话指针（非NULL）
 * @param len 输出：PDU 长度
 * @return PDU 数据指针（借用，调用者不得释放、不得跨 take 使用），无则NULL
 */
const uint8_t* XS7Session_frame(XS7Session* session, size_t* len);

/**
 * @brief 重置会话（断线后调用）
 * @param session 会话指针（非NULL）
 * @note 清缓冲/状态/事件，保留握手参数；握手失败必须先 reset 再重连，
 *       防残留帧污染二次握手
 */
void XS7Session_reset(XS7Session* session);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XS7Session_deleteLater       XObject_deleteLater
#define XS7Session_deinitLater       XObject_deinitLater

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XS7Session_create
#define XS7Session_create() XS7Session_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XS7SESSION_H
