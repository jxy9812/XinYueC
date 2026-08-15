// XThreadData.h - 线程私有数据（内部使用，完整对标 Qt 6.8 QThreadData）
#ifndef XTHREADDATA_H
#define XTHREADDATA_H
#include "XSync_config.h"
#if XSYNC_ON
#if XTHREADDATA_ON

/* XThread forward declaration: XThreadData only borrows the pointer; still needed when XTHREAD_ON is off. */
typedef struct XThread XThread;
#include "XAbstractEventDispatcher.h"
#include "XMutex.h"
#include "XVector.h"
#include "XStack.h"
#include "XLockFreeQueue.h"
#include "XAtomic.h"
#include "XSemaphore.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 投递事件（对标 Qt QPostEvent）
 * @param receiver 事件接收者对象
 * @param event 待投递事件
 * @param priority 事件优先级（越大越优先）
 */
typedef struct {
    XObject* receiver;
    XEvent* event;
    int      priority;
} XPostEvent;

/**
 * @brief 发送者栈帧（对标 Qt QObjectPrivate::Sender）
 * @param receiver 信号接收者
 * @param sender 信号发送者
 * @param signal 当前信号索引（对标 QObjectPrivate::Sender::signal）
 */
typedef struct {
    XObject* receiver;
    XObject* sender;
    size_t  signal;
} XSenderFrame;

/**
 * @brief 线程私有数据（完整对标 Qt 6.8 QThreadData）
 *
 * 每个线程拥有一个 XThreadData，记录事件循环栈、投递队列、事件分发器等。
 * Qt 6.8 QThreadData 字段对照:
 *   _ref                    -> m_ref                    (引用计数)
 *   loopLevel               -> m_loopLevel              (事件循环嵌套层级)
 *   scopeLevel              -> m_scopeLevel             (作用域层级)
 *   eventLoops              -> m_eventLoops             (事件循环栈)
 *   postEventList           -> m_postEventList          (投递事件队列)
 *   thread                  -> m_thread                 (所属线程)
 *   threadId                -> m_threadId               (线程ID)
 *   eventDispatcher         -> m_eventDispatcher        (事件分发器)
 *   tls                     -> m_tls                    (线程局部存储)
 *   quitNow                 -> m_quitNow                (立即退出标志)
 *   canWait                 -> m_canWait                (是否可阻塞等待)
 *   isAdopted               -> m_isAdopted              (是否为 adopted 线程)
 *   requiresCoreApplication -> m_requiresCoreApplication (是否需要 QCoreApplication)
 */
typedef struct XThreadData{
    XAtomic_int32_t m_ref;            ///< 引用计数（对标 QAtomicInt _ref）
    XAtomic_size_t  m_loopLevel;      ///< 事件循环嵌套层级（对标 int loopLevel）
    int             m_scopeLevel;     ///< 作用域层级（对标 int scopeLevel）

    XStack          m_eventLoops;     ///< 事件循环栈（对标 QStack<QEventLoop*> eventLoops）

    XMutex*         m_mutex;          ///< 投递队列互斥锁（对标 QPostEventList::mutex）
    XVector         m_postEventList;  ///< 投递事件队列（对标 QPostEventList postEventList）
    XLockFreeQueue  m_tryPostEventList;     ///< 无锁投递队列（中断安全，扩展字段）
    XStack          m_activePostEventLists; ///< 正在派发的本地事件批次（扩展字段）

    XThread*        m_thread;         ///< 所属线程（对标 QAtomicPointer<QThread> thread）
    XHandle         m_threadId;       ///< 线程ID（对标 QAtomicPointer<void> threadId）
    XAbstractEventDispatcher* m_eventDispatcher; ///< 事件分发器（对标 QAtomicPointer<QAbstractEventDispatcher>）
    //XVector*        m_tls;            ///< 线程局部存储（对标 QList<void*> tls）

    XAtomic_bool    m_canWait;        ///< 是否可阻塞等待（对标 bool canWait）
    bool            m_quitNow;        ///< 立即退出标志（对标 bool quitNow）
    bool            m_isAdopted;      ///< 是否为 adopted 线程（对标 bool isAdopted）
    bool            m_requiresCoreApplication; ///< 是否需要 QCoreApplication（对标 bool requiresCoreApplication）

    XSemaphore*     m_wakeSemaphore;  ///< 唤醒信号量（工作线程阻塞等待用，扩展字段）
    XStack          m_senderStack;    ///< 发送者栈（对标 QObjectPrivate::senderStack）
} XThreadData;

/**
 * @brief 创建平台事件分发器（需平台层实现）
 * @param parent 父对象，可为 NULL
 * @return 新创建的事件分发器
 */
XAbstractEventDispatcher* XEventDispatcher_create_ex(XMemoryType memory,  XObject* parent);

/* ======================== 构造/析构/引用计数（对标 QThreadData 构造/析构/ref/deref） ======================== */

/**
 * @brief 创建线程数据（堆对象，对标 QThreadData 构造函数）
 * @param thread 所属线程，可为 NULL（adopted 线程）
 * @return 新创建的 XThreadData 指针，失败返回 NULL
 */
XThreadData* XThreadData_create(XThread* thread);

/**
 * @brief 初始化线程数据（栈对象，对标 QThreadData 构造函数体）
 * @param data 待初始化的线程数据
 * @param thread 所属线程，可为 NULL（adopted 线程）
 */
void XThreadData_init(XThreadData* data, XThread* thread);

/**
 * @brief 销毁线程数据（对标 ~QThreadData 析构函数）
 * @param data 待销毁的线程数据
 */
void XThreadData_delete(XThreadData* data);

/**
 * @brief 增加引用计数（对标 QThreadData::ref）
 * @param data 线程数据
 */
void XThreadData_ref(XThreadData* data);

/**
 * @brief 减少引用计数，归零时自动销毁（对标 QThreadData::deref）
 * @param data 线程数据
 */
void XThreadData_deref(XThreadData* data);

/* ======================== 线程查找（对标 QThreadData::current / get2 / clearCurrentThreadData） ======================== */

/**
 * @brief 获取当前线程的线程数据，不存在则自动创建 adopted 数据（对标 QThreadData::current）
 * @return 当前线程的 XThreadData 指针（O(1) TLS 查询，永不为 NULL）
 */
XThreadData* XThreadData_current(void);

#if XTHREAD_ON
/**
 * @brief 从 XThread 对象获取其线程数据（对标 QThreadData::get2）
 * @param thread 线程对象
 * @return 该线程的 XThreadData 指针，thread 为 NULL 时返回 NULL
 */
XThreadData* XThreadData_get2(XThread* thread);
#endif // XTHREAD_ON

/**
 * @brief 清除当前线程的 TLS 指针（对标 QThreadData::clearCurrentThreadData）
 * @note 仅清除 TLS 缓存，不减少引用计数；deref 由调用方负责（对标 Qt cleanup 流程）
 */
void XThreadData_clearCurrentThreadData(void);

/**
 * @brief TLS 析构回调（对标 Qt destroy_current_thread_data）
 * @param p 线程退出时由 pthread_key 析构传入的 XThreadData 指针
 * @note 由平台层 pthread_key 析构函数在线程退出时自动调用，清理 adopted 线程残留数据
 */
void XThreadData_destroyTlsData(void* p);

/* ======================== 主线程管理（对标 QCoreApplicationPrivate::theMainThread） ======================== */

/**
 * @brief 初始化主线程数据并绑定主线程 XThread（对标 QCoreApplicationPrivate 设置 theMainThread）
 * @param thread 主线程 XThread 对象
 * @return 主线程的 XThreadData 指针
 */
XThreadData* XThreadData_initMainThread(XThread* thread);

/**
 * @brief 获取主线程数据（对标 QCoreApplicationPrivate::theMainThread，原子读取）
 * @return 主线程的 XThreadData 指针，未初始化时返回 NULL
 */
XThreadData* XThreadData_mainThread(void);

/* ======================== 事件分发器（对标 hasEventDispatcher / createEventDispatcher / ensureEventDispatcher） ======================== */

/**
 * @brief 判断是否已创建事件分发器（对标 QThreadData::hasEventDispatcher）
 * @param data 线程数据
 * @return 已创建返回 true，否则返回 false
 */
bool XThreadData_hasEventDispatcher(XThreadData* data);

/**
 * @brief 创建事件分发器并关联到线程数据（对标 QThreadData::createEventDispatcher）
 * @param data 线程数据
 * @return 新创建的事件分发器，已存在则返回已有的
 */
XAbstractEventDispatcher* XThreadData_createEventDispatcher(XThreadData* data);

/**
 * @brief 确保事件分发器已创建，未创建则创建（对标 QThreadData::ensureEventDispatcher）
 * @param data 线程数据
 * @return 事件分发器指针
 */
XAbstractEventDispatcher* XThreadData_ensureEventDispatcher(XThreadData* data);

/* ======================== 事件循环栈（对标 QThreadData::eventLoops） ======================== */

/**
 * @brief 压入事件循环到栈（对标 eventLoops.push()）
 * @param data 线程数据
 * @param loop 要压入的事件循环
 */
void XThreadData_pushEventloop(XThreadData* data, XEventLoop* loop);

/**
 * @brief 弹出事件循环栈中指定循环（对标 eventLoops.removeAll()）
 * @param data 线程数据
 * @param loop 要弹出的事件循环
 */
void XThreadData_popEventloop(XThreadData* data, XEventLoop* loop);

/* ======================== 阻塞控制（对标 canWait / canWaitLocked） ======================== */

/**
 * @brief 查询是否可阻塞等待（无锁读取，对标直接读取 canWait 字段）
 * @param data 线程数据
 * @return 可阻塞返回 true，否则返回 false
 */
bool XThreadData_canWait(XThreadData* data);

/**
 * @brief 加锁查询是否可阻塞等待（对标 QThreadData::canWaitLocked）
 * @param data 线程数据
 * @return 可阻塞返回 true，否则返回 false
 */
bool XThreadData_canWaitLocked(XThreadData* data);

/* ======================== 事件投递（对标 postEventList / lockThreadPostEventList） ======================== */

/**
 * @brief 锁定接收者所属线程的投递队列并返回其线程数据（对标 QCoreApplication::postEvent 锁定流程）
 * @param receiver 事件接收者
 * @return 接收者所属线程的 XThreadData 指针（已加锁，需配对解锁）
 */
XThreadData* XThreadData_lockPostEventList(XObject* receiver);

/**
 * @brief 投递事件到接收者所属线程（对标 QCoreApplication::postEvent）
 * @param receiver 事件接收者
 * @param event 待投递事件
 * @param priority 事件优先级（越大越优先）
 */
void XThreadData_postEvent(XObject* receiver, XEvent* event, int priority);

/**
 * @brief 无锁尝试投递事件（中断安全，扩展接口）
 * @param receiver 事件接收者
 * @param event 待投递事件
 * @param priority 事件优先级
 * @return 投递成功返回 true，否则返回 false
 */
bool XThreadData_tryPostEvent(XObject* receiver, XEvent* event, int priority);

/**
 * @brief 将事件批次插入投递队列前端（扩展接口）
 * @param events 待插入的事件向量
 */
void XThreadData_push_front_list(const XVector* events);

/**
 * @brief 取出当前线程所有投递事件并按优先级稳定排序（对标 sendPostedEvents 取队列）
 * @return 包含投递事件的 XVector 指针，无事件返回空向量
 */
XVector* XThreadData_takePostedEvents(void);

/**
 * @brief 派发单个投递事件（对标 QCoreApplication::sendPostedEvents 派发）
 * @param post 待派发的投递事件
 * @return 派发成功返回 true，否则返回 false
 */
bool XThreadData_deliverPostedEvent(XPostEvent* post);

/**
 * @brief 丢弃单个投递事件并释放资源（对标 Qt 清理流程）
 * @param post 待丢弃的投递事件
 */
void XThreadData_discardPostedEvent(XPostEvent* post);

/**
 * @brief 将事件批次压入活跃派发栈（扩展接口，防止重入时重复派发）
 * @param events 正在派发的事件向量
 * @return 压入成功返回 true，否则返回 false
 */
bool XThreadData_pushActivePostedEvents(XVector* events);

/**
 * @brief 从活跃派发栈弹出指定事件批次（扩展接口）
 * @param events 要弹出的事件向量
 */
void XThreadData_popActivePostedEvents(XVector* events);

/**
 * @brief 丢弃活跃派发栈中匹配接收者/事件类型的事件（扩展接口）
 * @param receiver 事件接收者，NULL 表示所有接收者
 * @param eventType 事件类型，0 表示所有类型
 */
void XThreadData_discardActivePostedEvents(XObject* receiver, XEventType eventType);

/* ======================== 唤醒机制（对标 QAbstractEventDispatcher::wakeUp） ======================== */

/**
 * @brief 阻塞等待唤醒信号（工作线程事件循环阻塞用）
 * @param td 线程数据
 * @param timeoutMs 超时时间（毫秒），负数使用默认值
 */
void XThreadData_waitForWake(XThreadData* td, int timeoutMs);

/**
 * @brief 发送唤醒信号（对标 QAbstractEventDispatcher::wakeUp）
 * @param td 线程数据
 */
void XThreadData_signalWake(XThreadData* td);

/* ======================== 发送者栈（对标 QObjectPrivate::senderStack） ======================== */

/**
 * @brief 压入发送者栈帧（对标 QObjectPrivate::pushSender）
 * @param receiver 信号接收者
 * @param sender 信号发送者
 * @param signal 当前信号索引
 */
void XThreadData_pushSender(XObject* receiver, XObject* sender, size_t signal);

/**
 * @brief 弹出栈顶发送者帧（对标 QObjectPrivate::popSender）
 */
void XThreadData_popSender(void);

/**
 * @brief 获取当前接收者的发送者（对标 QObject::sender）
 * @param receiver 信号接收者
 * @return 发送者对象指针，无则返回 NULL
 */
XObject* XThreadData_currentSender(XObject* receiver);

/**
 * @brief 获取当前接收者的发送信号索引（对标 QObjectPrivate::Sender::signal）
 * @param receiver 信号接收者
 * @return 信号索引，无则返回 -1
 */
int XThreadData_currentSenderSignalIndex(XObject* receiver);

#ifdef __cplusplus
}
#endif

#endif // XTHREADDATA_ON
#endif /* XSYNC_ON */

/* XClass create API default-memory wrappers. */
#undef XEventDispatcher_create
#define XEventDispatcher_create(...) XEventDispatcher_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XTHREADDATA_H
