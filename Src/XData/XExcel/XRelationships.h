/******************************************************************************
 * @file       XRelationships.h
 * @brief      XRelationships OOXML 关系类（对标 QXlsx::Relationships）
 * @author     XinYueC 团队
 * @note       管理 OOXML 包内部文件之间的关系，包括文档关系、包关系等。
 *             对齐 QXlsx::Relationships 全部功能
 ******************************************************************************/
#ifndef XRELATIONSHIPS_H
#define XRELATIONSHIPS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XStringList.h"
#include "XVector.h"

/**
 * @brief      OOXML 关系结构体
 * @note       包含关系 ID、类型、目标和目标模式。
 */
typedef struct XlsxRelationship
{
    XString* m_id;          /**< 关系 ID（如 "rId1"） */
    XString* m_type;        /**< 关系类型（如 "http://..."） */
    XString* m_target;      /**< 关系目标路径 */
    XString* m_targetMode;  /**< 目标模式（"Internal" 或 "External"） */
} XlsxRelationship;

/**
 * @brief      XRelationships 关系集合类
 * @note       管理 OOXML 文件之间的关系列表。对齐 QXlsx::Relationships 全部功能。
 */
typedef struct XRelationships
{
    XVector* m_relationships;  /**< 关系列表（XlsxRelationship 数组） */
} XRelationships;

/**
 * @brief      创建一个空的 XRelationships 对象
 * @return     指向新创建的 XRelationships 的指针，失败返回 NULL
 */
XRelationships* XRelationships_create(void);

/**
 * @brief      在堆上删除 XRelationships 实例
 * @param self 待删除的指针
 */
void XRelationships_delete(XRelationships* self);

/**
 * @brief      添加文档关系
 * @param self         指针
 * @param relativeType 关系类型
 * @param target       目标路径
 */
void XRelationships_addDocumentRelationship(XRelationships* self, const char* relativeType, const char* target);

/**
 * @brief      添加包关系
 * @param self         指针
 * @param relativeType 关系类型
 * @param target       目标路径
 */
void XRelationships_addPackageRelationship(XRelationships* self, const char* relativeType, const char* target);

/**
 * @brief      添加工作表关系
 * @param self         指针
 * @param relativeType 关系类型
 * @param target       目标路径
 * @param targetMode   目标模式
 */
void XRelationships_addWorksheetRelationship(XRelationships* self, const char* relativeType, const char* target, const char* targetMode);

/**
 * @brief      通过 ID 获取关系
 * @param self 指针
 * @param id   关系 ID
 * @return     指向 XlsxRelationship 的指针，未找到返回 NULL
 */
XlsxRelationship* XRelationships_getRelationshipById(const XRelationships* self, const char* id);

/**
 * @brief      清空所有关系
 * @param self 指针
 */
void XRelationships_clear(XRelationships* self);

/**
 * @brief      获取关系数量
 * @param self 指针
 * @return     关系数量
 */
int XRelationships_count(const XRelationships* self);

/**
 * @brief      判断是否为空
 * @param self 指针
 * @return     为空返回 true
 */
bool XRelationships_isEmpty(const XRelationships* self);

/**
 * @brief      将关系保存到 XML 文件
 * @param self 指针
 * @param device 输出设备（文件路径）
 * @return     成功返回 true
 */
bool XRelationships_saveToXmlFile(const XRelationships* self, const char* filePath);

/**
 * @brief      从 XML 文件加载关系
 * @param self     指针
 * @param filePath 文件路径
 * @return     成功返回 true
 */
bool XRelationships_loadFromXmlFile(XRelationships* self, const char* filePath);

/* ========== 按类型查询关系 ========== */

/**
 * @brief      获取指定类型的文档关系列表
 * @param self         指针
 * @param relativeType 关系类型
 * @param outCount     输出数量
 * @return     关系数组（调用者不需释放，内部持有）
 */
XlsxRelationship** XRelationships_documentRelationships(const XRelationships* self, const char* relativeType, int* outCount);

/**
 * @brief      获取指定类型的包关系列表
 */
XlsxRelationship** XRelationships_packageRelationships(const XRelationships* self, const char* relativeType, int* outCount);

/**
 * @brief      获取指定类型的 MS 包关系列表
 */
XlsxRelationship** XRelationships_msPackageRelationships(const XRelationships* self, const char* relativeType, int* outCount);

/**
 * @brief      获取指定类型的工作表关系列表
 */
XlsxRelationship** XRelationships_worksheetRelationships(const XRelationships* self, const char* relativeType, int* outCount);

/**
 * @brief      添加 MS 包关系
 * @param self         指针
 * @param relativeType 关系类型
 * @param target       目标路径
 */
void XRelationships_addMsPackageRelationship(XRelationships* self, const char* relativeType, const char* target);

/* ========== XML 数据读写 ========== */

/**
 * @brief      将关系保存为 XML 数据
 * @param self    指针
 * @param outData 输出数据
 * @param outLen  输出长度
 * @return     成功返回 true
 */
bool XRelationships_saveToXmlData(const XRelationships* self, uint8_t** outData, size_t* outLen);

/**
 * @brief      从 XML 数据加载关系
 * @param self 指针
 * @param data XML 数据
 * @param len  数据长度
 * @return     成功返回 true
 */
bool XRelationships_loadFromXmlData(XRelationships* self, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* XRELATIONSHIPS_H */
