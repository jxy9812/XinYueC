# XinYueC 库代码风格指南

## 目录
1. [命名规范](#命名规范)
2. [类的创建](#类的创建)
3. [虚函数表与虚函数重载](#虚函数表与虚函数重载)
4. [对象生命周期管理](#对象生命周期管理)
5. [API 设计规范](#api-设计规范)
6. [内存管理注意事项](#内存管理注意事项)
7. [常见错误与最佳实践](#常见错误与最佳实践)

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
| `_base` | 所有虚函数 | `XHostAddress_deinit_base()` |
| `_utf8` | UTF-8 编码版本 | `XString_append_utf8()` |
| `_gbk` | GBK 编码版本 | `XString_create_gbk()` |

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

## API 设计规范

### 创建函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_create()` | 默认创建 |
| `XType_create_copy(const XType*)` | 拷贝创建 |
| `XType_create_2(...)` | 其他参数重载用数字后缀 |

### 设置函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_setXxx(XType*, ...)` | 设置属性 |
| `XType_setXxx_utf8(XType*, const char*)` | UTF-8 版本设置 |
| `XType_setXxx_gbk(XType*, const char*)` | GBK 版本设置 |

### 获取函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_xxx(const XType*)` | 获取属性（返回值或指针） |
| `XType_toXxx(const XType*)` | 转换为其他类型（返回新对象） |
| `XType_isXxx(const XType*)` | 布尔判断函数 |

### 返回值规范

- **创建函数**：成功返回对象指针，失败返回 `NULL`
- **设置函数**：成功返回 `true`，失败返回 `false`
- **获取函数**：返回值或指针，失败返回默认值或 `NULL`
- **判断函数**：返回 `bool` 类型

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

### 3. 使用宏简化栈对象管理

```c
// 使用宏快速创建栈上对象
XString_Init_Utf8(str, "Hello World");
// 使用 str...
XString_deinit_base(str);

// 使用格式化宏
XString_Init_Fmt_Utf8(msg, "Error code: %d", errorCode);
// 使用 msg...
XString_deinit_base(msg);
```

### 4. 检查返回值

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

## 总结

1. **命名规范**：`X` 前缀 + 大驼峰，函数名为 `X类型_功能`
2. **类创建**：继承 `XClass`，使用虚函数表宏定义
3. **生命周期**：`init`/`deinit_base` 必须成对出现
4. **内存管理**：禁止 `memcpy` 复制对象，使用 `copy_base`
5. **错误处理**：检查返回值，确保所有路径都清理资源