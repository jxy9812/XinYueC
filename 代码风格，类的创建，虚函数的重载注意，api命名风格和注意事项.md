# XinYueC 库代码风格指南

## 目录
1. [命名规范](#命名规范)
2. [外部依赖约束](#外部依赖约束)
3. [类的创建](#类的创建)
4. [虚函数表与虚函数重载](#虚函数表与虚函数重载)
5. [对象生命周期管理](#对象生命周期管理)
6. [API 设计规范](#api-设计规范)
7. [公共头文件与 API 注释格式](#公共头文件与-api-注释格式)
8. [内存管理注意事项](#内存管理注意事项)
9. [错误处理与断言](#错误处理与断言)
10. [文件组织规范](#文件组织规范)
11. [平台适配规范](#平台适配规范)
12. [第三方库集成规范](#第三方库集成规范)
13. [容器与数据结构规范](#容器与数据结构规范)
14. [信号与槽规范](#信号与槽规范)
15. [智能体协作与持续任务规范](#智能体协作与持续任务规范)
16. [常见错误与最佳实践](#常见错误与最佳实践)
17. [总结](#总结)

---

## 命名规范

### 类型命名
- **结构体/类**：`X` 前缀 + 大驼峰命名法
  ```c
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

- **枚举值**：类型名 + `_` + 大驼峰命名，如 `XHostAddress_IPv4Protocol`、`XNetworkAddressEntry_DnsEligible`

### 函数命名
- **格式**：`X类型名_功能名`
  ```c
  XHostAddress_init()       XHostAddress_setAddress()   XHostAddress_toString()
  XString_append_utf8()     XNetworkAddressEntry_createFull()
  ```

### 成员变量命名
- **格式**：`m_` 前缀 + 小驼峰命名
  ```c
  typedef struct XExample {
      XClass m_class;          // 基类（m_class 必须位于第一位）
      int m_value;             // 基本类型成员
      char* m_data;            // 指针成员
      uint32_t m_useCow : 1;   // 位域成员
  } XExample;
  ```

### 文件命名
- **公共头文件**：`XClassName.h`（类名与文件名一致），如 `XClass.h/XClass.c`、`XObject.h/XObject.c`、`XVtable.h/XVtable.c`
- **虚函数实现文件**（可选）：`XClassName_virtual.c`（虚函数实现与业务逻辑分离时使用）
- **内部保护头文件**：`XClassName_Protected.h`（暴露子类需要的内部接口，不对外公开）
- **平台适配文件**：两种风格并存（见 [平台适配规范](#平台适配规范)）

### 内部函数命名（static 虚函数实现）
- **格式**：`V` + 类名 + `_` + 函数名，如 `VXExample_deinit`、`VXExample_copy`、`bool VXExample_event(XExample* self, XEvent* e)`
- 这些函数是 static 的，仅供 `XClassName_class_init()` 中注册虚函数表使用

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
- 用 `typedef` 给泛型容器定义语义化别名：
  ```c
  typedef XMapBase XFuncCodeMap;       // 功能码映射
  typedef XMap XVariantMap;            // Variant 键值映射
  typedef XHashMap XVariantHashMap;    // Variant 哈希映射
  ```

### 宏命名
- **全大写 + 下划线分隔**：`XCLASS_VTABLE_SIZE`、`XVTABLE_CREAT_DEFAULT`、`ISNULL(args, str)`、`XAssert(args, str)`、`XNew(Type)`
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

## 外部依赖约束

> **核心规则：库内已提供同功能 API 时，禁止引入外部库或 C 标准库，一律使用库内接口。**

### 库内替代对照

| 需求 | 禁止引入 | 必须使用 |
|------|---------|---------|
| 文件读写 | `stdio.h` 等文件操作 | `XFile` 族（`Src/XCode/XFile/`：XFile、XDir、XFileInfo、XFileDevice、XSaveFile、XStorageInfo） |
| 删除路径（文件/空目录） | `remove`/`unlink`/`rmdir` | `XDir_removePath_static`（类型自动分流，对标 C remove 语义；递归删除用 `XDir_removeRecursively`） |
| 匿名临时文件 | `tmpfile`/`tmpnam`/`tempnam` | `XSaveFile_openUniqueTemp`（用后即焚）/`XSaveFile_uniqueTempPath_static`（唯一名生成）；临时目录经 `XSaveFile_setTempDir_static` 管理（嵌入式显式指向 scratch 挂载点） |
| 控制台打印 | `printf` 直出 | `XPrintf`（带输出重定向栈 `XPrintf_outputPush`；底层写出经 stdout 原语） |
| 时间日期 | `time.h` | `XDateTime`（`Src/XData/XDateTime/`） |
| 字符分类/转换 | `ctype.h` | `XChar`（`Src/XData/XChar/`） |
| 动态内存 | `malloc`/`calloc`/`realloc`/`free`/`strdup` | `XMemory`（`Src/XMemory/XMemory.h`）/`XClass_Malloc`（见「内存管理注意事项」） |
| 字符串 | `string.h` 字符串函数 | `XString`/`XChar`；`strtok` 用 `XStrtokReentrant`（可重入无静态状态，线程安全，`XStringUtils.h`） |
| 容器 | 手工裸数组/链表、外部容器库 | `XVarList`/`XVariant` 等库内容器（见「容器与数据结构规范」） |

### 时间源规则（2026-09-27 裁定）

- **`Src/` 禁止直呼任何时间源**：`clock()`、`clock_gettime()`、`GetTickCount*`、`time(NULL)` 等一律禁用，统一经 `XDateTime`。
- **计时/超时/限流/去伪等时长测量**：用 `XDateTime_currentMSecsSinceEpoch()`——其实现已切换为**单调时钟**（posix=`CLOCK_MONOTONIC`，win32=`GetTickCount64()`），不受对时/跳变影响。注意返回值不再是纪元毫秒。
- **墙钟日期时间**（展示当前日期、落库时间戳等真纪元场景）：用 `XDateTime_currentDateTime()`/`XDateTime_currentSecsSinceEpoch()`，不得用单调毫秒拼时刻。
- 佐证案例：`XFileDialog.c` 的 `xff_nowMs` 与 `XWidget.c` present 限频已从 `clock_gettime`/`clock` 迁移；反面教材：若用单调毫秒初始化 `XDateTimeEdit` 默认值，会显示 1970-01-01 加开机时长。

### 白名单（语言基础，无库内等价，允许继续使用）

- 头文件：`stdbool.h`、`stdint.h`、`stddef.h`、`limits.h`、`float.h`、`inttypes.h`、`stdarg.h`、`assert.h`
- `string.h` 仅限裸内存原语：`memcpy`、`memset`、`memmove`、`memcmp`（用于完整对象的拷贝仍被禁止，见「内存管理注意事项」）
- `math.h`（库内无数学模块）

### 例外与登记

- `Drive/` 平台适配层不受此限：调用 OS API 是平台适配层的职责（见「平台适配规范」）。
- 第三方库按「第三方库集成规范」三层架构接入；其适配层同样必须使用 `XMemory` 分配器。
- **新增任何外部依赖前，必须在本章节登记**：库名/头文件、用途、库内现有 API 为何不能替代。

---

## 类的创建

> ⚠️ **迁移说明（2026-09-08 起，本文示例均已按此更新）**：拷贝/移动统一使用
> `XCopy(dst, src)` / `XMove(dst, src)`（XClass.h 定义，多态分派到
> `VXType_copy/VXType_move` 虚槽），不再提供 `*_copy_base/*_move_base` 别名宏；
> `*_deinit_base/*_delete_base` 保持不变。
> 继承 `XClass` 的类型只公开 `XCopy`/`XMove` 与 `XType_deinit_base`/`XType_delete_base`
> （两者直接宏映射到带 XClass 指针转换的 `XClass_deinit_base`/`XClass_delete_base`），
> **禁止**为继承 XClass 的类型声明或实现 `XType_deinit`、`XType_copy`、`XType_move`
> 这类非 base 转发 API；栈对象和堆对象都必须通过对应的 base 入口释放，虚表中的
> `VXType_deinit` 只负责具体资源清理。这样保证未初始化目标的兜底初始化、COW 引用
> 计数和派生类重载行为始终由同一入口处理。

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
// 仅继承父类、不新增虚函数时改用：
// XCLASS_DEFINE_BEGING(XExample)
// XCLASS_DEFINE_EXTEND_END(XExample, XClass)

// ==================== 结构体定义 ====================
typedef struct XExample {
    XClass m_class;          // 基类（必须放在第一位）
    int m_value;             // 成员变量
    char* m_data;            // 动态分配的成员
} XExample;

// ==================== 虚函数表初始化 / 构造与析构 ====================
XVtable* XExample_class_init(void);
void XExample_init(XExample* obj);
XExample* XExample_create(void);   // 无参构造

// 反初始化/释放：用宏复用父类（拷贝/移动用 XCopy/XMove，见本章节开头迁移说明）
#define XExample_deinit_base XClass_deinit_base
#define XExample_delete_base XClass_delete_base

// ==================== 功能函数 ====================
void XExample_setValue(XExample* obj, int value);
void XExample_setData(XExample* obj, XString* value);          // 默认拷贝语义
void XExample_setData_move(XExample* obj, XString* value);     // move 后缀：调用移动操作
void XExample_setData_2(XExample* obj, char* value);           // 重载加数字后缀
void XExample_setData_ref_2(XExample* obj, char* value);       // 重载带 ref：引用
int XExample_value(const XExample* obj);                       // 默认返回拷贝
const char* XExample_data_const(const XExample* obj);          // const 后缀返回内部引用

// ==================== 自己新增的虚函数（对外只暴露 *_base） ====================
bool XExample_event_base(XExample* self, XEvent* e);
void XExample_childEvent_base(XExample* self, XChildEvent* event);

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

// ==================== 虚函数实现（static，仅供 class_init 注册） ====================
static bool VXExample_event(XExample* self, XEvent* e)
{
    // 这边实现自己的虚函数逻辑
}
static void VXExample_childEvent(XExample* self, XChildEvent* event)
{
    // 这边实现自己的虚函数逻辑
}
static void VXExample_deinit(XExample* obj)
{
    if (!obj) return;
    if (obj->m_data) {                       // 释放动态分配的资源
        XFree_System(obj->m_data);
        obj->m_data = NULL;
    }
    // 需要时调用父类 deinit：XClass_Deinit_Parent(XExample, obj);
}

static void VXExample_copy(XExample* dest, const XExample* src)
{
    if (!dest || !src) return;
    // vtable 为空（调用方忘了 init）就自动 init，保证 copy/move 在未 init 的目标上安全调用
    if (XClassIsVtableNull(dest)) {
        XExample_init(dest);
    }
    if (dest->m_data) {                      // 先释放目标对象的旧值
        XFree_System(dest->m_data);
        dest->m_data = NULL;
    }
    dest->m_value = src->m_value;            // 复制基本类型成员
    if (src->m_data) {                       // 深拷贝动态分配的成员
        dest->m_data = XMemory_strdup(src->m_data);
    }
    // 成员也是类对象时，直接调用成员的拷贝函数即可，无需先释放；
    // 成员未初始化时可直接调用成员的拷贝构造。
}

static void VXExample_move(XExample* dest, XExample* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest)) {          // 同 copy：vtable 为空就调 init
        XExample_init(dest);
    }
    dest->m_value = src->m_value;            // 转移资源
    dest->m_data = src->m_data;
    src->m_value = 0;                        // 清空源对象
    src->m_data = NULL;
    // 成员也是类对象时，直接调用成员的移动函数即可，无需先释放；
    // 成员未初始化时可直接调用成员的移动构造。
}

// ============== 虚函数表初始化（各宏逐项说明见「虚函数表与虚函数重载」） ==============
XVtable* XExample_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XExample)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);                                // 继承父类虚函数表
    void* table[] = { VXExample_event, VXExample_childEvent };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);                          // 添加自己新增的虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit);    // 重载 deinit
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXExample_copy);        // 重载 copy
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXExample_move);        // 重载 move
    return XVTABLE_DEFAULT;
}

// ==================== 构造函数 ====================
void XExample_init(XExample* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XExample));   // 清零内存
    XClass_init(obj);                   // 初始化基类
    XClassSetVtable(obj, XExample);     // 设置虚函数表
    obj->m_value = 0;                   // 初始化成员变量
    obj->m_data = NULL;
}

XExample* XExample_create(void)
{
    XExample* obj = (XExample*)XMalloc_System(sizeof(XExample));
    if (!obj) return NULL;
    XExample_init(obj);
    Set_Class_Memory(obj, XFree_System);   // 标记为堆分配（必做）
    return obj;
}

XExample* XExample_create_copy(const XExample* other)
{
    if (!other) return NULL;
    XExample* obj = XExample_create();
    if (!obj) return NULL;
    XCopy(obj, other);
    return obj;
}

// ==================== 功能函数 ====================
void XExample_setValue(XExample* obj, int value) { if (obj) obj->m_value = value; }
int XExample_getValue(const XExample* obj) { return obj ? obj->m_value : 0; }

// ========== 自己新增的虚函数：base 入口只查表分派，不写业务逻辑 ==========
bool XExample_event_base(XExample* self, XEvent* e)
{
    if (ISNULL(self, "XExample") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXExample_Event,
                                bool(*)(XExample*, XEvent*))(self, e);
}
// void 返回值的 XExample_childEvent_base 及空槽保护写法见「虚函数表与虚函数重载」。
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
XCLASS_DEFINE_ENUM(XChildClass, ChildFunc1) = XCLASS_VTABLE_GET_SIZE(XParentClass),  // 新增虚函数的枚举值从父类开始
XCLASS_DEFINE_ENUM(XChildClass, ChildFunc2),
XCLASS_DEFINE_END(XChildClass)

// 定义虚函数表枚举（继承自父类，不新增虚函数）
XCLASS_DEFINE_BEGING(XChildClass)
XCLASS_DEFINE_EXTEND_END(XChildClass, XParentClass)   // 指定父类
```

### 虚函数表初始化宏

`XExample_class_init()` 的标准流程（完整示例见「类的创建」源文件结构）：

1. `XVTABLE_CREAT_DEFAULT` —— 创建虚函数表（单例模式）；
2. `XVTABLE_STACK_INIT_DEFAULT(XExample)` 或 `XVTABLE_HEAP_INIT_DEFAULT` —— 初始化虚函数表，由 `#if VTABLE_ISSTACK` 选择栈或堆；
3. `XVTABLE_INHERIT_XCLASS(XParentClass)` —— 继承父类虚函数表；
4. `XVTABLE_ADD_FUNC_LIST_DEFAULT(table)` —— 添加自己新增的虚函数（`void* table[] = { VXExample_event, VXExample_childEvent };`）；
5. `XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit)` 等 —— 重载父类虚函数；
6. `return XVTABLE_DEFAULT;`

### 虚函数重载注意事项

1. **函数签名必须匹配**：虚函数的签名必须与虚函数表中定义的类型一致；
2. **先继承后重载**：必须先调用 `XVTABLE_INHERIT_XCLASS`，再调用 `XVTABLE_OVERLOAD_DEFAULT`，顺序颠倒重载会被覆盖；
3. **使用 `XClassGetVirtualFunc` 调用虚函数**：
   ```c
   XClassGetVirtualFunc(obj, EXClass_Deinit, void(*)(XExample*))(obj);
   ```

### `*_base` 调度入口：只调虚函数表，不写业务逻辑

- `XType_xxx_base` 是所有虚函数（`EXType_Xxx` 槽位）的统一调度入口。函数体内不要写业务逻辑，只能做三件事：
  1. 空指针/虚表检查（`ISNULL` / `XClassGetVtable`）；
  2. 通过 `XClassGetVirtualFunc(self, EXType_Xxx, 函数签名)(self, ...)` 调用对象虚表中对应槽位；
  3. 返回被调用槽位的结果。
- 具体的业务逻辑只放在类内 `static VXType_xxx` 虚函数实现里，并且只通过 `Xxx_class_init()` 注册到虚表；对外调用一律走 `*_base`。

```c
// 类自己新增的虚函数：base 入口只经虚表分派
bool XExample_event_base(XExample* self, XEvent* e)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return false;
    return XClassGetVirtualFunc(self, EXExample_Event,
                                bool(*)(XExample*, XEvent*))(self, e);
}
```

- `void` 返回值的槽位可以先判断槽是否已实现，避免调用空槽函数：

```c
void XExample_childEvent_base(XExample* self, XChildEvent* event)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return;
    if (!XClassGetVirtualFunc(self, EXExample_ChildEvent, bool))
        return;
    XClassGetVirtualFunc(self, EXExample_ChildEvent,
                         void(*)(XExample*, XChildEvent*))(self, event);
}
```

### 继承父类已有虚函数：直接宏复用，禁止写重复转发函数

- 子类没有新增逻辑、只是沿用父类已有虚函数时，**不要**在 `.c` 里写一个同名转发函数，应该在子类头文件里用宏直接替换：

```c
// 子类 XWidgetHead 继承 XHead 的 Event 虚函数入口
#define XWidgetHead_event_base(self, event) \
    XHead_event_base((XHead*)(self), (event))
```

- 生命周期入口 `XType_deinit_base / XType_delete_base` 一律宏复用父类；拷贝/移动统一用 `XCopy(dst, src) / XMove(dst, src)`（见「类的创建」迁移说明）。
- 如果某虚函数本来就从父类继承而来（例如 XWidget 的 Event 继承自 XObject），子类也不必重新实现 `XType_event_base`，直接宏替换成父类的 base 入口即可。
- 只有子类**新增了槽位**或**确实要改变行为/参数**时，才保留子类自己的 `*_base` 函数或在虚表中注册新的 `VXChild_xxx`。

### 子类重载虚函数后调用父类实现：必须用 XClass_Parent

- 子类确实要重载某个虚函数时，才实现 `VXChild_xxx` 并通过 `XVTABLE_OVERLOAD_DEFAULT` 注册。
- 在子类槽位内部需要"先执行父类逻辑 / 后执行父类逻辑"时，必须用 `XClass_Parent(父类, 槽位, 函数签名)` 直接调用**父类槽位**，不能调用对象自身的 `xxx_base`，否则会经对象虚表再次进入子类自己造成递归/栈溢出。

```c
static bool VXFrame_event(XWidget* self, XEvent* event)
{
    bool result;
    // ...子类前置逻辑...
    result = XClass_Parent(XWidget, EXObject_Event,
                           bool(*)(XObject*, XEvent*))((XObject*)self, event);
    // ...子类后置逻辑...
    return result;
}
```

### 虚函数实现/使用风格检查清单

1. `*_base` 入口只查表、只分派或宏复用父类，不写业务逻辑；
2. 子类继承父类已有虚函数时用宏替换，不重复写 C 转发包装；
3. 子类重载后调用父类实现用 `XClass_Parent(...)`，不用对象自身 `*_base`；
4. 业务逻辑统一放在 `static VXxx_xxx` 实现中，只在 `class_init` 注册；
5. `void` 槽位做空槽保护，返回默认值或直接 `return`；
6. 头文件只暴露 `XType_xxx_base` 宏或声明，不暴露内部 `VXxx_xxx`。

---

## 对象生命周期管理

### 栈上对象

```c
XHostAddress addr;                          // 1. 声明对象
XHostAddress_init(&addr);                   // 2. 初始化
XHostAddress_setAddress(&addr, "192.168.1.1");  // 3. 使用对象
XHostAddress_deinit_base(&addr);            // 4. 反初始化（必须成对出现！）
```

### 堆上对象

```c
XHostAddress* addr = XHostAddress_create();     // 1. 创建对象
if (!addr) return;
XHostAddress_setAddress(addr, "192.168.1.1");   // 2. 使用对象
XHostAddress_delete_base(addr);                 // 3. 删除对象
```

### init/deinit 成对原则

**重要**：`XType_init()` 和 `XType_deinit_base()` 必须成对出现！

```c
// ✅ 正确：init 与 deinit_base 成对
XHostAddress addr;
XHostAddress_init(&addr);
// ... 使用对象 ...
XHostAddress_deinit_base(&addr);

// ❌ 错误：忘记 init 是未定义行为；忘记 deinit_base 是内存泄漏
XHostAddress addr;
XHostAddress_setAddress(&addr, "192.168.1.1");
```

### 已初始化区域的清空与容量复用

"`init` / `deinit_base` 必须成对"是对象生命周期规则，不表示每次重新写入都必须销毁并重新初始化对象。对于已初始化、生命周期仍在使用中、且会被反复写入的 `XRegion`，应优先使用 `XRegion_clear()` 清空元素并保留已有的矩形数组容量，禁止在高频路径中无意义地销毁重建：

```c
// ❌ 错误：每次刷新都释放并重新申请矩形数组
XRegion_deinit(&region);
XRegion_init(&region);
XRegion_addRect(&region, &rect);

// ✅ 正确：region 已初始化，清空元素但复用容量
XRegion_clear(&region);
XRegion_addRect(&region, &rect);
```

必须遵守以下边界：

- `XRegion_clear()` 只能用于已通过 `XRegion_init()` 或等价构造流程初始化的对象；不能用它代替首次初始化，也不能对未初始化的栈对象调用。
- 反复写入的脏区、绘制区、裁剪区和后备缓冲提交区等长期对象，应在创建时初始化一次，在重绘/刷新时 `clear` 后复用容量，最终销毁时再调用一次 `XRegion_deinit()`。
- 当操作需要替换整个对象、替换底层数组、改变资源分配器或格式，必须保留对象替换流程，并保证旧资源只释放一次。涉及资源转移时使用类型已有的 `XMove` / 移动语义，不能用结构体赋值冒充所有权转移。
- 当输出对象必须接收新资源且需要保留强异常安全或失败回退语义时，可以先创建局部临时对象；临时对象必须先初始化。无论提交成功还是失败，临时对象的回收都按本指南「move 后源对象的清理责任」执行：由创建者按分配方式负责，不能为了减少一次释放而留下临时资源或悬挂指针。
- `XRegion_copy`、并集、交集、差集等写入已有输出对象的实现，应在容量足够时覆盖元素并更新 `count`，容量不足时才扩容；扩容失败应保留原输出内容，不能先 `deinit` 输出再尝试分配。
- 不能机械地全局删除 `deinit + init`：对象整体替换、失败路径清理、对象类型或分配器确实改变等场景仍必须走对应释放流程。被 move 的源对象由创建者按「move 后源对象的清理责任」释放，接收方不得在 move 现场代劳。

该规则同样适用于 `XImage`、`XPixmap`、容器和绘制状态等可复用资源，但必须先确认对应类型提供的 `clear`、容量复用或移动接口及其初始化前提；没有明确生命周期保证时，不得仅凭减少一次释放就修改代码。

### 快速初始化宏

对于简单场景，可以使用快捷宏：

```c
XString_Init_Utf8(str, "Hello World");        // 栈上字符串快速初始化
XString_deinit_base(str);

XString_Init_Fmt_Utf8(msg, "Error code: %d", errorCode);  // 格式化字符串
XString_deinit_base(msg);
```

### 拷贝构造与移动构造

```c
// 拷贝构造：深拷贝创建新对象
XExample* dest = XExample_create_copy(src);

// 移动构造：转移资源所有权，创建新对象（src 被清空）
XExample* dest = XExample_create_move(src);

// 栈上拷贝（虚函数内部会兜底 init，调用方仍推荐显式 init）
XExample dest, src;
XExample_init(&dest);
XExample_init(&src);
XCopy(&dest, &src);               // 多态拷贝
XExample_deinit_base(&dest);
XExample_deinit_base(&src);

// 栈上移动
XExample dest, src;
XExample_init(&dest);
XExample_init(&src);
XMove(&dest, &src);               // src 资源转移到 dest
XExample_deinit_base(&dest);
// src 已为空；创建者仍需 deinit_base 完成生命周期收尾（堆对象则用 delete_base）
XExample_deinit_base(&src);
```

#### copy/move 目标初始化约束

- 所有 `copy` / `move` 虚函数，以及负责私有数据复制或移动的辅助函数，在访问目标成员前必须检查目标对象的 vtable：目标未初始化（vtable 为空）时，必须先调用对应类型的 `XType_init()` 完整初始化（包括分配资源、设置 vtable），再释放旧资源或写入新资源。这样 `XCopy`/`XMove` 可以安全地在未 init 的目标上调用，`XType_create_copy(src)` 这类便捷 API 无需关心 dest 是否已 init；调用方也可以先 `XType_init(&dest)` 再拷贝（检测到 vtable 非空，不会重复 init）。
- 源对象未初始化时必须安全返回；源对象与目标对象相同时，`copy` / `move` 必须直接返回。
- 目标对象初始化检查应放在虚函数实现或私有操作入口中，不能只依赖调用方提前 `init`。
- ❌ 错误写法：只 set vtable、没分配成员——`if (XClassIsVtableNull(dest)) { XClassSetVtable(dest, XExample); }`，成员还未分配。也不用 `XClassEnsureVtable` 之类只设置 vtable 的捷径，对 copy/move 来说不够。

#### move 后源对象的清理责任

`XMove` / 移动虚槽只转移资源所有权。移动完成后，源对象处于"已初始化但为空"的状态：虚表、内存方法和堆所有权标记保持不变，源对象仍可被安全地 `deinit_base` 或 `delete_base`。**清理责任不随 move 转移，仍属于创建者；move 现场只转移资源，不代替创建者清理源对象。**

- 只有创建者知道源对象是栈对象还是堆对象：栈对象由 `XType_init()` 建立，用 `XType_deinit_base()` 释放；堆对象由 `XType_create()` / `XType_create_ex()` 等建立，用 `XType_delete_base()` / `XClass_delete_base()` 释放（`delete_base` 内部先反初始化，再按堆所有权释放结构体）。
- 当源对象来自参数、对象成员或调用方输出时，执行 move 的函数只是接收方，不得在 move 现场调用 `deinit_base` 或 `delete_base`：接收方不知道源对象的分配方式，擅自清理会造成错误释放或双释放。最终清理仍由创建者在自己的清理点完成。
- 当函数自己创建临时对象并把资源 move 出去时，该函数就是创建者：栈临时对象在作用域结束时按栈上对象规则收尾（`deinit_base`，资源已转出时为空操作）；堆临时对象必须由创建者 `delete_base`，不能因"源已为空"跳过结构体释放。
- 移动后仍继续存活的源对象（控件成员、容器元素等）保持原有清理点不变：由拥有它的对象在自身的 `deinit` / 私有释放路径中统一清理，不在 move 现场重复释放。

```c
// 接收方只转移资源，不清理源；源与目标都由各自的创建者按分配方式释放
XExample* heapSrc  = XExample_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
XExample* heapDest = XExample_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
XMove(heapDest, heapSrc);     // heapSrc 变为空对象
XExample_delete_base(heapDest);   // 创建者：堆对象用 delete_base
XExample_delete_base(heapSrc);    // 创建者：空源对象同样 delete_base（安全）
```

### deleteLater 模式

对于被事件系统引用的对象，不能直接 `delete`，需要使用延迟删除（通过事件循环在下次事件处理时安全释放）：

```c
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

### 字符串 API 主次顺序与 UTF-8 兼容重载

库内字符串对象统一使用 `XString`，对外 API 的主版本必须优先采用 `XString`：

```c
void XImage_setText(XImage* self, const XString* key, const XString* value);
void XImage_setText_2(XImage* self, const char* key, const char* value);
```

- 原名（无数字后缀）是 `XString` 版本，负责实际逻辑、复制和生命周期管理；`_2` 是 `const char*` UTF-8 兼容重载，只负责创建临时 `XString` 并转发到原名版本，禁止直接维护另一份字符串状态。
- `char*` 参数均按 UTF-8 解码，不得把它当作本地编码、GBK 或可写缓冲区。
- 字符串成员、私有状态和容器元素禁止以 `char*` 长期保存；需要字符串所有权时使用 `XString*`，需要多个字符串时使用 `XStringList`。
- 返回字符串时，原名优先返回新建的 `XString*`；需要借用内部对象时使用 `*_const`，并在注释中明确禁止释放和修改。`const char*` 兼容返回值只能来自临时转换缓存或 `_2` 包装，不得成为核心存储。
- 不要用 `_2` 表示 XString 版本；`_2` 只表示 UTF-8 `const char*` 兼容参数或返回值。

构造函数、静态工厂、文件名、格式名、主题名、错误描述、文本元数据、缓存键和 MIME 类型均遵循此规则。新增重载时先实现 XString 主版本，再实现 UTF-8 `_2` 转发版本。

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

```c
XVector_push_back(vec, data);   // 风格一：小写下划线（通用风格）
XQueue_Push(queue, data);       // 风格二：大驼峰（部分较早容器使用）
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

## 公共头文件与 API 注释格式

公共头文件是库使用者理解 ABI、生命周期和错误语义的主要依据。所有新增或修改的公开类型、枚举、结构体、成员和函数都必须有中文 Doxygen 注释；不能只在 `.c` 文件中解释公开行为。注释使用 UTF-8（带 BOM）保存，缩进和项目代码保持一致。

### 文件头注释

每个公开头文件第一行使用文件说明，随后才是头文件保护宏。文件说明至少包含文件名、模块用途、Qt 对齐对象和平台约束：

```c
/**
 * @file       XXmlDomElement.h
 * @brief      XML DOM 元素节点公开 API。
 * @details    对齐 Qt QDomElement；实现只依赖 XinYueC 抽象层，禁止调用
 *             Win32、POSIX、Qt 或其他平台 API。
 */
```

### 类型、枚举和成员注释

- 每个 `typedef struct`、`typedef enum` 先写 `@brief`；需要说明继承、句柄语义、线程安全性或所有权时使用 `@details`。
- 枚举类型要逐项解释每个枚举值的含义、数值约定以及是否可以按位组合。不要只解释枚举类型而省略枚举值。
- 结构体的每个公开成员使用行尾 `/**< ... */` 或成员前的块注释。必须说明单位、有效范围、是否可以为 `NULL`、是否由对象拥有以及调用者能否直接修改。
- `m_class` 必须注明"第一个成员、由 XClass 管理、禁止手工修改"；内部指针必须注明"仅供实现使用"或"返回的是借用指针"。

### 函数注释格式

每个公开函数都使用完整块注释。`@param` 必须覆盖声明中的每一个参数，参数名必须与声明完全一致；无参数函数也要说明其返回值。返回指针必须明确"新对象/借用对象/空句柄"和释放方式，布尔值必须说明成功条件，修改对象的函数必须说明失败时对象是否保持不变：

```c
/**
 * @brief      在元素中设置属性值。
 * @details    当同名属性已经存在时替换其值；不存在时创建新属性。
 * @param      self 目标元素；传入 NULL 时函数不执行任何操作。
 * @param      name 属性名；不能为 NULL 或空字符串。
 * @param      value 属性值；NULL 按空字符串处理，不转移调用者所有权。
 * @return     无。参数非法时保持原对象不变。
 * @note       XString 参数只在调用期间借用，函数返回后可以立即释放或修改。
 * @warning    返回的 `const XString*` 属于对象内部存储，不能释放，也不能强制转换后修改。
 */
void XXmlDomElement_setAttribute(XXmlDomElement* self,
                                 const XString* name,
                                 const XString* value);
```

### 统一标签和语义要求

- `@brief` 使用一句话概括可观察行为；不要写"设置变量""调用函数"等无信息注释。
- `@details` 说明与 Qt 的差异、节点类型限制、编码转换、快照/实时容器语义和错误替代策略。
- `@param` 说明输入方向：只读参数写"借用、不会取得所有权"，输出参数写"调用方提供存储空间"，输入输出参数写"函数可能修改"。
- `@return` 说明所有成功、失败、空结果和越界路径；返回对象若需要调用 `*_delete_base`，必须明确写出。
- `@note` 用于生命周期、隐式共享、UTF-16 索引、线程安全和 Qt 对齐细节；`@warning` 只用于错误后果明确的误用风险。
- UTF-8 接口要写明输入按 UTF-8 解码，`XString` 接口要写明字符串内部按 UTF-16 代码单元处理。索引和长度若按 UTF-16 代码单元计数，必须在参数说明中明确。
- 空句柄采用"对象已初始化但内部节点为空"的表述；NULL 指针采用"调用者没有提供对象"的表述，二者不得混写。
- `init`、`create`、`create_copy`、`create_move`、`deinit_base`、`delete_base` 都必须说明初始化前提、目标未初始化时的行为、源对象在移动后的状态和释放方式。
- Qt 对齐 API 的注释应直接写明对应 Qt 名称，例如"对齐 `QDomNode::appendChild`"；若 C API 因指针、返回值或错误处理不同，必须同时说明 XinYueC 的实际行为，不能只复制 Qt 文档。

### 头文件自检清单

- [ ] 文件头包含 `@file`、`@brief` 和平台依赖说明。
- [ ] 每个公开类型、枚举值、结构体成员和宏都有说明。
- [ ] 每个公开函数的每个参数都有 `@param`，返回值都有 `@return`。
- [ ] 返回的借用指针、新对象和空句柄语义清楚，释放责任明确。
- [ ] 编码、索引单位、线程安全、NULL 和失败时对象状态已经写明。
- [ ] 注释与实际实现一致；修改 API 行为时同步更新注释和测试。
- [ ] 公开头文件只声明公有 API；protected/内部接口已迁入 `XClassName_Protected.h`，调用方已显式 include。

---

## 内存管理注意事项

### 禁止使用 memcpy 复制对象

`XHostAddress`、`XString` 等类型可能包含动态分配的内存（`XString` 还包含编码缓存指针 `XStringCache* m_cache[6]`），`memcpy` 只做浅拷贝，会导致：多个对象指向同一块内存、内存泄漏（原对象的内存未被释放）、双重释放（多个对象尝试释放同一块内存），共享编码缓存后修改或释放时必定崩溃。

```c
// ❌ 错误：浅拷贝
memcpy(&dest, &src, sizeof(XHostAddress));

// ✅ 正确：多态拷贝（内部经虚槽深拷贝）
XHostAddress_init(&dest);
XCopy(&dest, &src);
XHostAddress_deinit_base(&dest);
XHostAddress_deinit_base(&src);
```

### 拷贝/移动/释放入口

| 函数 | 用途 |
|------|------|
| `XCopy(dest, src)` | 深拷贝对象（多态分派；回调槽位需要函数指针时用 `XClass_copy_base`） |
| `XMove(dest, src)` | 移动语义，转移资源所有权（函数指针用 `XClass_move_base`） |
| `XType_deinit_base(obj)` | 反初始化对象（释放资源） |
| `XType_delete_base(obj)` | 删除堆对象（反初始化 + 释放内存） |

### 强制内存分配规则与分配器体系

**严禁在 XinYueC 源码中直接使用 C 语言的 `malloc`、`calloc`、`realloc`、`free` 或 `strdup`。** 所有动态内存必须通过 `Src/XMemory/XMemory.h` 提供的接口申请、重分配和释放，不得绕过库的内存管理策略调用 C 运行库分配函数。只有 `Src/XMemory` 内部实现可以封装底层分配后端；业务模块、容器、驱动和第三方库适配层都必须使用 `XMemory.h` 的公开接口。

代码库提供三种内存分配器，必须用同一分配器族配对释放：`XMalloc_System` 对应 `XFree_System`，`XMalloc_MultiPool` 对应 `XFree_MultiPool`，`XMalloc_Hybrid` 对应 `XFree_Hybrid`。

```c
// 系统默认分配器
void* ptr = XMalloc_System(size);
ptr = XRealloc_System(ptr, newSize);
XFree_System(ptr);
void* calloced = XCalloc_System(count, size);

// 多级池分配器（适用于频繁小对象分配）
void* ptr = XMalloc_MultiPool(size);
XFree_MultiPool(ptr);

// 混合分配器
void* ptr = XMalloc_Hybrid(size);
XFree_Hybrid(ptr);

// 类型安全分配宏 / strdup 替代
XExample* obj = (XExample*)XNew(XExample);
char* text = XMemory_strdup(source);
XFree_System(text);
```

### COW（Copy-On-Write）机制

容器默认启用 COW，基于 `XSharedData` + 原子引用计数：

```c
// COW 开启时，拷贝构造仅增加引用计数
XVector* v2 = XVector_create_copy(v1);       // v1 和 v2 共享底层数据

// 写操作时触发深拷贝分离
XVector_push_back(v2, &data);                // v2 自动分离，深拷贝数据
XVector_push_back(v1, &data);                // v1 不受影响

// 禁用 COW 创建
XVector* v = XVector_create_ex(sizeof(int), false);   // useCow=false
```

---

## 错误处理与断言

### ISNULL 检查与 XAssert 断言

- `ISNULL(args, description_str)`：检查指针是否为空，为空时内部调用 `XERROR_PRINTF` 输出文件名、函数名和行号，然后返回 `true`（表示确实为 NULL），调用方据此返回。
- `XAssert(args, str)`：断言，失败时调用 `exit(-1)`，如 `XAssert(ptr != NULL, "Memory allocation failed");`
- `*_base` 函数中的标准检查与分派写法见「虚函数表与虚函数重载」的调度入口示例。

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
    return;   // 处理内存分配失败
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

所有 `.c` 和 `.h` 文件必须使用 **UTF-8 BOM**（文件开头 `\xEF\xBB\xBF`）。

### 头文件结构（按序 11 段）

```c
// XExample.h
#ifndef XEXAMPLE_H             // 1. 头文件保护
#define XEXAMPLE_H

#ifdef __cplusplus             // 2. C++ 兼容
extern "C" {
#endif

#if XEXAMPLE_ON                // 3. 条件编译开关（由 CXinYueConfig.h 中的开关控制）

#include "CXinYueConfig.h"     // 4. 包含依赖（全局配置文件通常第一个）
#include "XClass.h"
// 其他依赖...

// 5. 前向声明 / 类型定义（枚举、结构体前向声明、typedef...）

XCLASS_DEFINE_BEGING(XExample) // 6. 虚函数表定义
XCLASS_DEFINE_ENUM(XExample, Event) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XExample)

typedef struct XExample {      // 7. 结构体定义
    XObject m_class;
    int m_value;
} XExample;

XVtable* XExample_class_init(void);    // 8. 函数声明（按逻辑分组：虚函数表初始化、
void XExample_init(XExample* obj);     //    构造/析构、*_base 调度、功能函数）
XExample* XExample_create(void);
bool XExample_event_base(XExample* self, XEvent* e);
void XExample_setValue(XExample* obj, int value);
int XExample_value(const XExample* obj);

#endif // XEXAMPLE_ON          // 9. 关闭条件编译

#ifdef __cplusplus             // 10. 关闭 C++ 兼容
}
#endif
#endif // XEXAMPLE_H           // 11. 关闭头文件保护
```

### 源文件结构（按序 8 段）

```c
// XExample.c
#include "XExample.h"      // 1. 自包含头文件首先
#include <string.h>        // 2. 标准库
#include "XMemory.h"       // 3. 框架库

static void VXExample_deinit(XExample* obj) { ... }                        // 4. 静态虚函数实现
static void VXExample_copy(XExample* dest, const XExample* src) { ... }

XVtable* XExample_class_init(void)                                         // 5. 虚函数表初始化（单例）
{
    XVTABLE_CREAT_DEFAULT
    // ... 虚函数表初始化 ...
    return XVTABLE_DEFAULT;
}
void XExample_init(XExample* obj) { ... }                                  // 6. 构造函数
XExample* XExample_create(void) { ... }
bool XExample_event_base(XExample* self, XEvent* e) { ... }                // 7. 虚函数调度函数
void XExample_setValue(XExample* obj, int value) { ... }                   // 8. 功能函数
```

### 虚函数实现文件（可选分离）

当虚函数实现较多时，可分离到 `XClassName_virtual.c`，仅包含虚函数实现与 `class_init`：

```c
// XClass_virtual.c
static void VXClass_deinit(XObject* obj) { ... }
static void VXClass_copy(XObject* dest, const XObject* src) { ... }

XVtable* XClass_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    // ...
    return XVTABLE_DEFAULT;
}
```

### 保护头文件（`XClassName_Protected.h`）

保护头文件集中放置 Qt 中属于 `protected` 或内部实现的接口，只供子类与内部实现使用，不写入公开头文件，也不对外公开：

| API 类别 | 存放位置 | 示例 |
|---|---|---|
| 公开查询/设置 API | `XClassName.h` | `XWidget_sizeHint`、`XWidget_minimumSizeHint`、`XWidget_heightForWidth`、`XWidget_backingStore` |
| Qt protected 子类重载入口/事件分派 | `XClassName_Protected.h` | `XWidget_event_base`、`XWidget_paintEvent_base`、`XWidget_resizeEvent_base` |
| 子类/内部绘制辅助 | `XClassName_Protected.h` | `XWidget_setSizeHint`、`XWidget_paintDevice`、`XWidget_paintOffset`、`XWidget_drawContentCached` |
| 内容缓存/离屏缓存辅助 | `XClassName_Protected.h` | `XWidget_beginContentCache`、`XWidget_markContentCacheReady` |
| 平台/桥接内部钩子 | `XClassName_Protected.h` | `XWidget_applyWindowGeometry`、`XWidget_applyWindowVisibility`、`XWidget_flushBackingStore` |

```c
// XIODevice_Protected.h -- 仅供子类（XSerialPort 等）使用的 protected virtual 函数
int64_t XIODevice_readData_base(XIODevice* device, char* data, int64_t maxSize);
int64_t XIODevice_writeData_base(XIODevice* device, const char* data, int64_t maxSize);
```

硬性约定：

1. **文件命名**：固定为 `XClassName_Protected.h`（如 `XWidget_Protected.h`、`XIODevice_Protected.h`）。
2. **公开头文件只放公有 API**：`XClassName.h` 不得声明 `*_base` 事件分派、`setSizeHint` 一类子类存储位、内部绘制设备、内容缓存内部入口或平台钩子；普通用户 API 头文件链不能传递引入 `XClassName_Protected.h`。
3. **Protected 头文件内部自包含**：必须 `#include "XClassName.h"`，并保留自己的头文件保护宏。
4. **注释标准同公开头文件**：保护接口也必须写中文 Doxygen `@brief/@param/@return`；在文件头 `@details` 标明"仅供子类和内部实现使用，不对外公开"。
5. **使用者必须显式 include**：任何调用保护接口的 `.c` 文件都应显式 `#include "XClassName_Protected.h"`，不得依赖间接传递；新类创建时必须同步放置该头文件。
6. **迁移接口必须同步更新调用方**：把接口从公开头文件移入 Protected 后，用 `rg` 或等效工具扫描全部调用方并逐个补 include，按新头文件结构重新构建验证。
7. **与 Qt 对照时不要把公开查询接口误移**：如 `sizeHint`、`minimumSizeHint`、`heightForWidth`、`hasHeightForWidth`、`backingStore` 等 Qt 公开查询函数保持公开；只有 Qt protected 虚函数、内部存储位和平台/桥接钩子才进入 Protected。

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
struct XMutex { pthread_mutex_t mutex; };   // XMutexPosix.c
struct XMutex { CRITICAL_SECTION cs; };     // XMutexWin32.c

// 通用代码通过 PlatformPrivate 间接访问
static inline pthread_mutex_t* XMutex_get_pthread(XMutex* mutex) {
    return &mutex->mutex;   // 仅在 POSIX 平台可用
}
```

#### 模式 B：直接定义结构体（Semaphore、WaitCondition）

```c
// 平台文件直接定义完整结构
struct XSemaphore { SemaphoreHandle_t handle; int count; int max_count; };  // XSemaphoreFreeRTOS.c
struct XSemaphore { sem_t handle; };                                        // XSemaphorePosix.c

// 通用代码通过 _typeSize() 和 _init/_deinit 操作
size_t XSemaphore_typeSize();
bool XSemaphore_init(XSemaphore* sem, ...);
```

#### 模式 C：虚函数表（Thread、SerialPort）

```c
// 每个平台实现自己的 class_init()，如 XThreadPosix.c
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

仅当启用对应库时才编译适配层：`#if defined(XFILE_USE_FATFS)` 编译 FatFs 适配代码，`#if defined(XNETWORK_USE_LWIP)` 编译 lwIP 适配代码。

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

### 容器优先与字符串列表

实现类的动态数据必须优先复用 `Src/XContainer` 中已有容器，禁止在业务类中重新设计同等功能的裸数组、长度字段和手工扩容逻辑：

- 多个 `XString` 使用 `XStringList`；键值成对时使用两个对应索引的 `XStringList`，或使用已有的映射/键值容器。
- 连续的非字符串对象使用 `XVector` 或其类型别名；字节使用 `XByteArray`；映射使用 `XMap/XHashMap`。
- 容器字段应嵌入对象或由对象明确拥有，并在 `init` 中初始化，在 `deinit_base`/私有释放路径中调用对应的 `*_deinit_base` 或 `*_clear_base`。不得把容器元素指针直接当作独立堆对象数组管理。
- `XStringList` 的元素类型是 `XString` 对象，不是 `char*`。插入 XString 使用 `XStringList_push_back_base`/`XStringList_push_back_move_base`，插入 UTF-8 使用 `XStringList_push_back_utf8`；读取使用 `XStringList_at_base` 并按 `XString*` 处理。
- 容器复制、移动和写时复制必须使用容器已有的 `*_copy_base`、`*_move_base`、`*_clear_base` 等接口，不能用 `memcpy` 复制含有所有权或虚函数表的元素。
- 容器 API 的返回对象、借用指针和删除方式必须在头文件注释中写明；调用方不得释放 `at/front/back` 返回的内部元素。

示例：

```c
typedef struct XExamplePrivate {
    XStringList m_names;       /**< 对象拥有的字符串列表 */
    XVector     m_items;       /**< 对象拥有的连续元素容器 */
} XExamplePrivate;

static void XExamplePrivate_init(XExamplePrivate* data)
{
    XStringList_init(&data->m_names);
    XVector_init(&data->m_items, sizeof(XItem), true);
}
```

禁止示例：

```c
char** m_names;
int m_nameCount;
/* 手工 XMalloc/realloc/free、memcpy 和逐项 delete */
```

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
XVector* vec = XVector_create_ex(sizeof(int32_t), true);   // typeSize + useCow
XVector* vec = XVector_create_copy(otherVec);              // 深拷贝（COW 时共享）
XVector* vec = XVector_create_move(otherVec);              // 转移所有权

// 类型安全宏（推荐）
XVector* vec = XVector_Create(int32_t);

// 映射/集合创建（指定键值类型和比较函数）
XMap* m = XMap_Create(int32_t, XString*, compareFunc);
XHashMap* hm = XHashMap_Create(int32_t, XString*, compareFunc);
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
// XString 内部使用 UTF-16 存储；访问不同编码时自动缓存（最多 6 个缓存槽）
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
// 信号定义（在类的头文件中）：使用 XSignal_SLOT 宏或直接声明函数指针类型
typedef void (*XSignal_ResponseSignal)(XATComm*, XByteArray*);
#define XATComm_response_signal(self) \
    XObject_connect_sender_0(self, response_signal)

// 连接信号到槽（按参数个数选择版本）
XObject_connect_1(sender, signal, receiver, slot, XConnectionType_AutoConnection);
XObject_connect_2(sender, signal, receiver, slot, XConnectionType_AutoConnection);

// 连接类型
// XConnectionType_AutoConnection   -- 自动选择（同线程直接调用/跨线程队列）
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

## 智能体协作与持续任务规范

当用户明确要求使用多个智能体、持续执行、定时唤醒或由主线程负责审核时，必须遵循以下规则。该规范适用于代码修改、审查、构建验证和后续任务派发。

### 1. 任务必须发布在当前对话的智能体池

- 用户要求"在本对话开智能体"时，只能使用当前对话的智能体池，禁止通过新建外部对话、无关线程或不可追踪的临时任务替代。
- 每个智能体必须有明确的任务描述、限定的文件写入范围和预期验证方式。
- 多个智能体的写入范围必须互不重叠；共享基础文件必须先拆分任务或改为只读审查，不能让多个智能体同时修改同一段代码。
- 任务描述必须明确"不提交 Git、不回退用户改动"，除非用户另行授权。

### 2. 五智能体并发与空闲槽位管理

- 用户要求"五个智能体"时，应始终维护最多五个有明确工作的智能体；任务完成、失败或停止后，立即确认该槽位状态并补发下一项不重叠任务。
- 已完成的智能体优先复用；只有在无法复用或需要独立上下文时才创建新的智能体。已完成但仍占用并发槽位的智能体，应按平台能力关闭或复用，不能无限制保留空闲实例。
- 不得因为调用一次等待接口没有结果就认为任务停止。等待超时后应检查智能体状态和工作区是否有新改动；确认仍在运行则保留原任务，确认已完成则进入审核并补发任务。
- 不得向仍在执行的智能体重复派发同一范围任务。补充要求应作为原任务的审核意见发送，只有原任务结束后才能分配新的写入范围。

### 3. 主线程职责：审核、回派和补发

当用户指定主线程只负责审核和向智能体发布任务时，主线程禁止直接修改业务代码、禁止代替智能体提交 Git，也禁止为了赶进度自行回退已有改动。主线程应按以下顺序工作：

1. 读取智能体的最终报告，核对实际修改文件和任务限定范围。
2. 审核 `git diff`、对象所有权、初始化/反初始化配对、失败路径、别名语义、引用计数、线程安全和 C99 兼容性。
3. 使用 `git diff --check` 和限定编译验证结果；报告中的"已验证"必须有实际命令或构建结果支撑。
4. 发现问题时只把具体证据和修正要求回派给原智能体，不得静默接受有风险的改动；必要时要求原智能体停止扩展并说明需要撤回的本轮改动。
5. 审核通过后关闭或复用该智能体，并向空闲槽位补发下一个不重叠任务。

### 4. 生命周期和内存优化任务的审核底线

- 删除 `deinit + init` 只能在目标对象已初始化且析构责任明确时进行；不能通过读取未初始化栈对象的 vtable、字段或地址登记表推断其生命周期。
- 任何复用容量的优化都必须保留对象的可析构状态、旧数据失败回退语义和资源所有权；扩容或新资源创建失败时不得丢失原对象内容。
- 不得使用直接 `malloc`、`realloc`、`free` 替代项目内存 API；不得引入 C11 语法。值类型、容器、图像、字体和区域对象必须保持 `init/deinit_base` 成对。
- 涉及事件队列、脏区、绘制回调或线程数据时，必须检查重入、事件压缩、队列投递失败和绘制期间再次 `update()` 的行为，不能只以单次编译通过作为完成依据。
- 任何新增的全局生命周期登记、缓存或锁都必须说明并发保护、登记失败、地址复用、进程退出和线程退出语义；无法证明安全时不得作为优化合入。

### 5. 定时唤醒与持续任务

- 定时任务只负责唤醒当前对话并继续检查智能体状态、审核结果和空闲槽位，不得自动提交 Git、回退文件或扩大原任务范围。
- 每次唤醒先检查是否已有相同任务在运行，避免重复派发；有智能体完成时先审核再补发；所有智能体仍在执行时不得重复创建同范围任务。
- 唤醒后即使没有代理完成，也应进行有意义的只读检查，例如查看状态、差异、编译结果或任务边界；不能只重复等待并宣称任务已推进。
- 定时任务的提示必须写清当前仓库、审核职责、禁止提交和禁止回退等约束，使其脱离即时对话上下文后仍不会改变任务意图。

### 6. 向用户报告的最低内容

每轮持续任务至少说明：当前五个智能体各自的范围和状态、已审核或待审核的结果、是否补发了新任务、是否有构建或测试阻塞，以及是否发生了 Git 提交。不能把"已派发"写成"已完成"，也不能把等待超时写成任务失败。

---

## 常见错误与最佳实践

1. **忘记初始化/反初始化**：未 `init` 就使用是未定义行为，用完不 `deinit_base` 是内存泄漏；❌/✅ 写法见「对象生命周期管理 → init/deinit 成对原则」。
2. **错误返回路径未清理资源**：每个返回路径都必须先清理再返回。
   ```c
   XHostAddress temp;
   XHostAddress_init(&temp);
   if (someCondition) {
       XHostAddress_deinit_base(&temp);  // ✅ 清理后再返回（❌ 错误写法：直接 return 漏掉本行）
       return;
   }
   XCopy(result, &temp);
   XHostAddress_deinit_base(&temp);
   ```
3. **忘记给堆对象设置释放函数**：`XMalloc_System` + `init` 之后缺少 `Set_Class_Memory`，`delete_base` 时无法正确释放。
   ```c
   XExample* obj = (XExample*)XMalloc_System(sizeof(XExample));
   XExample_init(obj);
   Set_Class_Memory(obj, XFree_System);   // ✅ 必须设置！
   ```
4. **在虚函数表中先重载后继承**：先 `XVTABLE_OVERLOAD_DEFAULT` 再 `XVTABLE_INHERIT_XCLASS` 会覆盖重载，必须先继承后重载（见「虚函数重载注意事项」）。
5. **copy/move 不支持未初始化目标**：所有 `VXxxx_copy`/`VXxxx_move` 实现在第一行参数检查之后，必须用 `XClassIsVtableNull(dest)` 检查目标 vtable，为空则调用 `XType_init(dest)` 兜底完整初始化；只 `XClassSetVtable` 不分配成员是错误写法。规则与示例见「对象生命周期管理 → copy/move 目标初始化约束」。
6. **用 memcpy 代替拷贝**：浅拷贝共享底层数据，导致双重释放/crash；必须用 `XCopy`。`XString` 还会共享编码缓存指针，见「内存管理注意事项 → 禁止使用 memcpy 复制对象」。
7. **忘记检查虚函数表和 ISNULL**：`*_base` 函数中未做 `ISNULL(self, ...)` + `ISNULL(XClassGetVtable(self), ...)` 检查就直接分派，空指针会崩溃；正确写法见「虚函数表与虚函数重载 → `*_base` 调度入口」。

---

## 总结

1. **命名规范**：`X` 前缀 + 大驼峰，函数名为 `X类型_功能`，成员变量 `m_` 前缀，内部虚函数 `V` 前缀
2. **外部依赖**：库内已有同功能 API（XFile/XDateTime/XChar/XMemory/XString/XVarList 等）时禁止引入外部库/C 标准库；白名单与登记要求见「外部依赖约束」
3. **类创建**：继承 `XClass`/`XObject`，使用虚函数表宏定义，遵循标准头文件/源文件结构；拷贝/移动统一 `XCopy`/`XMove`
4. **虚函数表**：先继承 `XVTABLE_INHERIT_XCLASS`，再 `XVTABLE_OVERLOAD_DEFAULT`，不可颠倒
5. **生命周期**：`init` / `deinit_base` 必须成对出现，堆对象记得 `Set_Class_Memory`，move 后源对象由创建者清理
6. **内存管理**：禁止 `memcpy` 复制对象，禁用 `malloc` 族，统一使用 XMemory 接口
7. **错误处理**：使用 `ISNULL` 检查指针，`XAssert` 断言关键条件，检查所有返回值
8. **文件组织**：UTF-8 BOM 编码，标准头文件结构，可选 `_virtual.c` 和 `_Protected.h`
9. **平台适配**：使用条件编译守卫，支持三种适配模式，新代码使用下划线命名风格
10. **第三方库**：三层架构（上游 → 平台抽象 → 适配器），通过编译开关控制，分配器接入 XMemory
11. **容器**：统一使用 `XContainer` 基类，COW 默认开启，提供类型安全创建宏

### 快速检查清单

创建新类时，逐一检查：

- [ ] 文件编码为 UTF-8 BOM；头文件保护宏正确命名；`extern "C"` 块包围；条件编译开关正确
- [ ] 未引入库内已有同功能 API 的外部依赖（见「外部依赖约束」）
- [ ] `XCLASS_DEFINE_*` 正确设置虚函数枚举；结构体 `m_class` 位于第一位；成员变量用 `m_` 前缀
- [ ] `_init` 中 `memset` 清零子类字段 → 调用父类 `_init` → 设置 vtable；`_create` 中调用 `Set_Class_Memory`
- [ ] `_class_init` 中先 `XVTABLE_INHERIT_XCLASS` 再 `XVTABLE_OVERLOAD_DEFAULT`
- [ ] `_base` 函数中用 `ISNULL` 检查 self 和 vtable；头文件只暴露 `*_base`，不暴露内部 `VXxx_xxx`
- [ ] `copy`/`move` 支持未初始化目标（vtable 为空则兜底 init）；`copy` 中先释放旧值再深拷贝新值；`move` 中转移后清空源对象
- [ ] `deinit` 中释放资源后置 `NULL`；所有返回路径都清理栈上初始化的对象
