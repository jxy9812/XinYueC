#ifndef XVARIANTTYPEOPS_H
#define XVARIANTTYPEOPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XContainer.h"
#include "XCompare.h"

/**
 * @brief XVariant 支持的数据类型。
 * @details 基础类型由 XVariant 直接处理；Pair 及之后的类型通过
 *          XVariantTypeOps 表提供复制、清理、析构和比较行为。
 */
typedef enum XVariantType
{
	XVariantType_NULL,
	XVariantType_Uint8,
	XVariantType_Uint16,
	XVariantType_Uint32,
	XVariantType_Uint64,
	XVariantType_Int8,
	XVariantType_Int16,
	XVariantType_Int32,
	XVariantType_Int64,
	XVariantType_Bool,
	XVariantType_Char,
	XVariantType_UChar,
	XVariantType_Int,
	XVariantType_Size_t,
	XVariantType_Ptr,
	XVariantType_Float,
	XVariantType_Double,
	XVariantType_Pair,
	XVariantType_Point,
	XVariantType_Date,
	XVariantType_Time,
	XVariantType_DateTime,
	XVariantType_ByteArray,
	XVariantType_String,
	XVariantType_StringList,
	XVariantType_List,
	XVariantType_Map,
	XVariantType_Hash,
	XVariantType_JsonDocument,
	XVariantType_JsonArray,
	XVariantType_JsonObject,
	XVariantType_JsonValue,
	XVariantType_BsonArray,
	XVariantType_BsonDocument,
	XVariantType_BsonValue,
	XVariantType_User,
	XVariantType_ExtensionFirst = XVariantType_Pair,
	XVariantType_ExtensionCount = XVariantType_User - XVariantType_ExtensionFirst
} XVariantType;

/**
 * @brief 扩展 Variant 类型的生命周期和比较操作。
 * @details 每个扩展模块提供一份常量实例；XVariantTypeOps.c 按枚举值顺序
 *          聚合这些实例，XVariant 实例本身不保存函数指针。
 */
typedef struct XVariantTypeOps
{
	size_t dataSize;
	XCDataCopyMethod copy;
	XCDataMoveMethod move;
	XCDataClearMethod clear;
	XCDataDeinitMethod deinit;
	XCompare compare;
	const char* typeName;
} XVariantTypeOps;

/**
 * @brief 定义一个扩展 Variant 类型的操作表。
 * @param Type 类型名，同时生成 Type_variantTypeOps 变量名
 * @param DataSize 类型数据大小；柔性数据类型传 0
 * @param Copy 数据拷贝方法
 * @param Move 数据移动方法
 * @param Clear 数据清除方法
 * @param Deinit 数据析构方法
 * @param Compare 数据比较方法
 * @param TypeName 类型名称
 */
#define XVARIANT_TYPE_OPS_DEFINE(Type, DataSize, Copy, Move, Clear, Deinit, Compare, TypeName) \
	const XVariantTypeOps Type##_variantTypeOps = \
	{ \
		(DataSize), (XCDataCopyMethod)(Copy), (XCDataMoveMethod)(Move), \
		(XCDataClearMethod)(Clear), (XCDataDeinitMethod)(Deinit), \
		(XCompare)(Compare), (TypeName) \
	}

/** @brief 查找扩展类型的只读操作表；基础类型或无效类型返回 NULL。 */
const XVariantTypeOps* XVariantTypeOps_forType(int type);
/** @brief 获取扩展类型名称；基础类型或无效类型返回 NULL。 */
const char* XVariantTypeOps_nameForType(int type);

#ifdef __cplusplus
}
#endif

#endif
