#ifndef XOBJECT_H
#define XOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#include"XEvent.h"
#include"XSignalSlot.h"
#include"XAtomic.h"
#include"XVarList.h"

/**
 * @brief 开始定义XObject类的虚函数表枚举
 * @note 用于实现C语言中的多态机制，枚举值对应虚函数在表中的索引
 */
XCLASS_DEFINE_BEGING(XObject)
/**
 * @brief 定义XObject类的Poll虚函数枚举值
 * @note 继承自XClass的虚函数表大小，确保索引不冲突
 */
XCLASS_DEFINE_ENUM(XObject, Poll) = XCLASS_VTABLE_GET_SIZE(XClass),
/**
 * @brief 结束XObject虚函数表枚举定义，并定义虚函数表大小（EObject_END_SIZE）
 */
XCLASS_DEFINE_END(XObject)

/**
 * @brief XObject类结构体定义，是所有对象的基类
 * @note 继承自XClass，包含对象基础属性和行为控制成员
 */
typedef struct XObject
{
    XClass m_class;                      ///< 继承的基类成员，包含虚函数表指针（实现多态）
    bool m_isEventBubblingEnabled;       ///< 事件冒泡开关：true表示开启，未处理的事件会转发给父对象
    XAtomic_bool m_deleteState;          ///< 原子布尔变量：标记对象是否处于删除状态（线程安全）
    XAtomic_uint32_t m_eventCount;       ///< 原子无符号整数：记录当前待处理的事件数量（用于延迟释放）
    XObject* m_parent;                   ///< 父对象指针：构建对象树结构，用于事件传递和层级管理
    XThread* m_thread;                   ///< 所属线程指针：对象关联的线程，事件处理在该线程执行
    XSignalSlot* m_signalSlot;           ///< 信号与槽控制器：管理当前对象的所有信号与槽连接关系
    XTimerBase* m_poolTimer;             ///< 轮询定时器：用于定期触发Poll虚函数（控制轮询频率）
}XObject;

/**
 * @brief 初始化XObject类的虚函数表
 * @return 指向初始化完成的XVtable的指针
 * @note 注册XObject类的虚函数，实现多态接口
 */
XVtable* XObject_class_init();

/**
 * @brief 在堆上创建XObject实例并初始化
 * @return 指向新创建的XObject对象的指针，失败返回NULL
 * @note 内部调用XMemory_malloc分配内存，再调用XObject_init初始化
 */
XObject* XObject_create();

/**
 * @brief 初始化XObject实例的成员变量
 * @param object 待初始化的XObject对象指针（非NULL）
 * @note 初始化基类成员、线程关联、事件过滤器等基础属性
 */
void XObject_init(XObject* object);

/**
 * @brief 轮询函数的基础实现（多态入口）
 * @param object 调用轮询的XObject对象指针（非NULL）
 * @note 通过虚函数表调用实际的Poll虚函数，子类可重写该函数实现自定义轮询逻辑
 */
void XObject_poll_base(XObject* object);

/**
 * @brief 设置轮询间隔时间
 * @param object 目标XObject对象指针（非NULL）
 * @param interval 轮询间隔（毫秒），0表示关闭轮询
 * @note 通过m_poolTimer定时器控制Poll函数的调用频率
 */
void XObject_setPollingInterval(XObject* object, size_t interval);

/**
 * @brief 设置当前对象的父对象
 * @param object 目标XObject对象指针（非NULL）
 * @param parent 父对象指针（可为NULL，表示无父对象）
 * @note 用于构建对象树，影响事件冒泡和对象生命周期管理
 */
void XObject_setParent(XObject* object, XObject* parent);

/**
 * @brief 获取当前对象的父对象
 * @param object 目标XObject对象指针（非NULL）
 * @return 父对象指针，若没有父对象则返回NULL
 */
XObject* XObject_getParent(XObject* object);

/**
 * @brief 设置事件冒泡功能的启用状态
 * @param object 目标XObject对象指针（非NULL）
 * @param enable 启用状态：true为启用，false为禁用
 * @note 启用后，未被当前对象处理的事件会转发给父对象
 */
void XObject_setEventBubblingEnabled(XObject* object, bool enable);

/**
 * @brief 检查当前对象是否启用事件冒泡
 * @param object 目标XObject对象指针（非NULL）
 * @return 事件冒泡状态：true为启用，false为禁用
 */
bool XObject_isEventBubbling(XObject* object);

/**
 * @brief 为特定事件类型添加事件过滤器
 * @param object 目标XObject对象指针（非NULL）
 * @param code 事件类型编码（如XEVENT_SLOT_RUN）
 * @param cb 事件处理回调函数（XEventCB类型）
 * @param userData 传递给回调函数的用户数据（可为NULL）
 * @return 添加成功返回true，失败返回false
 * @note 事件过滤器用于在事件到达对象前预处理事件
 */
bool XObject_addEventFilter(XObject* object, int code, XEventCB cb, void* userData);

/**
 * @brief 移除特定事件类型的事件过滤器
 * @param object 目标XObject对象指针（非NULL）
 * @param code 事件类型编码（如XEVENT_SLOT_RUN）
 * @return 移除成功返回true，失败返回false
 */
bool XObject_removeEventFilter(XObject* object, int code);

/**
 * @brief 将对象移动到指定线程
 * @param object 目标XObject对象指针（非NULL）
 * @param thread 目标线程指针（非NULL）
 * @return 移动成功返回true，失败返回false
 * @note 移动后，对象的事件处理将在目标线程执行，确保线程安全
 */
bool XObject_moveToThread(XObject* object, XThread* thread);

/**
 * @brief 向对象投递事件
 * @param object 目标XObject对象指针（非NULL）
 * @param event 待投递的事件实例（非NULL）
 * @param priority 事件优先级（如XEVENT_PRIORITY_LOWEST）
 * @return 投递成功返回true，失败返回false
 * @note 事件将被加入对象所属线程的事件队列，按优先级处理
 */
bool XObject_postEvent(XObject* object, XEvent* event, XEventPriority priority);

/**
 * @brief 向对象投递异步执行的函数
 * @param object 目标XObject对象指针（非NULL）
 * @param func 待执行的函数指针（原型：void (*)(void*)）
 * @param args 传递给函数的参数（可为NULL）
 * @param del 参数的释放回调函数（可为NULL，用于释放args资源）
 * @param mode 发送模式（XEVENT_SEND_DIRECT：直接投递；XEVENT_SEND_QUEUED：队列投递）
 * @param priority 执行优先级（如XEVENT_PRIORITY_LOWEST）
 * @return 投递成功返回true，失败返回false
 * @note 函数将在对象所属线程的事件循环中执行，实现异步调用
 */
bool XObject_postFunc(XObject* object, void (*func)(void*), void* args, void(*del)(void*), XEventSendMode mode, XEventPriority priority);

/**
 * @brief 获取对象所属的线程
 * @param object 目标XObject对象指针（非NULL）
 * @return 所属线程指针，若未指定则返回NULL
 */
XThread* XObject_thread(XObject* object);

/**
 * @brief 获取对象所属线程的事件调度器
 * @param object 目标XObject对象指针（非NULL）
 * @return 事件调度器指针（XEventDispatcher*），失败返回NULL
 * @note 事件调度器负责管理线程的事件队列和分发
 */
XEventDispatcher* XObject_getEventDispatcher(XObject* object);

/**
 * @brief 获取对象所属线程的事件循环
 * @param object 目标XObject对象指针（非NULL）
 * @return 事件循环指针（XEventLoop*），失败返回NULL
 * @note 事件循环是线程处理事件的主循环
 */
XEventLoop* XObject_getEventLoop(XObject* object);

/**
 * @brief 为对象的信号绑定接收者的槽函数（信号与槽连接）
 * @param object 发送信号的对象指针（非NULL）
 * @param signal 信号标识（由XSignal宏生成）
 * @param receiver 接收槽函数的对象指针（非NULL）
 * @param slot_func 槽函数指针（XSlotFunc类型）
 * @param type 连接类型（直接调用/队列调用等）
 * @return 连接对象指针（XConnection*），失败返回NULL
 * @note 建立信号与槽的关联，当信号发射时触发槽函数
 */
XConnection* XObject_connect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type);

/**
 * @brief 信号绑定辅助宏，用于生成信号标识
 * @param signal 信号函数名
 * @return 信号对应的标识值
 * @note 将信号函数转换为统一的标识格式，方便信号与槽的绑定
 */
#define XSignal(signal)      ((void*(*)(XObject*))signal)(NULL)

/**
* @brief 断开对象信号与接收者槽函数的连接
* @param object 发送信号的对象指针（非NULL）
* @param signal 信号标识（由XSignal宏生成）
* @param receiver 接收槽函数的对象指针（非NULL）
* @param slot_func 槽函数指针（XSlotFunc类型）
* @return 断开成功返回true，失败返回false
*/
bool XObject_disconnect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func);

/**
 * @brief 通过连接对象断开信号与槽的连接
 * @param conn 连接对象指针（XConnection*，非NULL）
 * @return 断开成功返回true，失败返回false
 */
bool XObject_disconnect_conn(XConnection* conn);

/**
 * @brief 栈区对象延迟释放
 * @param object 待释放的XObject对象实例（非NULL）
 * @note 与XClass_deinit_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 */
void XObject_deinit_base(XObject* object);

/**
 * @brief 堆区对象延迟释放
 * @param object 待释放的XObject对象实例（非NULL）
 * @note 与XClass_delete_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 */
void XObject_delete_base(XObject* object);

/**
 * @brief 释放信号（槽函数原型对应的信号）
 * @param object 发送信号的XObject对象指针（非NULL）
 * @return 信号标识
 * @note 槽函数原型：void deinit_slot(XObject* receiver, void* args, XObject* m_sender)
 *       用于通知相关对象当前对象即将释放
 */
void* XObject_deinit_signal(XObject* object);

/**
* @brief 精简信号发射宏（同步发射）
* @param object 发送信号的对象指针（可为NULL，为NULL时不发射）
* @param signal 信号标识
* @param args 信号参数（可为NULL）
* @param del 参数释放回调（可为NULL）
* @param ref_count 引用计数（可为NULL）
* @param priority 信号优先级
* @return 信号标识
* @note 自动检查对象有效性，立即发射信号，触发绑定的槽函数
*/
#define XEmitSignal(object,signal,args,del,ref_count,priority) if(object)XObject_emitSignal(object,signal,args,del,ref_count,priority);return signal

/**
* @brief 精简信号发射宏（队列模式，异步发射）
* @param object 发送信号的对象指针（可为NULL，为NULL时不发射）
* @param signal 信号标识
* @param args 信号参数（可为NULL）
* @param del 参数释放回调（可为NULL）
* @param ref_count 引用计数（可为NULL）
* @param priority 信号优先级
* @return 信号标识
* @note 自动检查对象有效性，将信号加入事件队列，异步触发槽函数
*/
#define XEmitSignalQueue(object,signal,args,del,ref_count,priority) if(object)XObject_emitSignal_queue(object,signal,args,del,ref_count,priority);return signal

/**
 * @brief 同步发射信号
 * @param object 发送信号的对象指针（非NULL）
 * @param signal 信号标识
 * @param args 信号参数（可为NULL）
 * @param del 参数释放回调（可为NULL，用于释放args资源）
 * @param ref_count 引用计数（可为NULL，用于管理参数生命周期）
 * @param priority 信号优先级
 */
void XObject_emitSignal(XObject* object, size_t signal, void* args, void(*del)(void*), XAtomic_int32_t* ref_count, XEventPriority priority);

/**
* @brief 异步发射信号（队列模式，事件循环中延迟发送,可用在中断中,延迟发送）
* @param object 发送信号的对象指针（非NULL）
* @param signal 信号标识
* @param args 信号参数（可为NULL）
* @param del 参数释放回调（可为NULL，用于释放args资源）
* @param ref_count 引用计数（可为NULL，用于管理参数生命周期）
* @param priority 信号优先级
*/
void XObject_emitSignal_queue(XObject* object, size_t signal, void* args, void(*del)(void*), XAtomic_int32_t* ref_count, XEventPriority priority);

#ifdef __cplusplus
}
#endif
#endif