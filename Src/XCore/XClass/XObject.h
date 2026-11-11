/**
 * @file       XObject.h
 * @brief      XObject 对象基类公开 API。
 * @details    对齐 Qt 6.8 QObject：提供对象树（父子关系）、事件系统（过滤器、
 *             投递/发送）、定时器、信号与槽、动态属性与延迟删除等对象基础设施。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 */
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
#if XRegularExpression_ON
/* 正则表达式前向声明（对标 Qt 6.8 qobject.h:41 `class QRegularExpression;`）：
 * 只在 found 家族的正则重载签名里出现，完整定义由调用方包含
 * XRegularExpression.h；XObject.c 才 include 它的头（与 Qt 在
 * qobject.cpp 里 include qregularexpression.h 同构）。 */
typedef struct XRegularExpression XRegularExpression;
#endif
#define XSIGNALS
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
    XClass m_class;                          ///< 第一个成员，由 XClass 管理，禁止手工修改（含虚函数表指针，实现多态）
    XAtomic_Int32 m_posted_events;         ///< 已投递但未处理的事件计数（对标 Qt 6.8 QAtomicInt）

    /* 对象标志位 */
    uint32_t m_is_widget : 1;                ///< 是否为窗口部件
    uint32_t m_block_sig : 1;                ///< 信号是否被阻塞
    uint32_t m_was_deleted : 1;              ///< 对象是否已被标记为删除
    uint32_t m_is_deleting_children : 1;     ///< 是否正在删除子对象
    uint32_t m_send_child_events : 1;        ///< 是否发送子对象事件
    uint32_t m_receive_child_events : 1;     ///< 是否接收子对象事件
    uint32_t m_is_window : 1;                ///< 是否为窗口（用于 XWindow）
    uint32_t m_delete_later_called : 1;      ///< deleteLater 是否已被调用
    uint32_t m_is_quick_item : 1;            ///< 是否为 Quick 项
    uint32_t m_will_be_widget : 1;           ///< 构造时将变为窗口部件
    uint32_t m_was_widget : 1;               ///< 析构时曾是窗口部件
    uint32_t m_receive_parent_events : 1;    ///< 是否接收父对象事件
    uint32_t m_deferred_delete_is_release : 1; ///< 待处理的延迟删除是否要**释放内存**（deleteLater）而不是只反初始化（deinitLater）；后到的 deleteLater 会把 deinitLater 升级为释放
    uint32_t m_unused : 19;                  ///< 保留位
    XObject* m_currentChildBeingDeleted;     ///< 当前正在删除的子对象（对标 QObjectPrivate）
    XSignalSlot* m_signalSlot;               ///< 信号与槽控制器，管理当前对象的所有信号与槽连接关系

    /* 父子关系 */
    XObject* m_parent;                       ///< 父对象
    XVector* m_children;                     ///< 子对象列表
    XVector* m_filters;                      ///< 事件过滤器列表
    XAtomic_Uintptr m_threadData;          ///< 线程亲和性数据（对标 QObjectPrivate::threadData）

    /* 对象名称 */
    XString* m_object_name;                  ///< 对象名称

    /* 动态属性存储（对标 QObjectPrivate::ExtraData） */
    XVector* m_dynamicPropertyNames;         ///< 动态属性名列表（XString*）
    XVector* m_dynamicPropertyValues;        ///< 动态属性值列表（XVariant*）

    /* 事件过滤器回登记：本对象作为过滤器所观察的对象列表（懒创建，对象拥有），
     * 供本对象先亡时从各被观察对象的 m_filters 中反注册，防止悬挂引用 */
    XVector* m_watchedObjects;               ///< 本对象作为过滤器所观察的对象列表（XObject*）
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
 * @brief 创建对象并立即挂到指定父对象下（对标 Qt 6.8
 *        `explicit QObject(QObject *parent = nullptr)`）
 * @param memory 对象内存方法
 * @param parent 父对象指针（可为 NULL，等价于 XObject_create_ex）
 * @return 新对象指针，失败返回 NULL
 * @note Qt 的 `new QObject(parent)` 在 C 侧对应本函数：先
 *       `XObject_create_ex` 再 `XObject_setParent`。setParent 失败（跨线程
 *       父对象等）时对象仍被创建，但不会挂到 parent 上——与 Qt 的
 *       "构造期 setParent 不生效则保持无父" 同口径。
 */
XObject* XObject_createWithParent_ex(XMemoryType memory, XObject* parent);

/** @brief 按默认内存方法创建并挂到 parent 下（对标 `new QObject(parent)`）。 */
#define XObject_createWithParent(parent) \
	XObject_createWithParent_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))

/**
 * @brief 获取对象名称（对标 Qt 6.8 QObject::objectName）
 * @param self 目标对象指针
 * @return 对象名称字符串指针，未设置返回 NULL
 */
const XString* XObject_objectName(const XObject* self);

/**
 * @brief 设置对象名称（对标 Qt 6.8 QObject::setObjectName）
 * @param self 目标对象指针
 * @param name 要设置的名称字符串指针；**NULL 表示清空名称**（对标 Qt 的
 *        `setObjectName(QString())`——空 QString 即"无名字"，本机 Qt 实测
 *        清空后 `objectName().isNull()==true` 且**发**一次 objectNameChanged）
 * @note 同值不发射 `objectNameChanged`（Qt 同口径）；名称真的变化时发射一次。
 * @note XString 没有 Qt 的 null 态（空串的 `XString_toUtf8` 返回 NULL，
 *       XString.c:511），故 X 侧"清空"落地为"删除名称对象"，`XObject_objectName`
 *       随之返回 NULL——与 Qt 的 `isNull()==true` 观测等价。旧实现把 NULL
 *       入参当 no-op（C 的"空指针=不改"惯例），与 Qt 的"空串=清空"相反，
 *       2026-11-11 按 Qt 口径改为清空。
 */
void XObject_setObjectName(XObject* self, const XString* name);

/**
 * @brief 获取当前对象的父对象（对标 Qt 6.8 QObject::parent）
 * @param object 目标 XObject 对象指针（非 NULL）
 * @return 父对象指针，若无父对象返回 NULL
 * @note Qt 6.8 的签名是 `QObject *parent() const`（qobject.h:130）——只读，
 *       不改对象状态，故取 const 入参；返回的父指针仍是可变借用（Qt 同样
 *       返回非 const 的 QObject*）。
 */
XObject* XObject_parent(const XObject* object);

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
 * @note **命名保留 `children`（不加 `_const`）是有意的**：Qt 6.8 的签名是
 *       `const QList<QObject*> &children() const`（qobject.h），返回的是
 *       借用引用但名字没有 const/borrow 后缀；本仓库的 `_const` 后缀用于
 *       「同一功能存在**拷贝版与借用版两个入口**」时区分所有权（如
 *       `XObject_dynamicPropertyNames` / `_const`、`XObject_property_copy` /
 *       `_const`）。children 只有借用这一个入口、没有拷贝版，加后缀反而
 *       制造"另一个版本在哪"的误导，故与 Qt 同名。返回指针在对象析构或
 *       子对象增删后失效。
 */
const XVector* XObject_children(const XObject* self);

/**
 * @brief 获取父对象子表中本对象的上一个相邻兄弟对象（对标 Qt 6.8
 *        QStyleSheetStyle::previousSiblingNode 的兄弟查询需求）
 * @param self 目标对象指针
 * @return 相邻兄弟对象指针（借用指针，勿释放）；无父对象、父对象无子表
 *         或本对象是第一个子对象时返回 NULL
 * @note 实现为父容器子表顺序查找；子表中的空位（正在析构的子对象）不跳过，
 *       与 Qt children() 相邻语义一致。供 QSS 兄弟组合符 '+'/'~' 匹配使用
 * @note **Qt 6.8 的 qobject.h 没有公开 previousSibling（项目有意保留）**：
 *       Qt 的兄弟查询是 `QStyleSheetStyle` 内部的 `previousSiblingNode`；
 *       X 侧 QSS 需要同一能力，且已有 2 处回归断言
 *       （xgui_regression_test.c 的兄弟导航用例）依赖它，故作为公共 API 保留。
 */
XObject* XObject_previousSibling(const XObject* self);

/**
 * @brief 获取父对象子表中本对象的下一个相邻兄弟对象（对标 Qt 6.8
 *        QStyleSheetStyle 兄弟节点查询语义）
 * @param self 目标对象指针
 * @return 相邻兄弟对象指针（借用指针，勿释放）；无父对象、父对象无子表
 *         或本对象是最后一个子对象时返回 NULL
 * @note 实现为父容器子表顺序查找；子表空位不跳过。供 QSS 兄弟组合符
 *       '+'/'~' 匹配使用
 * @note **Qt 6.8 无 `nextSibling` 公开 API（项目有意保留）**：Qt 6.8 的
 *       qobject.h 全无 nextSibling（只有 QStyleSheetStyle 私有的兄弟节点
 *       语义）；X 侧保留是因为 `xgui_regression_test.c` 的兄弟导航断言与
 *       QSS '+'/'~' 匹配都用它，删除无处改名。
 */
XObject* XObject_nextSibling(const XObject* self);

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

/* ======================== 类元信息（对标 Qt 6.8 QMetaObject 的最小集） ========================
 * Qt 的类身份来自 `Q_OBJECT` 生成的 `staticMetaObject`：每个类有
 * `className()`，并通过 `superClass()` 连成继承链；`QObject::inherits()`
 * 与 `qobject_cast<T>` / `findChild<T>` 都沿这条链比对。X 没有 moc，
 * **类身份的事实源是虚函数表**——`XVTABLE_INIT_DEFAULT(类名)` 把类名写进
 * `m_name`，`XVTABLE_INHERIT_XCLASS(父类)` 把父类表登记进 `m_parent`
 * （见 XVtable.h）。因此：
 *   - 类名 = `XVTABLE_GET_NAME(XClassGetVtable(self))`（等价 className()）
 *   - 父链 = `XVtable_parent(...)` 逐级上溯（等价 superClass()）
 *   - 「是不是某个类」= 沿父链找到**同一张类表**（等价 qobject_cast 的
 *     `staticMetaObject.cast`，即 inherits 语义而非「精确类型相等」）
 * 注意「类名」不止 `XVTABLE_INIT_DEFAULT(Type)` 一条路：少数类因为按父类
 * 槽位容量建表而用 `XVTABLE_INIT_DEFAULT_SIZE(父类槽数)`，此时必须紧跟
 * 一句 `XCLASS_SET_CLASS_NAME_DEFAULT("类名")`（图表轴 6 类与
 * XAbstractScrollAreaFilter 即此形态，2026-11-11 补齐）。
 * 本轮实现最小可用集；完整的 staticMetaObject 数据表（信号/槽/属性/
 * 枚举登记、bindingStorage、bindableObjectName）仍 deferred。 */

/**
 * @brief 取对象所属类的类名（对标 Qt 6.8 `metaObject()->className()`）
 * @param self 目标对象指针
 * @return 类名字符串（**借用**，进程生命周期，禁止释放）；类名未编译进
 *         构建（XCLASS_VTABLE_NAME_ON=0）或 self 为 NULL 时返回 "XObject"
 * @note Qt 的 className() 返回的是 moc 生成的**不带命名空间**的类名；
 *       X 的类名来自 `XVTABLE_INIT_DEFAULT(Xxx)` 的 `#Type`（按父类容量
 *       建表的类则由 `XCLASS_SET_CLASS_NAME_DEFAULT` 显式登记），与之一致。
 */
const char* XObject_className(const XObject* self);

/**
 * @brief 判断对象是否**是**某个类或其后代（对标 Qt 6.8 QObject::inherits）
 * @param self 目标对象指针
 * @param className 类名（区分大小写，Qt 同口径）
 * @return 命中返回 true；self/className 为 NULL 或链上没有该类返回 false
 * @note 语义对齐 Qt 6.8 `bool QObject::inherits(const char *className) const`
 *       （qobject.h:348 的内联实现 = `qt_metacast(classname) != nullptr`；
 *       元对象侧同义接口见 qobjectdefs.h:236-239 的
 *       `QMetaObject::inherits(const QMetaObject*)`）：从自身类沿
 *       `superClass()`（qobjectdefs.h:676-677）逐级比对类名，
 *       **不是**精确类型相等——`QWidget` 对象 `inherits("QObject")` 为 true。
 *       本机 Qt 6.8.0 offscreen 实测：`QPushButton` 对象
 *       `inherits("QWidget")==true`、`inherits("QObject")==true`、
 *       `inherits("QPushButton")==true`；`QWidget` 对象
 *       `inherits("QPushButton")==false`；类名**大小写敏感**
 *       （`inherits("qwidget")==false`）。该断言已锁进
 *       Test/QtAlign/XObjectAlignTest.c 的 `classMetadata` 用例与
 *       Test/QtAlign/XClassMetaAlignTest.c。
 */
bool XObject_inherits(const XObject* self, const char* className);

/**
 * @brief `XObject_inherits` 的别名（Qt 习惯的 isA 叫法）
 * @param self 目标对象指针
 * @param className 类名（区分大小写）
 * @return 同 XObject_inherits：对象是该类或其派生类时返回 true
 * @note 单独提供是为了让调用点写成 `XObject_isA(obj, "XWidget")` 的表达力；
 *       实现直接转调 XObject_inherits，**语义完全一致**（不是精确类型相等，
 *       也不是按类表指针的 `XObject_isA_class`）。
 */
bool XObject_isA(const XObject* self, const char* className);

/**
 * @brief 判断对象是否可转换为指定类（对标 Qt 6.8 `qobject_cast<T*>` /
 *        `QMetaObject::inherits(const QMetaObject*)`）
 * @param self 目标对象指针
 * @param classVtable 目标类的虚函数表（如 `XWidget_class_init()` 返回值）
 * @return 可转换返回 true
 * @note 等价于 Qt 的 `T::staticMetaObject.cast(obj) != nullptr`：沿父链
 *       比对**类表指针**。比 `XObject_inherits`/`XObject_isA`（按类名）更
 *       严格——同名不同模块的两张表不会互相命中；X 的 class_init 是每类
 *       一份单例，故 `XObject_isA_class(obj, XWidget_class_init())` 对
 *       XWidget 及其子类恒真。
 * @note C 没有模板，**类表指针就是 findChild 的类型实参**：
 *       `XObject_findChild_class(root, name, XWidget_class_init(), opt)`
 *       与 Qt 的 `findChild<QWidget*>(name, opt)` 同义。
 */
bool XObject_isA_class(const XObject* self, const XVtable* classVtable);

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
 * @note 语义对齐 Qt 6.8（本机实测）：
 *       ① 阻塞期间**任何信号都不派发**（直接同步发射与队列发射都在发射
 *          时刻被拦下，队列帧根本不会投递）；
 *       ② `destroyed()` **不受阻塞影响**——Qt 文档明确 "destroyed() is
 *          emitted even if signals are blocked"，本实现按信号标识放行；
 *       ③ 阻塞位是**布尔位**（非嵌套计数）：Qt 实测连续两次 blockSignals(true)
 *          第二次同样返回 true、两次 unblock 后恢复，本实现同口径；
 *       ④ 返回值是**调用前**的阻塞状态（Qt 实测 prev=false → 阻塞后
 *          signalsBlocked()==true → 再 unblock 返回 prev=true）。
 */
bool XObject_blockSignals(XObject* self, bool block);

/**
 * @brief 信号阻塞守卫（对标 Qt 6.8 QSignalBlocker，qobject.h:482-565）
 * @note C 没有 RAII，故以「初始化/销毁成对函数」表达同一语义：
 *       ```c
 *       XSignalBlocker blocker;
 *       XSignalBlocker_init(&blocker, obj);   // 等价 QSignalBlocker b(obj);
 *       ...                                    // 期间 obj 的信号被阻塞
 *       XSignalBlocker_destroy(&blocker);      // 等价 b 出作用域：恢复原阻塞状态
 *       ```
 *       Qt 的 QSignalBlocker 在析构时恢复**构造前**的阻塞状态（不是无条件
 *       解开）：构造时若对象已被阻塞，析构后仍是阻塞——本实现同口径，
 *       用 `m_wasBlocked` 记住前态。作用域内重复构造（嵌套守卫）同样成立：
 *       内层记住"已被阻塞"，退出时保持阻塞，最外层退出才解开。
 *       本机 Qt 6.8 探针实测（/tmp/probe_conn.cpp）：
 *         block(true) 后建 blocker → 退出后 signalsBlocked()==true；
 *         block(false) 后建 blocker → 退出后 signalsBlocked()==false。
 *       `XSignalBlocker_init` 传 NULL 对象时守卫失效（destroy 为 no-op）。
 * @note **中途改主意**（对标 Qt 6.8 qobject.h:495-497 的
 *       `reblock()/unblock()/dismiss()`，实现 qobject.h:548-565）：
 *       ```c
 *       XSignalBlocker bl; XSignalBlocker_init(&bl, obj);
 *       XSignalBlocker_unblock(&bl);   // 立刻恢复进入前状态；dtor 不再动对象
 *       XSignalBlocker_reblock(&bl);   // 重新阻塞；dtor 恢复进入前状态
 *       XSignalBlocker_dismiss(&bl);   // 放弃守卫：dtor 什么都不做（对象保持阻塞）
 *       ```
 *       三者与本机 Qt 6.8 探针逐条同口径：
 *         - `unblock()` 恢复的是 `m_wasBlocked`（**进入前**的状态，不是无条件
 *           false——进入前已阻塞的对象 unblock 后仍阻塞），并置 inhibit 位
 *           使 destroy 不再动对象；
 *         - `reblock()` 无条件 `blockSignals(true)`，并**清掉** inhibit 位
 *           （Qt 的实现同样不恢复前态：后续 destroy 会按 `m_wasBlocked`
 *           恢复，探针实测 reblock 后出作用域回到进入前状态）；
 *         - `dismiss()` 只清 `m_object`（不改变对象当前阻塞状态），此后
 *           destroy / reblock / unblock 对对象均无副作用（Qt 只清 m_o、
 *           不动 inhibit 位，本实现同口径）。
 *       重复 `destroy` 仍然安全（第二次起 no-op，inhibit 或 dismiss 后
 *       同样安全）。
 * @note 命名：`_init` / `_destroy` 与本框架栈值类型的 `_init` / `_deinit`
 *       风格一致；`_ctor` / `_dtor` 是同一实现对 Qt 构造/析构读法的别名，
 *       保留给既有调用点（行为完全相同）。
 * @warning 守卫必须**先于被守卫对象**离开作用域（Qt 同款约束）：对象析构后
 *          再 destroy 会对已释放内存调 blockSignals。需要跨对象生命周期
 *          时不要用栈守卫。
 */
typedef struct XSignalBlocker
{
	XObject* m_object;      /**< 被守卫的对象（借用，不取得所有权） */
	bool m_wasBlocked;      /**< 进入前的阻塞状态（销毁时按此恢复） */
	bool m_inhibited;       /**< true：destroy 不再动对象（对标 Qt 的 m_inhibited，
	                             由 unblock() 置位、reblock() 清位） */
} XSignalBlocker;

/**
 * @brief 初始化信号阻塞守卫（相当于 QSignalBlocker 的构造函数）
 * @param self 守卫存储（调用方栈上分配，非 NULL）
 * @param object 要阻塞的对象；NULL 时守卫为空操作
 * @note 进入即 `blockSignals(true)`，并把**进入前**的阻塞状态记入
 *       `m_wasBlocked`；嵌套守卫靠这一份前态自然成立。inhibit 位清零
 *       （对标 Qt 构造函数 `m_inhibited(false)`）。
 */
void XSignalBlocker_init(XSignalBlocker* self, XObject* object);

/**
 * @brief 销毁信号阻塞守卫（相当于 QSignalBlocker 的析构函数）
 * @param self 守卫存储；可重复调用（第二次起为 no-op）
 * @note 把对象恢复到**进入前**的阻塞状态（不是无条件 false）。
 *       inhibit 位为真（曾调 unblock）或 `m_object` 已被 dismiss 清空时
 *       **不触碰对象**——对标 Qt `~QSignalBlocker` 的
 *       `if (m_o && !m_inhibited) m_o->blockSignals(m_blocked);`。
 */
void XSignalBlocker_destroy(XSignalBlocker* self);

/**
 * @brief 重新阻塞守卫对象（对标 Qt 6.8 QSignalBlocker::reblock，qobject.h:548-553）
 * @param self 守卫存储；NULL 或已 dismiss 时安全
 * @note `m_object` 非 NULL 时执行 `blockSignals(true)`（无条件阻塞，
 *       不参考前态）；随后**清掉** inhibit 位，使 destroy 再按
 *       `m_wasBlocked` 恢复。与 Qt 同口径：不恢复前态。
 */
void XSignalBlocker_reblock(XSignalBlocker* self);

/**
 * @brief 提前解除守卫（对标 Qt 6.8 QSignalBlocker::unblock，qobject.h:555-560）
 * @param self 守卫存储；NULL 或已 dismiss 时安全
 * @note 把对象恢复到 `m_wasBlocked`（**进入前**的阻塞状态），并**置上**
 *       inhibit 位——之后 destroy 不再动对象。与 Qt 同口径。
 */
void XSignalBlocker_unblock(XSignalBlocker* self);

/**
 * @brief 放弃守卫（对标 Qt 6.8 QSignalBlocker::dismiss，qobject.h:562-565）
 * @param self 守卫存储；NULL 时安全
 * @note 只清 `m_object`，**不改变对象当前的阻塞状态**——典型用法下对象
 *       保持被守卫时的阻塞态（Qt 探针实测：dismiss 后出作用域
 *       `signalsBlocked()` 仍为 true；若 dismiss 之前先 unblock，则保持
 *       unblock 后的状态）。此后 destroy 为 no-op。
 */
void XSignalBlocker_dismiss(XSignalBlocker* self);

/**
 * @brief `XSignalBlocker_init` 的 Qt 命名别名（兼容既有调用点）。
 * @param self 守卫存储（非 NULL）
 * @param object 要阻塞的对象；NULL 时守卫为空操作
 */
void XSignalBlocker_ctor(XSignalBlocker* self, XObject* object);

/**
 * @brief `XSignalBlocker_destroy` 的 Qt 命名别名（兼容既有调用点）。
 * @param self 守卫存储；可重复调用（第二次起为 no-op）
 */
void XSignalBlocker_dtor(XSignalBlocker* self);

/* ======================== 定时器 ======================== */

/**
 * @brief 启动一个定时器（毫秒，对标 Qt 6.8 QObject::startTimer）
 * @param self 目标对象指针
 * @param interval 定时器间隔（毫秒）；0 合法，表示"尽快触发"
 * @param timerType 定时器精度类型
 * @return 定时器 ID；失败返回 XTIMER_INVALID_ID(0)
 * @note 对齐 Qt 6.8（本机实测）：
 *       ① `startTimer(0)` 返回**合法 ID**（Qt 实测 id=1）；X 的调度器以纳秒
 *          为单位且拒绝 0（`XAbstractEventDispatcher.c` 的
 *          `interval == 0` 分支），故 0 毫秒按 1 纳秒下限注册——效果同 Qt
 *          的"每轮事件循环尽快触发"；
 *       ② **跨线程拒绝**：Qt 实测 `startTimer` 从非对象所属线程调用返回 0
 *          并告警 "Timers cannot be started from another thread"，本实现
 *          同口径（告警 + 返回 XTIMER_INVALID_ID）；
 *       ③ 无事件调度器（对象所属线程尚未建立）时返回 XTIMER_INVALID_ID。
 */
XTimerId XObject_startTimer_ms(XObject* self, uint64_t interval, XTimerType timerType);

/**
 * @brief 启动一个定时器（纳秒，对标 Qt 6.8
 *        `QObject::startTimer(std::chrono::nanoseconds)`）
 * @param self 目标对象指针
 * @param interval_ns 定时器间隔（纳秒）；0 合法，表示"尽快触发"
 * @param timerType 定时器精度类型
 * @return 定时器 ID；失败返回 XTIMER_INVALID_ID(0)
 * @note 跨线程调用与 0 间隔语义同 XObject_startTimer_ms（见其说明）。
 */
XTimerId XObject_startTimer_ns(XObject* self, uint64_t interval_ns, XTimerType timerType);

/**
 * @brief 停止一个定时器（对标 Qt 6.8 QObject::killTimer）
 * @param self 目标对象指针
 * @param timerId 要停止的定时器 ID
 * @note 对齐 Qt 6.8（本机实测）：**跨线程调用被拒绝**并告警
 *       "Timers cannot be stopped from another thread"（Qt 只告警，函数返回
 *       void，不会去动别人线程的定时器表）。
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
 * @param name 子对象名称；**NULL 或空串表示"不按名字过滤"**
 * @param options 查找选项
 * @return 匹配的子对象指针（借用），未找到返回 NULL
 * @note 查找顺序对齐 Qt（本机 Qt 6.8.0 offscreen 实测）：先按**直接子对象**
 *       顺序找一遍，命中即返回；只有 options 含 ChildrenRecursively 时才继续
 *       递归到各子树（因此"浅层优先"；孙子层的对象只有在没有直接子对象命中时
 *       才会返回）。
 * @note **空名字语义是项目有意偏差**：Qt 6.8 的参数是 `QAnyStringView`，
 *       `findChild<T>({})`（null）匹配全部，而 `findChild<T>("")`（非 null
 *       空串）只匹配 objectName() 为空的对象——本机实测：
 *       `root2.findChild<QObject*>(QString())` 返回第一个**有名**子对象，
 *       `findChild<QObject*>(QString(""))` 返回第一个**无名**子对象。
 *       XString 没有 Qt 的 null 态（AGENTS.md 已知限制：空串的
 *       XString_toUtf8 返回 NULL），C 侧无法区分「null QString」与「空串」；
 *       本仓库取 Qt **文档**口径 "If name is empty, all objects are matched"，
 *       NULL 与空 XString 一律不过滤。QtAlign 用例
 *       `XObject.findChild` 锁定该口径。
 */
XObject* XObject_findChild(const XObject* self, const XString* name, XFindChildOption options);

/**
 * @brief 查找所有匹配的子对象（对标 Qt 6.8 QObject::findChildren）
 * @param self 目标对象指针
 * @param name 子对象名称；NULL 或空串表示"不按名字过滤"（同 XObject_findChild）
 * @param options 查找选项
 * @return 匹配的子对象列表指针（新建堆对象，元素为借用指针，由调用方拥有），
 *         未找到返回空列表；用完必须由调用方 XClassDelete 释放
 * @note 遍历顺序对齐 Qt：**深度优先前序**——先父对象、再按 children 顺序对
 *       每个子对象「命中就追加，然后递归它的子树」。本机 Qt 6.8.0 实测：
 *       根下依次有 c1、c2，c1 下再挂 gc 时，`findChildren<QObject*>(QString())`
 *       的顺序是 `c1, gc, c2`（不是"先所有直接子对象"`c1, c2, gc`）。
 *       ⚠ 注意 Qt 的 findChild（单个）是**浅层优先**，两者顺序语义不同，
 *       不能共用一份递归实现。
 */
XObjectList* XObject_findChildren(const XObject* self, const XString* name, XFindChildOption options);

/**
 * @brief 查找一个子对象，按**类类型**过滤（对标 Qt 6.8 `findChild<T>(name, options)`）
 * @param self 目标对象指针
 * @param name 子对象名称；NULL 或空串表示"不按名字过滤"（同 XObject_findChild）
 * @param classVtable 目标类的虚函数表（如 `XWidget_class_init()`），即 C 侧
 *        的「类型参数」——与 Qt 模板实参 `T::staticMetaObject` 同构；
 *        **NULL 表示不做类型过滤**（等价于 Qt 的 `findChild<QObject*>`，
 *        或 `XObject_isA_class` 的 NULL 豁免）
 * @param options 查找选项
 * @return 匹配的子对象指针（借用），未找到返回 NULL
 * @note Qt 6.8 的 `findChild<T>` 用 `T::staticMetaObject.cast(child)` 过滤
 *       （qobject.h:154-199）——**含派生类**，不是精确类型相等。本机实测
 *       `findChild<QWidget*>` 能命中 QPushButton、`findChild<QPushButton*>`
 *       不能命中 QWidget。本函数同口径地走 `XObject_isA_class`（沿父链比对类表
 *       指针），比按类名字符串更严格：同名不同库的两张表不会互相命中。
 * @note 「名字 + 类型」两个条件是 **AND**（Qt 同口径）；查找顺序与
 *       XObject_findChild 完全一致（先直接子对象、再按 ChildrenRecursively
 *       递归），已由 QtAlign 用例 `XClassMeta.findChildByClass` 锁定。
 */
XObject* XObject_findChild_class(const XObject* self, const XString* name,
                                 const XVtable* classVtable, XFindChildOption options);

/**
 * @brief 查找所有匹配的子对象，按**类类型**过滤（对标 Qt 6.8 `findChildren<T>`）
 * @param self 目标对象指针
 * @param name 子对象名称；NULL 或空串表示"不按名字过滤"
 * @param classVtable 目标类的虚函数表；NULL 表示不做类型过滤
 * @param options 查找选项
 * @return 匹配的子对象列表（新建堆对象，元素为借用指针，由调用方拥有），
 *         用完必须由调用方 XClassDelete 释放
 * @note 类型过滤语义同 XObject_findChild_class（见 XObject_isA_class 的父链比对）。
 *       遍历顺序同 XObject_findChildren（Qt 实测的**深度优先前序**：
 *       `c1, gc, c2`），与单个 findChild 的浅层优先不同。
 */
XObjectList* XObject_findChildren_class(const XObject* self, const XString* name,
                                        const XVtable* classVtable, XFindChildOption options);

/* ==================== 按正则表达式的 findChild/findChildren
 * （对标 Qt 6.8 qobject.h:186-198 `findChildren<T>(const QRegularExpression&, options)`）
 * ==================== */

#if XRegularExpression_ON
/**
 * @brief 查找所有**对象名匹配正则**的子对象（对标 Qt 6.8
 *        `findChildren<T>(const QRegularExpression &re, options)`）
 * @param self 目标对象指针
 * @param expression 正则表达式；NULL 或**无效模式**一律**不命中任何对象**
 *        （Qt 对无效 QRegularExpression 的匹配结果为"无匹配"，本机 Qt 6.8
 *        实测 `findChildren(QRegularExpression("("))` 返回空表）
 * @param options 查找选项（递归开关）
 * @return 匹配的子对象列表（新建堆对象，元素为借用指针，由调用方拥有；
 *         无匹配返回**空列表**，self 为 NULL 返回 NULL），
 *         用完必须由调用方 `XClassDelete` 释放
 *
 * @note **匹配口径与 Qt 逐条同源**（qobject.cpp:2152-2166
 *       `qt_qFindChildren_helper(parent, re, mo, list, options)`）：
 *       ① 主题是 `obj->objectName()`——**对象未设名字按空串**参与匹配
 *          （所以空模式 `""`、`"a*"` 都能命中无名对象；`".+"` 不能）；
 *       ② `re.match(subject)` 是**部分匹配**（非锚定）：模式 `"e"` 命中
 *          `"deep"`，想锚定首尾要自己写 `"^d"` / `"e$"`；
 *       ③ 遍历顺序同 `XObject_findChildren`：**深度优先前序**
 *          （命中就追加，然后递归该子树）；本机 Qt 6.8 实测根下
 *          a/b（b 下挂 deep）时 `findChildren(re("e"))` 得到 `[deep]`，
 *          `findChildren(re(""))` 得到 `[a, b, deep, 无名]`；
 *       ④ 与类型过滤是 **AND**（Qt 的模板实参 `T`）；本函数是
 *          `findChildren<QObject*>` 的等价物。
 * @note 每次匹配都经 `XRegularExpression_match` 生成一个匹配结果并立即
 *       释放（Qt 侧同样是值返回的 QRegularExpressionMatch），
 *       返回的列表元素是**借用**指针，随对象树变化失效。
 */
XObjectList* XObject_findChildren_regex(const XObject* self,
                                        const XRegularExpression* expression,
                                        XFindChildOption options);

/**
 * @brief 查找所有"**类类型匹配 + 对象名匹配正则**"的子对象（对标 Qt 6.8
 *        `findChildren<T>(const QRegularExpression &re, options)` 的**完整**语义）
 * @param self 目标对象指针
 * @param classVtable 目标类的虚函数表（C 侧的模板实参 `T::staticMetaObject`）；
 *        NULL 表示不做类型过滤（等价于 `findChildren<QObject*>`）
 * @param expression 正则表达式；NULL/无效模式不命中任何对象（同
 *        XObject_findChildren_regex）
 * @param options 查找选项
 * @return 匹配的子对象列表（新建堆对象，元素为借用指针，由调用方拥有），
 *         用完必须由调用方 `XClassDelete` 释放
 * @note 两个条件是 **AND**：先 `XObject_isA_class`（含派生类）再正则匹配
 *       objectName；遍历顺序与匹配口径同 XObject_findChildren_regex。
 */
XObjectList* XObject_findChildren_class_regex(const XObject* self,
                                              const XVtable* classVtable,
                                              const XRegularExpression* expression,
                                              XFindChildOption options);

/**
 * @brief 查找一个"对象名匹配正则"的子对象（**X 扩展**，非 Qt 6.8 成员）
 * @param self 目标对象指针
 * @param expression 正则表达式；NULL/无效模式不命中任何对象
 * @param options 查找选项
 * @return 匹配的子对象指针（借用），未找到返回 NULL
 * @note **Qt 6.8 没有单对象正则重载**：`qobject.h:186-198` 只有
 *       `findChildren<T>(const QRegularExpression&, options)`（本机头文件
 *       `/usr/include/x86_64-linux-gnu/qt6/QtCore/qobject.h` 已核对，
 *       qobject.cpp:2152 也只提供 `qt_qFindChildren_helper` 的正则版，
 *       没有 `qt_qFindChild_helper(re)`）。本函数为 C 侧查找家族补齐
 *       「单个对象 + 正则」形态：**查找顺序与 XObject_findChild 一致**
 *       （先直接子对象、再按 ChildrenRecursively 递归，即浅层优先），
 *       而不是 findChildren 的深度优先前序——需要 Qt 的列表语义请用
 *       XObject_findChildren_regex 并取首元素。
 */
XObject* XObject_findChild_regex(const XObject* self,
                                 const XRegularExpression* expression,
                                 XFindChildOption options);

/**
 * @brief 查找一个"类类型匹配 + 对象名匹配正则"的子对象（**X 扩展**，
 *        对标 `findChild<T>` 的类型过滤 + 正则名字条件；非 Qt 6.8 成员）
 * @param self 目标对象指针
 * @param classVtable 目标类的虚函数表；NULL 表示不做类型过滤
 * @param expression 正则表达式；NULL/无效模式不命中任何对象
 * @param options 查找选项
 * @return 匹配的子对象指针（借用），未找到返回 NULL
 * @note 两个条件是 AND；查找顺序同 XObject_findChild_regex（浅层优先）。
 *       Qt 侧对应的表达能力在 `findChildren<T>(re, options)` 里，没有
 *       单对象重载（见 XObject_findChild_regex 的说明）。
 */
XObject* XObject_findChild_class_regex(const XObject* self,
                                       const XVtable* classVtable,
                                       const XRegularExpression* expression,
                                       XFindChildOption options);
#endif /* XRegularExpression_ON */

/* ======================== 动态属性（对标 Qt 6.8 QObject::setProperty/property/dynamicPropertyNames） ======================== */

/**
 * @brief 设置动态属性（对标 Qt 6.8 QObject::setProperty）
 * @param self 目标对象指针
 * @param name 属性名
 * @param value 属性值（XVariant*）；**调用即转移所有权**（成功/失败都由本函数
 *              接管并释放，见下），传 NULL 表示移除该动态属性
 *              （对标 Qt 写无效 QVariant 即删除）
 * @return 对齐 Qt 6.8 的语义：**仅当命中静态（类声明的）属性时返回 true**；
 *         写入**动态**属性恒返回 false（哪怕写入成功）
 *
 * @warning **返回值的含义是 Qt 语义，不是"所有权是否被接管"**：
 *          Qt 6.8 `QObject::setProperty`（qobject.h:334）返回的是
 *          "有没有静态 Q_PROPERTY 接受这次写入"。本机 Qt 6.8 实测：
 *          `obj.setProperty("dyn", 42)` → **false**，而
 *          `obj.dynamicPropertyNames().size()==1`（值确实写进去了）。
 *          本仓库没有 moc，静态属性层未实现（见「类元信息」小节的
 *          deferred 说明），因此**当前恒返回 false**；将来接入静态属性
 *          登记后，命中静态属性返回 true。
 *
 * @warning **所有权与返回值解耦（2026-11-11 口径翻转）**：本函数**总是**
 *          接管 `value`（等价 Qt 的按值 `const QVariant&` 形参——Qt 侧
 *          调用方不保留任何所有权）。失败路径（OOM/参数非法）由本函数
 *          自行释放 `value`，调用方**不要再释放**。旧口径是"返回值 = 是否
 *          接管"，7 处调用点按 `if (!XObject_setProperty(...)) 释放(value)`
 *          书写；口径翻转后这些调用点已同步改为"调用即不释放"（XToolButton.c /
 *          XWidget.c ×2 / XMenu.c ×2 / XFileDialog.c / XStateMachine.c）。
 *
 * @note 属性值真的发生变化时，本函数会向对象自身同步投递一个
 *       `XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE` 事件（对标 Qt 的
 *       `QEvent::DynamicPropertyChange`，见 XDynamicPropertyChangeEvent）。
 */
bool XObject_setProperty(XObject* self, const XString* name, XVariant* value);

/**
 * @brief 获取动态属性（借用；对标 Qt 6.8 `QObject::property`）
 * @param self 目标对象指针
 * @param name 属性名
 * @return 属性值指针（**借用**，禁止释放），不存在返回 NULL
 * @note 动态属性读取的**唯一入口**：Qt 的 `property()` 按值返回 `QVariant`
 *       （拷贝），本实现返回对象内部的借用指针——零拷贝换生命周期约束，
 *       故按仓库「`_const` = 借用」约定命名。借用指针在
 *       `setProperty(name, NULL)`（删除动态属性）/ 对象析构后立即失效。
 */
XVariant* XObject_property_const(const XObject* self, const XString* name);

/**
 * @brief 按值读取动态属性（对标 Qt 6.8 `QObject::property` 的**返回值语义**）
 * @param self 目标对象指针
 * @param name 属性名
 * @return 属性值的**独立副本**（调用方拥有，用完必须 `XClassDelete`）；
 *         属性不存在返回 NULL
 * @note Qt 的 `QVariant property(const char *name) const`（qobject.h:336）按值
 *       返回 `QVariant` 拷贝，调用方改副本不影响对象。C 侧没有值类型返回值，
 *       故以「堆副本」表达同一语义；零拷贝读取仍走 `XObject_property_const`
 *       （借用指针，随对象/属性变更失效）。二者与 JSON/BSON 的
 *       `value_keyUtf8`（借用）/`value_copy_keyUtf8`（副本）是同一条所有权约定。
 */
XVariant* XObject_property_copy(const XObject* self, const XString* name);

/* 【已删除 API：XObject_property（2026-11-11 API 收敛批次）】
 * 与本文件 `XObject_property_const` 同签名、同语义、同实现（后者曾直接转调
 * 它），是纯重复入口；Qt 6.8 也只有 `QObject::property` 一个入口，没有
 * 「无后缀 = 拷贝 / `_const` = 借用」并存的第二个读取函数。全仓库调用点
 * （XToolButton.c / XWidget.c / XMenu.c / XFileDialog.c / XStyleSheetStyle.c /
 * XStateMachine.c / XStateMachineTest.c / xgui_regression_test.c / 本模块对齐
 * 测试）已全部改用 `XObject_property_const`。 */

/**
 * @brief 获取所有动态属性的名称列表（对标 Qt 6.8 QObject::dynamicPropertyNames）
 * @param self 目标对象指针
 * @return **新建**的属性名列表（元素为 XString* 副本），调用方拥有，
 *         用完必须 `XClassDelete` 释放（元素随容器一起释放）；无属性时返回
 *         空列表（非 NULL），self 为 NULL 时返回 NULL
 * @note Qt 按值返回 `QList<QByteArray>`（拷贝语义）；需要零拷贝借用时用
 *       `XObject_dynamicPropertyNames_const`。
 */
XVector* XObject_dynamicPropertyNames(const XObject* self);

/**
 * @brief 获取所有动态属性名列表（借用版）
 * @param self 目标对象指针
 * @return 内部名列表（**借用**，禁止释放/修改），无属性或 self 为 NULL
 *         返回 NULL
 * @note 随对象析构或 `setProperty(name, NULL)`（删除动态属性）失效。
 */
const XVector* XObject_dynamicPropertyNames_const(const XObject* self);

/* 【已删除 API：XObject_removeProperty（2026-11-11 API 收敛批次）】
 * Qt 6.8 **没有** `QObject::removeProperty`：删除动态属性的唯一入口是
 * `setProperty(name, QVariant())`（qobject.h:332-336 只有 setProperty /
 * property 两个入口，QVariant 无效值即删除——本机 Qt 6.8 实测
 * dynamicPropertyNames().size() 由 1 变 0）。本仓库因此删掉这条冗余入口，
 * 删除语义统一由 `XObject_setProperty(self, name, NULL)` 表达
 * （XObject_setProperty 的 `value == NULL` 分支，行为与旧函数逐条一致：
 * 属性不存在静默返回、删除**存在**的属性发一次
 * `XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE`）。
 * 全仓库调用点（XToolButton.c / XWidget.c / XMenu.c / XFileDialog.c 与
 * 本模块对齐测试）已全部改为 `(void)XObject_setProperty(self, name, NULL)`。 */

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
 * @return 信号标识；**未在信号发射上下文中调用时返回 -1**
 * @note 对齐 Qt 6.8：Qt 的 `senderSignalIndex()` 在非槽上下文返回 -1
 *       （本机实测槽内为元方法索引 4、槽外为 -1）。本实现同样返回 -1
 *       （经 XThreadData_currentSenderSignalIndex 的失败值），旧版本文档
 *       写的"返回 0"与实现不符，已按实际行为更正。
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
 * @note **公开面比 Qt 宽（项目有意保留）**：Qt 把 threadData 放在
 *       QObjectPrivate（qobject_p.h:203）里，只有 QObject 自己的成员函数和
 *       少数 friend 用得到；X 的 C 侧没有 private 段，而 XCore 内部有 12 处
 *       外部调用（XCoreApplication.c / XEventLoop.c / XThreadData.c），
 *       公共头是唯一可共享的入口，故保留在 XObject.h。语义与 Qt 一致：
 *       返回**借用**指针，不增加引用计数。
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
 * @note **Qt 无同名公开成员（项目有意保留）**：Qt 侧等价物是
 *       `QAbstractEventDispatcher::instance(QThread*)`（另一个类上的静态
 *       工厂），对象自身不暴露 dispatcher。X 侧 10 处调用都是「拿到某个
 *       对象的线程 → 往那个线程的调度器上注册/注销定时器」，从对象出发取
 *       比再传线程更直接；语义相同（借用指针）。
 */
XAbstractEventDispatcher* XObject_eventDispatcher(const XObject* object);

/**
 * @brief 打印对象树结构（对标 Qt 6.8 QObject::dumpObjectTree，调试用）
 * @param self 对象指针
 * @note 输出格式对齐 Qt 6.8 实测：每行 `<类名>::<objectName>` + 一个空格，
 *       每层缩进 4 个空格，根对象不缩进；类名取对象虚函数表登记的类名
 *       （`XVTABLE_GET_NAME`），无名对象输出 `<类名>::`。递归层级由参数
 *       传递（不再用函数级 static，线程安全）。
 */
void XObject_dumpObjectTree(const XObject* self);

/**
 * @brief 打印对象详细信息（对标 Qt 6.8 QObject::dumpObjectInfo，调试用）
 * @param self 对象指针
 * @note 输出格式对齐 Qt 6.8 实测：
 *       ```
 *       OBJECT <类名>::<objectName>
 *         SIGNALS OUT
 *               <signal 0x...> -> <类名>::<objectName>
 *         SIGNALS IN
 *               <-- <发送者类名>::<发送者名>
 *       ```
 *       无连接时对应小节输出 `<None>`。X 的信号标识是信号函数地址，故
 *       SIGNALS OUT 行以地址代替 Qt 的 `signal: timeout()` 签名。
 */
void XObject_dumpObjectInfo(const XObject* self);

/**
 * @brief 为对象的信号绑定接收者的槽函数（信号与槽连接，对标 Qt 6.8 QObject::connect）
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）
 * @param receiver 接收槽函数的对象指针（非 NULL）
 * @param slot_func 槽函数指针（XSlotFunc1 类型）
 * @param type 连接类型（直接调用/队列调用等）
 * @return 连接对象指针（XConnection*），失败返回 NULL
 * @note 建立信号与槽的关联，当信号发射时触发槽函数
 * @note **无效信号 id**（对标 Qt 6.8 `QObject::connect` 的 "No such signal"
 *       告警 + 返回无效连接）：`signal == 0` 是 C 侧**唯一可判定**的非法值，
 *       本函数返回 NULL 并打 `XObject::connect: No such signal (id=0)`。
 *       非 0 地址无法反查合法性（X 的信号 id = 信号函数在定义模块内的地址，
 *       无 moc 元数据表）——按名字/签名校验需要每个信号登记签名表，属新
 *       子系统，登记为 deferred；**不会**静默为 0 号信号建桶。
 *       本机 Qt 探针实测：`connect(sender, SIGNAL(noSuchSignal(int)), ...)`
 *       打印 "QObject::connect: No such signal"、返回的 Connection 为 false，
 *       对其再 disconnect 返回 false。
 * @note 返回的 `XConnection*` 即**连接句柄**（`XConnectionHandle`，对标
 *       Qt 的 `QMetaObject::Connection`）：要跨断开保存需先
 *       `XConnection_retain`，用完 `XConnection_release`。
 */
XConnection* XObject_connect_1(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type);

/**
 * @brief 带 context 对象的函数对象连接（对标 Qt 6.8
 *        `connect(sender, signal, context, functor, type)`，qobject.h:226-271）
 * @param object 发送信号的对象（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）
 * @param context **生命周期/线程亲和上下文**（非 NULL）；连接在该对象析构时
 *        自动断开（登记进 context 的 bindSignalList，随其 XSignalSlot 反注册）
 * @param slot_func 槽函数（XSlotFunc2 类型：首参为**发送者**，不是 context）
 * @param type 连接类型；`Auto` 按「发送线程 vs context 线程」判定直接/队列
 * @return 连接对象指针（XConnection*），失败返回 NULL
 * @note Qt 的 context 重载解决两个问题，本函数同口径：
 *       ① **生命周期**：functor 没有接收者对象，若不绑定 context，发送者
 *          活着而上下文（窗口/会话）先亡时槽会打到已释放的捕获状态上。
 *          Qt 在 context 析构时断开该连接；X 侧复用"连接挂在 receiver 的
 *          bindSignalList、receiver 的 XSignalSlot 析构时反注册"这条既有链路，
 *          故 context 必须参与 receiver 簿记——这是它和 `XObject_connect_2`
 *          （无 receiver、不随任何对象析构）的关键区别。
 *       ② **线程亲和**：`Auto` 连接按 context 所属线程判定队列/直接派发
 *          （`XConnection_autoEmit` 用 `XObject_thread(conn->receiver)`）。
 * @note 派发时槽的**首参仍是发送者**（对标 Qt 把信号参数原样交给 functor）：
 *       内部用 `XConnectionType_ContextReceiver` 标记"receiver 只做生命周期
 *       簿记、不做槽首参"，队列派发同样按该标记投递（XMetaCallEvent 的
 *       functor 标志）。
 * @warning `type` 只接受 Auto/Direct/Queued/BlockingQueued 与 Unique/SingleShot
 *          组合。**不要求** sender 与 context 同线程（Qt 的 connect 路径没有
 *          这条限制；跨线程时 `Auto` 退化为队列派发，槽在 context 所属线程
 *          执行——见 XConnection_autoEmit）。
 */
XConnection* XObject_connect_context(XObject* object, size_t signal, XObject* context,
                                     XSlotFunc2 slot_func, XConnectionType type);

/**
 * @brief 为对象的信号绑定槽函数（无接收者，同步触发，对标 Qt 6.8 QObject::connect）
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）
 * @param slot_func 槽函数指针（XSlotFunc2 类型）
 * @return 连接对象指针（XConnection*），失败返回 NULL
 * @note 建立信号与槽的关联，当信号发射时触发槽函数，无接收者故同步触发。
 *       [Qt 背景警示] 槽被调用时 receiver 参数恒为**发送者 object 自身**
 *       （连接挂在发送者上，无独立接收者）——不是 Qt 四参 connect 里
 *       的接收者。槽内需要其它对象上下文时改用 connect_1 显式传
 *       receiver，或在槽内经 sender 派生；把 receiver 当 Qt 语义的
 *       接收者强转会在发信即错型（以发送者内存布局解读别的类型）。
 */
XConnection* XObject_connect_2(XObject* object, size_t signal, XSlotFunc2 slot_func);

/**
 * @brief 取信号函数的规范标识（跨模块稳定）。
 * @param fn 信号函数（首参为对象指针，返回自身地址）。
 * @return 信号标识值。
 * @details 信号标识取「信号函数在**其定义模块内**的地址」。各信号函数在
 *          self==NULL 时统一返回 `(void*)(size_t)(自身名)`（契约见各 signal
 *          实现），故此处以 NULL 调用取其返回值，得到的是定义模块内的真址。
 *
 *          **为何必须取返回值而不是直接取地址**：跨 DLL 边界时，调用方
 *          （可执行文件）侧 `&fn` 拿到的是 MSVC 增量链接生成的**导入跳板
 *          地址**，而信号发射侧（动态库内）用的是**真实函数地址**，两者不等
 *          → signalMap 查不中 → 槽函数永不触发（表现为菜单动作/信号槽
 *          静默失效）。经 NULL 调用取返回值则两侧同为定义模块真址，天然一致。
 *
 * @note  参数经统一原型 `void*(*)(XObject*)` 中转：各 signal 原型首参类型
 *        与参数个数各异（`(XAction*, bool)`、`(XObject*)` 等），直接强转调用
 *        会触发 MSVC「function called through a non-compatible type」(C4198)
 *        告警；经本统一原型中转可规避（2026-10-25 实测）。
 */
size_t XObject_signalIdOf(void* (*fn)(XObject*));

/**
 * @brief 信号绑定辅助宏，用于生成信号标识
 * @param signal 信号函数名
 * @return 信号对应的标识值
 * @note 信号标识=信号函数在**其定义模块内**的地址。取地址直转
 *       （`((size_t)(signal))`）在同模块内正确，但跨 DLL 边界时调用方侧
 *       取得的是导入跳板地址，与动态库发射侧的真实函数地址不等，导致
 *       信号槽静默不触发（2026-10-25 根修：XTestMenu 菜单动作全失效）。
 *       故改为经 XObject_signalIdOf 中转：以 NULL 调用信号函数取其返回值，
 *       得到的返回值即定义模块内的真址，与发射侧一致。
 */
#define XSignal(signal)      XObject_signalIdOf((void*(*)(XObject*))(signal))

/**
 * @brief 断开本对象信号上的连接（对标 Qt 6.8
 *        `QObject::disconnect(const QObject *sender, const char *signal,
 *        const QObject *receiver, const char *member)` 的 C 映射）
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识（由 XSignal 宏生成）；**传 0 表示"任意信号"**
 *        （Qt 的 signal==nullptr）
 * @param receiver 接收槽函数的对象指针；**传 NULL 表示"任意接收者"**
 *        （Qt 的 receiver==nullptr）
 * @param slot_func1 槽函数指针；**传 NULL 表示"任意槽"**（Qt 的
 *        member==nullptr）
 * @return 至少断开一条连接返回 true；没有任何匹配返回 false（Qt 同口径）
 * @note 语义对齐 Qt 6.8（本机实测）：
 *       ① **匹配到多少条就断多少条**：同一 (signal, receiver, slot) 连过
 *          三次，一次 disconnect 后槽触发次数为 0（旧实现只断第一条，会
 *          残留一条继续触发）；
 *       ② 三个过滤条件都可以为 NULL/0 ⇒ 可表达 Qt 的四种批量重载：
 *          `disconnect(sender, signal, receiver, member)`（全指定）、
 *          `disconnect(sender, 0, receiver, NULL)`（按接收者全断）、
 *          `disconnect(sender, signal, NULL, NULL)`（按信号全断）、
 *          `disconnect(sender, 0, NULL, NULL)`（等价 Qt 实例版
 *          `sender->disconnect()`，断开本对象全部发信连接）。
 *       ③ 断开时按 Qt 6.8 口径向**信号所属对象**（发送者）发 disconnectNotify
 *          （qobject.cpp:1083/3430 都在 sender 上调用；本机探针实测发送者命中
 *          1 次、接收者 0 次）。本实现已按此口径。
 * @warning 收集匹配连接与逐条断开之间存在解锁窗口；多线程同时 connect
 *          同一 (sender,signal) 时，本函数保证"不误删不匹配的连接"，
 *          但不保证"新连接一定被本次调用断掉"（Qt 在全局锁内完成，语义
 *          更强，见 gaps）。
 */
bool XObject_disconnect_1(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func1);

/**
 * @brief 通过连接对象断开信号与槽的连接（对标 Qt 6.8 QObject::disconnect）
 * @param conn 连接对象指针（XConnection*）；**可以是已断开的旧句柄**
 * @return 本次真的断掉一条连接返回 true；句柄已失效/已断开返回 false
 * @note 句柄语义（2026-11-11 对齐 Qt 的 `QMetaObject::Connection`）：
 *       ① 连接对象带引用计数：发送者连接表持 1 个引用，断开时释放；调用方
 *          若要把句柄留到断开之后（跨作用域保存），先用 `XConnection_retain`
 *          增加引用，用完 `XConnection_release`；
 *       ② **重复断开安全**：连接对象释放时会从全局"活连接表"注销，
 *          本函数先查表再解引用——已释放的旧句柄直接返回 false，
 *          不解引用悬垂指针（Qt 的 Connection 是引用计数值类型，
 *          重复 disconnect 同样安全且返回 false）；
 *       ③ 断开时按 Qt 6.8 口径向**信号所属对象**（发送者）发 disconnectNotify。
 */
bool XObject_disconnect_2(XConnection* conn);

/**
 * @brief 栈区对象延迟释放（对标 Qt 6.8 QObject::deleteLater 语义）
 * @param object 待释放的 XObject 对象实例（非 NULL）
 * @note 与 XClass_deinit_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 * @note **Qt 无此 API（项目扩展）**：Qt 只有 `deleteLater()`。本函数是
 *       X 侧的"只反初始化、不释放内存"变体，供嵌入式/栈对象使用；现存
 *       2 处真实调用（XTcpServer.c:209/630）。若同一对象先 deinitLater 再
 *       deleteLater，后者会把待处理动作**升级为释放**（见下）；反过来的
 *       顺序（先 deleteLater 再 deinitLater）保持"释放"不变——以更重的
 *       动作为准，与 Qt 多个 DeferredDelete 事件里"先到先做"不同，但
 *       X 侧只有一个事件，故按动作强度收敛。
 */
void XObject_deinitLater(XObject* object);

/**
 * @brief 堆区对象延迟释放（对标 Qt 6.8 QObject::deleteLater）
 * @param object 待释放的 XObject 对象实例（非 NULL）
 * @note 与 XClass_delete_base（立即释放）不同，该函数将对象加入事件循环，
 *       等待所有待处理事件完成后再释放，避免未处理事件导致的错误
 * @note 捕捉调用时的 `(loopLevel, scopeLevel)`（对标 Qt 6.8 qobject.cpp:2459-2486
 *       的 deleteLater：`loopLevel = threadData->loopLevel; scopeLevel =
 *       threadData->scopeLevel;`，以及「scopeLevel==0 而 loopLevel!=0 时
 *       取 1」的兜底），投递判据见 XDeferredDeleteEvent_shouldDeliver。
 *       `QCoreApplication::exec()` 返回前会补一拍「显式点名 DeferredDelete」
 *       的派发（对标 Qt `QCoreApplicationPrivate::execCleanup`），故退出时
 *       已排队的延迟删除仍会兑现。
 */
void XObject_deleteLater(XObject* object);

XSIGNALS

/**
 * @brief 对象销毁信号（对标 Qt 6.8 QObject::destroyed）
 * @param object 发送信号的 XObject 对象指针（非 NULL）
 * @return 信号标识
 * @note 槽函数原型：void deinit_slot(XObject* receiver, XVarList* argList, XObject* m_sender)
 *       用于通知相关对象当前对象即将释放
 * @note **携带被销毁对象作为唯一参数**（对标 Qt 的
 *       `void destroyed(QObject *obj = nullptr)`）：参数值与
 *       `XObject_sender(receiver)` 相同，槽可直接取 `XVarList_args_1(argList, XObject*, obj)`
 *       读出——Qt 的槽签名就带这个参数，旧实现发 NULL args 使按 Qt 写法移植
 *       的槽拿不到对象。参数是**借用指针**（不转移所有权，列表释放时不销毁
 *       该对象，销毁由延迟删除事件负责）。
 */
void* XObject_destroyed_signal(XObject* object);

/**
 * @brief 对象名称改变信号（对标 Qt 6.8 QObject::objectNameChanged）
 * @param object 发送信号的 XObject 对象指针
 * @param objectName 新的对象名称；**NULL 表示"清空"**（发射无载荷信号）
 * @return 信号标识（=本函数在定义模块内的地址；`object==NULL` 时用于
 *         `XSignal(...)` 采集信号 ID，此时不发射且**不触碰** objectName）
 */
void* XObject_objectNameChanged_signal(XObject* object, const XString* objectName);

/**
 * @brief 精简信号发射宏（同步发射）
 * @param object 发送信号的对象指针（可为 NULL，为 NULL 时不发射）
 * @param signal 信号标识
 * @param args 信号参数（可为 NULL）
 * @param del 参数释放回调（可为 NULL）
 * @param ref_count 引用计数（可为 NULL）
 * @param priority 信号优先级
 * @return 信号标识
 * @note 自动检查对象有效性，立即发射信号，触发绑定的槽函数。实参
 *       object 统一显式上行转换 `(XObject*)`（与守护分支同型）：各派生
 *       类 signal 函数直传派生指针会在每个发射点触发 arg1 不兼容指针
 *       告警（门禁构建日志 error 子串扫描被 error_signal/errorOccurred
 *       等命名命中而误判 BUILD-FAIL，2026-10-21 根修）
 */
#define XEmitSignal(object,signal,args,del,ref_count,priority) if(object&&((XObject*)object)->m_signalSlot)XObject_emitSignal((XObject*)(object),(size_t)(signal),args,del,ref_count,priority);return (void*)(size_t)(signal)

/* 【已删除 API：XEmitSignalQueue（2026-11-10 Qt 6.8 对齐批次）】
 * 原宏展开为 `XObject_emitSignal_queue(object, ...)`，但该函数在全仓库
 * **没有实现**（只有本宏这一处引用），任何使用点都会在链接期报未定义符号；
 * 且 Qt 6.8 没有对应 API——Qt 的队列语义由 connect 时的
 * Qt::QueuedConnection 决定，不在发射侧选择。全仓库零调用点，按
 * 「Qt 无对应 + 功能被覆盖 + 零调用」删除。
 * 需要队列语义请用 `XObject_connect_1(..., XConnectionType_Queued)`。 */

/**
 * @brief 同步发射信号
 * @param object 发送信号的对象指针（非 NULL）
 * @param signal 信号标识
 * @param args 信号参数（可为 NULL）
 * @param del 参数释放回调（可为 NULL，用于释放 args 资源）
 * @param ref_count 引用计数（可为 NULL，用于管理参数生命周期）
 * @param priority 信号优先级
 */
void XObject_emitSignal(XObject* object, size_t signal, XVarList * args, void(*del)(XVarList*), XAtomic_Int32* ref_count, XEventPriority priority);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XObject_create
#define XObject_create() XObject_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
