# XinYueC 面向对象系统文档

## 目录

- [概述](#概述)
- [XVtable 虚函数表](#xvtable-虚函数表)
- [XClass 类基类](#xclass-类基类)
- [XObject 对象基类](#xobject-对象基类)
- [继承与多态](#继承与多态)
- [信号与槽](#信号与槽)
- [事件系统](#事件系统)
- [附录](#附录)

---

## 概述

XinYueC面向对象系统是用C语言实现的轻量级面向对象框架，支持继承、多态、信号槽和事件系统。

### 核心特性

- **虚函数表机制**：实现运行时多态
- **单继承模型**：简洁的类继承体系
- **信号与槽**：灵活的对象间通信机制
- **事件系统**：完善的事件处理机制
- **对象树**：父子对象关系管理

### 命名规范

| 类型 | 命名格式 | 示例 |
|------|---------|------|
| 类结构体 | `X类名` | `XObject` |
| 虚函数表枚举 | `E类名_函数名` | `EXObject_Event` |
| 类初始化函数 | `X类名_class_init` | `XObject_class_init` |
| 对象创建函数 | `X类名_create` | `XObject_create` |
| 对象初始化函数 | `X类名_init` | `XObject_init` |

---

## XVtable 虚函数表

虚函数表是实现C语言多态的核心机制，存储类的虚函数指针。

### 头文件

```c
#include "XVtable.h"
```

### 结构体定义

```c
typedef struct {
    void** data;        // 函数指针数组
    uint16_t size;      // 当前元素数量
    uint16_t capacity;  // 最大容量
    uint16_t isStack;   // 是否在栈上
} XVtable;
```

### 创建与初始化

#### XVtable_create

```c
XVtable* XVtable_create()
```

在堆上创建虚函数表。

**返回值:** 成功返回XVtable指针，失败返回NULL

---

#### XVtable_init_stack

```c
void XVtable_init_stack(XVtable* vtable, void** data, size_t size)
```

初始化栈上的虚函数表。

**参数:**
- `vtable` - XVtable指针
- `data` - 存储函数指针的数组
- `size` - 数组大小

**返回值:** 无

---

### 添加函数

#### XVtable_append_array

```c
void XVtable_append_array(XVtable* vtable, const void** begin, size_t n)
```

追加函数指针数组。

**参数:**
- `vtable` - XVtable指针
- `begin` - 函数指针数组起始地址
- `n` - 元素数量

**返回值:** 无

---

#### XVtable_append_vtable

```c
void XVtable_append_vtable(XVtable* vtable, XVtable* other)
```

追加另一个虚函数表的内容(用于继承)。

**参数:**
- `vtable` - 目标XVtable指针
- `other` - 源XVtable指针

**返回值:** 无

---

### 虚函数表枚举定义宏

| 宏 | 说明 |
|----|------|
| `XCLASS_DEFINE_BEGING(Class)` | 开始定义虚函数枚举 |
| `XCLASS_DEFINE_ENUM(Class, Value)` | 定义枚举值 |
| `XCLASS_DEFINE_END(Class)` | 结束定义 |
| `XCLASS_DEFINE_EXTEND_END(Class, Parent)` | 结束定义(继承版本) |
| `XCLASS_VTABLE_GET_SIZE(Class)` | 获取虚函数表大小 |

### 示例：定义虚函数枚举

```c
XCLASS_DEFINE_BEGING(XClass)
XCLASS_DEFINE_ENUM(XClass, Copy),
XCLASS_DEFINE_ENUM(XClass, Move),
XCLASS_DEFINE_ENUM(XClass, Deinit),
XCLASS_DEFINE_END(XClass)
```

展开后相当于:
```c
enum XClassVtableEnum {
    EXClass_Copy,
    EXClass_Move,
    EXClass_Deinit,
    EXClass_END_SIZE
};
```

---

## XClass 类基类

XClass是所有类的基类，提供基本的虚函数表管理和对象生命周期控制。

### 头文件

```c
#include "XClass.h"
```

### 结构体定义

```c
typedef struct XClass {
    XVtable* m_vtable;  // 虚函数表
    FreeMethod m_free;  // 释放方法
} XClass;
```

### 虚函数枚举

```c
enum XClassVtableEnum {
    EXClass_Copy,    // 拷贝函数索引
    EXClass_Move,    // 移动函数索引
    EXClass_Deinit,  // 反初始化函数索引
    EXClass_END_SIZE // 虚函数表大小
};
```

### 创建与初始化

#### XClass_class_init

```c
XVtable* XClass_class_init()
```

初始化XClass的虚函数表。

**返回值:** 违始化完成的虚函数表指针

---

#### XClass_init

```c
void XClass_init(XClass* object)
```

初始化XClass对象。

**参数:**
- `object` - 待初始化的XClass指针

**返回值:** 无

---

#### XClass_delete_base

```c
void XClass_delete_base(XClass* object)
```

销毁XClass对象并释放内存。

**参数:**
- `object` - XClass指针，可为NULL

**返回值:** 无

---

### 核心操作

#### XClass_copy_base

```c
void XClass_copy_base(XClass* object, const XClass* src)
```

拷贝对象内容。

**参数:**
- `object` - 目标对象
- `src` - 源对象

**返回值:** 无

---

#### XClass_move_base

```c
void XClass_move_base(XClass* object, XClass* src)
```

移动对象资源。

**参数:**
- `object` - 目标对象
- `src` - 源对象

**返回值:** 无

---

#### XClass_deinit_base

```c
void XClass_deinit_base(XClass* object)
```

反初始化对象(释放资源但不销毁对象本身)。

**参数:**
- `object` - XClass指针

**返回值:** 无

---

### 常用宏

| 宏 | 说明 |
|----|------|
| `XClassGetVtable(Object)` | 获取对象的虚函数表 |
| `XClassSetVtable(Obj, Type)` | 设置对象的虚函数表 |
| `XClassGetVirtualFunc(Object, Offset, Type)` | 获取虚函数指针 |
| `XVTABLE_CREAT(Vtable)` | 创建虚函数表 |
| `XVTABLE_HEAP_INIT(Vtable)` | 堆上初始化虚函数表 |
| `XVTABLE_STACK_INIT(Vtable, Size)` | 栈上初始化虚函数表 |
| `XVTABLE_INHERIT(Vtable, VtableBase)` | 继承虚函数表 |
| `XVTABLE_OVERLOAD(Vtable, Type, Func)` | 重载虚函数 |

### 参数检查宏

```c
// 检查参数是否为NULL，为NULL时返回
#define ISNULL(args, str) (ArgIsNULL(isNULLInfo(args, str)))

// 检查参数是否为NULL，为NULL时退出程序
#define XAssert(args, str) { if(ISNULL(args, str)) exit(-1); }
```

---

## XObject 对象基类

XObject是所有对象的基类，继承自XClass，提供对象树、信号槽、事件系统等功能。

### 头文件

```c
#include "XObject.h"
```

### 结构体定义

```c
typedef struct XObject {
    XClass m_class;                      // 继承的基类
    XAtomic_uint32_t m_posted_events;    // 已投递事件计数
    
    // 对象标志位
    uint32_t is_widget : 1;              // 是否为窗口部件
    uint32_t block_sig : 1;              // 信号是否被阻塞
    uint32_t was_deleted : 1;            // 是否已标记删除
    uint32_t is_deleting_children : 1;   // 是否正在删除子对象
    // ... 其他标志位
    
    XTimerId pollId;                     // 轮询定时器ID
    XSignalSlot* m_signalSlot;           // 信号槽控制器
    XObject* parent;                     // 父对象
    XVector* children;                   // 子对象列表
    XObject* sender;                     // 发送者对象
    XVector* filters;                    // 事件过滤器列表
    XThread* m_thread;                   // 所属线程
    XString* object_name;                // 对象名称
} XObject;
```

### 虚函数枚举

```c
enum XObjectVtableEnum {
    EXObject_Poll,            // 轮询函数
    EXObject_Event,           // 事件处理
    EXObject_EventFilter,     // 事件过滤器
    EXObject_ChildEvent,      // 子对象事件
    EXObject_ConnectNotify,   // 连接通知
    EXObject_CustomEvent,     // 自定义事件
    EXObject_DisconnectNotify,// 断开连接通知
    EXObject_TimerEvent,      // 定时器事件
    EXObject_END_SIZE
};
```

### 创建与初始化

#### XObject_create

```c
XObject* XObject_create()
```

在堆上创建XObject实例。

**返回值:** 成功返回XObject指针，失败返回NULL

---

#### XObject_init

```c
void XObject_init(XObject* object)
```

初始化XObject实例。

**参数:**
- `object` - 待初始化的XObject指针

**返回值:** 无

---

### 对象树管理

#### XObject_parent

```c
XObject* XObject_parent(XObject* object)
```

获取父对象。

**参数:**
- `object` - XObject指针

**返回值:** 返回父对象指针，无父对象返回NULL

---

#### XObject_setParent

```c
void XObject_setParent(XObject* object, XObject* parent)
```

设置父对象。

**参数:**
- `object` - XObject指针
- `parent` - 新的父对象(可为NULL)

**返回值:** 无

---

#### XObject_children

```c
const XVector* XObject_children(const XObject* self)
```

获取子对象列表。

**参数:**
- `self` - XObject指针

**返回值:** 返回子对象列表

---

#### XObject_findChild

```c
XObject* XObject_findChild(const XObject* self, const char* name, XFindChildOption options)
```

查找子对象。

**参数:**
- `self` - XObject指针
- `name` - 对象名称
- `options` - 查找选项

**返回值:** 找到返回对象指针，未找到返回NULL

---

### 对象属性

#### XObject_objectName

```c
const XString* XObject_objectName(const XObject* self)
```

获取对象名称。

**参数:**
- `self` - XObject指针

**返回值:** 返回对象名称

---

#### XObject_setObjectName

```c
void XObject_setObjectName(XObject* self, const XString* name)
```

设置对象名称。

**参数:**
- `self` - XObject指针
- `name` - 新名称

**返回值:** 无

---

### 定时器

#### XObject_startTimer_ms

```c
XTimerId XObject_startTimer_ms(XObject* self, uint64_t interval, XTimerType timerType)
```

启动定时器(毫秒)。

**参数:**
- `self` - XObject指针
- `interval` - 定时间隔(毫秒)
- `timerType` - 定时器类型

**返回值:** 返回定时器ID

---

#### XObject_killTimer

```c
void XObject_killTimer(XObject* self, XTimerId timerId)
```

停止定时器。

**参数:**
- `self` - XObject指针
- `timerId` - 定时器ID

**返回值:** 无

---

### 线程相关

#### XObject_thread

```c
XThread* XObject_thread(XObject* object)
```

获取对象所属线程。

**参数:**
- `object` - XObject指针

**返回值:** 返回线程指针

---

#### XObject_moveToThread

```c
bool XObject_moveToThread(XObject* object, XThread* target_thread)
```

将对象移动到指定线程。

**参数:**
- `object` - XObject指针
- `target_thread` - 目标线程

**返回值:** 成功返回true，失败返回false

---

### 延迟删除

#### XObject_deleteLater

```c
void XObject_deleteLater(XObject* object)
```

延迟删除对象(堆对象)。

**参数:**
- `object` - XObject指针

**返回值:** 无

**说明:** 对象将在事件循环处理完所有待处理事件后被删除

---

#### XObject_deinitLater

```c
void XObject_deinitLater(XObject* object)
```

延迟反初始化对象(栈对象)。

**参数:**
- `object` - XObject指针

**返回值:** 无

---

## 继承与多态

XinYueC使用虚函数表实现单继承和多态。

### 定义派生类

#### 1. 定义结构体

```c
// 继承XClass
typedef struct MyObject {
    XClass m_class;  // 基类成员必须放在第一位
    int my_data;     // 派生类成员
} MyObject;
```

#### 2. 定义虚函数枚举

```c
// 继承XClass的虚函数
XCLASS_DEFINE_BEGING(MyObject)
XCLASS_DEFINE_ENUM(MyObject, MyFunc)= XCLASS_VTABLE_GET_SIZE(XClass),//第一个
XCLASS_DEFINE_END(MyObject, XClass)
```

#### 3. 实现类初始化函数

```c
XVtable* MyObject_class_init() 
{
   	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
    
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, (void*)MyObject_deinit_base);
    
    // 添加新函数
    void* funcs[] = { (void*)MyObject_myFunc };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(funcs);
    
    return XVTABLE_DEFAULT;
}
```

#### 4. 实现创建和初始化函数

```c
MyObject* MyObject_create() {
    MyObject* obj = XMalloc(sizeof(MyObject), XMEMORY_TYPE_SYSTEM);
    if (obj) MyObject_init(obj);
    return obj;
}

void MyObject_init(MyObject* obj) {
    XClass_init(&obj->m_class);           // 初始化基类
    XClassSetVtable(obj, MyObject);       // 设置虚函数表
    obj->my_data = 0;
}
```

### 多态调用

#### 调用虚函数

```c
// 通过虚函数表调用
void MyObject_deinit_base(MyObject* obj) {
    // 先清理派生类资源
    obj->my_data = 0;
    
    // 调用基类的deinit
    void (*deinit)(XClass*) = XClassGetVirtualFunc(obj, EXClass_Deinit, void(*)(XClass*));
    deinit(&obj->m_class);
}
```

### 继承XObject

```c
// 定义结构体
typedef struct MyWidget {
    XObject m_class;  // 继承XObject
    int x, y;
} MyWidget;

// 定义虚函数枚举
XCLASS_DEFINE_BEGING(MyWidget)
XCLASS_DEFINE_EXTEND_END(MyWidget, XObject)//继承虚函数表不扩展

// 类初始化
XVtable* MyWidget_class_init() {
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(MyWidget))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XObject);
    
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, (void*)MyWidget_event_base);
    
    void* funcs[] = { (void*)MyWidget_paint };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(funcs);
    
    return XVTABLE_DEFAULT;
}
```

---

## 信号与槽

XinYueC实现了类似Qt的信号槽机制，支持对象间的松耦合通信。

### 信号定义

使用`Signals`关键字标记信号函数：

```c
Signals
void* MyObject_clicked_signal(MyObject* obj) {
    // 信号实现
    XEmitSignal(obj, (size_t)MyObject_clicked_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
```

### 连接信号与槽

#### XObject_connect1

```c
XConnection* XObject_connect1(XObject* sender, size_t signal, 
                               XObject* receiver, XSlotFunc1 slot_func, 
                               XConnectionType type)
```

连接信号到槽函数。

**参数:**
- `sender` - 发送者对象
- `signal` - 信号标识
- `receiver` - 接收者对象
- `slot_func` - 槽函数
- `type` - 连接类型

**返回值:** 返回连接对象指针

---

#### XObject_disconnect1

```c
bool XObject_disconnect1(XObject* sender, size_t signal, 
                          XObject* receiver, XSlotFunc1 slot_func)
```

断开信号与槽的连接。

**返回值:** 成功返回true

---

### 发射信号

#### XEmitSignal 宏

```c
#define XEmitSignal(object, signal, args, del, ref_count, priority) \
    if(object) XObject_emitSignal(object, signal, args, del, ref_count, priority); \
    return signal
```

同步发射信号。

#### XEmitSignalQueue 宏

异步发射信号(队列模式)。

### 示例

```c
// 定义槽函数
void on_clicked(XObject* receiver, XVarList* args, XObject* sender) {
    printf("Button clicked!\n");
}

// 连接信号与槽
XObject_connect1(button, XSignal(MyButton_clicked_signal), 
                 handler, on_clicked, XCONNECTION_AUTO);

// 发射信号
MyButton_clicked_signal(button);
```

---

## 事件系统

XObject提供了完善的事件处理机制。

### 事件处理函数

#### XObject_event_base

```c
bool XObject_event_base(XObject* self, XEvent* e)
```

核心事件处理器。

**参数:**
- `self` - XObject指针
- `e` - 事件指针

**返回值:** 事件已处理返回true

---

#### XObject_timerEvent_base

```c
void XObject_timerEvent_base(XObject* self, XEventTimer* event)
```

定时器事件处理。

---

#### XObject_childEvent_base

```c
void XObject_childEvent_base(XObject* self, XChildEvent* event)
```

子对象事件处理。

---

### 事件过滤器

#### XObject_installEventFilter

```c
void XObject_installEventFilter(XObject* self, XObject* filterObj)
```

安装事件过滤器。

**参数:**
- `self` - 目标对象
- `filterObj` - 过滤器对象

---

#### XObject_removeEventFilter

```c
void XObject_removeEventFilter(XObject* self, XObject* obj)
```

移除事件过滤器。

---

#### XObject_eventFilter_base

```c
bool XObject_eventFilter_base(XObject* self, XObject* watched, XEvent* event)
```

事件过滤器实现。

---

### 轮询机制

#### XObject_poll_base

```c
void XObject_poll_base(XObject* object)
```

轮询函数，在事件循环中周期性调用。

---

#### XObject_setPollTime

```c
void XObject_setPollTime(XObject* object, size_t interval)
```

设置轮询间隔。

**参数:**
- `object` - XObject指针
- `interval` - 间隔(毫秒)，0表示关闭轮询

---

## 附录

### 虚函数表布局

```
+------------------+
| XClass虚函数      |
+------------------+
| Copy             |  索引 0
| Move             |  索引 1
| Deinit           |  索引 2
+------------------+
| XObject虚函数     |
+------------------+
| Poll             |  索引 3
| Event            |  索引 4
| EventFilter      |  索引 5
| ChildEvent       |  索引 6
| ConnectNotify    |  索引 7
| CustomEvent      |  索引 8
| DisconnectNotify |  索引 9
| TimerEvent       |  索引 10
+------------------+
```

### 对象生命周期

| 阶段 | 堆对象 | 栈对象 |
|------|--------|--------|
| 创建 | `Xxx_create()` | 声明变量 |
| 初始化 | `Xxx_init()` | `Xxx_init()` |
| 使用 | ... | ... |
| 清理 | `Xxx_delete_base()` | `Xxx_deinit_base()` |
| 延迟清理 | `XObject_deleteLater()` | `XObject_deinitLater()` |

### 定时器类型

| 类型 | 说明 |
|------|------|
| `XTimerType_PreciseTimer` | 精确定时器，高精度高功耗 |
| `XTimerType_CoarseTimer` | 粗略定时器，允许±5%误差 |
| `XTimerType_VeryCoarseTimer` | 秒级精度 |

### 查找子对象选项

| 选项 | 说明 |
|------|------|
| `XFindDirectChildrenOnly` | 仅查找直接子对象 |
| `XFindChildrenRecursively` | 递归查找所有后代 |

### 常用宏速查

| 宏 | 用途 |
|----|------|
| `XClassGetVtable(obj)` | 获取虚函数表 |
| `XClassSetVtable(obj, Type)` | 设置虚函数表 |
| `XClassGetVirtualFunc(obj, idx, type)` | 获取虚函数 |
| `XVtable_At(vtable, idx)` | 访问虚函数表元素 |
| `container_of(ptr, type, member)` | 从成员指针获取结构体指针 |
| `XSignal(signal)` | 生成信号标识 |
| `XEmitSignal(...)` | 同步发射信号 |
| `XEmitSignalQueue(...)` | 异步发射信号 |
| `ISNULL(ptr, msg)` | NULL检查 |
| `XAssert(ptr, msg)` | NULL断言 |

### 继承实现步骤

1. **定义结构体**：基类成员放在第一位
2. **定义虚函数枚举**：使用`XCLASS_DEFINE_*`宏
3. **实现`class_init`函数**：创建并初始化虚函数表
4. **实现`init`函数**：初始化对象成员
5. **实现虚函数重载**：使用`XVTABLE_OVERLOAD`宏

### 线程安全

- 虚函数表初始化是线程安全的(使用静态局部变量)
- 对象操作不是线程安全的，需要外部同步
- 信号槽支持跨线程连接

### 最佳实践

1. **对象创建**
   - 堆对象使用`create`/`delete_base`
   - 栈对象使用`init`/`deinit_base`

2. **虚函数重写**
   - 重写`deinit`时记得调用父类的`deinit`，必须用虚函数表的方式才可以调用父类或者使用提供的宏XClass_Deinit_Parent
   - 使用`XClass_Parent`宏获取父类函数

3. **信号槽**
   - 使用`XEmitSignal`宏发射信号
   - 注意循环连接导致的无限递归

4. **事件处理**
   - 重写`event_base`处理自定义事件
   - 使用事件过滤器进行事件拦截