# XJson —— JSON 文档模型使用手册

> 适用范围：`Src/XCore/XData/XJson/**`
> 头文件入口：`#include "XJson.h"` 及 `XJsonValue.h` / `XJsonObject.h` / `XJsonArray.h` / `XJsonDocument.h`
> 自动化回归：`bin\XinYueC_Static.exe --test xjson-bson`（见第 9 节）

---

## 目录

1. [概览](#1-概览)
2. [文件布局与依赖](#2-文件布局与依赖)
3. [对象模型](#3-对象模型)
4. [内存与所有权模型（必读）](#4-内存与所有权模型必读)
5. [API 参考](#5-api-参考)
   - 5.1 [XJsonValue](#51-xjsonvalue)
   - 5.2 [XJsonObject](#52-xjsonobject)
   - 5.3 [XJsonArray](#53-xjsonarray)
   - 5.4 [XJsonDocument](#54-xjsondocument)
6. [解析与序列化规则](#6-解析与序列化规则)
7. [与 BSON / XVariant 的互转](#7-与-bson--xvariant-的互转)
8. [常见陷阱清单（按踩坑频率排序）](#8-常见陷阱清单按踩坑频率排序)
9. [测试与验证](#9-测试与验证)
10. [完整示例](#10-完整示例)

---

## 1. 概览

XJson 是 XinYueC 的 JSON 文档模型，接口与行为整体对齐 Qt 6 的
`QJsonValue` / `QJsonObject` / `QJsonArray` / `QJsonDocument`：

| 概念 | C 类型 | 对应 Qt |
|---|---|---|
| JSON 值（标量/容器） | `XJsonValue` | `QJsonValue` |
| JSON 对象（键值映射） | `XJsonObject` | `QJsonObject` |
| JSON 数组 | `XJsonArray` | `QJsonArray` |
| 文档（解析/序列化的入口） | `XJsonDocument` | `QJsonDocument` |

设计约束（实现层面）：

- 只依赖 XinYueC 抽象层（`XString` / `XMap` / `XVector` / `XByteArray` /
  `XVariant` / `XMemory`），**不直接调用** Win32、POSIX、Qt 或 libc 文件 API；
- 所有容器都基于框架的 COW（写时复制）容器实现，拷贝廉价；
- 默认内存类型为 `XCLASS_DEFAULT_MEMORY_TYPE`，可用 `*_create_ex(XMemoryType)`
  指定分配器；
- 非线程安全：**一个对象同一时刻只能由一个线程持有/修改**，跨线程传递必须
  转移所有权（`*_move`）或加外部锁。

### 1.1 快速上手

```c
#include "XJsonDocument.h"

XJsonObject* obj = XJsonObject_create();
XJsonObject_insert_keyUtf8_utf8(obj, "name", "root");
XJsonObject_insert_keyUtf8_int(obj, "role", 1);

XJsonDocument* doc = XJsonDocument_create_object_move(obj); /* obj 变空壳 */
XClassDelete((XClass*)obj);                                 /* 释放空壳 */

XString* text = XJsonDocument_toString(doc, XJsonDocument_Compact);
XPrintf("%s\n", XString_toUtf8(text));   /* {"name":"root","role":1} */

XClassDelete((XClass*)text);
XClassDelete((XClass*)doc);
```

---

## 2. 文件布局与依赖

```
Src/XCore/XData/XJson/
├── XJson.h                     # 公共枚举：输出格式、解析错误码、错误结构
├── XJsonValue/
│   ├── XJsonValue.h/.c         # 值类型：标量 + 容器指针 + Variant 桥
├── XJsonObject/
│   ├── XJsonObject.h/.c        # XMap<XString, XJsonValue> 封装（COW、键有序）
├── XJsonArray/
│   ├── XJsonArray.h/.c         # XVector<XJsonValue> 封装（COW）
└── XJsonDocument/
    ├── XJsonDocument.h/.c      # 根值 + 解析器 + 序列化器 + BSON 桥
```

编译期依赖开关（头文件里有 `#error` 守卫）：

- `XJsonValue` 需要 `XString_ON` 与 `XVariant_ON`；
- `XJsonObject` 需要 `XMap_ON`；
- `XJsonArray` 需要 `XVector_ON`；
- `XJsonDocument` 需要 `XByteArray_ON`（BSON 桥另需 `XBson*`）。

---

## 3. 对象模型

```
XJsonDocument
└── root: XJsonValue
    ├── type = Null / Bool / Double / Int / String / Array / Object / Undefined / Invalid
    └── data
        ├── boolean / number / integer / string(XString*)
        ├── array  → XJsonArray → XVector<XJsonValue>
        └── object → XJsonObject → XMap<XString, XJsonValue>
```

### 3.1 XJsonValue 的类型枚举

| 枚举 | 含义 | 说明 |
|---|---|---|
| `XJsonValue_Invalid` | 无效/未初始化 | 空文档的根值就是它 |
| `XJsonValue_Null` | JSON `null` | |
| `XJsonValue_Bool` | `true` / `false` | |
| `XJsonValue_Double` | 双精度浮点 | JSON 无 int/double 之分，解析时按语法判定 |
| `XJsonValue_Int` | 64 位有符号整数 | 解析整数且落在 int64 范围时使用 |
| `XJsonValue_String` | UTF-8 字符串 | 内部用 `XString` 保存 |
| `XJsonValue_Array` | 数组 | 深拷贝/移动语义见第 4 节 |
| `XJsonValue_Object` | 对象 | 同上 |
| `XJsonValue_Undefined` | 未定义（0x80） | 查询缺失键的返回值，**不是** JSON `null` |

> `Undefined` 是框架扩展值，序列化时输出为 `null`；它和 `Null` 不相等。

### 3.2 XJsonObject 的键序

`XJsonObject` 内部是 `XMap`（红黑树），键为 `XString`，按
`XString_compare`（大小写敏感、码点字典序）**排序存储**。

结论：

- **不要依赖插入顺序**：`toJson`/`toString` 输出的是排序后的键序；
- 与 BSON 互转时键序会重排（BSON 文档是有序的，JSON 对象不是）；
- 若需要保序/重复键，请使用 `XBsonDocument`。

### 3.3 COW（写时复制）

`XJsonObject`、`XJsonArray` 在 `*_init` 时都使用 `useCow = true`：

```c
XMap_init(object, sizeof(XString), sizeof(XJsonValue), XString_compare, true);
XVector_init(array, sizeof(XJsonValue), true);
```

因此 `XJsonObject_create_copy` / `XJsonArray_create_copy` 是**共享底层数据**
的浅拷贝；任一方向容器写入时框架自动 detach（复制出新的一份）。

使用上可以当深拷贝用，但要注意：

- 拷贝后大量写入会产生一次 detach 开销；
- 不要绕过公开 API 直接改 `m_elements` / 内部 map；
- `XJsonObject_toVariantMap_move` / `XJsonArray_toVariantList_move` 内部会
  `XMap_detach` / `XVector_detach`，迁移的是**内容**（见 5.2/5.3）。

---

## 4. 内存与所有权模型（必读）

这一节是使用 XJson 最容易出问题的地方。所有结论都有自动化测试覆盖
（第 9 节）。

### 4.1 创建与销毁

| 操作 | API | 所有权 |
|---|---|---|
| 堆创建 | `XJsonValue_create_*` / `XJsonValue_create_copy/move` | 调用方 `XJsonValue_delete` |
| 堆创建 | `XJsonObject_create[_ex]` / `XJsonObject_create_copy/move` | 调用方 `XClassDelete` |
| 堆创建 | `XJsonArray_create[_ex]` / `XJsonArray_create_copy/move` | 调用方 `XClassDelete` |
| 堆创建 | `XJsonDocument_create[_ex]` / `*_create_copy/move` | 调用方 `XClassDelete` |
| 栈初始化 | `XJsonValue_init`、`XJsonObject_init`、`XJsonArray_init`、`XJsonDocument_init` | 调用方 `*_deinit` / `XClassDeinit` |
| 清空保留对象 | `XJsonValue_clear`、`XJsonDocument_clear`、`*_clear_base` | 对象仍可继续使用 |

`XJsonValue_deinit` 会把类型重置为 `Invalid` 并释放内部容器；`XJsonValue_clear`
只释放内容、**保留类型**（数组/对象会清空元素但容器还在）。

`XJsonValue_init(&v, type)` 与 `XBsonValue_init` 统一：**只设置类型标签，
不分配容器载荷**（String/Array/Object 的 `data` 初始为 `NULL`）。需要空容器
时用 `setString_utf8(v, "")` / `setArray(v, empty)` / `setObject(v, empty)`，
或直接用 `XBsonValue_create(type)`（BSON 侧 create 会分配空载荷）。

### 4.2 返回值所有权速查表

| API | 返回 | 调用方是否释放 |
|---|---|---|
| `XJsonObject_value_keyUtf8` | **借用**；键缺失时返回 `NULL` | **否**（与 BSON 同名同义） |
| `XJsonObject_value_copy_keyUtf8` | **深拷贝**；键缺失时返回 `NULL` | **必须 `XJsonValue_delete`** |
| `XJsonObject_value_base` | 借用指针（指向 map 内部） | 否 |
| `XJsonObject_take_keyUtf8` | **拷贝**并移除；缺失返回新建 `Undefined` | **必须释放** |
| `XJsonObject_keys_base` | 新建 `XVector`（键的拷贝） | **必须 `XClassDelete`** |
| `XJsonArray_at` / `at_const` | 借用指针；越界 `NULL` | 否 |
| `XJsonArray_first` / `last` | **拷贝**；空数组返回新建 `Undefined` | **必须释放** |
| `XJsonArray_takeAt` | **拷贝**并移除；非法索引返回新建 `Undefined` | **必须释放** |
| `XJsonDocument_root` / `root_const` | 借用（指向文档内部） | 否 |
| `XJsonDocument_object` / `array` | 借用（指向文档内部） | 否 |
| `XJsonDocument_toString` / `toJson` | 新建 | **必须释放** |
| `XJsonValue_toVariant` / `toVariant_move` | 新建 | **必须释放** |
| `XJsonValue_toVariant_ref` | 非拥有包装 | 删除包装前把 `m_data` 置 `NULL` |
| `XString_toUtf8` | 内部缓冲借用 | 否 |

> **统一口径**：`value_keyUtf8` 在两库中都是**借用**，缺失返回 `NULL`，
> 不要释放；需要返回值在对象修改后仍有效时用 `value_copy_keyUtf8`（堆拷贝，
> 必须释放）。
>
> 历史事故：`XLinkUserStore_load` 曾按「深拷贝」语义使用 `value_keyUtf8`
> 却既不删除也不检查，每轮泄漏约 1.8 KB；统一为借用后自然归零。

### 4.3 `*_move` 的统一约定：转移内容、留下空壳

框架里所有 `*_move` 都遵循同一模式（`XClassMove` / `XString_create_move`）：

> **内容转移到目标，源对象变成一个「已初始化但空」的壳，壳仍由调用方释放。**

```c
XJsonArray* a = XJsonArray_create();
/* ...填充... */
XJsonValue v;
XJsonValue_init(&v, XJsonValue_Array);
XJsonValue_setArray_move(&v, a);   /* a 的内容转移给 v */
XClassDelete((XClass*)a);          /* 释放 a 的空壳，必须！ */
```

`XJsonValue_deinit(&v)` 最终释放内容。

例外/注意：

- 所有 `*_move` **一律**是「内容转移、源留空壳」，包括
  `XJsonObject_insert_keyUtf8_string_move`（内部 `XString_create_move`）。
  需要「容器接管指针」时用 `_ref` 系列（见 4.4）；
- `XJsonArray_toVariantList_move(array)` / `XJsonObject_toVariantMap_move(object)`
  是「逐元素转移内容」，**完成后源容器被清空（size=0）**，源容器可继续使用或释放；
- `XJsonDocument_setRoot_move(document, root)` 后 `root` 变 `Invalid`（类型 0），
  仍可 `XJsonValue_delete(root)` 释放外壳。

### 4.4 `_ref`：指针接管（零分配）

`_move` 会额外分配一个包装对象（`XString_create_move` / `XJsonObject_create_move`），
`_ref` 则**直接把指针挂到目标上**，适合「刚创建、不再复用」的字符串/容器：

| API | 作用 |
|---|---|
| `XJsonValue_setString_ref(value, XString*)` | 值接管字符串指针 |
| `XJsonValue_setArray_ref(value, XJsonArray*)` | 值接管数组指针 |
| `XJsonValue_setObject_ref(value, XJsonObject*)` | 值接管对象指针 |
| `XJsonObject_insert_keyUtf8_string_ref(obj, key, XString*)` | 对象接管插入字符串 |
| `XJsonObject_insert_keyUtf8_array_ref(obj, key, XJsonArray*)` | 对象接管插入数组 |
| `XJsonObject_insert_keyUtf8_object_ref(obj, key, XJsonObject*)` | 对象接管插入对象 |
| `XJsonDocument_setObject_ref(doc, XJsonObject*)` | 文档根接管对象 |
| `XJsonDocument_setArray_ref(doc, XJsonArray*)` | 文档根接管数组 |

契约：

- 参数非 NULL 时**调用即移交所有权**（无论返回 `true` / `false`）；返回
  `false` 时资源已由本函数释放；
- 参数为 NULL 时直接返回 `false`，**不接管**，源仍归调用方；
- 调用后不得再访问或释放源对象。

```c
XString* s = XString_create_utf8("一次性的值");
XJsonObject_insert_keyUtf8_string_ref(obj, "k", s);  /* 零分配接管 */
/* 不要 XClassDelete(s) */
```

选择建议：

- 源对象是从 `create` 出来、插入后不再使用 → `_ref`（省一次分配）；
- 源对象还要复用、或失败后还要继续使用 → `_move`（内容转移，壳归你）。

### 4.5 深拷贝入参

所有 `insert_keyUtf8_*`（不带 `_move`）和 `set*`（不带 `_move`）都会**拷贝**
入参。调用方保留自己的对象，并且仍然负责释放它：

```c
XString* s = XString_create_utf8("v");
XJsonObject_insert_keyUtf8_string(obj, "k", s);  /* 拷贝一份进对象 */
XClassDelete((XClass*)s);                        /* 自己的这份要释放 */
```

---

## 5. API 参考

### 5.1 XJsonValue

#### 创建

```c
XJsonValue* XJsonValue_create_null(void);
XJsonValue* XJsonValue_create_undefined(void);
XJsonValue* XJsonValue_create_bool(bool value);
XJsonValue* XJsonValue_create_double(double value);
XJsonValue* XJsonValue_create_int(int64_t value);
XJsonValue* XJsonValue_create_string(const XString* string);   /* 深拷贝 */
XJsonValue* XJsonValue_create_array(XJsonArray* array);        /* 深拷贝 */
XJsonValue* XJsonValue_create_object(XJsonObject* object);     /* 深拷贝 */
XJsonValue* XJsonValue_create_copy(XJsonValue* copy);
XJsonValue* XJsonValue_create_move(XJsonValue* move);          /* 源变空壳 */
```

#### 生命周期

```c
void XJsonValue_init(XJsonValue* var, XJsonValueType type);
void XJsonValue_copy(XJsonValue* var, const XJsonValue* src);  /* 深拷贝 */
void XJsonValue_move(XJsonValue* var, XJsonValue* src);        /* 源变 Invalid（类型 0） */
void XJsonValue_deinit(XJsonValue* value);                     /* 变 Invalid */
void XJsonValue_delete(XJsonValue* value);                     /* 堆对象 */
void XJsonValue_clear(XJsonValue* value);                      /* 释放内容、保留类型 */
```

#### 类型判断与取值

| 函数 | 说明 |
|---|---|
| `XJsonValue_type(v)` | 返回 `XJsonValueType`；`NULL` 返回 `Invalid` |
| `XJsonValue_isNull/isUndefined/isBool/isDouble/isInt/isString/isArray/isObject` | 精确类型判断 |
| `XJsonValue_toBool(v, def)` | 仅 Bool 返回值，否则返回 `def` |
| `XJsonValue_toDouble(v, def)` | Double 直接返回；**Int 会自动转 double**；其它返回 `def` |
| `XJsonValue_toInt(v, def)` | Int 直接返回；**只有整数值的 Double**（`(double)(int64_t)d == d`）才转换；其它返回 `def` |
| `XJsonValue_toString(v)` | 返回借用 `const XString*`；类型不符 `NULL` |
| `XJsonValue_toArray/toObject(v)` | 借用指针；类型不符 `NULL` |
| `XJsonValue_equals(l, r)` | Int/Double 互比按数值；容器递归比较 |
| `XJsonValue_compare(l, r)` | 供容器/排序使用 |

> `toDouble` 与 `toInt` 的转换规则不对称：`3.5 -> toInt` 返回缺省值，
> 不会截断成 3。写业务代码时不要假设「自动取整」。

#### 设置

```c
void XJsonValue_setNull(XJsonValue* value);
void XJsonValue_setUndefined(XJsonValue* value);
void XJsonValue_setBool(XJsonValue* value, bool b);
void XJsonValue_setDouble(XJsonValue* value, double d);
void XJsonValue_setInt(XJsonValue* value, int64_t i);
void XJsonValue_setString(XJsonValue* value, const XString* s);        /* 拷贝 */
void XJsonValue_setString_move(XJsonValue* value, XString* s);         /* s 留空壳 */
void XJsonValue_setString_ref(XJsonValue* value, XString* s);          /* 指针接管 */
void XJsonValue_setString_utf8(XJsonValue* value, const char* utf8);
void XJsonValue_setArray(XJsonValue* value, XJsonArray* a);            /* 拷贝 */
void XJsonValue_setArray_move(XJsonValue* value, XJsonArray* a);       /* a 留空壳 */
void XJsonValue_setArray_ref(XJsonValue* value, XJsonArray* a);        /* 指针接管 */
void XJsonValue_setObject(XJsonValue* value, XJsonObject* o);          /* 拷贝 */
void XJsonValue_setObject_move(XJsonValue* value, XJsonObject* o);     /* o 留空壳 */
void XJsonValue_setObject_ref(XJsonValue* value, XJsonObject* o);      /* 指针接管 */
```

每次 `set*` 都会先释放旧内容。`setString(value, NULL)` /
`setString_utf8(value, NULL)` 与 BSON 同款接口一致：**空操作、保留原值**。
（`create_copy(NULL)` / `create_move(NULL)` 也统一返回 `NULL`。）

#### XVariant 桥

```c
XVariant* XJsonValue_toVariant(const XJsonValue* value);       /* 深拷贝 */
XVariant* XJsonValue_toVariant_move(XJsonValue* value);        /* 转换后源置 Invalid（0） */
XVariant* XJsonValue_toVariant_ref(XJsonValue* value);         /* 非拥有 */
XJsonValue* XJsonValue_fromVariant(const XVariant* variant);   /* 新建值 */
XJsonValue* XJsonValue_fromVariant_copy(const XVariant* variant);
XJsonValue* XJsonValue_fromVariant_ref(const XVariant* variant);

void XJsonValue_setVariant(XVariant* variant, const XJsonValue* value);
void XJsonValue_setVariant_move(XVariant* variant, XJsonValue* value);
void XJsonValue_setVariant_ref(XVariant* variant, XJsonValue* value);
```

标量映射：`Bool→Bool`、`Int→Int64`、`Double→Double`、`String→String`、
`Array→List`、`Object→Map`、`Null/Undefined→NULL`。

---

### 5.2 XJsonObject

#### 创建与生命周期

```c
XJsonObject* XJsonObject_create(void);                 /* 宏 → create_ex(默认内存) */
XJsonObject* XJsonObject_create_ex(XMemoryType memory);
XJsonObject* XJsonObject_create_copy(XJsonObject* copy);  /* COW 浅拷贝 */
XJsonObject* XJsonObject_create_move(XJsonObject* move);  /* 源留空壳 */
void XJsonObject_init(XJsonObject* object);
```

容量/查询：

```c
size_t XJsonObject_size_base(const XJsonObject*);
bool   XJsonObject_isEmpty_base(const XJsonObject*);
void   XJsonObject_clear_base(XJsonObject*);
```

#### 插入（全部 11 种 + 3 个基础接口）

| API | 键 | 值语义 |
|---|---|---|
| `XJsonObject_insert_keyUtf8_int(obj, key, i)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_double(obj, key, d)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_bool(obj, key, b)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_null(obj, key)` | UTF-8 | — |
| `XJsonObject_insert_keyUtf8_utf8(obj, key, utf8)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_string(obj, key, XString*)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_string_move(obj, key, XString*)` | UTF-8 | 内容转移，源留空壳 |
| `XJsonObject_insert_keyUtf8_string_ref(obj, key, XString*)` | UTF-8 | **指针接管**（零分配，调用即失所有权） |
| `XJsonObject_insert_keyUtf8_array(obj, key, XJsonArray*)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_array_move(obj, key, XJsonArray*)` | UTF-8 | 内容转移，源留空壳 |
| `XJsonObject_insert_keyUtf8_array_ref(obj, key, XJsonArray*)` | UTF-8 | **指针接管** |
| `XJsonObject_insert_keyUtf8_object(obj, key, XJsonObject*)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_object_move(obj, key, XJsonObject*)` | UTF-8 | 内容转移，源留空壳 |
| `XJsonObject_insert_keyUtf8_object_ref(obj, key, XJsonObject*)` | UTF-8 | **指针接管** |
| `XJsonObject_insert_keyUtf8_value(obj, key, XJsonValue*)` | UTF-8 | 拷贝 |
| `XJsonObject_insert_keyUtf8_value_move(obj, key, XJsonValue*)` | UTF-8 | 内容转移，源留空壳 |
| `XJsonObject_insert_value_move(obj, const XString* key, XJsonValue*)` | XString* | 键/值内容转移 |

- 键已存在时**覆盖旧值**，`size` 不增加；
- 键或值非法（NULL、键含内嵌 NUL）返回 `false`；
- `insert_keyUtf8_array_move` / `object_move` 内部使用 `create_move`，所以
  **源对象的空壳必须由调用方 `XClassDelete`**（详见 4.3）。

#### 查询

```c
const XJsonValue* XJsonObject_value_keyUtf8(const XJsonObject* obj, const char* key);      /* 借用；缺失 NULL */
XJsonValue*       XJsonObject_value_copy_keyUtf8(const XJsonObject* obj, const char* key); /* 拷贝；缺失 NULL */
const XJsonValue* XJsonObject_value_base(XJsonObject* obj, const XString* key);            /* 借用 */
bool              XJsonObject_contains_keyUtf8(const XJsonObject* obj, const char* key);
XJsonValue*       XJsonObject_take_keyUtf8(XJsonObject* obj, const char* key);             /* 拷贝并移除 */
bool              XJsonObject_remove_keyUtf8(XJsonObject* obj, const char* key);
XVector*          XJsonObject_keys_base(const XJsonObject* obj);                           /* 新建，需释放 */
bool              XJsonObject_equals(const XJsonObject* l, const XJsonObject* r);
int32_t           XJsonObject_compare(const XJsonObject* l, const XJsonObject* r);
```

#### 序列化

```c
XString*    XJsonObject_toString(const XJsonObject* obj, XJsonDocumentFormat format);
XByteArray* XJsonObject_toJson(const XJsonObject* obj, XJsonDocumentFormat format);
```

格式：`XJsonDocument_Compact`（单行）或 `XJsonDocument_Indented`（4 空格缩进）。
输出键序是 **XMap 排序后的键序**，不是插入顺序。

#### VariantMap / VariantHash

```c
XVariantMap*     XJsonObject_toVariantMap(const XJsonObject* obj);
XVariantMap*     XJsonObject_toVariantMap_move(XJsonObject* obj);   /* 逐值转移 */
XVariantHashMap* XJsonObject_toVariantHash(const XJsonObject* obj);
XJsonObject*     XJsonObject_fromVariantMap(const XVariantMap* map);
XJsonObject*     XJsonObject_fromVariantHash(const XVariantHashMap* hash);
XVariant*        XJsonObject_toVariant(const XJsonObject* obj);
XVariant*        XJsonObject_toVariant_move(XJsonObject* obj);       /* 源清空 */
XVariant*        XJsonObject_toVariant_ref(XJsonObject* obj);        /* 非拥有 */
```

---

### 5.3 XJsonArray

#### 创建与增删改查

```c
XJsonArray* XJsonArray_create(void);
XJsonArray* XJsonArray_create_ex(XMemoryType memory);
XJsonArray* XJsonArray_create_copy(XJsonArray* copy);
XJsonArray* XJsonArray_create_move(XJsonArray* move);   /* 源留空壳 */
void        XJsonArray_init(XJsonArray* array);

/* 基础容器操作（宏映射到 XVector） */
size_t XJsonArray_size_base(const XJsonArray* a);
bool   XJsonArray_isEmpty_base(const XJsonArray* a);
void   XJsonArray_clear_base(XJsonArray* a);            /* 清空、保留容量 */

/* 增删改（value 均为 XJsonValue*） */
bool XJsonArray_append_base(XJsonArray* a, XJsonValue* v);        /* 深拷贝 */
bool XJsonArray_append_move_base(XJsonArray* a, XJsonValue* v);   /* 移动，v 变 Invalid */
bool XJsonArray_prepend_base(XJsonArray* a, XJsonValue* v);       /* 深拷贝 */
bool XJsonArray_prepend_move_base(XJsonArray* a, XJsonValue* v);
bool XJsonArray_insert(XJsonArray* a, int64_t index, XJsonValue* v);
bool XJsonArray_insert_move(XJsonArray* a, int64_t index, XJsonValue* v);
bool XJsonArray_replace(XJsonArray* a, int64_t index, XJsonValue* v);
bool XJsonArray_replace_move(XJsonArray* a, int64_t index, XJsonValue* v);
void XJsonArray_removeAt_base(XJsonArray* a, int64_t index);      /* void，越界忽略 */
```

> **高频事故**：`append_base` 是深拷贝，调用后源 `XJsonValue*` 仍归调用方所有。
> 如果源值内部持有对象/数组（如 `item.data.object = object`），不释放源值就会
> 泄漏整个对象。需要移交所有权时一律用 `append_move_base`。

#### 访问

```c
XJsonValue*       XJsonArray_at(XJsonArray* a, int64_t index);         /* 支持负索引 */
const XJsonValue* XJsonArray_at_const(const XJsonArray* a, int64_t index);
XJsonValue*       XJsonArray_first(const XJsonArray* a);               /* 拷贝；空→Undefined */
XJsonValue*       XJsonArray_last(const XJsonArray* a);                /* 拷贝；空→Undefined */
XJsonValue*       XJsonArray_takeAt(XJsonArray* a, int64_t index);     /* 拷贝并移除 */
bool              XJsonArray_contains(const XJsonArray* a, const XJsonValue* v);
bool              XJsonArray_equals(const XJsonArray* l, const XJsonArray* r);
int32_t           XJsonArray_compare(const XJsonArray* l, const XJsonArray* r);
```

- `at` 支持负索引（`-1` 表示最后一个元素）；`at_base` **不支持**负索引；
- `first/last/takeAt` 返回堆拷贝，**必须释放**；
- 越界：`at` 返回 `NULL`，`takeAt` 返回新建 `Undefined`，`removeAt` 静默忽略。

#### 序列化与列表转换

```c
XString*    XJsonArray_toString(const XJsonArray* a, XJsonDocumentFormat format);
XJsonArray* XJsonArray_fromStringList(const XStringList* list);   /* 拷贝 */
XVariantList* XJsonArray_toVariantList(const XJsonArray* a);
XVariantList* XJsonArray_toVariantList_move(XJsonArray* a);       /* 逐元素转移内容 */
XJsonArray*   XJsonArray_fromVariantList(const XVariantList* list);
```

`toVariantList_move` 与 `XBsonArray_toVariantList_move` 同义：每个元素的内容
被移入 `XVariant`，**移动完成后源数组被清空（size=0）**。

---

### 5.4 XJsonDocument

#### 创建与根节点

```c
XJsonDocument* XJsonDocument_create(void);
XJsonDocument* XJsonDocument_create_ex(XMemoryType memory);
XJsonDocument* XJsonDocument_create_copy(XJsonDocument* copy);
XJsonDocument* XJsonDocument_create_move(XJsonDocument* move);      /* 源清空 */

XJsonDocument* XJsonDocument_create_object(XJsonObject* obj);       /* 深拷贝 */
XJsonDocument* XJsonDocument_create_object_move(XJsonObject* obj);  /* 源留空壳 */
XJsonDocument* XJsonDocument_create_array(XJsonArray* arr);         /* 深拷贝 */
XJsonDocument* XJsonDocument_create_array_move(XJsonArray* arr);    /* 源留空壳 */

XJsonValue*       XJsonDocument_root(XJsonDocument* doc);
const XJsonValue* XJsonDocument_root_const(const XJsonDocument* doc);
void              XJsonDocument_setRoot(XJsonDocument* doc, const XJsonValue* root);
void              XJsonDocument_setRoot_move(XJsonDocument* doc, XJsonValue* root);
bool              XJsonDocument_setObject(XJsonDocument* doc, const XJsonObject* obj);
bool              XJsonDocument_setObject_move(XJsonDocument* doc, XJsonObject* obj);
bool              XJsonDocument_setObject_ref(XJsonDocument* doc, XJsonObject* obj);  /* 指针接管 */
bool              XJsonDocument_setArray(XJsonDocument* doc, const XJsonArray* arr);
bool              XJsonDocument_setArray_move(XJsonDocument* doc, XJsonArray* arr);
bool              XJsonDocument_setArray_ref(XJsonDocument* doc, XJsonArray* arr);    /* 指针接管 */
void              XJsonDocument_clear(XJsonDocument* doc);          /* 根置 Invalid */

bool XJsonDocument_isArray(const XJsonDocument* doc);
bool XJsonDocument_isObject(const XJsonDocument* doc);
bool XJsonDocument_isNull(const XJsonDocument* doc);    /* 根为 Invalid 时 true */
bool XJsonDocument_isEmpty(const XJsonDocument* doc);   /* 同 isNull */
XJsonObject* XJsonDocument_object(XJsonDocument* doc);  /* 根非对象→NULL */
XJsonArray*  XJsonDocument_array(XJsonDocument* doc);   /* 根非数组→NULL */
```

> `isNull` 的语义是「**没有根**（`Invalid`）」，不是「根是 JSON null」。
> 判断 JSON null 请用 `XJsonValue_isNull(XJsonDocument_root_const(doc))`。

#### 解析

```c
XJsonDocument* XJsonDocument_fromString(const XString* json);
XJsonDocument* XJsonDocument_fromString_ex(const XString* json, XJsonParseError* error);
XJsonDocument* XJsonDocument_fromJson(const XByteArray* json);
XJsonDocument* XJsonDocument_fromJson_ex(const XByteArray* json, XJsonParseError* error);
```

- 入参必须是 `XString*` / `XByteArray*`，**不是 `char*`**；把 `char*` 强转成
  `XString*` 会按结构体解引用导致崩溃（2026-11-03 实测）；
- 解析失败返回 `NULL`，`error->error` 为具体错误码，`error->offset` 为字节偏移；
- 空输入（长度为 0）解析失败；
- 允许前后空白，但根值之后出现非空白字符报 `GarbageAtEnd`；
- 嵌套深度上限 **1024**，超过报 `DeepNesting`；
- 输入大小上限 `INT32_MAX`，超过报 `DocumentTooLarge`。

#### 序列化

```c
XString*    XJsonDocument_toString(const XJsonDocument* doc, XJsonDocumentFormat format);
XByteArray* XJsonDocument_toJson(const XJsonDocument* doc, XJsonDocumentFormat format);
```

- `Compact`：单行、无多余空白；
- `Indented`：4 空格缩进，并在末尾追加 `\n`；
- 根为 `Invalid`（空文档）时返回**空** `XByteArray` / 空 `XString`，不是 `NULL`；
- 浮点输出使用 `%.17g` 以保证往返精度；`NaN/Inf` 输出为 `null`。

---

## 6. 解析与序列化规则

### 6.1 错误码（对齐 Qt）

| 错误码 | 触发条件 |
|---|---|
| `XJsonParseError_NoError` | 无错误 |
| `UnterminatedObject` | 对象缺 `}` |
| `MissingNameSeparator` | 键后缺 `:` |
| `UnterminatedArray` | 数组缺 `]` |
| `MissingValueSeparator` | 值之间缺 `,` |
| `IllegalValue` | 非法字面量/值 |
| `TerminationByNumber` | 数字后非法终止字符 |
| `IllegalNumber` | 非法数字格式（如 `1e`） |
| `IllegalEscapeSequence` | 非法转义（如 `\q`） |
| `IllegalUtf8String` | 非法 UTF-8 字节 |
| `UnterminatedString` | 字符串缺结束引号 |
| `MissingObject` | 需要对象的位置不是对象 |
| `DeepNesting` | 嵌套超过 1024 层 |
| `DocumentTooLarge` | 超过 `INT32_MAX` 字节 |
| `GarbageAtEnd` | 根值后还有非空白字符 |

### 6.2 数字

- 纯整数且落在 `int64` 范围 → `XJsonValue_Int`；
- 带小数点/指数，或超出 int64 → `XJsonValue_Double`；
- `toInt` 只接受**数学上为整数**的 double（`3.0 → 3`，`3.5 → 缺省值`）；
- 序列化 double 使用 `%.17g`；`-0.0` 输出 `0`；
- `NaN` / `±Inf` 序列化为 `null`。

### 6.3 字符串

- 输入必须是合法 UTF-8，否则 `IllegalUtf8String`；
- 支持 `\" \\ \/ \b \f \n \r \t` 与 `\uXXXX`（含代理对组合）；
- 输出时对 `"`、`\`、控制字符做转义；非 ASCII 保持 UTF-8 原样输出。

---

## 7. 与 BSON / XVariant 的互转

### 7.1 JSON → BSON

```c
XByteArray* XJsonDocument_toBson(const XJsonDocument* doc);
```

- 根必须是对象或数组；空文档返回 `NULL`；
- 对象键序在 BSON 中按键排序（因为来自 XJsonObject）；
- 整数按范围编码为 int32 / int64；
- BSON 特有类型通过 `$` 前缀包装对象识别（见 `XBson.md` 第 6 节）。

### 7.2 BSON → JSON

```c
XJsonDocument* XJsonDocument_fromBson_document(const XByteArray* bson);
XJsonDocument* XJsonDocument_fromBson_array(const XByteArray* bson);
```

- 输入为完整 BSON 文档（含 4 字节长度前缀与结尾 `0x00`）；
- 失败返回 `NULL`；
- BSON 特有类型会转成 `$oid` / `$date` / `$binarySubtype`+`$binaryData` 等包装
  对象，保证 JSON → BSON 可逆。

### 7.3 JSON 文档 ↔ BSON 文档的完整往返

`XJsonDocument_toBson` → `XJsonDocument_fromBson_document` → `XJsonDocument_toBson`
对本库支持的所有类型**字节一致**（自动化测试 `cross` 场景覆盖）。

---

## 8. 常见陷阱清单（按踩坑频率排序）

### 8.1 `value_keyUtf8` 是借用；要所有权用 `value_copy_keyUtf8`

```c
/* 正确：借用，不释放 */
const XJsonValue* v = XJsonObject_value_keyUtf8(obj, "users");
if (v) { ... }

/* 正确：需要跨修改持有/需要独立生命周期 */
XJsonValue* owned = XJsonObject_value_copy_keyUtf8(obj, "users");
if (owned) { ...; XJsonValue_delete(owned); }
```

- 借用指针在对象发生插入/删除/清空后可能失效，**不要跨修改持有**；
- 历史事故：`XLinkUserStore_load` 曾按「深拷贝」语义使用且不释放，每轮
  泄漏约 1.8 KB；统一为借用后归零。

### 8.2 `append_base` 深拷贝后源值仍归你

```c
/* 错误：object 泄漏 */
XJsonValue item; XJsonValue_init(&item, XJsonValue_Object);
item.data.object = object;
XJsonArray_append_base(users, &item);      /* 拷贝进数组 */
/* item 出作用域，object 无人释放 */

/* 正确：移交所有权 */
XJsonArray_append_move_base(users, &item);
```

实测事故：`XLinkUserStore_save` 每条账户泄漏一个对象（约 4.2 KB/次落盘）。

### 8.3 `*_move` 之后还要释放空壳

见 4.3。尤其注意 `create_move`、`set*_move`、`insert_*_move`、
`XJsonDocument_create_object_move`、`XJsonObject_toVariant_move`。

### 8.4 统一移动语义：源永远留空壳

```c
XString* s = XString_create_utf8("x");
XJsonObject_insert_keyUtf8_string_move(obj, "k", s);  /* 内容进对象 */
XClassDelete((XClass*)s);                             /* 壳仍归你，必须释放 */
```

这条规则适用于全部 `*_move`：`XJsonValue_setString_move`、
`XJsonObject_insert_keyUtf8_string_move/array_move/object_move`、
`XJsonDocument_*_move`、`*_toVariant_move` 等。没有「容器接管指针、源不得
释放」的接口。

### 8.5 `at_base` 不支持负索引

需要负索引语义时用 `XJsonArray_at`（支持 `-1`）。

### 8.6 不要依赖对象键序

`XJsonObject` 是排序 map；序列化、遍历、与 BSON 互转都会重排。需要保序或
重复键时用 `XBsonDocument`。

### 8.7 `fromString` 必须传 `XString*`

`char*` 强转 `XString*` 会按结构体解引用，崩溃点在
`XString_toUtf8`/`XStringToWidePath` 附近，且 Linux 上可能因分支提前返回而
不暴露（平台相关）。

### 8.8 空文档/空根的行为

- `XJsonDocument_create()` 后 `isNull()`/`isEmpty()` 为 `true`；
- `toJson` 返回**空数组**（非 `NULL`）；
- `toBson` 返回 `NULL`；
- `fromBson_document(NULL/空)` 返回 `NULL`。

### 8.9 `clear` 不是 `deinit`

`XJsonValue_clear` 释放内容但保留类型（数组会变空数组）；需要回到
`Invalid` 用 `deinit`。

### 8.10 线程安全

所有容器均无锁、COW 引用计数非原子保护业务语义；**不要跨线程共享同一对象**。
跨线程传递用 `*_move` 转移，接收线程负责最终释放。

### 8.11 解析器上限

- 深度 1024 层；
- 单文档 `INT32_MAX` 字节；
- 超限返回错误而不是崩溃——业务层应检查 `fromString` 返回值与错误码。

### 8.12 跨库同名 API 对照（XJson vs XBson，2026-11 统一后）

两库同名 API 的**行为已统一**：

| API | 统一后行为 |
|---|---|
| `value_keyUtf8` | 两库均**借用**；缺失返回 `NULL`；不得释放 |
| `value_copy_keyUtf8` | 两库均返回**堆拷贝**；缺失返回 `NULL`；必须释放 |
| `create_copy(NULL)` / `create_move(NULL)` | 两库均返回 `NULL` |
| `setString(NULL)` / `setString_utf8(NULL)` | 两库均空操作、保留原值 |
| `toDouble` | 两库均接受整数拓宽（JSON Int / BSON Int32、Int64） |
| `*_move` | 两库均「内容转移、源留空壳」 |
| `*_ref` | 两库均「指针接管、零分配、调用即失所有权」 |
| `*_init(type)` | 两库均只设置类型标签、容器载荷为 `NULL`（空容器用 `create`） |
| `move` 后源状态 | 两库均类型清零（JSON `Invalid` / BSON `type 0`，数值均为 0） |
| `toVariantList_move` / `toVariantMap_move` | 两库均移动后清空源容器 |
| `clear` / `deinit` / `delete` | 语义一致 |

仍保留的**有意差异**（由类型系统/容器模型决定，不是缺陷）：

| 维度 | XJson | XBson |
|---|---|---|
| `toString` | 仅 String | String / Symbol / JavaScript |
| 对象键序 | 排序（XMap） | 插入顺序 |
| 重复键 | 覆盖 | 允许 |
| `remove_keyUtf8` | 唯一键删除 | 删除第一个同名元素 |

---

## 9. 测试与验证

### 9.1 运行

```bat
build_static.bat
bin\XinYueC_Static.exe --test xjson-bson
```

或并入 Data 全量门禁：

```bat
bin\XinYueC_Static.exe --test xdata
```

### 9.2 覆盖内容

`Test/XDataTest/XJsonBsonCoverageTest.c` 覆盖：

- `XJsonValue`：全部创建/设置/取值/拷贝/移动/清除/Variant 桥；含
  `create_copy(NULL)`/`create_move(NULL)` 与 `setString(NULL)` 的契约；
- `XJsonObject`：全部插入接口（含 `_move` / `_ref`）、`value_keyUtf8`（借用）
  与 `value_copy_keyUtf8`（拷贝）、查询/覆盖/删除、`keys`、`equals`、
  序列化、VariantMap/Hash；
- `XJsonArray`：append/prepend/insert/replace/removeAt/at/负索引/
  first/last/takeAt、拷贝/移动、嵌套、toVariantList_move（源清空）、
  StringList/VariantList；
- `XJsonDocument`：全部创建/设根接口（含 `setObject_ref`/`setArray_ref`）、
  Compact/Indented 序列化、解析错误码全集、深度边界、数字边界、
  Unicode/转义、BSON 互转、Variant 桥；
- **所有权/泄漏门禁**：每个场景前后各取一次 `XMemory_statistics`，
  差值非 0 即失败（先跑一轮预热，避免类虚表等一次性分配误报）。
  当前全量 398 项断言全绿。

### 9.3 扩展测试

在 `XJsonBsonCoverageTest.c` 中新增 `scenario_xxx` 函数，并在
`XJsonBsonCoverageTest_run` 的预热轮与计量轮各加一行即可。新场景必须：

1. 自包含地创建并释放全部对象；
2. 不依赖前一个场景遗留状态；
3. 尽量覆盖「拷贝 vs 移动」「成功 vs 失败」两个分支。

---

## 10. 完整示例

### 10.1 解析并安全读取字段

```c
static bool readUserRole(const XString* json, const char* user, int64_t* outRole)
{
    XJsonParseError err;
    XJsonDocument* doc = XJsonDocument_fromString_ex(json, &err);
    const XJsonValue* users;
    bool ok = false;
    if (!doc || !XJsonDocument_isObject(doc))
        return false;

    users = XJsonObject_value_keyUtf8(XJsonDocument_object(doc), "users");
    if (users && XJsonValue_isArray(users)) {
        XJsonArray* array = XJsonValue_toArray(users);
        size_t i;
        for (i = 0; i < XJsonArray_size_base(array); ++i) {
            const XJsonValue* element = XJsonArray_at_const(array, (int64_t)i);
            XJsonObject* object = XJsonValue_toObject(element);
            const XJsonValue* name;
            if (!object) continue;
            name = XJsonObject_value_keyUtf8(object, "name");   /* 借用 */
            if (name && XJsonValue_isString(name) &&
                XString_equals_utf8(XJsonValue_toString(name), user,
                                    XChar_CaseSensitive)) {
                const XJsonValue* role = XJsonObject_value_keyUtf8(object, "role");
                if (role && XJsonValue_isInt(role))
                    *outRole = XJsonValue_toInt(role, 0);
                ok = true;
            }
            if (ok) break;
        }
    }
    XClassDelete((XClass*)doc);
    return ok;
}
```

### 10.2 构建并序列化

```c
XJsonObject* root = XJsonObject_create();
XJsonArray* list = XJsonArray_create();
XJsonValue item;

XJsonValue_init(&item, XJsonValue_Int);
XJsonValue_setInt(&item, 1);
XJsonArray_append_move_base((XVector*)list, &item);    /* item 变 Invalid */
XJsonValue_setString_utf8(&item, "中文");
XJsonArray_append_move_base((XVector*)list, &item);
XJsonValue_deinit(&item);

XJsonObject_insert_keyUtf8_utf8(root, "name", "示例");
XJsonObject_insert_keyUtf8_array(root, "list", list);
XClassDelete((XClass*)list);

XJsonDocument* doc = XJsonDocument_create_object_move(root);
XClassDelete((XClass*)root);                            /* 源空壳 */

XByteArray* bytes = XJsonDocument_toJson(doc, XJsonDocument_Compact);
XPrintf("%s\n", (const char*)XByteArray_constData(bytes));

XClassDelete((XClass*)bytes);
XClassDelete((XClass*)doc);
```

---

## 附：一页速记

| 你要做的事 | 用哪个 API | 关键点 |
|---|---|---|
| 查对象键 | `value_keyUtf8` | 借用，不释放；要所有权用 `value_copy_keyUtf8` |
| 借对象键 | `value_base` | 借用，不要释放 |
| 取走键 | `take_keyUtf8` | 返回拷贝，必须释放 |
| 遍历数组 | `at` / `at_const` | 借用；`at` 支持负索引 |
| 追加元素 | `append_move_base` | 移交所有权，避免泄漏 |
| 拷贝对象/数组 | `create_copy` | COW 共享，写时 detach |
| 移动对象/数组 | `create_move` / `*_move` | 源留空壳，仍需释放 |
| 接管指针（省分配） | `*_ref` | 调用即失所有权，失败由函数释放 |
| 解析 | `fromString` / `fromJson` | 入参必须是 `XString*` / `XByteArray*` |
| 序列化 | `toString` / `toJson` | 键序为排序序 |
| 判空文档 | `isNull`/`isEmpty` | 指根为 `Invalid` |
| 判 JSON null | `XJsonValue_isNull(root)` | 不要用 `isNull` |
