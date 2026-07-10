# XinYueC 库代码风格指南

## 目录
1. [命名规范](#命名规范)
2. [类的创建](#类的创建)
3. [虚函数表与虚函数重载](#虚函数表与虚函数重载)
4. [对象生命周期管理](#对象生命周期管理)
5. [API 设计规范](#api-设计规范)
6. [内存管理注意事项](#内存管理注意事项)
7. [错误处理与断言](#错误处理与断言)
8. [文件组织规范](#文件组织规范)
9. [平台适配规范](#平台适配规范)
10. [第三方库集成规范](#第三方库集成规范)
11. [容器与数据结构规范](#容器与数据结构规范)
12. [信号与槽规范](#信号与槽规范)
13. [常见错误与最佳实践](#常见错误与最佳实践)

---

## 命名规范

### 类型命名
- **结构体/类**：`X` 前缀 + 大驼峰命名法
  ```c
  typedef struct XHostAddress { ... } XHostAddress;
  typedef struct XString { ... } XString;
  typedef struct XNetworkAddressEntry { ... } XNetworkAddressEntry;
  ```

- **枚举类型**：`X` 前缀 + 大驼峰命名法
  ```c
  typedef enum XHostAddress_NetworkLayerProtocol {
      XHostAddress_UnknownNetworkLayerProtocol = -1,
      XHostAddress_IPv4Protocol = 0,
      XHostAddress_IPv6Protocol = 1
  } XHostAddress_NetworkLayerProtocol;
  ```

- **枚举值**：类型名 + `_` + 大驼峰命名
  ```c
  XHostAddress_IPv4Protocol
  XHostAddress_IPv6Protocol
  XNetworkAddressEntry_DnsEligible
  ```

### 函数命名
- **格式**：`X类型名_功能名`
- **示例**：
  ```c
  XHostAddress_init()
  XHostAddress_setAddress()
  XHostAddress_toString()
  XString_append_utf8()
  XNetworkAddressEntry_createFull()
  ```

### 成员变量命名
- **格式**：`m_` 前缀 + 小驼峰命名
  ```c
  typedef struct XExample {
      XClass m_class;          // 基类（m_class 必须位于第一位）
      int m_value;             // 基本类型成员
      char* m_data;            // 指针成员
      uint32_t m_useCow : 1;   // 位域成员
      uint32_t m_isRun : 1;
  } XExample;
  ```

### 文件命名
- **公共头文件**：`XClassName.h`（类名与文件名一致）
  ```
  XClass.h / XClass.c
  XObject.h / XObject.c
  XVtable.h / XVtable.c
  ```
- **虚函数实现文件**（可选）：`XClassName_virtual.c`（虚函数实现与业务逻辑分离时使用）
- **内部保护头文件**：`XClassName_Protected.h`（暴露子类需要的内部接口，不对外公开）
- **平台适配文件**：两种风格并存（见 [平台适配规范](#平台适配规范)）

### 内部函数命名（static 虚函数实现）
- **格式**：`V` + 类名 + `_` + 函数名
  ```c
  static void VXExample_deinit(XExample* obj);   // deinit 的虚函数实现
  static void VXExample_copy(XExample* dest, const XExample* src);
  static bool VXExample_event(XExample* self, XEvent* e);
  ```
- 这些函数是 static 的，仅供 `XClassName_class_init()` 中注册虚函数表使用

### 宏命名
- **全大写 + 下划线分隔**
  ```c
  #define XCLASS_VTABLE_SIZE
  #define XVTABLE_CREAT_DEFAULT
  #define XString_copy_base
  ```

### 特殊后缀
| 后缀 | 含义 | 示例 |
|------|------|------|
| `_base` | 所有虚函数的公共调度入口 | `XHostAddress_deinit_base()`、`XExample_event_base()` |
| `_utf8` | UTF-8 编码版本 | `XString_append_utf8()` |
| `_gbk` | GBK 编码版本 | `XString_create_gbk()` |
| `_utf16` | UTF-16 编码版本 | 编码缓存相关 |
| `_move` | 移动语义（转移资源所有权） | `XExample_setData_move()`、`XContainer_create_move()` |
| `_copy` | 深拷贝创建 | `XContainer_create_copy()` |
| `_const` | 返回内部引用（非拷贝），const 语义 | `const char* XExample_data_const(...)` |
| `_ref` | 引用语义（不拷贝，直接引用传入数据） | `XExample_setData_ref_2(obj, char* value)` |
| `_ref_2` | 带引用的重载版本（ref + 数字后缀） | |
| `_2`、`_3`... | 同功能不同参数的重载版本 | `XExample_setData_2()` |
| `_ex` | 扩展构造函数（比默认多参数） | `XContainer_create_ex(typeSize, useCow)` |
| `_later` | 延迟操作（通过事件循环执行） | `XObject_deleteLater()` |

### 类型别名
- **格式**：用 `typedef` 给泛型容器定义语义化别名
  ```c
  typedef XMapBase XFuncCodeMap;       // 功能码映射
  typedef XMap XVariantMap;            // Variant 键值映射
  typedef XHashMap XVariantHashMap;    // Variant 哈希映射
  ```

### 宏命名
- **全大写 + 下划线分隔**
  ```c
  #define XCLASS_VTABLE_SIZE
  #define XVTABLE_CREAT_DEFAULT
  #define ISNULL(args, str)
  #define XAssert(args, str)
  #define XNew(Type)
  ```
- **容器创建宏**：`_Create` 后缀自动推导 sizeof
  ```c
  #define XVector_Create(Type) XVector_create_ex(sizeof(Type), true)
  #define XString_Init_Utf8(str, utf8) ...
  #define XString_Init_Fmt_Utf8(msg, fmt, ...) ...
  #define XPair_Create(firstType, secondType) ...
  ```
- **条件编译开关**：`模块名_ON` 或 `模块名_USE_库名`
  ```c
  #define XContainer_ON 1
  #define XMap_ON 1
  #define XFILE_USE_FATFS
  #define XNETWORK_USE_LWIP
  #define XCHAR_USE_SYSTEM_GBK
  #define VTABLE_ISSTACK
  ```

---

## 类的创建

### 1. 头文件结构

```c
// XExample.h
#ifndef XEXAMPLE_H
#define XEXAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"

// ==================== 虚函数表定义，增加自己的虚函数表 ====================
XCLASS_DEFINE_BEGING(XExample)
XCLASS_DEFINE_ENUM(XExample, Event) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XExample, ChildEvent),
XCLASS_DEFINE_END(XExample)
// ==================== 虚函数表定义，仅继承父类 ====================
XCLASS_DEFINE_BEGING(XExample)
XCLASS_DEFINE_EXTEND_END(XExample, XClass)
// ==================== 结构体定义 ====================
typedef struct XExample {
    XClass m_class;          // 基类（必须放在第一位）
    int m_value;             // 成员变量
    char* m_data;            // 动态分配的成员
} XExample;

// ==================== 虚函数表初始化函数 ====================
XVtable* XExample_class_init(void);

// ==================== 构造与析构函数 ====================
void XExample_init(XExample* obj);
XExample* XExample_create(void);//无参构造
XExample* XExample_create_copy(const XExample* other);//拷贝构造
XExample* XExample_create_move(XExample* other);//移动构造
#define XExample_deinit_base XClass_deinit_base //反初始化,用宏复用父类
#define XExample_delete_base XClass_delete_base //释放，用宏复用父类
#define XExample_copy_base XClass_copy_base //拷贝，用宏复用父类
#define XExample_move_base XClass_move_base //移动，用宏复用父类

// ==================== 功能函数 ====================
void XExample_setValue(XExample* obj, int value);
void XExample_setData(XExample* obj, XString* value);
void XExample_setData_move(XExample* obj, XString* value);//对于类有移动api提供，所以有了move后缀，可以调用移动操作
void XExample_setData_2(XExample* obj, char* value);//重载函数加数字后缀，默认是拷贝
void XExample_setData_ref_2(XExample* obj, char* value);//重载函数加数字后缀，带ref是引用
int XExample_value(const XExample* obj);
char*XExample_value(const XExample* obj);//默认返回的都是拷贝
const char*XExample_data_const(const XExample* obj);//const后缀函数返回内部引用    
// ==================== 自己增加的虚函数 ====================
bool XExample_event_base(XExample* self, XEvent* e);
void XExample_childEvent_base(XExample* self,XChildEvent* event);
#ifdef __cplusplus
}
#endif
#endif // XEXAMPLE_H
```

### 2. 源文件结构

```c
// XExample.c
#include "XExample.h"
#include "XMemory.h"
#include <string.h>

// ==================== 虚函数实现 ====================
static bool VXExample_event(XExample* self, XEvent* e)
{
    //这边实现自己的虚函数逻辑
}
static void VXExample_childEvent(XExample* self,XChildEvent* event)
{
     //这边实现自己的虚函数逻辑
}
static void VXExample_deinit(XExample* obj)
{
    if (!obj) return;
    
    // 释放动态分配的资源
    if (obj->m_data) {
        XFree_System(obj->m_data);
        obj->m_data = NULL;
    }
    
    // 调用父类的 deinit（如果需要）
    // XClass_Deinit_Parent(XExample, obj);
}

static void VXExample_copy(XExample* dest, const XExample* src)
{
    if (!dest || !src) return;
    
    // 确保目标对象已初始化
    XClassEnsureVtable(dest, XExample);
    
    // 先释放目标对象的旧值
    if (dest->m_data) {
        XFree_System(dest->m_data);
        dest->m_data = NULL;
    }
    
    // 复制基本类型成员
    dest->m_value = src->m_value;
    
    // 深拷贝动态分配的成员
    if (src->m_data) {
        dest->m_data = XStrdup(src->m_data);
    }
    
    //对于成员也是类的，可以直接调用成员的拷贝函数，而无需先释放.
    //成员未初始化的时候可以直接调用成员的拷贝构造.
}

static void VXExample_move(XExample* dest, XExample* src)
{
    if (!dest || !src) return;
    
    // 确保目标对象已初始化
    XClassEnsureVtable(dest, XExample);
    
    // 转移资源
    dest->m_value = src->m_value;
    dest->m_data = src->m_data;
    
    // 清空源对象
    src->m_value = 0;
    src->m_data = NULL;
    //对于成员也是类的，可以直接调用成员的移动函数，而无需先释放.
    //成员未初始化的时候可以直接调用成员的移动构造.
}

// ==================== 虚函数表初始化 ====================

XVtable* XExample_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XExample))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);                    // 继承父类虚函数表
    	void* table[] = { 
		VXExample_event，VXExample_childEvent };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);//添加自己新增的虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit);   // 重载 deinit
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXExample_copy);       // 重载 copy
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXExample_move);       // 重载 move
    return XVTABLE_DEFAULT;
}

// ==================== 构造函数 ====================

void XExample_init(XExample* obj)
{
    if (!obj) return;
    
    // 清零内存
    memset(obj, 0, sizeof(XExample));
    
    // 初始化基类
    XClass_init(obj);
    XClassSetVtable(obj, XExample);
    
    // 初始化成员变量
    obj->m_value = 0;
    obj->m_data = NULL;
}

XExample* XExample_create(void)
{
    XExample* obj = (XExample*)XMalloc_System(sizeof(XExample));
    if (!obj) return NULL;
    
    XExample_init(obj);
    Set_Class_MemoryFree(obj, XFree_System);  // 标记为堆分配
    return obj;
}

XExample* XExample_create_copy(const XExample* other)
{
    if (!other) return NULL;
    
    XExample* obj = XExample_create();
    if (!obj) return NULL;
    
    XExample_copy_base(obj, other);
    return obj;
}

// ==================== 功能函数 ====================

void XExample_setValue(XExample* obj, int value)
{
    if (obj) {
        obj->m_value = value;
    }
}

int XExample_getValue(const XExample* obj)
{
    return obj ? obj->m_value : 0;
}
// ==================== 自己增加的虚函数 ====================
bool XExample_event_base(XExample* self, XEvent* e)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
	 return false;
   return XClassGetVirtualFunc(src, EXExample_Event, bool(*)(XExample*, XEvent*))(self,e);
}
void XExample_childEvent_base(XExample* self,XChildEvent* event)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
	 return ;
   XClassGetVirtualFunc(src, EXExample_ChildEvent, bool(*)(XExample*, XChildEvent*))(self,event);
}
```

---

## 虚函数表与虚函数重载

### 虚函数表定义宏

```c
// 定义虚函数表枚举（不继承其他类）
XCLASS_DEFINE_BEGING(XClassName)
XCLASS_DEFINE_ENUM(XClassName, Func1),
XCLASS_DEFINE_ENUM(XClassName, Func2),
XCLASS_DEFINE_END(XClassName)

// 定义虚函数表枚举（继承自父类，增加自己的虚函数）
XCLASS_DEFINE_BEGING(XChildClass)
XCLASS_DEFINE_ENUM(XChildClass, ChildFunc1) = XCLASS_VTABLE_GET_SIZE(XParentClass),    // 子类新增的虚函数设置枚举值从父类开始
XCLASS_DEFINE_ENUM(XChildClass, ChildFunc2),
XCLASS_DEFINE_END(XChildClass) 
// 定义虚函数表枚举（继承自父类，不新增虚函数）
XCLASS_DEFINE_BEGING(XChildClass)
XCLASS_DEFINE_EXTEND_END(XChildClass, XParentClass)  // 指定父类    
```

### 虚函数表初始化宏

```c
XVtable* XExample_class_init(void)
{
    // 1. 创建虚函数表（单例模式）
    XVTABLE_CREAT_DEFAULT
    
    // 2. 初始化虚函数表（选择栈或堆）
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XExample))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    
    // 3. 继承父类虚函数表
    XVTABLE_INHERIT_XCLASS(XParentClass);
    // 4.新增自己的虚函数
 	void* table[] = { 
		VXExample_event，VXExample_childEvent };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 5. 重载父类虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXExample_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXExample_move);
    
    return XVTABLE_DEFAULT;
}
```

### 虚函数重载注意事项

1. **函数签名必须匹配**：虚函数的签名必须与虚函数表中定义的类型一致
2. **先继承后重载**：必须先调用 `XVTABLE_INHERIT_XCLASS`，再调用 `XVTABLE_OVERLOAD_DEFAULT`
3. **使用 `XClassGetVirtualFunc` 调用虚函数**：
   ```c
   XClassGetVirtualFunc(obj, EXClass_Deinit, void(*)(XExample*))(obj);
   ```

---

## 对象生命周期管理

### 栈上对象

```c
void stackObjectExample(void)
{
    // 1. 声明对象
    XHostAddress addr;
    
    // 2. 初始化
    XHostAddress_init(&addr);
    
    // 3. 使用对象
    XHostAddress_setAddress(&addr, "192.168.1.1");
    
    // 4. 反初始化（必须成对出现！）
    XHostAddress_deinit_base(&addr);
}
```

### 堆上对象

```c
void heapObjectExample(void)
{
    // 1. 创建对象
    XHostAddress* addr = XHostAddress_create();
    if (!addr) return;
    
    // 2. 使用对象
    XHostAddress_setAddress(addr, "192.168.1.1");
    
    // 3. 删除对象
    XHostAddress_delete_base(addr);
}
```

### init/deinit 成对原则

**重要**：`XType_init()` 和 `XType_deinit_base()` 必须成对出现！

```c
// ✅ 正确示例
void correctExample(void)
{
    XHostAddress addr;
    XHostAddress_init(&addr);
    // ... 使用对象 ...
    XHostAddress_deinit_base(&addr);  // 必须调用
}

// ❌ 错误示例
void wrongExample(void)
{
    XHostAddress addr;
    // XHostAddress_init(&addr);  // 忘记初始化
    XHostAddress_setAddress(&addr, "192.168.1.1");  // 未定义行为！
    // XHostAddress_deinit_base(&addr);  // 忘记反初始化 = 内存泄漏
}
```

---

### 快速初始化宏

对于简单场景，可以使用快捷宏：

```c
// 栈上字符串快速初始化
XString_Init_Utf8(str, "Hello World");
// 使用 str...
XString_deinit_base(str);

// 格式化字符串
XString_Init_Fmt_Utf8(msg, "Error code: %d", errorCode);
// 使用 msg...
XString_deinit_base(msg);
```

### 拷贝构造与移动构造

```c
// 拷贝构造：深拷贝创建新对象
XExample* dest = XExample_create_copy(src);

// 移动构造：转移资源所有权，创建新对象
XExample* dest = XExample_create_move(src);  // src 被清空

// 栈上拷贝
XExample dest, src;
XExample_init(&dest);
XExample_init(&src);
XExample_copy_base(&dest, &src);  // 必须先 init dest
XExample_deinit_base(&dest);
XExample_deinit_base(&src);

// 栈上移动
XExample dest, src;
XExample_init(&dest);
XExample_init(&src);
XExample_move_base(&dest, &src);  // src 资源转移到 dest
XExample_deinit_base(&dest);
// src 已为空，仍需 deinit_base 释放基类资源
XExample_deinit_base(&src);
```

### deleteLater 模式

对于被事件系统引用的对象，不能直接 `delete`，需要使用延迟删除：

```c
// 延迟删除：通过事件循环在下次事件处理时安全释放
XObject_deleteLater(obj);
```

---

## API 设计规范

### 创建函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_create()` | 默认创建（堆分配） |
| `XType_create_ex(args)` | 扩展创建（带额外参数） |
| `XType_create_copy(const XType*)` | 拷贝创建 |
| `XType_create_move(XType*)` | 移动创建 |
| `XType_init(XType*)` | 栈上初始化 |
| `XType_Create(ElementType)` | 宏版本创建（自动 sizeof） |

### 设置函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_setXxx(XType*, value)` | 设置属性（拷贝语义，默认行为） |
| `XType_setXxx_move(XType*, value)` | 移动语义版本 |
| `XType_setXxx_2(XType*, other_type)` | 不同参数类型的重载 |
| `XType_setXxx_ref_2(XType*, ptr)` | 引用语义的重载（不拷贝） |
| `XType_setXxx_utf8(XType*, const char*)` | UTF-8 编码版本 |
| `XType_setXxx_gbk(XType*, const char*)` | GBK 编码版本 |

### 获取函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_xxx(const XType*)` | 获取属性（返回值拷贝） |
| `XType_xxx_const(const XType*)` | 获取属性（返回内部 const 引用，不拷贝） |
| `XType_toXxx(const XType*)` | 转换为其他类型（返回新对象） |
| `XType_isXxx(const XType*)` | 布尔判断函数 |

### 返回值规范

- **创建函数**：成功返回对象指针，失败返回 `NULL`
- **设置函数**：成功返回 `true`，失败返回 `false`
- **获取函数**：返回值或指针，失败返回默认值（0/NULL/false）
- **判断函数**：返回 `bool` 类型

### 容器 API 设计

容器类有两套并行的 API 风格：

**风格一：小写下划线（通用风格）**
```c
XVector_push_back(vec, data);
XList_pop_front(list);
XMap_insert(map, key, value);
```

**风格二：大驼峰（部分容器使用）**
```c
XQueue_Push(queue, data);
XStack_Pop(stack);
XVector_At(vector, index);
XContainer_Swap(a, b);
```

> **注意**：新代码建议统一使用小写下划线风格。部分较早的容器代码使用大驼峰风格，重构时统一为小写下划线。

### 类型安全创建宏

泛型容器需要用宏来推导 `sizeof(Type)`，避免手动传入错误的大小：

```c
// ✅ 推荐：使用类型安全宏
XVector* vec = XVector_Create(int32_t);
XMap* map = XMap_Create(int32_t, XString*);
XPair* pair = XPair_Create(int32_t, float);

// ❌ 不推荐：手动传 sizeof
XVector* vec = XVector_create_ex(sizeof(int32_t), true);
```

---

## 内存管理注意事项

### 禁止使用 memcpy 复制对象

**错误做法**：
```c
XHostAddress dest, src;
XHostAddress_init(&src);
XHostAddress_setAddress(&src, "192.168.1.1");

// ❌ 错误！会导致浅拷贝问题
memcpy(&dest, &src, sizeof(XHostAddress));
```

**正确做法**：
```c
XHostAddress dest, src;
XHostAddress_init(&src);
XHostAddress_setAddress(&src, "192.168.1.1");

XHostAddress_init(&dest);
XHostAddress_copy_base(&dest, &src);  // ✅ 正确：使用 copy_base

XHostAddress_deinit_base(&dest);
XHostAddress_deinit_base(&src);
```

### 原因
- `XHostAddress`、`XString` 等类型可能包含动态分配的内存
- `memcpy` 只做浅拷贝，会导致：
  1. 多个对象指向同一块内存
  2. 内存泄漏（原对象的内存未被释放）
  3. 双重释放（多个对象尝试释放同一块内存）

### 使用 _base 函数

| 函数 | 用途 |
|------|------|
| `XType_copy_base(dest, src)` | 深拷贝对象 |
| `XType_move_base(dest, src)` | 移动语义（转移资源所有权） |
| `XType_deinit_base(obj)` | 反初始化对象（释放资源） |
| `XType_delete_base(obj)` | 删除堆对象（反初始化 + 释放内存） |

### 内存分配器体系

代码库提供三种内存分配器：

```c
// 系统默认分配器
void* ptr = XMalloc_System(size);
XFree_System(ptr);
void* calloced = XCalloc_System(count, size);

// 多级池分配器（适用于频繁小对象分配）
void* ptr = XMalloc_MultiPool(size);
XFree_MultiPool(ptr);

// 混合分配器
void* ptr = XMalloc_Hybrid(size);
XFree_Hybrid(ptr);

// 类型安全分配宏
XExample* obj = (XExample*)XNew(XExample);
```

### COW（Copy-On-Write）机制

容器默认启用 COW，基于 `XSharedData` + 原子引用计数：

```c
// COW 开启时，拷贝构造仅增加引用计数
XVector* v2 = XVector_create_copy(v1);  // v1 和 v2 共享底层数据

// 写操作时触发深拷贝分离
XVector_push_back(v2, &data);  // v2 自动分离，深拷贝数据
XVector_push_back(v1, &data);  // v1 不受影响

// 禁用 COW 创建
XVector* v = XVector_create_ex(sizeof(int), false);  // useCow=false
```

---

## 错误处理与断言

### ISNULL 检查模式

```c
// ISNULL(args, description_str) -- 检查指针是否为空，为空时打印错误并返回
bool XExample_event_base(XExample* self, XEvent* e)
{
    if (ISNULL(self, "XExample") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXExample_Event, bool(*)(XExample*, XEvent*))(self, e);
}

// XAssert(args, str) -- 断言，失败时调用 exit(-1)
XAssert(ptr != NULL, "Memory allocation failed");
```

> `ISNULL` 内部调用 `XERROR_PRINTF` 输出文件名、函数名和行号，然后返回 `true`（表示确实为 NULL）

### 错误日志宏

```c
// 配置文件 CXinYueConfig.h 中定义
#if XERROR_ON
    #define XERROR_PRINTF(fmt, ...) XPrintf("[ERROR] %s:%d %s(): " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define XERROR_PRINTF(fmt, ...)
#endif

#if DEBUG_ON
    #define XDEBUG_PRINTF(fmt, ...) XPrintf("[DEBUG] %s:%d %s(): " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define XDEBUG_PRINTF(fmt, ...)
#endif
```

### 返回值检查

```c
XHostAddress* addr = XHostAddress_create();
if (!addr) {
    // 处理内存分配失败
    return;
}

XString* str = XHostAddress_toString(addr);
if (str) {
    // 使用 str
    XString_delete_base(str);
}
XHostAddress_delete_base(addr);
```

---

## 文件组织规范

### 文件编码
- 所有 `.c` 和 `.h` 文件必须使用 **UTF-8 BOM**（文件开头 `\xEF\xBB\xBF`）

### 头文件结构

```c
// ===== 文件头注释（可选） =====
// XExample.h - 简要说明

// ===== 1. 头文件保护 =====
#ifndef XEXAMPLE_H
#define XEXAMPLE_H

// ===== 2. C++ 兼容 =====
#ifdef __cplusplus
extern "C" {
#endif

// ===== 3. 条件编译开关 =====
#if !defined(XEXAMPLE_H) && XExample_ON  // 由 CXinYueConfig.h 中的开关控制

// ===== 4. 包含依赖 =====
#include "CXinYueConfig.h"  // 全局配置文件（通常第一个）
#include "XClass.h"
// 其他依赖...

// ===== 5. 前向声明 / 类型定义 =====
// 枚举、结构体前向声明、typedef...

// ===== 6. 虚函数表定义 =====
XCLASS_DEFINE_BEGING(XExample)
XCLASS_DEFINE_ENUM(XExample, Event) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XExample)

// ===== 7. 结构体定义 =====
typedef struct XExample {
    XObject m_class;
    int m_value;
} XExample;

// ===== 8. 函数声明（按逻辑分组） =====
// 虚函数表初始化
XVtable* XExample_class_init(void);

// 构造/析构
void XExample_init(XExample* obj);
XExample* XExample_create(void);

// 虚函数调度（_base 函数）
bool XExample_event_base(XExample* self, XEvent* e);

// 功能函数
void XExample_setValue(XExample* obj, int value);
int XExample_value(const XExample* obj);

// ===== 9. 关闭条件编译 =====
#endif // XExample_ON

// ===== 10. 关闭 C++ 兼容 =====
#ifdef __cplusplus
}
#endif

// ===== 11. 关闭头文件保护 =====
#endif // XEXAMPLE_H
```

### 源文件结构

```c
// XExample.c

// ===== 1. 自包含头文件首先 =====
#include "XExample.h"

// ===== 2. 标准库 =====
#include <string.h>

// ===== 3. 框架库 =====
#include "XMemory.h"

// ===== 4. 静态虚函数实现 =====
static void VXExample_deinit(XExample* obj) { ... }
static void VXExample_copy(XExample* dest, const XExample* src) { ... }

// ===== 5. 虚函数表初始化（单例） =====
XVtable* XExample_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    // ... 虚函数表初始化 ...
    return XVTABLE_DEFAULT;
}

// ===== 6. 构造函数 =====
void XExample_init(XExample* obj) { ... }
XExample* XExample_create(void) { ... }

// ===== 7. 虚函数调度函数 =====
bool XExample_event_base(XExample* self, XEvent* e) { ... }

// ===== 8. 功能函数 =====
void XExample_setValue(XExample* obj, int value) { ... }
```

### 虚函数实现文件（可选分离）
当虚函数实现较多时，可分离到 `XClassName_virtual.c`：
```c
// XClass_virtual.c -- 仅包含虚函数实现
static void VXClass_deinit(XObject* obj) { ... }
static void VXClass_copy(XObject* dest, const XObject* src) { ... }

XVtable* XClass_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    // ...
    return XVTABLE_DEFAULT;
}
```

### 保护头文件
用于暴露内部接口给子类，不对外公开：
```c
// XIODevice_Protected.h
// 仅供子类（XSerialPort 等）使用的 protected virtual 函数
int64_t XIODevice_readData_base(XIODevice* device, char* data, int64_t maxSize);
int64_t XIODevice_writeData_base(XIODevice* device, const char* data, int64_t maxSize);
```

---

## 平台适配规范

### 目录结构

```
Drive/
  FreeRTOS/     -- RTOS 原语适配（mutex, semaphore, thread...）
  gcc/          -- GCC 编译器原子操作
  keil/         -- Keil/ARMCC 编译器原子操作
  msvc/         -- MSVC 编译器原子操作
  Posix/        -- POSIX 平台适配（Linux/macOS/BSD）
  STM32/        -- STM32 MCU 外设适配
  windows/      -- Win32 平台适配
    Sync/       -- 同步原语子目录
    File/       -- 文件系统子目录
    XNetwork/   -- 网络子目录
```

### 文件命名两种风格

| 风格 | 模式 | 示例 |
|------|------|------|
| **帕斯卡连接** | `X{模块}{平台}.c` | `XMutexFreeRTOS.c`、`XThreadPosix.c`、`XSerialPortWin32.c` |
| **下划线连接** | `X{模块}_{平台}.c` | `XChar_posix.c`、`XDateTime_win32.c`、`XAtomic_GCC.c` |

> **约定**：新平台适配统一使用下划线连接风格（`X{模块}_{平台}.c`）。旧代码中帕斯卡连接风格的文件逐步重命名。

### 平台条件编译

每个平台文件顶层使用对应宏守卫：

| 平台 | 守卫宏 |
|------|--------|
| FreeRTOS | `#ifdef __FreeRTOS__` |
| GCC | `#ifdef __GNUC__` |
| Keil/ARMCC | `#if defined(__CC_ARM) \|\| defined(__ARMCC_VERSION) \|\| defined(__clang__)` |
| MSVC | `#ifdef _MSC_VER` |
| POSIX | `#if defined(__linux__) \|\| defined(__APPLE__) \|\| defined(__BSD__)` |
| Win32 | `#ifdef _WIN32` |
| STM32 | `#ifdef USE_STDPERIPH_DRIVER`（内嵌套 `#ifdef STM32F40_41xxx`） |

### 平台适配模式

#### 模式 A：PlatformPrivate 不透明类型（Mutex 等）

```c
// 通用头文件 XSync.h：声明 XMutex 为不透明类型
typedef struct XMutex XMutex;

// 每个平台文件定义具体结构
// XMutexPosix.c
struct XMutex { pthread_mutex_t mutex; };

// XMutexWin32.c
struct XMutex { CRITICAL_SECTION cs; };

// 通用代码通过 PlatformPrivate 间接访问
static inline pthread_mutex_t* XMutex_get_pthread(XMutex* mutex) {
    return &mutex->mutex;  // 仅在 POSIX 平台可用
}
```

#### 模式 B：直接定义结构体（Semaphore、WaitCondition）

```c
// 平台文件直接定义完整结构
// XSemaphoreFreeRTOS.c
struct XSemaphore { SemaphoreHandle_t handle; int count; int max_count; };

// XSemaphorePosix.c
struct XSemaphore { sem_t handle; };

// 通用代码通过 _typeSize() 和 _init/_deinit 操作
size_t XSemaphore_typeSize();
bool XSemaphore_init(XSemaphore* sem, ...);
```

#### 模式 C：虚函数表（Thread、SerialPort）

```c
// 每个平台实现自己的 class_init()
// XThreadPosix.c
XVtable* XThread_class_init(void) {
    XVTABLE_CREAT_DEFAULT
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { VXThread_start, VXThread_wait, ... };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
    return XVTABLE_DEFAULT;
}
```

### 功能开关叠加

平台文件可能同时受平台宏和功能开关控制：

```c
// XFileSystem_win32.c
#if defined(_WIN32) && defined(XFILE_USE_PLATFORM_API)
// ... Windows 文件系统实现
#endif

// XNetwork_lwip_win32.c
#if defined(_WIN32) && defined(XNETWORK_USE_LWIP)
// ... lwIP 在 Windows 上的平台适配
#endif
```

---

## 第三方库集成规范

### 总体原则
第三方库代码放在 `Library/` 目录，尽量**不修改上游源码**，通过适配层与 XinYueC 框架对接。

### 三层架构

```
层次 1：上游源码（Library/<库名>/）  ← 尽量不修改
层次 2：平台抽象层（X*_<库名>_platform.h） ← 定义每个平台必须实现的接口
层次 3：适配层（X*_<库名>.c）          ← 连接上游库与 XinYueC 框架
```

### 文件命名

| 文件类型 | 命名规则 | 示例 |
|---------|---------|------|
| 适配器 | `X{子系统}_{库名}.c` | `XFileSystem_Fatfs.c`、`XNetwork_lwip.c` |
| 平台抽象头 | `X{子系统}_{库名}_platform.h` | `XFileSystem_Fatfs_platform.h`、`XNetwork_lwip_platform.h` |
| 平台实现 | `X{子系统}_{库名}_{平台}.c` | `XFileSystem_Fatfs_diskioWin32.c`、`XNetwork_lwip_win32.c` |

### 编译开关

```c
// 仅当启用对应库时才编译适配层
#if defined(XFILE_USE_FATFS)
    // FatFs 适配代码
#endif

#if defined(XNETWORK_USE_LWIP)
    // lwIP 适配代码
#endif
```

### 内存分配器集成

适配层必须使用 XinYueC 的内存分配器而非上游默认的 malloc/free：

```c
// FatFs ffsystem.c
#define ff_memalloc  XMalloc_System
#define ff_memfree   XFree_System

// lwIP lwipopts.h
#define MEM_CUSTOM_MALLOC  XMalloc_System
#define MEM_CUSTOM_FREE    XFree_System
#define MEM_CUSTOM_CALLOC  XCalloc_System
```

### 平台抽象层接口设计

```c
// XFileSystem_Fatfs_platform.h -- 定义平台必须实现的三类接口

// 1. 驱动器管理
int XFatfsDrives_count(void);
const XFatfsDrive* XFatfsDrives_at(int index);
int XFatfsDrives_prefixToIndex(const char* pathPrefix);

// 2. 工作目录
const char* XFatfsPath_current(void);
void XFatfsPath_setCurrent(const char* path);

// 3. 特殊路径
const char* XFatfsPath_home(void);
const char* XFatfsPath_root(void);
const char* XFatfsPath_temp(void);
```

### 与平台适配代码的关系

```
XFileSystem API（通用层，Src/ 中）
  │
  ├── XFileSystem_Fatfs.c（适配器，Library/ 中）
  │     └── XFileSystem_Fatfs_platform.h（平台抽象接口）
  │           ├── XFileSystem_Fatfs_diskioWin32.c（Drive/windows/ 中）
  │           ├── XFileSystem_Fatfs_diskioSTM32.c（Drive/STM32/ 中）
  │           └── XFileSystem_Fatfs_diskioPosix.c（Drive/Posix/ 中）
  │
  └── XFileSystem_win32.c（纯平台 API 实现，Drive/windows/ 中）
```

---

## 容器与数据结构规范

### 继承层次

```
XClass
  └── XContainer（容器基类：COW、类型大小、数据操作方法）
        ├── XListBase（链表基类）
        │     ├── XListDLinked（双向循环链表）
        │     ├── XListSLinked（单向链表带尾指针）
        │     └── XLockFreeList（无锁链表）
        ├── XVector（动态数组）
        │     ├── XByteArray（字节数组）
        │     ├── XString（字符串，内部 UTF-16 存储）
        │     ├── XStringList（字符串列表）
        │     ├── XVariantList（Variant 列表）
        │     ├── XCircularQueue（循环队列）
        │     ├── XLockFreeQueue（无锁队列）
        │     └── XPriorityQueue（优先队列，堆实现）
        ├── XSetBase（集合基类）
        │     ├── XSet（有序集合）
        │     └── XHashSet（哈希集合）
        ├── XMapBase（映射基类）
        │     ├── XMap（有序映射）
        │     └── XHashMap（哈希映射）
        ├── XQueueBase（队列基类）
        │     └── XQueue（基于 XListSLinked）
        ├── XStackBase（栈基类）
        │     ├── XStack（基于 XVector）
        │     └── XLockFreeStack（无锁栈）
        ├── XRingChunk（环形数据块）
        ├── XRingBuffer（分块环形缓冲区）
        └── XBitArray（位数组）
```

### 容器创建模式

```c
// 基本创建
XVector* vec = XVector_create_ex(sizeof(int32_t), true);  // typeSize + useCow
XVector* vec = XVector_create_copy(otherVec);              // 深拷贝（COW 时共享）
XVector* vec = XVector_create_move(otherVec);              // 转移所有权

// 类型安全宏（推荐）
XVector* vec = XVector_Create(int32_t);

// 映射创建（指定键值类型和比较函数）
XMap* m = XMap_Create(int32_t, XString*, compareFunc);
XHashMap* hm = XHashMap_Create(int32_t, XString*, compareFunc);

// 集合创建
XSet* s = XSet_Create(int32_t, compareFunc);
XHashSet* hs = XHashSet_Create(int32_t, compareFunc);

// 指定容量（优化）
XHashMap* hm = XHashMap_create_ex(keySize, valSize, compareFunc, capacity, loadFactor);
```

### 数据操作方法回调

容器通过回调支持自定义类型的拷贝、移动、清理：

```c
// 回调类型定义
typedef void (*XCDataDeinitMethod)(void* data);
typedef void (*XCDataCopyMethod)(void* dest, const void* src);
typedef void (*XCDataMoveMethod)(void* dest, void* src);

// 注册回调
XContainer_setDataCopyMethod(container, yourCopyFunc);
XContainer_setDataMoveMethod(container, yourMoveFunc);
XContainer_setDataDeinitMethod(container, yourDeinitFunc);
```

### 迭代器

```c
// 正向迭代
for (XVector_iterator it = XVector_begin(vec); 
     XVector_iterator_notEqual(it, XVector_end(vec));
     XVector_iterator_next(&it))
{
    int* val = (int*)XVector_iterator_data(it);
}

// 反向迭代
for (XVector_reverse_iterator it = XVector_rbegin(vec); ...) { ... }
```

### XString 编码缓存

```c
// XString 内部使用 UTF-16 存储
// 访问不同编码时自动缓存（最多 6 个缓存槽）
const char* utf8 = XString_data_utf8(str);
const char* gbk = XString_data_gbk(str);
const uint16_t* utf16 = XString_utf16Data_const(str);

// 修改字符串后缓存自动失效
XString_append_utf8(str, "追加内容");
```

---

## 信号与槽规范

### 基本用法

```c
// 信号定义（在类的头文件中）
// 使用 XSignal_SLOT 宏或直接声明函数指针类型
typedef void (*XSignal_ResponseSignal)(XATComm*, XByteArray*);
#define XATComm_response_signal(self) \
    XObject_connect_sender_0(self, response_signal)

// 连接信号到槽
// 1 个参数的连接
XObject_connect_1(sender, signal, receiver, slot, XConnectionType_AutoConnection);

// 2 个参数的连接
XObject_connect_2(sender, signal, receiver, slot, XConnectionType_AutoConnection);

// 连接类型
// XConnectionType_AutoConnection  -- 自动选择（同线程直接调用/跨线程队列）
// XConnectionType_DirectConnection -- 直接调用（同步）
// XConnectionType_QueuedConnection -- 通过事件队列（异步）
```

### 信号声明规范

```c
// 在结构体中声明信号槽管理器
typedef struct XATComm {
    XObject m_class;
    // ...
    XSignalSlot m_signalSlot;  // 信号槽管理器
} XATComm;

// 声明回调函数指针类型
typedef void (*XATComm_ResponseCallback)(XATComm*, XByteArray*);
typedef void (*XATComm_OkCallback)(XATComm*);
typedef void (*XATComm_ErrorCallback)(XATComm*);
```

---

## 常见错误与最佳实践

### 1. 忘记初始化/反初始化

```c
// ❌ 错误
void wrong(void)
{
    XHostAddress addr;
    XHostAddress_setAddress(&addr, "192.168.1.1");  // 未初始化！
}

// ✅ 正确
void right(void)
{
    XHostAddress addr;
    XHostAddress_init(&addr);
    XHostAddress_setAddress(&addr, "192.168.1.1");
    XHostAddress_deinit_base(&addr);
}
```

### 2. 错误返回路径未清理资源

```c
// ❌ 错误
void wrong(XHostAddress* result)
{
    XHostAddress temp;
    XHostAddress_init(&temp);
    
    if (someCondition) {
        return;  // 忘记清理 temp！
    }
    
    XHostAddress_copy_base(result, &temp);
    XHostAddress_deinit_base(&temp);
}

// ✅ 正确
void right(XHostAddress* result)
{
    XHostAddress temp;
    XHostAddress_init(&temp);
    
    if (someCondition) {
        XHostAddress_deinit_base(&temp);  // 清理后再返回
        return;
    }
    
    XHostAddress_copy_base(result, &temp);
    XHostAddress_deinit_base(&temp);
}
```

### 3. 忘记给堆对象设置释放函数

```c
// ❌ 错误：堆对象未设置释放函数，delete_base 时无法正确释放
XExample* obj = (XExample*)XMalloc_System(sizeof(XExample));
XExample_init(obj);
// 缺少 Set_Class_MemoryFree！

// ✅ 正确
XExample* obj = (XExample*)XMalloc_System(sizeof(XExample));
XExample_init(obj);
Set_Class_MemoryFree(obj, XFree_System);  // 必须设置！
```

### 4. 在虚函数表中重载顺序错误

```c
// ❌ 错误：先重载后继承，重载会被覆盖
XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit);
XVTABLE_INHERIT_XCLASS(XClass);

// ✅ 正确：先继承后重载
XVTABLE_INHERIT_XCLASS(XClass);
XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit);
```

### 5. copy_base 前未初始化目标对象

```c
// ❌ 错误
XExample dest, src;
XExample_init(&src);
XExample_copy_base(&dest, &src);  // dest 未初始化！

// ✅ 正确
XExample dest, src;
XExample_init(&src);
XExample_init(&dest);             // 必须先 init
XExample_copy_base(&dest, &src);
XExample_deinit_base(&src);
XExample_deinit_base(&dest);
```

### 6. 使用 memcpy 代替 copy_base

```c
// ❌ 错误
XString a, b;
XString_init_utf8(&a, "hello");
memcpy(&b, &a, sizeof(XString));  // 浅拷贝，会导致双重释放/crash

// ✅ 正确
XString a, b;
XString_init_utf8(&a, "hello");
XString_init(&b);
XString_copy_base(&b, &a);
```

### 7. 忘记检查虚函数表和 ISNULL

```c
// ❌ 错误：_base 函数中未做空指针检查
bool XExample_event_base(XExample* self, XEvent* e)
{
    return XClassGetVirtualFunc(self, EXExample_Event, ...)(self, e);
}

// ✅ 正确：必须检查 self 和 vtable
bool XExample_event_base(XExample* self, XEvent* e)
{
    if (ISNULL(self, "XExample") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXExample_Event, bool(*)(XExample*, XEvent*))(self, e);
}
```

### 8. 在 XString 上使用 memcpy

`XString` 包含编码缓存指针（`XStringCache* m_cache[6]`），`memcpy` 拷贝后多个对象共享同一缓存，修改或释放时必定崩溃。

---

## 总结

1. **命名规范**：`X` 前缀 + 大驼峰，函数名为 `X类型_功能`，成员变量 `m_` 前缀，内部虚函数 `V` 前缀
2. **类创建**：继承 `XClass`/`XObject`，使用虚函数表宏定义，遵循标准头文件/源文件结构
3. **虚函数表**：先继承 `XVTABLE_INHERIT_XCLASS`，再 `XVTABLE_OVERLOAD_DEFAULT`，不可颠倒
4. **生命周期**：`init` / `deinit_base` 必须成对出现，堆对象记得 `Set_Class_MemoryFree`
5. **内存管理**：禁止 `memcpy` 复制对象，使用 `copy_base` / `move_base`
6. **错误处理**：使用 `ISNULL` 检查指针，`XAssert` 断言关键条件，检查所有返回值
7. **文件组织**：UTF-8 BOM 编码，标准头文件结构，可选 `_virtual.c` 和 `_Protected.h`
8. **平台适配**：使用条件编译守卫，支持三种适配模式，新代码使用下划线命名风格
9. **第三方库**：三层架构（上游 → 平台抽象 → 适配器），通过编译开关控制
10. **容器**：统一使用 `XContainer` 基类，COW 默认开启，提供类型安全创建宏

### 快速检查清单

创建新类时，逐一检查：

- [ ] 文件编码为 UTF-8 BOM
- [ ] 头文件保护宏正确命名
- [ ] `extern "C"` 块包围
- [ ] 条件编译开关正确
- [ ] `XCLASS_DEFINE_*` 正确设置虚函数枚举
- [ ] 结构体 `m_class` 位于第一位
- [ ] 成员变量用 `m_` 前缀
- [ ] `_init` 中 `memset` 清零子类字段 → 调用父类 `_init` → 设置 vtable
- [ ] `_create` 中调用 `Set_Class_MemoryFree`
- [ ] `_class_init` 中先 `XVTABLE_INHERIT_XCLASS` 再 `XVTABLE_OVERLOAD_DEFAULT`
- [ ] `_base` 函数中用 `ISNULL` 检查 self 和 vtable
- [ ] `copy_base` 调用前目标对象已 `init`
- [ ] `copy` 中先释放旧值再深拷贝新值
- [ ] `move` 中转移后清空源对象
- [ ] `deinit` 中释放资源后置 `NULL`
- [ ] 所有返回路径都清理栈上初始化的对象