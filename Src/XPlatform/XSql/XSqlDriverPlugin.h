/**
 * @file       XSqlDriverPlugin.h
 * @brief      SQL 驱动插件兼容抽象类，对齐 Qt 6.8 QSqlDriverPlugin。
 * @details    XinYueC 默认不动态加载插件；该类仅保留源码驱动工厂可以复用的
 *             公共抽象，具体驱动建议通过 XSqlDatabase_registerSqlDriver 注册。
 */
#ifndef XSQLDRIVERPLUGIN_H
#define XSQLDRIVERPLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlDriver.h"
#include "XObject.h"

/** @brief XSqlDriverPlugin 的内部工厂虚函数槽位；调用方使用 XSqlDriverPlugin_create_base 分派。 */
XCLASS_DEFINE_BEGING(XSqlDriverPlugin)
XCLASS_DEFINE_ENUM(XSqlDriverPlugin, Create) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XSqlDriverPlugin)

/**
 * @brief SQL 驱动插件工厂抽象对象。
 * @details 对齐 Qt QSqlDriverPlugin；XinYueC 不负责动态加载，插件对象仅作为
 *          源码注册工厂的抽象接口使用。
 */
typedef struct XSqlDriverPlugin {
    XObject m_class; /**< 第一个成员，由 XObject 管理；不可复制。 */
} XSqlDriverPlugin;

/**
 * @brief 初始化驱动插件虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlDriverPlugin_class_init(void);

/**
 * @brief 初始化驱动插件对象。
 * @param plugin 待初始化对象；不能为 NULL。
 * @return 无；插件对象不可复制。
 */
void XSqlDriverPlugin_init(XSqlDriverPlugin* plugin);

/**
 * @brief 创建驱动插件基对象。
 * @return 新插件对象，调用者必须使用 XSqlDriverPlugin_delete_base 释放；失败返回 NULL。
 */
XSqlDriverPlugin* XSqlDriverPlugin_create_ex(XMemoryType memory);
/** @brief 调用 XClass 析构入口释放插件工厂对象。 */
#define XSqlDriverPlugin_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlDriverPlugin_create 返回的插件工厂对象。 */
#define XSqlDriverPlugin_delete_base XClass_delete_base
/**
 * @brief 按驱动名称创建驱动。
 * @param plugin 插件对象；不能为 NULL。
 * @param key 驱动名称；UTF-8 借用字符串，不能为 NULL。
 * @return 新驱动对象，调用者取得所有权并负责释放；未匹配时返回 NULL。
 */
XSqlDriver* XSqlDriverPlugin_create_base(XSqlDriverPlugin* plugin, const char* key);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlDriverPlugin_create
#define XSqlDriverPlugin_create() XSqlDriverPlugin_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XSQLDRIVERPLUGIN_H */
