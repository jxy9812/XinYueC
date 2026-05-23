# XinYueC 定时器模块文档

## 目录

- [概述](#概述)
- [XTimer 定时器](#xtimer-定时器)
- [XTimerData 定时器数据](#xtimerdata-定时器数据)
- [XTimerGroupBase 定时器组基类](#xtimergroupbase-定时器组基类)
- [XTimeWheelGroup 时间轮组](#xtimewheelgroup-时间轮组)
- [XHrTimerGroup 高精度定时器组](#xhrtimergroup-高精度定时器组)
- [配置说明](#配置说明)
- [附录](#附录)

---

## 概述

XinYueC定时器模块提供了跨平台的高效定时器实现，支持多种定时器类型和后端实现。

### 核心特性

- **多平台支持**：Windows、Linux、macOS、FreeRTOS
- **多种实现后端**：时间轮、红黑树、系统原生定时器
- **高精度定时**：支持纳秒级精度
- **单次/周期性**：支持单次触发和周期性触发
- **信号槽机制**：支持面向对象的回调方式
- **线程安全**：内置互斥锁保护

### 模块结构

```
XTimer
├── XTimer.h          # 主定时器类
├── XTimerData.h      # 定时器数据结构
├── XTimerConfig.h    # 配置文件
└── XTimerGroup/
    ├── XTimerGroupBase.h   # 定时器组基类
    ├── XTimeWheelGroup/    # 时间轮实现
    └── XHrTimerGroup/      # 高精度红黑树实现
```

---

## XTimer 定时器

XTimer是主要的定时器类，继承自XObject，支持信号槽机制。XTimer默认是依赖事件调度器。

### 头文件

```c
#include "XTimer.h"
```

### 结构体定义

```c
typedef struct XTimer {
    XObject m_class;           // 基类
    uint32_t m_isRun : 1;      // 是否正在运行
    uint32_t m_firstTrigger : 1; // 首次触发标志
    XTimerData m_timerData;    // 定时器数据
    XTimerType m_type;         // 定时器类型
} XTimer;
```

### 创建与初始化

#### XTimer_create

```c
XTimer* XTimer_create()
```

创建XTimer实例。

**返回值:** 成功返回XTimer指针，失败返回NULL

---

#### XTimer_init

```c
void XTimer_init(XTimer* timer)
```

初始化XTimer实例。

**参数:**
- `timer` - XTimer指针

---

#### XTimer_class_init

```c
XVtable* XTimer_class_init()
```

初始化XTimer类的虚函数表。

---

### 启动与停止

#### XTimer_start_base

```c
void XTimer_start_base(XTimer* timer)
```

启动定时器。

**参数:**
- `timer` - XTimer指针

---

#### XTimer_stop_base

```c
void XTimer_stop_base(XTimer* timer)
```

停止定时器。

---

### 属性设置

#### XTimer_setTimeout

```c
void XTimer_setTimeout(XTimer* timer, size_t value)
```

设置定时器超时时间。

**参数:**
- `timer` - XTimer指针
- `value` - 超时时间（毫秒）

---

#### XTimer_setInterval

```c
void XTimer_setInterval(XTimer* timer, size_t value)
```

设置定时器周期间隔。

**参数:**
- `timer` - XTimer指针
- `value` - 周期间隔（毫秒）

---

#### XTimer_setUserData

```c
void XTimer_setUserData(XTimer* timer, void* userData)
```

设置用户自定义数据。

---

#### XTimer_setTimerCallback

```c
void XTimer_setTimerCallback(XTimer* timer, XTimerCallback callback)
```

设置定时器回调函数。

---

#### XTimer_setTimerId

```c
void XTimer_setTimerId(XTimer* timer, size_t timerId)
```

设置定时器ID。

---

#### XTimer_setAutoDelete

```c
void XTimer_setAutoDelete(XTimer* timer, bool del)
```

设置定时器是否自动释放。

**参数:**
- `timer` - XTimer指针
- `del` - true：超时后自动释放，false：不自动释放

---

#### XTimer_setSingleShot

```c
void XTimer_setSingleShot(XTimer* timer, bool ss)
```

设置定时器是否为单次触发模式。

**参数:**
- `timer` - XTimer指针
- `ss` - true：单次触发，false：周期性触发

---

#### XTimer_setTimerType

```c
void XTimer_setTimerType(XTimer* timer, XTimerType type)
```

设置定时器类型。

---

### 属性获取

#### XTimer_timeout

```c
size_t XTimer_timeout(XTimer* timer)
```

获取定时器超时时间。

**返回值:** 超时时间（毫秒）

---

#### XTimer_interval

```c
size_t XTimer_interval(XTimer* timer)
```

获取定时器周期间隔。

---

#### XTimer_timerId

```c
size_t XTimer_timerId(XTimer* timer)
```

获取定时器ID。

---

#### XTimer_userData

```c
void* XTimer_userData(XTimer* timer)
```

获取用户自定义数据。

---

#### XTimer_isSingleShot

```c
bool XTimer_isSingleShot(XTimer* timer)
```

判断是否为单次触发模式。

---

#### XTimer_isPeriodic

```c
bool XTimer_isPeriodic(XTimer* timer)
```

判断是否为周期性任务。

**返回值:** true：非周期性（单次），false：周期性

---

#### XTimer_isRunning

```c
bool XTimer_isRunning(XTimer* timer)
```

判断定时器是否正在运行。

---

#### XTimer_isAutoDelete

```c
bool XTimer_isAutoDelete(XTimer* timer)
```

判断定时器是否自动释放。

---

#### XTimer_timerType

```c
XTimerType XTimer_timerType(XTimer* timer)
```

获取定时器类型。

---

### 信号与槽

#### XTimer_timeout_signal

```c
void* XTimer_timeout_signal(XTimer* timer)
```

定时器超时触发信号。

---

#### XTimer_callOnTimeout1

```c
void XTimer_callOnTimeout1(XTimer* timer, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
```

连接定时器超时信号到槽函数（带接收者）。

**参数:**
- `timer` - XTimer指针
- `receiver` - 信号接收者对象
- `slot_func` - 槽函数
- `type` - 连接类型

---

#### XTimer_callOnTimeout2

```c
void XTimer_callOnTimeout2(XTimer* timer, XSlotFunc2 slot_func)
```

连接定时器超时信号到槽函数（无接收者）。

---

### 静态方法

#### XTimer_singleShot1

```c
void XTimer_singleShot1(size_t msec, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
```

创建单次触发的定时器。

**参数:**
- `msec` - 超时时间（毫秒）
- `receiver` - 信号接收者对象
- `slot_func` - 槽函数
- `type` - 连接类型

---

#### XTimer_singleShot2

```c
void XTimer_singleShot2(size_t msec, XSlotFunc2 slot_func)
```

创建单次触发的定时器（无接收者）。

---

### 析构

#### XTimer_deleteLater

```c
#define XTimer_deleteLater XObject_deleteLater
```

延迟删除定时器。

---

## XTimerData 定时器数据

XTimerData是定时器的数据结构，包含定时器的核心参数。

### 头文件

```c
#include "XTimerData.h"
```

### 类型定义

#### XTimerCallback

```c
typedef void (*XTimerCallback)(void* userData, XObject* timer)
```

定时器超时回调函数类型。

**参数:**
- `userData` - 用户自定义数据
- `timer` - 定时器对象

---

### 结构体定义

```c
typedef struct XTimerData {
    uint32_t m_autoDelete : 1;      // 超时后是否自动释放
    uint32_t m_isSingleShot : 1;    // 是否为单次定时器
    XTimerId timerId;               // 定时器唯一标识ID
    void* m_userData;               // 用户自定义数据
    XTimerCallback m_timerCallback; // 超时回调函数
    uint64_t m_timeout;             // 首次超时时间
    uint64_t m_interval;            // 周期性触发时间间隔
} XTimerData;
```

### 创建与初始化

#### XTimerData_create

```c
XTimerData* XTimerData_create(XVtable* vtable)
```

创建XTimerData实例。

**参数:**
- `vtable` - 虚函数表指针

**返回值:** 成功返回XTimerData指针，失败返回NULL

---

#### XTimerData_init

```c
void XTimerData_init(XTimerData* timer, XVtable* vtable)
```

初始化XTimerData实例。

---

#### XTimerData_delete

```c
void XTimerData_delete(XTimerData* timer)
```

销毁XTimerData实例。

---

### 属性设置

```c
void XTimerData_setTimeout(XTimerData* timer, size_t value);
void XTimerData_setInterval(XTimerData* timer, size_t value);
void XTimerData_setUserData(XTimerData* timer, void* userData);
void XTimerData_setTimerCallback(XTimerData* timer, XTimerCallback callback);
void XTimerData_setTimerId(XTimerData* timer, size_t timerId);
void XTimerData_setAutoDelete(XTimerData* timer, bool del);
void XTimerData_setSingleShot(XTimerData* timer, bool ss);
```

### 属性获取

```c
bool XTimerData_isSingleShot(XTimerData* timer);
bool XTimerData_isPeriodic(XTimerData* timer);
size_t XTimerData_timeout(XTimerData* timer);
size_t XTimerData_interval(XTimerData* timer);
size_t XTimerData_timerId(XTimerData* timer);
void* XTimerData_userData(XTimerData* timer);
bool XTimerData_isAutoDelete(XTimerData* timer);
```

### 超时处理

#### XTimerData_out

```c
void XTimerData_out(XTimerData* timer)
```

定时器超时处理函数。

---

---

## XTimerGroupBase 定时器组基类

XTimerGroupBase是定时器组的基类，定义了定时器组的通用接口。

### 头文件

```c
#include "XTimerGroupBase.h"
```

### 类型定义

#### XHighResTimeFunc

```c
typedef uint64_t(*XHighResTimeFunc)(void)
```

高精度时间获取函数指针类型，返回纳秒数。

---

### 结构体定义

```c
typedef struct XTimerGroupBase {
    XObject m_class;              // 基类
    uint64_t m_precision;         // 定时器组精度
    uint64_t m_min_time;          // 最小时间
    uint64_t m_max_time;          // 最大时间
    uint64_t m_current_tick;      // 当前系统滴答
    XHighResTimeFunc m_high_res_time_func; // 高精度时间获取函数
} XTimerGroupBase;
```

### 初始化

#### XTimerGroupBase_init

```c
void XTimerGroupBase_init(XTimerGroupBase* group, uint16_t precision)
```

初始化定时器组基类。

**参数:**
- `group` - XTimerGroupBase指针
- `precision` - 精度（毫秒或纳秒）

---

#### XTimerGroupBase_setHighResTimeFunc

```c
void XTimerGroupBase_setHighResTimeFunc(XTimerGroupBase* base, XHighResTimeFunc func)
```

设置高精度时间源。

---

### 添加定时器

#### XTimerGroupBase_addTimerMs_base

```c
XHandle XTimerGroupBase_addTimerMs_base(XTimerGroupBase* group, XTimerData data)
```

添加毫秒级定时器。

**参数:**
- `group` - 定时器组指针
- `data` - 定时器数据

**返回值:** 定时器句柄

---

#### XTimerGroupBase_addTimerNs_base

```c
XHandle XTimerGroupBase_addTimerNs_base(XTimerGroupBase* group, XTimerData data)
```

添加纳秒级定时器。

---

### 移除定时器

#### XTimerGroupBase_removeTimer_base

```c
bool XTimerGroupBase_removeTimer_base(XTimerGroupBase* group, XHandle handle)
```

移除定时器（仅从任务中删除，需要手动释放）。

**参数:**
- `group` - 定时器组指针
- `handle` - 定时器句柄

**返回值:** 成功返回true，失败返回false

---

### 时间范围

#### XTimerGroupBase_timeRange

```c
bool XTimerGroupBase_timeRange(XTimerGroupBase* group, size_t* min_time, size_t* max_time)
```

获取定时器组可以管理的时间范围。

---

#### XTimerGroupBase_min_time

```c
size_t XTimerGroupBase_min_time(XTimerGroupBase* group)
```

获取最小可管理时间。

---

#### XTimerGroupBase_max_time

```c
size_t XTimerGroupBase_max_time(XTimerGroupBase* group)
```

获取最大可管理时间。

---

### 运行控制

#### XTimerGroupBase_tick_base

```c
void XTimerGroupBase_tick_base(XTimerGroupBase* group)
```

定时器组滴答处理。

---

#### XTimerGroupBase_handler_base

```c
void XTimerGroupBase_handler_base(XTimerGroupBase* group)
```

定时器组处理器。

---

#### XTimerGroupBase_clear_base

```c
void XTimerGroupBase_clear_base(XTimerGroupBase* group)
```

清空定时器组。

---

### 析构

```c
#define XTimerGroupBase_deleteLater XObject_deleteLater
```

---

## XTimeWheelGroup 时间轮组

XTimeWheelGroup是基于时间轮算法的定时器组实现，适用于毫秒级定时。

### 头文件

```c
#include "XTimeWheelGroup.h"
```

### 结构体定义

```c
typedef struct XTimeWheelGroup {
    XTimerGroupBase m_class;  // 继承自XTimerGroupBase
    XVector m_timeWheel;      // 多时间轮
    XAtomic_size_t m_count;   // 正在管理的定时器数量
    XMutex* m_mutex;          // 互斥锁
} XTimeWheelGroup;
```

### 创建与初始化

#### XTimeWheelGroup_create

```c
XTimeWheelGroup* XTimeWheelGroup_create(uint16_t precision)
```

创建时间轮组实例。

**参数:**
- `precision` - 精度（毫秒）

**返回值:** 成功返回XTimeWheelGroup指针，失败返回NULL

---

#### XTimeWheelGroup_init

```c
void XTimeWheelGroup_init(XTimeWheelGroup* group, uint16_t precision)
```

初始化时间轮组实例。

---

#### XTimeWheelGroup_class_init

```c
XVtable* XTimeWheelGroup_class_init()
```

初始化类的虚函数表。

---

### 时间轮管理

#### XTimeWheelGroup_addTimeWheel

```c
void XTimeWheelGroup_addTimeWheel(XTimeWheelGroup* group, size_t slotsCount)
```

添加时间轮层。

**参数:**
- `group` - XTimeWheelGroup指针
- `slotsCount` - 时间轮槽位数量

---

#### XTimeWheelGroup_removeTimeWheel

```c
void XTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group)
```

移除时间轮层。

---

### 属性获取

#### XTimeWheelGroup_count

```c
size_t XTimeWheelGroup_count(XTimeWheelGroup* group)
```

获取正在管理的定时器数量。

---

### 全局时间轮

#### XTimeWheelGroup_global

```c
XTimeWheelGroup* XTimeWheelGroup_global()
```

获取全局时间轮实例。

---

#### XTimeWheelGroup_GlobalExists

```c
bool XTimeWheelGroup_GlobalExists(void)
```

判断全局时间轮是否存在。

---

### 继承的操作宏

```c
#define XTimeWheelGroup_addTimerMs_base    XTimerGroupBase_addTimerMs_base
#define XTimeWheelGroup_removeTimer_base   XTimerGroupBase_removeTimer_base
#define XTimeWheelGroup_timeRange          XTimerGroupBase_timeRange
#define XTimeWheelGroup_min_time           XTimerGroupBase_min_time
#define XTimeWheelGroup_max_time           XTimerGroupBase_max_time
#define XTimeWheelGroup_tick_base          XTimerGroupBase_tick_base
#define XTimeWheelGroup_handler_base       XTimerGroupBase_handler_base
#define XTimeWheelGroup_clear_base         XTimerGroupBase_clear_base
#define XTimeWheelGroup_deleteLater        XTimerGroupBase_deleteLater
```

---

## XHrTimerGroup 高精度定时器组

XHrTimerGroup是基于红黑树的高精度定时器组实现，支持纳秒级精度。

### 头文件

```c
#include "XHrTimerGroup.h"
```

### 结构体定义

#### XHrTimerNodeData

```c
typedef struct XHrTimerNodeData {
    bool m_is_detached;        // 是否已从红黑树中分离
    bool m_is_was_deleted;     // 已被标记为删除
    uint64_t m_expire_time_ns; // 绝对到期时间（纳秒）
    XTimerData m_timer_data;   // 定时器参数
} XHrTimerNodeData;
```

#### XHrTimerGroup

```c
typedef struct XHrTimerGroup {
    XTimerGroupBase m_class;    // 继承自XTimerGroupBase
    XRBTreeNode* m_rbtree_root; // 红黑树根节点
    XRBTreeNode* m_min_node;    // 到期时间最小的节点(O(1)查找)
    XAtomic_size_t m_count;     // 正在管理的定时器数量
    XMutex* m_mutex;            // 互斥锁
} XHrTimerGroup;
```

### 创建与初始化

#### XHrTimerGroup_create

```c
XHrTimerGroup* XHrTimerGroup_create(uint64_t precision_ns)
```

创建高精度定时器组实例。

**参数:**
- `precision_ns` - 精度（纳秒）

**返回值:** 成功返回XHrTimerGroup指针，失败返回NULL

---

#### XHrTimerGroup_init

```c
void XHrTimerGroup_init(XHrTimerGroup* group, uint64_t precision_ns)
```

初始化高精度定时器组实例。

---

#### XHrTimerGroup_class_init

```c
XVtable* XHrTimerGroup_class_init(void)
```

初始化类的虚函数表。

---

### 属性获取

#### XHrTimerGroup_getNextExpireTime

```c
uint64_t XHrTimerGroup_getNextExpireTime(XHrTimerGroup* group)
```

获取最近一个定时器的绝对到期时间。

**返回值:** 如果存在定时器返回到期时间（纳秒），否则返回UINT64_MAX

---

#### XHrTimerGroup_count

```c
size_t XHrTimerGroup_count(XHrTimerGroup* group)
```

获取正在管理的定时器数量。

---

### 继承的操作宏

```c
#define XHrTimerGroup_setHighResTimeFunc XTimerGroupBase_setHighResTimeFunc
#define XHrTimerGroup_addTimerMs_base    XTimerGroupBase_addTimerMs_base
#define XHrTimerGroup_addTimerNs_base    XTimerGroupBase_addTimerNs_base
#define XHrTimerGroup_removeTimer_base   XTimerGroupBase_removeTimer_base
#define XHrTimerGroup_timeRange          XTimerGroupBase_timeRange
#define XHrTimerGroup_min_time           XTimerGroupBase_min_time
#define XHrTimerGroup_max_time           XTimerGroupBase_max_time
#define XHrTimerGroup_tick_base          XTimerGroupBase_tick_base
#define XHrTimerGroup_handler_base       XTimerGroupBase_handler_base
#define XHrTimerGroup_clear_base         XTimerGroupBase_clear_base
#define XHrTimerGroup_deleteLater        XTimerGroupBase_deleteLater
```

---

## 配置说明

XTimerConfig.h定义了定时器的实现方式配置。

### 头文件

```c
#include "XTimerConfig.h"
```

### 配置选项

#### XTIMER_IS_TIMEWHEEL

```c
#define XTIMER_IS_TIMEWHEEL 0
```

配置为时间轮实现。
- `0` - 使用平台原生实现
- `1` - 使用XTimeWheelGroup时间轮实现

---

#### Windows平台配置

```c
#define XTIMER_IS_TIMESETEVENT   1  // 使用timeSetEvent
#define XTIMER_IS_THREADPOOLTIMER 1  // 使用Threadpool Timer
```

当`XTIMER_IS_TIMEWHEEL`为0时，Windows平台可选择：
- `XTIMER_IS_TIMESETEVENT` - 使用多媒体定时器timeSetEvent
- `XTIMER_IS_THREADPOOLTIMER` - 使用线程池定时器

---

#### Linux/macOS/BSD平台配置

```c
#define XTIMER_IS_TIMERFD 1
```

使用timerfd实现。

---

#### FreeRTOS平台

```c
// FreeRTOS平台专用配置
```

---

### 实现优先级

1. **高优先级**：XTIMER_IS_TIMEWHEEL
2. **Windows平台**：XTIMER_IS_TIMESETEVENT > XTIMER_IS_THREADPOOLTIMER
3. **Posix平台**：XTIMER_IS_TIMERFD

---

## 附录

### 定时器类型对比

| 特性 | XTimeWheelGroup | XHrTimerGroup |
|------|-----------------|---------------|
| 数据结构 | 多层时间轮 | 红黑树 |
| 精度 | 毫秒级 | 纳秒级 |
| 添加定时器 | O(1) | O(log n) |
| 删除定时器 | O(1) | O(log n) |
| 查找最近到期 | O(1) | O(1) |
| 内存占用 | 较高 | 较低 |
| 适用场景 | 大量定时器 | 高精度定时 |

### 回调函数类型

#### XTimerCallback

```c
typedef void (*XTimerCallback)(void* userData, XObject* timer)
```

传统回调函数类型。

#### XSlotFunc1 / XSlotFunc2

信号槽机制的槽函数类型，用于面向对象的回调方式。

### 常用头文件包含

```c
#include "XTimer.h"
#include "XTimerData.h"
#include "XTimerGroupBase.h"
#include "XTimeWheelGroup.h"
#include "XHrTimerGroup.h"
```

### 使用示例

#### 基本定时器使用

```c
// 创建定时器
XTimer* timer = XTimer_create();

// 设置1000ms超时
XTimer_setTimeout(timer, 1000);

// 设置周期性触发
XTimer_setSingleShot(timer, false);

// 设置回调
void on_timeout(void* userData, XObject* timer) {
    printf("Timer triggered!\n");
}
XTimer_setTimerCallback(timer, on_timeout);

// 启动定时器
XTimer_start_base(timer);

// ... 运行中 ...

// 停止定时器
XTimer_stop_base(timer);

// 销毁
XTimer_deleteLater(timer);
```

#### 单次触发定时器

```c
// 3秒后触发一次，自动释放
XTimer_singleShot2(3000, my_callback_func);
```

#### 使用信号槽

```c
XTimer* timer = XTimer_create();
XTimer_setInterval(timer, 500);

// 连接超时信号到槽函数
XTimer_callOnTimeout1(timer, receiver, my_slot_func, XConnection_AutoConnection);

XTimer_start_base(timer);
```

#### 使用时间轮组

```c
// 创建时间轮组
XTimeWheelGroup* group = XTimeWheelGroup_create(10); // 10ms精度

// 添加时间轮层
XTimeWheelGroup_addTimeWheel(group, 100); // 100槽位
XTimeWheelGroup_addTimeWheel(group, 60);  // 60槽位

// 添加定时器
XTimerData data;
XTimerData_init(&data, NULL);
data.m_timeout = 1000;
data.m_interval = 500;
data.m_isSingleShot = 0;

XHandle handle = XTimeWheelGroup_addTimerMs_base(group, data);

// 在循环中调用tick
while (running) {
    XTimeWheelGroup_tick_base(group);
    XTimeWheelGroup_handler_base(group);
}

// 清理
XTimeWheelGroup_clear_base(group);
XTimeWheelGroup_deleteLater(group);
```

#### 使用高精度定时器组

```c
// 创建纳秒级精度定时器组
XHrTimerGroup* group = XHrTimerGroup_create(1000000); // 1ms精度

// 设置高精度时间源
XHrTimerGroup_setHighResTimeFunc(group, my_highres_time_func);

// 添加纳秒级定时器
XTimerData data;
// ... 设置data ...

XHandle handle = XHrTimerGroup_addTimerNs_base(group, data);

// 获取最近到期时间
uint64_t next_expire = XHrTimerGroup_getNextExpireTime(group);

// 清理
XHrTimerGroup_clear_base(group);
XHrTimerGroup_deleteLater(group);
```

### 最佳实践

1. **选择合适的实现**
   - 大量定时器：使用XTimeWheelGroup
   - 高精度需求：使用XHrTimerGroup
   - 简单场景：使用全局时间轮

2. **内存管理**
   - 设置`autoDelete`让定时器自动释放
   - 手动删除时使用`deleteLater`而非直接删除

3. **线程安全**
   - 定时器组内置互斥锁保护
   - 回调函数中避免长时间阻塞

4. **性能优化**
   - 合理设置时间轮层数和槽位数
   - 使用合适的时间精度避免不必要的开销

5. **调试建议**
   - 使用`timerId`标识定时器便于调试
   - 通过`userData`传递上下文信息