# XBson —— BSON 1.1 文档模型使用手册

> 适用范围：`Src/XCore/XData/XBson/**`
> 头文件入口：`#include "XBson.h"` 及 `XBsonValue.h` / `XBsonDocument.h` / `XBsonArray.h`
> 自动化回归：`bin\XinYueC_Static.exe --test xjson-bson`（见第 9 节）

---

## 目录

1. [概览](#1-概览)
2. [文件布局与依赖](#2-文件布局与依赖)
3. [对象模型](#3-对象模型)
4. [内存与所有权模型（必读）](#4-内存与所有权模型必读)
5. [API 参考](#5-api-参考)
   - 5.1 [XBsonValue](#51-xbsonvalue)
   - 5.2 [XBsonDocument](#52-xbsondocument)
   - 5.3 [XBsonArray](#53-xbsonarray)
6. [BSON 线格式](#6-bson-线格式)
7. [与 JSON 的互转（$ 包装约定）](#7-与-json-的互转-包装约定)
8. [与 XVariant 的互转](#8-与-xvariant-的互转)
9. [常见陷阱清单（按踩坑频率排序）](#9-常见陷阱清单按踩坑频率排序)
10. [测试与验证](#10-测试与验证)
11. [完整示例](#11-完整示例)

---

## 1. 概览

XBson 是 MongoDB 风格 BSON 1.1 的实现，用于二进制序列化、持久化和跨进程协议。

| 概念 | C 类型 | 说明 |
|---|---|---|
| BSON 值 | `XBsonValue` | 21 种类型（含已废弃类型） |
| BSON 元素 | `XBsonElement` | 键 + 值，由文档拥有 |
| BSON 文档 | `XBsonDocument` | **有序**元素序列，**允许重复键** |
| BSON 数组 | `XBsonArray` | `XBsonValue` 序列，序列化为 `"0","1",...` 键的文档 |

与 XJson 的核心差异：

| 维度 | XJsonObject | XBsonDocument |
|---|---|---|
| 键序 | 排序（红黑树），**不保序** | **保持插入顺序** |
| 重复键 | 覆盖 | **允许**（同名元素共存） |
| 键查找 | 返回深拷贝 | 返回借用指针 / `NULL` |
| 类型系统 | 8 种 JSON 类型 | 21 种 BSON 类型 |
| null 语义 | `Null` 与 `Undefined` 区分 | `NULL`、`Undefined` 均可表达 |

### 1.1 快速上手

```c
#include "XBsonDocument.h"

XBsonDocument* doc = XBsonDocument_create();
XBsonDocument_insert_keyUtf8_utf8(doc, "name", "root");
XBsonDocument_insert_keyUtf8_int32(doc, "role", 1);

XByteArray* bytes = XBsonDocument_toBson(doc);
/* ... 传输/落盘 ... */

XBsonDocument* parsed = XBsonDocument_fromBson(bytes);
XPrintf("name=%s\n",
        XString_toUtf8(XBsonValue_toString(
            XBsonDocument_value_keyUtf8(parsed, "name"))));

XClassDelete((XClass*)parsed);
XClassDelete((XClass*)bytes);
XClassDelete((XClass*)doc);
```

---

## 2. 文件布局与依赖

```
Src/XCore/XData/XBson/
├── XBson.h                     # 类型枚举、二进制子类型、前向声明
├── XBsonValue/
│   ├── XBsonValue.h/.c         # 值类型：21 种 BSON 类型 + 线格式编解码
├── XBsonDocument/
│   ├── XBsonDocument.h/.c      # 有序元素序列 + 文档级序列化 + JSON 桥
└── XBsonArray/
    ├── XBsonArray.h/.c         # XVector<XBsonValue> + 数组级序列化
```

依赖：`XString`、`XByteArray`、`XVector`、`XMemory`、`XJson*`（JSON 桥）。
XBson 值/文档/数组均基于框架的 COW 容器（`XVector`，`useCow = true`）。

---

## 3. 对象模型

```
XBsonDocument
└── m_elements: XVector<XBsonElement>
    └── XBsonElement { XString m_key; XBsonValue m_value; }

XBsonValue
├── type: XBsonType
└── data: union { double; XString*; XBsonDocument*; XBsonArray*; ... }
```

### 3.1 类型枚举（字节值与 BSON 规范一致）

| 类型 | 字节 | 载荷 |
|---|---|---|
| `XBSON_TYPE_DOUBLE` | `0x01` | 8 字节 IEEE754（LE） |
| `XBSON_TYPE_STRING` | `0x02` | UTF-8 字符串 |
| `XBSON_TYPE_DOCUMENT` | `0x03` | 嵌套文档 |
| `XBSON_TYPE_ARRAY` | `0x04` | 数组 |
| `XBSON_TYPE_BINARY` | `0x05` | 子类型 + 字节 |
| `XBSON_TYPE_UNDEFINED` | `0x06` | 无（已废弃） |
| `XBSON_TYPE_OBJECT_ID` | `0x07` | 12 字节 |
| `XBSON_TYPE_BOOL` | `0x08` | 1 字节（0/1） |
| `XBSON_TYPE_DATETIME` | `0x09` | int64 毫秒（UTC） |
| `XBSON_TYPE_NULL` | `0x0A` | 无 |
| `XBSON_TYPE_REGEX` | `0x0B` | pattern + options（C 字符串） |
| `XBSON_TYPE_DBPOINTER` | `0x0C` | namespace + 12 字节 oid（已废弃） |
| `XBSON_TYPE_JAVASCRIPT` | `0x0D` | 代码字符串 |
| `XBSON_TYPE_SYMBOL` | `0x0E` | 符号字符串（已废弃） |
| `XBSON_TYPE_JAVASCRIPT_SCOPE` | `0x0F` | 代码 + 作用域文档 |
| `XBSON_TYPE_INT32` | `0x10` | 4 字节（LE） |
| `XBSON_TYPE_TIMESTAMP` | `0x11` | increment + timestamp（两个 uint32） |
| `XBSON_TYPE_INT64` | `0x12` | 8 字节（LE） |
| `XBSON_TYPE_DECIMAL128` | `0x13` | 原始 16 字节 |
| `XBSON_TYPE_MIN_KEY` | `0xFF` | 无 |
| `XBSON_TYPE_MAX_KEY` | `0x7F` | 无 |

### 3.2 二进制子类型

`XBSON_BINARY_GENERIC(0x00)`、`FUNCTION(0x01)`、`BINARY_OLD(0x02)`、
`UUID_OLD(0x03)`、`UUID(0x04)`、`MD5(0x05)`、`ENCRYPTED(0x06)`、
`COLUMN(0x07)`、`SENSITIVE(0x08)`、`VECTOR(0x09)`、`USER_DEFINED(0x80)`。

> `BINARY_OLD` 的线格式特殊：外层长度包含一个 4 字节内层长度字段，
> 框架会自动处理（见 6.2）。

---

## 4. 内存与所有权模型（必读）

### 4.1 创建与销毁

| 操作 | API | 所有权 |
|---|---|---|
| 堆创建值 | `XBsonValue_create[_xxx]` / `create_copy/move` | `XBsonValue_delete` |
| 栈初始化值 | `XBsonValue_init(&v, type)` | `XBsonValue_deinit(&v)` |
| 堆创建文档 | `XBsonDocument_create[_ex]` / `create_copy/move` | `XClassDelete` |
| 栈初始化文档 | `XBsonDocument_init(&doc)` | `XClassDeinit(&doc)` |
| 堆创建数组 | `XBsonArray_create[_ex]` / `create_copy/move` | `XClassDelete` |
| 栈初始化数组 | `XBsonArray_init(&a)` | `XClassDeinit(&a)` |

```c
/* 堆值 */
XBsonValue* v = XBsonValue_create_int32(7);
XBsonValue_delete(v);

/* 栈值（宏：定义 _v 并让 v 指向它） */
XBsonValue_Init(v, XBSON_TYPE_STRING);
XBsonValue_setString_utf8(v, "x");
XBsonValue_deinit(v);
```

- `XBsonValue_init` 与 `XJsonValue_init` 统一：**只设置类型标签，不分配容器
  载荷**；需要可直接 `toDocument/toArray/toBinary` 的空容器时用
  `XBsonValue_create(type)`（create 会额外分配空载荷）；
- `XBsonValue_deinit` 释放内部容器并把整个值 `memset` 为 0；
- `XBsonValue` 的 `type == 0` 表示「已移动或未初始化」，此时
  `deinit/delete` 是安全的空操作（JSON 侧对应 `Invalid`，数值同为 0）；
- 未知类型 `XBsonValue_create((XBsonType)0x42)` 返回 `NULL`。

### 4.2 `*_move` 的统一约定：转移内容、留下空壳

与 XJson 一致：**内容转移给目标，源对象变成空壳，壳仍由调用方释放。**

```c
XBsonDocument* nested = XBsonDocument_create();
/* ... */
XBsonValue value;
XBsonValue_init(&value, XBSON_TYPE_NULL);
XBsonValue_setDocument_move(&value, nested);  /* nested 内容转移 */
XClassDelete((XClass*)nested);                /* nested 空壳必须释放 */
```

⚠️ **与 XJson 完全一致（2026-11 统一）**：

`XBsonDocument_insert_keyUtf8_string_move` 内部是
`XBsonValue_create_string` + `XString_create_move`，即**源 `XString` 留空壳**，
调用方必须 `XClassDelete`。`XJsonObject_insert_keyUtf8_string_move` 也已统一为
同一语义（内容转移、源留壳），两库不再有差异。

### 4.2.1 `_ref`：指针接管（零分配）

`_move` 会额外分配包装对象；`_ref` 直接把指针交给目标，零分配，适合
「刚创建、不再复用」的字符串/文档/数组：

| API | 作用 |
|---|---|
| `XBsonValue_setString_ref(value, XString*)` | 值接管字符串指针 |
| `XBsonValue_setDocument_ref(value, XBsonDocument*)` | 值接管文档指针 |
| `XBsonValue_setArray_ref(value, XBsonArray*)` | 值接管数组指针 |
| `XBsonValue_setBinary_ref(value, subtype, XByteArray*)` | 值接管二进制指针 |
| `XBsonDocument_insert_keyUtf8_string_ref(doc, key, XString*)` | 文档接管插入字符串 |
| `XBsonDocument_insert_keyUtf8_array_ref(doc, key, XBsonArray*)` | 文档接管插入数组 |
| `XBsonDocument_insert_keyUtf8_document_ref(doc, key, XBsonDocument*)` | 文档接管插入文档 |

契约：

- 参数非 NULL 时**调用即移交所有权**（无论返回 `true` / `false`）；返回
  `false` 时资源已由本函数释放；
- 参数为 NULL 时直接返回 `false`，**不接管**，源仍归调用方；
- 调用后不得再访问或释放源对象。

```c
XString* s = XString_create_utf8("一次性的值");
XBsonDocument_insert_keyUtf8_string_ref(doc, "k", s);  /* 零分配接管 */
/* 不要 XClassDelete(s) */
```

### 4.3 插入接口的所有权

| API | 值语义 | 源对象处理 |
|---|---|---|
| `XBsonDocument_append(doc, key, value)` | 拷贝 | 保留，调用方释放 |
| `XBsonDocument_append_move(doc, key, value)` | 移动 | key/value 留空壳，调用方释放 |
| `XBsonDocument_insert_keyUtf8_*`（不带 move） | 拷贝 | 保留 |
| `XBsonDocument_insert_keyUtf8_*_move` | 移动 | 留空壳，调用方释放 |
| `XBsonDocument_insert_keyUtf8_string_move` | 移动（`XString_create_move`） | **XString 留空壳** |
| `XBsonDocument_insert_keyUtf8_array_move` / `document_move` | 移动 | 源容器留空壳 |
| `XBsonDocument_insert_keyUtf8_value_move` | 移动 | 堆 `XBsonValue*` 留空壳（type=0），调用方 `XBsonValue_delete` |

推荐写法（避免漏壳）：

```c
static bool insertOwned(XBsonDocument* doc, const char* key, XBsonValue* value)
{
    bool ok = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
    XBsonValue_delete(value);   /* 移动后是空壳 */
    return ok;
}
```

框架内部所有便捷插入函数都是这个模式（创建临时 `XBsonValue` → 移动插入 →
删除壳）。

### 4.4 查询返回值所有权

| API | 返回 | 是否释放 |
|---|---|---|
| `XBsonDocument_value_keyUtf8(doc, key)` | **借用** `XBsonValue*`；缺失 `NULL` | 否 |
| `XBsonDocument_value_copy_keyUtf8(doc, key)` | **深拷贝** `XBsonValue*`；缺失 `NULL` | **必须 `XBsonValue_delete`** |
| `XBsonDocument_value_base(doc, key)` | 借用 | 否 |
| `XBsonDocument_indexOf(doc, key)` | 下标；缺失 `-1` | — |
| `XBsonDocument_at_base` / `at_const` | 借用 `XBsonElement*` | 否 |
| `XBsonElement_value(element)` | 借用 `XBsonValue*` | 否 |
| `XBsonDocument_keys_base(doc)` | **新建** `XVector`（键拷贝） | **必须 `XClassDelete`** |
| `XBsonValue_toString/toDocument/toArray/toBinary/toObjectId/...` | 借用 | 否 |
| `XBsonValue_create_*` / `toVariant` | 新建 | **必须释放** |

> 统一口径：`value_keyUtf8` 在两库中都是**借用**，缺失返回 `NULL`，不要
> 释放；需要返回值在文档修改后仍有效时用 `value_copy_keyUtf8`（堆拷贝，
> 必须释放）。

### 4.5 重复键语义

`XBsonDocument` 允许同名元素共存（这是 BSON 规范允许的）：

- `count_keyUtf8(doc, "k")`：返回同名元素个数；
- `value_keyUtf8` / `indexOf`：返回**第一个**匹配；
- `remove_keyUtf8`：删除**第一个**匹配；
- `removeAll_keyUtf8`：删除全部匹配并返回删除个数；
- 遍历 `at_const(doc, i)` 可以看到全部重复元素。

---

## 5. API 参考

### 5.1 XBsonValue

#### 创建

```c
XBsonValue* XBsonValue_create(XBsonType type);
XBsonValue* XBsonValue_create_copy(const XBsonValue* other);
XBsonValue* XBsonValue_create_move(XBsonValue* other);        /* 源 type=0 */

XBsonValue* XBsonValue_create_null(void);
XBsonValue* XBsonValue_create_undefined(void);
XBsonValue* XBsonValue_create_bool(bool value);
XBsonValue* XBsonValue_create_double(double value);
XBsonValue* XBsonValue_create_int32(int32_t value);
XBsonValue* XBsonValue_create_int64(int64_t value);
XBsonValue* XBsonValue_create_string(const XString* str);
XBsonValue* XBsonValue_create_symbol(const XString* str);
XBsonValue* XBsonValue_create_document(const XBsonDocument* doc);   /* 深拷贝 */
XBsonValue* XBsonValue_create_array(const XBsonArray* arr);         /* 深拷贝 */
XBsonValue* XBsonValue_create_binary(XBsonBinarySubtype subtype,
                                     const XByteArray* data);       /* 深拷贝 */
XBsonValue* XBsonValue_create_object_id(const uint8_t* oid12);
XBsonValue* XBsonValue_create_datetime(int64_t timestamp_ms);
XBsonValue* XBsonValue_create_regex(const XString* pattern,
                                    const XString* options);
XBsonValue* XBsonValue_create_dbpointer(const XString* ns,
                                        const uint8_t* oid12);
XBsonValue* XBsonValue_create_javascript(const XString* code);
XBsonValue* XBsonValue_create_javascript_scope(const XString* code,
                                               const XBsonDocument* scope);
XBsonValue* XBsonValue_create_timestamp(uint32_t increment, uint32_t timestamp);
XBsonValue* XBsonValue_create_decimal128(const uint8_t* decimal16);
XBsonValue* XBsonValue_create_min_key(void);
XBsonValue* XBsonValue_create_max_key(void);
```

#### 生命周期与拷贝

```c
void XBsonValue_init(XBsonValue* value, XBsonType type);
void XBsonValue_deinit(XBsonValue* value);      /* 释放内容并清零 */
void XBsonValue_delete(XBsonValue* value);      /* 堆对象 */
void XBsonValue_clear(XBsonValue* value);       /* 清空内容、保留类型 */
void XBsonValue_copy(XBsonValue* dest, const XBsonValue* src);  /* 深拷贝 */
void XBsonValue_move(XBsonValue* dest, XBsonValue* src);        /* 源 type=0 */
```

#### 类型判断

```c
XBsonType XBsonValue_type(const XBsonValue* value);
bool XBsonValue_is_type(const XBsonValue* value, XBsonType type);
bool XBsonValue_isNull/isUndefined/isBool/isDouble/isInt32/isInt64/isString/
     isSymbol/isDocument/isArray/isBinary/isObjectId/isDatetime/isRegex/
     isDbpointer/isJavascript/isJavascriptScope/isTimestamp/isDecimal128/
     isMinKey/isMaxKey(const XBsonValue* value);
```

#### 取值（类型不匹配返回缺省值 / NULL）

```c
bool           XBsonValue_toBool(const XBsonValue*, bool def);
double         XBsonValue_toDouble(const XBsonValue*, double def);
int32_t        XBsonValue_toInt32(const XBsonValue*, int32_t def);
int64_t        XBsonValue_toInt64(const XBsonValue*, int64_t def);
const XString* XBsonValue_toString(const XBsonValue*);        /* string/symbol/js */
const XBsonDocument* XBsonValue_toDocument(const XBsonValue*);
const XBsonArray*    XBsonValue_toArray(const XBsonValue*);
const XByteArray*    XBsonValue_toBinary(const XBsonValue*,
                                         XBsonBinarySubtype* outSubtype);
const uint8_t* XBsonValue_toObjectId(const XBsonValue*);
int64_t        XBsonValue_toDatetime(const XBsonValue*, int64_t def);
const XString* XBsonValue_toRegexPattern(const XBsonValue*);
const XString* XBsonValue_toRegexOptions(const XBsonValue*);
const XString* XBsonValue_toDbpointerNamespace(const XBsonValue*);
const uint8_t* XBsonValue_toDbpointerObjectId(const XBsonValue*);
const XBsonDocument* XBsonValue_toJavascriptScope(const XBsonValue*);
const uint8_t* XBsonValue_toDecimal128(const XBsonValue*);
uint32_t       XBsonValue_timestampIncrement(const XBsonValue*);
uint32_t       XBsonValue_timestamp(const XBsonValue*);
```

> `toDouble` 接受 Double / Int32 / Int64（整数按数值拓宽，与
> `XJsonValue_toDouble` 一致）；`toInt32` **只接受** Int32；`toInt64`
> 接受 Int64 **和 Int32**。三者都不接受 String，类型不符返回缺省值。

#### 设置

```c
void XBsonValue_setNull(XBsonValue*);
void XBsonValue_setUndefined(XBsonValue*);
void XBsonValue_setBool(XBsonValue*, bool);
void XBsonValue_setDouble(XBsonValue*, double);
void XBsonValue_setInt32(XBsonValue*, int32_t);
void XBsonValue_setInt64(XBsonValue*, int64_t);
void XBsonValue_setString(XBsonValue*, const XString*);          /* 拷贝 */
void XBsonValue_setString_move(XBsonValue*, XString*);           /* 源留空壳 */
void XBsonValue_setString_ref(XBsonValue*, XString*);            /* 指针接管 */
void XBsonValue_setString_utf8(XBsonValue*, const char*);
void XBsonValue_setSymbol(XBsonValue*, const XString*);          /* 拷贝 */
void XBsonValue_setDocument(XBsonValue*, const XBsonDocument*);  /* 拷贝 */
void XBsonValue_setDocument_move(XBsonValue*, XBsonDocument*);   /* 源留空壳 */
void XBsonValue_setDocument_ref(XBsonValue*, XBsonDocument*);    /* 指针接管 */
void XBsonValue_setArray(XBsonValue*, const XBsonArray*);        /* 拷贝 */
void XBsonValue_setArray_move(XBsonValue*, XBsonArray*);         /* 源留空壳 */
void XBsonValue_setArray_ref(XBsonValue*, XBsonArray*);          /* 指针接管 */
void XBsonValue_setBinary(XBsonValue*, XBsonBinarySubtype,
                          const XByteArray*);                     /* 拷贝 */
void XBsonValue_setBinary_move(XBsonValue*, XBsonBinarySubtype,
                               XByteArray*);                      /* 源留空壳 */
void XBsonValue_setBinary_ref(XBsonValue*, XBsonBinarySubtype,
                              XByteArray*);                       /* 指针接管 */
void XBsonValue_setObjectId(XBsonValue*, const uint8_t* oid12);
void XBsonValue_setDatetime(XBsonValue*, int64_t ms);
void XBsonValue_setRegex(XBsonValue*, const XString* pattern,
                         const XString* options);
void XBsonValue_setDbpointer(XBsonValue*, const XString* ns,
                             const uint8_t* oid12);
void XBsonValue_setJavascript(XBsonValue*, const XString* code);
void XBsonValue_setJavascript_scope(XBsonValue*, const XString* code,
                                    const XBsonDocument* scope);
void XBsonValue_setTimestamp(XBsonValue*, uint32_t increment,
                             uint32_t timestamp);
void XBsonValue_setDecimal128(XBsonValue*, const uint8_t* decimal16);
void XBsonValue_setMin_key(XBsonValue*);
void XBsonValue_setMax_key(XBsonValue*);
```

所有 `set*` 都会先释放旧内容。Regex 的 options 必须按字节升序（例如 `"im"`），
否则序列化会失败（写入前会校验）。

#### 单元素编解码

```c
bool XBsonValue_serialize(const XBsonValue* value, const char* key,
                          XByteArray* output);
XBsonValue* XBsonValue_deserialize(const uint8_t** ptr,
                                   const uint8_t* end,
                                   XString** key_out);
```

- `serialize` 写一个完整元素：`type byte + key cstring + value`；
- `deserialize` 解析一个元素并返回新建值，`key_out` 为新建 `XString*`
  （调用方释放）；失败返回 `NULL`；
- 这两个函数不写文档长度前缀/结尾，文档级编解码用
  `XBsonDocument_toBson/fromBson`。

#### JSON 桥

```c
XJsonValue* XBsonValue_to_json(const XBsonValue* value);      /* 新建 */
XBsonValue* XBsonValue_from_json(const XJsonValue* value);    /* 新建 */
```

#### XVariant 桥

```c
XVariant*   XBsonValue_toVariant(const XBsonValue*);
XVariant*   XBsonValue_toVariant_move(XBsonValue*);           /* 源 type=0 */
XVariant*   XBsonValue_toVariant_ref(XBsonValue*);            /* 非拥有 */
XBsonValue* XBsonValue_fromVariant(const XVariant*);
XBsonValue* XBsonValue_fromVariant_ref(const XVariant*);
```

---

### 5.2 XBsonDocument

#### 创建与生命周期

```c
XBsonDocument* XBsonDocument_create(void);
XBsonDocument* XBsonDocument_create_ex(XMemoryType memory);
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other);
XBsonDocument* XBsonDocument_create_move(XBsonDocument* other);  /* 源留空壳 */
void           XBsonDocument_init(XBsonDocument* doc);
```

容器接口（宏映射到 `XVector`）：

```c
size_t XBsonDocument_size_base(const XBsonDocument*);
bool   XBsonDocument_isEmpty_base(const XBsonDocument*);
void   XBsonDocument_clear_base(XBsonDocument*);
```

#### 插入

| API | 键 | 值语义 |
|---|---|---|
| `XBsonDocument_append(doc, const XString* key, const XBsonValue* value)` | 拷贝 | 拷贝 |
| `XBsonDocument_append_move(doc, XString* key, XBsonValue* value)` | 移动（key 留壳） | 移动（value type=0） |
| `XBsonDocument_insert_base(doc, key, value)` | 拷贝 | 拷贝 |
| `XBsonDocument_insert_move_base(doc, key, value)` | 移动 | 移动 |
| `XBsonDocument_insert_value_move(doc, key, value)` | 拷贝 | 移动 |
| `XBsonDocument_insert_keyUtf8_value(doc, key, XBsonValue*)` | UTF-8 | 拷贝 |
| `XBsonDocument_insert_keyUtf8_value_move(doc, key, XBsonValue*)` | UTF-8 | 移动（壳由调用方删） |
| `XBsonDocument_insert_keyUtf8_double/int32/int64` | UTF-8 | 值 |
| `XBsonDocument_insert_keyUtf8_string(doc, key, XString*)` | UTF-8 | 拷贝 |
| `XBsonDocument_insert_keyUtf8_string_move(doc, key, XString*)` | UTF-8 | 移动（**XString 留壳**） |
| `XBsonDocument_insert_keyUtf8_string_ref(doc, key, XString*)` | UTF-8 | **指针接管**（零分配） |
| `XBsonDocument_insert_keyUtf8_utf8(doc, key, utf8)` | UTF-8 | 拷贝 |
| `XBsonDocument_insert_keyUtf8_null/bool` | UTF-8 | 值 |
| `XBsonDocument_insert_keyUtf8_array(doc, key, XBsonArray*)` | UTF-8 | 拷贝 |
| `XBsonDocument_insert_keyUtf8_array_move(doc, key, XBsonArray*)` | UTF-8 | 移动（源留壳） |
| `XBsonDocument_insert_keyUtf8_array_ref(doc, key, XBsonArray*)` | UTF-8 | **指针接管** |
| `XBsonDocument_insert_keyUtf8_document(doc, key, XBsonDocument*)` | UTF-8 | 拷贝 |
| `XBsonDocument_insert_keyUtf8_document_move(doc, key, XBsonDocument*)` | UTF-8 | 移动（源留壳） |
| `XBsonDocument_insert_keyUtf8_document_ref(doc, key, XBsonDocument*)` | UTF-8 | **指针接管** |

规则：

- 键不能为空（`NULL`）且**不能包含内嵌 NUL**，否则返回 `false`；
- 插入永远追加到末尾（不覆盖已有同名键）；
- 失败时目标文档不变。

#### 查询与删除

```c
XBsonElement*       XBsonDocument_at_base(XBsonDocument* doc, int64_t index);
const XBsonElement* XBsonDocument_at_const(const XBsonDocument* doc, int64_t index);
XBsonValue*         XBsonDocument_value_base(XBsonDocument* doc, const XString* key);
XBsonValue*         XBsonDocument_value_keyUtf8(XBsonDocument* doc, const char* key);       /* 借用 */
XBsonValue*         XBsonDocument_value_copy_keyUtf8(const XBsonDocument* doc, const char* key); /* 拷贝 */
int64_t             XBsonDocument_indexOf(const XBsonDocument* doc, const XString* key);
size_t              XBsonDocument_count_keyUtf8(const XBsonDocument* doc, const char* key);
bool                XBsonDocument_contains_keyUtf8(const XBsonDocument* doc, const char* key);
bool                XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key);
size_t              XBsonDocument_removeAll_keyUtf8(XBsonDocument* doc, const char* key);
bool                XBsonDocument_removeAt(XBsonDocument* doc, int64_t index);
XVector*            XBsonDocument_keys_base(const XBsonDocument* doc);  /* 新建，需释放 */

/* 元素访问 */
const XString*    XBsonElement_key_const(const XBsonElement* element);
const XBsonValue* XBsonElement_value_const(const XBsonElement* element);
XBsonValue*       XBsonElement_value(XBsonElement* element);
```

- `at_base/at_const` 越界返回 `NULL`；
- `value_keyUtf8` 返回**借用**指针，缺失返回 `NULL`；
- `removeAt` 只接受非负索引，越界/负数返回 `false`；
- `keys_base` 返回新建向量，**必须 `XClassDelete`**。

#### 序列化

```c
XByteArray* XBsonDocument_toBson(const XBsonDocument* doc);
XBsonDocument* XBsonDocument_fromBson(XByteArray* data);   /* 参数非 const 但不消费 */
bool XBsonDocument_from_bytes(XBsonDocument* doc, const uint8_t* data, size_t size);
```

- `toBson`：空文档也生成 5 字节合法文档（`04 00 00 00 00`）；
- `fromBson`：输入长度必须 ≥ 5，且长度字段必须等于实际长度、末字节为 `0`；
- `from_bytes`：先清空目标文档，解析失败返回 `false` 且目标保持空；
- 文档总长度上限 `INT32_MAX`；超过时序列化失败。

#### JSON 桥

```c
XJsonObject* XBsonDocument_toJsonObject(const XBsonDocument* doc);    /* 新建 */
XBsonDocument* XBsonDocument_fromJsonObject(const XJsonObject* obj);  /* 新建 */
XByteArray* XBsonDocument_toJson(const XBsonDocument* doc,
                                 XJsonDocumentFormat format);
XString*    XBsonDocument_toJson_string(const XBsonDocument* doc,
                                        XJsonDocumentFormat format);
```

`fromJsonObject` 会还原 `$oid` / `$date` / `$binary*` 等包装对象
（见第 7 节）。

#### XVariant 桥

```c
XVariantMap*     XBsonDocument_toVariantMap(const XBsonDocument* doc);
XVariantMap*     XBsonDocument_toVariantMap_move(XBsonDocument* doc);
XVariant*        XBsonDocument_toVariant(const XBsonDocument* doc);
XVariant*        XBsonDocument_toVariant_move(XBsonDocument* doc);
XVariant*        XBsonDocument_toVariant_ref(XBsonDocument* doc);
XBsonDocument*   XBsonDocument_fromVariant(const XVariant* variant);
XBsonDocument*   XBsonDocument_fromVariant_ref(const XVariant* variant);
void XBsonDocument_setVariant(XVariant* variant, const XBsonDocument* doc);
void XBsonDocument_setVariant_move(XVariant* variant, XBsonDocument* doc);
void XBsonDocument_setVariant_ref(XVariant* variant, XBsonDocument* doc);
```

> `*_toVariantMap_move` / `*_toVariant_move` 移动后源文档为空（size=0）；
> `*_ref` 为非拥有包装，删除包装前需把 `variant->m_data` 置 `NULL`。

---

### 5.3 XBsonArray

```c
XBsonArray* XBsonArray_create(void);
XBsonArray* XBsonArray_create_ex(XMemoryType memory);
XBsonArray* XBsonArray_create_copy(const XBsonArray* other);   /* COW 浅拷贝 */
XBsonArray* XBsonArray_create_move(XBsonArray* other);         /* 源留空壳 */
void        XBsonArray_init(XBsonArray* array);

size_t XBsonArray_size_base(const XBsonArray*);
bool   XBsonArray_isEmpty_base(const XBsonArray*);
void   XBsonArray_clear_base(XBsonArray*);

bool XBsonArray_append_base(XBsonArray*, XBsonValue*);       /* 拷贝 */
bool XBsonArray_append_move_base(XBsonArray*, XBsonValue*);  /* 移动 */
bool XBsonArray_prepend_base / prepend_move_base(...);
bool XBsonArray_insert / insert_move(XBsonArray*, int64_t index, XBsonValue*);
bool XBsonArray_replace / replace_move(XBsonArray*, int64_t index, XBsonValue*);
void XBsonArray_removeAt_base(XBsonArray*, int64_t index);   /* 越界忽略 */

XBsonValue* XBsonArray_at_base(XBsonArray*, int64_t index);  /* 借用；越界 NULL */

XByteArray*   XBsonArray_toBson(const XBsonArray* array);
XByteArray*   XBsonArray_to_bytes(const XBsonArray* array);
XBsonArray*   XBsonArray_fromBson(XByteArray* data);
bool          XBsonArray_from_bytes(XBsonArray* array, const uint8_t* data, size_t size);

XJsonArray*   XBsonArray_toJsonArray(const XBsonArray* array);
XBsonArray*   XBsonArray_fromJsonArray(const XJsonArray* array);

XVariantList* XBsonArray_toVariantList(const XBsonArray* array);
XVariantList* XBsonArray_toVariantList_move(XBsonArray* array);
XVariant*     XBsonArray_toVariant(const XBsonArray* array);
XVariant*     XBsonArray_toVariant_move(XBsonArray* array);
XVariant*     XBsonArray_toVariant_ref(XBsonArray* array);
XBsonArray*   XBsonArray_fromVariant(const XVariant* variant);
XBsonArray*   XBsonArray_fromVariant_ref(const XVariant* variant);
void XBsonArray_setVariant / setVariant_move / setVariant_ref(...);
```

数组的 BSON 表示是「键为 `"0"`、`"1"`、… 的文档」。`from_bytes` 会校验键
必须严格等于下标十进制串且顺序递增，否则解析失败。

> `XBsonArray_at_base` **不支持负索引**（`XVector_at_base` 语义），越界返回
> `NULL`；需要负索引请自行换算。

---

## 6. BSON 线格式

### 6.1 文档

```
int32 totalLength   (little-endian，包含自身 4 字节和结尾 0x00)
element*
0x00
empty document = 04 00 00 00 00
```

### 6.2 元素

```
type byte | key (UTF-8, 以 0x00 结尾，键内不得含 0x00) | value bytes
```

### 6.3 各类型 value bytes

| 类型 | 编码 |
|---|---|
| Double | 8 字节 IEEE754 LE |
| String/Symbol/JavaScript | `int32 len`（含结尾 NUL）+ UTF-8 + `0x00` |
| Document / Array | 嵌套文档字节（各自带长度前缀） |
| Binary | `int32 size` + `subtype` + data；`BINARY_OLD` 时 `size` 含 4 字节内层长度，内层再写实际长度 |
| Undefined / Null / MinKey / MaxKey | 无 |
| ObjectId | 12 字节 |
| Bool | 1 字节（0/1） |
| Datetime | int64 LE（毫秒） |
| Regex | pattern cstring + options cstring（options 必须字节升序） |
| DBPointer | namespace string + 12 字节 oid |
| JavaScriptScope | `int32 total`（4+4+code+1+scope）+ code string + scope doc |
| Int32 | 4 字节 LE |
| Timestamp | uint32 increment + uint32 timestamp |
| Int64 | 8 字节 LE |
| Decimal128 | 原始 16 字节 |

### 6.4 校验规则（实现已强制）

- 文档长度字段必须等于输入长度，且 ≥ 5、末字节为 `0x00`；
- 键不能包含内嵌 NUL；
- 数组键必须是连续下标字符串；
- Regex options 必须升序；
- 未知类型字节 → 解析失败返回 `NULL`；
- 嵌套文档/数组长度必须自洽。

---

## 7. 与 JSON 的互转（$ 包装约定）

BSON 特有类型在 JSON 中用带 `$` 前缀的包装对象表示，保证
**BSON → JSON → BSON** 类型可逆：

| BSON 类型 | JSON 表示 |
|---|---|
| Document | 普通对象 |
| Array | 普通数组 |
| Binary | `{"$binarySubtype": <int>, "$binaryData": "<hex>"}` |
| ObjectId | `{"$oid": "<24 位 hex>"}` |
| Datetime | `{"$date": <int64 毫秒>}` |
| Regex | `{"$regex": "<pattern>", "$options": "<options>"}` |
| DBPointer | `{"$dbPointer": {"$ref": "<ns>", "$id": {"$oid": "<hex>"}}}` |
| JavaScript | `{"$javascript": "<code>"}` |
| Symbol | `{"$symbol": "<text>"}` |
| JavaScriptScope | `{"$javascript": "<code>", "$scope": { ... }}` |
| Timestamp | `{"$timestamp": <timestamp>, "$increment": <increment>}` |
| Decimal128 | `{"$decimal128": "<32 位 hex>"}` |
| MinKey | `{"$minKey": null}` |
| MaxKey | `{"$maxKey": null}` |
| Undefined | `{"$undefined": true}` |
| Null | `null` |
| Int32/Int64 | JSON 整数 |
| Double | JSON 浮点 |

注意事项：

- 普通业务对象如果恰好含 `$oid` 等键，会在 `fromJsonObject` 时被当作特殊
  类型解析——避免用这些保留键名；
- JSON 对象键序会被排序，所以 **BSON → JSON → BSON 的键序可能变化**，
  但逐键等值（`XJsonObject_equals`）成立；
- BSON → BSON 直接往返（`toBson → fromBson → toBson`）保持原始字节序。

---

## 8. 与 XVariant 的互转

- 标量：`Bool/Double/Int32/Int64/String/...` 映射到同名 `XVariantType`；
- Document ↔ `XVariantMap` / `XVariantType_BsonDocument`；
- Array ↔ `XVariantList` / `XVariantType_BsonArray`；
- 特殊类型在 Variant 中保留类型信息（`XVariantType_BsonValue` 等）；
- `*_toVariant_ref` 是**非拥有包装**：删除包装前必须把 `variant->m_data`
  置 `NULL`，否则会连源对象一起释放；
- `*_toVariant_move` 移动后源对象变为空壳（`type=0` / 空文档），壳仍由调用方
  释放。

---

## 9. 常见陷阱清单（按踩坑频率排序）

### 9.1 `insert_keyUtf8_*_move` 会留下空壳

```c
/* 错误：value 与 key 的壳泄漏 */
XBsonDocument_insert_keyUtf8_value_move(doc, "k", XBsonValue_create_int32(1));

/* 正确 */
XBsonValue* v = XBsonValue_create_int32(1);
XBsonDocument_insert_keyUtf8_value_move(doc, "k", v);
XBsonValue_delete(v);                      /* type=0 空壳 */
```

`insert_keyUtf8_string_move` 的 `XString*` 同理；`array_move`/`document_move`
的源容器同理。这是本库最集中的泄漏来源（自动化测试专门覆盖）。

### 9.1.1 `_ref` 是「调用即失去所有权」

`XBsonDocument_insert_keyUtf8_*_ref` / `XBsonValue_set*_ref` 不做拷贝、不做
`create_move`，直接把指针挂到目标上：

```c
XString* s = XString_create_utf8("v");
XBsonDocument_insert_keyUtf8_string_ref(doc, "k", s);
/* 成功或失败都不要再碰 s：失败时已由函数释放 */
```

参数为 NULL 时才不接管（可直接返回 `false`）。需要保留源对象时用 `_move`，
两者不要混用。

### 9.2 `value_keyUtf8` 是借用；要所有权用 `value_copy_keyUtf8`

两库已统一：

- `XBsonDocument_value_keyUtf8` / `XJsonObject_value_keyUtf8`：**借用**，
  缺失返回 `NULL`，**不要删**；
- `value_copy_keyUtf8`（两库同名）：**堆拷贝**，缺失返回 `NULL`，
  **必须释放**。

借用指针在文档发生插入/删除/清空后可能失效，不要跨修改持有。

### 9.3 `XBsonDocument_at_base` 不支持负索引

与 `XJsonArray_at` 不同；`XBsonDocument_removeAt` 同样只接受非负索引
（负数/越界返回 `false`）。需要负索引请自行换算。

### 9.4 重复键

`insert_*` 永远追加，不覆盖；查询/删除默认只作用第一个。需要合并语义时
先 `removeAll_keyUtf8` 再插入。

### 9.5 Regex options 必须升序

`XBsonValue_setRegex` 不校验，但 `serialize` 会校验；乱序会导致整份文档
序列化失败（返回 `NULL`）。使用前对 options 排序（如 `"im"`）。

### 9.6 空文档

- `XBsonDocument_toBson(空文档)` 返回 5 字节合法文档；
- `XBsonDocument_fromBson` 要求长度 ≥ 5，空指针/短数据返回 `NULL`；
- `XBsonArray_fromBson` 同理。

### 9.7 `XBsonArray_fromBson` / `from_bytes` 会校验下标键

手工拼的数组字节如果键不是 `"0","1",...` 或顺序错乱，解析失败。用
`XBsonArray_toBson` 生成即可。

### 9.8 键不能含 NUL

`append`/`insert` 会检查 UTF-8 键中是否有内嵌 `0x00`，有则返回 `false`。

### 9.9 `type == 0` 是移动后的安全状态

移动后的 `XBsonValue` 仍可安全 `deinit/delete`；不要用 `is_type` 之外的方式
假设它还有内容。

### 9.10 线程安全

与 XJson 相同：非线程安全，跨线程传递用 `*_move` 转移所有权，接收方负责
最终释放。

### 9.11 JSON 保留键

业务 JSON 中避免使用 `$oid`、`$date`、`$binarySubtype`、`$regex` 等保留键，
否则 BSON 互转时会被解释为特殊类型。

### 9.12 跨库同名 API 对照（与 XJson，2026-11 统一后）

- `value_keyUtf8`：两库均**借用**，缺失 `NULL`，不得释放；
- `value_copy_keyUtf8`：两库均**堆拷贝**，缺失 `NULL`，必须释放；
- `create_copy(NULL)` / `create_move(NULL)`：两库均返回 `NULL`；
- `setString(NULL)` / `setString_utf8(NULL)`：两库均空操作；
- `toDouble`：两库均把整数拓宽为 double；
- `*_move`：两库均「内容转移、源留空壳」；
- `*_ref`：两库均「指针接管、零分配」；
- `toVariantList_move` / `toVariantMap_move`：两库均清空源容器；
- `*_init(type)`：两库均只设置类型标签、容器载荷为 `NULL`；
- `move` 后源状态：两库均类型清零（JSON=`Invalid` / BSON=`type 0`）。

保留的有意差异：`toString` 的类型覆盖、键序/重复键语义。详见
`Src/XCore/XData/XJson/XJson.md` 第 8.12 节。

---

## 10. 测试与验证

### 10.1 运行

```bat
build_static.bat
bin\XinYueC_Static.exe --test xjson-bson
```

### 10.2 覆盖内容

`Test/XDataTest/XJsonBsonCoverageTest.c` 中与 BSON 相关的场景：

- `bson_value_all_types`：21 种类型的创建/判断/取值/setter/JSON 桥；
  含 `toDouble` 对 INT32/INT64 的拓宽断言；
- `bson_value_setters`：全部 set*（含 `_ref` 接管）+ 拷贝/移动/clear/deinit；
- `bson_document_inserts`：全部插入接口（含拷贝/移动）+ 查询 + keys +
  `value_copy_keyUtf8` 独立性；
- `bson_document_move_inserts`：5 种 `_move` 接口的空壳处理；
- `bson_document_stack`：栈上 init/deinit；
- `bson_ref_apis` / `json_ref_apis`：`_ref` 指针接管（含 NULL 参数不接管契约）；
- `bson_document_query_remove`：重复键计数/单删/全删/removeAt/clear；
- `bson_document_serialize`：富文档（21 键、21 种类型）toBson/fromBson/
  from_bytes/JSON 对象桥/JSON 字符串；
- `bson_array_ops`：增删改查/拷贝/移动/BSON 往返/JSON 桥；
- `bson_direct_serialize`：单元素 `XBsonValue_serialize/deserialize`、
  空文档往返、非法字节流拒绝；
- `cross`：JSON ↔ BSON 双向字节/语义往返；
- `variant_*`：BSON 文档/数组/值的 Variant 桥与 move/ref（含
  `toVariantMap_move` 移动后源文档清空）。

每个场景前后做堆在用字节数回基线断言；当前全量 398 项断言全绿。

---

## 11. 完整示例

### 11.1 构建含全部类型的文档并往返

```c
static void buildAndRoundtrip(void)
{
    uint8_t oid[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    uint8_t dec[16] = {0};
    XByteArray* bin = XByteArray_create_with_data("\xde\xad", 2);
    XString* pat = XString_create_utf8("^a");
    XString* opt = XString_create_utf8("i");
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    XBsonDocument* parsed;

    XBsonDocument_insert_keyUtf8_double(doc, "d", 2.5);
    XBsonDocument_insert_keyUtf8_int32(doc, "i32", -7);
    XBsonDocument_insert_keyUtf8_int64(doc, "i64", (int64_t)1 << 40);
    XBsonDocument_insert_keyUtf8_utf8(doc, "s", "文本");
    XBsonDocument_insert_keyUtf8_bool(doc, "b", true);
    XBsonDocument_insert_keyUtf8_null(doc, "n");

    {
        XBsonValue* v = XBsonValue_create_binary(XBSON_BINARY_GENERIC, bin);
        XBsonDocument_insert_keyUtf8_value_move(doc, "bin", v);
        XBsonValue_delete(v);
    }
    {
        XBsonValue* v = XBsonValue_create_object_id(oid);
        XBsonDocument_insert_keyUtf8_value_move(doc, "oid", v);
        XBsonValue_delete(v);
    }
    {
        XBsonValue* v = XBsonValue_create_regex(pat, opt);
        XBsonDocument_insert_keyUtf8_value_move(doc, "re", v);
        XBsonValue_delete(v);
    }
    {
        XBsonValue* v = XBsonValue_create_decimal128(dec);
        XBsonDocument_insert_keyUtf8_value_move(doc, "dec", v);
        XBsonValue_delete(v);
    }

    bytes = XBsonDocument_toBson(doc);
    parsed = XBsonDocument_fromBson(bytes);
    /* parsed 与 doc 逐键等值、toBson 字节一致 */

    XClassDelete((XClass*)parsed);
    XClassDelete((XClass*)bytes);
    XClassDelete((XClass*)doc);
    XClassDelete((XClass*)bin);
    XClassDelete((XClass*)pat);
    XClassDelete((XClass*)opt);
}
```

### 11.2 安全读取（借用语义）

```c
static int32_t readInt32(const XBsonDocument* doc, const char* key, int32_t def)
{
    const XBsonValue* v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, key);
    return (v && XBsonValue_isInt32(v)) ? XBsonValue_toInt32(v, def) : def;
}
```

> `XBsonDocument_value_keyUtf8` 的参数类型是 `XBsonDocument*`（非 const），
> 只读场景可用显式转换（实现不修改文档）。

### 11.3 JSON 互转

```c
XByteArray* bson = XBsonDocument_toBson(doc);
XJsonDocument* json = XJsonDocument_fromBson_document(bson);
XByteArray* bson2 = XJsonDocument_toBson(json);   /* 语义等价，键序可能重排 */

XClassDelete((XClass*)bson2);
XClassDelete((XClass*)json);
XClassDelete((XClass*)bson);
```

---

## 附：一页速记

| 你要做的事 | 用哪个 API | 关键点 |
|---|---|---|
| 查文档键 | `value_keyUtf8` | **借用**，不要释放 |
| 查文档键（独立） | `value_copy_keyUtf8` | 堆拷贝，必须释放 |
| 遍历元素 | `at_const(doc, i)` | 借用 |
| 追加元素 | `append`（拷贝）/ `append_move`（移动） | `_move` 后释放壳 |
| 插入特殊类型 | `insert_keyUtf8_value_move` | 堆值壳要 `XBsonValue_delete` |
| 插入字符串（移动） | `insert_keyUtf8_string_move` | **XString 留壳，要释放** |
| 插入字符串（接管） | `insert_keyUtf8_string_ref` | 零分配，调用即失所有权 |
| 数组访问 | `XBsonArray_at_base` | 借用，不支持负索引 |
| 删除 | `remove_keyUtf8` / `removeAll_keyUtf8` / `removeAt` | 重复键语义不同 |
| 序列化 | `XBsonDocument_toBson` | 空文档合法 |
| 反序列化 | `fromBson` / `from_bytes` | 校验长度/终止符/数组下标键 |
| 转 JSON | `toJsonObject` / `toJson` | 特殊类型用 `$` 包装 |
| 转 Variant | `toVariant` / `_move` / `_ref` | ref 包装删除前脱钩 |
