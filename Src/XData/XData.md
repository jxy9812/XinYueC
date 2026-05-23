# XinYueC 数据类型文档

## 目录

- [概述](#概述)
- [XVarList 变量列表](#xvarlist-变量列表)
- [XPair 数据对](#xpair-数据对)
- [XSharedData 共享数据](#xshareddata-共享数据)
- [XVariant 变体类型](#xvariant-变体类型)
- [JSON模块](#json模块)
  - [XJsonValue](#xjsonvalue)
  - [XJsonArray](#xjsonarray)
  - [XJsonObject](#xjsonobject)
  - [XJsonDocument](#xjsondocument)
- [日期时间模块](#日期时间模块)
  - [XDate](#xdate)
  - [XTime](#xtime)
  - [XDateTime](#xdatetime)
- [XHostAddress 主机地址](#xhostaddress-主机地址)
- [XPoint 点坐标](#xpoint-点坐标)
- [BSON模块](#bson模块)
  - [XBsonValue](#xbsonvalue)
  - [XBsonArray](#xbsonarray)
  - [XBsonDocument](#xbsondocument)
- [附录](#附录)

---

## 概述

XinYueC数据类型模块提供了丰富的数据结构和类型支持。

### 核心特性

- **变量列表**：轻量级的可变参数列表
- **数据对**：组合两个不同类型数据的结构
- **共享数据**：支持Copy-On-Write的隐式共享数据
- **变体类型**：可存储多种类型数据的容器
- **JSON支持**：完整的JSON解析和生成
- **日期时间**：跨平台的日期时间处理
- **网络地址**：IPv4/IPv6地址支持
- **BSON支持**：MongoDB风格的二进制JSON

---

## XVarList 变量列表

XVarList是轻量级变量列表，用于存储一系列不同类型的变量。

### 头文件

```c
#include \"XVarList.h\"
```

### 结构体定义

```c
typedef struct XVarList {
    uint8_t* ptr;       // 当前访问指针位置
    FreeMethod m_free;  // 释放方法
    void(*argsDel)(struct XVarList*); // 参数删除函数
    char data[];        // 存储变量数据
} XVarList;
```

### 宏定义

#### XVar

```c
#define XVar(type, var) sizeof(type), &var
```

包装变量的类型大小和地址，用于构建XVarList的参数列表。

**参数:**
- `type` - 变量的数据类型
- `var` - 变量实例

---

#### XVarList_Create

```c
#define XVarList_Create(...) XVarList_create(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
```

创建XVarList实例，自动计算参数数量。

**参数:**
- `...` - 由XVar宏包装的参数列表

**返回值:** 新创建的XVarList实例，失败返回NULL

---

### 函数

#### XVarList_create

```c
XVarList* XVarList_create(uint8_t count, ...)
```

创建XVarList实例。

**参数:**
- `count` - 参数数量
- `...` - 由XVar宏包装的参数列表

**返回值:** 新创建的XVarList实例，失败返回NULL

---

#### XVarList_delete

```c
void XVarList_delete(XVarList* list)
```

释放XVarList实例。

**参数:**
- `list` - XVarList指针

---

### 数据访问宏

#### XVarList_start

```c
#define XVarList_start(list)
```

初始化指针，使其指向数据起始位置。

---

#### XVarList_arg

```c
#define XVarList_arg(list, type)
```

获取当前指针指向的指定类型变量，并移动指针。

---

### 示例

```c
int a = 10;
double b = 3.14;
char c = 'X';

// 创建变量列表
XVarList* list = XVarList_Create(XVar(int, a), XVar(double, b), XVar(char, c));

// 解包变量
XVarList_start(list);
int va = XVarList_arg(list, int);       // 10
double vb = XVarList_arg(list, double); // 3.14
char vc = XVarList_arg(list, char);     // 'X'

XVarList_delete(list);
```

---

## XPair 数据对

XPair用于组合两个不同类型数据的结构体。

### 头文件

```c
#include "XPair.h"
```

### 结构体定义

```c
typedef struct XPair {
    size_t m_firstTypeSize;   // 第一个数据的类型大小
    size_t m_secondTypeSize;  // 第二个数据的类型大小
    char m_data[];            // 数据存储区
} XPair;
```

### 宏定义

#### XPair_Create

```c
#define XPair_Create(firstType, secondType) XPair_create(sizeof(firstType), sizeof(secondType))
```

创建XPair实例的宏。

**参数:**
- `firstType` - 第一个数据的类型
- `secondType` - 第二个数据的类型

**返回值:** 成功返回XPair指针，失败返回NULL

---

#### XPair_Insert

```c
#define XPair_Insert(this_pair, firstData, secondData) XPair_insert(this_pair, &firstData, &secondData)
```

插入两个数据到XPair。

---

#### XPair_First

```c
#define XPair_First(this_pair, firstType) (*(firstType*)XPair_first(this_pair))
```

获取第一个数据。

---

#### XPair_Second

```c
#define XPair_Second(this_pair, secondType) (*(secondType*)XPair_second(this_pair))
```

获取第二个数据。

---

### 函数

#### XPair_create

```c
XPair* XPair_create(const size_t firstTypeSize, const size_t secondTypeSize)
```

创建XPair实例。

**参数:**
- `firstTypeSize` - 第一个数据的类型大小(字节)
- `secondTypeSize` - 第二个数据的类型大小(字节)

**返回值:** 成功返回XPair指针，失败返回NULL

---

#### XPair_create_copy

```c
XPair* XPair_create_copy(const XPair* other)
```

通过深拷贝创建XPair实例。

**参数:**
- `other` - 被拷贝的XPair实例

**返回值:** 成功返回新的XPair指针，失败返回NULL

---

#### XPair_create_move

```c
XPair* XPair_create_move(XPair* other)
```

通过资源移动创建XPair实例。

**参数:**
- `other` - 被移动的XPair实例

**返回值:** 成功返回新的XPair指针，失败返回NULL

---

#### XPair_insert

```c
void XPair_insert(XPair* this_pair, void* firstData, void* secondData)
```

插入两个数据到XPair。

**参数:**
- `this_pair` - XPair指针
- `firstData` - 第一个数据的指针
- `secondData` - 第二个数据的指针

---

#### XPair_first

```c
void* XPair_first(XPair* this_pair)
```

获取第一个数据的指针。

**参数:**
- `this_pair` - XPair指针

**返回值:** 第一个数据的指针

---

#### XPair_second

```c
void* XPair_second(XPair* this_pair)
```

获取第二个数据的指针。

**参数:**
- `this_pair` - XPair指针

**返回值:** 第二个数据的指针

---

#### XPair_delete

```c
void XPair_delete(XPair* this_pair)
```

释放XPair实例。

**参数:**
- `this_pair` - XPair指针

---

### 示例

```c
// 创建存储int和double的Pair
XPair* pair = XPair_Create(int, double);

int a = 10;
double b = 3.14;
XPair_Insert(pair, a, b);

int first = XPair_First(pair, int);       // 10
double second = XPair_Second(pair, double); // 3.14

XPair_delete(pair);
```

---

## XSharedData 共享数据

XSharedData是隐式共享(Copy-On-Write)数据块，支持引用计数机制。

### 头文件

```c
#include "XSharedData.h"
```

### 结构体定义

```c
typedef struct XSharedData {
    XAtomic_int32_t refCount;  // 原子引用计数(初始为1)
    char data[];               // 实际数据区
} XSharedData;
```

### 函数

#### XSharedData_create

```c
XSharedData* XSharedData_create(void* dataPtr, size_t dataSize)
```

创建并初始化XSharedData块。

**参数:**
- `dataPtr` - 数据区指针
- `dataSize` - 数据大小

**返回值:** 成功返回XSharedData指针，失败返回NULL

---

#### XSharedData_addRef

```c
void XSharedData_addRef(XSharedData* sd)
```

增加引用计数。

**参数:**
- `sd` - XSharedData指针

---

#### XSharedData_release

```c
bool XSharedData_release(XSharedData* sd)
```

减少引用计数，减到0则释放。

**参数:**
- `sd` - XSharedData指针

**返回值:** true表示已释放，false表示还有引用

---

#### XSharedData_release_with

```c
bool XSharedData_release_with(XSharedData* sd, void (*dataDeleter)(void* data, void* arg), void* arg)
```

减少引用计数，减到0时调用回调释放数据。

**参数:**
- `sd` - XSharedData指针
- `dataDeleter` - 数据释放回调
- `arg` - 回调参数

**返回值:** true表示已释放，false表示还有引用

---

#### XSharedData_isShared

```c
bool XSharedData_isShared(const XSharedData* sd)
```

判断数据是否被共享(引用计数 > 1)。

**参数:**
- `sd` - XSharedData指针

**返回值:** 被共享返回true，否则返回false

---

#### XSharedData_refCount

```c
int32_t XSharedData_refCount(const XSharedData* sd)
```

获取引用计数。

**参数:**
- `sd` - XSharedData指针

**返回值:** 当前引用计数

---

### 示例

```c
// 创建共享数据
XSharedData* shared = XSharedData_create(NULL, 100);

// 增加引用
XSharedData_addRef(shared);
printf("Ref count: %d\n", XSharedData_refCount(shared)); // 2

// 释放引用
XSharedData_release(shared); // false, 还有引用
printf("Is shared: %d\n", XSharedData_isShared(shared)); // false

XSharedData_release(shared); // true, 已释放
```

---

## XVariant 变体类型

XVariant是可存储多种类型数据的容器，类似于Qt的QVariant。

### 头文件

```c
#include "XVariant.h"
```

### 支持的类型

XVariant支持多种数据类型，包括：

- 基本类型：bool, int, double, int64_t等
- 字符串：XString, char*
- 容器：XVariantList, XVariantMap
- JSON：XJsonArray, XJsonObject
- 其他：XDateTime, XHostAddress等

### 主要函数

#### XVariant_create_*

```c
XVariant* XVariant_create_bool(bool value);
XVariant* XVariant_create_int(int value);
XVariant* XVariant_create_double(double value);
XVariant* XVariant_create_int64(int64_t value);
XVariant* XVariant_create_string(const XString* str);
XVariant* XVariant_create_variantList(const XVariantList* list);
XVariant* XVariant_create_variantMap(const XVariantMap* map);
```

创建指定类型的XVariant实例。

---

#### XVariant_type

```c
XVariantType XVariant_type(const XVariant* variant)
```

获取XVariant的类型。

**返回值:** XVariantType枚举值

---

#### XVariant_canConvert

```c
bool XVariant_canConvert(const XVariant* variant, XVariantType type)
```

判断是否可以转换为指定类型。

---

#### XVariant_to*

```c
bool XVariant_toBool(const XVariant* variant, bool defaultValue);
int XVariant_toInt(const XVariant* variant, int defaultValue);
double XVariant_toDouble(const XVariant* variant, double defaultValue);
int64_t XVariant_toInt64(const XVariant* variant, int64_t defaultValue);
const XString* XVariant_toString(const XVariant* variant);
```

转换为指定类型的值。

---

#### XVariant_delete

```c
void XVariant_delete(XVariant* variant)
```

释放XVariant实例。

---

---

## JSON模块

XinYueC提供完整的JSON解析和生成支持。

### 头文件

```c
#include "XJson.h"
#include "XJsonValue.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XJsonDocument.h"
```

### 枚举

#### XJsonDocumentFormat

```c
typedef enum XJsonDocumentFormat {
    XJsonDocument_Indented,  // 缩进格式(美化)
    XJsonDocument_Compact    // 紧凑格式
} XJsonDocumentFormat;
```

---

#### XJsonValueType

```c
typedef enum XJsonValueType {
    XJsonValue_Invalid,  // 无效类型
    XJsonValue_Null,     // Null类型
    XJsonValue_Bool,     // 布尔类型
    XJsonValue_Double,   // 双精度浮点
    XJsonValue_Int,      // 64位整数
    XJsonValue_String,   // 字符串
    XJsonValue_Array,    // 数组
    XJsonValue_Object    // 对象
} XJsonValueType;
```

---

### XJsonValue

XJsonValue是JSON值结构体，支持多种JSON数据类型。

#### 结构体定义

```c
typedef struct XJsonValue {
    XJsonValueType type;   // 存储当前值的类型
    union {
        bool boolean;      // 布尔类型值
        double number;     // 双精度浮点类型值
        int64_t integer;   // 64位整数类型值
        XString* string;   // 字符串类型值
        XJsonArray* array; // 数组类型值
        XJsonObject* object; // 对象类型值
    } data;
} XJsonValue;
```

#### 创建函数

##### XJsonValue_create_null

```c
XJsonValue* XJsonValue_create_null(void)
```

创建一个Null类型的XJsonValue实例。

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_bool

```c
XJsonValue* XJsonValue_create_bool(bool value)
```

创建一个布尔类型的XJsonValue实例。

**参数:**
- `value` - 布尔值

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_double

```c
XJsonValue* XJsonValue_create_double(double value)
```

创建一个双精度浮点类型的XJsonValue实例。

**参数:**
- `value` - 双精度浮点值

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_int

```c
XJsonValue* XJsonValue_create_int(int64_t value)
```

创建一个64位整数类型的XJsonValue实例。

**参数:**
- `value` - 64位整数值

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_string

```c
XJsonValue* XJsonValue_create_string(const XString* string)
```

创建一个字符串类型的XJsonValue实例(深拷贝)。

**参数:**
- `string` - 源字符串

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_array

```c
XJsonValue* XJsonValue_create_array(XJsonArray* array)
```

创建一个数组类型的XJsonValue实例(深拷贝)。

**参数:**
- `array` - 源数组

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_object

```c
XJsonValue* XJsonValue_create_object(XJsonObject* object)
```

创建一个对象类型的XJsonValue实例(深拷贝)。

**参数:**
- `object` - 源对象

**返回值:** 成功返回XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_copy

```c
XJsonValue* XJsonValue_create_copy(XJsonValue* copy)
```

通过深拷贝创建XJsonValue实例。

**参数:**
- `copy` - 被拷贝的XJsonValue实例

**返回值:** 成功返回新的XJsonValue指针，失败返回NULL

---

##### XJsonValue_create_move

```c
XJsonValue* XJsonValue_create_move(XJsonValue* move)
```

通过资源移动创建XJsonValue实例。

**参数:**
- `move` - 被移动的XJsonValue实例

**返回值:** 成功返回新的XJsonValue指针，失败返回NULL

---

#### 类型检查函数

##### XJsonValue_type

```c
XJsonValueType XJsonValue_type(const XJsonValue* value)
```

获取XJsonValue的类型。

**参数:**
- `value` - XJsonValue指针

**返回值:** 返回XJsonValueType枚举值

---

##### XJsonValue_isNull

```c
bool XJsonValue_isNull(const XJsonValue* value)
```

检查是否为Null类型。

---

##### XJsonValue_isBool

```c
bool XJsonValue_isBool(const XJsonValue* value)
```

检查是否为布尔类型。

---

##### XJsonValue_isDouble

```c
bool XJsonValue_isDouble(const XJsonValue* value)
```

检查是否为双精度浮点类型。

---

##### XJsonValue_isInt

```c
bool XJsonValue_isInt(const XJsonValue* value)
```

检查是否为64位整数类型。

---

##### XJsonValue_isString

```c
bool XJsonValue_isString(const XJsonValue* value)
```

检查是否为字符串类型。

---

##### XJsonValue_isArray

```c
bool XJsonValue_isArray(const XJsonValue* value)
```

检查是否为数组类型。

---

##### XJsonValue_isObject

```c
bool XJsonValue_isObject(const XJsonValue* value)
```

检查是否为对象类型。

---

#### 值获取函数

##### XJsonValue_toBool

```c
bool XJsonValue_toBool(const XJsonValue* value, bool defaultValue)
```

获取布尔类型值。

**参数:**
- `value` - XJsonValue指针
- `defaultValue` - 类型不匹配时的默认值

**返回值:** 布尔值

---

##### XJsonValue_toDouble

```c
double XJsonValue_toDouble(const XJsonValue* value, double defaultValue)
```

获取双精度浮点类型值。

---

##### XJsonValue_toInt

```c
int64_t XJsonValue_toInt(const XJsonValue* value, int64_t defaultValue)
```

获取64位整数类型值。

---

##### XJsonValue_toString

```c
const XString* XJsonValue_toString(const XJsonValue* value)
```

获取字符串类型值。

**返回值:** 若为字符串类型返回XString指针，否则返回NULL

---

##### XJsonValue_toArray

```c
XJsonArray* XJsonValue_toArray(const XJsonValue* value)
```

获取数组类型值。

**返回值:** 若为数组类型返回XJsonArray指针，否则返回NULL

---

##### XJsonValue_toObject

```c
XJsonObject* XJsonValue_toObject(const XJsonValue* value)
```

获取对象类型值。

**返回值:** 若为对象类型返回XJsonObject指针，否则返回NULL

---

#### 设置值函数

##### XJsonValue_setNull

```c
void XJsonValue_setNull(XJsonValue* value)
```

设置为Null类型。

---

##### XJsonValue_setBool

```c
void XJsonValue_setBool(XJsonValue* value, bool b)
```

设置为布尔类型。

---

##### XJsonValue_setDouble

```c
void XJsonValue_setDouble(XJsonValue* value, double d)
```

设置为双精度浮点类型。

---

##### XJsonValue_setInt

```c
void XJsonValue_setInt(XJsonValue* value, int64_t i)
```

设置为64位整数类型。

---

##### XJsonValue_setString

```c
void XJsonValue_setString(XJsonValue* value, const XString* s)
```

设置为字符串类型(深拷贝)。

---

##### XJsonValue_setString_utf8

```c
void XJsonValue_setString_utf8(XJsonValue* value, const char* utf8)
```

设置为字符串类型(从UTF-8字符串)。

---

##### XJsonValue_setArray

```c
void XJsonValue_setArray(XJsonValue* value, XJsonArray* a)
```

设置为数组类型(深拷贝)。

---

##### XJsonValue_setObject

```c
void XJsonValue_setObject(XJsonValue* value, XJsonObject* o)
```

设置为对象类型(深拷贝)。

---

#### 析构函数

##### XJsonValue_delete

```c
void XJsonValue_delete(XJsonValue* value)
```

销毁XJsonValue实例。

---

##### XJsonValue_clear

```c
void XJsonValue_clear(XJsonValue* value)
```

清空XJsonValue的内容。

---

### XJsonArray

XJsonArray是JSON数组结构体。

#### 结构体定义

```c
typedef struct XJsonArray {
    XVector elements;  // 存储XJsonValue元素的向量
} XJsonArray;
```

#### 创建函数

##### XJsonArray_create

```c
XJsonArray* XJsonArray_create()
```

创建一个空的XJsonArray实例。

**返回值:** 成功返回XJsonArray指针，失败返回NULL

---

##### XJsonArray_create_copy

```c
XJsonArray* XJsonArray_create_copy(XJsonArray* copy)
```

通过深拷贝创建XJsonArray实例。

---

##### XJsonArray_create_move

```c
XJsonArray* XJsonArray_create_move(XJsonArray* move)
```

通过资源移动创建XJsonArray实例。

---

#### 元素访问

##### XJsonArray_at

```c
XJsonValue* XJsonArray_at(XJsonArray* array, int64_t index)
```

获取指定索引位置的XJsonValue元素。

**参数:**
- `array` - XJsonArray指针
- `index` - 元素索引(支持负索引，-1表示最后一个元素)

**返回值:** 成功返回XJsonValue指针，索引无效返回NULL

---

##### XJsonArray_at_const

```c
const XJsonValue* XJsonArray_at_const(const XJsonArray* array, int64_t index)
```

获取指定索引位置的XJsonValue元素(只读)。

---

#### 基础操作宏

```c
#define XJsonArray_size_base        XVector_size_base
#define XJsonArray_isEmpty_base     XVector_isEmpty_base
#define XJsonArray_append_base      XVector_append_base
#define XJsonArray_prepend_base     XVector_prepend_base
#define XJsonArray_insert           XVector_insert
#define XJsonArray_removeAt_base    XVector_removeAt_base
#define XJsonArray_clear_base       XVector_clear_base
#define XJsonArray_delete_base      XVector_delete_base
```

#### 转换函数

##### XJsonArray_toString

```c
XString* XJsonArray_toString(const XJsonArray* array, XJsonDocumentFormat format)
```

将XJsonArray序列化为XString。

---

##### XJsonArray_toVariantList

```c
XVariantList* XJsonArray_toVariantList(const XJsonArray* array)
```

将XJsonArray转换为XVariantList。

---

---

### XJsonObject

XJsonObject是JSON对象结构体。

#### 结构体定义

```c
typedef struct XJsonObject {
    XMap members;  // 存储键值对(键:XString, 值:XJsonValue)
} XJsonObject;
```

#### 创建函数

##### XJsonObject_create

```c
XJsonObject* XJsonObject_create(void)
```

创建一个空的XJsonObject实例。

**返回值:** 成功返回XJsonObject指针，失败返回NULL

---

##### XJsonObject_create_copy

```c
XJsonObject* XJsonObject_create_copy(XJsonObject* copy)
```

通过深拷贝创建XJsonObject实例。

---

##### XJsonObject_create_move

```c
XJsonObject* XJsonObject_create_move(XJsonObject* move)
```

通过资源移动创建XJsonObject实例。

---

#### 插入操作

##### XJsonObject_insert_keyUtf8_value

```c
bool XJsonObject_insert_keyUtf8_value(XJsonObject* object, const char* key, XJsonValue* value)
```

向JSON对象插入UTF-8键和XJsonValue值(深拷贝值)。

**参数:**
- `object` - XJsonObject指针
- `key` - UTF-8编码的键字符串
- `value` - 要插入的XJsonValue值

**返回值:** 插入成功返回true，失败返回false

---

##### XJsonObject_insert_keyUtf8_double

```c
bool XJsonObject_insert_keyUtf8_double(XJsonObject* object, const char* key, double d)
```

向JSON对象插入UTF-8键和double值。

---

##### XJsonObject_insert_keyUtf8_int

```c
bool XJsonObject_insert_keyUtf8_int(XJsonObject* object, const char* key, int64_t i)
```

向JSON对象插入UTF-8键和int64_t值。

---

##### XJsonObject_insert_keyUtf8_string

```c
bool XJsonObject_insert_keyUtf8_string(XJsonObject* object, const char* key, const XString* str)
```

向JSON对象插入UTF-8键和XString值(深拷贝字符串)。

---

##### XJsonObject_insert_keyUtf8_utf8

```c
bool XJsonObject_insert_keyUtf8_utf8(XJsonObject* object, const char* key, const char* utf8)
```

向JSON对象插入UTF-8键和UTF-8字符串值。

---

##### XJsonObject_insert_keyUtf8_null

```c
bool XJsonObject_insert_keyUtf8_null(XJsonObject* object, const char* key)
```

向JSON对象插入UTF-8键和Null值。

---

##### XJsonObject_insert_keyUtf8_bool

```c
bool XJsonObject_insert_keyUtf8_bool(XJsonObject* object, const char* key, bool b)
```

向JSON对象插入UTF-8键和bool值。

---

##### XJsonObject_insert_keyUtf8_array

```c
bool XJsonObject_insert_keyUtf8_array(XJsonObject* object, const char* key, const XJsonArray* array)
```

向JSON对象插入UTF-8键和XJsonArray值(深拷贝数组)。

---

##### XJsonObject_insert_keyUtf8_object

```c
bool XJsonObject_insert_keyUtf8_object(XJsonObject* object, const char* key, const XJsonObject* value)
```

向JSON对象插入UTF-8键和XJsonObject值(深拷贝对象)。

---

#### 删除操作

##### XJsonObject_remove_keyUtf8

```c
bool XJsonObject_remove_keyUtf8(XJsonObject* object, const char* key)
```

从JSON对象中删除指定UTF-8键对应的键值对。

---

#### 基础操作宏

```c
#define XJsonObject_contains      XMap_contains
#define XJsonObject_value_base    XMap_value_base
#define XJsonObject_find_base     XMap_find_base
#define XJsonObject_size_base     XMap_size_base
#define XJsonObject_isEmpty_base  XMap_isEmpty_base
#define XJsonObject_clear_base    XMap_clear_base
#define XJsonObject_delete_base   XMap_delete_base
```

#### 转换函数

##### XJsonObject_toString

```c
XString* XJsonObject_toString(const XJsonObject* object, XJsonDocumentFormat format)
```

将XJsonObject序列化为XString。

---

##### XJsonObject_toVariantMap

```c
XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object)
```

将XJsonObject转换为XVariantMap。

---

---

### XJsonDocument

XJsonDocument是JSON文档结构体，提供JSON的解析与生成功能。

#### 结构体定义

```c
typedef struct XJsonDocument {
    XJsonValue root;  // 文档的根节点值
} XJsonDocument;
```

#### 创建函数

##### XJsonDocument_create

```c
XJsonDocument* XJsonDocument_create(void)
```

创建一个空的XJsonDocument实例。

**返回值:** 成功返回XJsonDocument指针，失败返回NULL

---

##### XJsonDocument_create_copy

```c
XJsonDocument* XJsonDocument_create_copy(XJsonDocument* copy)
```

通过深拷贝创建XJsonDocument实例。

---

##### XJsonDocument_create_move

```c
XJsonDocument* XJsonDocument_create_move(XJsonDocument* move)
```

通过资源移动创建XJsonDocument实例。

---

##### XJsonDocument_create_object

```c
XJsonDocument* XJsonDocument_create_object(XJsonObject* object)
```

从XJsonObject创建XJsonDocument(根节点为对象)。

---

##### XJsonDocument_create_array

```c
XJsonDocument* XJsonDocument_create_array(XJsonArray* array)
```

从XJsonArray创建XJsonDocument(根节点为数组)。

---

#### 解析函数

##### XJsonDocument_fromString

```c
XJsonDocument* XJsonDocument_fromString(const XString* json)
```

从XString解析JSON文档。

**参数:**
- `json` - 包含JSON字符串的XString

**返回值:** 成功返回XJsonDocument指针，失败返回NULL

---

##### XJsonDocument_fromJson

```c
XJsonDocument* XJsonDocument_fromJson(const XByteArray* json)
```

从XByteArray解析JSON文档(UTF-8编码)。

**参数:**
- `json` - 包含JSON数据的XByteArray

**返回值:** 成功返回XJsonDocument指针，失败返回NULL

---

#### 序列化函数

##### XJsonDocument_toString

```c
XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format)
```

将JSON文档序列化为XString。

---

##### XJsonDocument_toJson

```c
XByteArray* XJsonDocument_toJson(const XJsonDocument* document, XJsonDocumentFormat format)
```

将JSON文档序列化为XByteArray(UTF-8编码，适合传输)。

---

#### 根节点操作

##### XJsonDocument_root

```c
XJsonValue* XJsonDocument_root(XJsonDocument* document)
```

获取文档的根节点。

---

##### XJsonDocument_object

```c
XJsonObject* XJsonDocument_object(XJsonDocument* document)
```

获取文档根节点的XJsonObject。

**返回值:** 成功返回XJsonObject指针，根节点非对象返回NULL

---

##### XJsonDocument_array

```c
XJsonArray* XJsonDocument_array(XJsonDocument* document)
```

获取文档根节点的XJsonArray。

**返回值:** 成功返回XJsonArray指针，根节点非数组返回NULL

---

##### XJsonDocument_isArray

```c
bool XJsonDocument_isArray(const XJsonDocument* document)
```

判断文档根节点是否为数组。

---

##### XJsonDocument_isObject

```c
bool XJsonDocument_isObject(const XJsonDocument* document)
```

判断文档根节点是否为对象。

---

##### XJsonDocument_isNull

```c
bool XJsonDocument_isNull(const XJsonDocument* document)
```

判断文档根节点是否为Null。

---

##### XJsonDocument_isEmpty

```c
bool XJsonDocument_isEmpty(const XJsonDocument* document)
```

判断文档是否为空。

---

#### 析构函数

##### XJsonDocument_delete

```c
void XJsonDocument_delete(XJsonDocument* document)
```

销毁XJsonDocument实例。

---

##### XJsonDocument_clear

```c
void XJsonDocument_clear(XJsonDocument* document)
```

清空XJsonDocument的内容。

---

### JSON示例

```c
// 解析JSON字符串
XString* jsonStr = XString_fromUtf8("{\"name\":\"test\",\"value\":123}");
XJsonDocument* doc = XJsonDocument_fromString(jsonStr);

if (doc && XJsonDocument_isObject(doc)) {
    XJsonObject* obj = XJsonDocument_object(doc);
    
    // 检查键是否存在
    if (XJsonObject_contains(obj, "name")) {
        XJsonValue* nameVal = XJsonObject_value_base(obj, "name");
        const XString* name = XJsonValue_toString(nameVal);
        printf("name: %s\n", XString_toUtf8(name));
    }
}

XJsonDocument_delete(doc);

// 创建JSON对象
XJsonObject* obj = XJsonObject_create();
XJsonObject_insert_keyUtf8_utf8(obj, "name", "test");
XJsonObject_insert_keyUtf8_int(obj, "value", 123);
XJsonObject_insert_keyUtf8_bool(obj, "active", true);

// 序列化
XJsonDocument* doc2 = XJsonDocument_create_object(obj);
XString* output = XJsonDocument_toString(doc2, XJsonDocument_Indented);
printf("JSON: %s\n", XString_toUtf8(output));

XJsonDocument_delete(doc2);
```

---

## 日期时间模块

XinYueC提供完整的日期时间处理功能。

### 头文件

```c
#include "XDate.h"
#include "XTime.h"
#include "XDateTime.h"
```

---

### XDate

XDate是日期结构体，内部使用儒略日(Julian Day)表示。

#### 结构体定义

```c
typedef struct XDate {
    int64_t m_jd;  // 儒略日，-1表示无效
} XDate;
```

#### 创建函数

##### XDate_create

```c
XDate XDate_create(void)
```

创建一个空的XDate对象。

**返回值:** 空的XDate对象

---

##### XDate_create_date

```c
XDate XDate_create_date(int year, int month, int day)
```

创建一个指定日期的XDate对象。

**参数:**
- `year` - 年份(1-9999)
- `month` - 月份(1-12)
- `day` - 日期(1-31)

**返回值:** 如果日期有效返回对应的XDate对象，否则返回无效对象

---

##### XDate_currentDate

```c
XDate XDate_currentDate(void)
```

获取当前本地日期。

**返回值:** 表示当前日期的XDate对象

---

#### 查询函数

##### XDate_isNull

```c
bool XDate_isNull(const XDate* date)
```

检查日期对象是否为空(未初始化)。

---

##### XDate_isValid

```c
bool XDate_isValid(const XDate* date)
```

检查日期对象是否有效。

---

##### XDate_year

```c
int XDate_year(const XDate* date)
```

获取年份。

**返回值:** 年份(1-9999)，日期无效返回0

---

##### XDate_month

```c
int XDate_month(const XDate* date)
```

获取月份。

**返回值:** 月份(1-12)，日期无效返回0

---

##### XDate_day

```c
int XDate_day(const XDate* date)
```

获取日期。

**返回值:** 日期(1-31)，日期无效返回0

---

##### XDate_dayOfWeek

```c
int XDate_dayOfWeek(const XDate* date)
```

获取星期几。

**返回值:** 星期几(1=Monday, 7=Sunday)，日期无效返回0

---

##### XDate_dayOfYear

```c
int XDate_dayOfYear(const XDate* date)
```

获取一年中的第几天。

**返回值:** 一年中的第几天(1-366)，日期无效返回0

---

##### XDate_daysInMonth

```c
int XDate_daysInMonth(const XDate* date)
```

获取该月的总天数。

---

##### XDate_daysInYear

```c
int XDate_daysInYear(const XDate* date)
```

获取该年的总天数。

**返回值:** 该年的总天数(365或366)

---

#### 日期运算

##### XDate_setDate

```c
bool XDate_setDate(XDate* date, int year, int month, int day)
```

设置日期。

**返回值:** 设置成功返回true，日期无效返回false

---

##### XDate_addDays

```c
XDate XDate_addDays(const XDate* date, int64_t days)
```

在日期上增加指定天数。

**参数:**
- `days` - 要增加的天数(可为负数)

**返回值:** 新的XDate对象

---

##### XDate_addMonths

```c
XDate XDate_addMonths(const XDate* date, int months)
```

在日期上增加指定月数。

---

##### XDate_addYears

```c
XDate XDate_addYears(const XDate* date, int years)
```

在日期上增加指定年数。

---

##### XDate_daysTo

```c
int64_t XDate_daysTo(const XDate* from, const XDate* to)
```

计算两个日期之间的天数差。

**返回值:** 从from到to的天数差(to - from)

---

#### 格式化

##### XDate_toString_format

```c
XString* XDate_toString_format(const XDate* date, const char* format)
```

将日期格式化为字符串。

**参数:**
- `date` - XDate对象指针
- `format` - 格式字符串(如"yyyy-MM-dd")

**返回值:** 格式化后的XString指针

---

##### XDate_toString_iso

```c
XString* XDate_toString_iso(const XDate* date)
```

将日期格式化为ISO标准字符串("yyyy-MM-dd")。

---

##### XDate_fromString_format

```c
XDate XDate_fromString_format(const char* str, const char* format)
```

从格式化字符串解析日期。

---

##### XDate_fromString_iso

```c
XDate XDate_fromString_iso(const char* str)
```

从ISO标准字符串解析日期。

---

#### 静态方法

##### XDate_isValid_static

```c
bool XDate_isValid_static(int year, int month, int day)
```

检查给定的年、月、日是否构成有效日期。

---

##### XDate_isLeapYear_static

```c
bool XDate_isLeapYear_static(int year)
```

判断指定年份是否为闰年。

---

---

### XTime

XTime是时间结构体，内部使用自午夜以来的毫秒数表示。

#### 结构体定义

```c
typedef struct XTime {
    int m_msecs;  // 自午夜以来的毫秒数，-1表示无效
} XTime;
```

#### 创建函数

##### XTime_create

```c
XTime XTime_create(void)
```

创建一个空的XTime对象。

---

##### XTime_create_time

```c
XTime XTime_create_time(int hour, int minute, int second, int msec)
```

创建一个指定时间的XTime对象。

**参数:**
- `hour` - 小时(0-23)
- `minute` - 分钟(0-59)
- `second` - 秒(0-59)
- `msec` - 毫秒(0-999)

**返回值:** 如果时间有效返回对应的XTime对象

---

##### XTime_currentTime

```c
XTime XTime_currentTime(void)
```

获取当前本地时间。

---

#### 查询函数

##### XTime_isNull

```c
bool XTime_isNull(const XTime* time)
```

检查时间对象是否为空。

---

##### XTime_isValid

```c
bool XTime_isValid(const XTime* time)
```

检查时间对象是否有效。

---

##### XTime_hour

```c
int XTime_hour(const XTime* time)
```

获取小时。

**返回值:** 小时(0-23)，时间无效返回-1

---

##### XTime_minute

```c
int XTime_minute(const XTime* time)
```

获取分钟。

---

##### XTime_second

```c
int XTime_second(const XTime* time)
```

获取秒。

---

##### XTime_msec

```c
int XTime_msec(const XTime* time)
```

获取毫秒。

---

#### 时间运算

##### XTime_setHMS

```c
bool XTime_setHMS(XTime* time, int hour, int minute, int second, int msec)
```

设置时间。

**返回值:** 设置成功返回true，时间无效返回false

---

##### XTime_addSecs

```c
XTime XTime_addSecs(const XTime* time, int secs)
```

在时间上增加指定秒数。

---

##### XTime_addMSecs

```c
XTime XTime_addMSecs(const XTime* time, int msecs)
```

在时间上增加指定毫秒数。

---

##### XTime_secsTo

```c
int XTime_secsTo(const XTime* from, const XTime* to)
```

计算两个时间之间的秒数差。

---

##### XTime_msecsTo

```c
int XTime_msecsTo(const XTime* from, const XTime* to)
```

计算两个时间之间的毫秒数差。

---

#### 格式化

##### XTime_toString_format

```c
XString* XTime_toString_format(const XTime* time, const char* format)
```

将时间格式化为字符串。

**参数:**
- `format` - 格式字符串(如"HH:mm:ss")

---

##### XTime_fromString_format

```c
XTime XTime_fromString_format(const char* str, const char* format)
```

从格式化字符串解析时间。

---

#### 静态方法

##### XTime_isValid_static

```c
bool XTime_isValid_static(int hour, int minute, int second, int msec)
```

检查给定的时、分、秒、毫秒是否构成有效时间。

---

---

### XDateTime

XDateTime是日期时间结构体，组合了XDate和XTime。

#### 结构体定义

```c
typedef struct XDateTime {
    XDate m_date;  // 日期部分
    XTime m_time;  // 时间部分
} XDateTime;
```

#### 创建函数

##### XDateTime_create

```c
XDateTime XDateTime_create(void)
```

创建一个空的XDateTime对象。

---

##### XDateTime_create_datetime

```c
XDateTime XDateTime_create_datetime(XDate date, XTime time)
```

创建一个指定日期和时间的XDateTime对象。

---

##### XDateTime_currentDateTime

```c
XDateTime XDateTime_currentDateTime(void)
```

获取当前本地日期和时间。

---

##### XDateTime_currentDateTimeUtc

```c
XDateTime XDateTime_currentDateTimeUtc(void)
```

获取当前的UTC日期和时间。

---

#### 时间戳

##### XDateTime_currentNSecsSinceEpoch

```c
int64_t XDateTime_currentNSecsSinceEpoch(void)
```

获取自Unix纪元以来的当前纳秒数。

---

##### XDateTime_currentMSecsSinceEpoch

```c
int64_t XDateTime_currentMSecsSinceEpoch(void)
```

获取自Unix纪元以来的当前毫秒数。

---

##### XDateTime_currentSecsSinceEpoch

```c
int64_t XDateTime_currentSecsSinceEpoch(void)
```

获取自Unix纪元以来的当前秒数。

---

##### XDateTime_toMSecsSinceEpoch

```c
int64_t XDateTime_toMSecsSinceEpoch(const XDateTime* datetime)
```

转换为自Unix纪元以来的毫秒数。

---

##### XDateTime_toSecsSinceEpoch

```c
int64_t XDateTime_toSecsSinceEpoch(const XDateTime* datetime)
```

转换为自Unix纪元以来的秒数。

---

##### XDateTime_setMSecsSinceEpoch

```c
bool XDateTime_setMSecsSinceEpoch(XDateTime* datetime, int64_t msecs)
```

从毫秒时间戳设置日期时间。

---

##### XDateTime_setSecsSinceEpoch

```c
bool XDateTime_setSecsSinceEpoch(XDateTime* datetime, int64_t secs)
```

从秒时间戳设置日期时间。

---

#### 日期时间操作

##### XDateTime_isNull

```c
bool XDateTime_isNull(const XDateTime* datetime)
```

检查日期时间对象是否为空。

---

##### XDateTime_isValid

```c
bool XDateTime_isValid(const XDateTime* datetime)
```

检查日期时间对象是否有效。

---

##### XDateTime_date

```c
XDate XDateTime_date(const XDateTime* datetime)
```

获取日期部分。

---

##### XDateTime_time

```c
XTime XDateTime_time(const XDateTime* datetime)
```

获取时间部分。

---

##### XDateTime_setDate

```c
bool XDateTime_setDate(XDateTime* datetime, XDate date)
```

设置日期部分。

---

##### XDateTime_setTime

```c
bool XDateTime_setTime(XDateTime* datetime, XTime time)
```

设置时间部分。

---

#### 日期时间运算

##### XDateTime_addDays

```c
XDateTime XDateTime_addDays(const XDateTime* datetime, int64_t days)
```

在日期时间上增加指定天数。

---

##### XDateTime_addMonths

```c
XDateTime XDateTime_addMonths(const XDateTime* datetime, int months)
```

在日期时间上增加指定月数。

---

##### XDateTime_addYears

```c
XDateTime XDateTime_addYears(const XDateTime* datetime, int years)
```

在日期时间上增加指定年数。

---

##### XDateTime_addSecs

```c
XDateTime XDateTime_addSecs(const XDateTime* datetime, int64_t secs)
```

在日期时间上增加指定秒数。

---

##### XDateTime_addMSecs

```c
XDateTime XDateTime_addMSecs(const XDateTime* datetime, int64_t msecs)
```

在日期时间上增加指定毫秒数。

---

##### XDateTime_daysTo

```c
int64_t XDateTime_daysTo(const XDateTime* from, const XDateTime* to)
```

计算两个日期时间之间的天数差。

---

##### XDateTime_secsTo

```c
int64_t XDateTime_secsTo(const XDateTime* from, const XDateTime* to)
```

计算两个日期时间之间的秒数差。

---

##### XDateTime_msecsTo

```c
int64_t XDateTime_msecsTo(const XDateTime* from, const XDateTime* to)
```

计算两个日期时间之间的毫秒数差。

---

#### 格式化

##### XDateTime_toString_format

```c
XString* XDateTime_toString_format(const XDateTime* datetime, const char* format)
```

将日期时间格式化为字符串。

---

##### XDateTime_toString_iso

```c
XString* XDateTime_toString_iso(const XDateTime* datetime)
```

将日期时间格式化为ISO标准字符串。

---

##### XDateTime_fromString_format

```c
XDateTime XDateTime_fromString_format(const char* str, const char* format)
```

从格式化字符串解析日期时间。

---

##### XDateTime_fromString_iso

```c
XDateTime XDateTime_fromString_iso(const char* str)
```

从ISO标准字符串解析日期时间。

---

### 日期时间示例

```c
// 获取当前时间
XDateTime now = XDateTime_currentDateTime();

// 格式化输出
XString* str = XDateTime_toString_format(&now, "yyyy-MM-dd HH:mm:ss");
printf("Current: %s\n", XString_toUtf8(str));

// 日期运算
XDateTime tomorrow = XDateTime_addDays(&now, 1);
int64_t diff = XDateTime_secsTo(&now, &tomorrow);  // 86400

// 时间戳
int64_t timestamp = XDateTime_toMSecsSinceEpoch(&now);

// 从时间戳创建
XDateTime fromTs;
XDateTime_setMSecsSinceEpoch(&fromTs, timestamp);
```

---

## XHostAddress 主机地址

XHostAddress表示IPv4或IPv6地址，支持地址解析、类型判断和子网判断。

### 头文件

```c
#include "XHostAddress.h"
```

### 枚举

#### XHostAddress_NetworkLayerProtocol

```c
typedef enum XHostAddress_NetworkLayerProtocol {
    XHostAddress_UnknownNetworkLayerProtocol = -1,  // 未知或null地址
    XHostAddress_IPv4Protocol = 0,                   // IPv4地址
    XHostAddress_IPv6Protocol = 1,                   // IPv6地址
    XHostAddress_AnyIPProtocol = 2                   // 任意IP
} XHostAddress_NetworkLayerProtocol;
```

---

#### XHostAddress_SpecialAddress

```c
typedef enum XHostAddress_SpecialAddress {
    XHostAddress_NullSpecial,           // 空地址
    XHostAddress_AnySpecial,            // IPv4通配(0.0.0.0)
    XHostAddress_AnyIPv4Special,        // 显式IPv4通配
    XHostAddress_AnyIPv6Special,        // IPv6通配(::)
    XHostAddress_LocalHostSpecial,      // IPv4回环(127.0.0.1)
    XHostAddress_LocalHostIPv6Special,  // IPv6回环(::1)
    XHostAddress_BroadcastSpecial,      // IPv4广播
    XHostAddress_AnyAllSpecial          // 任意地址
} XHostAddress_SpecialAddress;
```

---

### 结构体定义

```c
typedef struct XHostAddress {
    XClass m_class;                             // 基类
    uint8_t a6[16];                             // IPv6格式存储地址
    XHostAddress_NetworkLayerProtocol protocol; // 协议类型
    char scopeId[64];                           // IPv6 zone ID
    bool isNull;                                // 是否为null
} XHostAddress;
```

### 创建函数

#### XHostAddress_create

```c
XHostAddress* XHostAddress_create(void)
```

创建null地址。

**返回值:** 新分配的XHostAddress实例

---

#### XHostAddress_create_copy

```c
XHostAddress* XHostAddress_create_copy(const XHostAddress* other)
```

拷贝构造。

---

#### XHostAddress_create_fromString

```c
XHostAddress* XHostAddress_create_fromString(const char* address)
```

从字符串构造(仅接受IP字面量)。

**参数:**
- `address` - IPv4或IPv6字符串(如"192.168.1.1"或"::1")

**返回值:** 新实例，若无效则返回null地址

---

#### XHostAddress_create_fromIPv4Address

```c
XHostAddress* XHostAddress_create_fromIPv4Address(uint32_t ip)
```

从IPv4地址构造(主机字节序)。

**参数:**
- `ip` - 32位IPv4地址(如0x7F000001表示127.0.0.1)

---

#### XHostAddress_create_fromIPv6Address

```c
XHostAddress* XHostAddress_create_fromIPv6Address(const uint8_t ip[16])
```

从IPv6地址构造(16字节数组)。

---

#### XHostAddress_create_fromSpecial

```c
XHostAddress* XHostAddress_create_fromSpecial(XHostAddress_SpecialAddress special)
```

从特殊地址类型构造。

---

### 地址设置

#### XHostAddress_setAddress

```c
void XHostAddress_setAddress(XHostAddress* addr, const char* address)
```

从字符串设置地址。

---

#### XHostAddress_setAddressIPv4

```c
void XHostAddress_setAddressIPv4(XHostAddress* addr, uint32_t ip)
```

从IPv4设置(主机字节序)。

---

#### XHostAddress_setAddressIPv6

```c
void XHostAddress_setAddressIPv6(XHostAddress* addr, const uint8_t ip[16])
```

从IPv6设置。

---

#### XHostAddress_setAddressSpecial

```c
void XHostAddress_setAddressSpecial(XHostAddress* addr, XHostAddress_SpecialAddress special)
```

设置为特殊地址。

---

#### XHostAddress_setScopeId

```c
void XHostAddress_setScopeId(XHostAddress* addr, const char* id)
```

设置IPv6 scope ID。

---

### 地址查询

#### XHostAddress_isNull

```c
bool XHostAddress_isNull(const XHostAddress* addr)
```

判断是否为null地址。

---

#### XHostAddress_protocol

```c
XHostAddress_NetworkLayerProtocol XHostAddress_protocol(const XHostAddress* addr)
```

获取协议类型。

---

#### XHostAddress_toIPv4Address

```c
uint32_t XHostAddress_toIPv4Address(const XHostAddress* addr)
```

转为IPv4地址(主机字节序)。

**返回值:** 32位IPv4地址，若非IPv4则返回0

---

#### XHostAddress_toIPv6Address

```c
void XHostAddress_toIPv6Address(const XHostAddress* addr, uint8_t out[16])
```

转为IPv6地址。

---

#### XHostAddress_scopeId

```c
const char* XHostAddress_scopeId(const XHostAddress* addr)
```

获取scope ID。

---

#### XHostAddress_toString

```c
XString* XHostAddress_toString(const XHostAddress* addr)
```

转为字符串表示。

---

### 地址类型判断

#### XHostAddress_isLoopback

```c
bool XHostAddress_isLoopback(const XHostAddress* addr)
```

是否为回环地址(127.0.0.0/8或::1)。

---

#### XHostAddress_isMulticast

```c
bool XHostAddress_isMulticast(const XHostAddress* addr)
```

是否为多播地址(224.0.0.0/4或ff00::/8)。

---

#### XHostAddress_isGlobal

```c
bool XHostAddress_isGlobal(const XHostAddress* addr)
```

是否为全局可路由地址。

---

#### XHostAddress_isLinkLocal

```c
bool XHostAddress_isLinkLocal(const XHostAddress* addr)
```

是否为链路本地地址(169.254.0.0/16或fe80::/10)。

---

#### XHostAddress_isSiteLocal

```c
bool XHostAddress_isSiteLocal(const XHostAddress* addr)
```

是否为站点本地地址(fec0::/10)。

---

#### XHostAddress_isUniqueLocal

```c
bool XHostAddress_isUniqueLocal(const XHostAddress* addr)
```

是否为唯一本地地址(fc00::/7)。

---

### 子网判断

#### XHostAddress_isInSubnet

```c
bool XHostAddress_isInSubnet(const XHostAddress* addr, const XHostAddress* subnet, int prefixLength)
```

是否在子网中。

**参数:**
- `addr` - 待检测地址
- `subnet` - 子网基地址
- `prefixLength` - 前缀长度(如24表示/24)

**返回值:** 若addr属于subnet/prefixLength返回true

---

#### XHostAddress_parseSubnet

```c
bool XHostAddress_parseSubnet(const char* subnet, XHostAddress* host, int* prefixLen)
```

解析子网字符串(如"192.168.1.0/24")。

---

### 比较

#### XHostAddress_operator_equal

```c
bool XHostAddress_operator_equal(const XHostAddress* a, const XHostAddress* b)
```

相等比较。

---

#### XHostAddress_operator_compare

```c
int XHostAddress_operator_compare(const XHostAddress* a, const XHostAddress* b)
```

排序比较。

**返回值:** <0, ==0, >0

---

### 静态常量

```c
extern const XHostAddress XHostAddress_Null;        // 空地址
extern const XHostAddress XHostAddress_Any;         // IPv4通配(0.0.0.0)
extern const XHostAddress XHostAddress_AnyIPv4;     // 显式IPv4通配
extern const XHostAddress XHostAddress_AnyIPv6;     // IPv6通配(::)
extern const XHostAddress XHostAddress_LocalHost;   // IPv4回环(127.0.0.1)
extern const XHostAddress XHostAddress_LocalHostIPv6; // IPv6回环(::1)
extern const XHostAddress XHostAddress_Broadcast;   // IPv4广播
```

### 辅助函数

#### XHostAddress_isIPv4Address

```c
bool XHostAddress_isIPv4Address(const char* address)
```

判断字符串是否为合法IPv4地址。

---

#### XHostAddress_isIPv6Address

```c
bool XHostAddress_isIPv6Address(const char* address)
```

判断字符串是否为合法IPv6地址。

---

### 示例

```c
// 从字符串创建
XHostAddress* addr = XHostAddress_create_fromString("192.168.1.1");

// 检查地址类型
if (XHostAddress_isLoopback(addr)) {
    printf("This is a loopback address\n");
}

// 转换为字符串
XString* str = XHostAddress_toString(addr);
printf("Address: %s\n", XString_toUtf8(str));

// 特殊地址
XHostAddress* any = XHostAddress_create_fromSpecial(XHostAddress_AnySpecial);
XHostAddress* localhost = XHostAddress_create_fromSpecial(XHostAddress_LocalHostSpecial);

// 子网判断
XHostAddress* subnet = XHostAddress_create_fromString("192.168.0.0");
if (XHostAddress_isInSubnet(addr, subnet, 16)) {
    printf("Address is in subnet\n");
}
```

---

## XPoint 点坐标

XPoint是简单的二维点坐标结构体。

### 头文件

```c
#include "XPoint.h"
```

### 结构体定义

```c
typedef struct XPoint {
    int x;
    int y;
} XPoint;
```

### 函数

#### XPoint_compare

```c
int32_t XPoint_compare(const XPoint* lhs, const XPoint* rhs)
```

比较两个点坐标。

**参数:**
- `lhs` - 左侧XPoint指针
- `rhs` - 右侧XPoint指针

**返回值:** 相等返回XCompare_Equality，否则返回XCompare_Other

---

## BSON模块

BSON(Binary JSON)是一种二进制编码的JSON格式，广泛应用于MongoDB等数据库。

### 头文件

```c
#include "XBson.h"
#include "XBsonValue.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
```

---

### 枚举

#### XBsonType

```c
typedef enum {
    XBSON_TYPE_DOUBLE = 0x01,
    XBSON_TYPE_STRING = 0x02,
    XBSON_TYPE_DOCUMENT = 0x03,
    XBSON_TYPE_ARRAY = 0x04,
    XBSON_TYPE_BINARY = 0x05,
    XBSON_TYPE_OBJECT_ID = 0x07,
    XBSON_TYPE_BOOL = 0x08,
    XBSON_TYPE_DATETIME = 0x09,
    XBSON_TYPE_NULL = 0x0A,
    XBSON_TYPE_REGEX = 0x0B,
    XBSON_TYPE_JAVASCRIPT = 0x0D,
    XBSON_TYPE_INT32 = 0x10,
    XBSON_TYPE_TIMESTAMP = 0x11,
    XBSON_TYPE_INT64 = 0x12,
    XBSON_TYPE_DECIMAL128 = 0x13,
    XBSON_TYPE_MIN_KEY = 0xFF,
    XBSON_TYPE_MAX_KEY = 0x7F
} XBsonType;
```

---

#### XBsonBinarySubtype

```c
typedef enum {
    XBSON_BINARY_GENERIC = 0x00,
    XBSON_BINARY_FUNCTION = 0x01,
    XBSON_BINARY_UUID = 0x04,
    XBSON_BINARY_MD5 = 0x05,
    XBSON_BINARY_ENCRYPTED = 0x06,
    XBSON_BINARY_COLUMN = 0x07
} XBsonBinarySubtype;
```

---

### XBsonValue

XBsonValue是BSON值结构体，支持多种BSON数据类型。

#### 结构体定义

```c
typedef struct XBsonValue {
    XBsonType type;
    union {
        double dbl;
        XString* str;
        struct XBsonDocument* doc;
        struct XBsonArray* arr;
        struct {
            XBsonBinarySubtype subtype;
            XByteArray* data;
        } binary;
        uint8_t oid[12];
        bool boolean;
        int64_t datetime;
        struct {
            XString* pattern;
            XString* options;
        } regex;
        int32_t int32;
        struct {
            uint32_t increment;
            uint32_t timestamp;
        } ts;
        int64_t int64;
        uint8_t decimal[16];
    } data;
} XBsonValue;
```

#### 创建函数

##### XBsonValue_create_null

```c
XBsonValue* XBsonValue_create_null(void)
```

创建一个NULL类型的BSON值。

**返回值:** 成功返回XBsonValue指针，失败返回NULL

---

##### XBsonValue_create_bool

```c
XBsonValue* XBsonValue_create_bool(bool value)
```

创建一个布尔类型的BSON值。

---

##### XBsonValue_create_double

```c
XBsonValue* XBsonValue_create_double(double value)
```

创建一个双精度浮点类型的BSON值。

---

##### XBsonValue_create_int32

```c
XBsonValue* XBsonValue_create_int32(int32_t value)
```

创建一个32位整数类型的BSON值。

---

##### XBsonValue_create_int64

```c
XBsonValue* XBsonValue_create_int64(int64_t value)
```

创建一个64位整数类型的BSON值。

---

##### XBsonValue_create_string

```c
XBsonValue* XBsonValue_create_string(const XString* str)
```

创建一个字符串类型的BSON值。

---

##### XBsonValue_create_document

```c
XBsonValue* XBsonValue_create_document(const XBsonDocument* doc)
```

创建一个文档类型的BSON值。

---

##### XBsonValue_create_array

```c
XBsonValue* XBsonValue_create_array(const XBsonArray* arr)
```

创建一个数组类型的BSON值。

---

##### XBsonValue_create_binary

```c
XBsonValue* XBsonValue_create_binary(XBsonBinarySubtype subtype, const XByteArray* data)
```

创建一个二进制类型的BSON值。

---

##### XBsonValue_create_object_id

```c
XBsonValue* XBsonValue_create_object_id(const uint8_t* oid)
```

创建一个ObjectId类型的BSON值(复制12字节ID)。

---

##### XBsonValue_create_datetime

```c
XBsonValue* XBsonValue_create_datetime(int64_t timestamp)
```

创建一个datetime类型的BSON值(毫秒级时间戳)。

---

##### XBsonValue_create_regex

```c
XBsonValue* XBsonValue_create_regex(const XString* pattern, const XString* options)
```

创建一个正则表达式类型的BSON值。

---

##### XBsonValue_create_javascript

```c
XBsonValue* XBsonValue_create_javascript(const XString* code)
```

创建一个JavaScript代码类型的BSON值。

---

##### XBsonValue_create_timestamp

```c
XBsonValue* XBsonValue_create_timestamp(uint32_t increment, uint32_t timestamp)
```

创建一个timestamp类型的BSON值。

---

##### XBsonValue_create_decimal128

```c
XBsonValue* XBsonValue_create_decimal128(const uint8_t* decimal)
```

创建一个decimal128类型的BSON值。

---

##### XBsonValue_create_min_key

```c
XBsonValue* XBsonValue_create_min_key(void)
```

创建一个MinKey类型的BSON值。

---

##### XBsonValue_create_max_key

```c
XBsonValue* XBsonValue_create_max_key(void)
```

创建一个MaxKey类型的BSON值。

---

#### 类型检查函数

##### XBsonValue_type

```c
XBsonType XBsonValue_type(const XBsonValue* value)
```

获取BSON值的类型。

---

##### XBsonValue_isNull

```c
bool XBsonValue_isNull(const XBsonValue* value)
```

检查是否为NULL类型。

---

##### XBsonValue_isBool

```c
bool XBsonValue_isBool(const XBsonValue* value)
```

检查是否为布尔类型。

---

##### XBsonValue_isDouble

```c
bool XBsonValue_isDouble(const XBsonValue* value)
```

检查是否为双精度浮点类型。

---

##### XBsonValue_isInt32

```c
bool XBsonValue_isInt32(const XBsonValue* value)
```

检查是否为32位整数类型。

---

##### XBsonValue_isInt64

```c
bool XBsonValue_isInt64(const XBsonValue* value)
```

检查是否为64位整数类型。

---

##### XBsonValue_isString

```c
bool XBsonValue_isString(const XBsonValue* value)
```

检查是否为字符串类型。

---

##### XBsonValue_isDocument

```c
bool XBsonValue_isDocument(const XBsonValue* value)
```

检查是否为文档类型。

---

##### XBsonValue_isArray

```c
bool XBsonValue_isArray(const XBsonValue* value)
```

检查是否为数组类型。

---

##### XBsonValue_isBinary

```c
bool XBsonValue_isBinary(const XBsonValue* value)
```

检查是否为二进制类型。

---

##### XBsonValue_isObjectId

```c
bool XBsonValue_isObjectId(const XBsonValue* value)
```

检查是否为ObjectId类型。

---

##### XBsonValue_isDatetime

```c
bool XBsonValue_isDatetime(const XBsonValue* value)
```

检查是否为datetime类型。

---

##### XBsonValue_isRegex

```c
bool XBsonValue_isRegex(const XBsonValue* value)
```

检查是否为正则表达式类型。

---

##### XBsonValue_isJavascript

```c
bool XBsonValue_isJavascript(const XBsonValue* value)
```

检查是否为JavaScript代码类型。

---

##### XBsonValue_isTimestamp

```c
bool XBsonValue_isTimestamp(const XBsonValue* value)
```

检查是否为timestamp类型。

---

##### XBsonValue_isDecimal128

```c
bool XBsonValue_isDecimal128(const XBsonValue* value)
```

检查是否为decimal128类型。

---

#### 值获取函数

##### XBsonValue_toBool

```c
bool XBsonValue_toBool(const XBsonValue* value, bool defaultValue)
```

转换为布尔值。

---

##### XBsonValue_toDouble

```c
double XBsonValue_toDouble(const XBsonValue* value, double defaultValue)
```

转换为双精度浮点数。

---

##### XBsonValue_toInt32

```c
int32_t XBsonValue_toInt32(const XBsonValue* value, int32_t defaultValue)
```

转换为32位整数。

---

##### XBsonValue_toInt64

```c
int64_t XBsonValue_toInt64(const XBsonValue* value, int64_t defaultValue)
```

转换为64位整数。

---

##### XBsonValue_toString

```c
const XString* XBsonValue_toString(const XBsonValue* value)
```

获取字符串数据。

**返回值:** 若为字符串类型返回XString指针，否则返回NULL

---

##### XBsonValue_toDocument

```c
const XBsonDocument* XBsonValue_toDocument(const XBsonValue* value)
```

获取文档数据。

---

##### XBsonValue_toArray

```c
const XBsonArray* XBsonValue_toArray(const XBsonValue* value)
```

获取数组数据。

---

##### XBsonValue_toBinary

```c
const XByteArray* XBsonValue_toBinary(const XBsonValue* value, XBsonBinarySubtype* outSubtype)
```

获取二进制数据。

---

##### XBsonValue_toObjectId

```c
const uint8_t* XBsonValue_toObjectId(const XBsonValue* value)
```

获取ObjectId数据(12字节)。

---

##### XBsonValue_toDatetime

```c
int64_t XBsonValue_toDatetime(const XBsonValue* value, int64_t defaultValue)
```

获取datetime时间戳(毫秒)。

---

##### XBsonValue_toRegexPattern

```c
const XString* XBsonValue_toRegexPattern(const XBsonValue* value)
```

获取正则表达式模式。

---

##### XBsonValue_toRegexOptions

```c
const XString* XBsonValue_toRegexOptions(const XBsonValue* value)
```

获取正则表达式选项。

---

#### 设置值函数

##### XBsonValue_setNull

```c
void XBsonValue_setNull(XBsonValue* value)
```

设置为NULL类型。

---

##### XBsonValue_setBool

```c
void XBsonValue_setBool(XBsonValue* value, bool b)
```

设置为布尔类型。

---

##### XBsonValue_setDouble

```c
void XBsonValue_setDouble(XBsonValue* value, double d)
```

设置为双精度浮点类型。

---

##### XBsonValue_setInt32

```c
void XBsonValue_setInt32(XBsonValue* value, int32_t i)
```

设置为32位整数类型。

---

##### XBsonValue_setInt64

```c
void XBsonValue_setInt64(XBsonValue* value, int64_t i)
```

设置为64位整数类型。

---

##### XBsonValue_setString

```c
void XBsonValue_setString(XBsonValue* value, const XString* str)
```

设置为字符串类型(深拷贝)。

---

##### XBsonValue_setString_utf8

```c
void XBsonValue_setString_utf8(XBsonValue* value, const char* utf8)
```

设置为字符串类型(从UTF-8字符串)。

---

##### XBsonValue_setDocument

```c
void XBsonValue_setDocument(XBsonValue* value, const XBsonDocument* doc)
```

设置为文档类型(深拷贝)。

---

##### XBsonValue_setArray

```c
void XBsonValue_setArray(XBsonValue* value, const XBsonArray* arr)
```

设置为数组类型(深拷贝)。

---

##### XBsonValue_setBinary

```c
void XBsonValue_setBinary(XBsonValue* value, XBsonBinarySubtype subtype, const XByteArray* data)
```

设置为二进制类型。

---

##### XBsonValue_setObjectId

```c
void XBsonValue_setObjectId(XBsonValue* value, const uint8_t* oid)
```

设置为ObjectId类型。

---

##### XBsonValue_setDatetime

```c
void XBsonValue_setDatetime(XBsonValue* value, int64_t timestamp)
```

设置为datetime类型。

---

##### XBsonValue_setRegex

```c
void XBsonValue_setRegex(XBsonValue* value, const XString* pattern, const XString* options)
```

设置为正则表达式类型。

---

##### XBsonValue_setJavascript

```c
void XBsonValue_setJavascript(XBsonValue* value, const XString* code)
```

设置为JavaScript代码类型。

---

##### XBsonValue_setTimestamp

```c
void XBsonValue_setTimestamp(XBsonValue* value, uint32_t increment, uint32_t timestamp)
```

设置为timestamp类型。

---

#### 析构函数

##### XBsonValue_delete

```c
void XBsonValue_delete(XBsonValue* value)
```

销毁XBsonValue实例。

---

##### XBsonValue_clear

```c
void XBsonValue_clear(XBsonValue* value)
```

清空XBsonValue的内容。

---

### XBsonArray

XBsonArray是BSON数组结构体。

#### 结构体定义

```c
typedef struct XBsonArray {
    XVector elements;
} XBsonArray;
```

#### 创建函数

##### XBsonArray_create

```c
XBsonArray* XBsonArray_create()
```

创建一个空的XBsonArray实例。

---

##### XBsonArray_create_copy

```c
XBsonArray* XBsonArray_create_copy(const XBsonArray* other)
```

通过深拷贝创建XBsonArray实例。

---

##### XBsonArray_create_move

```c
XBsonArray* XBsonArray_create_move(XBsonArray* other)
```

通过资源移动创建XBsonArray实例。

---

#### 基础操作宏

```c
#define XBsonArray_at_base          XVector_at_base
#define XBsonArray_append_base      XVector_append_base
#define XBsonArray_append_move_base XVector_append_move_base
#define XBsonArray_size_base        XVector_size_base
#define XBsonArray_isEmpty_base     XVector_isEmpty_base
#define XBsonArray_clear_base       XVector_clear_base
#define XBsonArray_delete_base      XVector_delete_base
```

#### 转换函数

##### XBsonArray_toJsonArray

```c
XJsonArray* XBsonArray_toJsonArray(const XBsonArray* bson_arr)
```

将XBsonArray转换为XJsonArray。

---

##### XBsonArray_fromJsonArray

```c
XBsonArray* XBsonArray_fromJsonArray(const XJsonArray* json_arr)
```

从XJsonArray创建XBsonArray。

---

#### 序列化

##### XBsonArray_toBson

```c
XByteArray* XBsonArray_toBson(const XBsonArray* array)
```

将XBsonArray序列化为BSON格式。

---

##### XBsonArray_fromBson

```c
XBsonArray* XBsonArray_fromBson(XByteArray* data)
```

从BSON格式反序列化创建XBsonArray。

---

---

### XBsonDocument

XBsonDocument是BSON文档结构体，用于存储键值对。

#### 结构体定义

```c
typedef struct XBsonDocument {
    XMap members;
} XBsonDocument;
```

#### 创建函数

##### XBsonDocument_create

```c
XBsonDocument* XBsonDocument_create()
```

创建一个空的XBsonDocument实例。

---

##### XBsonDocument_create_copy

```c
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other)
```

通过深拷贝创建XBsonDocument实例。

---

##### XBsonDocument_create_move

```c
XBsonDocument* XBsonDocument_create_move(XBsonDocument* other)
```

通过资源移动创建XBsonDocument实例。

---

#### 插入操作

##### XBsonDocument_insert_keyUtf8_value

```c
bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* doc, const char* key, XBsonValue* value)
```

插入UTF-8键和XBsonValue值(深拷贝值)。

---

##### XBsonDocument_insert_keyUtf8_double

```c
bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* doc, const char* key, double d)
```

插入UTF-8键和double值。

---

##### XBsonDocument_insert_keyUtf8_int32

```c
bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* doc, const char* key, int32_t i)
```

插入UTF-8键和int32_t值。

---

##### XBsonDocument_insert_keyUtf8_int64

```c
bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* doc, const char* key, int64_t i)
```

插入UTF-8键和int64_t值。

---

##### XBsonDocument_insert_keyUtf8_string

```c
bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* doc, const char* key, const XString* str)
```

插入UTF-8键和XString值。

---

##### XBsonDocument_insert_keyUtf8_utf8

```c
bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* doc, const char* key, const char* utf8)
```

插入UTF-8键和UTF-8字符串值。

---

##### XBsonDocument_insert_keyUtf8_null

```c
bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* doc, const char* key)
```

插入UTF-8键和Null值。

---

##### XBsonDocument_insert_keyUtf8_bool

```c
bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* doc, const char* key, bool b)
```

插入UTF-8键和bool值。

---

##### XBsonDocument_insert_keyUtf8_array

```c
bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* doc, const char* key, const XBsonArray* array)
```

插入UTF-8键和XBsonArray值。

---

##### XBsonDocument_insert_keyUtf8_document

```c
bool XBsonDocument_insert_keyUtf8_document(XBsonDocument* doc, const char* key, const XBsonDocument* newDoc)
```

插入UTF-8键和XBsonDocument值。

---

#### 删除操作

##### XBsonDocument_remove_keyUtf8

```c
bool XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key)
```

删除指定UTF-8键对应的键值对。

---

#### 基础操作宏

```c
#define XBsonDocument_value_base    XMap_value_base
#define XBsonDocument_find_base     XMap_find_base
#define XBsonDocument_contains      XMap_contains
#define XBsonDocument_size_base     XMap_size_base
#define XBsonDocument_isEmpty_base  XMap_isEmpty_base
#define XBsonDocument_clear_base    XMap_clear_base
#define XBsonDocument_delete_base   XMap_delete_base
```

#### 转换函数

##### XBsonDocument_toJsonObject

```c
XJsonObject* XBsonDocument_toJsonObject(const XBsonDocument* bson_obj)
```

将XBsonDocument转换为XJsonObject。

---

##### XBsonDocument_fromJsonObject

```c
XBsonDocument* XBsonDocument_fromJsonObject(const XJsonObject* json_obj)
```

从XJsonObject创建XBsonDocument。

---

##### XBsonDocument_toJson

```c
XByteArray* XBsonDocument_toJson(const XBsonDocument* bson_doc, XJsonDocumentFormat format)
```

将XBsonDocument转换为JSON格式。

---

##### XBsonValue_to_json

```c
XJsonValue* XBsonValue_to_json(const XBsonValue* bson_val)
```

将XBsonValue转换为XJsonValue。

---

##### XBsonValue_from_json

```c
XBsonValue* XBsonValue_from_json(const XJsonValue* json_val)
```

从XJsonValue创建XBsonValue。

---

#### 序列化

##### XBsonDocument_toBson

```c
XByteArray* XBsonDocument_toBson(const XBsonDocument* doc)
```

将XBsonDocument序列化为BSON格式。

---

##### XBsonDocument_fromBson

```c
XBsonDocument* XBsonDocument_fromBson(XByteArray* data)
```

从BSON格式反序列化创建XBsonDocument。

---

### BSON示例

```c
// 创建BSON文档
XBsonDocument* doc = XBsonDocument_create();

// 插入各种类型的值
XBsonDocument_insert_keyUtf8_utf8(doc, "name", "test");
XBsonDocument_insert_keyUtf8_int32(doc, "age", 25);
XBsonDocument_insert_keyUtf8_double(doc, "score", 95.5);
XBsonDocument_insert_keyUtf8_bool(doc, "active", true);
XBsonDocument_insert_keyUtf8_null(doc, "optional");

// 插入嵌套文档
XBsonDocument* nested = XBsonDocument_create();
XBsonDocument_insert_keyUtf8_utf8(nested, "city", "Beijing");
XBsonDocument_insert_keyUtf8_document(doc, "address", nested);
XBsonDocument_delete_base(nested);

// 插入数组
XBsonArray* arr = XBsonArray_create();
XBsonValue* v1 = XBsonValue_create_int32(1);
XBsonValue* v2 = XBsonValue_create_int32(2);
XBsonArray_append_move_base(arr, v1);
XBsonArray_append_move_base(arr, v2);
XBsonDocument_insert_keyUtf8_array(doc, "tags", arr);
XBsonArray_delete_base(arr);

// 序列化为BSON二进制
XByteArray* bsonData = XBsonDocument_toBson(doc);

// 转换为JSON
XByteArray* jsonData = XBsonDocument_toJson(doc, XJsonDocument_Indented);
printf("JSON: %s\n", XByteArray_data(jsonData));

// 清理
XBsonDocument_delete(doc);
```

---

## 附录

### 格式化字符串说明

#### 日期格式

| 格式 | 说明 |
|------|------|
| `yyyy` | 4位数年份 |
| `yy` | 2位数年份 |
| `MM` | 2位数月份(01-12) |
| `M` | 月份(1-12) |
| `dd` | 2位数日期(01-31) |
| `d` | 日期(1-31) |

#### 时间格式

| 格式 | 说明 |
|------|------|
| `HH` | 24小时制小时(00-23) |
| `H` | 24小时制小时(0-23) |
| `hh` | 12小时制小时(01-12) |
| `h` | 12小时制小时(1-12) |
| `mm` | 分钟(00-59) |
| `m` | 分钟(0-59) |
| `ss` | 秒(00-59) |
| `s` | 秒(0-59) |
| `zzz` | 毫秒(000-999) |
| `z` | 毫秒(0-999) |
| `AP` | AM/PM |
| `ap` | am/pm |

### JSON类型对应表

| JSON类型 | XJsonValueType | C类型 |
|---------|----------------|-------|
| null | `XJsonValue_Null` | - |
| true/false | `XJsonValue_Bool` | `bool` |
| number(整数) | `XJsonValue_Int` | `int64_t` |
| number(浮点) | `XJsonValue_Double` | `double` |
| string | `XJsonValue_String` | `XString*` |
| array | `XJsonValue_Array` | `XJsonArray*` |
| object | `XJsonValue_Object` | `XJsonObject*` |

### BSON类型对应表

| BSON类型 | XBsonType | C类型 |
|---------|-----------|-------|
| Double | `XBSON_TYPE_DOUBLE` | `double` |
| String | `XBSON_TYPE_STRING` | `XString*` |
| Document | `XBSON_TYPE_DOCUMENT` | `XBsonDocument*` |
| Array | `XBSON_TYPE_ARRAY` | `XBsonArray*` |
| Binary | `XBSON_TYPE_BINARY` | `XByteArray*` |
| ObjectId | `XBSON_TYPE_OBJECT_ID` | `uint8_t[12]` |
| Boolean | `XBSON_TYPE_BOOL` | `bool` |
| DateTime | `XBSON_TYPE_DATETIME` | `int64_t` |
| Null | `XBSON_TYPE_NULL` | - |
| Int32 | `XBSON_TYPE_INT32` | `int32_t` |
| Timestamp | `XBSON_TYPE_TIMESTAMP` | `uint32_t` + `uint32_t` |
| Int64 | `XBSON_TYPE_INT64` | `int64_t` |
| Decimal128 | `XBSON_TYPE_DECIMAL128` | `uint8_t[16]` |

### 常用头文件包含

```c
// 基础数据类型
#include "XVarList.h"
#include "XPair.h"
#include "XSharedData.h"
#include "XVariant.h"

// JSON
#include "XJson.h"
#include "XJsonValue.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XJsonDocument.h"

// 日期时间
#include "XDate.h"
#include "XTime.h"
#include "XDateTime.h"

// 网络
#include "XHostAddress.h"

// 几何
#include "XPoint.h"

// BSON
#include "XBson.h"
#include "XBsonValue.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
```

### 最佳实践

1. **JSON处理**
   - 使用`XJsonDocument_fromJson`解析JSON字符串
   - 使用`XJsonDocument_toJson`序列化为字节数组
   - 注意检查类型后再访问数据

2. **日期时间**
   - 使用`XDateTime_currentDateTime`获取当前时间
   - 使用Unix时间戳进行跨平台时间传递
   - 格式化字符串区分大小写

3. **网络地址**
   - 使用特殊地址枚举创建常用地址
   - IPv4地址兼容存储在IPv6格式中
   - 使用`isInSubnet`判断地址归属

4. **BSON处理**
   - BSON支持比JSON更多的数据类型
   - 使用`toBson`/`fromBson`进行二进制序列化
   - 可与JSON相互转换

5. **内存管理**
   - 所有`create`函数返回的对象需要手动释放
   - 使用对应的`delete`函数释放
   - 注意移动语义函数的所有权转移
