# XinYueC 核心代码文档

## 目录

- [概述](#概述)
- [XAction 动作](#xaction-动作)
- [XAtomic 原子操作](#xatomic-原子操作)
- [XCoreApplication 核心应用](#xcoreapplication-核心应用)
- [XMenu 菜单](#xmenu-菜单)
- [XPrintf 打印](#xprintf-打印)
- [XSignalSlot 信号与槽](#xsignalslot-信号与槽)
- [XSocketNotifier 套接字通知](#xsocketnotifier-套接字通知)
- [XSync 同步模块](#xsync-同步模块)
- [附录](#附录)

---

## 概述

XinYueC核心代码模块提供了应用程序开发的核心功能支持。

---

## XAction 动作

XAction是动作结构体，封装了文本、回调函数和用户数据。

### 头文件

```c
#include "XAction.h"
```

### 结构体定义

```c
typedef void(*Action)(XVariant* data);

typedef struct XAction {
    char* text;
    Action action;
    XVariant* data;
} XAction;
```

### 函数

#### XAction_create

```c
XAction* XAction_create(const char* text)
```

创建XAction实例。

**参数:**
- `text` - 动作文本

**返回值:** 成功返回XAction指针，失败返回NULL

---

#### XAction_init

```c
void XAction_init(XAction* action, const char* text)
```

初始化XAction实例。

---

#### XAction_setText

```c
void XAction_setText(XAction* action, const char* text)
```

设置动作文本。

---

#### XAction_setAction

```c
void XAction_setAction(XAction* action, Action func)
```

设置动作回调函数。

---

#### XAction_setData

```c
void XAction_setData(XAction* action, XVariant* data)
```

设置用户数据。

---

#### XAction_trigger

```c
void XAction_trigger(XAction* action)
```

触发动作，执行回调函数。

---

#### XAction_getText

```c
const char* XAction_getText(XAction* action)
```

获取动作文本。

---

#### XAction_getAction

```c
Action XAction_getAction(XAction* action)
```

获取动作回调函数。

---

#### XAction_getData

```c
XVariant* XAction_getData(XAction* action)
```

获取用户数据。

---

#### XAction_delete

```c
void XAction_delete(XAction* action)
```

销毁XAction实例。

---

## XAtomic 原子操作

> **平台实现**: 需要针对不同平台实现底层原子操作。
> - Windows: 使用Interlocked系列API
> - Linux/Unix: 使用GCC/Clang内置原子操作`__atomic_*`或POSIX pthread原子操作
> - macOS: 使用OSAtomic或C11原子操作

XAtomic提供跨平台的原子操作支持，包括原子加载、存储、交换、比较交换、加减运算等。

### 头文件

```c
#include "XAtomic.h"
#include "XAtomic_load.h"
#include "XAtomic_store.h"
#include "XAtomic_exchange.h"
#include "XAtomic_compare.h"
#include "XAtomic_add.h"
#include "XAtomic_sub.h"
```

---

### 内存序枚举

```c
typedef enum {
    XAtomic_MemoryOrder_Relaxed,   // 宽松内存序，只保证原子性
    XAtomic_MemoryOrder_Consume,   // 消费内存序
    XAtomic_MemoryOrder_Acquire,   // 获取内存序，用于临界区入口
    XAtomic_MemoryOrder_Release,   // 释放内存序，用于临界区出口
    XAtomic_MemoryOrder_AcqRel,    // 获取-释放内存序
    XAtomic_MemoryOrder_SeqCst     // 顺序一致性内存序（默认）
} XAtomic_MemoryOrder;
```

---

### 原子类型定义

```c
typedef struct { volatile long value; } XAtomic_bool;
typedef struct XAtomic_int32_t { volatile int32_t value; } XAtomic_int32_t;
typedef struct XAtomic_uint32_t { volatile uint32_t value; } XAtomic_uint32_t;
typedef struct XAtomic_int64_t { volatile int64_t value; } XAtomic_int64_t;
typedef struct XAtomic_uint64_t { volatile uint64_t value; } XAtomic_uint64_t;
typedef struct XAtomic_size_t { volatile size_t value; } XAtomic_size_t;
typedef struct { volatile void* value; } XAtomic_uintptr_t;
```

---

### 宏定义

#### XAtomic_create

```c
#define XAtomic_create(type)    XCalloc_System(1, sizeof(XAtomic_##type))
```

创建原子变量。

---

#### XAtomic_init

```c
#define XAtomic_init(var, v) do { (var).value = (v); } while(0)
```

初始化原子变量为指定值。

---

#### XAtomic_delete

```c
#define XAtomic_delete  XFree_System
```

释放原子变量。

---

### 原子加载操作

#### XAtomic_load_bool

```c
bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order)
```

从原子布尔变量中读取值。

---

#### XAtomic_load_int32

```c
int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order)
```

从原子32位有符号整数变量中读取值。

---

#### XAtomic_load_int64

```c
int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order)
```

从原子64位有符号整数变量中读取值。

---

#### XAtomic_load_uint32 / XAtomic_load_uint64 / XAtomic_load_size_t / XAtomic_load_uintptr_t

类似地，提供无符号整数和指针类型的原子加载。

---

### 原子存储操作

#### XAtomic_store_bool

```c
void XAtomic_store_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
```

将值写入原子布尔变量。

---

#### XAtomic_store_int32 / XAtomic_store_int64 / XAtomic_store_uint32 / XAtomic_store_uint64

类似地，提供各种类型的原子存储。

---

### 原子交换操作

#### XAtomic_exchange_bool

```c
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
```

用新值替换原子变量的值，并返回旧值。

---

#### XAtomic_exchange_int32 / XAtomic_exchange_int64 / XAtomic_exchange_uintptr_t

类似地，提供各种类型的原子交换。

---

### 原子比较交换操作 (CAS)

#### XAtomic_compare_exchange_strong_bool

```c
bool XAtomic_compare_exchange_strong_bool(
    XAtomic_bool* var, 
    bool* expected, 
    bool desired, 
    XAtomic_MemoryOrder success_order, 
    XAtomic_MemoryOrder failure_order)
```

比较原子变量的当前值与`*expected`：
- 如果相等，则将`desired`存储到原子变量中，返回true
- 如果不相等，则将原子变量的当前值写入`*expected`，返回false

---

#### XAtomic_compare_exchange_strong_int32 / XAtomic_compare_exchange_strong_int64 / XAtomic_compare_exchange_strong_size_t

类似地，提供各种类型的原子比较交换。

---

### 原子加法操作

#### XAtomic_fetch_add_int32

```c
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
```

原子地将值加到原子变量上，并返回旧值。

---

#### XAtomic_fetch_add_int64 / XAtomic_fetch_add_uint32 / XAtomic_fetch_add_uint64 / XAtomic_fetch_add_size_t

类似地，提供各种类型的原子加法。

---

### 原子减法操作

#### XAtomic_fetch_sub_int32

```c
int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
```

原子地从原子变量中减去值，并返回旧值。

---

#### XAtomic_fetch_sub_int64 / XAtomic_fetch_sub_uint32 / XAtomic_fetch_sub_uint64 / XAtomic_fetch_sub_size_t

类似地，提供各种类型的原子减法。

---

### 内存屏障

#### XAtomic_memory_barrier

```c
void XAtomic_memory_barrier()
```

完全内存屏障，确保所有读写操作按顺序执行。

---

#### XAtomic_memory_barrier_acquire

```c
void XAtomic_memory_barrier_acquire()
```

获取屏障，确保后续读操作不会重排到屏障前。

---

#### XAtomic_memory_barrier_release

```c
void XAtomic_memory_barrier_release()
```

释放屏障，确保之前的写操作不会重排到屏障后。

---

### 索引版本号打包

#### XAtomic_index_bits

```c
size_t XAtomic_index_bits(size_t max_value)
```

计算能容纳[0, max_value]所需的最少位数。

---

#### XAtomic_pack_index_version

```c
size_t XAtomic_pack_index_version(size_t index, size_t version, size_t index_bits, uintptr_t version_mask)
```

打包索引和版本号。

---

#### XAtomic_unpack_index

```c
size_t XAtomic_unpack_index(size_t packed, size_t index_mask)
```

从打包值中解包出索引。

---

#### XAtomic_unpack_version

```c
size_t XAtomic_unpack_version(size_t packed, size_t index_bits, size_t version_mask)
```

从打包值中解包出版本号。

---

### 示例

```c
// 创建原子变量
XAtomic_int32_t* counter = XAtomic_create(int32_t);
XAtomic_init(*counter, 0);

// 原子加法
int32_t old = XAtomic_fetch_add_int32(counter, 1, XAtomic_MemoryOrder_SeqCst);

// 比较交换
int32_t expected = 1;
bool success = XAtomic_compare_exchange_strong_int32(
    counter, &expected, 10, 
    XAtomic_MemoryOrder_AcqRel, 
    XAtomic_MemoryOrder_Acquire);

// 原子加载
int32_t value = XAtomic_load_int32(counter, XAtomic_MemoryOrder_Acquire);

// 清理
XAtomic_delete(counter);
```

---

## XCoreApplication 核心应用

> **平台实现**: 需要针对不同平台实现底层功能。
> - **事件循环**: Windows使用消息队列，Linux使用epoll/eventfd，macOS使用kqueue
> - **进程信息**: Windows使用GetModuleFileName/GetCurrentProcessId，Linux使用readlink/proc/self/getpid
> - **库路径**: Windows使用注册表/环境变量，Linux使用ldconfig/环境变量
> - **命令行参数**: 平台相关的argc/argv处理

XCoreApplication是应用程序的核心类，管理应用程序生命周期、事件循环和命令行解析。

### 头文件

```c
#include "XCoreApplication.h"
```

### 枚举

#### XCoreApplicationAttribute

应用程序属性枚举，控制应用程序的各种行为。

```c
typedef enum {
    XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS = 2,
    XCORE_APPLICATION_ATTRIBUTE_NATIVE_WINDOWS = 3,
    XCORE_APPLICATION_ATTRIBUTE_DONT_USE_NATIVE_MENU_BAR = 6,
    XCORE_APPLICATION_ATTRIBUTE_DISABLE_NATIVE_VIRTUAL_KEYBOARD = 9,
    // ... 更多属性
} XCoreApplicationAttribute;
```

---

### 结构体定义

```c
typedef struct XCoreApplication {
    XObject m_class;               // 父对象
    int m_argc;                    // 命令行参数数量
    char** m_argv;                 // 命令行参数数组
    XString* m_applicationName;    // 应用程序名称
    XString* m_version;            // 应用程序版本号
    XString* m_orgName;            // 组织名称
    XString* m_orgDomain;          // 组织域名
    XBitArray m_attribute;         // 属性位数组
    XStringList* m_paths;          // 库搜索路径列表
} XCoreApplication;
```

---

### 创建与初始化

#### XCoreApplication_create

```c
XCoreApplication* XCoreApplication_create(int argc, char** argv)
```

创建应用程序实例。

**参数:**
- `argc` - 命令行参数数量
- `argv` - 命令行参数数组

---

#### XCoreApplication_init

```c
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv)
```

初始化应用程序实例。

---

#### XCoreApplication_instance

```c
XCoreApplication* XCoreApplication_instance()
```

获取全局应用程序实例。

---

#### xApp宏

```c
#define xApp XCoreApplication_instance()
```

快捷访问全局应用程序实例。

---

### 应用程序元数据

#### XCoreApplication_setApplicationName

```c
void XCoreApplication_setApplicationName(const XString* applicationName)
```

设置应用程序名称。

---

#### XCoreApplication_applicationName

```c
const XString* XCoreApplication_applicationName(void)
```

获取应用程序名称。

---

#### XCoreApplication_setApplicationVersion

```c
void XCoreApplication_setApplicationVersion(const XString* version)
```

设置应用程序版本号。

---

#### XCoreApplication_applicationVersion

```c
const XString* XCoreApplication_applicationVersion(void)
```

获取应用程序版本号。

---

#### XCoreApplication_setOrganizationName

```c
void XCoreApplication_setOrganizationName(const XString* orgName)
```

设置组织名称。

---

#### XCoreApplication_organizationName

```c
const XString* XCoreApplication_organizationName(void)
```

获取组织名称。

---

#### XCoreApplication_setOrganizationDomain

```c
void XCoreApplication_setOrganizationDomain(const XString* orgDomain)
```

设置组织域名。

---

#### XCoreApplication_organizationDomain

```c
const XString* XCoreApplication_organizationDomain(void)
```

获取组织域名。

---

### 应用程序属性

#### XCoreApplication_setAttribute

```c
void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on)
```

设置应用程序属性。

---

#### XCoreApplication_testAttribute

```c
bool XCoreApplication_testAttribute(XCoreApplicationAttribute attribute)
```

测试应用程序属性是否已启用。

---

### 事件处理

#### XCoreApplication_exec

```c
int XCoreApplication_exec()
```

启动应用程序事件循环。

**返回值:** 退出码

---

#### XCoreApplication_quit

```c
void XCoreApplication_quit()
```

退出应用程序。

---

#### XCoreApplication_exit

```c
void XCoreApplication_exit(int returnCode)
```

以指定返回码退出应用程序。

---

#### XCoreApplication_processEvents

```c
void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags)
```

处理待处理事件。

---

#### XCoreApplication_processEventsWithMaxTime

```c
void XCoreApplication_processEventsWithMaxTime(XEventLoopProcessEventsFlags flags, int maxtime)
```

处理事件，最多运行指定毫秒数。

---

#### XCoreApplication_sendEvent

```c
bool XCoreApplication_sendEvent(XObject* receiver, XEvent* event)
```

向指定接收者发送事件（立即处理）。

---

#### XCoreApplication_postEvent

```c
void XCoreApplication_postEvent(XObject* receiver, XEvent* event, int priority)
```

向指定接收者投递事件（稍后处理）。

---

#### XCoreApplication_sendPostedEvents

```c
void XCoreApplication_sendPostedEvents(XObject* receiver, XEventType eventType)
```

立即发送所有已投递的事件。

---

#### XCoreApplication_removePostedEvents

```c
void XCoreApplication_removePostedEvents(XObject* receiver, XEventType eventType)
```

移除指定接收者的已投递事件。

---

### 路径与进程信息

#### XCoreApplication_arguments

```c
XStringList* XCoreApplication_arguments(void)
```

获取命令行参数列表。

---

#### XCoreApplication_applicationDirPath

```c
const XString* XCoreApplication_applicationDirPath(void)
```

获取应用程序可执行文件所在目录。

---

#### XCoreApplication_applicationFilePath

```c
const XString* XCoreApplication_applicationFilePath(void)
```

获取应用程序可执行文件的完整路径。

---

#### XCoreApplication_applicationPid

```c
int64_t XCoreApplication_applicationPid(void)
```

获取当前进程的PID。

---

### 库路径管理

#### XCoreApplication_setLibraryPaths

```c
void XCoreApplication_setLibraryPaths(const XStringList* paths)
```

设置库搜索路径列表。

---

#### XCoreApplication_libraryPaths

```c
const XStringList* XCoreApplication_libraryPaths(void)
```

获取库搜索路径列表。

---

#### XCoreApplication_addLibraryPath

```c
void XCoreApplication_addLibraryPath(const XString* path)
```

添加库搜索路径。

---

#### XCoreApplication_removeLibraryPath

```c
void XCoreApplication_removeLibraryPath(const XString* path)
```

移除库搜索路径。

---

### 事件分发器

#### XCoreApplication_eventDispatcher

```c
XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void)
```

获取事件分发器。

---

#### XCoreApplication_setEventDispatcher

```c
void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher)
```

设置事件分发器。

---

### 信号

#### XCoreApplication_aboutToQuit_signal

```c
void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app)
```

应用程序即将退出信号。

---

---

## XMenu 菜单

XMenu是菜单结构体，支持层级菜单管理。

### 头文件

```c
#include "XMenu.h"
```

### 结构体定义

```c
typedef struct XMenu {
    XHTreeNode m_class;    // 继承树节点，支持层级结构
    void* m_userData;      // 用户数据
} XMenu;
```

### 函数

#### XMenu_create

```c
XMenu* XMenu_create(const char* title)
```

创建菜单。

**参数:**
- `title` - 菜单标题

---

#### XMenu_setTitle

```c
void XMenu_setTitle(XMenu* menu, const char* title)
```

设置菜单标题。

---

#### XMenu_getTitle

```c
const char* XMenu_getTitle(XMenu* menu)
```

获取菜单标题。

---

#### XMenu_addAction

```c
XAction* XMenu_addAction(XMenu* menu, const char* text)
```

添加动作到菜单。

---

#### XMenu_removeAction

```c
bool XMenu_removeAction(XMenu* menu, XAction* action)
```

从菜单移除动作。

---

#### XMenu_getActions

```c
const XVector* XMenu_getActions(XMenu* menu)
```

获取菜单中的所有动作。

---

#### XMenu_addMenu

```c
bool XMenu_addMenu(XMenu* menu, XMenu* newMenu)
```

添加子菜单。

---

#### XMenu_getMenus

```c
XVector* XMenu_getMenus(XMenu* menu)
```

获取所有子菜单。

---

#### XMenu_delete

```c
void XMenu_delete(XMenu* menu)
```

销毁菜单。

---

---

## XPrintf 打印

XPrintf提供跨平台的格式化打印功能。

### 头文件

```c
#include "XPrintf.h"
```

### 函数

#### XPrintf_string

```c
int XPrintf_string(const XString* str)
```

打印XString字符串。

**返回值:** 打印的字符数

---

#### XPrintf_utf8

```c
int XPrintf_utf8(const char* utf8_str)
```

打印UTF-8编码字符串。

---

#### XPrintf

```c
int XPrintf(const char* format, ...)
```

格式化打印。

---

#### XPrintf_char

```c
int XPrintf_char(XChar* ch)
```

输出单个XChar字符。

---

---

## XSignalSlot 信号与槽

XSignalSlot实现了信号与槽机制，是一种观察者模式的事件通信方式。

### 头文件

```c
#include "XSignalSlot.h"
```

### 枚举

#### XEventSendMode

```c
typedef enum {
    XEVENT_SEND_INVALID,   // 无效模式
    XEVENT_SEND_DIRECT,    // 直接发送：同步调用
    XEVENT_SEND_QUEUED     // 队列发送：异步调用
} XEventSendMode;
```

#### XConnectionType

```c
typedef enum XConnectionType {
    XConnectionType_Auto = 0,
    XConnectionType_Direct = 1,
    XConnectionType_Queued = 2,
    XConnectionType_BlockingQueued = 3,
    XConnectionType_Unique = 0x80,
    XConnectionType_SingleShot = 0x100
} XConnectionType;
```

---

### 结构体定义

```c
typedef void (*XSlotFunc1)(XObject* receiver, XVarList* args);
typedef void (*XSlotFunc2)(XObject* sender, XVarList* args);

typedef struct XConnection {
    XConnectionType type;
    XSignal* signal;
    XObject* receiver;
    union {
        XSlotFunc1 slot_func1;
        XSlotFunc2 slot_func2;
    };
} XConnection;

typedef struct XSignal {
    XObject* sender;
    size_t type;
    XVector* connList;
} XSignal;

typedef struct XSignalSlot {
    XObject* obj;
    XMap* signalMap;
    XVector* bindSignalList;
    XMutex* mutex;
} XSignalSlot;
```

---

### 函数

#### XSignalSlot_create

```c
XSignalSlot* XSignalSlot_create(XObject* obj)
```

创建信号槽管理器。

---

#### XSignalSlot_delete

```c
void XSignalSlot_delete(XSignalSlot* manager)
```

销毁信号槽管理器。

---

#### XSignalSlot_connect1

```c
XConnection* XSignalSlot_connect1(
    XSignalSlot* manager,
    size_t signal,
    XObject* receiver,
    XSlotFunc1 slot_func,
    XConnectionType type)
```

连接信号与槽（带接收者）。

---

#### XSignalSlot_connect2

```c
XConnection* XSignalSlot_connect2(
    XSignalSlot* manager,
    size_t signal,
    XSlotFunc2 slot_func)
```

连接信号与槽（无接收者）。

---

#### XSignalSlot_disconnect1

```c
bool XSignalSlot_disconnect1(
    XSignalSlot* manager,
    size_t signal,
    XObject* receiver,
    XSlotFunc1 slot_func1)
```

断开信号与槽的连接。

---

#### XSignalSlot_disconnect2

```c
bool XSignalSlot_disconnect2(XConnection* conn)
```

断开指定连接。

---

#### XSignalSlot_emit

```c
void XSignalSlot_emit(
    XSignalSlot* manager,
    size_t signal,
    XVarList* args,
    void(*del)(XVarList*),
    XAtomic_int32_t* ref_count,
    int priority)
```

触发信号。

---

## XSocketNotifier 套接字通知

> **平台实现**: 需要针对不同平台实现I/O事件监控。
> - Windows: 使用WSAEventSelect/WSAWaitForMultipleEvents或IOCP
> - Linux: 使用epoll_ctl/epoll_wait
> - macOS/BSD: 使用kqueue/kevent
> - 通用: 使用select/poll（性能较低）

XSocketNotifier用于监控套接字的I/O事件。

### 头文件

```c
#include "XSocketNotifier.h"
#include "XSocketDescriptor.h"
```

### 枚举

#### XSocketNotifierType

```c
typedef enum {
    XSocketNotifier_Read = 1,           // 读事件
    XSocketNotifier_Write = 2,          // 写事件
    XSocketNotifier_ReadWrite = 3,      // 读写事件
    XSocketNotifier_Exception = 4       // 异常事件
} XSocketNotifierType;
```

---

### 结构体定义

```c
typedef struct XSocketDescriptor {
    intptr_t value;
} XSocketDescriptor;

typedef struct XSocketNotifier {
    XObject base;                  // 继承XObject
    XSocketDescriptor socket;      // 当前绑定的socket
    XSocketNotifierType type;      // 监听类型
    bool enabled;                  // 是否启用
} XSocketNotifier;
```

---

### XSocketDescriptor 函数

#### XSocketDescriptor_Invalid

```c
XSocketDescriptor XSocketDescriptor_Invalid(void)
```

返回无效描述符。

---

#### XSocketDescriptor_isValid

```c
bool XSocketDescriptor_isValid(XSocketDescriptor sd)
```

判断是否有效。

---

#### XSocketDescriptor_fromIntptr

```c
XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value)
```

从整数创建描述符。

---

#### XSocketDescriptor_toIntptr

```c
intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd)
```

转换为整数。

---

### XSocketNotifier 函数

#### XSocketNotifier_createWithType

```c
XSocketNotifier* XSocketNotifier_createWithType(XSocketNotifierType type)
```

创建套接字通知器（仅指定类型）。

---

#### XSocketNotifier_createWithSocket

```c
XSocketNotifier* XSocketNotifier_createWithSocket(
    XSocketDescriptor socket, 
    XSocketNotifierType type)
```

创建套接字通知器（指定socket和类型）。

---

#### XSocketNotifier_setSocket

```c
void XSocketNotifier_setSocket(XSocketNotifier* notifier, XSocketDescriptor socket)
```

设置要监控的socket。

---

#### XSocketNotifier_socket

```c
XSocketDescriptor XSocketNotifier_socket(const XSocketNotifier* notifier)
```

获取当前socket。

---

#### XSocketNotifier_type

```c
XSocketNotifierType XSocketNotifier_type(const XSocketNotifier* notifier)
```

获取监听类型。

---

#### XSocketNotifier_isValid

```c
bool XSocketNotifier_isValid(const XSocketNotifier* notifier)
```

是否绑定了有效socket。

---

#### XSocketNotifier_isEnabled

```c
bool XSocketNotifier_isEnabled(const XSocketNotifier* notifier)
```

是否已启用。

---

#### XSocketNotifier_setEnabled

```c
void XSocketNotifier_setEnabled(XSocketNotifier* notifier, bool enabled)
```

启用/禁用监听。

---

#### XSocketNotifier_activated_signal

```c
void* XSocketNotifier_activated_signal(
    XSocketNotifier* notifier, 
    XSocketDescriptor socket, 
    XSocketNotifierType type)
```

activated信号。

---

---

## XSync 同步模块

> **平台实现**: 需要针对不同平台实现同步原语。
> 
> **XMutex/XWaitCondition**:
> - Windows: 使用CRITICAL_SECTION/Condition Variable (SRWLock)
> - Linux/macOS: 使用pthread_mutex_t/pthread_cond_t
> 
> **XReadWriteLock**:
> - Windows: 使用SRWLock或自定义实现
> - Linux/macOS: 使用pthread_rwlock_t
> 
> **XSemaphore**:
> - Windows: 使用CreateSemaphore/WaitForSingleObject
> - Linux/macOS: 使用sem_init/sem_wait/sem_post（POSIX信号量）或dispatch_semaphore（macOS GCD）
> 
> **XThread**:
> - Windows: 使用CreateThread/WaitForSingleObject
> - Linux/macOS: 使用pthread_create/pthread_join
> 
> **XThreadPool**:
> - 基于XThread实现，跨平台通用

XSync模块提供了多线程同步原语，包括互斥锁、读写锁、信号量、条件变量、线程和线程池。

---

### XMutex

XMutex是互斥锁，用于保护临界区。

#### 头文件

```c
#include "XMutex.h"
```

#### 函数

##### XMutex_create

```c
XMutex* XMutex_create(XLock_Type type)
```

创建互斥锁。

**参数:**
- `type` - 锁类型（递归/非递归）

---

##### XMutex_init / XMutex_deinit

```c
void XMutex_init(XMutex* mutex, XLock_Type type)
void XMutex_deinit(XMutex* mutex)
```

初始化/销毁互斥锁（栈对象）。

---

##### XMutex_lock

```c
void XMutex_lock(XMutex* mutex)
```

上锁（阻塞等待）。

---

##### XMutex_tryLock

```c
bool XMutex_tryLock(XMutex* mutex)
```

尝试上锁（非阻塞）。

---

##### XMutex_tryLockTimeout

```c
bool XMutex_tryLockTimeout(XMutex* mutex, uint32_t timeout)
```

超时等待上锁。

---

##### XMutex_unlock

```c
void XMutex_unlock(XMutex* mutex)
```

解锁。

---

##### XMutex_isRecursive

```c
bool XMutex_isRecursive(XMutex* mutex)
```

判断是否为递归锁。

---

---

### XReadWriteLock

XReadWriteLock是读写锁，允许多个读操作并发，写操作独占。

#### 头文件

```c
#include "XReadWriteLock.h"
```

#### 函数

##### XReadWriteLock_create

```c
XReadWriteLock* XReadWriteLock_create(XLock_Type type)
```

创建读写锁。

---

##### XReadWriteLock_lockForRead

```c
void XReadWriteLock_lockForRead(XReadWriteLock* rwlock)
```

获取读锁（阻塞）。

---

##### XReadWriteLock_lockForWrite

```c
void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock)
```

获取写锁（阻塞）。

---

##### XReadWriteLock_tryLockForRead

```c
bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock)
```

尝试获取读锁（非阻塞）。

---

##### XReadWriteLock_tryLockForWrite

```c
bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock)
```

尝试获取写锁（非阻塞）。

---

##### XReadWriteLock_unlock

```c
void XReadWriteLock_unlock(XReadWriteLock* rwlock)
```

释放锁。

---

---

### XSemaphore

XSemaphore是信号量，用于控制并发资源访问。

#### 头文件

```c
#include "XSemaphore.h"
```

#### 函数

##### XSemaphore_create

```c
XSemaphore* XSemaphore_create(int32_t initial, int32_t maximum)
```

创建信号量。

---

##### XSemaphore_acquire

```c
void XSemaphore_acquire(XSemaphore* sem, int32_t n)
```

获取n个资源（阻塞）。

---

##### XSemaphore_release

```c
void XSemaphore_release(XSemaphore* sem, int32_t n)
```

释放n个资源。

---

##### XSemaphore_tryAcquire

```c
bool XSemaphore_tryAcquire(XSemaphore* sem, int32_t n)
```

尝试获取n个资源（非阻塞）。

---

##### XSemaphore_tryAcquireTimeout

```c
bool XSemaphore_tryAcquireTimeout(XSemaphore* sem, int32_t n, uint32_t timeout)
```

超时等待获取资源。

---

##### XSemaphore_available

```c
int32_t XSemaphore_available(XSemaphore* sem)
```

获取当前可用资源数量。

---

---

### XWaitCondition

XWaitCondition是条件变量，用于线程间的等待/通知机制。

#### 头文件

```c
#include "XWaitCondition.h"
```

#### 函数

##### XWaitCondition_create

```c
XWaitCondition* XWaitCondition_create()
```

创建条件变量。

---

##### XWaitCondition_wait

```c
bool XWaitCondition_wait(XWaitCondition* cond, XMutex* mutex, int32_t timeout)
```

等待条件满足（必须已持有mutex锁）。

---

##### XWaitCondition_wakeOne

```c
void XWaitCondition_wakeOne(XWaitCondition* cond)
```

唤醒一个等待的线程。

---

##### XWaitCondition_wakeAll

```c
void XWaitCondition_wakeAll(XWaitCondition* cond)
```

唤醒所有等待的线程。

---

---

### XThread

XThread是线程类，封装了线程的创建、管理和控制。

#### 头文件

```c
#include "XThread.h"
```

#### 枚举

##### XThread_Priority

```c
typedef enum {
    XThread_err = -1,
    XThread_IdlePriority,         // 空闲优先级
    XThread_LowestPriority,       // 最低优先级
    XThread_LowPriority,          // 低优先级
    XThread_NormalPriority,       // 正常优先级
    XThread_HighPriority,         // 高优先级
    XThread_HighestPriority,      // 最高优先级
    XThread_TimeCriticalPriority, // 时间关键优先级
    XThread_InheritPriority       // 继承优先级
} XThread_Priority;
```

---

#### 结构体定义

```c
typedef struct XThread {
    XObject m_base;                   // 基类
    uint8_t m_finished;               // 线程是否结束
    uint8_t m_interruptionRequested;  // 是否请求中断
    uint8_t m_running;                // 线程是否正在运行
    uint8_t m_isMainThread;           // 是否为主线程
    XThread_Priority m_priority;      // 线程优先级
    uint32_t m_stackSize;             // 线程栈大小
    XHandle m_handle;                 // 线程句柄
    XThreadData* m_data;              // 线程私有数据
    XThreadFunc m_start_routine;      // 线程函数
    XVarList* m_varList;              // 参数列表
    XEventLoop* m_loop;               // 事件循环
} XThread;
```

---

#### 函数

##### XThread_create

```c
XThread* XThread_create(XObject* parent)
```

创建线程对象。

---

##### XThread_create_func

```c
XThread* XThread_create_func(XThreadFunc start_routine, XVarList* varlist)
```

创建线程对象并指定线程函数。

---

##### XThread_start

```c
bool XThread_start(XThread* thread)
```

启动线程。

---

##### XThread_wait

```c
bool XThread_wait(XThread* thread, uint32_t time)
```

等待线程结束。

---

##### XThread_isRunning

```c
bool XThread_isRunning(const XThread* thread)
```

判断线程是否正在运行。

---

##### XThread_isFinished

```c
bool XThread_isFinished(const XThread* thread)
```

判断线程是否已结束。

---

##### XThread_requestInterruption

```c
void XThread_requestInterruption(XThread* thread)
```

请求中断线程。

---

##### XThread_isInterruptionRequested

```c
bool XThread_isInterruptionRequested(const XThread* thread)
```

判断是否请求中断。

---

##### XThread_terminate

```c
bool XThread_terminate(XThread* thread)
```

强制终止线程。

---

##### XThread_quit

```c
void XThread_quit(XThread* thread)
```

退出线程。

---

##### XThread_setPriority

```c
void XThread_setPriority(XThread* thread, XThread_Priority priority)
```

设置线程优先级。

---

##### XThread_setStackSize

```c
void XThread_setStackSize(XThread* thread, uint32_t stackSize)
```

设置线程栈大小。

---

#### 静态函数

##### XThread_currentThread

```c
XThread* XThread_currentThread()
```

获取当前线程对象。

---

##### XThread_isMainThread

```c
bool XThread_isMainThread()
```

判断当前是否为主线程。

---

##### XThread_idealThreadCount

```c
int XThread_idealThreadCount()
```

返回理想的线程数量。

---

##### XThread_msleep / XThread_sleep / XThread_usleep

```c
void XThread_msleep(uint32_t msecs)
void XThread_sleep(uint32_t secs)
void XThread_usleep(uint32_t usecs)
```

线程休眠。

---

##### XThread_yieldCurrentThread

```c
void XThread_yieldCurrentThread()
```

主动让出CPU时间片。

---

#### 信号

##### XThread_finished_signal

```c
void* XThread_finished_signal(XThread* thread)
```

线程结束信号。

---

##### XThread_started_signal

```c
void* XThread_started_signal(XThread* thread)
```

线程启动信号。

---

---

### XThreadPool

XThreadPool是线程池，用于管理和复用工作线程。

#### 头文件

```c
#include "XThreadPool.h"
```

#### 结构体定义

```c
typedef struct XThreadPool XThreadPool;
```

#### 函数

##### XThreadPool_create

```c
XThreadPool* XThreadPool_create(XObject* parent)
```

创建线程池。

---

##### XThreadPool_globalInstance

```c
XThreadPool* XThreadPool_globalInstance()
```

获取全局线程池实例。

---

##### XThreadPool_start1

```c
void XThreadPool_start1(XThreadPool* pool, XRunnable* runnable, int priority)
```

提交可运行任务到线程池。

---

##### XThreadPool_start2

```c
void XThreadPool_start2(XThreadPool* pool, XCallableToRun function, XVarList* argsList, int priority)
```

提交函数任务到线程池。

---

##### XThreadPool_tryStart1

```c
bool XThreadPool_tryStart1(XThreadPool* pool, XRunnable* runnable)
```

尝试启动任务。

---

##### XThreadPool_maxThreadCount

```c
int XThreadPool_maxThreadCount(const XThreadPool* pool)
```

获取最大线程数。

---

##### XThreadPool_setMaxThreadCount

```c
void XThreadPool_setMaxThreadCount(XThreadPool* pool, int maxThreadCount)
```

设置最大线程数。

---

##### XThreadPool_activeThreadCount

```c
int XThreadPool_activeThreadCount(const XThreadPool* pool)
```

获取活跃线程数。

---

##### XThreadPool_expiryTimeout

```c
int XThreadPool_expiryTimeout(const XThreadPool* pool)
```

获取线程过期超时时间。

---

##### XThreadPool_setExpiryTimeout

```c
void XThreadPool_setExpiryTimeout(XThreadPool* pool, int expiryTimeout)
```

设置线程过期超时时间。

---

##### XThreadPool_setStackSize

```c
void XThreadPool_setStackSize(XThreadPool* pool, uint32_t stackSize)
```

设置工作线程栈大小。

---

##### XThreadPool_reserveThread

```c
void XThreadPool_reserveThread(XThreadPool* pool)
```

预留线程。

---

##### XThreadPool_releaseThread

```c
void XThreadPool_releaseThread(XThreadPool* pool)
```

释放预留的线程。

---

##### XThreadPool_waitForDone

```c
bool XThreadPool_waitForDone(XThreadPool* pool, int msecs)
```

等待所有任务完成。

---

##### XThreadPool_clear

```c
void XThreadPool_clear(XThreadPool* pool)
```

清空待执行任务队列。

---

##### XThreadPool_tryTake

```c
bool XThreadPool_tryTake(XThreadPool* pool, XRunnable* runnable)
```

尝试从队列中移除任务。

---

#### 信号

##### XThreadPool_threadCreated_signal

```c
void* XThreadPool_threadCreated_signal(XThreadPool* pool, XThread* thread)
```

工作线程创建信号。

---

##### XThreadPool_threadDeleted_signal

```c
void* XThreadPool_threadDeleted_signal(XThreadPool* pool, XThread* thread)
```

工作线程删除信号。

---

---

### XRunnable

XRunnable是可运行任务基类。

#### 头文件

```c
#include "XRunnable.h"
```

#### 函数

##### XRunnable_create

```c
XRunnable* XRunnable_create()
```

创建可运行任务。

---

##### XRunnable_run_base

```c
void XRunnable_run_base(XRunnable* runnable)
```

执行任务（虚函数调用）。

---

##### XRunnable_autoDelete

```c
bool XRunnable_autoDelete(const XRunnable* runnable)
```

获取autoDelete属性。

---

##### XRunnable_setAutoDelete

```c
void XRunnable_setAutoDelete(XRunnable* runnable, bool autoDelete)
```

设置autoDelete属性。

---

##### XRunnable_create_from_function

```c
XRunnable* XRunnable_create_from_function(
    XCallableToRun function, 
    XVarList* argsList, 
    bool auto_delete)
```

从函数指针创建可运行任务。

---

---

### 内存序说明

| 内存序 | 说明 |
|--------|------|
| `XAtomic_MemoryOrder_Relaxed` | 宽松内存序，只保证原子性，性能最高 |
| `XAtomic_MemoryOrder_Consume` | 消费内存序，用于数据依赖同步 |
| `XAtomic_MemoryOrder_Acquire` | 获取内存序，用于临界区入口 |
| `XAtomic_MemoryOrder_Release` | 释放内存序，用于临界区出口 |
| `XAtomic_MemoryOrder_AcqRel` | 获取-释放内存序，用于读-修改-写操作 |
| `XAtomic_MemoryOrder_SeqCst` | 顺序一致性内存序，行为最直观 |

---

### 连接类型说明

| 连接类型 | 说明 |
|----------|------|
| `XConnectionType_Auto` | 自动选择（同线程Direct，跨线程Queued） |
| `XConnectionType_Direct` | 直接连接，同步调用 |
| `XConnectionType_Queued` | 队列连接，异步调用 |
| `XConnectionType_BlockingQueued` | 阻塞队列连接 |
| `XConnectionType_Unique` | 唯一连接，重复连接会被忽略 |
| `XConnectionType_SingleShot` | 单次连接，触发后自动断开 |

---

### 线程优先级说明

| 优先级 | 说明 |
|--------|------|
| `XThread_IdlePriority` | 空闲优先级，仅在系统空闲时运行 |
| `XThread_LowestPriority` | 最低优先级 |
| `XThread_LowPriority` | 低优先级 |
| `XThread_NormalPriority` | 正常优先级（默认） |
| `XThread_HighPriority` | 高优先级 |
| `XThread_HighestPriority` | 最高优先级 |
| `XThread_TimeCriticalPriority` | 时间关键优先级 |
| `XThread_InheritPriority` | 继承创建者线程的优先级 |

---

### 常用头文件包含

```c
// 动作
#include "XAction.h"

// 原子操作
#include "XAtomic.h"
#include "XAtomic_load.h"
#include "XAtomic_store.h"
#include "XAtomic_exchange.h"
#include "XAtomic_compare.h"
#include "XAtomic_add.h"
#include "XAtomic_sub.h"

// 核心应用
#include "XCoreApplication.h"

// 菜单
#include "XMenu.h"

// 打印
#include "XPrintf.h"

// 信号与槽
#include "XSignalSlot.h"

// 套接字通知
#include "XSocketNotifier.h"
#include "XSocketDescriptor.h"

// 同步原语
#include "XMutex.h"
#include "XReadWriteLock.h"
#include "XSemaphore.h"
#include "XWaitCondition.h"
#include "XThread.h"
#include "XThreadPool.h"
#include "XRunnable.h"
```

---

### 最佳实践

1. **原子操作**
   - 默认使用`XAtomic_MemoryOrder_SeqCst`，行为最直观
   - 性能敏感场景可使用更宽松的内存序
   - 注意CAS操作的ABA问题

2. **信号与槽**
   - 跨线程通信使用`XConnectionType_Queued`
   - 注意在槽函数中检查发送者是否有效
   - 使用`XConnectionType_SingleShot`实现一次性连接

3. **线程同步**
   - 优先使用RAII风格的锁管理
   - 避免死锁：按固定顺序获取多个锁
   - 读写锁适合读多写少的场景

4. **线程池**
   - 使用全局线程池处理短期任务
   - 设置合理的`expiryTimeout`避免频繁创建销毁线程
   - 长时间运行的任务不应使用线程池

5. **套接字通知**
   - 在slot中处理完事件后注意检查socket状态
   - 使用`setEnabled(false)`临时禁用通知

