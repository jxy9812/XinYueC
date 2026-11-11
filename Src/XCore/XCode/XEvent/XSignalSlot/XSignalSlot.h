#include"CXinYueConfig.h"
#if !defined(XSIGNALSLOTMANAGER_H)
#define XSIGNALSLOTMANAGER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include"XEventType.h"
#include"XVarList.h"
#include"XAtomic.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
// 前置声明
typedef struct XSignal XSignal;
typedef struct XConnection XConnection;
typedef void (*XSlotFunc1)(XObject* receiver, XVarList* args);
typedef void (*XSlotFunc2)(XObject* sender,XVarList* args);
/**
 * @brief 信号发送模式枚举
 * 定义信号从发送到执行槽函数的不同处理方式
 */
typedef enum 
{
    XEVENT_SEND_INVALID,  //无效模式
    XEVENT_SEND_DIRECT,   // 直接发送模式：在发送线程中立即执行槽函数（同步调用）
    XEVENT_SEND_QUEUED    // 队列发送模式：将信号放入接收者的消息队列，由接收者线程异步执行槽函数
} XEventSendMode;
typedef enum XConnectionType
{
    XConnectionType_Auto = 0,
    XConnectionType_Direct = 1,
    XConnectionType_Queued = 2,
    XConnectionType_BlockingQueued = 3,
    XConnectionType_Unique = 0x80,
    XConnectionType_SingleShot = 0x100,
    /* [2026-11-01 RawReceiver] receiver 仅为上下文指针（如 XIoPoint 等非
     * XObject 轻量类，布局与 XObject 不兼容）：连接/断开不做
     * receiver->m_signalSlot 簿记，仅允许 Direct 派发（pushSender 只存
     * 指针不解引用）。 */
    XConnectionType_RawReceiver = 0x200,
    /* [2026-11-11 ContextReceiver] receiver 只做**生命周期/线程亲和**簿记，
     * 不作为槽的首参——对标 Qt 6.8 `connect(sender, signal, context, functor)`：
     * functor 没有接收者对象，但连接的存亡由 context 决定。命中该位时：
     * ① 仍登记进 receiver->m_signalSlot->bindSignalList（context 析构即断开）；
     * ② 派发时走 slot_func2（首参=发送者），见 XConnection_directEmit /
     *    XMetaCallEvent 的 functor 标志；
     * ③ Auto 连接按 receiver（context）所属线程判定队列/直接。 */
    XConnectionType_ContextReceiver = 0x400,
}XConnectionType;
//槽
typedef struct XConnection
{
    XConnectionType type;
    XSignal* signal;
    XObject* receiver;          // 接收者对象（槽函数所属的对象）
    union//槽函数
    {
        XSlotFunc1 slot_func1;        //receiver!=NULL调用这个
        XSlotFunc2 slot_func2;        //receiver==NULL调用这个
    };
    /* [2026-11-11 引用计数字柄] 对标 Qt 6.8 `QMetaObject::Connection`
     * （qobjectdefs.h:645-668 的引用计数句柄语义）：
     *   - 发送者连接表（XSignal::connList）持 1 个引用；
     *   - 调用方要把句柄留到断开之后，必须 XConnection_retain 增加引用，
     *     用完 XConnection_release；不 retain 的句柄在断开后即失效；
     *   - 引用归零才真正 XFree_System（由 XConnection_dataDeinit 收口）。 */
    XAtomic_Int32 m_ref;
}XConnection;

//信号
typedef struct XSignal
{
    XObject* sender;//信号发送者对象
    size_t type;//信号类型(可以是函数也可以是枚举)
    XVector* connList;//连接列表  <XConnection>
} XSignal;
//信号与槽管理器
typedef struct XSignalSlot
{
    XObject* obj;//管理者/发送者
    //XEventSendMode sendMode;//发送模式
    XMap* signalMap;//信号列表 <size_t,XSignal>
    XVector* bindSignalList;//接收对象列表 绑定的其他对象信号
    XMutex* mutex;            //用于同步的互斥锁
}XSignalSlot;

XSignalSlot* XSignalSlot_create(XObject* obj);
void XSignalSlot_init(XSignalSlot* manager, XObject* obj);
void XSignalSlot_deinit(XSignalSlot* manager);
void XSignalSlot_delete(XSignalSlot* manager);
// 检查指定的信号是否有任何连接。
bool XSignalSlot_isSignalConnected(const XSignalSlot* manager, size_t signal);

// 返回连接到此对象上某个信号的接收者数量。
int XSignalSlot_receivers(const XSignalSlot* manager, size_t signal);
/**
 * @brief 连接信号与槽
 * @param m_signal 信号指针
 * @param receiver 槽函数所属的接收者对象
 * @param slot_func 槽函数
 * @return 连接对象指针（可用于后续断开连接）
 * @note **signal == 0 是无效信号 id**：打
 *       `XObject::connect: No such signal (id=0)` 并返回 NULL（对标 Qt 6.8
 *       "No such signal" 告警 + 无效 Connection），**不会**建空桶。
 *       依据：Qt 探针实测 connect(sender, SIGNAL(noSuchSignal(int)), ...)
 *       返回 bool(conn)==false；非 0 地址的合法性校验需信号签名登记表，
 *       属新子系统（deferred），本函数不做猜测。
 */
XConnection* XSignalSlot_connect1(XSignalSlot* manager,size_t signal, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type);
/**
 * @brief 连接信号与槽
 * @param m_signal 信号指针
 * @param slot_func 槽函数
 * @return 连接对象指针（可用于后续断开连接）
 * @note 同 `XSignalSlot_connect1`：signal==0 返回 NULL 并告警。
 */
XConnection* XSignalSlot_connect2(XSignalSlot* manager, size_t signal,XSlotFunc2 slot_func);
bool XSignalSlot_disconnect1(XSignalSlot* manager, size_t signal, XObject* receiver, XSlotFunc1 slot_func1);
/**
 * @brief 断开信号与槽的连接
 * @param conn 要断开的连接（由signal_connect返回）
 */
bool XSignalSlot_disconnect2(XConnection* conn);

/* ==================== 连接句柄（对标 Qt 6.8 QMetaObject::Connection） ====================
 * C 没有值类型句柄，故把「句柄」表达为**带引用计数的连接对象指针**：
 *   - 建立连接时返回的指针就是句柄（发送者表持 1 个引用）；
 *   - 需要跨断开保存句柄时调用 XConnection_retain，用完 XConnection_release；
 *   - 连接对象释放时会从全局活连接表注销，**任何**接受句柄的 API 先查表，
 *     已释放的旧句柄不会被解引用（重复断开安全）。
 *
 * 与 Qt 的 API 对应关系（Qt 6.8 `QMetaObject::Connection`，qobjectdefs.h:645-668）：
 * | Qt                                   | X 侧                                  |
 * |--------------------------------------|---------------------------------------|
 * | `connect(...)` 返回 Connection        | `XObject_connect_1/2/context` 的返回值 |
 * | 同上，按 Qt 类型读法书写               | `XConnectionHandle_create/create2`    |
 * | 拷贝构造（隐式持有一份引用）           | `XConnection_retain(conn)`            |
 * | 析构（释放自己那一份引用）             | `XConnection_release(conn)`           |
 * | `QObject::disconnect(conn)`           | `XConnection_disconnect(conn)` / `XObject_disconnect_2(conn)` |
 * | `if (conn)`（连接是否仍在）            | `XConnection_isValid(conn)`           |
 *
 * Qt 6.8 实测（本机探针，/tmp/probe_conn.cpp）：断开后 `operator bool` 立即为
 * false；对已断开的 Connection 再 disconnect 返回 false 且不崩溃；接收者或
 * 发送者析构后同样为 false / disconnect 返回 false。X 侧四条逐一对应。
 *
 * 命名说明：句柄类型别名是 `XConnectionHandle`；动词沿用底层类型名
 * `XConnection_*`（同一类型，见下方 typedef）。`create/create2` 用别名命名，
 * 便于按 Qt 的 `QMetaObject::Connection` 读法检索；它们只转调裸指针入口。 */

/**
 * @brief 连接句柄的**类型名**（对标 Qt 6.8 `QMetaObject::Connection`）
 * @note 与本文件其余接口的 `XConnection*` 是**同一类型**：X 侧句柄就是
 *       `XConnection*`（带引用计数 + 活连接表守护），本别名只让调用点按 Qt
 *       的读法书写（`XConnectionHandle* h = XObject_connect_1(...)`）。
 *       零开销、零额外分配。
 */
typedef XConnection XConnectionHandle;

/**
 * @brief 建立连接并返回引用计数句柄（对标 Qt `connect(...)` 返回
 *        `QMetaObject::Connection`）
 * @param sender 发送信号的对象（非 NULL）
 * @param signal 信号标识（`XSignal(信号函数)`）；0 为无效 id
 * @param receiver 槽函数所属的接收者对象（非 NULL）
 * @param slot_func 槽函数（`XSlotFunc1`）
 * @param type 连接类型（Auto/Direct/Queued/BlockingQueued，可带 Unique/SingleShot）
 * @return 连接句柄（`XConnectionHandle*` 即 `XConnection*`）；失败/无效信号返回 NULL
 * @note **只是 `XObject_connect_1` 的 Qt 类型读法工厂**：实现逐字转调
 *       `XObject_connect_1`，不复制任何连接逻辑（单一事实源）；保留裸指针
 *       入口是为了全仓既有调用点零改动，见 XObject.h 的同名说明。
 * @note 无效信号 id（signal==0）与 `XObject_connect_1` 同口径：打
 *       `XObject::connect: No such signal (id=0)` 并返回 NULL，**不建桶**。
 */
XConnectionHandle* XConnectionHandle_create(XObject* sender, size_t signal,
                                            XObject* receiver, XSlotFunc1 slot_func,
                                            XConnectionType type);

/**
 * @brief 建立无接收者的连接并返回句柄（对标 Qt `connect(sender, signal, functor)`）
 * @param sender 发送信号的对象（非 NULL）
 * @param signal 信号标识（`XSignal(信号函数)`）；0 为无效 id
 * @param slot_func 槽函数（`XSlotFunc2`：首参为发送者）
 * @return 连接句柄；失败/无效信号返回 NULL
 * @note **只是 `XObject_connect_2` 的 Qt 类型读法工厂**（逐字转调）。
 *       Qt 里这个重载没有 context 时连接不随任何对象析构自动断开——需要
 *       生命周期绑定请用 `XObject_connect_context`。
 */
XConnectionHandle* XConnectionHandle_create2(XObject* sender, size_t signal,
                                             XSlotFunc2 slot_func);

/**
 * @brief 判断连接句柄是否仍然有效（未被释放且未断开）
 * @param conn 连接句柄（可为 NULL/已释放的旧句柄）
 * @return 仍在发送者连接表中返回 true；已断开/已释放/NULL 返回 false
 * @note 对标 Qt 6.8 `Connection::operator bool()`：本机探针实测断开后
 *       `bool(conn)==false`（即使调用方仍持有其它副本）、接收者析构后同为
 *       false。X 侧判据是「活表命中 && conn->signal != NULL」——断开会把
 *       signal 清空但句柄对象（被 retain 时）仍存活，故与 Qt 同口径。
 */
bool XConnection_isValid(const XConnection* conn);

/**
 * @brief 增加连接句柄的引用计数（对标 Qt Connection 拷贝构造的隐式持有）
 * @param conn 连接句柄
 * @return 成功（句柄对象仍存活）返回 true；句柄已释放/为 NULL 返回 false
 * @note 语义分层（Qt 没有返回值，但拷贝一定安全）：
 *       ① 连接**仍有效**时 retain ⇒ true，此后断开不会回收句柄对象；
 *       ② 连接**已断开但调用方仍持有引用**时 retain ⇒ true（句柄对象还活着，
 *          与 Qt 拷贝一个已断开的 Connection 等价：copy 的 bool 为 false）；
 *       ③ 引用已归零（对象已释放）时 retain ⇒ false，**不解引用**旧指针。
 */
bool XConnection_retain(XConnection* conn);

/**
 * @brief 减少连接句柄的引用计数
 * @param conn 连接句柄
 * @return 本次释放后连接对象已销毁返回 true；句柄无效/仍被他人持有返回 false
 * @note 只释放**调用方自己 retain 的那一份**；发送者表的那一份由断开/析构收口。
 * @note **未 retain 就 release 会被拒绝**（返回 false + 告警）：连接仍有效
 *       （signal != NULL）且引用计数只剩 1 时，那一份必然属于发送者的连接表，
 *       释放它会让连接对象在仍挂在 connList 里时被回收 ⇒ 下次发射 UAF。
 *       Qt 的 Connection 析构只减自己那一份，发送者表的那份永远由
 *       disconnect 释放，故调用方无法"释放别人的引用"；C 侧指针无法分辨
 *       归属，按「仍有效 + 仅剩 1 份」这一可判定状态拒绝。
 */
bool XConnection_release(XConnection* conn);

/**
 * @brief 通过句柄断开连接（对标 Qt `bool QObject::disconnect(const QMetaObject::Connection &)`）
 * @param conn 连接句柄
 * @return 本次真的断掉一条连接返回 true；已断开/已释放返回 false
 * @note 与 XObject_disconnect_2 同一实现，命名对齐 Qt 的「断开这个连接」读法。
 *       Qt 6.8 实测：对已断开的 Connection 再 disconnect 返回 false；
 *       X 侧对已断开的保留句柄、对已释放的旧句柄同样返回 false。
 */
bool XConnection_disconnect(XConnection* conn);

/**
 * @brief 触发信号，通知所有关联的槽函数
 * @param m_signal 信号指针
 * @param argList 传递给槽函数的参数（通过void* args传递任意类型）调用所有槽后自动释放 建议只读不能修改
 * @param del  args的释放规则,可传入NULL
 * @param ref_count 优化参数,传入NULL,内部自己创建管理，外部传入可以与事件共享参数，而不用拷贝，谁最后谁释放参数
 * @param priority  信号与槽队列连接的时候的优先级(内部走的是事件投递)
 */
void XSignalSlot_emit(XSignalSlot* manager, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_Int32* ref_count, int priority);
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H