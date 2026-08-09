/******************************************************************************
 * @file       XAbstractOOXmlFile.h
 * @brief      XAbstractOOXmlFile OOXML 文件抽象基类（对标 QXlsx::AbstractOOXmlFile）
 * @author     XinYueC 团队
 * @note       提供 OOXML 文件的抽象基类，定义保存和加载接口。
 *             对齐 QXlsx::AbstractOOXmlFile 全部功能
 ******************************************************************************/
#ifndef XABSTRACTOOXMLFILE_H
#define XABSTRACTOOXMLFILE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XRelationships.h"

/* 前向声明 */
typedef struct XAbstractOOXmlFile XAbstractOOXmlFile;

/**
 * @brief      创建标志枚举
 */
typedef enum XAbstractOOXmlFile_CreateFlag
{
    XAbstractOOXmlFile_F_NewFromScratch = 0,  /**< 从头新建 */
    XAbstractOOXmlFile_F_LoadFromExists = 1   /**< 从已有文件加载 */
} XAbstractOOXmlFile_CreateFlag;

/**
 * @brief      OOXML 文件虚函数表
 */
typedef struct XAbstractOOXmlFile_Vtable
{
    bool (*saveToXmlFile)(XAbstractOOXmlFile* self, const XString* filePath);
    bool (*loadFromXmlFile)(XAbstractOOXmlFile* self, const XString* filePath);
    void (*delete)(XAbstractOOXmlFile* self);
} XAbstractOOXmlFile_Vtable;

/**
 * @brief      XAbstractOOXmlFile 抽象基类
 * @note       所有 OOXML 子文件的基类，定义了保存/加载的虚函数接口。
 *             对齐 QXlsx::AbstractOOXmlFile 全部功能。
 */
typedef struct XAbstractOOXmlFile
{
    XAbstractOOXmlFile_CreateFlag m_createFlag;  /**< 创建标志 */
    XString* m_filePath;                          /**< 文件路径 */
    XRelationships* m_relationships;              /**< 关系列表 */
    XAbstractOOXmlFile_Vtable* m_vtable;          /**< 虚函数表 */
} XAbstractOOXmlFile;

/**
 * @brief      初始化 XAbstractOOXmlFile 基类
 * @param self 待初始化的指针
 * @param flag 创建标志
 */
void XAbstractOOXmlFile_init(XAbstractOOXmlFile* self, XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief      释放资源
 * @param self 指针
 */
void XAbstractOOXmlFile_deinit(XAbstractOOXmlFile* self);

/**
 * @brief      获取关系列表
 * @param self 指针
 * @return     指向 XRelationships 的指针
 */
XRelationships* XAbstractOOXmlFile_relationships(const XAbstractOOXmlFile* self);

/**
 * @brief      设置文件路径
 * @param self 指针
 * @param path 文件路径
 */
void XAbstractOOXmlFile_setFilePath(XAbstractOOXmlFile* self, const XString* path);

/**
 * @brief      获取文件路径
 * @param self 指针
 * @return     文件路径字符串
 */
const XString* XAbstractOOXmlFile_filePath(const XAbstractOOXmlFile* self);

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTOOXMLFILE_H */
