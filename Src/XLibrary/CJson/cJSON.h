/*
  2009 年至 2017 年期间，版权归戴夫・甘布尔（Dave Gamble）和 cJSON 的贡献者所有。
特此授予任何获得本软件及其相关文档文件（以下简称 “软件”）副本的个人免费许可，允许其无限制地处理该软件，包括但不限于使用、复制、修改、合并、发布、分发、再许可和 / 或销售该软件的副本，并允许向其提供软件的人员这样做，但需遵守以下条件：
上述版权声明和本许可声明应包含在该软件的所有副本或实质性部分中。
本软件按 “原样” 提供，不提供任何形式的明示或暗示的保证，包括但不限于适销性、特定用途适用性和非侵权性的保证。在任何情况下，作者或版权所有者均不对任何索赔、损害赔偿或其他责任负责，无论这些责任是因合同、侵权或其他原因引起的，还是因使用或其他方式处理本软件而产生的。
*/
#include"CXinYueConfig.h"
#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(__WINDOWS__) && (defined(WIN32) || defined(WIN64) || defined(_MSC_VER) || defined(_WIN32))
#define __WINDOWS__
#endif

#ifdef __WINDOWS__

/*
版权归戴夫・甘布尔（Dave Gamble）以及 cJSON 的各位贡献者所有（2009 年至 2017 年）。
特此授予任何获得本软件及相关文档文件（以下简称 “软件”）副本的个人免费许可，允许其不受限制地使用本软件，包括但不限于使用、复制、修改、合并、发布、发再和 / 或销售该软件的副本，并允许向获得软件的人员提供该软件，但需遵守以下条件：
上述版权声明和本许可声明应包含在该软件的所有副本或重要部分中。
本软件按 “原样” 提供，不附带任何形式的明示或暗示的保证，包括但不限于适销性、特定用途适用性和非侵权性的保证。在任何情况下，作者或版权所有者均不对因软其用而产生的任何索赔、损害或其他责任（无论是合同责任、侵权责任或其他责任）承担责任，也不对与本软件的使用或其他交易相关的任何责任负责。
*/

#define CJSON_CDECL __cdecl
#define CJSON_STDCALL __stdcall

/* 默认导出符号，这对于复制粘贴 C 文件和头文件来说是必要的 */
#if !defined(CJSON_HIDE_SYMBOLS) && !defined(CJSON_IMPORT_SYMBOLS) && !defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_EXPORT_SYMBOLS
#endif

#if defined(CJSON_HIDE_SYMBOLS)
#define CJSON_PUBLIC(type)   type CJSON_STDCALL
#elif defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllexport) type CJSON_STDCALL
#elif defined(CJSON_IMPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllimport) type CJSON_STDCALL
#endif
#else /* !__WINDOWS__ */
#define CJSON_CDECL
#define CJSON_STDCALL

#if (defined(__GNUC__) || defined(__SUNPRO_CC) || defined (__SUNPRO_C)) && defined(CJSON_API_VISIBILITY)
#define CJSON_PUBLIC(type)   __attribute__((visibility("default"))) type
#else
#define CJSON_PUBLIC(type) type
#endif
#endif

    /* 项目版本 */
#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 7
#define CJSON_VERSION_PATCH 18

#include <stddef.h>

/* cJSON 类型： */
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7) /* raw json */

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* cJSON 结构体： */
typedef struct cJSON
{
    /* “next”（下一个）和 “prev”（上一个）可让你遍历数组或对象链。或者，也可以使用 GetArraySize（获取数组大小）、GetArrayItem（获取数组成员）、GetObjectItem（获取对象成员）函数。 */
    struct cJSON *next;
    struct cJSON *prev;
    /* 一个数组项或对象项会有一个 “child”（子项）指针，它指向数组或对象中各项组成的链。*/
    struct cJSON *child;

    /* 该项的类型，如上述（所定义的类型）。 */
    int type;

    /* 如果类型为 cJSON_String（字符串类型） 或 cJSON_Raw（原始类型），则该项是字符串。 */
    char *valuestring;
    /*写入 valueint 已过时，应改用 cJSON_SetNumberValue 函数。 */
    int valueint;
    /* 如果类型为 cJSON_Number（数字类型），则该项是数字。 */
    double valuedouble;

    /* 如果该项是某个对象的子项，或者在对象的子项列表中，那么该项是该项的名称字符串。 */
    char *string;
} cJSON;

//typedef struct cJSON_Hooks
//{
//      /* malloc/free are CDECL on Windows regardless of the default calling convention of the compiler, so ensure the hooks allow passing those functions directly. */
//      void *(CJSON_CDECL *malloc_fn)(size_t sz);
//      void (CJSON_CDECL *free_fn)(void *ptr);
//} cJSON_Hooks;

//typedef int cJSON_bool;
typedef char cJSON_bool;

/* 该限制规定了在 cJSON 拒绝解析之前，数组 / 对象的嵌套深度上限。
这是为了防止栈溢出。 */
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

/* 以字符串形式返回 cJSON 的版本 */
CJSON_PUBLIC(const char*) cJSON_Version(void);

/* Supply malloc, realloc and free functions to cJSON */
//CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks);

/* 内存管理：调用者始终有责任释放所有 cJSON_Parse 变体函数（使用 cJSON_Delete）和 cJSON_Print 函数（根据情况使用标准库的 free 函数、cJSON_Hooks.free_fn 或者 cJSON_free）返回的结果。例外情况是 cJSON_PrintPreallocated 函数，在该函数中，调用者要完全负责管理缓冲区。 */

/* 提供一段 JSON 数据，此函数会返回一个可供你查询的 cJSON 对象。 */
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length);
/* ParseWithOpts 函数允许你要求（并检查）JSON 字符串以空字符结尾，并且能获取到解析的最后一个字节的指针。 /
/ 如果你在 return_parse_end 中提供了一个指针，且解析失败，那么 return_parse_end 将包含指向错误位置的指针，这与 cJSON_GetErrorPtr() 的返回结果一致。 */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated);

/* 将一个 cJSON 实体渲染为文本，以便进行传输或存储。 */
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item);
/* 将一个 cJSON 实体渲染为无任何格式的文本，用于传输或存储。 */
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item);
/* 使用缓冲策略将一个 cJSON 实体渲染为文本。prebuffer 是对最终文本大小的预估。准确预估可以减少内存重新分配的次数。fmt 参数为 0 时输出无格式文本，为 1 时输出有格式文本。 */
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);
/* 使用已经在内存中分配好且具有指定长度的缓冲区，将一个 cJSON 实体渲染为文本。渲染成功返回 1，失败返回 0。 */
/* 注意：cJSON 在估算其所需内存大小时并非总是 100% 准确，因此为了安全起见，分配的内存要比实际需要的多 5 个字节。 */
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format);
/* 删除一个 cJSON 实体以及所有的子实体。 */
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item);

/* 返回数组（或对象）中元素的数量。 */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array);
/* 从数组 “array” 中获取第 “index” 个元素。若操作失败，则返回 NULL。 */
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index);
/* 从对象中获取名为 “string” 的项。此操作不区分大小写。 */
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
//检查一个 cJSON 对象中是否包含指定键的项
CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string);
/* 用于分析解析失败的情况。此函数会返回一个指向解析错误位置的指针。你可能需要往前查看几个字符才能理解错误的含义。当 cJSON_Parse() 返回 0 时该指针有定义；当 cJSON_Parse() 成功时，该指针为 0（即空指针）。 */
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void);

/* 检查项的类型并返回其值 */
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item);
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item);


/* 这些函数用于检查一个项的类型。 */
CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item);

/* 这些调用会创建一个具有相应类型的 cJSON 项。 */
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num);
CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string);

/* 原始的 JSON 数据 */
CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw);
CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void);

/* 创建一个字符串项，其中 valuestring 引用一个字符串，这样该字符串不会在调用 cJSON_Delete 时被释放。 */
CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string);
/* 创建一个对象或数组，该对象或数组仅引用其元素，这样在调用 cJSON_Delete 时，这些元素不会被释放。 */
CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child);


/* 这些工具函数用于创建一个包含 count 个元素的数组。
参数 count 不能大于数字数组中的元素数量，否则会发生数组越界访问。 */
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count);

/* 将一个项追加到指定的数组或对象中。 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
/* 当字符串明确为常量（例如，字符串字面量，或等效情况），并且该字符串肯定会在 cJSON 对象的生命周期之后仍然存在时，使用此函数。
警告：使用此函数时，在向 item->string 写入数据之前，务必检查 (item->type & cJSON_StringIsConst) 是否为零。 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);
/* 将对一个项的引用追加到指定的数组或对象中。当你想把一个现有的 cJSON 对象添加到一个新的 cJSON 对象中，同时又不想破坏原有的 cJSON 对象时，使用此函数。 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);

/* 从数组或对象中移除 / 分离项。 */
CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);

/* 更新数组中的元素。 */
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem); /* Shifts pre-existing items to the right. */
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object,const char *string,cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object,const char *string,cJSON *newitem);

/* 将已有的元素向右移动。 */
CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
/* “Duplicate” 函数将创建一个新的、与你传入的 cJSON 项完全相同的项，它位于新的内存中，后续需要释放该内存。当 “recurse” 不等于 0 时，它将复制与该项相关联的所有子项。从 “Duplicate” 函数返回时，项的 “item->next” 和 “item->prev” 指针始终为 0。 /
/ 递归地比较两个 cJSON 项是否相等。如果 “a” 或 “b” 为 NULL 或者无效，那么它们将被视为不相等。“case_sensitive” 决定在处理对象键时是区分大小写（值为 1）还是不区分大小写（值为 0）。 */
CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);

/* 压缩字符串，从字符串中移除空白字符（如空格 ' '、制表符 '\t'、回车符 '\r'、换行符 '\n'）。
输入指针 json 不能指向只读地址区域，例如字符串常量，而应指向可读写的地址区域。 */
CJSON_PUBLIC(void) cJSON_Minify(char *json);

/* 用于同时创建项并将其添加到对象中的辅助函数。
这些函数会返回添加的项，若操作失败则返回 NULL。 */
CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean);
CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number);
CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
CJSON_PUBLIC(cJSON*) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw);
CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name);

/* 当为对象赋予整数值时，该值也需要同步更新到 valuedouble 中。 */
#define cJSON_SetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuedouble = (number) : (number))
/* cJSON_SetNumberValue 宏的辅助函数 */
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number);
#define cJSON_SetNumberValue(object, number) ((object != NULL) ? cJSON_SetNumberHelper(object, (double)number) : (number))
/* 更改 cJSON_String 对象的 valuestring，此操作仅在对象类型为 cJSON_String 时生效。 */
CJSON_PUBLIC(char*) cJSON_SetValuestring(cJSON *object, const char *valuestring);

/* 如果该对象不是布尔类型，此操作不会产生任何效果并返回 cJSON_Invalid；否则，它将返回新的类型。 */
#define cJSON_SetBoolValue(object, boolValue) ( \
    (object != NULL && ((object)->type & (cJSON_False|cJSON_True))) ? \
    (object)->type=((object)->type &(~(cJSON_False|cJSON_True)))|((boolValue)?cJSON_True:cJSON_False) : \
    cJSON_Invalid\
)

/* 用于遍历数组或对象的宏 */
#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* 使用通过 cJSON_InitHooks 设置的 malloc/free 函数来进行对象的内存分配和释放操作。 */
CJSON_PUBLIC(void *) cJSON_malloc(size_t size);
CJSON_PUBLIC(void) cJSON_free(void *object);

#ifdef __cplusplus
}
#endif

#endif
