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
typedef struct XThreadData XThreadData;    ///< 线程私有数据（前向声明，对标 QThreadData）
#define Signals
/**
 * @brief XObject 类的虚函数表枚举（对标 Qt 6.8 QObject 虚函数）
 * @note 枚举值对应虚函数在虚函数表中的索引，用于实现 C 语言多态机制
 */
XCLASS_DEFINE_BEGING(XObject)
XCLASS_DEFINE_ENUM(XObject, Event) = XCLASS_VTABLE_GET_SIZE(XClass),   /**< 事件处理虚函数（对标 QObject::event） */
XCLASS_DEFINE_ENUM(XObject, EventFilter),                              /**< 事件过滤器虚函数（对标 QObject::eventFilter） */
XCLASS_DEFINE_ENUM(XObject, ChildEvent),                               /**< 子对象事件虚函数（对标 QObject::childEvent） */
XCLASS_DEFINE_ENUM(XObject, ConnectNotify),                            /**< 连接通知虚函数（对标 QObject::connectNotify） */
XCLASS_DEFINE_ENUM(XObject, CustomEvent),                              /**< 自定义事件虚函数（对标 QObject::customEvent） */
XCLASS_DEFINE_ENUM(XObject, DisconnectNotify),                         /**< 断开连接通知虚函数（对标 QObject::disconnectNotify） */
XCLASS_DEFINE_ENUM(XObject, TimerEvent),                               /**< 定时器事件虚函数（对标 QObject::timerEvent） */
XCLASS_DEFINE_END(XObject)

/**
 * @brief XObject 类结构体定义（对标 Qt 6.8 QObject）
 * @note 所有对象的基类，继承自 XClass，包含对象基础属性和行为控制成员
 */
typedef struct XObject
{
    XClass m_class;                          ///< 继承的基类成员，包含虚函数表指针（实现多态）
    XAtomic_int32_t m_posted_events;         ///< 已投递但未处理的事件计数（对标 Qt 6.8 QAtomicInt）

    /* 对象标志位 */
    uint32_t is_widget : 1;                  ///< 是否为窗口部件
    uint32_t block_sig : 1;                  ///< 信号是否被阻塞
    uint32_t was_deleted : 1;                ///< 对象是否已被标记为删除
    uint32_t is_deleting_children : 1;       ///< 是否正在删除子对象
    uint32_t send_child_events : 1;          ///< 是否发送子对象事件
    uint32_t receive_child_events : 1;       ///< 是否接收子对象事件
    uint32_t is_window : 1;                  ///< 是否为窗口（用于 XWindow）
    uint32_t delete_later_called : 1;        ///< deleteLater 是否已被调用
    uint32_t is_quick_item : 1;              ///< 是否为 Quick 项
    uint32_t will_be_widget : 1;             ///< 构造时将变为窗口部件
    uint32_t was_widget : 1;                 ///< 析构时曾是窗口部件
    uint32_t receive_parent_events : 1;      ///< 是否接收父对象事件
    uint32_t unused : 20;                    ///< 保留位
    XObject* currentChildBeingDeleted;       ///< 当前正在删除的子对象（对标 QObjectPrivate）
    XSignalSlot* m_signalSlot;               ///< 信号与槽控制器，管理当前对象的所有信号与槽连接关系

    /* 父子关系 */
    XObject* m_parent;                       ///< 父对象
    XVector* m_children;                     ///< 子对象列表
    XVector* m_filters;                      ///< 事件过滤器列表
    XAtomic_uintptr_t m_threadData;          ///< 线程亲和性数据（对标 QObjectPrivate::threadData）

    /* 对象名称 */
    XString* m_object_name;                  ///< 对象名称

    /* 动态属性存储（对标 QObjectPrivate::ExtraData） */
    XVector* m_dynamicPropertyNames;         ///< 动态属性名列表（XString*）
    XVector* m_dynamicPropertyValues;        ///< 动态属性值列表（XVariant*）
}XObject;

/**
 * @brief 初始化 XObject 类的虚函数表
 * @return 指向初始化完成的 XVtable 的指针
 * @note 注册 XObject 类的虚函数，实现多态接口
 */
XVtable* XObject_class_init();

/**
 * @brief 在堆上创建 XObject 实例并初始化
 * @return 指向新创建的 XObject 对象的指针，失败返回 NULL
 * @note 内部调用 XMalloc_System 分配内存，再调用 XObject_init 初始化
 */
XObject* XObject_create_ex(XMemoryType memory);

/**
 * @brief 初始化 XObject 实例的成员变量
 * @param object 待初始化的 XObject 对象指针（非 NULL）
 * @note 初始化基类成员、线程关联、事件过滤器等基础属性
 */
void XObject_init(XObject* object);

/**
 * @brief 获取对象名称（对标 Qt 6.8 QObject::objectName）
 * @param self 目标对象指针
 * @return 对象名称字符串指针，未设置返回 NULL
 */
const XString* XObject_objectName(const XObject* self);

/**
 * @brief 设置对象名称（对标 Qt 6.8 QObject::setObjectName）
 * @param self 目标对象指针
 * @param name 要设置的名称字符串指针
 */
void XObject_setObjectName(XObject* self, const XString* name);

/**
 * @brief 获取当前对象的父对象（对标 Qt 6.8 QObject::parent）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @return 父对象指针，若无父对象返回 NULL
 */
XObject* XObject_parent(XObject* object);

/**
 * @brief 设置当前对象的父对象（对标 Qt 6.8 QObject::setParent）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @param parent 父对象指针（可为 NULL，表示无父对象）
 * @note 用于构建对象树，影响事件冒泡和对象生命周期管理
 */
void XObject_setParent(XObject* object, XObject* parent);

/**
 * @brief 获取子对象列表（对标 Qt 6.8 QObject::children）
 * @param self 目标对象指针
 * @return 子对象列表指针（返回内部指针，勿修改）
 */
const XVector* XObject_children(const XObject* self);

/* ======================== 对象类型查询 ======================== */

/**
 * @brief 检查是否为窗口部件类型（对标 Qt 6.8 QObject::isWidgetType）
 * @param self 目标对象指针
 * @return 是窗口部件返回 true，否则返回 false
 */
bool XObject_isWidgetType(const XObject* self);

/**
 * @brief 检查是否为窗口类型（对标 Qt 6.8 QObject::isWindowType）
 * @param self 目标对象指针
 * @return 是窗口返回 true，否则返回 false
 */
bool XObject_isWindowType(const XObject* self);

/**
 * @brief 检查是否为 Quick 项类型（对标 Qt 6.8 QObject::isQuickItemType）
 * @param self 目标对象指针
 * @return 是 Quick 项返回 true，否则返回 false
 */
bool XObject_isQuickItemType(const XObject* self);

/**
 * @brief 将对象移动到指定线程（对标 Qt 6.8 QObject::moveToThread）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @param target_thread 目标线程指针；NULL 表示移除线程亲和性
 * @return 移动成功返回 true，失败返回 false
 * @note 移动后对象的事件处理将在目标线程执行，确保线程安全
 */
bool XObject_moveToThread(XObject* object, XThread* target_thread);

/* ======================== 信号与槽 ======================== */

/**
 * @brief 检查信号是否被阻塞（对标 Qt 6.8 QObject::signalsBlocked）
 * @param self 目标对象指针
 * @return 信号被阻塞返回 true，否则返回 false
 */
bool XObject_signalsBlocked(const XObject* self);

/**
 * @brief 阻塞或取消阻塞信号（对标 Qt 6.8 QObject::blockSignals）
 * @param self 目标对象指针
 * @param block true 表示阻塞信号，false 表示取消阻塞
 * @return 之前的阻塞状态
 */
bool XObject_blockSignals(XObject* self, bool block);

/* ======================== 定时器 ======================== */

/**
 * @brief 启动一个定时器（毫秒，对标 Qt 6.8 QObject::startTimer）
 * @param self 目标对象指针
 * @param interval 定时器间隔（毫秒）
 * @param timerType 定时器精度类型
 * @return 定时器 ID
 */
XTimerId XObject_startTimer_ms(XObject* self, uint64_t interval, XTimerType timerType);

/**
 * @brief 启动一个定时器（纳秒，对标 Qt 6.8 QObject::startTimer）
 * @param self 目标对象指针
 * @param interval_ns 定时器间隔（纳秒）
 * @param timerType 定时器精度类型
 * @return 定时器 ID
 */
XTimerId XObject_startTimer_ns(XObject* self, uint64_t interval_ns, XTimerType timerType);

/**
 * @brief 停止一个定时器（对标 Qt 6.8 QObject::killTimer）
 * @param self 目标对象指针
 * @param timerId 要停止的定时器 ID
 */
void XObject_killTimer(XObject* self, XTimerId timerId);

/* ======================== 事件系统 ======================== */

/**
 * @brief 安装事件过滤器（对标 Qt 6.8 QObject::installEventFilter）
 * @param self 目标对象指针
 * @param filterObj 事件过滤器对象指针
 */
void XObject_installEventFilter(XObject* self, XObject* filterObj);

/**
 * @brief 移除事件过滤器（对标 Qt 6.8 QObject::removeEventFilter）
 * @param self 目标对象指针
 * @param obj 要移除的事件过滤器对象指针
 */
void XObject_removeEventFilter(XObject* self, XObject* obj);

/**
 * @brief XObject 对象列表类型（对标 Qt 6.8 QList<QObject*>）
 */
typedef XVector XObjectList;

/* ======================== 对象查找 ======================== */

/**
 * @brief 查找一个子对象（对标 Qt 6.8 QObject::findChild）
 * @param self 目标对象指针
 * @param name 子对象名称
 * @param options 查找选项
 * @return 匹配的子对象指针，未找到返回 NULL
 */
XObject* XObject_findChild(const XObject* self, const XString* name, XFindChildOption options);

/**
 * @brief 查找所有匹配的子对象（对标 Qt 6.8 QObject::findChildren）
 * @param self 目标对象指针
 * @param name 子对象名称
 * @param options 查找选项
 * @return 匹配的子对象列表指针，未找到返回空列表
 */
XObjectList* XObject_findChildren(const XObject* self, const XString* name, XFindChildOption options);

/* ======================== 动态属性（对标 Qt 6.8 QObject::setProperty/property/dynamicPropertyNames） ======================== */

/**
 * @brief 设置动态属性（对标 Qt 6.8 QObject::setProperty）
 * @param self 目标对象指针
 * @param name 属性名
 * @param value 属性值（XVariant*，所有权转移给对象，由对象管理生命周期）
 * @return 设置成功返回 true，否则返回 false
 */
bool XObject_setProperty(XObject* self, const XString* name, XVariant* value);

/**
 * @brief 获取动态属性（对标 Qt 6.8 QObject::property）
 * @param self 目标对象指针
 * @param name 属性名
 * @return 属性值指针（XVariant*），不存在返回 NULL
 */
XVariant* XObject_property(const XObject* self, const XString* name);

/**
 * @brief 获取所有动态属性的名称列表（对标 Qt 6.8 QObject::dynamicPropertyNames）
 * @param self 目标对象指针
 * @return 动态属性名列表（XString* 的 XVector）
 */
XVector* XObject_dynamicPropertyNames(const XObject* self);

/**
 * @brief 移除动态属性
 * @param self 目标对象指针
 * @param name 属性名
 */
void XObject_removeProperty(XObject* self, const XString* name);

/* ======================== 受保护的虚函数与辅助函数（需由子类实现或使用） ======================== */

/**
 * @brief 核心事件处理器（对标 Qt 6.8 QObject::event）
 * @param self 目标对象指针
 * @param e 事件指针
 * @return 事件已被处理返回 true，否则返回 false
 * @note 应在子类初始化时通过函数指针重写
 */
bool XObject_event_base(XObject* self, XEvent* e);

/**
 * @brief 事件过滤器（对标 Qt 6.8 QObject::eventFilter）
 * @param self 目标对象指针
 * @param watched 被监视的对象
 * @param event 事件指针
 * @return 事件已被过滤（不再传播）返回 true，否则返回 false
 * @note watched 对象收到事件时先调用此过滤器
 */
bool XObject_eventFilter_base(XObject* self, XObject* watched, XEvent* event);

/**
 * @brief 定时器事件回调（对标 Qt 6.8 QObject::timerEvent）
 * @param self 目标对象指针
 * @param event 定时器事件指针
 * @note 定时器触发时被调用
 */
void XObject_timerEvent_base(XObject* self, XTimerEvent* event);

/**
 * @brief 子对象事件回调（对标 Qt 6.8 QObject::childEvent）
 * @param self 目标对象指针
 * @param event 子对象事件指针
 * @note 子对象被添加、移除或销毁时被调用
 */
void XObject_childEvent_base(XObject* self, XChildEvent* event);

/**
 * @brief 自定义事件回调（对标 Qt 6.8 QObject::customEvent）
 * @param self 目标对象指针
 * @param event 自定义事件指针
 * @note 用于处理用户自定义的事件类型
 */
void XObject_customEvent(XObject* self, XEvent* event);

/**
 * @brief 连接通知（对标 Qt 6.8 QObject::connectNotify）
 * @param self 目标对象指针
 * @param signal 信号标识
 * @note 有信号连接到此对象的槽时被调用
 */
void XObject_connectNotify_base(XObject* self, size_t signal);

/**
 * @brief 断开连接通知（对标 Qt 6.8 QObject::disconnectNotify）
 * @param self 目标对象指针
 * @param signal 信号标识
 * @note 有信号从此对象的槽断开时被调用
 */
void XObject_disconnectNotify_base(XObject* self, size_t signal);

/* ======================== 信号/槽上下文查询（仅在槽函数内有效） ======================== */

/**
 * @brief 获取当前激活信号的发送者（对标 Qt 6.8 QObject::sender）
 * @param self 接收者对象指针
 * @return 发送者对象指针，未在信号发射上下文中调用返回 NULL
 */
XObject* XObject_sender(const XObject* self);

/**
 * @brief 获取当前信号发送者的信号索引（对标 Qt 6.8 QObject::senderSignalIndex）
 * @param self 接收者对象指针
 * @return 信号索引，未在信号发射上下文中调用时返回 0
 */
int XObject_senderSignalIndex(const XObject* self);

/* ======================== 连接状态查询 ======================== */

/**
 * @brief 检查指定的信号是否有任何连接（对标 Qt 6.8 QObject::isSignalConnected）
 * @param self 目标对象指针
 * @param signal 信号标识
 * @return 有连接返回 true，否则返回 false
 */
bool XObject_isSignalConnected(const XObject* self, size_t signal);

/**
 * @brief 返回连接到此对象上某个信号的接收者数量（对标 Qt 6.8 QObject::receivers）
 * @param self 目标对象指针
 * @param signal 信号标识
 * @return 接收者数量
 */
int XObject_receivers(const XObject* self, size_t signal);

/**
 * @brief 获取对象所属的线程数据（对标 Qt 6.8 QObjectPrivate::threadData）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @return 所属线程的 XThreadData 指针，未指定返回 NULL
 */
XThreadData* XObject_threadData(const XObject* object);

/**
 * @brief 获取对象所属的线程（对标 Qt 6.8 QObject::thread）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @return 所属线程指针，未指定返回 NULL
 */
XThread* XObject_thread(const XObject* object);

/**
 * @brief 获取对象所属线程的事件调度器（对标 QAbstractEventDispatcher::instance）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @return 事件调度器指针（XAbstractEventDispatcher*），失败返回 NULL
 * @note 事件调度器负责管理线程的事件队列和分发
 */
XAbstractEventDispatcher* XObject_eventDispatcher(const XObject* object);

/**
 * @brief 打印对象树结构（对标 Qt 6.8 QObject::dumpObjectTree，调试用）
 * @param self 对象指针
 */
void XObject_dumpObjectTree(const XObject* self);

/**
 * @brief 打印对象详细信息（对标 Qt 6.8 QObject::dumpObjectInfo，调试用）
 * @param self 对象指针
 */
void XObject_dumpObjectInfo(const XObject* self);

/**
 * @brief 为对象的信号绑定接收者的槽函数（信号与槽连接，对标 Qt 6.8 QObject::connect）
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）
 * @param receiver 接收槽函数的对象指针（非 NULL）
 * @param slot_func1 槽函数指针（XSlotFunc1 类型）
 * @param type 连接类型（直接调用/队列调用等）
 * @return 连接对象指针（XConnection*），失败返回 NULL
 * @note 建立信号与槽的关联，当信号发射时触发槽函数
 */
XConnection* XObject_connect_1(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type);

/**
 * @brief 为对象的信号绑定槽函数（无接收者，同步触发，对标 Qt 6.8 QObject::connect）
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）
 * @param slot_func 槽函数指针（XSlotFunc2 类型）
 * @return 连接对象指针（XConnection*），失败返回 NULL
 * @note 建立信号与槽的关联，当信号发射时触发槽函数，无接收者故同步触发
 */
XConnection* XObject_connect_2(XObject* object, size_t signal, XSlotFunc2 slot_func);

/**
 * @brief 信号绑定辅助宏，用于生成信号标识
 * @param signal 信号函数名
 * @return 信号对应的标识值
 * @note 将信号函数转换为统一的标识格式，方便信号与槽的绑定
 */
#define XSignal(signal)      ((size_t)((void*(*)(XObject*))(signal))(NULL))

/**
 * @brief 断开对象信号与接收者槽函数的连接（对标 Qt 6.8 QObject::disconnect）
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）
 * @param receiver 接收槽函数的对象指针（非 NULL）
 * @param slot_func1 槽函数指针（XSlotFunc 类型）
 * @return 断开成功返回 true，失败返回 false
 */
bool XObject_disconnect_1(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func1);

/**
 * @brief 通过连接对象断开信号与槽的连接（对标 Qt 6.8 QObject::disconnect）
 * @param conn 连接对象指针（XConnection*，非 NULL）
 * @return 断开成功返回 true，失败返回 false
 */
bool XObject_disconnect_2(XConnection* conn);

/**
 * @brief 栈区对象延迟释放（对标 Qt 6.8 QObject::deleteLater 语义）
 * @param object 待释放的 XObject 对象实例（非 NULL）
 * @note 与 XClass_deinit_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 */
void XObject_deinitLater(XObject* object);

/**
 * @brief 堆区对象延迟释放（对标 Qt 6.8 QObject::deleteLater）
 * @param object 待释放的 XObject 对象实例（非 NULL）
 * @note 与 XClass_delete_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 */
void XObject_deleteLater(XObject* object);

Signals

/**
 * @brief 对象销毁信号（对标 Qt 6.8 QObject::destroyed）
 * @param object 发送信号的 XObject 对象指针（非 NULL）
 * @return 信号标识
 * @note 槽函数原型：void deinit_slot(XObject* receiver, XVarList* argList, XObject* m_sender)
 *       用于通知相关对象当前对象即将释放
 */
void* XObject_destroyed_signal(XObject* object);

/**
 * @brief 对象名称改变信号（对标 Qt 6.8 QObject::objectNameChanged）
 * @param object 发送信号的 XObject 对象指针
 * @param objectName 新的对象名称
 * @return 信号标识
 */
void XObject_objectNameChanged_signal(XObject* object, const XString* objectName);

/**
 * @brief 精简信号发射宏（同步发射）
 * @param object 发送信号的对象指针（可为 NULL，为 NULL 时不发射）
 * @param signal 信号标识
 * @param args 信号参数（可为 NULL）
 * @param del 参数释放回调（可为 NULL）
 * @param ref_count 引用计数（可为 NULL）
 * @param priority 信号优先级
 * @return 信号标识
 * @note 自动检查对象有效性，立即发射信号，触发绑定的槽函数
 */
#define XEmitSignal(object,signal,args,del,ref_count,priority) if(object&&((XObject*)object)->m_signalSlot)XObject_emitSignal(object,(size_t)(signal),args,del,ref_count,priority);return (void*)(size_t)(signal)

/**
 * @brief 精简信号发射宏（队列模式，异步发射）
 * @param object 发送信号的对象指针（可为 NULL，为 NULL 时不发射）
 * @param signal 信号标识
 * @param args 信号参数（可为 NULL）
 * @param del 参数释放回调（可为 NULL）
 * @param ref_count 引用计数（可为 NULL）
 * @param priority 信号优先级
 * @return 信号标识
 * @note 自动检查对象有效性，将信号加入事件队列，异步触发槽函数
 */
#define XEmitSignalQueue(object,signal,args,del,ref_count,priority) if(object)XObject_emitSignal_queue(object,(size_t)(signal),args,del,ref_count,priority);return (void*)(size_t)(signal)

/**
 * @brief 同步发射信号
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识
 * @param args 信号参数（可为 NULL）
 * @param del 参数释放回调（可为 NULL，用于释放 args 资源）
 * @param ref_count 引用计数（可为 NULL，用于管理参数生命周期）
 * @param priority 信号优先级
 */
void XObject_emitSignal(XObject* object, size_t signal, XVarList * args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, XEventPriority priority);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XObject_create
#define XObject_create() XObject_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
