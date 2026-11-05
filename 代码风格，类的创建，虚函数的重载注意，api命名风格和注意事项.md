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
- [定时器使用规范（2026-10-26 裁定）](#定时器使用规范2026-10-26-裁定)：XObject 派生类内禁内嵌独立 XTimer
- [框架/应用分层写法（2026-10-29 裁定）](#框架应用分层写法2026-10-29-裁定)：框架/APP 写法分离，动态文本一律控件化
- **`_const` 所有权契约（2026-11-03 裁定）**：带 `_const` = 借用（禁止释放），不带 `_const` = 拷贝（调用方必须释放），见「获取函数的所有权契约」
- **`set*` 三态所有权契约（2026-11-03 裁定）**：`setXxx` 拷贝 / `setXxx_move` 抽取资源留空壳（外壳调用方释放）/ `setXxx_ref` 指针接管（所有权全转），见「`set*` 三态所有权契约」
- **`insert*` 三态所有权契约（2026-11-05 裁定）**：插入接口与 `set*` 同款三态——`insert_*` 拷贝 / `insert_*_move` 内容转移留壳 / `insert_*_ref` 指针接管，见「`insert*` 与 `set*` 同款三态」
- **键查询所有权契约（2026-11-05 裁定）**：`value_keyUtf8` 借用（缺失 NULL、禁止释放）/ `value_copy_keyUtf8` 堆拷贝（必须释放），JSON/BSON 同名同义，见「键查询接口的所有权契约」
- **字符串后缀契约（2026-11-03 裁定）**：带 `_utf8` = `char*`，不带 = `XString*`；存储优先 `XString`，仅"只记录不加工"用 `char*`，见「字符串 API 后缀契约」
- **`string.h` 移出白名单（2026-11-03 裁定）**：`memcpy`/`memset`/`memmove`/`memcmp` 一律改用 `XMemcpy`/`XMemset`/`XMemmove`/`XMemcmp`，见「白名单」

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
| `_base` | 所有虚函数的公共调度入口 | `XClassDeinit()`、`XExample_event_base()` |
| `_utf8` | UTF-8 编码版本 | `XString_append_utf8()` |
| `_gbk` | GBK 编码版本 | `XString_create_gbk()` |
| `_utf16` | UTF-16 编码版本 | 编码缓存相关 |
| `_move` | 移动语义：抽取入参内部堆资源，入参变空壳（**外壳仍由调用方释放**） | `XExample_setData_move()`、`XContainer_create_move()` |
| `_copy` | 深拷贝创建；查询接口中表示「返回堆拷贝，调用方释放」 | `XContainer_create_copy()`、`value_copy_keyUtf8()` |
| `_const` | 返回内部引用（**借用**，调用者禁止释放） | `const char* XExample_data_const(...)` |
| `_ref` | `set*/insert*_ref`：指针接管，**所有权完全转移**，调用方不得再释放或访问；`to*/from*_ref`：非拥有包装（借用） | `XModbusReply_setResult_ref()`、`XJsonValue_toVariant_ref()` |
| `_ref_2` | 带引用的重载版本（ref + 数字后缀） | |
| `_2`、`_3`... | 同功能不同参数的重载版本 | `XExample_setData_2()` |
| `_ex` | 扩展构造函数（比默认多参数） | `XContainer_create_ex(typeSize, useCow)` |
| `_later` | 延迟操作（通过事件循环执行） | `XObject_deleteLater()` |

> `_const`（借用，不转移）与 `_ref`（接管，转移所有权）在语义上相反但都节省拷贝，
> 区别是**谁负责最终释放**：`_const` 归原对象，`_ref` 归接收方。
> **注意 `_ref` 有两种用法**：`set*_ref` / `insert*_ref` 是所有权接管（调用即失所有权）；
> `to*_ref` / `from*_ref` 是**非拥有包装**（借用），删除包装前必须把
> `variant->m_data` 置 `NULL`，否则包装析构会释放被引用对象。
> 完整的 setter 三态契约见「`set*` 三态所有权契约」；插入接口同款三态见
> 「`insert*` 与 `set*` 同款三态」；键查询的借用/拷贝二分见「键查询接口的所有权契约」。

### 类型别名
- 用 `typedef` 给泛型容器定义语义化别名：
  ```c
  typedef XMapBase XFuncCodeMap;       // 功能码映射
  typedef XMap XVariantMap;            // Variant 键值映射
  typedef XHashMap XVariantHashMap;    // Variant 哈希映射
  ```

### 宏命名
- **全大写 + 下划线分隔**：`XCLASS_VTABLE_SIZE`、`XVTABLE_INIT_DEFAULT`、`ISNULL(args, str)`、`XAssert(args, str)`、`XNew(Type)`
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
| 动态内存 | `malloc`/`calloc`/`realloc`/`free` | `XMemory`（`Src/XMemory/XMemory.h`）/`XClass_Malloc`（见「内存管理注意事项」） |
| `strdup` | `XStrdup`（`Src/XCode/XAlgorithm/XStringUtils.h`，经 XMemory 分配器） |
| 字符串 | `string.h` 字符串函数 | `XString`/`XChar`；`strtok` 用 `XStrtokReentrant`（可重入无静态状态，线程安全，`XStringUtils.h`） |
| 容器 | 手工裸数组/链表、外部容器库 | `XVarList`/`XVariant` 等库内容器（见「容器与数据结构规范」） |

### 时间源规则（2026-09-27 裁定）

- **`Src/` 禁止直呼任何时间源**：`clock()`、`clock_gettime()`、`GetTickCount*`、`time(NULL)` 等一律禁用，统一经 `XDateTime`。
- **计时/超时/限流/去伪等时长测量**：用 `XDateTime_currentMSecsSinceEpoch()`——其实现已切换为**单调时钟**（posix=`CLOCK_MONOTONIC`，win32=`GetTickCount64()`），不受对时/跳变影响。注意返回值不再是纪元毫秒。
- **墙钟日期时间**（展示当前日期、落库时间戳等真纪元场景）：用 `XDateTime_currentDateTime()`/`XDateTime_currentSecsSinceEpoch()`，不得用单调毫秒拼时刻。
- 佐证案例：`XFileDialog.c` 的 `xff_nowMs` 与 `XWidget.c` present 限频已从 `clock_gettime`/`clock` 迁移；反面教材：若用单调毫秒初始化 `XDateTimeEdit` 默认值，会显示 1970-01-01 加开机时长。

### 白名单（语言基础，无库内等价，允许继续使用）

- 头文件：`stdbool.h`、`stdint.h`、`stddef.h`、`limits.h`、`float.h`、`inttypes.h`、`stdarg.h`、`assert.h`
- `math.h`（库内无数学模块）

> **`string.h` 已移出白名单（2026-11-03 裁定）。** 裸内存原语库内已有等价实现，
> 必须改用 `XMemory.h` 的同族接口：
>
> | 禁止 | 必须使用 |
> |------|---------|
> | `memcpy` | `XMemcpy` |
> | `memset` | `XMemset` |
> | `memmove` | `XMemmove` |
> | `memcmp` | `XMemcmp` |
> | `strlen` / `strcmp` / `strncmp` | `XString` 族（`XString_size`/`XString_equals`/`XString_startsWith_utf8`） |
> | `strdup` | `XStrdup`（见「库内替代对照」） |
>
> 语义与 libc 一致（`XMemcpy` 为编译器内建实现），迁移是纯替换，不改变行为。
> 例外仅剩：`Drive/` 平台适配层在与 OS API 直接对接时可按需使用；即便如此，
> 能用 `XMemory` 族表达时必须用 `XMemory` 族。用于完整对象的拷贝仍被禁止
> （见「内存管理注意事项」）。

### 例外与登记

- `Drive/` 平台适配层不受此限：调用 OS API 是平台适配层的职责（见「平台适配规范」）。
- 第三方库按「第三方库集成规范」三层架构接入；其适配层同样必须使用 `XMemory` 分配器。
- **新增任何外部依赖前，必须在本章节登记**：库名/头文件、用途、库内现有 API 为何不能替代。

---

## 类的创建

> ⚠️ **迁移说明（拷贝/移动 2026-09-08 起，反初始化/删除随后统一，本文示例均已按此更新）**：
> 拷贝/移动统一使用 `XClassCopy(dst, src)` / `XClassMove(dst, src)`（XClass.h 定义，多态分派到
> `VXType_copy/VXType_move` 虚槽），不再提供 `*_copy_base/*_move_base` 别名宏；
> 反初始化/删除同样已统一：派生类不再声明 `*_deinit_base`/`*_delete_base` 别名宏，
> 一律改用 `XClassDeinit(obj)` / `XClassDelete(obj)`（XClass.h 定义，仿 XClassCopy/XClassMove）。
> 继承 `XClass` 的类型只公开 `XClassCopy`/`XClassMove` 与 `XClassDeinit`/`XClassDelete`
> （`XClassDeinit`/`XClassDelete` 直接宏映射到带 XClass 指针转换的
> `XClass_deinit_base`/`XClass_delete_base`，这两个底层真实函数仍存在），
> **禁止**为继承 XClass 的类型声明或实现 `XType_deinit`、`XType_copy`、`XType_move`
> 这类非 base 转发 API；栈对象和堆对象都必须通过对应入口释放，虚表中的
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

// 反初始化/释放：统一用 XClassDeinit/XClassDelete（拷贝/移动用 XClassCopy/XClassMove，见本章节开头迁移说明）

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
        dest->m_data = XStrdup(src->m_data);
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
    XVTABLE_INIT_DEFAULT(XExample)                                 // 一体宏：建表+初始化（栈/堆由配置选择）
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
    XExample* obj = (XExample*)XMemory_calloc(1, sizeof(XExample), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XExample_init(obj);
    Set_Class_IsHeap(obj, true);   // 标记为堆对象（必做，配对 XClassDelete）
    return obj;
}

XExample* XExample_create_copy(const XExample* other)
{
    if (!other) return NULL;
    XExample* obj = XExample_create();
    if (!obj) return NULL;
    XClassCopy(obj, other);
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

1. `XVTABLE_INIT_DEFAULT(XExample)` —— 一体宏：创建虚函数表（单例模式）并按配置初始化（栈模式按该类枚举容量建栈表，堆模式建可扩容堆表），同时登记默认类名；
2. `XVTABLE_INHERIT_XCLASS(XParentClass)` —— 继承父类虚函数表；
3. `XVTABLE_ADD_FUNC_LIST_DEFAULT(table)` —— 添加自己新增的虚函数（`void* table[] = { VXExample_event, VXExample_childEvent };`）；
4. `XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExample_deinit)` 等 —— 重载父类虚函数；
5. `return XVTABLE_DEFAULT;`

> ⚠️ **迁移说明（2026-10 起）**：旧的 `XVTABLE_CREAT_DEFAULT` + `#if VTABLE_ISSTACK
> XVTABLE_STACK_INIT_DEFAULT(X)` / `XVTABLE_HEAP_INIT_DEFAULT` 分段写法已废弃，
> 由 `XVTABLE_INIT_DEFAULT(Type)` 一体宏取代（展开时完成分支选择，无运行时开销）。
> 新代码一律使用一体宏；文档历史示例中出现的分段写法均按此条替换理解。

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

- 生命周期入口 `XClassDeinit / XClassDelete` 一律宏复用父类；拷贝/移动统一用 `XClassCopy(dst, src) / XClassMove(dst, src)`（见「类的创建」迁移说明）。
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
XClassDeinit(&addr);            // 4. 反初始化（必须成对出现！）
```

### 堆上对象

```c
XHostAddress* addr = XHostAddress_create();     // 1. 创建对象
if (!addr) return;
XHostAddress_setAddress(addr, "192.168.1.1");   // 2. 使用对象
XClassDelete(addr);                 // 3. 删除对象
```

### init/deinit 成对原则

**重要**：`XType_init()` 和 `XClassDeinit()` 必须成对出现！

> **值类型补充（2026-11-05）**：`XJsonValue_init` / `XBsonValue_init` 一类
> `*_init(type)` 接口只设置类型标签，**不分配容器载荷**；`deinit` 对空载荷
> 必须安全。需要空容器时用 `create(type)`（BSON）或显式 `set*`（JSON）。
> 详见「值类型 `*_init(type)` 契约」。

```c
// ✅ 正确：init 与 XClassDeinit 成对
XHostAddress addr;
XHostAddress_init(&addr);
// ... 使用对象 ...
XClassDeinit(&addr);

// ❌ 错误：忘记 init 是未定义行为；忘记 XClassDeinit 是内存泄漏
XHostAddress addr;
XHostAddress_setAddress(&addr, "192.168.1.1");
```

### 已初始化区域的清空与容量复用

"`init` / `XClassDeinit` 必须成对"是对象生命周期规则，不表示每次重新写入都必须销毁并重新初始化对象。对于已初始化、生命周期仍在使用中、且会被反复写入的 `XRegion`，应优先使用 `XRegion_clear()` 清空元素并保留已有的矩形数组容量，禁止在高频路径中无意义地销毁重建：

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
- 当操作需要替换整个对象、替换底层数组、改变资源分配器或格式，必须保留对象替换流程，并保证旧资源只释放一次。涉及资源转移时使用类型已有的 `XClassMove` / 移动语义，不能用结构体赋值冒充所有权转移。
- 当输出对象必须接收新资源且需要保留强异常安全或失败回退语义时，可以先创建局部临时对象；临时对象必须先初始化。无论提交成功还是失败，临时对象的回收都按本指南「move 后源对象的清理责任」执行：由创建者按分配方式负责，不能为了减少一次释放而留下临时资源或悬挂指针。
- `XRegion_copy`、并集、交集、差集等写入已有输出对象的实现，应在容量足够时覆盖元素并更新 `count`，容量不足时才扩容；扩容失败应保留原输出内容，不能先 `deinit` 输出再尝试分配。
- 不能机械地全局删除 `deinit + init`：对象整体替换、失败路径清理、对象类型或分配器确实改变等场景仍必须走对应释放流程。被 move 的源对象由创建者按「move 后源对象的清理责任」释放，接收方不得在 move 现场代劳。

该规则同样适用于 `XImage`、`XPixmap`、容器和绘制状态等可复用资源，但必须先确认对应类型提供的 `clear`、容量复用或移动接口及其初始化前提；没有明确生命周期保证时，不得仅凭减少一次释放就修改代码。

### 按值返回带堆资源的结构体：所有权归属

有些 API **按值返回一个结构体**，而该结构体内部持有堆指针（如 `XRegion` 的 `XRect* rects`）。这类返回值是**一次深拷贝**：函数内部新分配了堆内存，把指针塞进返回值里。此时**所有权转移给调用方**，调用方必须负责释放。

```c
// XRegion 的 rects 是堆指针，XRegion_copy 用 XRealloc_System 分配
typedef struct XRegion
{
    XRect* rects;   // 堆内存，由对象拥有
    int    count;
    int    capacity;
} XRegion;

// 按值返回深拷贝：调用方拿到 rects 的所有权，必须 XRegion_deinit
XRegion XPaintEvent_region(const XPaintEvent* event);
```

**硬性要求**：

- **返回值的所有权必须写进头文件注释**。凡是按值返回带堆资源的结构体的 API，注释必须写明「返回值是深拷贝／调用方负责 `Xxx_deinit`」或「返回值是借用／调用方不得释放」，二者必居其一，**不许留白**。函数名和返回类型本身表达不了所有权——`XRegion` 只是一层薄壳，看不出里面有没有堆指针。
- **取到返回值就必须释放**，且**每条出口都要释放**：正常返回路径、`early return`、错误分支，一个都不能漏。
- **高频路径优先用借用版 API**。如果每帧都要用这个区域，应提供一个只读借用版（返回 `const XRegion*`，零拷贝、不转移所有权），让调用方根本拿不到需要释放的东西——**从根上消除"忘了 deinit 即泄漏"**，而不是靠每次记得写对。
- **确实需要独占副本时**：先用 `XRegion_clear()` 复用已有容量，再 `XRegion_copy()` 写入；用完 `XRegion_deinit()`。见上一节「已初始化区域的清空与容量复用」。

```c
// ❌ 错误：取了深拷贝却不释放 ⇒ 每帧泄漏一块矩形数组
//    （实测 RK3568 板端 27 次/秒 × 64 字节 ≈ 107 KB/min 线性增长）
XRegion region = XPaintEvent_region(event);
for (int i = 0; i < region.count; ++i) { /* ... 只读 ... */ }
// 函数返回，region.rects 的所有权随栈帧一起消失 ⇒ 泄漏

// ✅ 正确 A：高频路径用借用版，零分配、零释放
const XRegion* region = XPaintEvent_region_const(event);
for (int i = 0; i < region->count; ++i) { /* ... 只读 ... */ }

// ✅ 正确 B：确需独占副本时，复用容量 + 末尾释放
XRegion region;
XRegion_init(&region);
XRegion_copy(event->m_region, &region);   // 写入已初始化的 region
/* ... 使用 ... */
XRegion_deinit(&region);                   // 无论从哪个出口返回都要走到
```

**借用版的借用期**：只读借用指针在**事件（或被借用对象）的生命周期内**有效，不得跨调用长期持有；事件销毁后即失效。借用版必须返回 `const` 指针，让误写成为编译错误。

**自检**：新增或修改此类 API 时，问三个问题——① 返回值的所有权在头文件注释里写清了吗？② 调用方的**每一条**出口都释放了吗？③ 这是高频路径吗（能否换成零拷贝借用版）？

### 快速初始化宏

对于简单场景，可以使用快捷宏：

```c
XString_Init_Utf8(str, "Hello World");        // 栈上字符串快速初始化
XClassDeinit(str);

XString_Init_Fmt_Utf8(msg, "Error code: %d", errorCode);  // 格式化字符串
XClassDeinit(msg);
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
XClassCopy(&dest, &src);               // 多态拷贝
XClassDeinit(&dest);
XClassDeinit(&src);

// 栈上移动
XExample dest, src;
XExample_init(&dest);
XExample_init(&src);
XClassMove(&dest, &src);               // src 资源转移到 dest
XClassDeinit(&dest);
// src 已为空；创建者仍需 XClassDeinit 完成生命周期收尾（堆对象则用 XClassDelete）
XClassDeinit(&src);
```

#### copy/move 目标初始化约束

- 所有 `copy` / `move` 虚函数，以及负责私有数据复制或移动的辅助函数，在访问目标成员前必须检查目标对象的 vtable：目标未初始化（vtable 为空）时，必须先调用对应类型的 `XType_init()` 完整初始化（包括分配资源、设置 vtable），再释放旧资源或写入新资源。这样 `XClassCopy`/`XClassMove` 可以安全地在未 init 的目标上调用，`XType_create_copy(src)` 这类便捷 API 无需关心 dest 是否已 init；调用方也可以先 `XType_init(&dest)` 再拷贝（检测到 vtable 非空，不会重复 init）。
- 源对象未初始化时必须安全返回；源对象与目标对象相同时，`copy` / `move` 必须直接返回。
- 目标对象初始化检查应放在虚函数实现或私有操作入口中，不能只依赖调用方提前 `init`。
- ❌ 错误写法：只 set vtable、没分配成员——`if (XClassIsVtableNull(dest)) { XClassSetVtable(dest, XExample); }`，成员还未分配。也不用 `XClassEnsureVtable` 之类只设置 vtable 的捷径，对 copy/move 来说不够。

#### move 后源对象的清理责任

`XClassMove` / 移动虚槽只转移资源所有权。移动完成后，源对象处于"已初始化但为空"的状态：虚表、内存方法和堆所有权标记保持不变，源对象仍可被安全地 `XClassDeinit` 或 `XClassDelete`。**清理责任不随 move 转移，仍属于创建者；move 现场只转移资源，不代替创建者清理源对象。**

- 只有创建者知道源对象是栈对象还是堆对象：栈对象由 `XType_init()` 建立，用 `XClassDeinit()` 释放；堆对象由 `XType_create()` / `XType_create_ex()` 等建立，用 `XClassDelete()` / `XClassDelete()` 释放（`XClassDelete` 内部先反初始化，再按堆所有权释放结构体）。
- 当源对象来自参数、对象成员或调用方输出时，执行 move 的函数只是接收方，不得在 move 现场调用 `XClassDeinit` 或 `XClassDelete`：接收方不知道源对象的分配方式，擅自清理会造成错误释放或双释放。最终清理仍由创建者在自己的清理点完成。
- 当函数自己创建临时对象并把资源 move 出去时，该函数就是创建者：栈临时对象在作用域结束时按栈上对象规则收尾（`XClassDeinit`，资源已转出时为空操作）；堆临时对象必须由创建者 `XClassDelete`，不能因"源已为空"跳过结构体释放。
- 移动后仍继续存活的源对象（控件成员、容器元素等）保持原有清理点不变：由拥有它的对象在自身的 `deinit` / 私有释放路径中统一清理，不在 move 现场重复释放。
- **值类型移动后源类型清零**（2026-11-05 裁定）：`XJsonValue_move` /
  `XBsonValue_move` / 各 `*_create_move` 执行后，源对象类型置为
  `XJsonValue_Invalid` / `XBsonValue` 的 `type 0`（数值均为 0），载荷清零。
  此后源对象只允许 `deinit`/`delete` 或重新 `set*`，不得按原类型解引用。

```c
// 接收方只转移资源，不清理源；源与目标都由各自的创建者按分配方式释放
XExample* heapSrc  = XExample_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
XExample* heapDest = XExample_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
XClassMove(heapDest, heapSrc);     // heapSrc 变为空对象
XClassDelete(heapDest);   // 创建者：堆对象用 XClassDelete
XClassDelete(heapSrc);    // 创建者：空源对象同样 XClassDelete（安全）
```

### deleteLater 模式

对于被事件系统引用的对象，不能直接 `delete`，需要使用延迟删除（通过事件循环在下次事件处理时安全释放）：

```c
XObject_deleteLater(obj);
```

#### 对象释放方式选择约束（2026-10 裁定，对标 Qt deleteLater 语义）

XObject 派生对象（结构体首成员为 XObject，或 `_init` 调用 `XObject_init`）**何时必须延迟释放、何时必须同步释放**，按以下判据执行：

**必须延迟释放（堆对象用 `XObject_deleteLater`，栈对象用 `XObject_deinitLater`）：**

1. 删除动作发生在对象自身的事件/信号调用栈上——事件处理器（`VXxx_event`）、槽函数（经 `XObject_connect` 注册的回调）、观察者回调、输入处理路径中删除"自己"或删除当前发射链上的对象；
2. 对象可能仍有未处理的投递事件（`XObject_postEvent` 已投递、输入事件在途）；
3. 对象可能在信号发射中途被删除，导致其他连接方在发射循环中访问已释放内存。

```c
// ✗ 错误：槽函数内同步删除自身参与发射链的对象（悬垂）
static void VXView_onClose(XView* self, XVarList* args) {
    XClassDelete((XClass*)self);        // 发射循环可能还在遍历本对象连接
}
// ✓ 正确
static void VXView_onClose(XView* self, XVarList* args) {
    XObject_deleteLater((XObject*)self);
    XCoreApplication_processEvents(XEventLoop_AllEvents);  // 需立即回收时由事件循环统一处理
}
```

**必须同步释放（禁止改为延迟）：**

1. 析构函数内部（`VXxx_deinit`）清理自己的成员与子对象——延迟删除析构路径会引入重入与顺序问题；
2. 栈上/嵌入对象的 `XClassDeinit` 收尾；
3. 非 XObject 的值类型（XString/XVariant/容器等，无 deleteLater 入口）；
4. 能证明对象无任何信号槽连接、无在途事件且删除不在对象自身调用栈上的纯业务对象（此时同步释放成本更低、语义更明确）。

**信号槽连接的自动断开不构成豁免**：连接虽随对象析构自动清理，但"析构时机"本身若可能落在发射循环/事件在途窗口内，就必须延迟释放。无法证明安全时，选择延迟释放并注明理由。

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
| `XType_setXxx_move(XType*, value)` | 移动语义版本（抽取资源，留下空壳） |
| `XType_setXxx_ref(XType*, ptr)` | 指针接管（所有权转移给被设对象） |
| `XType_setXxx_2(XType*, other_type)` | 不同参数类型的重载 |
| `XType_setXxx_utf8(XType*, const char*)` | UTF-8 编码版本 |
| `XType_setXxx_gbk(XType*, const char*)` | GBK 编码版本 |

### `insert*` 与 `set*` 同款三态

插入接口（容器/文档/对象追加元素）与 setter 使用完全相同的三态命名与义务，
不得因为「只是插入」就放松所有权要求：

| 形态 | 命名 | 内部动作 | 函数返回后调用方的义务 |
|------|------|----------|----------------------|
| **拷贝** | `XType_insert_xxx(obj, key, const T*)` | 深拷贝一份进容器 | 原参数**仍归调用方**，用完自行释放 |
| **移动** | `XType_insert_xxx_move(obj, key, T*)` | 抽取参数资源进容器，参数变**空壳** | **必须释放外壳**（`XClassDelete`），不得再解引用 |
| **指针接管** | `XType_insert_xxx_ref(obj, key, T*)` | 直接把指针交给容器（零分配） | **不得释放、不得访问**（所有权已交出；失败也由被调方释放） |

- 典型实现：`XJsonObject_insert_keyUtf8_string{,_move,_ref}`、
  `XBsonDocument_insert_keyUtf8_array{,_move,_ref}`；
- 插入接口的 `_move` 同样只转移内容：`XString`/`XJsonArray`/`XBsonDocument`
  等源对象空壳仍由调用方释放（`XJsonObject_insert_keyUtf8_string_move`
  曾直接接管指针、与 BSON 相反，2026-11-05 已统一为留壳语义）；
- `_ref` 的返回 `false` 表示「参数非法或分配失败」，**资源已由被调方释放**，
  调用方不要再碰；NULL 参数直接返回 `false` 且不接管。

### `set*` 三态所有权契约（拷贝 / 移动 / 指针接管）

> **这是硬约束。三种形态存在的唯一目的是性能：能不动内存就不动。**
> 选错形态会直接导致内存泄漏、双重释放或 UAF，三者代价相同，都不可接受。

| 形态 | 命名 | 入参 | 内部动作 | 函数返回后调用方的义务 |
|------|------|------|----------|----------------------|
| **拷贝** | `setXxx` | `const T*` | 深拷贝一份，被设对象持有副本 | 原参数**仍归调用方**，用完自行释放 |
| **移动** | `setXxx_move` | `T*` | 抽取参数内部堆资源，参数变**空壳** | **必须释放外壳本身**（`XClassDelete`），但不得再解引用其内容 |
| **指针接管** | `setXxx_ref` | `T*` | 直接存指针，被设对象持有该对象 | **不得释放、不得访问**（所有权已完全交出） |

#### 三者对照（以 `XVariant` 参数为例）

```c
/* ---------- 1. 拷贝：最安全，成本最高 ---------- */
XVariant* v = XVariant_create_int(1);
XModbusDevice_setConnectionParameter(dev, XModbusDevice_SerialBaudRateParameter, v);
XClassDelete((XClass*)v);           /* ✅ 调用方保留所有权，必须释放 */

/* ---------- 2. 移动：省掉深拷贝，外壳仍要自己清 ---------- */
XVariant* v = XVariant_create_int(1);
XModbusDevice_setConnectionParameter_move(dev, XModbusDevice_SerialBaudRateParameter, v);
XClassDelete((XClass*)v);           /* ✅ 内部资源已被抽走，只剩空壳需要释放 */
/* ❌ 不能写 XVariant_toInt(v) —— 已被抽空，读到的是空值 */

/* ---------- 3. 指针接管：零拷贝零分配 ---------- */
XModbusDataUnit* unit = XModbusDataUnit_create();
XModbusReply_setResult_ref(reply, unit);
/* ✅ 什么都不做 —— 所有权已转移给 reply */
/* ❌ 绝不能 XClassDelete(unit);  double free */
/* ❌ 绝不能 XModbusDataUnit_read(unit, ...);  可能已被 reply 释放 */
```

#### 落地要求

1. **`_move` 必写"外壳由调用方释放"**：这是全库最容易误解的一条。`_move` 转移的是**容器内部的堆资源**（`m_data`、`m_children` 等），不是对象本身。对象头（`XClass` 壳）仍在调用方栈/堆上，必须按原分配方式释放：
   - 堆对象（`Xxx_create` 出来的）→ `XClassDelete`
   - 栈对象（`Xxx_init` 出来的）→ `XClassDeinit`

   ```c
   /* 头文件注释必须这样写 */
   * @param value 参数值（XVariant；函数内经 XClassMove/create_move 抽取其资源，
    *        返回后 value 变为空壳——内存仍由调用方按原分配方式释放，
    *        此后不得再解引用其内容）。
   ```

2. **`_ref` 是所有权转移，不是"引用"**：名字里的 ref 指"直接把传入指针交给内部持有"，调用方交出的不只是访问权而是**完整的释放责任**。头文件必须写 `@warning`：
   ```c
   * @warning unit 必须是可由 XClassDelete 释放的堆对象；登记后调用方再释放即双重释放。
   ```

3. **`_ref` 只接受堆对象**：被设对象最终会用 `XClassDelete` 释放它，传栈对象必然崩。文档里的"引用"二字**不表示可以传借用指针**。

4. **`setXxx`（拷贝）入参必须 `const`**：拷贝语义不改变入参，加 `const` 让编译器兜住误改；`_move`/`_ref` 会改变所有权，入参必须是可写 `T*`。

5. **同一属性需要多形态时必须成对/成套提供**，禁止只给 `_move` 不给 `setXxx`（调用方没有安全退路）。典型成套参考（全库 20 处三件套）：
   `XModbusReply_setResult{,_move,_ref}`、`XJsonValue_setVariant{,_move,_ref}`、
   `XMap_setVariant{,_move,_ref}`、`XString_setVariant{,_move,_ref}`。

6. **`_ref` 覆盖语义**：内部已有旧值时先释放旧值再登记新指针（`XModbusReply_setResult_ref` 即此形态）。传 `NULL` 表示仅清空，不登记。

7. **性能取向的默认选择**：
   - 小标量 / 需要长期独立保存 → 拷贝（`setXxx`）
   - 大块数据（字符串、数组、容器、PDU）从临时对象转移 → 移动（`_move`）
   - 调用方本就把对象造好且不再使用 → 指针接管（`_ref`）

#### 全库现状与已修正的历史分歧（2026-11-03 扫描）

- 2927 个 setter 基名中，仅 **36 个**带 `_move`、**18 个**带 `_ref`；真正三件套齐全的 18 个集中在
  `XModbus*`、`XPlc*` 与 `XVariant` 转换族（`XJson*`/`XBson*`/`XMap`/`XHashMap`/`XPair`/
  `XString`/`XStringList`/`XByteArray`/`XVariantList`）。
- **已修正**：`XModbusDevice_setConnectionParameter_ref` 与 `XPlcDevice_setConnectionParameter_ref`
  原本名为 `_ref` 但实现是深拷贝（2026-10-30 UAF 根修时改的），与本节契约冲突。
  两个函数与无后缀的 `setConnectionParameter` 行为完全等价，属纯冗余且语义误导，
  **已连同声明一并删除**，全库 60 余处调用点统一改用无后缀拷贝版。教训：
  修语义时先看名字是否还成立——名字错了就改名字，不要留一个名不副实的导出函数。

> **新增 `_move` / `_ref` 前请先自问**：这个属性的拷贝成本真的高到需要加变体吗？
> 库里 2927 个 setter 只有 36 个带 `_move`，说明绝大多数属性不需要。三态并存会增加
> 调用方的心智负担与误用风险，只有在**大块数据 + 高频调用**时才值得。

---
### 字符串 API 后缀契约：`_utf8`（设置）/ `_toUtf8`（获取）= `char*`，无后缀 = `XString`

> **2026-11-03 裁定：后缀决定参数/返回值的字符串类型，这是硬约束。**
> **设置方向的 `char*` 用 `_utf8`，获取方向的 `char*` 用 `_toUtf8`，两端都不带后缀则是 `XString*`。**

```c
/* ✅ 正确：无后缀 = XString */
void        XWidget_setTitle(XWidget* self, const XString* title);
const XString* XWidget_title_const(const XWidget* self);
XString*    XWidget_title(const XWidget* self);          /* 拷贝版，调用方释放 */

/* ✅ 正确：设置侧 char* 用 _utf8 */
void        XWidget_setTitle_utf8(XWidget* self, const char* titleUtf8);

/* ✅ 正确：获取侧 char* 用 _toUtf8 */
const char* XWidget_title_toUtf8(const XWidget* self);   /* 借用视图，禁止释放 */
```

```c
/* ❌ 错误：无后缀却是 char*（两个方向都违规） */
void        XWidget_setTitle(XWidget* self, const char* title);   /* 应为 _utf8 */
const char* XWidget_title(const XWidget* self);                   /* 应为 _toUtf8 */
void        XWidget_setTitle_2(XWidget* self, const char* title); /* _2 不表达 UTF-8 */
```

#### 落盘要求

1. **无后缀一律 `XString*`**：`XType_setName(XType*, const XString*)`、`XType_getName(const XType*)` 返回 `XString*`。
2. **设置方向 `char*` 用 `_utf8`**：`XType_setName_utf8(XType*, const char*)`。
3. **获取方向 `char*` 用 `_toUtf8`**：`const char* XType_name_toUtf8(const XType*)`。命名遵循库内既有的 `XString_toUtf8` 惯例（"转换到 UTF-8 视图"），语义是**借用视图，调用者不得释放**。
4. **禁止用 `_2` / `_3` 表示字符串类型差异**：`_2`/`_3` 只表示"同语义、不同参数个数或不同参数类型"的重载（例如 `setRect(x,y,w,h)` 的两个重载）。**凡是参数/返回值是 `char*` 与 `XString*` 的差异，必须用 `_utf8`（写）/ `_toUtf8`（读）后缀区分。**
5. **可以同时提供两个版本**：鼓励成对提供 `setName`（XString）与 `setName_utf8`（char*）、`name`（XString 拷贝）/ `name_const`（XString 借用）/ `name_toUtf8`（char* 借用）。`_utf8`/`_toUtf8` 只做"临时 XString ↔ 文本"的转换转发，禁止维护第二份字符串状态。
6. **编解码**：`_utf8` / `_toUtf8` 一律按 UTF-8 处理，不得当作本地编码、GBK 或可写缓冲区；需要 GBK 时用 `_gbk`。

#### 内部存储选型（性能取向）

**字符串成员尽量直接用 `XString`**，只有"基本不需要字符串操作、只是原样记录"的场景才用 `char*`。

| 场景 | 存储 | 理由 |
|------|------|------|
| 需要比较/拼接/查找/截取/大小写转换 | `XString*` | 库内字符串算法齐全，免得每次转换 |
| 需要多次读取、跨调用保持 | `XString*` | 长度与编码已知，读写零转换 |
| 需要作为容器元素/键值保存 | `XString*` / `XStringList` | 统一所有权与生命周期 |
| **只管原样保存和回读、从不做字符串运算** | `char*`（UTF-8，NULL 终止） | 省一层对象头与转换，最省内存 |
| 协议字段、固定枚举名、路径常量 | `char*` | 只记录不再操作 |

```c
/* 例：状态机属性赋值记录只按名字匹配（比较时现场构造 XString 或 XStrcmp），
 * 从不做字符串运算 → 用 char* 更省。 */
typedef struct XState_PropertyAssignment {
    XObject*  m_object;
    char*     m_propertyName;   /* UTF-8 文本，记录自持；不再当字符串加工 */
    XVariant* m_value;
} XState_PropertyAssignment;
```

#### 与其他约定叠加时的优先级

- 借用 vs 拷贝：先按 `_const` 契约判断（`_const` 借用禁止释放 / 无 `_const` 拷贝须释放），再按本节的 `_utf8` 判断类型。
- 借用版 UTF-8：`const char* XType_name_utf8(const XType*)` —— 既是 `_utf8`（char*）又是借用（禁止释放），两条约束同时适用。
- 拷贝版 XString：`XString* XType_name(const XType*)` —— 无 `_utf8`（XString）且无 `_const`（拷贝，调用方必须释放）。

### 获取函数命名

| 函数名 | 用途 |
|--------|------|
| `XType_xxx(const XType*)` | 获取属性（返回值拷贝） |
| `XType_xxx_const(const XType*)` | 获取属性（返回内部 const 引用，不拷贝） |
| `XType_toXxx(const XType*)` | 转换为其他类型（返回新对象） |
| `XType_isXxx(const XType*)` | 布尔判断函数 |

### 获取函数的所有权契约（`_const` 后缀 = 借用，非 `_const` = 拷贝）

> **这是硬约束，不是风格偏好。带不带 `_const` 决定调用方要不要释放返回值。**

| 形态 | 返回物 | 所有权 | 调用方义务 |
|------|--------|--------|-----------|
| `const XType* XFoo_bar_const(...)` | 对象内部持有者的**借用指针** | 归对象所有 | **禁止释放**，禁止缓存后跨调用使用 |
| `const char* XFoo_bar_utf8(...)` 等借用视图 | 内部缓存区的**借用视图** | 归对象所有 | **禁止释放**，内容随下次写入/析构失效 |
| `XType* XFoo_bar(...)`（无 `_const`） | **新分配的拷贝** | 归调用方 | **必须释放**（`XClassDelete`），否则泄漏 |

**`_const` 版（借用）**

```c
/* 头文件：返回内部引用，随 foo 一起失效 */
const XString* XFoo_errorString_const(const XFoo* foo);
```

```c
/* 调用方：只读，不释放。用完即弃，不要长期持有。
 * 目标对象一旦 clearError/析构，这个指针立刻悬垂。 */
const XString* text = XFoo_errorString_const(foo);
XPrintf("%s\n", XString_toUtf8(text));
/* 绝不能写 XClassDelete((XClass*)text); */
```

**非 `_const` 版（拷贝）**

```c
/* 头文件：返回新建副本，调用方接管 */
XString* XFoo_errorString(const XFoo* foo);
```

```c
/* 调用方：必须释放，且每个出口都要释放 */
XString* text = XFoo_errorString(foo);
if (!text) { return; }
/* ... 使用 ... */
XClassDelete((XClass*)text);   /* 漏了这行就是泄漏 */
```

**落地要求**

1. **同名成对提供**：同一属性若要同时支持借用与拷贝，必须成对声明
   `XFoo_bar_const`（借用）与 `XFoo_bar`（拷贝），并在注释里分别写明
   "调用者不得释放" 与 "由调用者释放"。禁止只给其中一个让调用方猜。
2. **注释必须写明所有权**：借用版写 `@warning 返回值归对象所有，随 X/Y/析构失效，
   调用者不得释放或修改`；拷贝版写 `@return 新建副本，调用方用 XClassDelete 释放`。
3. **借用版禁止返回可变指针**：返回类型必须是 `const T*`（或 `const char*` 视图），
   让编译器协助阻止调用方就地改写内部状态。
4. **借用视图不可跨写缓存**：`XString_toUtf8` 一类视图指针在下次写入或对象析构后失效；
   需要长期保存时必须 `XStrdup` 或转成拷贝版。
5. **不要把借用的指针塞进容器或对象字段**：容器/字段的生命周期通常长于被借用对象，
   这是悬垂指针的典型来源。确需保存就取拷贝版，并自己管理释放。

**真实踩坑案例（2026-11-03，XStateMachine 属性赋值）**

`XState_assignProperty` 曾用 `XString_create_copy` 保存属性名，随后调用方释放了原键名。
`XString_create_copy` 走 COW，与源**共享底层缓冲**，源对象释放后记录里的名字指针指向
已回收的共享块 —— 第二次登记同一属性时必然段错误。教训：

- **"看起来像拷贝"的 API 不一定是拷贝**，`XString_create_copy` 是 COW 共享；
- 需要真正独立的副本时用 `XStrdup`（独立缓冲），或立刻把内容取走（`XString_toUtf8` + `XStrdup`）；
- 写这类代码前先读一遍本节的借用/拷贝二分表，别靠函数名猜。

### 键查询接口的所有权契约（`value_keyUtf8` 借用 / `value_copy_keyUtf8` 拷贝）

> 2026-11-05 裁定。这是对上一节 `_const` 通用规则的**显式例外**：键查询是高频
> 热路径，历史 API 名称已铺开，故用 `_copy` 区分所有权，但**两库必须同名同义**。

| API | 返回 | 调用方义务 |
|-----|------|-----------|
| `XJsonObject_value_keyUtf8` / `XBsonDocument_value_keyUtf8` | **借用**（缺失返回 `NULL`） | **禁止释放**；容器插入/删除/清空后失效，不得跨修改持有 |
| `XJsonObject_value_copy_keyUtf8` / `XBsonDocument_value_copy_keyUtf8` | **堆拷贝**（缺失返回 `NULL`） | **必须释放**（`XJsonValue_delete` / `XBsonValue_delete`） |

- **禁止一库借用、一库拷贝**：JSON 与 BSON 必须保持一致（2026-11-05 已统一）；
- 借用版返回只读指针语义（`const XJsonValue*`）；实现若返回可变指针，调用方
  也只读使用，禁止原地修改（COW 共享数据）；
- 键查询的借用指针不保证跨容器修改有效：拿到后立即取值/判型，不要长期保存；
- 需要返回值在容器修改后仍有效时，一律用 `_copy_keyUtf8`，并在每个出口释放。

```c
/* 借用：只读、不释放 */
const XJsonValue* v = XJsonObject_value_keyUtf8(obj, "k");
if (v && XJsonValue_isInt(v)) { ... }

/* 拷贝：独立生命周期，必须释放 */
XJsonValue* owned = XJsonObject_value_copy_keyUtf8(obj, "k");
if (owned) { ...; XJsonValue_delete(owned); }
```

### 值类型 `*_init(type)` 契约（2026-11-05 裁定）

`XJsonValue_init` / `XBsonValue_init` 等值类型初始化接口**只设置类型标签，
不分配容器载荷**（String/Array/Object/Document 的 `data` 初始为 `NULL`）。
需要「可直接 `toDocument/toArray/toString` 的空容器」时：

- BSON：用 `XBsonValue_create(type)`（create 会额外分配空载荷）；
- JSON：用 `setString_utf8(v, "")` / `setArray(v, empty)` / `setObject(v, empty)`。

`move` / `create_move` 后源对象类型清零（JSON `Invalid`、BSON `type 0`，数值
均为 0），此后只允许 `deinit/delete`，不得再解引用内容。

### 返回值规范

- **创建函数**：成功返回对象指针，失败返回 `NULL`
- **设置函数**：成功返回 `true`，失败返回 `false`
- **获取函数**：返回值或指针，失败返回默认值（0/NULL/false）；**所有权按上一节的 `_const` 契约判定**
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
- `@return` 说明所有成功、失败、空结果和越界路径；返回对象若需要调用 `XClassDelete`，必须明确写出。
- `@note` 用于生命周期、隐式共享、UTF-16 索引、线程安全和 Qt 对齐细节；`@warning` 只用于错误后果明确的误用风险。
- UTF-8 接口要写明输入按 UTF-8 解码，`XString` 接口要写明字符串内部按 UTF-16 代码单元处理。索引和长度若按 UTF-16 代码单元计数，必须在参数说明中明确。
- 空句柄采用"对象已初始化但内部节点为空"的表述；NULL 指针采用"调用者没有提供对象"的表述，二者不得混写。
- `init`、`create`、`create_copy`、`create_move`、`XClassDeinit`、`XClassDelete` 都必须说明初始化前提、目标未初始化时的行为、源对象在移动后的状态和释放方式。
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
XClassCopy(&dest, &src);
XClassDeinit(&dest);
XClassDeinit(&src);
```

### 拷贝/移动/释放入口

| 函数 | 用途 |
|------|------|
| `XClassCopy(dest, src)` | 深拷贝对象（多态分派；回调槽位需要函数指针时用 `XClass_copy_base`） |
| `XClassMove(dest, src)` | 移动语义，转移资源所有权（函数指针用 `XClass_move_base`） |
| `XClassDeinit(obj)` | 反初始化对象（释放资源） |
| `XClassDelete(obj)` | 删除堆对象（反初始化 + 释放内存） |

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
char* text = XStrdup(source);
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
    XClassDelete(str);
}
XClassDelete(addr);
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
    XVTABLE_INIT_DEFAULT(XExample)
    // ... 继承/添加/重载虚函数 ...
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
    XVTABLE_INIT_DEFAULT(XClass)
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
    XVTABLE_INIT_DEFAULT(XThread)
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
- 容器字段应嵌入对象或由对象明确拥有，并在 `init` 中初始化，在 `XClassDeinit`/私有释放路径中调用对应的反初始化或 `*_clear_base`。不得把容器元素指针直接当作独立堆对象数组管理。
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

### XString 使用注意事项（2026-10-29 审计裁定）

XString 内部以 **UTF-16** 存储（`XChar` = `uint16_t`），且首成员是容器基类
`XContainer`（首字段为 vtable 指针）。本节全部约束都由这两条事实派生。
全库审计（atoi 族 213 处 + 八类误用模式约 200 处）确认存量代码已符合本节规则，
新代码必须维持。

#### 对象指针禁止当 `char*` 使用

`XString*` 喂给任何 `char*` 形参的函数（`atoi`/`strtol`/`strlen`/`strcmp`/
`printf` 族 `%s`/`sscanf` 等），解析到的是 vtable 指针字节：数值解析恒失败
返 0，打印输出乱码。C 编译器对此**只给一条警告**（`-Wincompatible-pointer-types`），
照常出二进制，失败完全静默。实证（2026-10-29 ×2）：Modbus 波特率下拉把
`XComboBox_currentText` 返回值直喂 `atoi`，恒返 0 走 9600 兜底（日志显示
115200、实际 open 9600）；`XModbusTcpServer` 把 `XString_constData` 传给
`XHostAddress_create_fromString`，"127.0.0.1" 被截成 "1"。

数值解析必须走自带 API（带 `ok` 出参与进制基）：

```c
bool ok = false;
int v       = XString_toInt(str, &ok, 10);
long long l = XString_toLongLong(str, &ok, 16);
double d    = XString_toDouble(str, &ok);
```

需要 `const char*` 时经转换出口取（三者都返回**内部编码缓存指针**，生命周期见下）：

| 需求 | API | 返回 |
|---|---|---|
| UTF-8 | `XString_toUtf8(str)` | `const char*` |
| 本地编码 | `XString_toLocal(str)`（宏别名 `XString_c_str`） | `const char*` |
| GBK | `XString_toGbk(str)` | `const char*` |

#### UTF-16 访问器禁止喂 `char*` 函数

以下访问器返回 **UTF-16 内部数据**（`XChar*`/`const XChar*`/`uint16_t*`），
传给 `char*` 形参不是返 0 而是**乱码/静默截断**（ASCII 串第二个字符的高位
`\0` 截断，短串"看起来正常"更难发现）：

- `XString_data`（可写）、`XString_unicode`（宏别名 `XString_constData`）、
  `XString_utf16`、`XString_toUtf16`，及 `XStringView_*` 同语义族；
- 仅用于 UTF-16 互操作（如作为 `XString_create_utf16` 来源、宽字符 API 入参）。

#### 内容比较必须走 `XString_equals`

`==`/`!=` 比较的是**对象身份**（指针），不是内容。内容比较一律
`XString_equals(l, r, cs)` / `XString_equals_utf8(str, utf8, cs)`。
`==` 仅允许三种身份快路：①自赋值/自拷贝守卫；②排序比较器的 NULL/同对象
快路；③getter「未命中时原样返回 default 指针」的身份协议（XDom 属性缓存模式）。

#### 编码缓存与缓存指针生命周期

编码访问按需转换并缓存（5 个槽：Local/Utf8/Utf16/Utf32/Gbk），转换结果由
字符串对象持有。`toUtf8/toLocal/toGbk` 返回的**内部缓存指针**遵守：

- 源串任何修改（`assign/append/prepend/insert/remove/slice/clear` 等）或对象
  删除，缓存指针立即失效；
- 禁止把该指针存入对象成员、容器或跨函数长期持有；禁止 `return` 它；
  需要长期 `char*` 时用 `XStrdup` 自行分配并按 XMemory 规则释放。

```c
const char* s = XString_toUtf8(str);
XString_append_utf8(str, "!");   /* ❌ 此刻起 s 已失效 */
use(s);                           /* ❌ 悬空读 */
```

#### 新建对象必须由调用方删除

生产者 API 返回**新建副本**，调用方负责 `XClassDelete`：`XString_create*`
全族、`left/right/mid/toLower/toUpper/trimmed/fromVariant`，以及控件取值类
如 `XComboBox_currentText`（借用版走 `*_const`/`_2` 后缀变体，见「字符串 API
主次顺序与 UTF-8 兼容重载」）。移交所有权走 `*_move`/容器 `push_back_move`
并注释说明。

```c
XString* text = XComboBox_currentText(combo);   /* 新建副本 */
int baud = text ? XString_toInt(text, NULL, 10) : 0;
if (text) XClassDelete((XClass*)text);           /* ✅ 用完即删 */
```

#### 按值内嵌成员必须逐构造路径 init

结构体按值内嵌 `XString` 成员是合法模式（如 `XMqttStringPair::m_name`、
`XDeviceRegistryEntry::m_className`），但所有者**每一条构造路径**（`_init`、
`_create`、数组逐元素扩容、迭代器新建）都必须对该成员 `XString_init`，
释放路径配对 `XClassDeinit`；未经 init 直接使用 = vtable 未就绪，未定义行为。

#### 禁止结构体赋值浅拷贝

`*a = *b` 绕过 COW 引用计数 ⇒ 双重释放/UAF；统一走 `XClassCopy`/`XClassMove`
（见「对象生命周期管理」）。另见「常见错误与最佳实践」第 10 条
（`XString_constData` 实锅）。

---

## 信号与槽规范

### ⚠ 首要约束：槽函数第一个参数的语义由连接 API 决定，不可凭直觉假设

本框架的槽函数有**两种签名**，分别对应两种连接 API。**写槽函数前必须先确认自己用的是哪个 API**，
否则会把「连接接收者」当成「信号发送者」，读出完全错位的字段——且**不会崩溃、不报错**，
表现为"数据莫名其妙是 0/旧值"，极难定位。

```c
// Src/XCode/XEvent/XSignalSlot/XSignalSlot.h:16-17
typedef void (*XSlotFunc1)(XObject* receiver, XVarList* args);  // ← 第1参是「连接接收者」
typedef void (*XSlotFunc2)(XObject* sender,  XVarList* args);  // ← 第1参是「信号发送者」
```

发射时按 `conn->receiver` 是否为 NULL 二选一（`XSignalSlot.c:280-296`，`XConnection_directEmit`）：

```c
// Src/XCode/XEvent/XSignalSlot/XSignalSlot.c:286-295（XConnection_directEmit）
if(conn->receiver)           conn->slot_func1(conn->receiver, args);        // ← 传 receiver
else if (conn->slot_func2)    conn->slot_func2(conn->signal->sender, args);  // ← 传 sender
```

对应关系表：

| 连接 API | 槽函数类型 | 槽第 1 参含义 | 何时用 |
|----------|-----------|---------------|--------|
| `XObject_connect_1(obj, signal, receiver, slot, type)` | `XSlotFunc1` | **receiver**（连接时传入的那个对象） | 槽逻辑不关心是谁发的信号；或需要 receiver 参与后续派发 |
| `XObject_connect_2(obj, signal, slot)` | `XSlotFunc2` | **sender**（真正发射信号的对象） | 槽逻辑需要区分信号来源（多路复用同一槽） |

**硬性要求：**

1. **用 `XObject_connect_1` 时，槽函数的第一个参数命名为 `receiver`，绝不能当 sender 用。**
   需要真正的发送者时，必须显式取 `XObject_sender(receiver)`（`XObject.c:934`，
   内部按"当前线程的发送者栈"查找最近一次发射的 sender，跨线程与嵌套发射均安全），
   或改用 `XObject_connect_2`。
2. **用 `XObject_connect_1` 时，若槽里需要按类型区分不同来源对象，必须用
   `XObject_sender(receiver)` 取回 sender 后再判断**，不能直接把第 1 参强转成
   业务对象类型。
3. **禁止把 `connect_1` 的第 1 参强制转型成信号发送者的类型。** 两类对象的首成员
   都是 `XObject m_class`、字段偏移天然对齐，**错位读取不会触发任何断言或崩溃**，
   只会读出"看起来合法"的错值——这是本库最难排查的一类缺陷。

**实证（2026-10-25 安卓真机 Modbus 主站排障）**：`XModbusReply` 与 `XModbusTcpClient`
都以 `XObject m_class` 开头，探针误用 `connect_1` 的第 1 参当 `XModbusReply*`，
导致读出 `state=2`（实为 client 的 `m_state`）、`error=0`、`values=0`（实为
`m_params[0]`=NULL），并把真正的 reply 泄漏、`XObject_deleteLater()` 误删了
**client 本身**（进而触发设备状态置 Unconnected），最终表现为"应答收到了却像是
没收到、且连接莫名断开"。同一份库代码用 `connect_2` 后四相位轮询全部正常应答。

**正误对照（槽需要知道信号来自谁时）：**

```c
/* ✅ connect_1 + 显式取 sender：槽不依赖第 1 参的语义，安全 */
static void onReply(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusReply* reply = (XModbusReply*)XObject_sender(receiver);  // 真 sender
    if (!reply) return;
    /* ... 用 reply ... */
}
XObject_connect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                  (XObject*)self, onReply, XConnectionType_Direct);

/* ✅ 或直接用 connect_2：第 1 参天然就是 sender */
static void onReply2(XObject* sender, XVarList* args)
{
    (void)args;
    XModbusReply* reply = (XModbusReply*)sender;   // 真 sender
    /* ... */
}
XObject_connect_2((XObject*)reply, XSignal(XModbusReply_finished_signal), onReply2);

/* ❌ 禁止：把 connect_1 的第 1 参（receiver）当 sender 用 */
static void onReplyBad(XObject* receiver, XVarList* args)
{
    XModbusReply* reply = (XModbusReply*)receiver;  // ← receiver 不是 reply！
    /* 读到的是错位数据，且 deleteLater(receiver) 会删错对象 */
}
```

### 真实 API 签名（勿凭记忆书写）

```c
// Src/XClass/XObject.h:390 —— 第1参是 receiver
XConnection* XObject_connect_1(XObject* object, size_t signal, XObject* receiver,
                               XSlotFunc1 slot_func, XConnectionType type);

// Src/XClass/XObject.h:400 —— 只有 3 参，无 receiver；第1参是 sender
XConnection* XObject_connect_2(XObject* object, size_t signal, XSlotFunc2 slot_func);
```

> 注意：`XObject_connect_2` **没有** receiver 与连接类型参数（与 Qt 的
> `connect(sender, signal, receiver, slot, type)` 形似而不同，勿照抄 Qt 写法）。

连接类型枚举（`XSignalSlot.h:31-38`）：

```c
XConnectionType_Auto           = 0,      // 自动选择（同线程直接调用 / 跨线程队列）
XConnectionType_Direct         = 1,      // 直接调用（同步，在发射处执行）
XConnectionType_Queued         = 2,      // 投递到接收者线程的事件队列（异步）
XConnectionType_BlockingQueued = 3,      // 阻塞等待队列投递完成
XConnectionType_Unique         = 0x80,   // 标志位：同一 receiver+slot 只连一次
XConnectionType_SingleShot     = 0x100,  // 标志位：触发一次后自动断开
```

### 信号声明规范

```c
// 信号函数在类的头文件中声明；结构体里放「信号槽管理器指针」（非内嵌结构）
typedef struct XATComm {
    XObject m_class;
    // ...
    XSignalSlot* m_signalSlot;  // 槽管理器指针（XObject 内部持有，勿内嵌）
} XATComm;

// 信号发射函数：XEmitSignal 同步发射
void* XATComm_response_signal(XATComm* self) {
    XEmitSignal(self, XATComm_response_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}
```

**带参信号函数的写法**：参数用 `XVarList_Create(XVar(类型, 值))` 构造。
`XEmitSignal` 宏体是 `if(object && object->m_signalSlot) XObject_emitSignal(..., args, ...)`，
**实参 `args` 位于 `if` 的函数体内，惰性求值**——对象为空时 `XVarList_Create` 根本不会执行，
故信号函数被以 NULL 调用（取信号地址，见 `XObject_signalIdOf`）时无分配、无泄漏。
但**直接调用带参信号函数**（而非经 `XSignal()` 宏取地址）时 `args` 会真实求值并分配，
此时若发射路径最终未消费该 `XVarList` 就会泄漏——**带参信号函数应保证 args 必被消费**
（发射成功时由 `XSignalSlot_emitReleaseRef` 释放）。

### 信号地址必须经 `XSignal()` 宏获取

```c
// ✅ 正确：宏内部以 NULL 调用信号函数，取回其真实地址（跨 DLL 边界亦准）
XObject_connect_1((XObject*)sender, XSignal(XFoo_bar_signal),
                  (XObject*)receiver, on_bar, XConnectionType_Direct);

// ❌ 错误：直接取函数地址 &fn，跨 DLL 边界得到的是导入跳板地址，
//          与发射侧的真实地址不等 → signalMap 查不中 → 槽函数静默不触发
```

`XSignal()` 展开为 `XObject_signalIdOf((void*(*)(XObject*))(signal))`
（`XObject.h:434`），后者以 NULL 调用信号函数取其返回值
（`XObject.c:711-729`）——这是**跨 DLL 边界也能取到真实地址**的唯一可靠途径。

### 连接与断开

```c
// 连接
XObject_connect_1((XObject*)sender, XSignal(XFoo_bar_signal),
                  (XObject*)receiver, on_bar, XConnectionType_Direct);
XObject_connect_2((XObject*)sender, XSignal(XFoo_bar_signal), on_bar);

// 断开：connect_1 用对象+信号+receiver+槽 定位；connect_2 需持有返回的 XConnection*
XObject_disconnect_1((XObject*)sender, XSignal(XFoo_bar_signal),
                     (XObject*)receiver, on_bar);
XObject_disconnect_2(conn);   // conn 为 XObject_connect_2 的返回值
```

### 常见错误与最佳实践

| 错误 | 后果 | 正确做法 |
|------|------|---------|
| `connect_1` 的槽把第 1 参当 sender 强转 | 静默读到错位字段（无崩溃、无断言） | 用 `XObject_sender(receiver)`，或改 `connect_2` |
| 槽里 `XObject_deleteLater((XObject*)sender)` | 删错对象（把 client 当 reply 删） | 先确认第 1 参语义再决定删谁 |
| 槽依赖"信号来自谁"却用 `connect_1` | 多路复用时判错分支 | `connect_2` 或 `XObject_sender()` |
| 照抄 Qt 写 `connect_2(obj, sig, receiver, slot, type)` | 编译不过 / 参数错位 | `connect_2` 只有 3 参，无 receiver |
| 直接用 `&fn` 或 `(size_t)fn` 取信号地址 | 跨 DLL 边界取到导入跳板地址，signalMap 查不中 → **槽函数静默不触发** | 一律用 `XSignal(fn)` 宏 |
| 引用不存在的 API（如 `XObject_connect_sender_0`） | 编译失败 | 以 `XObject.h` 为准，见本节「真实 API 签名」 |

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
- 不得使用直接 `malloc`、`realloc`、`free` 替代项目内存 API；不得引入 C11 语法。值类型、容器、图像、字体和区域对象必须保持 `init`/`XClassDeinit` 成对。
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

## 定时器使用规范（2026-10-26 裁定）

> **XObject 派生类内部需要定时器时，一律用 XObject 自带定时器机制，禁止再
> 内嵌独立 `XTimer` 类实例。**

### 规则

1. **注册**：`XObject_startTimer_ms(obj, intervalMs, timerType)` 返回 `XTimerId`；
   到期经 `EXObject_TimerEvent` 虚函数重载分发（`XTimerEvent_timerId(event)`
   区分多路定时器，`XEvent_accept(event)` 收尾）。
2. **注销**：停止时必须 `XObject_killTimer(obj, id)` 并把成员 id 复位为
   `XTIMER_INVALID_ID`。**周期定时器只覆盖 id 不 kill = 定时器泄漏**（分发器
   startTimer 恒 singleShot=false 到期重挂，对象销毁才被统一注销——
   2026-10-25 键盘战役实证过此类泄漏）。
3. **独立 `XTimer` 类的适用面收窄**：仅限「对象本体不是 XObject 派生」或
   「需要独立生命周期/跨对象移交」的场景（如探针自建拍子）。业务对象内部
   拍子（轮询、重发、超时、限频）一律走对象自带定时器。
4. **回调里先比对 id 再动作**：多路定时器共用一个 timerEvent 入口，未知
   id 交父类处理（`XClass_Parent(..., EXObject_TimerEvent, ...)`），不要吞。

```c
/* ✅ 对象内定时器（XObject 派生类） */
obj->m_pollTimer = XObject_startTimer_ms((XObject*)obj, 1000, XTimerType_CoarseTimer);

static void VXHub_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusHub* hub = (XModbusHub*)obj;
    XTimerId id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    /* 按 id 分发；未知 id 落父类 */
}

/* 停止：必须 kill + 复位 */
if (obj->m_pollTimer != XTIMER_INVALID_ID) {
    XObject_killTimer((XObject*)obj, obj->m_pollTimer);
    obj->m_pollTimer = XTIMER_INVALID_ID;
}
```

---

## 框架/应用分层写法（2026-10-29 裁定）

> **框架有框架的写法，APP 有 APP 的写法，两种必须明确区分；应用层 UI 一律
> 对齐 Qt 的正规控件用法，禁止绕开控件体系自绘动态内容。**

### 规则

1. **应用显示动态文本（时钟/计数/状态值）：控件（XLabel）+ 定时器
   `setText`，与 Qt 同款**。文本变更后的脏区计算、差带同步、是否真实推帧
   全部由框架管线接管；APP 不手动 `XWidget_update`、不在自绘 paintEvent
   里画会变的字符串。
2. **自绘 paintEvent（自定义 chrome）只画静态或低频内容**；动态内容一律
   控件化后交给框架。反例：顶栏时钟第一版画在品牌条/应用栏 paintEvent 里
   + 1s 定时器手动 update 顶栏小条（2026-10-28），已重构为独立 XLabel +
   定时器 setText（2026-10-29）。
3. **文本色走调色板**（引擎控件自绘文本不消费 QSS color）：
   `XWidget_setPalette` + `XWidget_setForegroundRole`；需要与所在 chrome
   底色无缝时，把控件 palette 的 Window 角色设为同色即可（比透明度机制
   简单可靠）。
4. **框架级能力优先落在框架层**（如 present 差带同步、平台驱动信号），
   应用不得为绕开框架缺陷而在应用层复制框架机制；框架缺陷在框架层根修。

```c
/* ✅ 应用层动态文本（Qt 同款） */
label = XLabel_create(parent, 0);
/* palette 设色 + 对齐 + 鼠标穿透（按需） */
/* 定时器槽里： */
XLabel_setText_2(label, buf);   /* 其余全部由框架接管 */

/* ❌ 应用层自绘动态内容 */
/* paintEvent 里 drawText(当前时间)；定时器里 XWidget_update(宿主) */
```

---

## 常见错误与最佳实践

1. **忘记初始化/反初始化**：未 `init` 就使用是未定义行为，用完不 `XClassDeinit` 是内存泄漏；❌/✅ 写法见「对象生命周期管理 → init/deinit 成对原则」。
2. **错误返回路径未清理资源**：每个返回路径都必须先清理再返回。
   ```c
   XHostAddress temp;
   XHostAddress_init(&temp);
   if (someCondition) {
       XClassDeinit(&temp);  // ✅ 清理后再返回（❌ 错误写法：直接 return 漏掉本行）
       return;
   }
   XClassCopy(result, &temp);
   XClassDeinit(&temp);
   ```
3. **堆对象的分配与堆标记必须用真实 API**（2026-10-29 裁定，此前文档示例有误）：
   `Set_Class_Memory(Object, MemoryType)` 的第二参是 **XMemoryType 枚举**（经
   `XMemory_method` 查表），**不是**函数指针——`Set_Class_Memory(obj, XFree_System)`
   编译不过。堆对象的标准三步：`XMemory_calloc` 分配 → `_init` → `Set_Class_IsHeap(obj, true)`。
   ```c
   XExample* obj = (XExample*)XMemory_calloc(1, sizeof(XExample), XCLASS_DEFAULT_MEMORY_TYPE);
   if (!obj) return NULL;
   XExample_init(obj);
   Set_Class_IsHeap(obj, true);   // ✅ 必须标记堆所有权，配对 XClassDelete
   ```
4. **在虚函数表中先重载后继承**：先 `XVTABLE_OVERLOAD_DEFAULT` 再 `XVTABLE_INHERIT_XCLASS` 会覆盖重载，必须先继承后重载（见「虚函数重载注意事项」）。
5. **copy/move 不支持未初始化目标**：所有 `VXxxx_copy`/`VXxxx_move` 实现在第一行参数检查之后，必须用 `XClassIsVtableNull(dest)` 检查目标 vtable，为空则调用 `XType_init(dest)` 兜底完整初始化；只 `XClassSetVtable` 不分配成员是错误写法。规则与示例见「对象生命周期管理 → copy/move 目标初始化约束」。
6. **用 memcpy 代替拷贝**：浅拷贝共享底层数据，导致双重释放/crash；必须用 `XClassCopy`。`XString` 还会共享编码缓存指针，见「内存管理注意事项 → 禁止使用 memcpy 复制对象」。
7. **忘记检查虚函数表和 ISNULL**：`*_base` 函数中未做 `ISNULL(self, ...)` + `ISNULL(XClassGetVtable(self), ...)` 检查就直接分派，空指针会崩溃；正确写法见「虚函数表与虚函数重载 → `*_base` 调度入口」。
8. **条件编译判宏早于给它赋值的 include**：模块开关定义在模块自己的 config 头（如 `XIndustry_config.h`）时，公开头里 `#if 模块开关_ON` 必须放在 `#include "CXinYueConfig.h"` / `#include "Xxxx_config.h"` **之后**——放在前面时宏未定义、`#if` 恒为假，**整个头被静默裁成空**（不报错、无符号，适配件"看起来编译通过实际不存在"，2026-10-29 Industry 层实锅 ×2）。
9. **派生开关必须显式包含被派生的 config 头**：模块 config 里的开关默认值若从其他模块派生（如 `XINDUSTRY_MODBUS_ON` 按 `XMODBUS_ON` 派生），必须先 `#include "XProtocol_config.h"` 再 `#ifdef XMODBUS_ON`——本文件常被 CXinYueConfig.h **中途**包含（头保护挡住递归），被派生宏的定义点可能晚于本文件的包含点，不显式包含会恒派生为 0（2026-10-29 实锅）。
10. **`XString_constData` 不是 `const char*`**：它是 `XString_unicode` 的宏别名，返回 **UTF-16 内部数据的 `const XChar*`**（uint16_t 流）。取 UTF-8 的 `const char*` 必须用 **`XString_toUtf8`**（另有 `toGbk`/`toLocal`）。把它喂给任何 `const char*` 形参，UTF-16 字节流按 char 读，**非 ASCII 区间内容在首个 `\0` 处被静默截断**——不崩溃、常无警告，纯数据错。实证（2026-10-29）：`XModbusTcpServer` 把 `XString_constData` 结果传给 `XHostAddress_create_fromString(const char*)`，"127.0.0.1" 截成 "1"，设 address 参数 listen 恒败；同文件 `XModbusTcpClient` 同路径用 `XString_toUtf8` 即正确对照。凡跨类传字符串指针，先核对访问器返回的真实类型（系统规则见「容器与数据结构规范 → XString 使用注意事项」）。
11. **诊断/对账打点必须被编译开关门包裹，时间源与打印走框架体系**（2026-10-29 裁定）：排查疑难时写入 Src/ 的临时打点（对账、间隔测量、状态追踪），必须按"模块名_ON"惯例做**默认 0 的专属开关门**（如 `XEVENT_PUMPGAP_ON`，代码处 `#ifndef` 兜底定义即可），保证正常运行零开销、异平台/嵌入式构建零影响；块内时间源一律 `XDateTime_currentMSecsSinceEpoch`（禁止 `clock_gettime`/`GetTickCount` 直呼——异平台编不过且违反时间源规则），打印优先 `XDEBUG_PRINTF`/`XERROR_PRINTF`（自带文件/函数/行号上下文，随构建口径归零）。实证：`XAbstractEventDispatcher` 的 [PUMPGAP] 泵间隔打点直呼 `clock_gettime` 无守卫，合入即 Windows 构建回归，已改 `XEVENT_PUMPGAP_ON` 门 + XDateTime。调试代码的价值在"需要时一键开启、平时完全不存在"——裸写的打点既污染构建，又逼着后人替你修编译。

---

## 总结

1. **命名规范**：`X` 前缀 + 大驼峰，函数名为 `X类型_功能`，成员变量 `m_` 前缀，内部虚函数 `V` 前缀
2. **外部依赖**：库内已有同功能 API（XFile/XDateTime/XChar/XMemory/XString/XVarList 等）时禁止引入外部库/C 标准库；白名单与登记要求见「外部依赖约束」
3. **类创建**：继承 `XClass`/`XObject`，使用虚函数表宏定义，遵循标准头文件/源文件结构；拷贝/移动统一 `XClassCopy`/`XClassMove`
4. **虚函数表**：先继承 `XVTABLE_INHERIT_XCLASS`，再 `XVTABLE_OVERLOAD_DEFAULT`，不可颠倒
5. **生命周期**：`init` / `XClassDeinit` 必须成对出现，堆对象记得 `Set_Class_Memory`，move 后源对象由创建者清理
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
