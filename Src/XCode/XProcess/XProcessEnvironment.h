/**
 * @file XProcessEnvironment.h
 * @brief 进程环境变量对象，对齐 Qt 6.8 QProcessEnvironment 的公开 API。
 * @details
 * XProcessEnvironment 保存传递给子进程的 name=value 集合。默认构造创建
 * 空环境；XProcessEnvironment_initInherit 创建“启动时继承父进程环境”的
 * 标记对象。对象只使用 XString/XStringList 和 XinYueC 内存接口，不修改
 * 当前进程环境。Unix/Windows 环境枚举由 XProcess Drive 后端实现。
 */

#ifndef XPROCESS_ENVIRONMENT_H
#define XPROCESS_ENVIRONMENT_H

#include "XProcessConfig.h"

#if XProcess_ON

#include <stdbool.h>
#include "XString.h"
#include "XStringList.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 进程环境变量集合。
 * @details
 * m_entries 中每个元素均为 UTF-8 的 name=value 字符串；m_inherit 为 true
 * 时 m_entries 可以为空，表示进程启动时由后端重新读取父环境。结构体成员
 * 仅供实现使用，调用方必须通过本文件公共 API 访问。
 */
typedef struct XProcessEnvironment {
    XStringList* m_entries; /**< 对象拥有的 name=value 列表；NULL 表示分配失败。 */
    bool m_inherit;         /**< 是否在子进程启动时继承父进程环境。 */
} XProcessEnvironment;

/**
 * @brief 初始化空环境对象。
 * @param self 栈上环境对象；不能为空，调用方随后负责 deinit。
 */
void XProcessEnvironment_init(XProcessEnvironment* self);

/**
 * @brief 初始化继承父进程环境的对象。
 * @param self 栈上环境对象；不能为空，不会立即复制父环境。
 */
void XProcessEnvironment_initInherit(XProcessEnvironment* self);

/**
 * @brief 创建空环境对象。
 * @return 新对象；失败返回 NULL，调用方必须用 XProcessEnvironment_delete 释放。
 */
XProcessEnvironment* XProcessEnvironment_create(void);

/**
 * @brief 创建继承父环境的对象。
 * @return 新对象；失败返回 NULL，调用方必须用 XProcessEnvironment_delete 释放。
 */
XProcessEnvironment* XProcessEnvironment_createInherit(void);

/**
 * @brief 深拷贝环境对象。
 * @param other 源环境；借用，不能为 NULL。
 * @return 新环境对象；失败返回 NULL。
 */
XProcessEnvironment* XProcessEnvironment_createCopy(const XProcessEnvironment* other);

/**
 * @brief 释放环境对象内部资源但保留对象存储。
 * @param self 环境对象；可为 NULL。
 */
void XProcessEnvironment_deinit(XProcessEnvironment* self);

/**
 * @brief 释放堆分配环境对象。
 * @param self 由 create 系列返回的对象；可为 NULL。
 */
void XProcessEnvironment_delete(XProcessEnvironment* self);

/**
 * @brief 判断环境是否没有显式变量。
 * @param self 环境对象；可为 NULL。
 * @return 空环境或继承标记环境返回 true；否则返回 false。
 */
bool XProcessEnvironment_isEmpty(const XProcessEnvironment* self);

/**
 * @brief 判断对象是否表示启动时继承父进程环境。
 * @param self 环境对象；可为 NULL。
 * @return 继承标记为 true；显式环境为 false。
 */
bool XProcessEnvironment_inheritsFromParent(const XProcessEnvironment* self);

/**
 * @brief 清除所有显式变量。
 * @param self 环境对象；不能为空，继承标记保持不变。
 */
void XProcessEnvironment_clear(XProcessEnvironment* self);

/**
 * @brief 查询 UTF-8 名称是否存在。
 * @param self 环境对象；不能为空。
 * @param name 变量名；借用，不能包含等号。
 * @return 存在返回 true；继承标记环境始终返回 false。
 */
bool XProcessEnvironment_contains_utf8(const XProcessEnvironment* self, const char* name);

/**
 * @brief 查询 XString 名称是否存在。
 * @param self 环境对象；不能为空。
 * @param name 变量名对象；调用期间借用，按 UTF-8 转换后匹配。
 * @return 存在返回 true；参数非法或继承标记环境返回 false。
 */
bool XProcessEnvironment_contains(const XProcessEnvironment* self, const XString* name);

/**
 * @brief 插入或替换 UTF-8 环境变量。
 * @param name 变量名；借用，非 NULL、非空且不得包含等号。
 * @param value 变量值；借用，NULL 按空字符串处理，不取得所有权。
 * @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XProcessEnvironment_insert_utf8(XProcessEnvironment* self,
                                     const char* name, const char* value);

/**
 * @brief 插入或替换 XString 环境变量。
 * @param self 环境对象；不能为空。
 * @param name 变量名对象；调用期间借用。
 * @param value 变量值对象；调用期间借用，可为 NULL。
 * @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XProcessEnvironment_insert(XProcessEnvironment* self,
                                const XString* name, const XString* value);

/**
 * @brief 删除 UTF-8 环境变量。
 * @param self 环境对象；不能为空。
 * @param name 变量名；调用期间借用。
 * @return 找到并删除返回 true；变量不存在或参数非法返回 false。
 */
bool XProcessEnvironment_remove_utf8(XProcessEnvironment* self, const char* name);

/**
 * @brief 删除 XString 名称对应的变量。
 * @param self 环境对象；不能为空。
 * @param name 变量名对象；调用期间借用。
 * @return 找到并删除返回 true，否则返回 false。
 */
bool XProcessEnvironment_remove(XProcessEnvironment* self, const XString* name);

/**
 * @brief 查询 UTF-8 变量值。
 * @param defaultValue 未找到时复制返回的默认值；可为 NULL，按空字符串处理。
 * @return 新建 XString；调用方必须使用 XString_delete_base 释放。
 */
XString* XProcessEnvironment_value_utf8(const XProcessEnvironment* self,
                                         const char* name,
                                         const char* defaultValue);

/**
 * @brief 查询 XString 名称对应的变量值。
 * @param self 环境对象；不能为空。
 * @param name 变量名对象；调用期间借用。
 * @param defaultValue 未找到时使用的默认值对象，可为 NULL。
 * @return 新 XString；调用方负责释放，失败返回 NULL。
 */
XString* XProcessEnvironment_value(const XProcessEnvironment* self,
                                   const XString* name,
                                   const XString* defaultValue);

/**
 * @brief 返回显式环境的 name=value 列表副本。
 * @param self 环境对象；不能为空。
 * @return 新列表；继承标记环境返回空列表；调用方必须删除返回对象。
 */
XStringList* XProcessEnvironment_toStringList(const XProcessEnvironment* self);

/**
 * @brief 返回显式环境的变量名列表副本。
 * @param self 环境对象；不能为空。
 * @return 新列表；继承标记环境返回空列表；调用方必须删除返回对象。
 */
XStringList* XProcessEnvironment_keys(const XProcessEnvironment* self);

/**
 * @brief 将源环境中的变量合并到目标环境。
 * @details 源对象为继承标记时不修改目标；同名变量由源对象覆盖。
 * @param self 目标环境；不能为空。
 * @param other 源环境；调用期间借用，不能为空。
 * @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XProcessEnvironment_insertEnvironment(XProcessEnvironment* self,
                                            const XProcessEnvironment* other);

/**
 * @brief 读取当前系统环境。
 * @return 新建的显式环境；失败返回 NULL。返回对象与父环境建立时刻无关。
 */
XProcessEnvironment* XProcessEnvironment_systemEnvironment(void);

#ifdef __cplusplus
}
#endif

#endif /* XProcess_ON */
#endif /* XPROCESS_ENVIRONMENT_H */
