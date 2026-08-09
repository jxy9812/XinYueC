# XinYueC - 纯 C 语言面向对象库

[![许可证](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

------

## 目录

- [基本介绍](#基本介绍)
- [核心特性](#核心特性)
- [项目结构](#项目结构)
- [API 文档](#api-文档)
- [快速开始](#快速开始)
- [许可证](#许可证)

------

## 基本介绍

XinYueC 是一个使用纯 C 语言实现的、功能丰富的面向对象库。它旨在为 C 语言开发者提供一套强大、高效且易于使用的泛型容器、算法和跨平台接口，以弥补 C 语言在现代软件开发中的一些不足。

本项目最初是为了锻炼编程思维而创建，现已发展成为一个可用于实际项目的成熟库。其设计哲学深受 C++ STL 的影响，容器的成员函数命名尽可能与 C++ 保持一致，以降低学习成本。

### 核心特性

- **纯 C 实现**: 不依赖任何第三方库，仅使用标准 C 库。
- **泛型容器**: 提供多种数据结构（如链表、动态数组、映射等），支持任意数据类型。
- **面向对象风格**: 通过结构体和函数指针模拟类、继承和多态。
- **跨平台**: 提供了针对不同操作系统的抽象层，确保代码可移植性。
- **内存安全**: 容器支持自定义的元素拷贝、移动和析构回调，有效管理复杂对象的生命周期。

### 许可证

本项目采用 **MIT 开源许可协议**。

------

## 项目结构

```
XinYueC/
├── Src/                      # 源代码目录
│   ├── XClass/               # 面向对象基础模块
│   ├── XCode/                # 核心代码模块
│   ├── XContainer/           # 容器模块
│   ├── XData/                # 数据处理模块
│   ├── XEvent/               # 事件系统模块
│   ├── XMemory/              # 内存管理模块
│   └── XTimer/               # 定时器模块
├── Drive/                    # 平台驱动实现
│   ├── msvc/                 # MSVC编译器实现（原子操作）
│   ├── gcc/                  # GCC编译器实现（原子操作）
│   ├── windows/              # Windows平台实现（线程、互斥锁、IOCP等）
│   ├── Posix/                # POSIX平台实现（线程、互斥锁等）
│   ├── FreeRTOS/             # FreeRTOS实时操作系统实现
│   ├── keil/                 # Keil编译器实现
│   └── STM32/                # STM32单片机实现
├── Test/                     # 测试代码目录
│   ├── XMenuTest.c/h         # 菜单测试框架（交互式测试入口）
│   ├── XCodeTest/            # XCode模块测试
│   ├── XContainerTest/       # XContainer模块测试
│   ├── XTimerTest/           # XTimer模块测试
│   ├── XDeviceTest/          # 设备测试
│   ├── XIOTest/              # IO测试
│   ├── XLibraryTest/         # 库测试
│   ├── XMemoryTest/          # 内存测试
│   └── XProtocolTest/   # 协议栈测试
├── CMakeLists.txt            # CMake配置文件
└── README.md                 # 项目说明文档
```

> **平台支持说明**：因一人精力有限，目前完整的实现只有 **Windows 平台**。其他平台（Linux、macOS、FreeRTOS、STM32等）的部分功能可能尚未完成或未经充分测试，欢迎贡献代码！

------

## API 文档

XinYueC 由多个独立模块组成，每个模块都有详细的文档说明。

| 模块 | 说明 | 文档链接 |
|------|------|----------|
| **XClass** | 面向对象基础（类、继承、多态、虚函数表） | [XClass.md](Src/XClass/XClass.md) |
| **XCode** | 核心代码（原子操作、信号槽、线程、线程池、同步原语） | [XCode.md](Src/XCode/XCode.md) |
| **XContainer** | 泛型容器（Vector、List、Map、HashMap、Set等） | [XContainer.md](Src/XContainer/XContainer.md) |
| **XData** | 数据处理（String、ByteArray、DateTime、JSON、BSON） | [XData.md](Src/XData/XData.md) |
| **XEvent** | 事件系统（Event、EventLoop、EventDispatcher、XObject） | [XEvent.md](Src/XEvent/XEvent.md) |
| **XMemory** | 内存管理（内存池、智能指针、分配器） | [XMemory.md](Src/XMemory/XMemory.md) |
| **XTimer** | 定时器（单次定时器、周期定时器、高精度定时器） | [XTimer.md](Src/XTimer/XTimer.md) |

### 模块详情

#### XClass 面向对象基础

XClass模块是整个框架的基石，实现了C语言中的面向对象编程支持。

**主要功能：**
- 类、继承、多态的实现
- 虚函数表机制
- RTTI（运行时类型信息）

**详细文档：** [XClass.md](Src/XClass/XClass.md)

---

#### XCode 核心代码

XCode模块提供了应用程序开发的核心功能支持。

**主要功能：**
- **XAction** - 动作封装
- **XAtomic** - 跨平台原子操作
- **XCoreApplication** - 应用程序核心、事件循环
- **XMenu** - 层级菜单管理
- **XSignalSlot** - 信号与槽机制
- **XSocketNotifier** - 套接字I/O事件监控
- **XSync** - 线程同步原语（互斥锁、读写锁、信号量、条件变量）
- **XThread** - 线程封装
- **XThreadPool** - 线程池

**平台实现说明：**
- 原子操作：Windows使用Interlocked API，Linux使用GCC内置原子操作
- 事件循环：Windows使用消息队列，Linux使用epoll，macOS使用kqueue
- 线程同步：Windows使用CRITICAL_SECTION/SRWLock，Linux使用pthread

**详细文档：** [XCode.md](Src/XCode/XCode.md)

---

#### XContainer 容器

XContainer模块提供了丰富的泛型容器，支持任意数据类型。

**主要功能：**
- **XVector** - 动态数组，类似std::vector
- **XList** - 双向链表，类似std::list
- **XMap** - 红黑树映射，类似std::map
- **XHashMap** - 哈希映射，类似std::unordered_map
- **XSet** - 集合
- **XStack** - 栈
- **XQueue** - 队列
- **XDeque** - 双端队列

**详细文档：** [XContainer.md](Src/XContainer/XContainer.md)

---

#### XData 数据处理

XData模块提供了常用数据类型的封装和处理。

**主要功能：**
- **XString** - Unicode字符串
- **XStringList** - 字符串列表
- **XByteArray** - 字节数组
- **XDate/XTime/XDateTime** - 日期时间处理
- **XVariant** - 变体类型
- **JSON** - JSON解析与序列化
- **BSON** - BSON二进制JSON格式

**详细文档：** [XData.md](Src/XData/XData.md)

---

#### XEvent 事件系统

XEvent模块实现了事件驱动的编程模型。

**主要功能：**
- **XEvent** - 事件基类
- **XEventLoop** - 事件循环
- **XEventDispatcher** - 事件分发器
- **XObject** - 事件处理对象基类
- 定时器事件、自定义事件

**详细文档：** [XEvent.md](Src/XEvent/XEvent.md)

---

#### XMemory 内存管理

XMemory模块提供了内存管理功能。

**主要功能：**
- 内存池
- 智能指针
- 内存分配器

**详细文档：** [XMemory.md](Src/XMemory/XMemory.md)

---

#### XTimer 定时器

XTimer模块提供了定时器功能。

**主要功能：**
- 单次定时器
- 周期定时器
- 高精度定时器

**详细文档：** [XTimer.md](Src/XTimer/XTimer.md)

------

## 核心架构

XinYueC 的核心架构围绕几个关键概念构建：`XClass`（基类）、`XContainer`（容器基类）、`XObject`（事件处理对象）以及事件循环系统。

### XClass: 面向对象的基石

`XClass` 是所有类的基类，它实现了 C 语言中的“类”、“继承”和“多态”。

#### 结构体定义

```
1// XClass.h
2typedef struct XClass {
3    XVtable* m_vtable; // 虚函数表指针
4} XClass;
```

#### 关键宏与函数

- **`XClass_init(self)`**: 初始化一个 `XClass` 实例，通常作为派生类初始化的第一步。
- **`XClassGetVtable(self)`**: 获取对象的虚函数表指针。
- **`XClassGetVirtualFunc(self, func_index, func_type)`**: 通过虚函数表索引和函数类型，安全地调用虚函数，实现多态。

### XObject: 事件驱动的核心

`XObject` 是所有可以参与事件循环和信号槽通信的对象的基类。它继承自 `XClass` 并扩展了事件处理、线程亲和性和父子关系管理等功能。

#### 结构体定义 (关键部分)

```
1// XObject.h
2typedef struct XObject {
3    XClass m_class;                      // 继承自 XClass
4    
5    XAtomic_uint32_t m_posted_events;    // 原子计数：已投递但未处理的事件数量
6
7    uint32_t is_widget : 1;         // 是否为窗口部件
8    uint32_t block_sig : 1;         // 信号是否被阻塞
9    uint32_t was_deleted : 1;       // 对象是否已被标记为删除
10
11    XTimerId pollId;                // 轮询定时器ID
12    XSignalSlot* m_signalSlot;      // 信号与槽控制器
13    XObject* parent;                // 父对象
14    XVector* children;              // 子对象列表
15    XObject* sender;                // 发送者对象（用于信号槽）
16    XVector* filters;               // 事件过滤器列表
17    XThread* m_thread;              // 所属线程指针
18    XString* object_name;           // 对象名称
19} XObject;
```

#### 核心 API

- **`XObject_event(XObject\* self, XEvent\* e)`**: 对象的事件处理器。这是一个虚函数，派生类可以重写此方法来处理特定类型的事件。基类实现 `VXObject_event` 会根据事件类型分发到不同的处理函数（如 `timerEvent`, `childEvent` 等）。
- **`XObject_moveToThread(XObject\* object, XThread\* target_thread)`**: 将对象及其所有子对象移动到指定的线程。这是实现跨线程通信的关键。
- **`xobject_emit_signal(XObject\* sender, uint32_t signal_index, ...)`**: 发射一个信号。框架会根据连接类型（直接或队列）决定是立即调用槽函数还是将 `MetaCallEvent` 投递到接收者线程的事件队列。

------

## 容器系统

容器系统是 XinYueC 的重要组成部分，所有容器都继承自 `XContainer` 基类。

### XContainer: 容器基类

`XContainer` 为所有派生容器（如 `XList`, `XVector`, `XMap`）提供了统一的接口和基础功能。

#### 结构体定义

```
1// XContainer.h
2typedef struct XContainer {
3    XClass m_class;  // 虚函数表
4
5    // 回调函数：定义了如何操作容器内的元素
6    XCDataCopyMethod m_dataCopyMethod;      // 拷贝元素
7    XCDataMoveMethod m_dataMoveMethod;      // 移动元素
8    XCDataDeinitMethod m_dataDeinitMethod;  // 销毁元素
9    XCompare m_compare;                     // 比较元素
10
11    void* m_data;             // 数据区指针
12    size_t m_capacity;        // 容器容量
13    size_t m_size;            // 当前元素数量
14    size_t m_typeSize;        // 元素类型大小
15} XContainer;
```

#### 核心 API

- **`void XContainer_init(XContainer\* Object, size_t typeSize)`**: 初始化容器，`typeSize` 是单个元素的字节大小。
- **`size_t XContainer_size_base(const XContainer\* Object)`**: 返回容器元素数量。
- **`bool XContainer_isEmpty_base(const XContainer\* Object)`**: 判断容器是否为空。
- **`void XContainer_clear_base(XContainer\* Object)`**: 清空容器内容。
- **`void XContainer_swap_base(XContainer\* ObjectOne, XContainer\* ObjectTwo)`**: 交换两个容器的内容。

#### 便捷宏

为了简化使用，提供了大量宏：

```
1#define XContainerSize(Object) (((XContainer*)(Object))->m_size)
2#define XContainerIsEmpty(Object) (XContainerSize(Object) == 0)
3#define XContainerSetDataCopyMethod(Object, method) \
4    (((XContainer*)(Object))->m_dataCopyMethod = method)
5// ... 更多宏
```

### XList: 双向链表

`XList` 是一个功能完备的双向链表容器。

#### 核心 API

- 插入

  :

  - `XListNode* XList_push_front(XList* list, void* value)`: 在头部插入元素。
  - `XListNode* XList_push_back(XList* list, void* value)`: 在尾部插入元素。
  - `void XList_insert(XList* list, XListNode* pos, const void* begin, const void* end)`: 在指定位置前插入一个范围的元素。

- 删除

  :

  - `void XList_pop_front(XList* list)`: 删除头节点。
  - `void XList_pop_back(XList* list)`: 删除尾节点。
  - `void XList_erase(XList* list, const XListNode* begin, const XListNode* end)`: 删除一个范围内的节点。

- 访问

  :

  - `XListNode* XList_front(XList* list)`: 获取头节点。
  - `XListNode* XList_back(XList* list)`: 获取尾节点。
  - `XListNode* XList_find(const XList* list, XEquality equality, const void* value)`: 查找元素。

- 其他

  :

  - `void XList_sort(XList* list, XCompare compare)`: 对链表进行排序。
  - `void XList_clear(XList* list)`: 清空链表。

#### 迭代器

`XList` 支持正向和反向迭代器，便于遍历。

```
1// 正向遍历
2for (XList_iterator* it = XList_begin(list); 
3     it != XList_end(list); 
4     it = XList_iterator_add(list, it)) {
5    // 处理 *it
6}
7
8// 反向遍历
9for (XList_reverse_iterator* it = XList_rbegin(list); 
10     it != XList_rend(list); 
11     it = XList_reverse_iterator_add(list, it)) {
12    // 处理 *it
13}
```

### XVector: 动态数组

`XVector` 是一个类似 `std::vector` 的动态数组容器，支持随机访问。

#### 核心 API

- 容量管理

  :

  - `void XVector_reserve(XVector* vec, size_t new_cap)`: 预分配内存。
  - `void XVector_shrink_to_fit(XVector* vec)`: 释放未使用的内存。

- 元素访问

  :

  - `void* XVector_at(XVector* vec, size_t index)`: 访问指定索引的元素（带边界检查）。
  - `void* XVector_data(XVector* vec)`: 获取底层数组指针。

- 修改

  :

  - `void XVector_push_back(XVector* vec, const void* value)`: 在末尾添加元素。
  - `void XVector_pop_back(XVector* vec)`: 删除末尾元素。
  - `void XVector_insert(XVector* vec, size_t index, const void* value)`: 在指定位置插入元素。
  - `void XVector_erase(XVector* vec, size_t index)`: 删除指定位置的元素。

### XMap: 映射（关联容器）

`XMap` 是一个基于红黑树实现的键值对映射容器，保证元素按键有序存储。

#### 核心 API

- 构造/析构

  :

  - `void XMap_init(XMap* map, size_t key_size, size_t val_size)`: 初始化映射，指定键和值的大小。

- 容量

  :

  - `size_t XMap_size(const XMap* map)`: 返回元素数量。
  - `bool XMap_empty(const XMap* map)`: 判断是否为空。

- 元素访问

  :

  - `void* XMap_at(XMap* map, const void* key)`: 访问指定键对应的值（如果键不存在会插入默认值）。
  - `void* XMap_find(XMap* map, const void* key)`: 查找键，返回值指针或 `NULL`。

- 修改

  :

  - `void XMap_insert(XMap* map, const void* key, const void* value)`: 插入键值对。
  - `void XMap_erase(XMap* map, const void* key)`: 删除指定键的元素。

- 迭代器

  :

  - `XMap_iterator XMap_begin(XMap* map)`: 返回起始迭代器。
  - `XMap_iterator XMap_end(XMap* map)`: 返回结束迭代器。

------

## 事件循环与信号槽系统

这是 XinYueC 最具特色的部分，它使得在 C 语言中编写异步、响应式程序成为可能。

### 核心组件

1. **`XEvent`**: 事件的基类。所有具体事件（如 `XTimerEvent`, `XMetaCallEvent`）都继承自它。
2. **`XEventType`**: 定义了所有事件类型的枚举，例如 `XET_Timer`, `XET_MetaCall`, `XET_Custom` 等。
3. **`XAbstractEventDispatcher`**: 事件分发器的抽象基类，负责与操作系统底层（如 `epoll`, `kqueue`, `select`）交互，监听 I/O 和定时器事件。
4. **`XEventLoop`**: 事件循环的具体管理者。它的 `exec()` 方法是整个异步系统的主循环。
5. **`XSignalSlot`**: 信号槽连接管理器，负责维护信号与槽之间的连接，并在信号发射时执行相应的槽函数。
6. **`XThread` & `XThreadData`**: 线程及其私有数据。`XThreadData` 中包含了该线程的事件队列 (`m_postedEvents`) 和 `XSignalSlot` 实例。

### 工作流程

1. **事件投递**: 当需要异步处理某事时（例如，跨线程调用槽函数），会创建一个 `XEvent`（通常是 `XMetaCallEvent`）并通过 `XThreadData_postEvent` 将其放入目标线程的事件队列。
2. **事件循环**: 目标线程的 `XEventLoop` 在 `exec()` 循环中，会定期调用 `processPostedEvents()`。
3. **事件分发**: `processPostedEvents()` 从队列中取出事件，并调用 `event->receiver->event(event)`。
4. **事件处理**: `XObject` 的 `event` 虚函数根据事件类型进行分发。对于 `XET_MetaCall` 事件，会调用 `XEventMetaCall_handler`，该处理器最终会执行槽函数。
5. **信号发射**: 当 `xobject_emit_signal` 被调用时，`XSignalSlot` 会查找所有匹配的连接。如果是队列连接或跨线程，则会创建一个 `XMetaCallEvent` 并投递到接收者线程的事件队列，从而触发上述流程。

### 跨线程通信示例

```
1// 1. 创建工作对象并移动到新线程
2Worker* worker = malloc(sizeof(Worker));
3XThread* worker_thread = XThread_create();
4XObject_moveToThread((XObject*)worker, worker_thread);
5
6// 2. 建立连接（队列连接）
7xobject_connect(button, SIGNAL(clicked), worker, SLOT(doWork));
8
9// 3. 主线程发射信号
10xobject_emit_signal(button, SIGNAL(clicked));
11
12// 4. 框架自动将 MetaCallEvent 投递到 worker_thread 的事件队列
13// 5. worker_thread 的事件循环处理该事件，调用 worker->doWork()
```

------

## 快速开始

### CMake 集成

您可以轻松地将 XinYueC 集成到您自己的 CMake 项目中。

1. 在您的项目根目录下创建一个 `SourceFile` 文件夹。
2. 将 XinYueC 的源码（`.c` 和 `.h` 文件）放入此文件夹或其子文件夹中。
3. 在您的 `CMakeLists.txt` 中包含以下内容：

```
1cmake_minimum_required(VERSION 3.5)
2project(YourProject VERSION 0.1 LANGUAGES C)
3
4set(CMAKE_INCLUDE_CURRENT_DIR ON)
5set(CMAKE_C_STANDARD 99)
6set(CMAKE_C_STANDARD_REQUIRED ON)
7
8# 自动查找 SourceFile 目录下的所有头文件路径
9macro(FIND_INCLUDE_DIR result curdir)
10    file(GLOB_RECURSE children "${curdir}/*.hpp" "${curdir}/*.h")
11    set(dirlist "")
12    foreach(child ${children})
13        string(REGEX REPLACE "(.*)/.*" "\\1" LIB_NAME ${child})
14        if(IS_DIRECTORY ${LIB_NAME})
15            LIST(APPEND dirlist ${LIB_NAME})
16        endif()
17    endforeach()
18    set(${result} ${dirlist})
19endmacro()
20
21FIND_INCLUDE_DIR(INCLUDE_DIR_LIST "SourceFile")
22include_directories(${INCLUDE_DIR_LIST})
23
24# 递归搜索 SourceFile 目录下的所有源文件
25file(GLOB_RECURSE SOURCE_FILES "SourceFile/*.c" "SourceFile/*.h")
26
27# 添加可执行文件
28add_executable(${PROJECT_NAME} ${SOURCE_FILES})
```

每次添加或删除源文件后，只需右键点击 `CMakeLists.txt` 并重新配置即可。

### 运行测试

项目提供了交互式菜单测试系统，方便测试各个模块功能。

**测试目录结构：**

```
Test/
├── XMenuTest.c/h         # 菜单测试框架（交互式测试入口）
├── XCodeTest/            # XCode模块测试
│   ├── XThreadTest.c         # 线程测试
│   ├── XThreadPoolTest.c     # 线程池测试
│   ├── XDateTimeTest.c       # 日期时间测试
│   └── XStateMachineTest.c   # 状态机测试
├── XContainerTest/       # 容器模块测试
│   ├── XVectorTest.c         # 动态数组测试
│   ├── XListDLinkedTest.c    # 双向链表测试
│   ├── XMapTest.c            # 映射测试
│   ├── XHashMapTest.c        # 哈希映射测试
│   ├── XJsonTest.c           # JSON测试
│   ├── XBsonTest.c           # BSON测试
│   └── ...                   # 更多测试
├── XTimerTest/           # 定时器模块测试
│   ├── XTimerTest.c          # 定时器测试
│   ├── XHrTimerTest.c        # 高精度定时器测试
│   └── XTimerWheelTest.c     # 时间轮测试
├── XDeviceTest/          # 设备测试
├── XIOTest/              # IO测试
├── XLibraryTest/         # 库测试
├── XMemoryTest/          # 内存测试
└── XProtocolTest/   # 协议栈测试
```

**使用方法：**

1. 编译项目后运行可执行文件
2. 程序启动后会显示交互式菜单
3. 通过输入数字选择要测试的模块
4. 测试完成后自动返回上级菜单

**菜单示例：**

```
---------------测试代码---------------
1. 库测试
2. 容器测试
3. 核心代码测试
4. IO测试
5. 设备测试
6. 协议栈测试
7. 定时器测试
8. 内存测试
请输入选择:
```

------

## 许可证

本项目采用 **MIT 开源许可协议**。

```
MIT License

Copyright (c) 2024 XinYueC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

------

## 贡献

欢迎提交 Issue 和 Pull Request！

------

