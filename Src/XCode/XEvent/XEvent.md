# XinYueC 事件系统文档

## 目录

- [概述](#概述)
- [XEvent 事件基类](#xevent-事件基类)
- [XEventType 事件类型](#xeventtype-事件类型)
- [内置事件类型](#内置事件类型)
- [事件派生类](#事件派生类)
- [事件优先级](#事件优先级)
- [自定义事件](#自定义事件)
- [XEventLoop 事件循环](#xeventloop-事件循环)
- [XAbstractEventDispatcher 事件调度器](#xabstracteventdispatcher-事件调度器)
- [附录](#附录)

---

## 概述

XinYueC事件系统提供了完善的事件处理机制，支持多种内置事件类型和自定义事件。

### 核心特性

- **丰富的事件类型**：支持鼠标、键盘、定时器、窗口等多种事件
- **事件优先级**：支持不同优先级的事件处理
- **事件过滤**：支持事件过滤器机制
- **自定义事件**：支持用户自定义事件类型

---

## XEvent 事件基类

XEvent是所有事件的基类，定义了事件的公共属性和方法。

### 头文件

```c
#include "XEvent.h"
```

### 结构体定义

```c
typedef struct XEvent {
    XClass m_class;           // 继承自XClass
    int type;                 // 事件类型代码
    
    // 标志位（16 bits）
    uint16_t reserved : 10;   // 预留扩展
    uint16_t accepted : 1;    // 是否接受事件
    uint16_t spontaneous : 1; // 是否为自发事件
    uint16_t posted : 1;      // 是否来自事件队列
    uint16_t input_event : 1; // 是否是输入事件
    uint16_t pointer_event : 1;    // 是否是指针设备事件
    uint16_t single_point_event : 1; // 是否是单点事件
} XEvent;
```

### 虚函数枚举

```c
enum XEventVtableEnum {
    EXEvent_SetAccepted,  // 设置接受状态
    EXEvent_Clone,        // 克隆事件
    EXEvent_END_SIZE
};
```

### 创建与销毁

#### XEvent_create

```c
XEvent* XEvent_create(XEventType code)
```

创建基础事件。

**参数:**
- `code` - 事件类型

**返回值:** 成功返回XEvent指针，失败返回NULL

---

#### XEvent_init

```c
void XEvent_init(XEvent* event, XEventType type)
```

初始化事件。

**参数:**
- `event` - 待初始化的XEvent指针
- `type` - 事件类型

**返回值:** 无

---

#### XEvent_delete_base

```c
void XEvent_delete_base(XEvent* event)
```

销毁事件并释放内存。

---

### 事件状态

#### XEvent_accept

```c
void XEvent_accept(XEvent* event)
```

接受事件，标记为已处理。

**参数:**
- `event` - XEvent指针

**返回值:** 无

---

#### XEvent_ignore

```c
void XEvent_ignore(XEvent* event)
```

忽略事件，标记为未处理。

**参数:**
- `event` - XEvent指针

**返回值:** 无

---

#### XEvent_isAccepted

```c
bool XEvent_isAccepted(const XEvent* event)
```

判断事件是否已被接受。

**参数:**
- `event` - XEvent指针

**返回值:** 已接受返回true，否则返回false

---

### 事件类型判断

#### XEvent_isInputEvent

```c
bool XEvent_isInputEvent(const XEvent* event)
```

判断是否为输入事件(键盘、鼠标、触摸等)。

---

#### XEvent_isPointerEvent

```c
bool XEvent_isPointerEvent(const XEvent* event)
```

判断是否为指针事件(鼠标、触控笔)。

---

#### XEvent_isSinglePointEvent

```c
bool XEvent_isSinglePointEvent(const XEvent* event)
```

判断是否为单点事件。

---

#### XEvent_spontaneous

```c
bool XEvent_spontaneous(const XEvent* event)
```

判断是否为自发事件(由系统或用户真实触发)。

---

### 其他操作

#### XEvent_type

```c
XEventType XEvent_type(const XEvent* event)
```

获取事件类型。

**参数:**
- `event` - XEvent指针

**返回值:** 返回事件类型枚举值

---

#### XEvent_clone_base

```c
XEvent* XEvent_clone_base(const XEvent* event)
```

克隆事件。

**参数:**
- `event` - XEvent指针

**返回值:** 返回新事件的指针

---

#### XEvent_registerEventType

```c
int XEvent_registerEventType(int hint)
```

注册自定义事件类型。

**参数:**
- `hint` - 建议的事件类型值，-1表示自动分配

**返回值:** 返回分配的事件类型值

---

## XEventType 事件类型

XEventType定义了所有事件类型的枚举。

### 头文件

```c
#include "XEventType.h"
```

---

## 内置事件类型

### 鼠标事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_MOUSE_BUTTON_PRESS` | 2 | 鼠标按键按下 |
| `XEVENT_TYPE_MOUSE_BUTTON_RELEASE` | 3 | 鼠标按键释放 |
| `XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK` | 4 | 鼠标双击 |
| `XEVENT_TYPE_MOUSE_MOVE` | 5 | 鼠标移动 |
| `XEVENT_TYPE_WHEEL` | 31 | 鼠标滚轮 |
| `XEVENT_TYPE_ENTER` | 10 | 鼠标进入控件 |
| `XEVENT_TYPE_LEAVE` | 11 | 鼠标离开控件 |
| `XEVENT_TYPE_HOVER_ENTER` | 127 | 悬停进入 |
| `XEVENT_TYPE_HOVER_LEAVE` | 128 | 悬停离开 |
| `XEVENT_TYPE_HOVER_MOVE` | 129 | 悬停移动 |

### 键盘事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_KEY_PRESS` | 6 | 键盘按下 |
| `XEVENT_TYPE_KEY_RELEASE` | 7 | 键盘释放 |
| `XEVENT_TYPE_SHORTCUT` | 117 | 快捷键触发 |
| `XEVENT_TYPE_SHORTCUT_OVERRIDE` | 51 | 快捷键覆盖 |

### 焦点事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_FOCUS_IN` | 8 | 获得焦点 |
| `XEVENT_TYPE_FOCUS_OUT` | 9 | 失去焦点 |
| `XEVENT_TYPE_FOCUS_ABOUT_TO_CHANGE` | 23 | 焦点即将改变 |

### 窗口事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_CREATE` | 15 | 控件创建 |
| `XEVENT_TYPE_DESTROY` | 16 | 控件销毁 |
| `XEVENT_TYPE_SHOW` | 17 | 显示控件 |
| `XEVENT_TYPE_HIDE` | 18 | 隐藏控件 |
| `XEVENT_TYPE_CLOSE` | 19 | 关闭窗口 |
| `XEVENT_TYPE_MOVE` | 13 | 控件移动 |
| `XEVENT_TYPE_RESIZE` | 14 | 控件调整大小 |
| `XEVENT_TYPE_PAINT` | 12 | 重绘事件 |
| `XEVENT_TYPE_WINDOW_ACTIVATE` | 24 | 窗口激活 |
| `XEVENT_TYPE_WINDOW_DEACTIVATE` | 25 | 窗口停用 |
| `XEVENT_TYPE_WINDOW_STATE_CHANGE` | 105 | 窗口状态变更 |

### 定时器事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_TIMER` | 1 | 定时器事件 |
| `XEVENT_TYPE_ZERO_TIMER_EVENT` | 154 | 零间隔定时器事件 |

### 子对象事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_CHILD_ADDED` | 68 | 子对象添加 |
| `XEVENT_TYPE_CHILD_POLISHED` | 69 | 子对象polish完成 |
| `XEVENT_TYPE_CHILD_REMOVED` | 71 | 子对象移除 |

### 拖放事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_DRAG_ENTER` | 60 | 拖拽进入 |
| `XEVENT_TYPE_DRAG_MOVE` | 61 | 拖拽移动 |
| `XEVENT_TYPE_DRAG_LEAVE` | 62 | 拖拽离开 |
| `XEVENT_TYPE_DROP` | 63 | 拖拽释放 |

### 触摸事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_TOUCH_BEGIN` | 194 | 触摸开始 |
| `XEVENT_TYPE_TOUCH_UPDATE` | 195 | 触摸更新 |
| `XEVENT_TYPE_TOUCH_END` | 196 | 触摸结束 |
| `XEVENT_TYPE_TOUCH_CANCEL` | 209 | 触摸取消 |

### 套接字事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_SOCK_ACT` | 50 | 套接字活动 |
| `XEVENT_TYPE_SOCK_CLOSE` | 211 | 套接字关闭 |

### 其他事件

| 事件类型 | 值 | 说明 |
|---------|-----|------|
| `XEVENT_TYPE_QUIT` | 20 | 应用退出 |
| `XEVENT_TYPE_PARENT_CHANGE` | 21 | 父对象变更 |
| `XEVENT_TYPE_DEFERRED_DELETE` | 52 | 延迟删除 |
| `XEVENT_TYPE_META_CALL` | 43 | 元对象调用 |
| `XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE` | 170 | 动态属性变更 |
| `XEVENT_TYPE_THEME_CHANGE` | 210 | 主题变更 |
| `XEVENT_TYPE_LOCALE_CHANGE` | 88 | 区域设置变更 |
| `XEVENT_TYPE_LANGUAGE_CHANGE` | 89 | 语言变更 |
| `XEVENT_TYPE_FONT_CHANGE` | 97 | 字体变更 |
| `XEVENT_TYPE_CURSOR_CHANGE` | 183 | 光标形状变更 |

---

## 事件派生类

### XEventTimer 定时器事件

```c
typedef struct XEventTimer {
    XEvent m_base;      // 基类
    XTimerId timerId;   // 定时器ID
} XEventTimer;
```

#### XEventTimer_create

```c
XEventTimer* XEventTimer_create(XTimerId id)
```

创建定时器事件。

**参数:**
- `id` - 定时器ID

**返回值:** 返回事件指针

---

#### XEventTimer_timerId

```c
XTimerId XEventTimer_timerId(const XEventTimer* event)
```

获取定时器ID。

---

### XChildEvent 子对象事件

```c
typedef struct XChildEvent {
    XEvent m_base;    // 基类
    XObject* child;   // 子对象
} XChildEvent;
```

#### XChildEvent_create

```c
XChildEvent* XChildEvent_create(XEventType type, XObject* child)
```

创建子对象事件。

**参数:**
- `type` - 事件类型
- `child` - 子对象

**返回值:** 返回事件指针

---

#### XChildEvent_child

```c
XObject* XChildEvent_child(const XChildEvent* event)
```

获取涉及的子对象。

---

#### XChildEvent_added

```c
bool XChildEvent_added(const XChildEvent* event)
```

判断是否为"已添加"类型。

---

#### XChildEvent_removed

```c
bool XChildEvent_removed(const XChildEvent* event)
```

判断是否为"已移除"类型。

---

#### XChildEvent_polished

```c
bool XChildEvent_polished(const XChildEvent* event)
```

判断是否为"已polish"类型。

---

### XEventDeferredDelete 延迟删除事件

```c
typedef struct XEventDeferredDelete {
    XEvent m_base;   // 基类
    bool isDelete;   // 是否删除
} XEventDeferredDelete;
```

#### XEventDeferredDelete_create

```c
XEventDeferredDelete* XEventDeferredDelete_create(bool isDelete)
```

创建延迟删除事件。

---

### XEventSockAct 套接字活动事件

```c
typedef struct XEventSockAct {
    XEvent m_base;           // 基类
    XSocketDescriptor socket; // 套接字描述符
    XSocketActType actType;   // 活动类型
} XEventSockAct;
```

#### XEventSockAct_create

```c
XEventSockAct* XEventSockAct_create(XSocketDescriptor socket, XSocketActType actType)
```

创建套接字活动事件。

**参数:**
- `socket` - 套接字描述符
- `actType` - 活动类型

---

### XEventFunc 函数运行事件

```c
typedef struct XEventFunc {
    XEvent event;           // 基类
    XCallableToRun func;    // 需要执行的函数
    XVarList* argList;      // 函数参数
} XEventFunc;
```

#### XEventFunc_create

```c
XEventFunc* XEventFunc_create(XCallableToRun func, XVarList* argList, void(*del_argList)(XVarList*))
```

创建函数事件。

**参数:**
- `func` - 要执行的函数
- `argList` - 函数参数
- `del_argList` - 参数释放函数

---

### XEventMetaCall 槽函数调用事件

```c
typedef struct XEventMetaCall {
    XEvent event;           // 基类
    XSlotFunc1 func;        // 槽函数
    XObject* sender;        // 发送者对象
    XVarList* argList;      // 函数参数
    XSemaphore* sem;        // 信号量
    XAtomic_int32_t* ref_count; // 参数引用计数
} XEventMetaCall;
```

#### XEventMetaCall_create

```c
XEventMetaCall* XEventMetaCall_create(XObject* sender, XSlotFunc1 func,
    XVarList* argList, XAtomic_int32_t* ref_count, XSemaphore* sem)
```

创建槽函数调用事件。

---

### XDynamicPropertyChangeEvent 动态属性变更事件

```c
typedef struct XDynamicPropertyChangeEvent {
    XEvent m_base;          // 基类
    XByteArray* propertyName; // 属性名称
} XDynamicPropertyChangeEvent;
```

#### XDynamicPropertyChangeEvent_create

```c
XDynamicPropertyChangeEvent* XDynamicPropertyChangeEvent_create(const char* name)
```

创建动态属性变更事件。

---

## 事件优先级

事件系统支持不同优先级的事件处理。

### 优先级枚举

```c
typedef enum {
    XEVENT_PRIORITY_LOWEST = 0,   // 最低优先级
    XEVENT_PRIORITY_LOW,          // 低优先级
    XEVENT_PRIORITY_NORMAL,       // 正常优先级
    XEVENT_PRIORITY_HIGH,         // 高优先级
    XEVENT_PRIORITY_HIGHEST,      // 最高优先级
    XEVENT_PRIORITY_COUNT         // 优先级数量
} XEventPriority;
```

### 优先级说明

| 优先级 | 说明 | 使用场景 |
|--------|------|----------|
| `XEVENT_PRIORITY_LOWEST` | 最低优先级 | 不重要的后台任务 |
| `XEVENT_PRIORITY_LOW` | 低优先级 | 可延迟处理的操作 |
| `XEVENT_PRIORITY_NORMAL` | 正常优先级 | 默认优先级 |
| `XEVENT_PRIORITY_HIGH` | 高优先级 | 用户交互响应 |
| `XEVENT_PRIORITY_HIGHEST` | 最高优先级 | 紧急事件处理 |

---

## 自定义事件

### 注册自定义事件类型

```c
// 注册自定义事件类型
int myEventType = XEvent_registerEventType(-1);  // -1表示自动分配

// 使用指定值
int myEventType2 = XEvent_registerEventType(2000);
```

### 定义自定义事件结构体

```c
// 继承XEvent
typedef struct MyEvent {
    XEvent m_base;    // 基类
    int myData;       // 自定义数据
    void* ptr;        // 自定义指针
} MyEvent;

// 虚函数枚举
XCLASS_DEFINE_BEGING(MyEvent)
XCLASS_DEFINE_EXTEND_END(MyEvent, XEvent)

// 创建函数
MyEvent* MyEvent_create(int type, int data, void* ptr) {
    MyEvent* event = XMalloc(sizeof(MyEvent), XMEMORY_TYPE_SYSTEM);
    if (event) {
        XEvent_init(&event->m_base, type);
        XClassSetVtable(event, MyEvent);
        event->myData = data;
        event->ptr = ptr;
    }
    return event;
}

// 类初始化
XVtable* MyEvent_class_init() {
    XVTABLE_CREAT(MyEventVtable);
    XVTABLE_HEAP_INIT(MyEventVtable);
    XVTABLE_INHERIT(MyEventVtable, XEvent_class_init());
    return MyEventVtable;
}
```

### 发送自定义事件

```c
// 创建并发送事件
MyEvent* event = MyEvent_create(myEventType, 100, someData);
XObject_event_base(receiver, (XEvent*)event);
XEvent_delete_base((XEvent*)event);
```

---

## XEventLoop 事件循环

XEventLoop负责处理事件队列和定时器，是事件系统的核心组件。

### 头文件

```c
#include "XEventLoop.h"
```

### 结构体定义

```c
typedef struct XEventLoop {
    XObject m_class;                  // 父类
    XAbstractEventDispatcher* m_eventDispatcher;  // 事件调度器
    XEventLoopState m_state;          // 事件循环状态
    XTimer* m_deley;                  // 延迟定时器
    int m_exitCode;                   // 退出代码
} XEventLoop;
```

### 事件循环状态

```c
typedef enum {
    XEventLoop_Running,    // 运行中
    XEventLoop_Quit,       // 已退出
    XEventLoop_Suspended   // 已暂停
} XEventLoopState;
```

### 事件处理标志

```c
typedef enum {
    XEventLoop_AllEvents = 0x00,              // 处理所有事件
    XEventLoop_ExcludeUserInputEvents = 0x01, // 排除用户输入事件
    XEventLoop_ExcludeSocketNotifiers = 0x02, // 排除套接字通知事件
    XEventLoop_WaitForMoreEvents = 0x04,      // 等待更多事件
    XEventLoop_X11ExcludeTimers = 0x08,       // X11环境排除定时器
    XEventLoop_EventLoopExec = 0x20,          // 事件循环执行态
    XEventLoop_DialogExec = 0x40,             // 对话框执行态
    XEventLoop_ApplicationExec = 0x80         // 应用程序执行态
} XEventLoopProcessEventsFlags;
```

### 创建与初始化

#### XEventLoop_create

```c
XEventLoop* XEventLoop_create()
```

创建事件循环实例。

**返回值:** 成功返回XEventLoop指针，失败返回NULL

---

#### XEventLoop_init

```c
void XEventLoop_init(XEventLoop* loop)
```

初始化事件循环。

**参数:**
- `loop` - 待初始化的XEventLoop指针

**返回值:** 无

---

### 运行控制

#### XEventLoop_exec

```c
int XEventLoop_exec(XEventLoop* loop)
```

启动事件循环(阻塞运行)。

**参数:**
- `loop` - XEventLoop指针

**返回值:** 退出代码

---

#### XEventLoop_exit

```c
void XEventLoop_exit(XEventLoop* loop, int exitCode)
```

退出事件循环。

**参数:**
- `loop` - XEventLoop指针
- `exitCode` - 退出代码

**返回值:** 无

---

#### XEventLoop_quit

```c
void XEventLoop_quit(XEventLoop* loop)
```

退出事件循环(退出代码为0)。

---

#### XEventLoop_wakeUp

```c
void XEventLoop_wakeUp(XEventLoop* loop)
```

唤醒事件循环。

---

### 事件处理

#### XEventLoop_processEvents

```c
void XEventLoop_processEvents(XEventLoop* loop, XEventLoopProcessEventsFlags flags)
```

处理当前待处理的事件。

**参数:**
- `loop` - XEventLoop指针
- `flags` - 事件处理标志

**返回值:** 无

---

#### XEventLoop_delay

```c
void XEventLoop_delay(size_t msec)
```

延迟指定时间。

**参数:**
- `msec` - 延迟时间(毫秒)

---

---

## XAbstractEventDispatcher 事件调度器

XAbstractEventDispatcher是事件调度器的抽象基类，负责协调事件循环、定时器、套接字通知器等核心功能。

### 头文件

```c
#include "XAbstractEventDispatcher.h"
```

### 结构体定义

```c
typedef struct XAbstractEventDispatcher {
    XObject m_class;                     // 父类
    XDispatcherThreadType type;          // 线程类型
    XAbstractEventDispatcherPrivate* d_ptr; // 私有数据
} XAbstractEventDispatcher;
```

### 虚函数枚举

```c
enum XAbstractEventDispatcherVtableEnum {
    EXAbstractEventDispatcher_ProcessEvents,          // 处理事件
    EXAbstractEventDispatcher_RegisterSocketNotifier, // 注册套接字通知器
    EXAbstractEventDispatcher_UnregisterSocketNotifier,
    EXAbstractEventDispatcher_RegisterTimer,          // 注册定时器
    EXAbstractEventDispatcher_UnregisterTimer,        // 注销定时器
    EXAbstractEventDispatcher_UnregisterTimers,       // 注销对象所有定时器
    EXAbstractEventDispatcher_TimersForObject,        // 获取对象定时器
    EXAbstractEventDispatcher_RemainingTime,          // 获取剩余时间
    EXAbstractEventDispatcher_WakeUp,                 // 唤醒
    EXAbstractEventDispatcher_Interrupt,              // 中断
    EXAbstractEventDispatcher_StartingUp,             // 启动回调
    EXAbstractEventDispatcher_ClosingDown             // 关闭回调
};
```

### 创建与初始化

#### XAbstractEventDispatcher_create

```c
XAbstractEventDispatcher* XAbstractEventDispatcher_create(XObject* parent)
```

创建事件调度器实例。

**参数:**
- `parent` - 父对象

**返回值:** 返回调度器指针

---

#### XAbstractEventDispatcher_init

```c
void XAbstractEventDispatcher_init(XAbstractEventDispatcher* dispatcher, XObject* parent)
```

初始化事件调度器。

---

#### XAbstractEventDispatcher_instance

```c
XAbstractEventDispatcher* XAbstractEventDispatcher_instance(XThread* thread)
```

获取指定线程的事件调度器实例。

**参数:**
- `thread` - 线程指针(NULL表示当前线程)

**返回值:** 返回调度器指针

---

### 定时器管理

#### XAbstractEventDispatcher_registerTimer

```c
XTimerId XAbstractEventDispatcher_registerTimer(
    XAbstractEventDispatcher* self,
    XDuration interval,
    XTimerType timerType,
    XObject* object)
```

注册定时器(自动分配ID)。

**参数:**
- `self` - 调度器指针
- `interval` - 间隔(纳秒)
- `timerType` - 定时器类型
- `object` - 所属对象

**返回值:** 返回定时器ID，失败返回`XTIMER_ID_INVALID`

---

#### XAbstractEventDispatcher_registerTimer_base

```c
void XAbstractEventDispatcher_registerTimer_base(
    XAbstractEventDispatcher* self,
    XTimerId timerId,
    XDuration interval,
    XTimerType timerType,
    XObject* object)
```

注册定时器(指定ID)。

---

#### XAbstractEventDispatcher_unregisterTimer_base

```c
bool XAbstractEventDispatcher_unregisterTimer_base(XAbstractEventDispatcher* self, XTimerId timerId)
```

注销指定ID的定时器。

---

#### XAbstractEventDispatcher_unregisterTimers_base

```c
bool XAbstractEventDispatcher_unregisterTimers_base(XAbstractEventDispatcher* self, XObject* object)
```

注销某对象的所有定时器。

---

#### XAbstractEventDispatcher_remainingTime_base

```c
XDuration XAbstractEventDispatcher_remainingTime_base(const XAbstractEventDispatcher* self, XTimerId timerId)
```

获取指定定时器的剩余时间。

**返回值:** 返回剩余时间(纳秒)，无效返回-1

---

### 事件处理

#### XAbstractEventDispatcher_processEvents_base

```c
bool XAbstractEventDispatcher_processEvents_base(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags)
```

处理待处理的事件。

**返回值:** 至少处理了一个事件返回true

---

#### XAbstractEventDispatcher_wakeUp_base

```c
void XAbstractEventDispatcher_wakeUp_base(XAbstractEventDispatcher* self)
```

唤醒事件循环。

---

#### XAbstractEventDispatcher_interrupt_base

```c
void XAbstractEventDispatcher_interrupt_base(XAbstractEventDispatcher* self)
```

中断事件循环。

---

### 套接字通知器管理

#### XAbstractEventDispatcher_registerSocketNotifier_base

```c
void XAbstractEventDispatcher_registerSocketNotifier_base(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
```

注册套接字通知器。

---

#### XAbstractEventDispatcher_unregisterSocketNotifier_base

```c
void XAbstractEventDispatcher_unregisterSocketNotifier_base(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
```

注销套接字通知器。

---

### 原生事件过滤器

#### XAbstractEventDispatcher_installNativeEventFilter

```c
void XAbstractEventDispatcher_installNativeEventFilter(XAbstractEventDispatcher* self, XAbstractNativeEventFilter* filter)
```

安装原生事件过滤器。

---

#### XAbstractEventDispatcher_removeNativeEventFilter

```c
void XAbstractEventDispatcher_removeNativeEventFilter(XAbstractEventDispatcher* self, XAbstractNativeEventFilter* filter)
```

移除原生事件过滤器。

---

### 信号

#### XAbstractEventDispatcher_awake_signal

```c
void* XAbstractEventDispatcher_awake_signal(XAbstractEventDispatcher* self)
```

awake信号，事件循环被唤醒时发射。

---

#### XAbstractEventDispatcher_aboutToBlock_signal

```c
void* XAbstractEventDispatcher_aboutToBlock_signal(XAbstractEventDispatcher* self)
```

aboutToBlock信号，事件循环即将阻塞时发射。

---

## 附录

### 事件处理流程

```
1. 事件创建
   └─> XEvent_create() / XxxxEvent_create()

2. 事件投递
   └─> 通过事件循环投递
   └─> 或直接调用event函数

3. 事件处理
   └─> XObject_event_base()
   └─> 检查事件过滤器
   └─> 分发到具体处理函数

4. 事件销毁
   └─> XEvent_delete_base()
```

### 事件标志位说明

| 标志位               | 说明                         |
| -------------------- | ---------------------------- |
| `accepted`           | 事件是否被接受(已处理)       |
| `spontaneous`        | 是否为自发事件(用户真实触发) |
| `posted`             | 是否来自事件队列             |
| `input_event`        | 是否为输入事件               |
| `pointer_event`      | 是否为指针设备事件           |
| `single_point_event` | 是否为单点事件               |

### 用户自定义事件范围

| 范围                           | 说明                 |
| ------------------------------ | -------------------- |
| `XEVENT_TYPE_USER` (1000)      | 用户自定义事件起始值 |
| `XEVENT_TYPE_MAX_USER` (65535) | 用户自定义事件最大值 |

### 常用事件类型判断函数

| 函数                          | 判断条件                   |
| ----------------------------- | -------------------------- |
| `XEvent_isInputEvent()`       | 键盘、鼠标、触摸等输入事件 |
| `XEvent_isPointerEvent()`     | 鼠标、触控笔等指针事件     |
| `XEvent_isSinglePointEvent()` | 单点触控事件               |
| `XEvent_spontaneous()`        | 系统/用户真实触发的事件    |

### 事件与信号槽的区别

| 特性     | 事件           | 信号槽     |
| -------- | -------------- | ---------- |
| 通信方式 | 一对一         | 一对多     |
| 处理时机 | 事件循环中     | 同步/异步  |
| 优先级   | 支持           | 不支持     |
| 过滤     | 支持事件过滤器 | 不支持     |
| 适用场景 | 系统级消息     | 对象间通信 |

### 最佳实践

1. **事件创建**
   - 使用对应事件类型的create函数
   - 堆上分配的事件需要手动delete

2. **事件处理**
   - 重写`event_base`处理自定义事件
   - 使用`accept()`/`ignore()`标记处理状态

3. **事件过滤**
   - 安装事件过滤器拦截事件
   - 过滤器返回true表示事件已处理

4. **自定义事件**
   - 使用`XEvent_registerEventType()`注册类型
   - 自定义事件值应在`XEVENT_TYPE_USER`到`XEVENT_TYPE_MAX_USER`范围内

5. **线程安全**
   - 事件对象不是线程安全的
   - 跨线程发送事件使用队列模式

## 