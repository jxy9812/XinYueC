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
#include"XNamespace.h"
/**
 * @brief 开始定义XObject类的虚函数表枚举
 * @note 用于实现C语言中的多态机制，枚举值对应虚函数在表中的索引
 */
XCLASS_DEFINE_BEGING(XObject)
XCLASS_DEFINE_ENUM(XObject, Poll) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XObject, Event),
XCLASS_DEFINE_ENUM(XObject, EventFilter),
XCLASS_DEFINE_ENUM(XObject, ChildEvent),
XCLASS_DEFINE_ENUM(XObject, ConnectNotify),
XCLASS_DEFINE_ENUM(XObject, CustomEvent),
XCLASS_DEFINE_ENUM(XObject, DisconnectNotify),
XCLASS_DEFINE_ENUM(XObject, TimerEvent),
XCLASS_DEFINE_END(XObject)

/**
 * @brief XObject类结构体定义，是所有对象的基类
 * @note 继承自XClass，包含对象基础属性和行为控制成员
 */
typedef struct XObject
{
    XClass m_class;                      ///< 继承的基类成员，包含虚函数表指针（实现多态）
	//XAtomic_bool m_deleteState;          ///< 原子布尔变量：标记对象是否处于删除状态（线程安全）
	XAtomic_uint32_t m_posted_events;       ///< 原子无符号整数：已投递但未处理的事件计数
	
	// 对象标志位
	uint32_t is_widget : 1;         // 是否为窗口部件
	uint32_t block_sig : 1;         // 信号是否被阻塞
	uint32_t was_deleted : 1;       // 对象是否已被标记为删除
	uint32_t is_deleting_children : 1; // 是否正在删除子对象
	uint32_t send_child_events : 1; // 是否发送子对象事件
	uint32_t receive_child_events : 1; // 是否接收子对象事件
	uint32_t is_window : 1;         // 是否为窗口 (用于 XWindow)
	uint32_t delete_later_called : 1; // deleteLater 是否已被调用
	uint32_t is_quick_item : 1;     // 是否为 Quick 项
	uint32_t will_be_widget : 1;    // 构造时将变为窗口部件
	uint32_t was_widget : 1;        // 析构时曾是窗口部件
	uint32_t receive_parent_events : 1; // 是否接收父对象事件
	uint32_t unused : 20;           // 保留位
	XSignalSlot* m_signalSlot;           ///< 信号与槽控制器：管理当前对象的所有信号与槽连接关系
	// 父子关系
	XObject* parent;//父对象
	XVector* children; //子对象列表
	XObject* sender;              // 发送者对象
	XVector* filters;//过滤器列表
	XThread* m_thread;                   ///< 所属线程指针：对象关联的线程，事件处理在该线程执行
	// 事件与元对象
	//int posted_events;              // 已投递但未处理的事件计数
	//void* meta_object;              // 动态元对象数据 (void* 作为占位)
	//void* binding_storage;          // 绑定存储 (void* 作为占位)

	// 对象名称
	char* object_name;
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
// 获取对象名称
const char* XObject_objectName(const XObject* self);
// 设置对象名称
void XObject_setObjectName(XObject* self, const char* name);
/**
 * @brief 获取当前对象的父对象
 * @param object 目标XObject对象指针（非NULL）
 * @return 父对象指针，若没有父对象则返回NULL
 */
XObject* XObject_parent(XObject* object);
/**
 * @brief 设置当前对象的父对象
 * @param object 目标XObject对象指针（非NULL）
 * @param parent 父对象指针（可为NULL，表示无父对象）
 * @note 用于构建对象树，影响事件冒泡和对象生命周期管理
 */
void XObject_setParent(XObject* object, XObject* parent);
// 获取子对象列表 (返回内部指针，勿修改)
const XVector* XObject_children(const XObject* self);
// 对象类型查询
// ------------------------
// 检查是否为窗口部件类型
bool XObject_isWidgetType(const XObject* self);
// 检查是否为窗口类型
bool XObject_isWindowType(const XObject* self);
// 获取对象所属线程
XThread* XObject_thread(const XObject* self);
/**
 * @brief 将对象移动到指定线程
 * @param object 目标XObject对象指针（非NULL）
 * @param thread 目标线程指针（非NULL）
 * @return 移动成功返回true，失败返回false
 * @note 移动后，对象的事件处理将在目标线程执行，确保线程安全
 */
bool XObject_moveToThread(XObject* object, XThread* thread);

// ------------------------
// 信号与槽
// ------------------------
// 检查信号是否被阻塞
bool XObject_signalsBlocked(const XObject* self);
// 阻塞或取消阻塞信号，返回之前的阻塞状态
bool XObject_blockSignals(XObject* self, bool block);

// ------------------------
// 定时器
// ------------------------
// 启动一个定时器 (毫秒)
XTimerId XObject_startTimer_ms(XObject* self, int interval, XTimerType timerType);
// 启动一个定时器 (纳秒)
XTimerId XObject_startTimer_ns(XObject* self, uint64_t interval_ns, XTimerType timerType);
// 停止一个定时器 (通过 TimerId 类型)
void XObject_killTimer(XObject* self, XTimerId timerId);


// ------------------------
// 事件系统
// ------------------------
// 安装事件过滤器
void XObject_installEventFilter(XObject* self, XObject* filterObj);
// 移除事件过滤器
void XObject_removeEventFilter(XObject* self, XObject* obj);


typedef  XVector XObjectList;
// ------------------------
// 对象查找
// ------------------------
// 查找一个子对象 (通过名称)
XObject* XObject_findChild(const XObject* self, const char* name, XFindChildOption options);
// 查找所有匹配的子对象 (通过名称)
XObjectList* XObject_findChildren(const XObject* self, const char* name, XFindChildOption options);
// 查找所有匹配的子对象 (通过正则表达式)
//XObjectList XObject_findChildren_re(const XObject* self, const XRegularExpression* re, XFindChildOption options);


// ------------------------
// 动态属性
// ------------------------
// 设置动态属性
//bool XObject_setProperty(XObject* self, const char* name, XVariant* value);
//// 获取动态属性
//XVariant* XObject_property(const XObject* self, const char* name);
//// 获取所有动态属性的名称列表
//XByteArray* XObject_dynamicPropertyNames(const XObject* self);
// ------------------------
// 调试
// ------------------------
// 打印对象树结构（调试用）
//void XObject_dumpObjectTree(const XObject* self);
//// 打印对象详细信息（调试用）
//void XObject_dumpObjectInfo(const XObject* self);


// ========================
// 受保护的虚函数与辅助函数 (需由子类实现或使用)
// ========================
// 注意：这些函数应在子类初始化时通过函数指针进行重写或直接调用。

// ------------------------
// 核心事件处理
// ------------------------
// 核心事件处理器。返回 true 表示事件已被处理。
bool XObject_event_base(XObject* self, XEvent* e);

// 事件过滤器。当 watched 对象收到事件时，会先调用此过滤器。
bool XObject_eventFilter_base(XObject* self, XObject* watched, XEvent* event);

// ------------------------
// 特定事件回调
// ------------------------
// 定时器事件回调。当定时器触发时被调用。
void XObject_timerEvent_base(XObject* self,XTimerEvent* event);

// 子对象事件回调。当子对象被添加、移除或销毁时被调用。
void XObject_childEvent_base(XObject* self,XChildEvent* event);

// 自定义事件回调。用于处理用户自定义的事件类型。
void XObject_customEvent(XObject* self,XEvent* event);

// ------------------------
// 信号/槽连接通知
// ------------------------
// 连接通知。当有信号连接到此对象的槽时被调用。
void XObject_connectNotify_base(XObject* self, size_t signal);

// 断开连接通知。当有信号从此对象的槽断开时被调用。
void XObject_disconnectNotify_base(XObject* self, size_t signal);

// ------------------------
// 信号/槽上下文查询 (仅在槽函数内有效)
// ------------------------
// 获取当前激活的信号的发送者。
XObject* XObject_sender(const XObject* self);

// 获取触发当前槽函数的信号在其发送者类中的索引。
//int XObject_senderSignalIndex(const XObject* self);

// ------------------------
// 连接状态查询
// ------------------------
// 检查指定的信号是否有任何连接。
bool XObject_isSignalConnected(const XObject* self, size_t signal);

// 返回连接到此对象上某个信号的接收者数量。
int XObject_receivers(const XObject* self, size_t signal);


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
 * @brief 向对象投递异步执行的函数
 * @param object 目标XObject对象指针（非NULL）
 * @param func 待执行的函数指针（原型：void (*)(void*)）
 * @param argList 传递给函数的参数（可为NULL）
 * @param del 参数的释放回调函数（可为NULL，用于释放args资源）
 * @param mode 发送模式（XEVENT_SEND_DIRECT：直接投递；XEVENT_SEND_QUEUED：队列投递）
 * @param priority 执行优先级（如XEVENT_PRIORITY_LOWEST）
 * @return 投递成功返回true，失败返回false
 * @note 函数将在对象所属线程的事件循环中执行，实现异步调用
 */
//bool XObject_postFunc(XObject* object, void (*func)(void*), void* argList, void(*del)(void*), XEventSendMode mode, XEventPriority priority);

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
XAbstractEventDispatcher* XObject_eventDispatcher(XObject* object);

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
void XObject_deinitLater(XObject* object);

/**
 * @brief 堆区对象延迟释放
 * @param object 待释放的XObject对象实例（非NULL）
 * @note 与XClass_delete_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 */
void XObject_deleteLater(XObject* object);

/**
 * @brief 释放信号（槽函数原型对应的信号）
 * @param object 发送信号的XObject对象指针（非NULL）
 * @return 信号标识
 * @note 槽函数原型：void deinit_slot(XObject* receiver, XVarList* argList, XObject* m_sender)
 *       用于通知相关对象当前对象即将释放
 */
void* XObject_deinit_signal(XObject* object);

/**
* @brief 精简信号发射宏（同步发射）
* @param object 发送信号的对象指针（可为NULL，为NULL时不发射）
* @param signal 信号标识
* @param argList 信号参数（可为NULL）
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
* @param argList 信号参数（可为NULL）
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
 * @param argList 信号参数（可为NULL）
 * @param del 参数释放回调（可为NULL，用于释放args资源）
 * @param ref_count 引用计数（可为NULL，用于管理参数生命周期）
 * @param priority 信号优先级
 */
void XObject_emitSignal(XObject* object, size_t signal, XVarList * args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, XEventPriority priority);

/**
* @brief 异步发射信号（队列模式，事件循环中延迟发送,可用在中断中,延迟发送）
* @param object 发送信号的对象指针（非NULL）
* @param signal 信号标识
* @param argList 信号参数（可为NULL）
* @param del 参数释放回调（可为NULL，用于释放args资源）
* @param ref_count 引用计数（可为NULL，用于管理参数生命周期）
* @param priority 信号优先级
*/
void XObject_emitSignal_queue(XObject* object, size_t signal, void* args, void(*del)(void*), XAtomic_int32_t* ref_count, XEventPriority priority);

#ifdef __cplusplus
}
#endif
#endif