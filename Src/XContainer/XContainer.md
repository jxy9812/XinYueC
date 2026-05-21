# XinYueC 容器库文档

## 目录

- [概述](#概述)
- [容器基类](#容器基类)
  - [XContainer 容器基类](#xcontainer-容器基类)
  - [XListBase 链表基类](#xlistbase-链表基类)
  - [XMapBase 映射基类](#xmapbase-映射基类)
  - [XSetBase 集合基类](#xsetbase-集合基类)
  - [XStackBase 栈基类](#xstackbase-栈基类)
  - [XQueueBase 队列基类](#xqueuebase-队列基类)
- [序列容器](#序列容器)
  - [XVector 动态数组](#xvector-动态数组)
  - [XListDLinked 双向链表](#xlistdlinked-双向链表)
  - [XListSLinked 单向链表](#xlistslinked-单向链表)
  - [XLockFreeList 无锁链表](#xlockfreelist-无锁链表)
  - [XByteArray 字节数组](#xbytearray-字节数组)
  - [XString 字符串](#xstring-字符串)
  - [XStringList 字符串列表](#xstringlist-字符串列表)
  - [XVariantList 变体列表](#xvariantlist-变体列表)
- [关联容器](#关联容器)
  - [XMap 映射](#xmap-映射)
  - [XHashMap 哈希映射](#xhashmap-哈希映射)
  - [XSet 集合](#xset-集合)
  - [XHashSet 哈希集合](#xhashset-哈希集合)
- [适配器容器](#适配器容器)
  - [XStack 栈](#xstack-栈)
  - [XLockFreeStack 无锁栈](#xlockfreestack-无锁栈)
  - [XQueue 队列](#xqueue-队列)
  - [XLockFreeQueue 无锁队列](#xlockfreequeue-无锁队列)
  - [XPriorityQueue 优先队列](#xpriorityqueue-优先队列)
  - [XCircularQueue 环形队列](#xcircularqueue-环形队列)
- [特殊容器](#特殊容器)
  - [XBitArray 比特数组](#xbitarray-比特数组)
  - [XRingBuffer 环形缓冲区](#xringbuffer-环形缓冲区)
  - [XRingChunk 环形块](#xringchunk-环形块)
- [迭代器](#迭代器)
- [内存管理](#内存管理)
- [比较函数](#比较函数)
- [附录](#附录)

---

## 概述

XinYueC容器库是一套用C语言实现的通用容器库，采用面向对象设计，支持继承和多态。

### 容器分类

#### 基类

| 基类 | 说明 |
|------|------|
| XContainer | 所有容器的基类，定义公共接口 |
| XListBase | 链表基类，所有链表容器的父类 |
| XMapBase | 映射基类，所有映射容器的父类 |
| XSetBase | 集合基类，所有集合容器的父类 |
| XStackBase | 栈基类，所有栈容器的父类 |
| XQueueBase | 队列基类，所有队列容器的父类 |

#### 序列容器

| 容器 | 说明 |
|------|------|
| XVector | 动态数组，连续内存，支持O(1)随机访问 |
| XListDLinked | 双向循环链表，高效任意位置插入删除 |
| XListSLinked | 单向链表，内存占用小 |
| XLockFreeList | 无锁链表，多线程安全 |
| XByteArray | 字节数组，专用于uint8_t，支持编码转换 |
| XString | Unicode字符串，UTF-16编码 |
| XStringList | 字符串列表，XString的特化版本 |
| XVariantList | 变体列表，存储XVariant类型 |

#### 关联容器

| 容器 | 说明 |
|------|------|
| XMap | 有序映射，红黑树实现，O(log n)查找 |
| XHashMap | 哈希映射，O(1)平均查找 |
| XSet | 有序集合，红黑树实现 |
| XHashSet | 哈希集合，O(1)平均查找 |

#### 适配器容器

| 容器 | 说明 |
|------|------|
| XStack | 栈，LIFO，基于XVector实现 |
| XLockFreeStack | 无锁栈，多线程安全 |
| XQueue | 队列，FIFO，基于链表实现 |
| XLockFreeQueue | 无锁队列，多线程安全 |
| XPriorityQueue | 优先队列，堆实现 |
| XCircularQueue | 环形队列，固定大小 |

#### 特殊容器

| 容器 | 说明 |
|------|------|
| XBitArray | 比特数组，支持位级操作 |
| XRingBuffer | 环形缓冲区，动态扩容 |
| XRingChunk | 环形块，分块存储 |

### 命名规范

| 前缀/后缀 | 说明 | 示例 |
|-----------|------|------|
| `X` | 类型前缀 | `XVector`, `XMap` |
| `_base` | 基础函数后缀 | `XVector_size_base` |
| `_Base` | 类型安全宏后缀 | `XVector_Push_Back_Base` |
| `_create` | 创建函数 | `XVector_create` |
| `_init` | 初始化函数 | `XVector_init` |
| `_delete_base` | 销毁函数 | `XVector_delete_base` |

---

## 容器基类

### XContainer 容器基类

`XContainer`是所有容器的基类，定义了容器的公共接口。

#### 头文件

```c
#include "XContainer.h"
```

#### 核心结构

```c
typedef struct XContainer
{
    XClass m_class;           // 继承自XClass
    uint32_t m_useCow : 1;    // COW标志
    uint32_t m_typeSize : 31; // 元素类型大小
    void* m_data;             // 数据指针
    size_t m_capacity;        // 容量
    size_t m_size;            // 元素数量
} XContainer;
```

#### 通用操作

以下API适用于所有继承自XContainer的容器。

##### XContainer_copy_base

```c
void XContainer_copy_base(XContainer* dest, const XContainer* src)
```

深拷贝容器内容。

**参数:**
- `dest` - 目标容器指针
- `src` - 源容器指针

**返回值:** 无

**注意:** 目标容器原有的内容会被覆盖

---

##### XContainer_move_base

```c
void XContainer_move_base(XContainer* dest, XContainer* src)
```

移动容器资源(转移所有权)。

**参数:**
- `dest` - 目标容器指针
- `src` - 源容器指针

**返回值:** 无

**注意:** 移动后src变为空状态，不应再访问

---

##### XContainer_deinit_base

```c
void XContainer_deinit_base(XContainer* object)
```

反初始化容器(释放内部资源，保留容器本身)。

**参数:**
- `object` - 容器指针

**返回值:** 无

**注意:** 用于栈上分配的容器对象

---

##### XContainer_delete_base

```c
void XContainer_delete_base(XContainer* object)
```

销毁容器并释放所有内存。

**参数:**
- `object` - 容器指针，可为NULL

**返回值:** 无

**注意:** 释放容器本身及其所有元素，object为NULL时不执行任何操作

---

##### XContainer_clear_base

```c
void XContainer_clear_base(XContainer* object)
```

清空容器所有元素。

**参数:**
- `object` - 容器指针

**返回值:** 无

**注意:** 保留容量，不释放内存

---

##### XContainer_isEmpty_base

```c
bool XContainer_isEmpty_base(const XContainer* object)
```

判断容器是否为空。

**参数:**
- `object` - 容器指针

**返回值:** 为空返回true，否则返回false

---

##### XContainer_size_base

```c
size_t XContainer_size_base(const XContainer* object)
```

获取容器元素数量。

**参数:**
- `object` - 容器指针

**返回值:** 返回元素数量

---

##### XContainer_capacity_base

```c
size_t XContainer_capacity_base(const XContainer* object)
```

获取容器容量。

**参数:**
- `object` - 容器指针

**返回值:** 返回当前容量

---

##### XContainer_swap_base

```c
void XContainer_swap_base(XContainer* a, XContainer* b)
```

交换两个容器的内容。

**参数:**
- `a` - 第一个容器指针
- `b` - 第二个容器指针

**返回值:** 无

---

##### XContainer_typeSize_base

```c
size_t XContainer_typeSize_base(const XContainer* object)
```

获取容器元素类型大小。

**参数:**
- `object` - 容器指针

**返回值:** 返回元素类型大小(字节数)

---

### XListBase 链表基类

`XListBase`是所有链表容器(如XListDLinked、XListSLinked)的基类。

#### 头文件

```c
#include "XListBase.h"
```

#### 初始化

##### XListBase_init

```c
void XListBase_init(XListBase* list, size_t typeSize, bool useCow)
```

初始化已分配内存的链表基类对象。

**参数:**
- `list` - 待初始化的XListBase指针
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用COW

**返回值:** 无

---

#### 插入操作

##### XListBase_push_front_base

```c
XListBaseNode* XListBase_push_front_base(XListBase* list, void* pvData)
```

在链表头部插入元素(拷贝语义)。

**参数:**
- `list` - 链表指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListBase_Push_Front_Base

```c
#define XListBase_Push_Front_Base(list, Type, value) { Type t = value; XListBase_push_front_base(list, &t); }
```

类型安全头部插入宏。

**参数:**
- `list` - 链表指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XListBase_push_front_move_base

```c
XListBaseNode* XListBase_push_front_move_base(XListBase* list, void* pvData)
```

在链表头部插入元素(移动语义)。

**参数:**
- `list` - 链表指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListBase_push_front_node_base

```c
bool XListBase_push_front_node_base(XListBase* list, XListBaseNode* node)
```

在链表头部插入已有节点。

**参数:**
- `list` - 链表指针
- `node` - 待插入的节点指针

**返回值:** 成功返回true，失败返回false

**注意:** 转移节点所有权

---

##### XListBase_push_back_base

```c
XListBaseNode* XListBase_push_back_base(XListBase* list, void* pvData)
```

在链表尾部插入元素(拷贝语义)。

**参数:**
- `list` - 链表指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListBase_Push_Back_Base

```c
#define XListBase_Push_Back_Base(list, Type, value) { Type t = value; XListBase_push_back_base(list, &t); }
```

类型安全尾部插入宏。

**参数:**
- `list` - 链表指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XListBase_push_back_move_base

```c
XListBaseNode* XListBase_push_back_move_base(XListBase* list, void* pvData)
```

在链表尾部插入元素(移动语义)。

**参数:**
- `list` - 链表指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListBase_push_back_node_base

```c
bool XListBase_push_back_node_base(XListBase* list, XListBaseNode* node)
```

在链表尾部插入已有节点。

**参数:**
- `list` - 链表指针
- `node` - 待插入的节点指针

**返回值:** 成功返回true，失败返回false

**注意:** 转移节点所有权

---

##### XListBase_insert_base

```c
bool XListBase_insert_base(XListBase* list, XListBaseNode* curNode, void* pvData)
```

在指定节点前插入元素(拷贝语义)。

**参数:**
- `list` - 链表指针
- `curNode` - 目标节点(新元素插入到此节点之前)
- `pvData` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XListBase_insert_move_base

```c
bool XListBase_insert_move_base(XListBase* list, XListBaseNode* curNode, void* pvData)
```

在指定节点前插入元素(移动语义)。

**参数:**
- `list` - 链表指针
- `curNode` - 目标节点
- `pvData` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XListBase_insert_array_base

```c
size_t XListBase_insert_array_base(XListBase* list, XListBaseNode* curNode, void* array, size_t count)
```

在指定节点前插入数组(拷贝语义)。

**参数:**
- `list` - 链表指针
- `curNode` - 目标节点
- `array` - 数组起始地址
- `count` - 数组元素数量

**返回值:** 实际插入的元素数量

---

##### XListBase_insert_array_move_base

```c
size_t XListBase_insert_array_move_base(XListBase* list, XListBaseNode* curNode, void* array, size_t count)
```

在指定节点前插入数组(移动语义)。

**参数:**
- `list` - 链表指针
- `curNode` - 目标节点
- `array` - 数组起始地址
- `count` - 数组元素数量

**返回值:** 实际插入的元素数量

---

#### 删除操作

##### XListBase_pop_front_base

```c
bool XListBase_pop_front_base(XListBase* list)
```

删除第一个元素。

**参数:**
- `list` - 链表指针

**返回值:** 成功返回true，链表为空返回false

---

##### XListBase_pop_back_base

```c
bool XListBase_pop_back_base(XListBase* list)
```

删除最后一个元素。

**参数:**
- `list` - 链表指针

**返回值:** 成功返回true，链表为空返回false

---

##### XListBase_erase_base

```c
void XListBase_erase_base(XListBase* list, const XListBase_iterator* it, XListBase_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `list` - 链表指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XListBase_remove_base

```c
bool XListBase_remove_base(XListBase* list, void* pvData)
```

删除首个匹配的元素。

**参数:**
- `list` - 链表指针
- `pvData` - 待删除元素的指针(用于匹配)

**返回值:** 成功返回true，未找到返回false

---

##### XListBase_Remove_Base

```c
#define XListBase_Remove_Base(list, Type, value) { Type t = value; XListBase_remove_base(list, &t); }
```

类型安全删除宏。

**参数:**
- `list` - 链表指针
- `Type` - 元素类型
- `value` - 待删除的元素值

**返回值:** 无

---

#### 元素访问

##### XListBase_front_base

```c
void* XListBase_front_base(XListBase* list)
```

获取第一个元素的指针。

**参数:**
- `list` - 链表指针

**返回值:** 成功返回首元素指针，链表为空返回NULL

---

##### XListBase_Front_Base

```c
#define XListBase_Front_Base(list, Type) (*(Type*)XListBase_front_base(list))
```

类型安全获取第一个元素的值。

**参数:**
- `list` - 链表指针
- `Type` - 元素类型

**返回值:** 返回首元素值(Type类型)

---

##### XListBase_back_base

```c
void* XListBase_back_base(XListBase* list)
```

获取最后一个元素的指针。

**参数:**
- `list` - 链表指针

**返回值:** 成功返回尾元素指针，链表为空返回NULL

---

##### XListBase_Back_Base

```c
#define XListBase_Back_Base(list, Type) (*(Type*)XListBase_back_base(list))
```

类型安全获取最后一个元素的值。

**参数:**
- `list` - 链表指针
- `Type` - 元素类型

**返回值:** 返回尾元素值(Type类型)

---

#### 查找操作

##### XListBase_find_base

```c
bool XListBase_find_base(const XListBase* list, const void* findVal, XListBase_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `list` - 链表指针
- `findVal` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XListBase_contains

```c
bool XListBase_contains(const XListBase* list, const void* value)
```

判断链表是否包含指定元素。

**参数:**
- `list` - 链表指针
- `value` - 待判断的元素指针

**返回值:** 包含返回true，否则返回false

---

#### 其他操作

##### XListBase_sort_base

```c
void XListBase_sort_base(XListBase* list, XSortOrder order)
```

对链表元素进行排序。

**参数:**
- `list` - 链表指针
- `order` - 排序顺序(XSort_Ascending升序，XSort_Descending降序)

**返回值:** 无

---

### XMapBase 映射基类

`XMapBase`是所有映射容器(如XMap、XHashMap)的基类。

#### 头文件

```c
#include "XMapBase.h"
```

#### 初始化

##### XMapBase_init

```c
void XMapBase_init(XMapBase* map, size_t keyTypeSize, size_t valTypeSize, XCompare compare, bool useCow)
```

初始化已分配内存的映射基类对象。

**参数:**
- `map` - 待初始化的XMapBase指针
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `compare` - 键的比较函数
- `useCow` - 是否启用COW

**返回值:** 无

---

#### 插入操作

##### XMapBase_insert_base

```c
bool XMapBase_insert_base(XMapBase* map, const void* key, const void* pvValue)
```

插入键值对(拷贝语义)。

**参数:**
- `map` - 映射指针
- `key` - 键指针
- `pvValue` - 值指针

**返回值:** 成功返回true，失败返回false

---

##### XMapBase_Insert_Base

```c
#define XMapBase_Insert_Base(map, keyType, key, valType, value) { keyType k = key; valType v = value; XMapBase_insert_base(map, &k, &v); }
```

类型安全插入宏。

**参数:**
- `map` - 映射指针
- `keyType` - 键类型
- `key` - 键值
- `valType` - 值类型
- `value` - 值

**返回值:** 无

---

##### XMapBase_insert_move_base

```c
bool XMapBase_insert_move_base(XMapBase* map, void* key, void* pvValue)
```

插入键值对(移动语义，键和值均移动)。

**参数:**
- `map` - 映射指针
- `key` - 键指针(所有权转移)
- `pvValue` - 值指针(所有权转移)

**返回值:** 成功返回true，失败返回false

---

##### XMapBase_insert_keyMove_base

```c
bool XMapBase_insert_keyMove_base(XMapBase* map, void* key, const void* pvValue)
```

插入键值对(移动语义，仅键移动)。

**参数:**
- `map` - 映射指针
- `key` - 键指针(所有权转移)
- `pvValue` - 值指针(拷贝)

**返回值:** 成功返回true，失败返回false

---

##### XMapBase_insert_valueMove_base

```c
bool XMapBase_insert_valueMove_base(XMapBase* map, const void* key, void* pvValue)
```

插入键值对(移动语义，仅值移动)。

**参数:**
- `map` - 映射指针
- `key` - 键指针(拷贝)
- `pvValue` - 值指针(所有权转移)

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XMapBase_erase_base

```c
void XMapBase_erase_base(XMapBase* map, const XMapBase_iterator* it, XMapBase_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `map` - 映射指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XMapBase_remove_base

```c
bool XMapBase_remove_base(XMapBase* map, const void* key)
```

通过键删除元素。

**参数:**
- `map` - 映射指针
- `key` - 键指针

**返回值:** 成功返回true，键不存在返回false

---

##### XMapBase_Remove_Base

```c
#define XMapBase_Remove_Base(map, keyType, key) { keyType k = key; XMapBase_remove_base(map, &k); }
```

类型安全删除宏。

**参数:**
- `map` - 映射指针
- `keyType` - 键类型
- `key` - 键值

**返回值:** 无

---

#### 元素访问

##### XMapBase_value_base

```c
void* XMapBase_value_base(XMapBase* map, const void* key)
```

通过键获取值的指针。

**参数:**
- `map` - 映射指针
- `key` - 键指针

**返回值:** 成功返回值指针，键不存在返回NULL

---

##### XMapBase_Value_Base

```c
#define XMapBase_Value_Base(map, key, valueType) (*(valueType*)XMapBase_value_base(map, &(key)))
```

类型安全获取值。

**参数:**
- `map` - 映射指针
- `key` - 键值
- `valueType` - 值类型

**返回值:** 返回值(valueType类型)

---

#### 查找操作

##### XMapBase_find_base

```c
bool XMapBase_find_base(const XMapBase* map, const void* key, XMapBase_iterator* it)
```

通过键查找元素并获取迭代器。

**参数:**
- `map` - 映射指针
- `key` - 键指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XMapBase_contains

```c
bool XMapBase_contains(const XMapBase* map, const void* key)
```

判断映射是否包含指定键。

**参数:**
- `map` - 映射指针
- `key` - 键指针

**返回值:** 包含返回true，否则返回false

---

#### 键值集合

##### XMapBase_keys_base

```c
XVector* XMapBase_keys_base(const XMapBase* map)
```

获取所有键的集合。

**参数:**
- `map` - 映射指针

**返回值:** 返回存储所有键的XVector指针，需用户自行释放

---

##### XMapBase_values_base

```c
XVector* XMapBase_values_base(const XMapBase* map)
```

获取所有值的集合。

**参数:**
- `map` - 映射指针

**返回值:** 返回存储所有值的XVector指针，需用户自行释放

---

### XSetBase 集合基类

`XSetBase`是所有集合容器(如XSet、XHashSet)的基类。

#### 头文件

```c
#include "XSetBase.h"
```

#### 初始化

##### XSetBase_init

```c
void XSetBase_init(XSetBase* set, size_t keyTypeSize, XCompare compare, bool useCow)
```

初始化已分配内存的集合基类对象。

**参数:**
- `set` - 待初始化的XSetBase指针
- `keyTypeSize` - 键的类型大小(字节)
- `compare` - 键的比较函数
- `useCow` - 是否启用COW

**返回值:** 无

---

#### 插入操作

##### XSetBase_insert_base

```c
bool XSetBase_insert_base(XSetBase* set, const void* pvKey)
```

插入元素(拷贝语义)。

**参数:**
- `set` - 集合指针
- `pvKey` - 待插入元素的指针

**返回值:** 成功返回true，元素已存在返回false

---

##### XSetBase_Insert_Base

```c
#define XSetBase_Insert_Base(set, keyType, key) { keyType k = key; XSetBase_insert_base(set, &k); }
```

类型安全插入宏。

**参数:**
- `set` - 集合指针
- `keyType` - 元素类型
- `key` - 元素值

**返回值:** 无

---

##### XSetBase_insert_move_base

```c
bool XSetBase_insert_move_base(XSetBase* set, const void* pvKey)
```

插入元素(移动语义)。

**参数:**
- `set` - 集合指针
- `pvKey` - 待插入元素的指针(所有权转移)

**返回值:** 成功返回true，元素已存在返回false

---

#### 删除操作

##### XSetBase_erase_base

```c
void XSetBase_erase_base(XSetBase* set, const XSetBase_iterator* it, XSetBase_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `set` - 集合指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XSetBase_remove_base

```c
bool XSetBase_remove_base(XSetBase* set, const void* pvKey)
```

通过键删除元素。

**参数:**
- `set` - 集合指针
- `pvKey` - 键指针

**返回值:** 成功返回true，键不存在返回false

---

##### XSetBase_Remove_Base

```c
#define XSetBase_Remove_Base(set, keyType, key) { keyType k = key; XSetBase_remove_base(set, &k); }
```

类型安全删除宏。

**参数:**
- `set` - 集合指针
- `keyType` - 元素类型
- `key` - 元素值

**返回值:** 无

---

#### 查找操作

##### XSetBase_find_base

```c
bool XSetBase_find_base(XSetBase* set, const void* pvKey, XSetBase_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `set` - 集合指针
- `pvKey` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XSetBase_contains

```c
bool XSetBase_contains(XSetBase* set, const void* pvKey)
```

判断集合是否包含指定元素。

**参数:**
- `set` - 集合指针
- `pvKey` - 元素指针

**返回值:** 包含返回true，否则返回false

---

#### 键集合

##### XSetBase_keys_base

```c
XVector* XSetBase_keys_base(const XSetBase* set)
```

获取所有元素的集合。

**参数:**
- `set` - 集合指针

**返回值:** 返回存储所有元素的XVector指针，需用户自行释放

---

### XStackBase 栈基类

`XStackBase`是所有栈容器(如XStack、XLockFreeStack)的基类。

#### 头文件

```c
#include "XStackBase.h"
```

#### 插入操作

##### XStackBase_push_base

```c
bool XStackBase_push_base(XStackBase* stack, void* pvData)
```

压栈操作(拷贝语义)。

**参数:**
- `stack` - 栈指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XStackBase_Push_Base

```c
#define XStackBase_Push_Base(stack, Type, value) { Type t = value; XStackBase_push_base(stack, &t); }
```

类型安全压栈宏。

**参数:**
- `stack` - 栈指针
- `Type` - 元素类型
- `value` - 元素值

**返回值:** 无

---

##### XStackBase_push_move_base

```c
bool XStackBase_push_move_base(XStackBase* stack, void* pvData)
```

压栈操作(移动语义)。

**参数:**
- `stack` - 栈指针
- `pvData` - 待插入元素的指针(所有权转移)

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XStackBase_pop_base

```c
void XStackBase_pop_base(XStackBase* stack)
```

弹栈操作。

**参数:**
- `stack` - 栈指针

**返回值:** 无

**注意:** 栈为空时无操作

---

#### 元素访问

##### XStackBase_top_base

```c
void* XStackBase_top_base(XStackBase* stack)
```

获取栈顶元素指针。

**参数:**
- `stack` - 栈指针

**返回值:** 成功返回栈顶元素指针，栈为空返回NULL

---

##### XStackBase_Top_Base

```c
#define XStackBase_Top_Base(stack, Type) (*(Type*)XStackBase_top_base(stack))
```

类型安全获取栈顶元素。

**参数:**
- `stack` - 栈指针
- `Type` - 元素类型

**返回值:** 返回栈顶元素值(Type类型)

---

##### XStackBase_receive_base

```c
bool XStackBase_receive_base(XStackBase* stack, void* pvBuffer)
```

获取栈顶元素并弹栈。

**参数:**
- `stack` - 栈指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，栈为空返回false

---

#### 状态查询

##### XStackBase_isFull_base

```c
bool XStackBase_isFull_base(XStackBase* stack)
```

判断栈是否已满。

**参数:**
- `stack` - 栈指针

**返回值:** 已满返回true，否则返回false

**注意:** 无界栈始终返回false

---

### XQueueBase 队列基类

`XQueueBase`是所有队列容器(如XQueue、XPriorityQueue、XLockFreeQueue)的基类。

#### 头文件

```c
#include "XQueueBase.h"
```

#### 插入操作

##### XQueueBase_push_base

```c
bool XQueueBase_push_base(XQueueBase* queue, void* pvData)
```

入队操作(拷贝语义)。

**参数:**
- `queue` - 队列指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XQueueBase_Push_Base

```c
#define XQueueBase_Push_Base(queue, Type, value) { Type t = value; XQueueBase_push_base(queue, &t); }
```

类型安全入队宏。

**参数:**
- `queue` - 队列指针
- `Type` - 元素类型
- `value` - 元素值

**返回值:** 无

---

##### XQueueBase_push_move_base

```c
bool XQueueBase_push_move_base(XQueueBase* queue, void* pvData)
```

入队操作(移动语义)。

**参数:**
- `queue` - 队列指针
- `pvData` - 待插入元素的指针(所有权转移)

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XQueueBase_pop_base

```c
void XQueueBase_pop_base(XQueueBase* queue)
```

出队操作。

**参数:**
- `queue` - 队列指针

**返回值:** 无

**注意:** 队列为空时无操作

---

#### 元素访问

##### XQueueBase_top_base

```c
void* XQueueBase_top_base(XQueueBase* queue)
```

获取队头元素指针。

**参数:**
- `queue` - 队列指针

**返回值:** 成功返回队头元素指针，队列为空返回NULL

---

##### XQueueBase_Top_Base

```c
#define XQueueBase_Top_Base(queue, Type) (*(Type*)XQueueBase_top_base(queue))
```

类型安全获取队头元素。

**参数:**
- `queue` - 队列指针
- `Type` - 元素类型

**返回值:** 返回队头元素值(Type类型)

---

##### XQueueBase_receive_base

```c
bool XQueueBase_receive_base(XQueueBase* queue, void* pvBuffer)
```

获取队头元素并出队。

**参数:**
- `queue` - 队列指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，队列为空返回false

---

#### 状态查询

##### XQueueBase_isFull_base

```c
bool XQueueBase_isFull_base(XQueueBase* queue)
```

判断队列是否已满。

**参数:**
- `queue` - 队列指针

**返回值:** 已满返回true，否则返回false

**注意:** 无界队列始终返回false

---

## 序列容器

### XVector 动态数组

动态数组容器，提供连续内存存储，支持O(1)随机访问。

#### 头文件

```c
#include "XVector.h"
```

#### 创建与销毁

##### XVector_create

```c
XVector* XVector_create(size_t typeSize)
```

创建一个新的XVector对象，默认启用COW机制。

**参数:**
- `typeSize` - 元素类型大小(字节)，必须大于0

**返回值:** 成功返回XVector指针，失败返回NULL

---

##### XVector_create_ex

```c
XVector* XVector_create_ex(size_t typeSize, bool useCow)
```

创建一个新的XVector对象，可指定是否启用COW。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用写时复制，true启用，false禁用

**返回值:** 成功返回XVector指针，失败返回NULL

---

##### XVector_create_copy

```c
XVector* XVector_create_copy(const XVector* other)
```

通过拷贝另一个XVector创建新对象。

**参数:**
- `other` - 源向量指针

**返回值:** 成功返回新XVector指针(深拷贝)，失败返回NULL

**注意:** 新向量与源向量完全独立，修改互不影响

---

##### XVector_create_move

```c
XVector* XVector_create_move(XVector* other)
```

通过移动另一个XVector的资源创建新对象。

**参数:**
- `other` - 源向量指针

**返回值:** 成功返回新XVector指针，失败返回NULL

**注意:** 移动后other变为空状态，不应再访问

---

##### XVector_Create

```c
#define XVector_Create(Type) XVector_create(sizeof(Type))
```

类型安全创建宏，自动推导类型大小。

**参数:**
- `Type` - 元素类型(如int、float、MyStruct)

**返回值:** 成功返回XVector指针，失败返回NULL

---

##### XVector_init

```c
void XVector_init(XVector* vec, size_t typeSize, bool useCow)
```

初始化已分配内存的XVector对象。

**参数:**
- `vec` - 待初始化的XVector指针(需提前分配内存)
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用COW

**返回值:** 无

**注意:** 用于栈上分配的向量，需配合XVector_deinit_base使用

---

##### XVector_delete_base

```c
void XVector_delete_base(XVector* vec)
```

销毁XVector对象并释放所有内存。

**参数:**
- `vec` - XVector指针，可为NULL

**返回值:** 无

**注意:** 释放向量本身及其所有元素，vec为NULL时不执行任何操作

---

#### 元素访问

##### XVector_at_base

```c
void* XVector_at_base(const XVector* vec, int64_t index)
```

获取指定位置的元素指针。

**参数:**
- `vec` - XVector指针
- `index` - 元素索引(从0开始)，支持负数索引(-1表示最后一个元素)

**返回值:** 成功返回元素指针，索引越界返回NULL

**注意:** 返回的指针在向量修改后可能失效

---

##### XVector_At_Base

```c
#define XVector_At_Base(vec, Type, index) (*((Type*)XVector_at_base(vec, index)))
```

类型安全获取指定位置的元素值。

**参数:**
- `vec` - XVector指针
- `Type` - 元素类型(如int)
- `index` - 元素索引(支持负数)

**返回值:** 返回元素值(Type类型)

**注意:** 需确保索引有效，否则可能崩溃

---

##### XVector_front_base

```c
void* XVector_front_base(const XVector* vec)
```

获取第一个元素的指针。

**参数:**
- `vec` - XVector指针

**返回值:** 成功返回首元素指针，向量为空返回NULL

---

##### XVector_Front_Base

```c
#define XVector_Front_Base(vec, Type) (*((Type*)XVector_front_base(vec)))
```

类型安全获取第一个元素的值。

**参数:**
- `vec` - XVector指针
- `Type` - 元素类型

**返回值:** 返回首元素值(Type类型)

**注意:** 需确保向量非空

---

##### XVector_back_base

```c
void* XVector_back_base(const XVector* vec)
```

获取最后一个元素的指针。

**参数:**
- `vec` - XVector指针

**返回值:** 成功返回尾元素指针，向量为空返回NULL

---

##### XVector_Back_Base

```c
#define XVector_Back_Base(vec, Type) (*((Type*)XVector_back_base(vec)))
```

类型安全获取最后一个元素的值。

**参数:**
- `vec` - XVector指针
- `Type` - 元素类型

**返回值:** 返回尾元素值(Type类型)

**注意:** 需确保向量非空

---

##### XVector_data

```c
void* XVector_data(XVector* vec)
```

获取底层数组的指针。

**参数:**
- `vec` - XVector指针

**返回值:** 返回底层数组首地址，向量为空返回NULL

**注意:** 可用于直接操作底层数据，需谨慎使用

---

#### 插入操作

##### XVector_push_front_base

```c
bool XVector_push_front_base(XVector* vec, void* pvValue)
```

在向量头部插入元素(拷贝语义)。

**参数:**
- `vec` - XVector指针
- `pvValue` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

**注意:** 原数据会依次后移，可能触发扩容

---

##### XVector_Push_Front_Base

```c
#define XVector_Push_Front_Base(vec, Type, value) { Type t = value; XVector_push_front_base(vec, &t); }
```

类型安全头部插入宏。

**参数:**
- `vec` - XVector指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XVector_push_front_move_base

```c
bool XVector_push_front_move_base(XVector* vec, void* pvValue)
```

在向量头部插入元素(移动语义)。

**参数:**
- `vec` - XVector指针
- `pvValue` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

**注意:** 移动后源数据可能失效

---

##### XVector_push_back_base

```c
bool XVector_push_back_base(XVector* vec, void* pvValue)
```

在向量尾部插入元素(拷贝语义)。

**参数:**
- `vec` - XVector指针
- `pvValue` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XVector_Push_Back_Base

```c
#define XVector_Push_Back_Base(vec, Type, value) { Type t = value; XVector_push_back_base(vec, &t); }
```

类型安全尾部插入宏。

**参数:**
- `vec` - XVector指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XVector_push_back_move_base

```c
bool XVector_push_back_move_base(XVector* vec, void* pvValue)
```

在向量尾部插入元素(移动语义)。

**参数:**
- `vec` - XVector指针
- `pvValue` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

**注意:** 移动后源数据可能失效

---

##### XVector_insert

```c
bool XVector_insert(XVector* vec, int64_t index, const void* pvValue)
```

在指定位置插入元素(拷贝语义)。

**参数:**
- `vec` - XVector指针
- `index` - 插入位置索引(支持负数)
- `pvValue` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

**注意:** index超出范围时会插入到头部或尾部

---

##### XVector_Insert

```c
#define XVector_Insert(vec, index, Type, value) { Type t = value; XVector_insert(vec, index, &t); }
```

类型安全指定位置插入宏。

**参数:**
- `vec` - XVector指针
- `index` - 插入位置索引
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XVector_insert_move

```c
bool XVector_insert_move(XVector* vec, int64_t index, const void* pvValue)
```

在指定位置插入元素(移动语义)。

**参数:**
- `vec` - XVector指针
- `index` - 插入位置索引
- `pvValue` - 待插入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XVector_insert_array_base

```c
bool XVector_insert_array_base(XVector* vec, int64_t index, const void* begin, size_t n)
```

在指定位置插入数组(拷贝语义)。

**参数:**
- `vec` - XVector指针
- `index` - 插入位置索引
- `begin` - 数组起始地址
- `n` - 数组元素数量

**返回值:** 成功返回true，失败返回false

---

##### XVector_insert_array_move_base

```c
bool XVector_insert_array_move_base(XVector* vec, int64_t index, const void* begin, size_t n)
```

在指定位置插入数组(移动语义)。

**参数:**
- `vec` - XVector指针
- `index` - 插入位置索引
- `begin` - 数组起始地址
- `n` - 数组元素数量

**返回值:** 成功返回true，失败返回false

---

##### XVector_append_array_base

```c
bool XVector_append_array_base(XVector* vec, const void* begin, size_t n)
```

在尾部追加数组(拷贝语义)。

**参数:**
- `vec` - XVector指针
- `begin` - 数组起始地址
- `n` - 数组元素数量

**返回值:** 成功返回true，失败返回false

---

##### XVector_append_array_move_base

```c
bool XVector_append_array_move_base(XVector* vec, const void* begin, size_t n)
```

在尾部追加数组(移动语义)。

**参数:**
- `vec` - XVector指针
- `begin` - 数组起始地址
- `n` - 数组元素数量

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XVector_pop_front_base

```c
void XVector_pop_front_base(XVector* vec)
```

删除第一个元素。

**参数:**
- `vec` - XVector指针

**返回值:** 无

**注意:** 向量为空时操作无效

---

##### XVector_pop_back_base

```c
void XVector_pop_back_base(XVector* vec)
```

删除最后一个元素。

**参数:**
- `vec` - XVector指针

**返回值:** 无

**注意:** 向量为空时操作无效

---

##### XVector_erase_base

```c
void XVector_erase_base(XVector* vec, const XVector_iterator* it, XVector_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `vec` - XVector指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器(可为NULL)

**返回值:** 无

---

##### XVector_remove_base

```c
void XVector_remove_base(XVector* vec, int64_t index, int64_t n)
```

删除指定范围的元素。

**参数:**
- `vec` - XVector指针
- `index` - 起始删除位置
- `n` - 要删除的元素数量(n<0表示删除从index到末尾的所有元素)

**返回值:** 无

---

##### XVector_removeAt_base

```c
#define XVector_removeAt_base(vec, index) XVector_remove_base(vec, index, 1)
```

删除指定位置的单个元素。

**参数:**
- `vec` - XVector指针
- `index` - 元素索引

**返回值:** 无

---

##### XVector_clear_base

```c
void XVector_clear_base(XVector* vec)
```

清空所有元素。

**参数:**
- `vec` - XVector指针

**返回值:** 无

**注意:** 保留容量，不释放内存

---

#### 查找操作

##### XVector_find_base

```c
bool XVector_find_base(const XVector* vec, const void* findVal, XVector_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `vec` - XVector指针
- `findVal` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XVector_contains

```c
bool XVector_contains(const XVector* vec, const void* value)
```

判断向量是否包含指定元素。

**参数:**
- `vec` - XVector指针
- `value` - 待判断的元素指针

**返回值:** 包含返回true，否则返回false

---

##### XVector_indexOf

```c
int64_t XVector_indexOf(const XVector* vec, const void* value, int64_t from)
```

从指定位置开始查找元素索引。

**参数:**
- `vec` - XVector指针
- `value` - 待查找的元素指针
- `from` - 起始查找位置(默认为0)

**返回值:** 找到返回索引，未找到返回-1

---

##### XVector_lastIndexOf

```c
int64_t XVector_lastIndexOf(const XVector* vec, const void* value, int64_t from)
```

反向查找元素索引。

**参数:**
- `vec` - XVector指针
- `value` - 待查找的元素指针
- `from` - 起始查找位置(默认为-1，表示从最后一个元素开始)

**返回值:** 找到返回索引，未找到返回-1

---

#### 子向量操作

##### XVector_first

```c
XVector* XVector_first(const XVector* vec, int64_t n)
```

获取前n个元素组成的新向量。

**参数:**
- `vec` - 源向量指针
- `n` - 要获取的元素数量

**返回值:** 成功返回新XVector指针，失败返回NULL

**注意:** 若n<=0返回空向量，n大于向量大小返回整个向量的拷贝

---

##### XVector_last

```c
XVector* XVector_last(const XVector* vec, int64_t n)
```

获取后n个元素组成的新向量。

**参数:**
- `vec` - 源向量指针
- `n` - 要获取的元素数量

**返回值:** 成功返回新XVector指针，失败返回NULL

---

##### XVector_mid

```c
XVector* XVector_mid(const XVector* vec, int64_t pos, int64_t length)
```

获取从指定位置开始的子向量。

**参数:**
- `vec` - 源向量指针
- `pos` - 起始位置索引(从0开始)
- `length` - 要获取的元素数量(-1表示到末尾)

**返回值:** 成功返回新XVector指针，失败返回NULL

---

#### 容量操作

##### XVector_resize_base

```c
bool XVector_resize_base(XVector* vec, size_t size)
```

调整向量大小。

**参数:**
- `vec` - XVector指针
- `size` - 新的大小

**返回值:** 成功返回true，失败返回false

**注意:** 新大小超过当前大小，新增元素初始化为0；小于当前大小，超出部分被删除

---

##### XVector_size_base

```c
size_t XVector_size_base(const XVector* vec)
```

获取元素数量。

**参数:**
- `vec` - XVector指针

**返回值:** 返回元素数量

---

##### XVector_capacity_base

```c
size_t XVector_capacity_base(const XVector* vec)
```

获取容量。

**参数:**
- `vec` - XVector指针

**返回值:** 返回当前容量

---

##### XVector_isEmpty_base

```c
bool XVector_isEmpty_base(const XVector* vec)
```

判断向量是否为空。

**参数:**
- `vec` - XVector指针

**返回值:** 为空返回true，否则返回false

---

#### 其他操作

##### XVector_sort_base

```c
void XVector_sort_base(XVector* vec, XSortOrder order)
```

对向量元素进行排序。

**参数:**
- `vec` - XVector指针
- `order` - 排序顺序(XSort_Ascending升序，XSort_Descending降序)

**返回值:** 无

---

##### XVector_replace

```c
bool XVector_replace(XVector* vec, int64_t index, void* pvValue)
```

替换指定位置的元素(拷贝语义)。

**参数:**
- `vec` - XVector指针
- `index` - 元素索引
- `pvValue` - 新元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XVector_replace_move

```c
bool XVector_replace_move(XVector* vec, int64_t index, void* pvValue)
```

替换指定位置的元素(移动语义)。

**参数:**
- `vec` - XVector指针
- `index` - 元素索引
- `pvValue` - 新元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XVector_rcopy_base

```c
void XVector_rcopy_base(XVector* dest, const XVector* src)
```

逆序拷贝向量。

**参数:**
- `dest` - 目标向量
- `src` - 源向量

**返回值:** 无

---

##### XVector_swap_base

```c
void XVector_swap_base(XVector* a, XVector* b)
```

交换两个向量的内容。

**参数:**
- `a` - 第一个向量
- `b` - 第二个向量

**返回值:** 无

---

#### 使用示例

```c
// 创建int向量
XVector* vec = XVector_Create(int);

// 添加元素
for (int i = 0; i < 10; i++) {
    XVector_Push_Back_Base(vec, int, i * 10);
}

// 访问元素
int val = XVector_At_Base(vec, int, 5);  // 50

// 查找元素
int key = 50;
XVector_iterator it;
if (XVector_find_base(vec, &key, &it)) {
    // 找到元素
}

// 获取子向量
XVector* sub = XVector_mid(vec, 2, 5);

// 销毁
XVector_delete_base(vec);
XVector_delete_base(sub);
```

---
### XByteArray 字节数组

字节数组容器，专门用于存储和操作uint8_t类型数据，支持编码转换和压缩解压。

#### 头文件

```c
#include "XByteArray.h"
```

#### 创建与销毁

##### XByteArray_create

```c
XByteArray* XByteArray_create()
```

创建一个空的字节数组，默认启用COW机制。

**参数:** 无

**返回值:** 成功返回XByteArray指针，失败返回NULL

---

##### XByteArray_create_ex

```c
XByteArray* XByteArray_create_ex(bool useCow)
```

创建一个空的字节数组，可指定是否启用COW。

**参数:**
- `useCow` - 是否启用写时复制

**返回值:** 成功返回XByteArray指针，失败返回NULL

---

##### XByteArray_create_with_data

```c
XByteArray* XByteArray_create_with_data(const char* data, size_t size)
```

创建包含指定数据的字节数组。

**参数:**
- `data` - 初始数据指针
- `size` - 数据大小(字节数)

**返回值:** 成功返回XByteArray指针，失败返回NULL

---

##### XByteArray_create_copy

```c
XByteArray* XByteArray_create_copy(const XByteArray* other)
```

通过拷贝另一个字节数组创建新对象。

**参数:**
- `other` - 源字节数组指针

**返回值:** 成功返回新XByteArray指针，失败返回NULL

---

##### XByteArray_create_move

```c
XByteArray* XByteArray_create_move(XByteArray* other)
```

通过移动另一个字节数组的资源创建新对象。

**参数:**
- `other` - 源字节数组指针

**返回值:** 成功返回新XByteArray指针，失败返回NULL

**注意:** 移动后other变为空状态

---

##### XByteArray_init

```c
void XByteArray_init(XByteArray* array, bool useCow)
```

初始化已分配内存的字节数组对象。

**参数:**
- `array` - 待初始化的XByteArray指针
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XByteArray_delete_base

```c
void XByteArray_delete_base(XByteArray* array)
```

销毁字节数组并释放所有内存。

**参数:**
- `array` - XByteArray指针，可为NULL

**返回值:** 无

---

#### 元素添加

##### XByteArray_push_front_base

```c
bool XByteArray_push_front_base(XByteArray* array, const uint8_t byte)
```

在数组头部添加一个字节。

**参数:**
- `array` - XByteArray指针
- `byte` - 待添加的字节

**返回值:** 成功返回true，失败返回false

---

##### XByteArray_push_back_base

```c
bool XByteArray_push_back_base(XByteArray* array, const uint8_t byte)
```

在数组尾部添加一个字节。

**参数:**
- `array` - XByteArray指针
- `byte` - 待添加的字节

**返回值:** 成功返回true，失败返回false

---

##### XByteArray_insert_base

```c
bool XByteArray_insert_base(XByteArray* array, int64_t index, const uint8_t byte)
```

在指定位置插入一个字节。

**参数:**
- `array` - XByteArray指针
- `index` - 插入位置索引(支持负数)
- `byte` - 待插入的字节

**返回值:** 成功返回true，失败返回false

---

##### XByteArray_inserts_base

```c
bool XByteArray_inserts_base(XByteArray* array, int64_t index, uint8_t byte, size_t n)
```

在指定位置插入n个相同字节。

**参数:**
- `array` - XByteArray指针
- `index` - 插入位置索引
- `byte` - 待插入的字节值
- `n` - 插入的字节数量

**返回值:** 成功返回true，失败返回false

---

##### XByteArray_append_utf8

```c
bool XByteArray_append_utf8(XByteArray* array, const char* utf8)
```

追加UTF8字符串到字节数组(不含终止符'\0')。

**参数:**
- `array` - XByteArray指针
- `utf8` - UTF8字符串指针

**返回值:** 成功返回true，失败返回false

---

#### 元素删除

##### XByteArray_pop_front_base

```c
void XByteArray_pop_front_base(XByteArray* array)
```

删除第一个字节。

**参数:**
- `array` - XByteArray指针

**返回值:** 无

---

##### XByteArray_pop_back_base

```c
void XByteArray_pop_back_base(XByteArray* array)
```

删除最后一个字节。

**参数:**
- `array` - XByteArray指针

**返回值:** 无

---

##### XByteArray_remove_base

```c
void XByteArray_remove_base(XByteArray* array, int64_t index, int64_t n)
```

删除指定范围的字节。

**参数:**
- `array` - XByteArray指针
- `index` - 起始位置
- `n` - 删除数量(n<0表示删除index之后的所有字节)

**返回值:** 无

---

##### XByteArray_clear_base

```c
void XByteArray_clear_base(XByteArray* array)
```

清空所有字节。

**参数:**
- `array` - XByteArray指针

**返回值:** 无

---

#### 元素访问

##### XByteArray_at_base

```c
void* XByteArray_at_base(const XByteArray* array, int64_t index)
```

获取指定位置字节的指针。

**参数:**
- `array` - XByteArray指针
- `index` - 字节索引(支持负数)

**返回值:** 成功返回字节指针，索引越界返回NULL

---

##### XByteArray_At_Base

```c
#define XByteArray_At_Base(array, index) XVector_At_Base(array, uint8_t, index)
```

获取指定位置的字节值。

**参数:**
- `array` - XByteArray指针
- `index` - 字节索引

**返回值:** 返回uint8_t类型的字节值

---

##### XByteArray_front_base

```c
void* XByteArray_front_base(const XByteArray* array)
```

获取第一个字节的指针。

**参数:**
- `array` - XByteArray指针

**返回值:** 成功返回首字节指针，数组为空返回NULL

---

##### XByteArray_Front_Base

```c
#define XByteArray_Front_Base(array) XVector_Front_Base(array, uint8_t)
```

获取第一个字节的值。

**参数:**
- `array` - XByteArray指针

**返回值:** 返回uint8_t类型的字节值

---

##### XByteArray_back_base

```c
void* XByteArray_back_base(const XByteArray* array)
```

获取最后一个字节的指针。

**参数:**
- `array` - XByteArray指针

**返回值:** 成功返回尾字节指针，数组为空返回NULL

---

##### XByteArray_Back_Base

```c
#define XByteArray_Back_Base(array) XVector_Back_Base(array, uint8_t)
```

获取最后一个字节的值。

**参数:**
- `array` - XByteArray指针

**返回值:** 返回uint8_t类型的字节值

---

#### 查找操作

##### XByteArray_find_base

```c
bool XByteArray_find_base(const XByteArray* array, const uint8_t findVal, XByteArray_iterator* it)
```

查找指定字节并获取迭代器。

**参数:**
- `array` - XByteArray指针
- `findVal` - 待查找的字节
- `it` - 输出参数，存储找到的位置的迭代器

**返回值:** 找到返回true，未找到返回false

---

#### 容量操作

##### XByteArray_size_base

```c
size_t XByteArray_size_base(const XByteArray* array)
```

获取字节数量。

**参数:**
- `array` - XByteArray指针

**返回值:** 返回字节数量

---

##### XByteArray_isEmpty_base

```c
bool XByteArray_isEmpty_base(const XByteArray* array)
```

判断数组是否为空。

**参数:**
- `array` - XByteArray指针

**返回值:** 为空返回true，否则返回false

---

##### XByteArray_capacity_base

```c
size_t XByteArray_capacity_base(const XByteArray* array)
```

获取容量。

**参数:**
- `array` - XByteArray指针

**返回值:** 返回当前容量

---

##### XByteArray_resize_base

```c
bool XByteArray_resize_base(XByteArray* array, size_t size)
```

调整数组大小。

**参数:**
- `array` - XByteArray指针
- `size` - 新的大小

**返回值:** 成功返回true，失败返回false

---

#### 编码与转换

##### XByteArray_to16HexUtf8

```c
XByteArray* XByteArray_to16HexUtf8(XByteArray* array)
```

将字节数组转换为16进制UTF8字符串。

**参数:**
- `array` - XByteArray指针

**返回值:** 成功返回包含16进制字符串的XByteArray，失败返回NULL

---

##### XByteArray_to16HexString

```c
XString* XByteArray_to16HexString(XByteArray* array)
```

将字节数组转换为16进制字符串(XString)。

**参数:**
- `array` - XByteArray指针

**返回值:** 成功返回包含16进制字符串的XString，失败返回NULL

---

##### XByteArray_toBase64

```c
XByteArray* XByteArray_toBase64(XByteArray* array)
```

将字节数组转换为Base64编码。

**参数:**
- `array` - XByteArray指针

**返回值:** 成功返回Base64编码的XByteArray，失败返回NULL

---

##### XByteArray_fromBase64

```c
XByteArray* XByteArray_fromBase64(XByteArray* base64)
```

将Base64编码的字节数组解码为原始数据。

**参数:**
- `base64` - 包含Base64编码数据的XByteArray

**返回值:** 成功返回解码后的XByteArray，失败返回NULL

---

#### 压缩与解压

##### XByteArray_toCompress

```c
XByteArray* XByteArray_toCompress(XByteArray* sData)
```

对字节数组进行压缩。

**参数:**
- `sData` - 待压缩的字节数组

**返回值:** 成功返回压缩后的XByteArray，失败返回NULL

---

##### XByteArray_toDecompress

```c
XByteArray* XByteArray_toDecompress(XByteArray* sData)
```

对字节数组进行解压。

**参数:**
- `sData` - 待解压的字节数组

**返回值:** 成功返回解压后的XByteArray，失败返回NULL

---

#### 其他操作

##### XByteArray_compare

```c
int32_t XByteArray_compare(const XByteArray* lhs, const XByteArray* rhs)
```

比较两个字节数组。

**参数:**
- `lhs` - 左操作数
- `rhs` - 右操作数

**返回值:** lhs<rhs返回XCompare_Less(-1)，lhs>rhs返回XCompare_Greater(1)，相等返回XCompare_Equality(0)

---

##### XByteArray_sort_base

```c
void XByteArray_sort_base(XByteArray* array, XSortOrder order)
```

对字节数组进行排序。

**参数:**
- `array` - XByteArray指针
- `order` - 排序顺序

**返回值:** 无

---

##### XByteArray_swap_base

```c
void XByteArray_swap_base(XByteArray* a, XByteArray* b)
```

交换两个字节数组的内容。

**参数:**
- `a` - 第一个数组
- `b` - 第二个数组

**返回值:** 无

---

### XListSLinked 单向链表

单向链表容器，内存占用更小，适合不需要反向遍历的场景。

#### 头文件

```c
#include "XListSLinked.h"
```

#### 创建与销毁

##### XListSLinked_create

```c
XListSLinked* XListSLinked_create(size_t typeSize)
```

创建一个新的单向链表对象，默认启用COW机制。

**参数:**
- `typeSize` - 元素类型大小(字节)，必须大于0

**返回值:** 成功返回XListSLinked指针，失败返回NULL

---

##### XListSLinked_create_ex

```c
XListSLinked* XListSLinked_create_ex(size_t typeSize, bool useCow)
```

创建一个新的单向链表对象，可指定是否启用COW。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用写时复制

**返回值:** 成功返回XListSLinked指针，失败返回NULL

---

##### XListSLinked_Create

```c
#define XListSLinked_Create(Type) XListSLinked_create(sizeof(Type))
```

类型安全创建宏。

**参数:**
- `Type` - 元素类型

**返回值:** 成功返回XListSLinked指针，失败返回NULL

---

##### XListSLinked_init

```c
void XListSLinked_init(XListSLinked* list, size_t typeSize, bool useCow)
```

初始化已分配内存的单向链表对象。

**参数:**
- `list` - 待初始化的XListSLinked指针
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XListSLinked_delete_base

```c
void XListSLinked_delete_base(XListSLinked* list)
```

销毁单向链表并释放所有内存。

**参数:**
- `list` - XListSLinked指针，可为NULL

**返回值:** 无

---

#### 元素访问

##### XListSLinked_front_base

```c
void* XListSLinked_front_base(XListSLinked* list)
```

获取第一个元素的指针。

**参数:**
- `list` - XListSLinked指针

**返回值:** 成功返回首元素指针，链表为空返回NULL

---

##### XListSLinked_Front_Base

```c
#define XListSLinked_Front_Base(list, Type) (*(Type*)XListSLinked_front_base(list))
```

类型安全获取第一个元素的值。

**参数:**
- `list` - XListSLinked指针
- `Type` - 元素类型

**返回值:** 返回首元素值(Type类型)

---

##### XListSLinked_back_base

```c
void* XListSLinked_back_base(XListSLinked* list)
```

获取最后一个元素的指针。

**参数:**
- `list` - XListSLinked指针

**返回值:** 成功返回尾元素指针，链表为空返回NULL

---

##### XListSLinked_Back_Base

```c
#define XListSLinked_Back_Base(list, Type) (*(Type*)XListSLinked_back_base(list))
```

类型安全获取最后一个元素的值。

**参数:**
- `list` - XListSLinked指针
- `Type` - 元素类型

**返回值:** 返回尾元素值(Type类型)

---

#### 插入操作

##### XListSLinked_push_front_base

```c
XListSNode* XListSLinked_push_front_base(XListSLinked* list, void* pvData)
```

在链表头部插入元素(拷贝语义)。

**参数:**
- `list` - XListSLinked指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListSLinked_Push_Front_Base

```c
#define XListSLinked_Push_Front_Base(list, Type, value) { Type t = value; XListSLinked_push_front_base(list, &t); }
```

类型安全头部插入宏。

**参数:**
- `list` - XListSLinked指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XListSLinked_push_back_base

```c
XListSNode* XListSLinked_push_back_base(XListSLinked* list, void* pvData)
```

在链表尾部插入元素(拷贝语义)。

**参数:**
- `list` - XListSLinked指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListSLinked_Push_Back_Base

```c
#define XListSLinked_Push_Back_Base(list, Type, value) { Type t = value; XListSLinked_push_back_base(list, &t); }
```

类型安全尾部插入宏。

**参数:**
- `list` - XListSLinked指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

#### 删除操作

##### XListSLinked_pop_front_base

```c
bool XListSLinked_pop_front_base(XListSLinked* list)
```

删除第一个元素。

**参数:**
- `list` - XListSLinked指针

**返回值:** 成功返回true，链表为空返回false

---

##### XListSLinked_pop_back_base

```c
bool XListSLinked_pop_back_base(XListSLinked* list)
```

删除最后一个元素。

**参数:**
- `list` - XListSLinked指针

**返回值:** 成功返回true，链表为空返回false

**注意:** 单向链表删除尾节点需要O(n)时间

---

##### XListSLinked_erase_base

```c
void XListSLinked_erase_base(XListSLinked* list, const XListSLinked_iterator* it, XListSLinked_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `list` - XListSLinked指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XListSLinked_clear_base

```c
void XListSLinked_clear_base(XListSLinked* list)
```

清空所有元素。

**参数:**
- `list` - XListSLinked指针

**返回值:** 无

---

#### 查找操作

##### XListSLinked_find_base

```c
bool XListSLinked_find_base(const XListSLinked* list, const void* findVal, XListSLinked_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `list` - XListSLinked指针
- `findVal` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XListSLinked_contains_base

```c
bool XListSLinked_contains_base(const XListSLinked* list, const void* value)
```

判断链表是否包含指定元素。

**参数:**
- `list` - XListSLinked指针
- `value` - 待判断的元素指针

**返回值:** 包含返回true，否则返回false

---

#### 容量操作

##### XListSLinked_size_base

```c
size_t XListSLinked_size_base(const XListSLinked* list)
```

获取元素数量。

**参数:**
- `list` - XListSLinked指针

**返回值:** 返回元素数量

---

##### XListSLinked_isEmpty_base

```c
bool XListSLinked_isEmpty_base(const XListSLinked* list)
```

判断链表是否为空。

**参数:**
- `list` - XListSLinked指针

**返回值:** 为空返回true，否则返回false

---

#### 其他操作

##### XListSLinked_sort_base

```c
void XListSLinked_sort_base(XListSLinked* list, XSortOrder order)
```

对链表元素进行排序。

**参数:**
- `list` - XListSLinked指针
- `order` - 排序顺序(XSort_Ascending升序，XSort_Descending降序)

**返回值:** 无

---
### XListDLinked 双向链表

双向循环链表容器，支持高效的任意位置插入和删除操作。

#### 头文件

```c
#include "XListDLinked.h"
```

#### 创建与销毁

##### XListDLinked_create

```c
XListDLinked* XListDLinked_create(size_t typeSize)
```

创建一个新的双向链表对象，默认启用COW机制。

**参数:**
- `typeSize` - 元素类型大小(字节)，必须大于0

**返回值:** 成功返回XListDLinked指针，失败返回NULL

---

##### XListDLinked_create_ex

```c
XListDLinked* XListDLinked_create_ex(size_t typeSize, bool useCow)
```

创建一个新的双向链表对象，可指定是否启用COW。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用写时复制，true启用，false禁用

**返回值:** 成功返回XListDLinked指针，失败返回NULL

---

##### XListDLinked_create_copy

```c
XListDLinked* XListDLinked_create_copy(const XListDLinked* other)
```

通过拷贝另一个双向链表创建新对象。

**参数:**
- `other` - 源链表指针

**返回值:** 成功返回新XListDLinked指针(深拷贝)，失败返回NULL

---

##### XListDLinked_create_move

```c
XListDLinked* XListDLinked_create_move(XListDLinked* other)
```

通过移动另一个双向链表的资源创建新对象。

**参数:**
- `other` - 源链表指针

**返回值:** 成功返回新XListDLinked指针，失败返回NULL

**注意:** 移动后other变为空状态

---

##### XListDLinked_Create

```c
#define XListDLinked_Create(Type) XListDLinked_create(sizeof(Type))
```

类型安全创建宏。

**参数:**
- `Type` - 元素类型

**返回值:** 成功返回XListDLinked指针，失败返回NULL

---

##### XListDLinked_init

```c
void XListDLinked_init(XListDLinked* list, size_t typeSize, bool useCow)
```

初始化已分配内存的双向链表对象。

**参数:**
- `list` - 待初始化的XListDLinked指针
- `typeSize` - 元素类型大小(字节)
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XListDLinked_delete_base

```c
void XListDLinked_delete_base(XListDLinked* list)
```

销毁双向链表并释放所有内存。

**参数:**
- `list` - XListDLinked指针，可为NULL

**返回值:** 无

---

#### 元素访问

##### XListDLinked_front_base

```c
void* XListDLinked_front_base(XListDLinked* list)
```

获取第一个元素的指针。

**参数:**
- `list` - XListDLinked指针

**返回值:** 成功返回首元素指针，链表为空返回NULL

---

##### XListDLinked_Front_Base

```c
#define XListDLinked_Front_Base(list, Type) (*(Type*)XListDLinked_front_base(list))
```

类型安全获取第一个元素的值。

**参数:**
- `list` - XListDLinked指针
- `Type` - 元素类型

**返回值:** 返回首元素值(Type类型)

---

##### XListDLinked_back_base

```c
void* XListDLinked_back_base(XListDLinked* list)
```

获取最后一个元素的指针。

**参数:**
- `list` - XListDLinked指针

**返回值:** 成功返回尾元素指针，链表为空返回NULL

---

##### XListDLinked_Back_Base

```c
#define XListDLinked_Back_Base(list, Type) (*(Type*)XListDLinked_back_base(list))
```

类型安全获取最后一个元素的值。

**参数:**
- `list` - XListDLinked指针
- `Type` - 元素类型

**返回值:** 返回尾元素值(Type类型)

---

#### 插入操作

##### XListDLinked_push_front_base

```c
XListDLinkedNode* XListDLinked_push_front_base(XListDLinked* list, void* pvData)
```

在链表头部插入元素(拷贝语义)。

**参数:**
- `list` - XListDLinked指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListDLinked_Push_Front_Base

```c
#define XListDLinked_Push_Front_Base(list, Type, value) { Type t = value; XListDLinked_push_front_base(list, &t); }
```

类型安全头部插入宏。

**参数:**
- `list` - XListDLinked指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

##### XListDLinked_push_back_base

```c
XListDLinkedNode* XListDLinked_push_back_base(XListDLinked* list, void* pvData)
```

在链表尾部插入元素(拷贝语义)。

**参数:**
- `list` - XListDLinked指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XListDLinked_Push_Back_Base

```c
#define XListDLinked_Push_Back_Base(list, Type, value) { Type t = value; XListDLinked_push_back_base(list, &t); }
```

类型安全尾部插入宏。

**参数:**
- `list` - XListDLinked指针
- `Type` - 元素类型
- `value` - 待插入的元素值

**返回值:** 无

---

#### 删除操作

##### XListDLinked_pop_front_base

```c
bool XListDLinked_pop_front_base(XListDLinked* list)
```

删除第一个元素。

**参数:**
- `list` - XListDLinked指针

**返回值:** 成功返回true，链表为空返回false

---

##### XListDLinked_pop_back_base

```c
bool XListDLinked_pop_back_base(XListDLinked* list)
```

删除最后一个元素。

**参数:**
- `list` - XListDLinked指针

**返回值:** 成功返回true，链表为空返回false

---

##### XListDLinked_erase_base

```c
void XListDLinked_erase_base(XListDLinked* list, const XListDLinked_iterator* it, XListDLinked_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `list` - XListDLinked指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XListDLinked_clear_base

```c
void XListDLinked_clear_base(XListDLinked* list)
```

清空所有元素。

**参数:**
- `list` - XListDLinked指针

**返回值:** 无

---

#### 查找操作

##### XListDLinked_find_base

```c
bool XListDLinked_find_base(const XListDLinked* list, const void* findVal, XListDLinked_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `list` - XListDLinked指针
- `findVal` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XListDLinked_contains

```c
bool XListDLinked_contains(const XListDLinked* list, const void* value)
```

判断链表是否包含指定元素。

**参数:**
- `list` - XListDLinked指针
- `value` - 待判断的元素指针

**返回值:** 包含返回true，否则返回false

---

#### 容量操作

##### XListDLinked_size_base

```c
size_t XListDLinked_size_base(const XListDLinked* list)
```

获取元素数量。

**参数:**
- `list` - XListDLinked指针

**返回值:** 返回元素数量

---

##### XListDLinked_isEmpty_base

```c
bool XListDLinked_isEmpty_base(const XListDLinked* list)
```

判断链表是否为空。

**参数:**
- `list` - XListDLinked指针

**返回值:** 为空返回true，否则返回false

---

#### 其他操作

##### XListDLinked_sort_base

```c
void XListDLinked_sort_base(XListDLinked* list, XSortOrder order)
```

对链表元素进行排序。

**参数:**
- `list` - XListDLinked指针
- `order` - 排序顺序(XSort_Ascending升序，XSort_Descending降序)

**返回值:** 无

---

##### XListDLinked_swap_base

```c
void XListDLinked_swap_base(XListDLinked* a, XListDLinked* b)
```

交换两个链表的内容。

**参数:**
- `a` - 第一个链表
- `b` - 第二个链表

**返回值:** 无

---

### XLockFreeList 无锁链表

无锁单向链表容器，使用原子操作实现线程安全，适用于多线程环境。

#### 头文件

```c
#include "XLockFreeList.h"
```

#### 特点

- 基于CAS(Compare-And-Swap)原子操作实现
- 支持多生产者多消费者(MPMC)场景
- 高并发性能，无需锁机制
- 适合高频插入删除操作

#### 创建与销毁

##### XLockFreeList_create

```c
XLockFreeList* XLockFreeList_create(size_t typeSize)
```

创建一个无锁链表对象。

**参数:**
- `typeSize` - 元素类型大小(字节)

**返回值:** 成功返回XLockFreeList指针，失败返回NULL

---

##### XLockFreeList_Create

```c
#define XLockFreeList_Create(Type) XLockFreeList_create(sizeof(Type))
```

类型安全创建宏。

---

##### XLockFreeList_init

```c
void XLockFreeList_init(XLockFreeList* list, size_t typeSize)
```

初始化已分配内存的无锁链表对象。

**参数:**
- `list` - 待初始化的XLockFreeList指针
- `typeSize` - 元素类型大小(字节)

**返回值:** 无

---

##### XLockFreeList_delete_base

```c
void XLockFreeList_delete_base(XLockFreeList* list)
```

销毁无锁链表并释放所有内存。

**参数:**
- `list` - XLockFreeList指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XLockFreeList_push_front_base

```c
XLockFreeListNode* XLockFreeList_push_front_base(XLockFreeList* list, void* pvData)
```

在链表头部插入元素(拷贝语义)，线程安全。

**参数:**
- `list` - XLockFreeList指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XLockFreeList_Push_Front_Base

```c
#define XLockFreeList_Push_Front_Base(list, Type, value) { Type t = value; XLockFreeList_push_front_base(list, &t); }
```

类型安全头部插入宏。

---

##### XLockFreeList_push_back_base

```c
XLockFreeListNode* XLockFreeList_push_back_base(XLockFreeList* list, void* pvData)
```

在链表尾部插入元素(拷贝语义)，线程安全。

**参数:**
- `list` - XLockFreeList指针
- `pvData` - 待插入元素的指针

**返回值:** 成功返回新节点指针，失败返回NULL

---

##### XLockFreeList_Push_Back_Base

```c
#define XLockFreeList_Push_Back_Base(list, Type, value) { Type t = value; XLockFreeList_push_back_base(list, &t); }
```

类型安全尾部插入宏。

---

#### 删除操作

##### XLockFreeList_pop_and_copy_front

```c
bool XLockFreeList_pop_and_copy_front(XLockFreeList* list, void* pvOutData)
```

原子地删除链表头部元素，并将其数据拷贝到指定位置(线程安全，多消费者安全)。

**参数:**
- `list` - XLockFreeList指针
- `pvOutData` - 接收数据的目标地址

**返回值:** 成功返回true，链表为空返回false

---

##### XLockFreeList_pop_and_move_front

```c
bool XLockFreeList_pop_and_move_front(XLockFreeList* list, void* pvOutData)
```

原子地删除链表头部元素，并将其数据移动到指定位置(线程安全，多消费者安全)。

**参数:**
- `list` - XLockFreeList指针
- `pvOutData` - 接收数据的目标地址

**返回值:** 成功返回true，链表为空返回false

---

##### XLockFreeList_pop_front_base

```c
bool XLockFreeList_pop_front_base(XLockFreeList* list)
```

删除第一个元素。

**参数:**
- `list` - XLockFreeList指针

**返回值:** 成功返回true，链表为空返回false

---

##### XLockFreeList_clear_base

```c
void XLockFreeList_clear_base(XLockFreeList* list)
```

清空所有元素。

**参数:**
- `list` - XLockFreeList指针

**返回值:** 无

---

#### 元素访问

##### XLockFreeList_front_base

```c
void* XLockFreeList_front_base(XLockFreeList* list)
```

获取第一个元素的指针。

**参数:**
- `list` - XLockFreeList指针

**返回值:** 成功返回首元素指针，链表为空返回NULL

---

##### XLockFreeList_Front_Base

```c
#define XLockFreeList_Front_Base(list, Type) (*(Type*)XLockFreeList_front_base(list))
```

类型安全获取第一个元素的值。

---

##### XLockFreeList_back_base

```c
void* XLockFreeList_back_base(XLockFreeList* list)
```

获取最后一个元素的指针。

**参数:**
- `list` - XLockFreeList指针

**返回值:** 成功返回尾元素指针，链表为空返回NULL

---

##### XLockFreeList_Back_Base

```c
#define XLockFreeList_Back_Base(list, Type) (*(Type*)XLockFreeList_back_base(list))
```

类型安全获取最后一个元素的值。

---

#### 查找操作

##### XLockFreeList_find_base

```c
bool XLockFreeList_find_base(const XLockFreeList* list, const void* findVal, XLockFreeList_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `list` - XLockFreeList指针
- `findVal` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

#### 容量操作

##### XLockFreeList_size_base

```c
size_t XLockFreeList_size_base(const XLockFreeList* list)
```

获取元素数量。

**参数:**
- `list` - XLockFreeList指针

**返回值:** 返回元素数量

---

##### XLockFreeList_isEmpty_base

```c
bool XLockFreeList_isEmpty_base(const XLockFreeList* list)
```

判断链表是否为空。

**参数:**
- `list` - XLockFreeList指针

**返回值:** 为空返回true，否则返回false

---

### XString 字符串

Unicode字符串容器，内部以UTF-16编码存储，支持多种编码转换。

#### 头文件

```c
#include "XString.h"
```

#### 创建与销毁

##### XString_create

```c
XString* XString_create()
```

创建一个空的字符串对象。

**参数:** 无

**返回值:** 成功返回XString指针，失败返回NULL

---

##### XString_create_copy

```c
XString* XString_create_copy(const XString* other)
```

通过拷贝另一个字符串创建新对象。

**参数:**
- `other` - 源字符串指针

**返回值:** 成功返回新XString指针，失败返回NULL

---

##### XString_create_move

```c
XString* XString_create_move(XString* other)
```

通过移动另一个字符串的资源创建新对象。

**参数:**
- `other` - 源字符串指针

**返回值:** 成功返回新XString指针，失败返回NULL

---

##### XString_create_utf8

```c
XString* XString_create_utf8(const char* utf8_str)
```

从UTF-8字符串创建XString对象。

**参数:**
- `utf8_str` - UTF-8字符串指针

**返回值:** 成功返回XString指针，失败返回NULL

---

##### XString_create_gbk

```c
XString* XString_create_gbk(const char* gbk_str)
```

从GBK字符串创建XString对象。

**参数:**
- `gbk_str` - GBK字符串指针

**返回值:** 成功返回XString指针，失败返回NULL

---

##### XString_init

```c
void XString_init(XString* str)
```

初始化已分配内存的字符串对象。

**参数:**
- `str` - 待初始化的XString指针

**返回值:** 无

---

##### XString_delete_base

```c
void XString_delete_base(XString* str)
```

销毁字符串并释放所有内存。

**参数:**
- `str` - XString指针，可为NULL

**返回值:** 无

---

#### 元素访问

##### XString_at

```c
XChar XString_at(const XString* str, size_t index)
```

获取指定位置的XChar字符。

**参数:**
- `str` - XString指针
- `index` - 字符索引

**返回值:** 成功返回XChar字符，失败返回空字符

---

##### XString_front

```c
XChar XString_front(const XString* str)
```

获取第一个字符。

**参数:**
- `str` - XString指针

**返回值:** 返回首字符，字符串为空返回空字符

---

##### XString_back

```c
XChar XString_back(const XString* str)
```

获取最后一个字符。

**参数:**
- `str` - XString指针

**返回值:** 返回尾字符，字符串为空返回空字符

---

##### XString_unicode

```c
const XChar* XString_unicode(const XString* str)
```

获取内部存储的Unicode字符数组。

**参数:**
- `str` - XString指针

**返回值:** 返回常量XChar数组指针

---

#### 字符串修改

##### XString_append

```c
bool XString_append(XString* str, const XString* app_str)
```

追加字符串到末尾。

**参数:**
- `str` - XString指针
- `app_str` - 待追加的字符串

**返回值:** 成功返回true，失败返回false

---

##### XString_append_utf8

```c
bool XString_append_utf8(XString* str, const char* utf8_str)
```

追加UTF-8字符串到末尾。

**参数:**
- `str` - XString指针
- `utf8_str` - UTF-8字符串

**返回值:** 成功返回true，失败返回false

---

##### XString_assign_utf8

```c
bool XString_assign_utf8(XString* str, const char* utf8_str)
```

赋值UTF-8字符串。

**参数:**
- `str` - XString指针
- `utf8_str` - UTF-8字符串

**返回值:** 成功返回true，失败返回false

---

##### XString_prepend

```c
bool XString_prepend(XString* str, const XString* pre_str)
```

在字符串开头前置添加字符串。

**参数:**
- `str` - XString指针
- `pre_str` - 待前置的字符串

**返回值:** 成功返回true，失败返回false

---

##### XString_insert

```c
bool XString_insert(XString* str, size_t pos, const XString* in_str)
```

在指定位置插入字符串。

**参数:**
- `str` - XString指针
- `pos` - 插入位置
- `in_str` - 待插入的字符串

**返回值:** 成功返回true，失败返回false

---

##### XString_remove_base

```c
bool XString_remove_base(XString* str, size_t pos, size_t len)
```

移除指定范围的字符。

**参数:**
- `str` - XString指针
- `pos` - 起始位置
- `len` - 移除的字符数

**返回值:** 成功返回true，失败返回false

---

##### XString_replace

```c
bool XString_replace(XString* str, const XString* before, const XString* after, XCharCaseSensitivity cs)
```

替换字符串中的子串。

**参数:**
- `str` - XString指针
- `before` - 待替换的子串
- `after` - 替换后的子串
- `cs` - 大小写敏感性

**返回值:** 成功返回true，失败返回false

---

##### XString_push_back_base

```c
bool XString_push_back_base(XString* str, XChar ch)
```

在字符串末尾添加字符。

**参数:**
- `str` - XString指针
- `ch` - 待添加的XChar字符

**返回值:** 成功返回true，失败返回false

---

##### XString_pop_back_base

```c
bool XString_pop_back_base(XString* str)
```

删除字符串末尾的字符。

**参数:**
- `str` - XString指针

**返回值:** 成功返回true，字符串为空返回false

---

#### 查找操作

##### XString_index_of

```c
int64_t XString_index_of(const XString* str, const XString* substr, size_t from, XCharCaseSensitivity cs)
```

查找子串首次出现的位置。

**参数:**
- `str` - XString指针
- `substr` - 待查找的子串
- `from` - 起始查找位置
- `cs` - 大小写敏感性

**返回值:** 成功返回子串起始索引，未找到返回-1

---

##### XString_contains

```c
bool XString_contains(const XString* str, const XString* substr, XCharCaseSensitivity cs)
```

检查字符串是否包含指定子串。

**参数:**
- `str` - XString指针
- `substr` - 待查找的子串
- `cs` - 大小写敏感性

**返回值:** 包含返回true，否则返回false

---

#### 比较操作

##### XString_compare

```c
int32_t XString_compare(const XString* str1, const XString* str2)
```

比较两个字符串(字典序)。

**参数:**
- `str1` - 第一个字符串
- `str2` - 第二个字符串

**返回值:** 小于返回-1，等于返回0，大于返回1

---

##### XString_equals

```c
bool XString_equals(const XString* str1, const XString* str2, XCharCaseSensitivity cs)
```

判断两个字符串是否相等。

**参数:**
- `str1` - 第一个字符串
- `str2` - 第二个字符串
- `cs` - 大小写敏感性

**返回值:** 相等返回true，否则返回false

---

##### XString_starts_with

```c
bool XString_starts_with(const XString* str, const XString* prefix, XCharCaseSensitivity cs)
```

判断字符串是否以指定前缀开头。

**参数:**
- `str` - XString指针
- `prefix` - 前缀字符串
- `cs` - 大小写敏感性

**返回值:** 是返回true，否则返回false

---

##### XString_ends_with

```c
bool XString_ends_with(const XString* str, const XString* suffix, XCharCaseSensitivity cs)
```

判断字符串是否以指定后缀结尾。

**参数:**
- `str` - XString指针
- `suffix` - 后缀字符串
- `cs` - 大小写敏感性

**返回值:** 是返回true，否则返回false

---

#### 编码转换

##### XString_toUtf8

```c
const char* XString_toUtf8(const XString* str)
```

转换为UTF-8编码字符串。

**参数:**
- `str` - XString指针

**返回值:** 返回常量UTF-8字符串指针(内部缓存)

---

##### XString_toGbk

```c
const char* XString_toGbk(const XString* str)
```

转换为GBK编码字符串。

**参数:**
- `str` - XString指针

**返回值:** 返回常量GBK字符串指针(内部缓存)

---

##### XString_toLocal

```c
const char* XString_toLocal(const XString* str)
```

转换为本地编码字符串。

**参数:**
- `str` - XString指针

**返回值:** 返回常量本地编码字符串指针(内部缓存)

---

#### 字符串转换

##### XString_toLower

```c
XString* XString_toLower(const XString* str)
```

转换为小写字符串(创建新对象)。

**参数:**
- `str` - 源字符串

**返回值:** 成功返回新的XString指针(小写)，失败返回NULL

---

##### XString_toUpper

```c
XString* XString_toUpper(const XString* str)
```

转换为大写字符串(创建新对象)。

**参数:**
- `str` - 源字符串

**返回值:** 成功返回新的XString指针(大写)，失败返回NULL

---

##### XString_trimmed

```c
XString* XString_trimmed(const XString* str)
```

修剪字符串前后的空白字符(创建新对象)。

**参数:**
- `str` - 源字符串

**返回值:** 成功返回新的XString指针(修剪后)，失败返回NULL

---

#### 数值转换

##### XString_toInt

```c
int XString_toInt(const XString* str, bool* ok, int base)
```

转换为int类型数值。

**参数:**
- `str` - XString指针
- `ok` - 输出参数：转换成功则为true
- `base` - 进制(2-36，0表示自动识别)

**返回值:** 转换结果(失败返回0)

---

##### XString_toDouble

```c
double XString_toDouble(const XString* str, bool* ok)
```

转换为double类型数值。

**参数:**
- `str` - XString指针
- `ok` - 输出参数：转换成功则为true

**返回值:** 转换结果(失败返回0.0)

---

#### 子串操作

##### XString_left

```c
XString* XString_left(const XString* str, size_t n)
```

获取字符串左侧指定长度的子串。

**参数:**
- `str` - 源字符串
- `n` - 子串长度

**返回值:** 成功返回新的XString指针(子串)，失败返回NULL

---

##### XString_right

```c
XString* XString_right(const XString* str, size_t n)
```

获取字符串右侧指定长度的子串。

**参数:**
- `str` - 源字符串
- `n` - 子串长度

**返回值:** 成功返回新的XString指针(子串)，失败返回NULL

---

##### XString_mid

```c
XString* XString_mid(const XString* str, size_t pos, size_t n)
```

获取从指定位置开始的子串。

**参数:**
- `str` - 源字符串
- `pos` - 起始位置
- `n` - 子串长度(0表示到末尾)

**返回值:** 成功返回新的XString指针(子串)，失败返回NULL

---

#### 容量操作

##### XString_size_base

```c
size_t XString_size_base(const XString* str)
```

获取字符串长度(字符数)。

**参数:**
- `str` - XString指针

**返回值:** 返回字符数量

---

##### XString_isEmpty_base

```c
bool XString_isEmpty_base(const XString* str)
```

判断字符串是否为空。

**参数:**
- `str` - XString指针

**返回值:** 为空返回true，否则返回false

---

##### XString_reserve

```c
bool XString_reserve(XString* str, size_t capacity)
```

预留指定容量的存储空间。

**参数:**
- `str` - XString指针
- `capacity` - 预分配的字符数

**返回值:** 成功返回true，失败返回false

---

#### 拆分操作

##### XString_split_utf8

```c
XStringList* XString_split_utf8(const XString* str, const char* delimiter, XCharCaseSensitivity cs)
```

按分隔符拆分字符串为字符串列表。

**参数:**
- `str` - 源字符串
- `delimiter` - 分隔符(UTF-8)
- `cs` - 大小写敏感性

**返回值:** 成功返回XStringList指针(需手动释放)，失败返回NULL

---

### XStringList 字符串列表

字符串列表容器，是XVector的特化版本，专门用于存储XString对象。

#### 头文件

```c
#include "XStringList.h"
```

#### 创建与销毁

##### XStringList_create

```c
XStringList* XStringList_create()
```

创建一个空的字符串列表对象。

**参数:** 无

**返回值:** 成功返回XStringList指针，失败返回NULL

---

##### XStringList_create_copy

```c
XStringList* XStringList_create_copy(const XStringList* other)
```

通过拷贝另一个字符串列表创建新对象。

**参数:**
- `other` - 源字符串列表指针

**返回值:** 成功返回新XStringList指针，失败返回NULL

---

##### XStringList_create_move

```c
XStringList* XStringList_create_move(XStringList* other)
```

通过移动另一个字符串列表的资源创建新对象。

**参数:**
- `other` - 源字符串列表指针

**返回值:** 成功返回新XStringList指针，失败返回NULL

---

##### XStringList_init

```c
void XStringList_init(XStringList* strList)
```

初始化已分配内存的字符串列表对象。

**参数:**
- `strList` - 待初始化的XStringList指针

**返回值:** 无

---

##### XStringList_delete_base

```c
void XStringList_delete_base(XStringList* strList)
```

销毁字符串列表并释放所有内存。

**参数:**
- `strList` - XStringList指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XStringList_push_front_base

```c
void XStringList_push_front_base(XStringList* strList, const XString* value)
```

在列表头部插入XString对象(拷贝语义)。

**参数:**
- `strList` - XStringList指针
- `value` - 待插入的XString对象指针

**返回值:** 无

---

##### XStringList_push_front_utf8

```c
void XStringList_push_front_utf8(XStringList* strList, const char* utf8_str)
```

在列表头部插入UTF-8字符串。

**参数:**
- `strList` - XStringList指针
- `utf8_str` - UTF-8字符串

**返回值:** 无

---

##### XStringList_push_back_base

```c
void XStringList_push_back_base(XStringList* strList, const XString* value)
```

在列表尾部插入XString对象(拷贝语义)。

**参数:**
- `strList` - XStringList指针
- `value` - 待插入的XString对象指针

**返回值:** 无

---

##### XStringList_push_back_utf8

```c
void XStringList_push_back_utf8(XStringList* strList, const char* utf8_str)
```

在列表尾部插入UTF-8字符串。

**参数:**
- `strList` - XStringList指针
- `utf8_str` - UTF-8字符串

**返回值:** 无

---

##### XStringList_insert_base

```c
bool XStringList_insert_base(XStringList* strList, int64_t index, const XString* value)
```

在指定位置插入XString对象(拷贝语义)。

**参数:**
- `strList` - XStringList指针
- `index` - 插入位置索引
- `value` - 待插入的XString对象指针

**返回值:** 成功返回true，失败返回false

---

##### XStringList_insert_utf8

```c
void XStringList_insert_utf8(XStringList* strList, int64_t index, const char* utf8_str)
```

在指定位置插入UTF-8字符串。

**参数:**
- `strList` - XStringList指针
- `index` - 插入位置索引
- `utf8_str` - UTF-8字符串

**返回值:** 无

---

#### 删除操作

##### XStringList_pop_front_base

```c
void XStringList_pop_front_base(XStringList* strList)
```

删除第一个元素。

**参数:**
- `strList` - XStringList指针

**返回值:** 无

---

##### XStringList_pop_back_base

```c
void XStringList_pop_back_base(XStringList* strList)
```

删除最后一个元素。

**参数:**
- `strList` - XStringList指针

**返回值:** 无

---

##### XStringList_remove_base

```c
void XStringList_remove_base(XStringList* strList, int64_t index)
```

删除指定位置的元素。

**参数:**
- `strList` - XStringList指针
- `index` - 元素索引

**返回值:** 无

---

##### XStringList_clear_base

```c
void XStringList_clear_base(XStringList* strList)
```

清空所有元素。

**参数:**
- `strList` - XStringList指针

**返回值:** 无

---

#### 元素访问

##### XStringList_at_base

```c
XString* XStringList_at_base(const XStringList* strList, int64_t index)
```

获取指定位置的XString对象指针。

**参数:**
- `strList` - XStringList指针
- `index` - 元素索引

**返回值:** 成功返回XString指针，索引越界返回NULL

---

##### XStringList_front_base

```c
XString* XStringList_front_base(const XStringList* strList)
```

获取第一个XString对象指针。

**参数:**
- `strList` - XStringList指针

**返回值:** 成功返回首元素指针，列表为空返回NULL

---

##### XStringList_back_base

```c
XString* XStringList_back_base(const XStringList* strList)
```

获取最后一个XString对象指针。

**参数:**
- `strList` - XStringList指针

**返回值:** 成功返回尾元素指针，列表为空返回NULL

---

#### 字符串操作

##### XStringList_join

```c
XString* XStringList_join(const XStringList* strList, const XString* separator)
```

用指定分隔符拼接列表中的所有字符串。

**参数:**
- `strList` - XStringList指针
- `separator` - 分隔符XString对象

**返回值:** 成功返回拼接后的新XString对象，失败返回NULL

---

##### XStringList_join_utf8

```c
XString* XStringList_join_utf8(const XStringList* strList, const char* separator)
```

用指定UTF-8分隔符拼接列表中的所有字符串。

**参数:**
- `strList` - XStringList指针
- `separator` - UTF-8分隔符字符串

**返回值:** 成功返回拼接后的新XString对象，失败返回NULL

---

#### 容量操作

##### XStringList_size_base

```c
size_t XStringList_size_base(const XStringList* strList)
```

获取元素数量。

**参数:**
- `strList` - XStringList指针

**返回值:** 返回元素数量

---

##### XStringList_isEmpty_base

```c
bool XStringList_isEmpty_base(const XStringList* strList)
```

判断列表是否为空。

**参数:**
- `strList` - XStringList指针

**返回值:** 为空返回true，否则返回false

---

### XVariantList 变体列表

变体列表容器，是XVector的特化版本，专门用于存储XVariant对象。

#### 头文件

```c
#include "XVariantList.h"
```

#### 创建与销毁

##### XVariantList_create

```c
XVariantList* XVariantList_create()
```

创建一个空的变体列表对象。

**参数:** 无

**返回值:** 成功返回XVariantList指针，失败返回NULL

---

##### XVariantList_create_copy

```c
XVariantList* XVariantList_create_copy(const XVariantList* other)
```

通过拷贝另一个变体列表创建新对象。

**参数:**
- `other` - 源变体列表指针

**返回值:** 成功返回新XVariantList指针，失败返回NULL

---

##### XVariantList_create_move

```c
XVariantList* XVariantList_create_move(XVariantList* other)
```

通过移动另一个变体列表的资源创建新对象。

**参数:**
- `other` - 源变体列表指针

**返回值:** 成功返回新XVariantList指针，失败返回NULL

---

##### XVariantList_init

```c
void XVariantList_init(XVariantList* list)
```

初始化已分配内存的变体列表对象。

**参数:**
- `list` - 待初始化的XVariantList指针

**返回值:** 无

---

##### XVariantList_delete_base

```c
void XVariantList_delete_base(XVariantList* list)
```

销毁变体列表并释放所有内存。

**参数:**
- `list` - XVariantList指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XVariantList_push_front_base

```c
void XVariantList_push_front_base(XVariantList* list, const XVariant* value)
```

在列表头部插入XVariant对象(拷贝语义)。

**参数:**
- `list` - XVariantList指针
- `value` - 待插入的XVariant对象指针

**返回值:** 无

---

##### XVariantList_push_back_base

```c
void XVariantList_push_back_base(XVariantList* list, const XVariant* value)
```

在列表尾部插入XVariant对象(拷贝语义)。

**参数:**
- `list` - XVariantList指针
- `value` - 待插入的XVariant对象指针

**返回值:** 无

---

##### XVariantList_insert

```c
bool XVariantList_insert(XVariantList* list, int64_t index, const XVariant* value)
```

在指定位置插入XVariant对象(拷贝语义)。

**参数:**
- `list` - XVariantList指针
- `index` - 插入位置索引
- `value` - 待插入的XVariant对象指针

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XVariantList_pop_front_base

```c
void XVariantList_pop_front_base(XVariantList* list)
```

删除第一个元素。

**参数:**
- `list` - XVariantList指针

**返回值:** 无

---

##### XVariantList_pop_back_base

```c
void XVariantList_pop_back_base(XVariantList* list)
```

删除最后一个元素。

**参数:**
- `list` - XVariantList指针

**返回值:** 无

---

##### XVariantList_remove_base

```c
void XVariantList_remove_base(XVariantList* list, int64_t index)
```

删除指定位置的元素。

**参数:**
- `list` - XVariantList指针
- `index` - 元素索引

**返回值:** 无

---

##### XVariantList_clear_base

```c
void XVariantList_clear_base(XVariantList* list)
```

清空所有元素。

**参数:**
- `list` - XVariantList指针

**返回值:** 无

---

#### 元素访问

##### XVariantList_at_base

```c
XVariant* XVariantList_at_base(const XVariantList* list, int64_t index)
```

获取指定位置的XVariant对象指针。

**参数:**
- `list` - XVariantList指针
- `index` - 元素索引

**返回值:** 成功返回XVariant指针，索引越界返回NULL

---

##### XVariantList_front_base

```c
XVariant* XVariantList_front_base(const XVariantList* list)
```

获取第一个XVariant对象指针。

**参数:**
- `list` - XVariantList指针

**返回值:** 成功返回首元素指针，列表为空返回NULL

---

##### XVariantList_back_base

```c
XVariant* XVariantList_back_base(const XVariantList* list)
```

获取最后一个XVariant对象指针。

**参数:**
- `list` - XVariantList指针

**返回值:** 成功返回尾元素指针，列表为空返回NULL

---

#### 容量操作

##### XVariantList_size_base

```c
size_t XVariantList_size_base(const XVariantList* list)
```

获取元素数量。

**参数:**
- `list` - XVariantList指针

**返回值:** 返回元素数量

---

##### XVariantList_isEmpty_base

```c
bool XVariantList_isEmpty_base(const XVariantList* list)
```

判断列表是否为空。

**参数:**
- `list` - XVariantList指针

**返回值:** 为空返回true，否则返回false

---

## 关联容器

### XMap 有序映射

有序映射容器，基于红黑树实现，支持O(log n)查找。

#### 头文件

```c
#include "XMap.h"
```

#### 创建与销毁

##### XMap_create

```c
XMap* XMap_create(size_t keyTypeSize, size_t valTypeSize, XCompare compare)
```

创建一个有序映射对象，默认启用COW机制。

**参数:**
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `compare` - 键的比较函数

**返回值:** 成功返回XMap指针，失败返回NULL

---

##### XMap_create_ex

```c
XMap* XMap_create_ex(size_t keyTypeSize, size_t valTypeSize, XCompare compare, bool useCow)
```

创建一个有序映射对象，可指定是否启用COW。

**参数:**
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `compare` - 键的比较函数
- `useCow` - 是否启用写时复制

**返回值:** 成功返回XMap指针，失败返回NULL

---

##### XMap_Create

```c
#define XMap_Create(keyType, valType, compare) XMap_create(sizeof(keyType), sizeof(valType), compare)
```

类型安全创建宏。

**参数:**
- `keyType` - 键类型
- `valType` - 值类型
- `compare` - 键的比较函数

**返回值:** 成功返回XMap指针，失败返回NULL

---

##### XMap_create_copy

```c
XMap* XMap_create_copy(const XMap* other)
```

通过拷贝另一个映射创建新对象。

**参数:**
- `other` - 源映射指针

**返回值:** 成功返回新XMap指针，失败返回NULL

---

##### XMap_create_move

```c
XMap* XMap_create_move(XMap* other)
```

通过移动另一个映射的资源创建新对象。

**参数:**
- `other` - 源映射指针

**返回值:** 成功返回新XMap指针，失败返回NULL

---

##### XMap_init

```c
void XMap_init(XMap* map, size_t keyTypeSize, size_t valTypeSize, XCompare compare, bool useCow)
```

初始化已分配内存的映射对象。

**参数:**
- `map` - 待初始化的XMap指针
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `compare` - 键的比较函数
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XMap_delete_base

```c
void XMap_delete_base(XMap* map)
```

销毁映射并释放所有内存。

**参数:**
- `map` - XMap指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XMap_insert_base

```c
bool XMap_insert_base(XMap* map, const void* key, const void* pvValue)
```

插入键值对(拷贝语义)。

**参数:**
- `map` - XMap指针
- `key` - 键指针
- `pvValue` - 值指针

**返回值:** 成功返回true，失败返回false

---

##### XMap_Insert_Base

```c
#define XMap_Insert_Base(map, keyType, key, valType, value) { keyType k = key; valType v = value; XMap_insert_base(map, &k, &v); }
```

类型安全插入宏。

---

##### XMap_insert_move_base

```c
bool XMap_insert_move_base(XMap* map, void* key, void* pvValue)
```

插入键值对(移动语义，键和值均移动)。

**参数:**
- `map` - XMap指针
- `key` - 键指针(所有权转移)
- `pvValue` - 值指针(所有权转移)

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XMap_erase_base

```c
void XMap_erase_base(XMap* map, const XMap_iterator* it, XMap_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `map` - XMap指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XMap_remove_base

```c
bool XMap_remove_base(XMap* map, const void* key)
```

通过键删除元素。

**参数:**
- `map` - XMap指针
- `key` - 键指针

**返回值:** 成功返回true，键不存在返回false

---

##### XMap_Remove_Base

```c
#define XMap_Remove_Base(map, keyType, key) { keyType k = key; XMap_remove_base(map, &k); }
```

类型安全删除宏。

---

#### 元素访问

##### XMap_value_base

```c
void* XMap_value_base(XMap* map, const void* key)
```

通过键获取值的指针。

**参数:**
- `map` - XMap指针
- `key` - 键指针

**返回值:** 成功返回值指针，键不存在返回NULL

---

##### XMap_Value_Base

```c
#define XMap_Value_Base(map, key, valueType) (*(valueType*)XMap_value_base(map, &(key)))
```

类型安全获取值。

---

#### 查找操作

##### XMap_find_base

```c
bool XMap_find_base(const XMap* map, const void* key, XMap_iterator* it)
```

通过键查找元素并获取迭代器。

**参数:**
- `map` - XMap指针
- `key` - 键指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XMap_contains

```c
bool XMap_contains(const XMap* map, const void* key)
```

判断映射是否包含指定键。

**参数:**
- `map` - XMap指针
- `key` - 键指针

**返回值:** 包含返回true，否则返回false

---

#### 其他操作

##### XMap_keys_base

```c
XVector* XMap_keys_base(const XMap* map)
```

获取所有键的集合。

**参数:**
- `map` - XMap指针

**返回值:** 返回存储所有键的XVector指针，需用户自行释放

---

##### XMap_size_base

```c
size_t XMap_size_base(const XMap* map)
```

获取元素数量。

**参数:**
- `map` - XMap指针

**返回值:** 返回元素数量

---

##### XMap_isEmpty_base

```c
bool XMap_isEmpty_base(const XMap* map)
```

判断映射是否为空。

**参数:**
- `map` - XMap指针

**返回值:** 为空返回true，否则返回false

---

##### XMap_clear_base

```c
void XMap_clear_base(XMap* map)
```

清空所有元素。

**参数:**
- `map` - XMap指针

**返回值:** 无

---

### XHashMap 哈希映射

哈希映射容器，基于哈希表实现，支持O(1)平均查找时间。

#### 头文件

```c
#include "XHashMap.h"
```

#### 创建与销毁

##### XHashMap_create

```c
XHashMap* XHashMap_create(size_t keyTypeSize, size_t valTypeSize, XHashFunc hash, XCompare compare)
```

创建一个哈希映射对象，默认启用COW机制。

**参数:**
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `hash` - 哈希函数
- `compare` - 键的比较函数

**返回值:** 成功返回XHashMap指针，失败返回NULL

---

##### XHashMap_create_ex

```c
XHashMap* XHashMap_create_ex(size_t keyTypeSize, size_t valTypeSize, XHashFunc hash, XCompare compare, bool useCow)
```

创建一个哈希映射对象，可指定是否启用COW。

**参数:**
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `hash` - 哈希函数
- `compare` - 键的比较函数
- `useCow` - 是否启用写时复制

**返回值:** 成功返回XHashMap指针，失败返回NULL

---

##### XHashMap_Create

```c
#define XHashMap_Create(keyType, valType, compare) XHashMap_create(sizeof(keyType), sizeof(valType), XHash_xxhash64, compare)
```

类型安全创建宏，默认使用xxhash64哈希函数。

---

##### XHashMap_create_copy

```c
XHashMap* XHashMap_create_copy(const XHashMap* other)
```

通过拷贝另一个哈希映射创建新对象。

**参数:**
- `other` - 源哈希映射指针

**返回值:** 成功返回新XHashMap指针，失败返回NULL

---

##### XHashMap_create_move

```c
XHashMap* XHashMap_create_move(XHashMap* other)
```

通过移动另一个哈希映射的资源创建新对象。

**参数:**
- `other` - 源哈希映射指针

**返回值:** 成功返回新XHashMap指针，失败返回NULL

---

##### XHashMap_init

```c
void XHashMap_init(XHashMap* map, size_t keyTypeSize, size_t valTypeSize, XHashFunc hash, XCompare compare, bool useCow)
```

初始化已分配内存的哈希映射对象。

**参数:**
- `map` - 待初始化的XHashMap指针
- `keyTypeSize` - 键的类型大小(字节)
- `valTypeSize` - 值的类型大小(字节)
- `hash` - 哈希函数
- `compare` - 键的比较函数
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XHashMap_delete_base

```c
void XHashMap_delete_base(XHashMap* map)
```

销毁哈希映射并释放所有内存。

**参数:**
- `map` - XHashMap指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XHashMap_insert_base

```c
bool XHashMap_insert_base(XHashMap* map, const void* key, const void* pvValue)
```

插入键值对(拷贝语义)。

**参数:**
- `map` - XHashMap指针
- `key` - 键指针
- `pvValue` - 值指针

**返回值:** 成功返回true，失败返回false

---

#### 删除操作

##### XHashMap_erase_base

```c
void XHashMap_erase_base(XHashMap* map, const XHashMap_iterator* it, XHashMap_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `map` - XHashMap指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XHashMap_remove_base

```c
bool XHashMap_remove_base(XHashMap* map, const void* key)
```

通过键删除元素。

**参数:**
- `map` - XHashMap指针
- `key` - 键指针

**返回值:** 成功返回true，键不存在返回false

---

#### 元素访问

##### XHashMap_value_base

```c
void* XHashMap_value_base(XHashMap* map, const void* key)
```

通过键获取值的指针。

**参数:**
- `map` - XHashMap指针
- `key` - 键指针

**返回值:** 成功返回值指针，键不存在返回NULL

---

#### 查找操作

##### XHashMap_find_base

```c
bool XHashMap_find_base(const XHashMap* map, const void* key, XHashMap_iterator* it)
```

通过键查找元素并获取迭代器。

**参数:**
- `map` - XHashMap指针
- `key` - 键指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XHashMap_contains

```c
bool XHashMap_contains(const XHashMap* map, const void* key)
```

判断哈希映射是否包含指定键。

**参数:**
- `map` - XHashMap指针
- `key` - 键指针

**返回值:** 包含返回true，否则返回false

---

#### 容量操作

##### XHashMap_size_base

```c
size_t XHashMap_size_base(const XHashMap* map)
```

获取元素数量。

**参数:**
- `map` - XHashMap指针

**返回值:** 返回元素数量

---

##### XHashMap_capacity_base

```c
size_t XHashMap_capacity_base(const XHashMap* map)
```

获取哈希表容量(桶数量)。

**参数:**
- `map` - XHashMap指针

**返回值:** 返回当前桶数量

---

##### XHashMap_isEmpty_base

```c
bool XHashMap_isEmpty_base(const XHashMap* map)
```

判断哈希映射是否为空。

**参数:**
- `map` - XHashMap指针

**返回值:** 为空返回true，否则返回false

---

#### 其他操作

##### XHashMap_clear_base

```c
void XHashMap_clear_base(XHashMap* map)
```

清空所有元素。

**参数:**
- `map` - XHashMap指针

**返回值:** 无

---

##### XHashMap_copy_base

```c
void XHashMap_copy_base(XHashMap* dest, const XHashMap* src)
```

拷贝另一个哈希映射的内容。

**参数:**
- `dest` - 目标哈希映射
- `src` - 源哈希映射

**返回值:** 无

---

##### XHashMap_move_base

```c
void XHashMap_move_base(XHashMap* dest, XHashMap* src)
```

移动另一个哈希映射的资源。

**参数:**
- `dest` - 目标哈希映射
- `src` - 源哈希映射

**返回值:** 无

---

##### XHashMap_swap_base

```c
void XHashMap_swap_base(XHashMap* a, XHashMap* b)
```

交换两个哈希映射的内容。

**参数:**
- `a` - 第一个哈希映射
- `b` - 第二个哈希映射

**返回值:** 无

---

### XSet 有序集合

有序集合容器，基于红黑树实现，元素自动排序且不重复。

#### 头文件

```c
#include "XSet.h"
```

#### 创建与销毁

##### XSet_create

```c
XSet* XSet_create(size_t keyTypeSize, XCompare compare)
```

创建一个有序集合对象，默认启用COW机制。

**参数:**
- `keyTypeSize` - 元素类型大小(字节)
- `compare` - 元素的比较函数

**返回值:** 成功返回XSet指针，失败返回NULL

---

##### XSet_create_ex

```c
XSet* XSet_create_ex(size_t keyTypeSize, XCompare compare, bool useCow)
```

创建一个有序集合对象，可指定是否启用COW。

**参数:**
- `keyTypeSize` - 元素类型大小(字节)
- `compare` - 元素的比较函数
- `useCow` - 是否启用写时复制

**返回值:** 成功返回XSet指针，失败返回NULL

---

##### XSet_Create

```c
#define XSet_Create(keyType, compare) XSet_create(sizeof(keyType), compare)
```

类型安全创建宏。

---

##### XSet_init

```c
void XSet_init(XSet* set, size_t keyTypeSize, XCompare compare, bool useCow)
```

初始化已分配内存的有序集合对象。

**参数:**
- `set` - 待初始化的XSet指针
- `keyTypeSize` - 元素类型大小(字节)
- `compare` - 元素的比较函数
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XSet_delete_base

```c
void XSet_delete_base(XSet* set)
```

销毁有序集合并释放所有内存。

**参数:**
- `set` - XSet指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XSet_insert_base

```c
bool XSet_insert_base(XSet* set, const void* key)
```

插入元素(拷贝语义)。

**参数:**
- `set` - XSet指针
- `key` - 待插入元素的指针

**返回值:** 成功返回true，元素已存在返回false

---

##### XSet_Insert_Base

```c
#define XSet_Insert_Base(set, keyType, key) { keyType k = key; XSet_insert_base(set, &k); }
```

类型安全插入宏。

---

##### XSet_insert_move_base

```c
bool XSet_insert_move_base(XSet* set, void* key)
```

插入元素(移动语义)。

**参数:**
- `set` - XSet指针
- `key` - 待插入元素的指针(所有权转移)

**返回值:** 成功返回true，元素已存在返回false

---

#### 删除操作

##### XSet_erase_base

```c
void XSet_erase_base(XSet* set, const XSet_iterator* it, XSet_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `set` - XSet指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XSet_remove_base

```c
bool XSet_remove_base(XSet* set, const void* key)
```

通过元素值删除元素。

**参数:**
- `set` - XSet指针
- `key` - 元素指针

**返回值:** 成功返回true，元素不存在返回false

---

##### XSet_Remove_Base

```c
#define XSet_Remove_Base(set, keyType, key) { keyType k = key; XSet_remove_base(set, &k); }
```

类型安全删除宏。

---

#### 查找操作

##### XSet_find_base

```c
bool XSet_find_base(XSet* set, const void* key, XSet_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `set` - XSet指针
- `key` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XSet_contains

```c
bool XSet_contains(const XSet* set, const void* key)
```

判断集合是否包含指定元素。

**参数:**
- `set` - XSet指针
- `key` - 元素指针

**返回值:** 包含返回true，否则返回false

---

#### 其他操作

##### XSet_keys_base

```c
XVector* XSet_keys_base(const XSet* set)
```

获取所有元素的集合。

**参数:**
- `set` - XSet指针

**返回值:** 返回存储所有元素的XVector指针，需用户自行释放

---

##### XSet_size_base

```c
size_t XSet_size_base(const XSet* set)
```

获取元素数量。

**参数:**
- `set` - XSet指针

**返回值:** 返回元素数量

---

##### XSet_isEmpty_base

```c
bool XSet_isEmpty_base(const XSet* set)
```

判断集合是否为空。

**参数:**
- `set` - XSet指针

**返回值:** 为空返回true，否则返回false

---

##### XSet_clear_base

```c
void XSet_clear_base(XSet* set)
```

清空所有元素。

**参数:**
- `set` - XSet指针

**返回值:** 无

---

### XHashSet 哈希集合

哈希集合容器，基于哈希表实现，元素不重复，支持O(1)平均查找时间。

#### 头文件

```c
#include "XHashSet.h"
```

#### 创建与销毁

##### XHashSet_create

```c
XHashSet* XHashSet_create(size_t keyTypeSize, XHashFunc hash, XCompare compare)
```

创建一个哈希集合对象，默认启用COW机制。

**参数:**
- `keyTypeSize` - 元素类型大小(字节)
- `hash` - 哈希函数
- `compare` - 元素的比较函数

**返回值:** 成功返回XHashSet指针，失败返回NULL

---

##### XHashSet_create_ex

```c
XHashSet* XHashSet_create_ex(size_t keyTypeSize, XHashFunc hash, XCompare compare, bool useCow)
```

创建一个哈希集合对象，可指定是否启用COW。

**参数:**
- `keyTypeSize` - 元素类型大小(字节)
- `hash` - 哈希函数
- `compare` - 元素的比较函数
- `useCow` - 是否启用写时复制

**返回值:** 成功返回XHashSet指针，失败返回NULL

---

##### XHashSet_Create

```c
#define XHashSet_Create(keyType, compare) XHashSet_create(sizeof(keyType), XHash_xxhash64, compare)
```

类型安全创建宏，默认使用xxhash64哈希函数。

---

##### XHashSet_init

```c
void XHashSet_init(XHashSet* set, size_t keyTypeSize, XHashFunc hash, XCompare compare, bool useCow)
```

初始化已分配内存的哈希集合对象。

**参数:**
- `set` - 待初始化的XHashSet指针
- `keyTypeSize` - 元素类型大小(字节)
- `hash` - 哈希函数
- `compare` - 元素的比较函数
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XHashSet_delete_base

```c
void XHashSet_delete_base(XHashSet* set)
```

销毁哈希集合并释放所有内存。

**参数:**
- `set` - XHashSet指针，可为NULL

**返回值:** 无

---

#### 插入操作

##### XHashSet_insert_base

```c
bool XHashSet_insert_base(XHashSet* set, const void* key)
```

插入元素(拷贝语义)。

**参数:**
- `set` - XHashSet指针
- `key` - 待插入元素的指针

**返回值:** 成功返回true，元素已存在返回false

---

##### XHashSet_insert_move_base

```c
bool XHashSet_insert_move_base(XHashSet* set, void* key)
```

插入元素(移动语义)。

**参数:**
- `set` - XHashSet指针
- `key` - 待插入元素的指针(所有权转移)

**返回值:** 成功返回true，元素已存在返回false

---

#### 删除操作

##### XHashSet_erase_base

```c
void XHashSet_erase_base(XHashSet* set, const XHashSet_iterator* it, XHashSet_iterator* next)
```

删除迭代器指向的元素。

**参数:**
- `set` - XHashSet指针
- `it` - 指向待删除元素的迭代器
- `next` - 输出参数，存储删除后的下一个迭代器

**返回值:** 无

---

##### XHashSet_remove_base

```c
bool XHashSet_remove_base(XHashSet* set, const void* key)
```

通过元素值删除元素。

**参数:**
- `set` - XHashSet指针
- `key` - 元素指针

**返回值:** 成功返回true，元素不存在返回false

---

#### 查找操作

##### XHashSet_find_base

```c
bool XHashSet_find_base(XHashSet* set, const void* key, XHashSet_iterator* it)
```

查找元素并获取迭代器。

**参数:**
- `set` - XHashSet指针
- `key` - 待查找元素的指针
- `it` - 输出参数，存储找到的元素的迭代器

**返回值:** 找到返回true，未找到返回false

---

##### XHashSet_contains

```c
bool XHashSet_contains(const XHashSet* set, const void* key)
```

判断集合是否包含指定元素。

**参数:**
- `set` - XHashSet指针
- `key` - 元素指针

**返回值:** 包含返回true，否则返回false

---

#### 其他操作

##### XHashSet_keys_base

```c
XVector* XHashSet_keys_base(const XHashSet* set)
```

获取所有元素的集合。

**参数:**
- `set` - XHashSet指针

**返回值:** 返回存储所有元素的XVector指针，需用户自行释放

---

##### XHashSet_size_base

```c
size_t XHashSet_size_base(const XHashSet* set)
```

获取元素数量。

**参数:**
- `set` - XHashSet指针

**返回值:** 返回元素数量

---

##### XHashSet_capacity_base

```c
size_t XHashSet_capacity_base(const XHashSet* set)
```

获取哈希表容量(桶数量)。

**参数:**
- `set` - XHashSet指针

**返回值:** 返回当前桶数量

---

##### XHashSet_isEmpty_base

```c
bool XHashSet_isEmpty_base(const XHashSet* set)
```

判断集合是否为空。

**参数:**
- `set` - XHashSet指针

**返回值:** 为空返回true，否则返回false

---

##### XHashSet_clear_base

```c
void XHashSet_clear_base(XHashSet* set)
```

清空所有元素。

**参数:**
- `set` - XHashSet指针

**返回值:** 无

---

## 适配器容器

### XStack 栈

栈容器，后进先出(LIFO)，基于XVector实现。

#### 头文件

```c
#include "XStack.h"
```

#### 创建与销毁

##### XStack_create

```c
XStack* XStack_create(size_t typeSize)
```

创建一个栈对象。

**参数:**
- `typeSize` - 元素类型大小(字节)

**返回值:** 成功返回XStack指针，失败返回NULL

---

##### XStack_Create

```c
#define XStack_Create(Type) XStack_create(sizeof(Type))
```

类型安全创建宏。

---

##### XStack_init

```c
void XStack_init(XStack* stack, size_t typeSize)
```

初始化已分配内存的栈对象。

**参数:**
- `stack` - 待初始化的XStack指针
- `typeSize` - 元素类型大小(字节)

**返回值:** 无

---

##### XStack_delete_base

```c
void XStack_delete_base(XStack* stack)
```

销毁栈并释放所有内存。

**参数:**
- `stack` - XStack指针，可为NULL

**返回值:** 无

---

#### 栈操作

##### XStack_push_base

```c
bool XStack_push_base(XStack* stack, void* pvData)
```

压栈(拷贝语义)。

**参数:**
- `stack` - XStack指针
- `pvData` - 待压入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XStack_Push_Base

```c
#define XStack_Push_Base(stack, Type, value) { Type t = value; XStack_push_base(stack, &t); }
```

类型安全压栈宏。

---

##### XStack_pop_base

```c
void XStack_pop_base(XStack* stack)
```

弹栈(删除栈顶元素)。

**参数:**
- `stack` - XStack指针

**返回值:** 无

---

##### XStack_top_base

```c
void* XStack_top_base(XStack* stack)
```

获取栈顶元素指针。

**参数:**
- `stack` - XStack指针

**返回值:** 成功返回栈顶元素指针，栈为空返回NULL

---

##### XStack_Top_Base

```c
#define XStack_Top_Base(stack, Type) (*(Type*)XStack_top_base(stack))
```

类型安全获取栈顶元素。

---

#### 容量操作

##### XStack_size_base

```c
size_t XStack_size_base(const XStack* stack)
```

获取元素数量。

**参数:**
- `stack` - XStack指针

**返回值:** 返回元素数量

---

##### XStack_isEmpty_base

```c
bool XStack_isEmpty_base(const XStack* stack)
```

判断栈是否为空。

**参数:**
- `stack` - XStack指针

**返回值:** 为空返回true，否则返回false

---

##### XStack_clear_base

```c
void XStack_clear_base(XStack* stack)
```

清空所有元素。

**参数:**
- `stack` - XStack指针

**返回值:** 无

---

### XLockFreeStack 无锁栈

无锁栈容器，基于原子操作实现线程安全，适用于多线程环境。

#### 头文件

```c
#include "XLockFreeStack.h"
```

#### 特点

- 基于CAS原子操作实现
- 支持多生产者多消费者(MPMC)场景
- 高并发性能，无需锁机制
- 固定容量，不支持动态扩容

#### 创建与销毁

##### XLockFreeStack_create

```c
XLockFreeStack* XLockFreeStack_create(size_t typeSize, size_t capacity)
```

创建一个无锁栈对象。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `capacity` - 栈的最大容量

**返回值:** 成功返回XLockFreeStack指针，失败返回NULL

---

##### XLockFreeStack_Create

```c
#define XLockFreeStack_Create(Type, capacity) XLockFreeStack_create(sizeof(Type), capacity)
```

类型安全创建宏。

---

##### XLockFreeStack_init

```c
void XLockFreeStack_init(XLockFreeStack* stack, size_t typeSize, size_t capacity)
```

初始化已分配内存的无锁栈对象。

**参数:**
- `stack` - 待初始化的XLockFreeStack指针
- `typeSize` - 元素类型大小(字节)
- `capacity` - 栈的最大容量

**返回值:** 无

---

##### XLockFreeStack_delete_base

```c
void XLockFreeStack_delete_base(XLockFreeStack* stack)
```

销毁无锁栈并释放所有内存。

**参数:**
- `stack` - XLockFreeStack指针，可为NULL

**返回值:** 无

---

#### 栈操作

##### XLockFreeStack_push_base

```c
bool XLockFreeStack_push_base(XLockFreeStack* stack, void* pvData)
```

压栈(拷贝语义)，线程安全。

**参数:**
- `stack` - XLockFreeStack指针
- `pvData` - 待压入元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XLockFreeStack_pop_base

```c
void XLockFreeStack_pop_base(XLockFreeStack* stack)
```

弹栈(删除栈顶元素)，线程安全。

**参数:**
- `stack` - XLockFreeStack指针

**返回值:** 无

---

##### XLockFreeStack_top_base

```c
void* XLockFreeStack_top_base(XLockFreeStack* stack)
```

获取栈顶元素指针。

**参数:**
- `stack` - XLockFreeStack指针

**返回值:** 成功返回栈顶元素指针，栈为空返回NULL

---

##### XLockFreeStack_receive_base

```c
bool XLockFreeStack_receive_base(XLockFreeStack* stack, void* pvBuffer)
```

获取栈顶元素并弹栈(线程安全)。

**参数:**
- `stack` - XLockFreeStack指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，栈为空返回false

---

#### 状态查询

##### XLockFreeStack_isFull_base

```c
bool XLockFreeStack_isFull_base(const XLockFreeStack* stack)
```

判断栈是否已满。

**参数:**
- `stack` - XLockFreeStack指针

**返回值:** 已满返回true，否则返回false

---

##### XLockFreeStack_isEmpty_base

```c
bool XLockFreeStack_isEmpty_base(const XLockFreeStack* stack)
```

判断栈是否为空。

**参数:**
- `stack` - XLockFreeStack指针

**返回值:** 为空返回true，否则返回false

---

### XQueue 队列

队列容器，先进先出(FIFO)，基于单向链表实现。

#### 头文件

```c
#include "XQueue.h"
```

#### 创建与销毁

##### XQueue_create

```c
XQueue* XQueue_create(size_t typeSize)
```

创建一个队列对象。

**参数:**
- `typeSize` - 元素类型大小(字节)

**返回值:** 成功返回XQueue指针，失败返回NULL

---

##### XQueue_Create

```c
#define XQueue_Create(Type) XQueue_create(sizeof(Type))
```

类型安全创建宏。

---

##### XQueue_init

```c
void XQueue_init(XQueue* queue, size_t typeSize)
```

初始化已分配内存的队列对象。

**参数:**
- `queue` - 待初始化的XQueue指针
- `typeSize` - 元素类型大小(字节)

**返回值:** 无

---

##### XQueue_delete_base

```c
void XQueue_delete_base(XQueue* queue)
```

销毁队列并释放所有内存。

**参数:**
- `queue` - XQueue指针，可为NULL

**返回值:** 无

---

#### 入队操作

##### XQueue_push_base

```c
bool XQueue_push_base(XQueue* queue, void* pvData)
```

入队(拷贝语义)。

**参数:**
- `queue` - XQueue指针
- `pvData` - 待入队元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XQueue_Push_Base

```c
#define XQueue_Push_Base(queue, Type, value) { Type t = value; XQueue_push_base(queue, &t); }
```

类型安全入队宏。

---

#### 出队操作

##### XQueue_pop_base

```c
void XQueue_pop_base(XQueue* queue)
```

出队(删除队头元素)。

**参数:**
- `queue` - XQueue指针

**返回值:** 无

---

##### XQueue_receive_base

```c
bool XQueue_receive_base(XQueue* queue, void* pvBuffer)
```

获取队头元素并出队。

**参数:**
- `queue` - XQueue指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，队列为空返回false

---

#### 元素访问

##### XQueue_top_base

```c
void* XQueue_top_base(XQueue* queue)
```

获取队头元素指针。

**参数:**
- `queue` - XQueue指针

**返回值:** 成功返回队头元素指针，队列为空返回NULL

---

##### XQueue_Top_Base

```c
#define XQueue_Top_Base(queue, Type) (*(Type*)XQueue_top_base(queue))
```

类型安全获取队头元素。

---

#### 容量操作

##### XQueue_size_base

```c
size_t XQueue_size_base(const XQueue* queue)
```

获取元素数量。

**参数:**
- `queue` - XQueue指针

**返回值:** 返回元素数量

---

##### XQueue_isEmpty_base

```c
bool XQueue_isEmpty_base(const XQueue* queue)
```

判断队列是否为空。

**参数:**
- `queue` - XQueue指针

**返回值:** 为空返回true，否则返回false

---

##### XQueue_clear_base

```c
void XQueue_clear_base(XQueue* queue)
```

清空所有元素。

**参数:**
- `queue` - XQueue指针

**返回值:** 无

---

### XLockFreeQueue 无锁队列

无锁队列容器，基于原子操作实现线程安全，适用于多线程环境。

#### 头文件

```c
#include "XLockFreeQueue.h"
```

#### 特点

- 基于CAS原子操作实现
- 支持多生产者多消费者(MPMC)场景
- 高并发性能，无需锁机制
- 固定容量，不支持动态扩容

#### 创建与销毁

##### XLockFreeQueue_create

```c
XLockFreeQueue* XLockFreeQueue_create(size_t typeSize, size_t count)
```

创建一个无锁队列对象。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `count` - 队列的最大容量

**返回值:** 成功返回XLockFreeQueue指针，失败返回NULL

---

##### XLockFreeQueue_Create

```c
#define XLockFreeQueue_Create(Type, count) XLockFreeQueue_create(sizeof(Type), count)
```

类型安全创建宏。

---

##### XLockFreeQueue_init

```c
void XLockFreeQueue_init(XLockFreeQueue* queue, size_t typeSize, size_t count)
```

初始化已分配内存的无锁队列对象。

**参数:**
- `queue` - 待初始化的XLockFreeQueue指针
- `typeSize` - 元素类型大小(字节)
- `count` - 队列的最大容量

**返回值:** 无

---

##### XLockFreeQueue_delete_base

```c
void XLockFreeQueue_delete_base(XLockFreeQueue* queue)
```

销毁无锁队列并释放所有内存。

**参数:**
- `queue` - XLockFreeQueue指针，可为NULL

**返回值:** 无

---

#### 入队操作

##### XLockFreeQueue_push_base

```c
bool XLockFreeQueue_push_base(XLockFreeQueue* queue, void* pvData)
```

入队(拷贝语义)，线程安全。

**参数:**
- `queue` - XLockFreeQueue指针
- `pvData` - 待入队元素的指针

**返回值:** 成功返回true，队列已满返回false

---

#### 出队操作

##### XLockFreeQueue_pop_base

```c
void XLockFreeQueue_pop_base(XLockFreeQueue* queue)
```

出队(删除队头元素)，线程安全。

**参数:**
- `queue` - XLockFreeQueue指针

**返回值:** 无

---

##### XLockFreeQueue_receive_base

```c
bool XLockFreeQueue_receive_base(XLockFreeQueue* queue, void* pvBuffer)
```

获取队头元素并出队(线程安全)。

**参数:**
- `queue` - XLockFreeQueue指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，队列为空返回false

---

#### 元素访问

##### XLockFreeQueue_top_base

```c
void* XLockFreeQueue_top_base(XLockFreeQueue* queue)
```

获取队头元素指针。

**参数:**
- `queue` - XLockFreeQueue指针

**返回值:** 成功返回队头元素指针，队列为空返回NULL

---

#### 状态查询

##### XLockFreeQueue_isFull_base

```c
bool XLockFreeQueue_isFull_base(const XLockFreeQueue* queue)
```

判断队列是否已满。

**参数:**
- `queue` - XLockFreeQueue指针

**返回值:** 已满返回true，否则返回false

---

##### XLockFreeQueue_isEmpty_base

```c
bool XLockFreeQueue_isEmpty_base(const XLockFreeQueue* queue)
```

判断队列是否为空。

**参数:**
- `queue` - XLockFreeQueue指针

**返回值:** 为空返回true，否则返回false

---

### XPriorityQueue 优先队列

优先队列容器，基于堆实现，元素按优先级出队。

#### 头文件

```c
#include "XPriorityQueue.h"
```

#### 创建与销毁

##### XPriorityQueue_create

```c
XPriorityQueue* XPriorityQueue_create(size_t typeSize, XCompare compare, XSortOrder order)
```

创建一个优先队列对象。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `compare` - 元素比较函数
- `order` - 排序顺序(XSort_Ascending升序，XSort_Descending降序)

**返回值:** 成功返回XPriorityQueue指针，失败返回NULL

---

##### XPriorityQueue_Create

```c
#define XPriorityQueue_Create(Type, compare, order) XPriorityQueue_create(sizeof(Type), compare, order)
```

类型安全创建宏。

---

##### XPriorityQueue_init

```c
void XPriorityQueue_init(XPriorityQueue* queue, size_t typeSize, XCompare compare, XSortOrder order)
```

初始化已分配内存的优先队列对象。

**参数:**
- `queue` - 待初始化的XPriorityQueue指针
- `typeSize` - 元素类型大小(字节)
- `compare` - 元素比较函数
- `order` - 排序顺序

**返回值:** 无

---

##### XPriorityQueue_delete_base

```c
void XPriorityQueue_delete_base(XPriorityQueue* queue)
```

销毁优先队列并释放所有内存。

**参数:**
- `queue` - XPriorityQueue指针，可为NULL

**返回值:** 无

---

#### 入队操作

##### XPriorityQueue_push_base

```c
bool XPriorityQueue_push_base(XPriorityQueue* queue, void* pvData)
```

入队(拷贝语义)，自动调整堆结构。

**参数:**
- `queue` - XPriorityQueue指针
- `pvData` - 待入队元素的指针

**返回值:** 成功返回true，失败返回false

---

##### XPriorityQueue_Push_Base

```c
#define XPriorityQueue_Push_Base(queue, Type, value) { Type t = value; XPriorityQueue_push_base(queue, &t); }
```

类型安全入队宏。

---

#### 出队操作

##### XPriorityQueue_pop_base

```c
void XPriorityQueue_pop_base(XPriorityQueue* queue)
```

出队(删除优先级最高的元素)。

**参数:**
- `queue` - XPriorityQueue指针

**返回值:** 无

---

##### XPriorityQueue_receive_base

```c
bool XPriorityQueue_receive_base(XPriorityQueue* queue, void* pvBuffer)
```

获取优先级最高元素并出队。

**参数:**
- `queue` - XPriorityQueue指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，队列为空返回false

---

#### 元素访问

##### XPriorityQueue_top_base

```c
void* XPriorityQueue_top_base(XPriorityQueue* queue)
```

获取优先级最高元素的指针。

**参数:**
- `queue` - XPriorityQueue指针

**返回值:** 成功返回元素指针，队列为空返回NULL

---

##### XPriorityQueue_Top_Base

```c
#define XPriorityQueue_Top_Base(queue, Type) (*(Type*)XPriorityQueue_top_base(queue))
```

类型安全获取优先级最高元素。

---

##### XPriorityQueue_remove

```c
size_t XPriorityQueue_remove(XPriorityQueue* queue, const void* value, size_t n)
```

移除指定元素。

**参数:**
- `queue` - XPriorityQueue指针
- `value` - 待移除元素的指针
- `n` - 最多移除数量(0表示移除所有匹配元素)

**返回值:** 实际移除的元素数量

---

#### 容量操作

##### XPriorityQueue_size_base

```c
size_t XPriorityQueue_size_base(const XPriorityQueue* queue)
```

获取元素数量。

**参数:**
- `queue` - XPriorityQueue指针

**返回值:** 返回元素数量

---

##### XPriorityQueue_isEmpty_base

```c
bool XPriorityQueue_isEmpty_base(const XPriorityQueue* queue)
```

判断队列是否为空。

**参数:**
- `queue` - XPriorityQueue指针

**返回值:** 为空返回true，否则返回false

---

### XCircularQueue 环形队列

环形队列容器，固定大小循环缓冲区，支持可选的自动扩容。

#### 头文件

```c
#include "XCircularQueue.h"
```

#### 特点

- 固定容量，内存连续
- 支持可选的自动扩容模式
- 高效的入队出队操作
- 适合数据流缓冲场景

#### 创建与销毁

##### XCircularQueue_create

```c
XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count)
```

创建一个环形队列对象。

**参数:**
- `typeSize` - 元素类型大小(字节)
- `count` - 队列的初始容量

**返回值:** 成功返回XCircularQueue指针，失败返回NULL

---

##### XCircularQueue_Create

```c
#define XCircularQueue_Create(Type, count) XCircularQueue_create(sizeof(Type), count)
```

类型安全创建宏。

---

##### XCircularQueue_init

```c
void XCircularQueue_init(XCircularQueue* queue, size_t typeSize, size_t count)
```

初始化已分配内存的环形队列对象。

**参数:**
- `queue` - 待初始化的XCircularQueue指针
- `typeSize` - 元素类型大小(字节)
- `count` - 队列的初始容量

**返回值:** 无

---

##### XCircularQueue_delete_base

```c
void XCircularQueue_delete_base(XCircularQueue* queue)
```

销毁环形队列并释放所有内存。

**参数:**
- `queue` - XCircularQueue指针，可为NULL

**返回值:** 无

---

#### 配置操作

##### XCircularQueue_setAutoExpansion

```c
void XCircularQueue_setAutoExpansion(XCircularQueue* queue, bool autoExpansion)
```

设置是否开启自动扩容。

**参数:**
- `queue` - XCircularQueue指针
- `autoExpansion` - true开启，false关闭

**返回值:** 无

**注意:** 开启后，队列满时会自动扩容至原容量的1.5倍

---

#### 入队操作

##### XCircularQueue_push_base

```c
bool XCircularQueue_push_base(XCircularQueue* queue, void* pvData)
```

入队(拷贝语义)。

**参数:**
- `queue` - XCircularQueue指针
- `pvData` - 待入队元素的指针

**返回值:** 成功返回true，队列已满返回false

---

##### XCircularQueue_Push_Base

```c
#define XCircularQueue_Push_Base(queue, Type, value) { Type t = value; XCircularQueue_push_base(queue, &t); }
```

类型安全入队宏。

---

#### 出队操作

##### XCircularQueue_pop_base

```c
void XCircularQueue_pop_base(XCircularQueue* queue)
```

出队(删除队头元素)。

**参数:**
- `queue` - XCircularQueue指针

**返回值:** 无

---

##### XCircularQueue_receive_base

```c
bool XCircularQueue_receive_base(XCircularQueue* queue, void* pvBuffer)
```

获取队头元素并出队。

**参数:**
- `queue` - XCircularQueue指针
- `pvBuffer` - 接收元素数据的缓冲区

**返回值:** 成功返回true，队列为空返回false

---

#### 元素访问

##### XCircularQueue_top_base

```c
void* XCircularQueue_top_base(XCircularQueue* queue)
```

获取队头元素指针。

**参数:**
- `queue` - XCircularQueue指针

**返回值:** 成功返回队头元素指针，队列为空返回NULL

---

##### XCircularQueue_Top_Base

```c
#define XCircularQueue_Top_Base(queue, Type) (*(Type*)XCircularQueue_top_base(queue))
```

类型安全获取队头元素。

---

#### 状态查询

##### XCircularQueue_isFull_base

```c
bool XCircularQueue_isFull_base(const XCircularQueue* queue)
```

判断队列是否已满。

**参数:**
- `queue` - XCircularQueue指针

**返回值:** 已满返回true，否则返回false

---

##### XCircularQueue_isEmpty_base

```c
bool XCircularQueue_isEmpty_base(const XCircularQueue* queue)
```

判断队列是否为空。

**参数:**
- `queue` - XCircularQueue指针

**返回值:** 为空返回true，否则返回false

---

##### XCircularQueue_size_base

```c
size_t XCircularQueue_size_base(const XCircularQueue* queue)
```

获取元素数量。

**参数:**
- `queue` - XCircularQueue指针

**返回值:** 返回元素数量

---

##### XCircularQueue_capacity_base

```c
size_t XCircularQueue_capacity_base(const XCircularQueue* queue)
```

获取队列容量。

**参数:**
- `queue` - XCircularQueue指针

**返回值:** 返回队列容量

---

## 特殊容器

### XBitArray 比特数组

比特数组容器，支持位级别的操作，适用于标志位集合、位图等场景。

#### 头文件

```c
#include "XBitArray.h"
```

#### 创建与销毁

##### XBitArray_create

```c
XBitArray* XBitArray_create(size_t initialBitCount)
```

创建一个指定比特数量的比特数组。

**参数:**
- `initialBitCount` - 初始比特数量

**返回值:** 成功返回XBitArray指针，失败返回NULL

---

##### XBitArray_Create

```c
#define XBitArray_Create(initialBits) XBitArray_create(initialBits)
```

简化创建宏。

---

##### XBitArray_create_copy

```c
XBitArray* XBitArray_create_copy(const XBitArray* other)
```

通过拷贝另一个比特数组创建新对象。

**参数:**
- `other` - 源比特数组指针

**返回值:** 成功返回新XBitArray指针，失败返回NULL

---

##### XBitArray_init

```c
void XBitArray_init(XBitArray* array, size_t initialBitCount, bool useCow)
```

初始化已分配内存的比特数组对象。

**参数:**
- `array` - 待初始化的XBitArray指针
- `initialBitCount` - 初始比特数量
- `useCow` - 是否启用COW

**返回值:** 无

---

##### XBitArray_delete_base

```c
void XBitArray_delete_base(XBitArray* array)
```

销毁比特数组并释放所有内存。

**参数:**
- `array` - XBitArray指针，可为NULL

**返回值:** 无

---

#### 位操作

##### XBitArray_setBit

```c
bool XBitArray_setBit(XBitArray* array, size_t index, bool value)
```

设置指定索引的比特值。

**参数:**
- `array` - XBitArray指针
- `index` - 比特索引(从0开始)
- `value` - 要设置的值(true为1，false为0)

**返回值:** 成功返回true，索引越界返回false

---

##### XBitArray_getBit

```c
bool XBitArray_getBit(const XBitArray* array, size_t index)
```

获取指定索引的比特值。

**参数:**
- `array` - XBitArray指针
- `index` - 比特索引(从0开始)

**返回值:** 返回比特值(true为1，false为0)

---

##### XBitArray_toggleBit

```c
bool XBitArray_toggleBit(XBitArray* array, size_t index)
```

翻转指定索引的比特值(0变1，1变0)。

**参数:**
- `array` - XBitArray指针
- `index` - 比特索引(从0开始)

**返回值:** 成功返回true，索引越界返回false

---

##### XBitArray_fill

```c
void XBitArray_fill(XBitArray* array, bool value)
```

用指定值填充整个比特数组。

**参数:**
- `array` - XBitArray指针
- `value` - 填充值(true为1，false为0)

**返回值:** 无

---

#### 批量操作

##### XBitArray_writeBits

```c
bool XBitArray_writeBits(XBitArray* array, size_t startIndex, size_t bitCount, const uint8_t* src, size_t srcByteLen)
```

从指定索引开始写入指定比特数的数据。

**参数:**
- `array` - XBitArray指针
- `startIndex` - 起始写入索引
- `bitCount` - 要写入的比特数量
- `src` - 源uint8数组
- `srcByteLen` - 源数组的字节长度

**返回值:** 成功返回true，失败返回false

---

##### XBitArray_readBits

```c
bool XBitArray_readBits(const XBitArray* array, size_t startIndex, size_t bitCount, uint8_t* dest, size_t destByteLen)
```

从指定索引开始读取指定比特数的数据。

**参数:**
- `array` - XBitArray指针
- `startIndex` - 起始读取索引
- `bitCount` - 要读取的比特数量
- `dest` - 目标uint8数组
- `destByteLen` - 目标数组的字节长度

**返回值:** 成功返回true，失败返回false

---

#### 其他操作

##### XBitArray_resize

```c
bool XBitArray_resize(XBitArray* array, size_t newBitCount)
```

调整比特数组的大小。

**参数:**
- `array` - XBitArray指针
- `newBitCount` - 新的比特数量

**返回值:** 成功返回true，失败返回false

---

##### XBitArray_truncate

```c
void XBitArray_truncate(XBitArray* array, int64_t pos)
```

截断比特数组到指定位置。

**参数:**
- `array` - XBitArray指针
- `pos` - 截断位置(负数则清空数组)

**返回值:** 无

---

##### XBitArray_size_base

```c
size_t XBitArray_size_base(const XBitArray* array)
```

获取比特数量。

**参数:**
- `array` - XBitArray指针

**返回值:** 返回比特总数

---

### XRingBuffer 环形缓冲区

环形缓冲区容器，动态扩容，由多个固定大小的chunk组成，适用于数据流缓冲场景。

#### 头文件

```c
#include "XRingBuffer.h"
```

#### 特点

- 动态扩容，无固定容量上限
- 由多个固定大小的chunk组成
- 支持标记/回滚操作
- 适合网络数据流缓冲

#### 创建与销毁

##### XRingBuffer_create

```c
XRingBuffer* XRingBuffer_create(size_t chunkSize)
```

创建一个环形缓冲区对象。

**参数:**
- `chunkSize` - 每个内部chunk的大小(字节)

**返回值:** 成功返回XRingBuffer指针，失败返回NULL

---

##### XRingBuffer_init

```c
void XRingBuffer_init(XRingBuffer* buffer, size_t chunkSize)
```

初始化已分配内存的环形缓冲区对象。

**参数:**
- `buffer` - 待初始化的XRingBuffer指针
- `chunkSize` - 每个内部chunk的大小(字节)

**返回值:** 无

---

##### XRingBuffer_delete_base

```c
void XRingBuffer_delete_base(XRingBuffer* buffer)
```

销毁环形缓冲区并释放所有内存。

**参数:**
- `buffer` - XRingBuffer指针，可为NULL

**返回值:** 无

---

#### 读写操作

##### XRingBuffer_write

```c
size_t XRingBuffer_write(XRingBuffer* buffer, const void* data, size_t size)
```

向环形缓冲区写入数据。

**参数:**
- `buffer` - XRingBuffer指针
- `data` - 源数据指针
- `size` - 要写入的数据大小(字节)

**返回值:** 实际成功写入的数据大小

---

##### XRingBuffer_read

```c
size_t XRingBuffer_read(XRingBuffer* buffer, void* buffer_out, size_t size)
```

从环形缓冲区读取数据。

**参数:**
- `buffer` - XRingBuffer指针
- `buffer_out` - 目标缓冲区
- `size` - 要读取的数据大小(字节)

**返回值:** 实际成功读取的数据大小

---

##### XRingBuffer_peek

```c
size_t XRingBuffer_peek(XRingBuffer* buffer, void* buffer_out, size_t size)
```

查看(窥探)环形缓冲区中的数据(不移动读指针)。

**参数:**
- `buffer` - XRingBuffer指针
- `buffer_out` - 目标缓冲区
- `size` - 要查看的数据大小(字节)

**返回值:** 实际成功查看的数据大小

---

##### XRingBuffer_skip

```c
void XRingBuffer_skip(XRingBuffer* buffer, size_t size)
```

跳过指定字节数的数据。

**参数:**
- `buffer` - XRingBuffer指针
- `size` - 要跳过的字节数

**返回值:** 无

---

#### 标记操作

##### XRingBuffer_mark

```c
void XRingBuffer_mark(XRingBuffer* buffer)
```

在当前读取位置设置标记。

**参数:**
- `buffer` - XRingBuffer指针

**返回值:** 无

---

##### XRingBuffer_resetToMark

```c
void XRingBuffer_resetToMark(XRingBuffer* buffer)
```

将读取状态重置到最近一次mark()调用时的状态。

**参数:**
- `buffer` - XRingBuffer指针

**返回值:** 无

---

#### 状态查询

##### XRingBuffer_available

```c
size_t XRingBuffer_available(const XRingBuffer* buffer)
```

获取可读取的数据量。

**参数:**
- `buffer` - XRingBuffer指针

**返回值:** 返回可读取的数据量(字节)

---

##### XRingBuffer_writeable

```c
size_t XRingBuffer_writeable(const XRingBuffer* buffer)
```

获取当前写入chunk的剩余可写空间。

**参数:**
- `buffer` - XRingBuffer指针

**返回值:** 返回当前写入chunk的剩余空间(字节)

---

##### XRingBuffer_reset

```c
void XRingBuffer_reset(XRingBuffer* buffer)
```

重置环形缓冲区。

**参数:**
- `buffer` - XRingBuffer指针

**返回值:** 无

---

### XRingChunk 环形块

环形块容器，固定大小的环形缓冲区，支持标记/回滚操作。

#### 头文件

```c
#include "XRingChunk.h"
```

#### 特点

- 固定容量，内存连续
- 支持标记/回滚操作
- 支持数据退回(unget)操作
- 适合作为XRingBuffer的内部存储单元

#### 创建与销毁

##### XRingChunk_create

```c
XRingChunk* XRingChunk_create(size_t capacity)
```

创建一个指定容量的环形块对象。

**参数:**
- `capacity` - 环形缓冲区的逻辑容量(字节)

**返回值:** 成功返回XRingChunk指针，失败返回NULL

---

##### XRingChunk_create_copy

```c
XRingChunk* XRingChunk_create_copy(XRingChunk* src)
```

通过拷贝另一个环形块创建新对象。

**参数:**
- `src` - 源环形块指针

**返回值:** 成功返回新XRingChunk指针，失败返回NULL

---

##### XRingChunk_init

```c
void XRingChunk_init(XRingChunk* chunk, size_t capacity)
```

初始化已分配内存的环形块对象。

**参数:**
- `chunk` - 待初始化的XRingChunk指针
- `capacity` - 环形缓冲区的逻辑容量(字节)

**返回值:** 无

---

##### XRingChunk_delete_base

```c
void XRingChunk_delete_base(XRingChunk* chunk)
```

销毁环形块并释放所有内存。

**参数:**
- `chunk` - XRingChunk指针，可为NULL

**返回值:** 无

---

#### 读写操作

##### XRingChunk_write

```c
size_t XRingChunk_write(XRingChunk* chunk, const void* data, size_t size)
```

向环形块写入数据。

**参数:**
- `chunk` - XRingChunk指针
- `data` - 源数据指针
- `size` - 要写入的数据大小(字节)

**返回值:** 实际成功写入的数据大小

---

##### XRingChunk_read

```c
size_t XRingChunk_read(XRingChunk* chunk, void* buffer, size_t size)
```

从环形块读取数据。

**参数:**
- `chunk` - XRingChunk指针
- `buffer` - 目标缓冲区
- `size` - 要读取的数据大小(字节)

**返回值:** 实际成功读取的数据大小

---

##### XRingChunk_peek

```c
size_t XRingChunk_peek(XRingChunk* chunk, void* buffer, size_t size)
```

查看(窥探)环形块中的数据(不移动读指针)。

**参数:**
- `chunk` - XRingChunk指针
- `buffer` - 目标缓冲区
- `size` - 要查看的数据大小(字节)

**返回值:** 实际成功查看的数据大小

---

##### XRingChunk_skip

```c
void XRingChunk_skip(XRingChunk* chunk, size_t size)
```

跳过指定字节数的数据。

**参数:**
- `chunk` - XRingChunk指针
- `size` - 要跳过的字节数

**返回值:** 无

---

##### XRingChunk_unget

```c
size_t XRingChunk_unget(XRingChunk* chunk, const void* data, size_t size)
```

将数据退回到读取位置之前。

**参数:**
- `chunk` - XRingChunk指针
- `data` - 要退回的数据
- `size` - 要退回的数据大小(字节)

**返回值:** 实际成功退回的数据大小

---

#### 标记操作

##### XRingChunk_mark

```c
void XRingChunk_mark(XRingChunk* chunk)
```

在当前读取位置设置标记。

**参数:**
- `chunk` - XRingChunk指针

**返回值:** 无

---

##### XRingChunk_resetToMark

```c
void XRingChunk_resetToMark(XRingChunk* chunk)
```

将读取位置重置到最近一次mark()调用时的位置。

**参数:**
- `chunk` - XRingChunk指针

**返回值:** 无

---

#### 状态查询

##### XRingChunk_available

```c
size_t XRingChunk_available(const XRingChunk* chunk)
```

获取可读取的数据量。

**参数:**
- `chunk` - XRingChunk指针

**返回值:** 返回可读取的数据量(字节)

---

##### XRingChunk_reset

```c
void XRingChunk_reset(XRingChunk* chunk)
```

重置环形块。

**参数:**
- `chunk` - XRingChunk指针

**返回值:** 无

---

## 迭代器

XinYueC容器库为每个容器提供了正向迭代器和反向迭代器，支持统一的遍历接口。

### 迭代器命名规范

| 迭代器类型 | 命名格式 | 示例 |
|-----------|---------|------|
| 正向迭代器 | `容器名_iterator` | `XVector_iterator` |
| 反向迭代器 | `容器名_reverse_iterator` | `XVector_reverse_iterator` |

### 迭代器基本操作

#### 获取迭代器

| 函数 | 说明 |
|------|------|
| `容器名_begin(container)` | 获取指向第一个元素的迭代器 |
| `容器名_end(container)` | 获取结束迭代器(哨兵位置) |
| `容器名_rbegin(container)` | 获取反向第一个元素的迭代器 |
| `容器名_rend(container)` | 获取反向结束迭代器 |

#### 迭代器操作

| 函数 | 说明 |
|------|------|
| `容器名_iterator_add(container, it)` | 将迭代器移动到下一个元素 |
| `容器名_iterator_equality(it1, it2)` | 判断两个迭代器是否相等 |
| `容器名_iterator_data(it)` | 获取迭代器当前指向的元素数据指针 |
| `容器名_iterator_isEnd(it)` | 判断迭代器是否已到达末尾 |

### 遍历宏

#### 正向遍历

```c
for_each_iterator(container, ContainerType, it) {
    void* data = ContainerType_iterator_data(&it);
    // 处理数据...
}
```

#### 反向遍历

```c
for_each_reverse_iterator(container, ContainerType, it) {
    void* data = ContainerType_reverse_iterator_data(&it);
    // 处理数据...
}
```

### 示例：遍历XVector

```c
XVector* vec = XVector_Create(int);
// ... 添加元素 ...

// 使用遍历宏
for_each_iterator(vec, XVector, it) {
    int* data = (int*)XVector_iterator_data(&it);
    printf("%d ", *data);
}

// 使用迭代器函数
for (XVector_iterator it = XVector_begin(vec), endIt = XVector_end(vec);
     !XVector_iterator_equality(&it, &endIt);
     XVector_iterator_add(vec, &it)) {
    int* data = (int*)XVector_iterator_data(&it);
    printf("%d ", *data);
}
```

---

## 内存管理

XinYueC容器库提供了灵活的内存管理机制，支持自定义内存分配器。

### 头文件

```c
#include "XMemory.h"
```

### 内存类型

| 类型 | 说明 |
|------|------|
| `XMEMORY_TYPE_SYSTEM` | 使用系统malloc/free |
| `XMEMORY_TYPE_MULTIPOOL` | 使用内存池 |
| `XMEMORY_TYPE_HYBRID` | 组合模式：小内存用内存池，大内存用系统 |

### 内存函数

#### XMemory_malloc

```c
void* XMemory_malloc(size_t size, XMemoryType type)
```

申请内存。

**参数:**
- `size` - 申请的内存大小(字节)
- `type` - 内存类型

**返回值:** 成功返回内存块指针，失败返回NULL

---

#### XMemory_free

```c
void XMemory_free(void* ptr, XMemoryType type)
```

释放内存。

**参数:**
- `ptr` - 待释放的内存块指针
- `type` - 内存类型

**返回值:** 无

---

#### XMemory_realloc

```c
void* XMemory_realloc(void* ptr, size_t size, XMemoryType type)
```

重新分配内存。

**参数:**
- `ptr` - 原内存块指针
- `size` - 新的内存大小(字节)
- `type` - 内存类型

**返回值:** 成功返回新内存块指针，失败返回NULL

---

#### XMemory_calloc

```c
void* XMemory_calloc(size_t count, size_t size, XMemoryType type)
```

申请并初始化为零的内存。

**参数:**
- `count` - 元素数量
- `size` - 单个元素大小(字节)
- `type` - 内存类型

**返回值:** 成功返回内存块指针，失败返回NULL

---

### 快捷宏

| 宏 | 说明 |
|----|------|
| `XMalloc(size, type)` | 申请内存 |
| `XFree(ptr, type)` | 释放内存 |
| `XRealloc(ptr, size, type)` | 重分配内存 |
| `XCalloc(count, size, type)` | 零初始化分配 |
| `XNew(type)` | 申请指定类型对象的内存 |
| `XDelete(ptr)` | 释放内存 |

### 自定义内存管理器

```c
XMemory myMemory = {
    .malloc = my_malloc,
    .free = my_free,
    .realloc = my_realloc,
    .calloc = my_calloc
};
XMemory_setMethod(&myMemory, XMEMORY_TYPE_SYSTEM);
```

---

## 比较函数

XinYueC容器库使用比较函数进行元素比较，支持自定义比较规则。

### 头文件

```c
#include "XCompare.h"
```

### 比较函数类型

```c
typedef int32_t (*XCompare)(const void* lhs, const void* rhs);
```

### 返回值

| 返回值 | 说明 |
|--------|------|
| `XCompare_Less` (-1) | lhs < rhs |
| `XCompare_Equality` (0) | lhs == rhs |
| `XCompare_Greater` (1) | lhs > rhs |

### 内置比较函数

| 函数 | 说明 |
|------|------|
| `bool_compare` | bool类型比较 |
| `char_compare` | char类型比较 |
| `int_compare` | int类型比较 |
| `long_compare` | long类型比较 |
| `float_compare` | float类型比较 |
| `double_compare` | double类型比较 |
| `int8_t_compare` | int8_t类型比较 |
| `int16_t_compare` | int16_t类型比较 |
| `int32_t_compare` | int32_t类型比较 |
| `int64_t_compare` | int64_t类型比较 |
| `uint8_t_compare` | uint8_t类型比较 |
| `uint16_t_compare` | uint16_t类型比较 |
| `uint32_t_compare` | uint32_t类型比较 |
| `uint64_t_compare` | uint64_t类型比较 |
| `size_t_compare` | size_t类型比较 |

### 定义自定义比较函数

#### 使用宏定义

```c
// 声明
XCompare_Define(MyStruct);

// 实现
XCompare_Come(MyStruct);
```

#### 手动实现

```c
int32_t MyStruct_compare(const MyStruct* lhs, const MyStruct* rhs) {
    if (lhs->id < rhs->id) return XCompare_Less;
    if (lhs->id > rhs->id) return XCompare_Greater;
    return XCompare_Equality;
}
```

### 排序顺序

```c
typedef enum {
    XSORT_ASC,   // 升序（从小到大）
    XSORT_DESC   // 降序（从大到小）
} XSortOrder;
```

---

## 附录

### 容器时间复杂度

| 容器 | 随机访问 | 头部插入 | 尾部插入 | 查找 |
|------|---------|---------|---------|------|
| XVector | O(1) | O(n) | O(1)* | O(n) |
| XListSLinked | O(n) | O(1) | O(n) | O(n) |
| XListDLinked | O(n) | O(1) | O(1) | O(n) |
| XMap | O(log n) | O(log n) | O(log n) | O(log n) |
| XHashMap | O(1)* | O(1)* | O(1)* | O(1)* |
| XSet | O(log n) | O(log n) | O(log n) | O(log n) |
| XHashSet | O(1)* | O(1)* | O(1)* | O(1)* |

*注：均摊复杂度或平均复杂度

### 命名规范总结

| 类型 | 命名格式 | 示例 |
|------|---------|------|
| 容器结构体 | `X容器名` | `XVector` |
| 创建函数 | `X容器名_create` | `XVector_create` |
| 类型安全宏 | `X容器名_Create` | `XVector_Create` |
| 基础操作 | `X容器名_操作_base` | `XVector_push_back_base` |
| 类型安全操作 | `X容器名_操作_Base` | `XVector_Push_Back_Base` |
| 迭代器 | `X容器名_iterator` | `XVector_iterator` |
| 反向迭代器 | `X容器名_reverse_iterator` | `XVector_reverse_iterator` |

### 错误处理

大多数函数在失败时返回以下值：
- 指针类型：返回`NULL`
- 布尔类型：返回`false`
- 整数类型：返回`-1`或`0`

### 线程安全

- **无锁容器**：`XLockFreeList`、`XLockFreeStack`、`XLockFreeQueue`是线程安全的
- **普通容器**：非线程安全，多线程环境下需要外部同步

### 使用建议

1. **选择合适的容器**
   - 需要随机访问：使用`XVector`
   - 频繁插入删除：使用`XListDLinked`
   - 需要键值对：使用`XMap`或`XHashMap`
   - 多线程环境：使用无锁容器

2. **内存管理**
   - 使用`create`函数创建对象，使用`delete_base`函数销毁
   - 栈上分配的对象使用`init`初始化，使用`deinit_base`清理

3. **性能优化**
   - 预分配容量避免频繁扩容
   - 使用移动语义减少拷贝开销
   - 选择合适的比较函数
