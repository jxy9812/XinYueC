/**
 * @file       XSettingsNative.h
 * @brief      XSettings 原生设置存储（Windows 注册表）平台后端契约。
 * @details    对标 Qt 6.8.3 qtbase/src/corelib/io/qsettings_win.cpp 所依赖的
 *             注册表原语集合：键的打开/创建/关闭、五类值的读写与删除、子键与
 *             子值枚举、键树删除、刷盘。本文件只声明能力与所有权契约，不含
 *             任何平台头文件，Windows 实现在 Drive/windows/Core/
 *             XSettingsNative_win32.c，其余平台由
 *             Drive/Unsupported/Core/XSettingsNative_unsupported.c 提供存根。
 *
 *             与 Qt 的分层对应关系：
 *             - Qt 的 QWinSettingsPrivate 同时承担「注册表原语」与「QSettings
 *               语义（回退链、OrganizationDefaults、QVariant ↔ REG_* 映射、
 *               fileName 拼装）」。本实现把前者下沉到平台后端（本文件），
 *               后者留在 XSettings.c——因为 QVariant ↔ REG_* 的映射依赖
 *               XVariant 的 variantToString，属于对象层知识。
 *             - Qt 在非 Windows 平台上 NativeFormat 就是 INI 文件，因此本
 *               契约只被 Windows 构建引用；其他平台的存根仅为保持可链接。
 *
 *             所有权与生命周期：
 *             - 键句柄 XSettingsNativeKey 由 openKey 产出，必须用 closeKey
 *               释放；句柄不可跨线程共享。
 *             - 读取结果 XSettingsNativeValue 内部指针由后端分配，必须用
 *               valueFree 释放；调用方不得自行 free。
 *             - 所有字符串参数均为 UTF-16（uint16_t，与 XChar 同宽），
 *               以 NUL 结尾；实现不得修改调用方传入的缓冲区。
 * @note       仅供 XSettings 对象层与 Drive 平台文件包含，上层业务与 Shell
 *             禁止直接调用。
 * @author     XinYueC 团队
 */
#ifndef XSETTINGS_NATIVE_H
#define XSETTINGS_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if XSETTINGS_ON

/* ==================== 根键 ==================== */

/**
 * @brief 注册表根键（对标 qsettings_win.cpp 中使用的 HKEY_* 常量集合）。
 * @details 取值顺序即 Windows 后端的判定顺序；其它平台后端可忽略。
 */
typedef enum XSettingsNativeRoot
{
    XSettingsNativeRoot_CurrentUser  = 0,   /**< HKEY_CURRENT_USER（对标 HKEY_CURRENT_USER）。 */
    XSettingsNativeRoot_LocalMachine = 1,   /**< HKEY_LOCAL_MACHINE（对标 HKEY_LOCAL_MACHINE）。 */
    XSettingsNativeRoot_ClassesRoot  = 2,   /**< HKEY_CLASSES_ROOT（对标 HKEY_CLASSES_ROOT）。 */
    XSettingsNativeRoot_Users        = 3,   /**< HKEY_USERS（对标 HKEY_USERS）。 */
    XSettingsNativeRoot_Count        = 4    /**< 根键个数（非有效根键）。 */
} XSettingsNativeRoot;

/**
 * @brief 注册表视图（对标 KEY_WOW64_32KEY / KEY_WOW64_64KEY）。
 * @details Default 表示跟随进程位数（不附加任何 WOW64 标志），对应 Qt 的
 *          NativeFormat；32 对应 Registry32Format，64 对应 Registry64Format。
 */
typedef enum XSettingsNativeView
{
    XSettingsNativeView_Default = 0,   /**< 跟随进程默认视图（对标 NativeFormat）。 */
    XSettingsNativeView_32      = 1,   /**< 强制 32 位视图（对标 KEY_WOW64_32KEY）。 */
    XSettingsNativeView_64      = 2    /**< 强制 64 位视图（对标 KEY_WOW64_64KEY）。 */
} XSettingsNativeView;

/* ==================== 值种类 ==================== */

/**
 * @brief 读取结果的注册表值种类（对标 qsettings_win.cpp readKey 的 switch）。
 * @details 写入侧由调用方按 XVariant 类型选择；读取侧由后端按实际 REG_*
 *          类型回报，调用方据此还原 XVariant。
 */
typedef enum XSettingsNativeValueKind
{
    XSettingsNativeValueKind_None = 0,        /**< 值不存在或读取失败。 */
    XSettingsNativeValueKind_String = 1,      /**< REG_SZ / REG_EXPAND_SZ（对标 Qt 两分支合并）。 */
    XSettingsNativeValueKind_StringList = 2,  /**< REG_MULTI_SZ。 */
    XSettingsNativeValueKind_Int32 = 3,       /**< REG_DWORD / REG_DWORD_BIG_ENDIAN。 */
    XSettingsNativeValueKind_Int64 = 4,       /**< REG_QWORD。 */
    XSettingsNativeValueKind_Binary = 5,      /**< REG_BINARY / REG_NONE（按 UTF-16 文本承载）。 */
    XSettingsNativeValueKind_Unsupported = 6  /**< 未知类型（对标 Qt 的 qWarning + nullopt）。 */
} XSettingsNativeValueKind;

/**
 * @brief 不透明键句柄（Windows 后端为 HKEY，其它平台可自定义）。
 */
typedef void* XSettingsNativeKey;

/**
 * @brief 读取结果（所有权归调用方，用 XSettingsNative_valueFree 释放）。
 * @details text 与 bytes 至多一个非 NULL，取决于 kind：
 *          - String / Binary：text 为 NUL 结尾的 UTF-16 序列；
 *          - StringList：text 为双 NUL 结尾的 UTF-16 序列（REQ_MULTI_SZ 布局）；
 *          - Binary：bytes 有效，text 为 NULL。
 */
typedef struct XSettingsNativeValue
{
    XSettingsNativeValueKind kind;   /**< 值种类；None/Unsupported 时其余字段无意义。 */
    int32_t int32Value;              /**< kind = Int32 时的值。 */
    int64_t int64Value;              /**< kind = Int64 时的值。 */
    uint16_t* text;                  /**< String/StringList 的 UTF-16 承载；可为 NULL。 */
    size_t textUnits;                /**< text 的 UTF-16 码元数（不含结尾 NUL）。 */
    uint8_t* bytes;                  /**< Binary 的原始字节；可为 NULL。 */
    size_t byteSize;                 /**< bytes 的有效字节数。 */
} XSettingsNativeValue;

/**
 * @brief 子键/子值枚举回调。
 * @param userData 调用方传入的上下文指针。
 * @param name 子项名字（UTF-16，NUL 结尾；借用，回调返回后失效）。
 * @return true 继续枚举；false 提前终止（不视为错误）。
 */
typedef bool (*XSettingsNativeEnumFunc)(void* userData, const uint16_t* name);

/**
 * @brief 全键递归枚举回调（对标 qsettings_win.cpp allKeys 的 NameSet 收集）。
 * @param userData 调用方传入的上下文指针。
 * @param relPath 相对起始键的完整路径（UTF-16，NUL 结尾，'\\' 分隔；借用）。
 * @return true 继续枚举；false 提前终止（不视为错误）。
 */
typedef bool (*XSettingsNativePathEnumFunc)(void* userData, const uint16_t* relPath);

/* ==================== 键生命周期 ==================== */

/**
 * @brief 打开（必要时创建）注册表键（对标 openKey / createOrOpenKey）。
 * @param root 根键。
 * @param view WOW64 视图。
 * @param subKey 相对根键的子键路径（UTF-16，以 '\\' 分隔）；可为 NULL/空（根键本身）。
 * @param write true 表示需要写权限；后端应先尝试读写权限打开，失败后回落只读。
 * @param createIfMissing true 时键不存在则创建。
 * @param outKey 输出键句柄；失败时置 NULL。
 * @param outReadOnly 输出：句柄是否只读（requested write 但只拿到读权限）。
 * @return 成功返回 true；失败返回 false（对应 Qt 返回 0 句柄）。
 * @note write=true 且 createIfMissing=false 时，键不存在应返回 false。
 */
bool XSettingsNative_openKey(XSettingsNativeRoot root, XSettingsNativeView view,
                             const uint16_t* subKey, bool write, bool createIfMissing,
                             XSettingsNativeKey* outKey, bool* outReadOnly);

/**
 * @brief 关闭键句柄；key 为 NULL 时空操作。
 */
void XSettingsNative_closeKey(XSettingsNativeKey key);

/**
 * @brief 判断键句柄是否有效且可写（对标 QWinSettingsPrivate::writeHandle）。
 * @return 句柄非空且非只读返回 true。
 */
bool XSettingsNative_isWritable(XSettingsNativeKey key);

/**
 * @brief 根键的短名（ASCII），用于 fileName() 拼装。
 * @return 形如 "HKEY_CURRENT_USER" 的静态字符串；非法枚举返回 NULL。
 */
const char* XSettingsNative_rootName(XSettingsNativeRoot root);

/* ==================== 读取 ==================== */

/**
 * @brief 读取值（对标 QWinSettingsPrivate::readKey）。
 * @param key 已打开的键句柄。
 * @param name 值名（UTF-16）；NULL 或空串表示默认值（对标 Qt 把 "Default"/"." 映射成空名）。
 * @param out 输出结果；调用方用 XSettingsNative_valueFree 释放。
 * @return 读到值返回 true；不存在/失败返回 false（out 置 kind=None）。
 */
bool XSettingsNative_readValue(XSettingsNativeKey key, const uint16_t* name,
                               XSettingsNativeValue* out);

/**
 * @brief 释放读取结果内部缓冲；可安全重复调用。
 */
void XSettingsNative_valueFree(XSettingsNativeValue* value);

/* ==================== 写入 ==================== */

/** @brief 写 REG_SZ 字符串值（text 为 UTF-16）。 */
bool XSettingsNative_writeString(XSettingsNativeKey key, const uint16_t* name,
                                 const uint16_t* text);

/**
 * @brief 写 REG_MULTI_SZ 值。
 * @param multiSz 完整 REG_MULTI_SZ 载荷（各元素 NUL 分隔、末尾额外一个 NUL）。
 * @param units multiSz 的有效 UTF-16 码元数（含结尾双 NUL）；&lt; 2 时按空表处理。
 */
bool XSettingsNative_writeStringList(XSettingsNativeKey key, const uint16_t* name,
                                     const uint16_t* multiSz, size_t units);

/** @brief 写 REG_BINARY 值。 */
bool XSettingsNative_writeBinary(XSettingsNativeKey key, const uint16_t* name,
                                 const uint8_t* data, size_t size);

/** @brief 写 REG_DWORD 值。 */
bool XSettingsNative_writeInt32(XSettingsNativeKey key, const uint16_t* name, int32_t value);

/** @brief 写 REG_QWORD 值。 */
bool XSettingsNative_writeInt64(XSettingsNativeKey key, const uint16_t* name, int64_t value);

/* ==================== 删除与枚举 ==================== */

/**
 * @brief 删除单个值（对标 RegDeleteValue）；值不存在视为失败。
 */
bool XSettingsNative_deleteValue(XSettingsNativeKey key, const uint16_t* name);

/**
 * @brief 递归删除子键（含其全部子键与值，对标 deleteChildGroups + RegDeleteKey）。
 * @param parent 父键句柄。
 * @param subKey 待删子键相对路径（UTF-16）；空串表示删除 parent 自身的全部子键
 *        （对标 Qt remove("") 分支）。
 * @return 全部删除成功返回 true。
 */
bool XSettingsNative_deleteSubKeyTree(XSettingsNativeKey parent, const uint16_t* subKey);

/**
 * @brief 枚举直接子键（groups=true）或直接子值名（groups=false）。
 * @param func 回调；收到空名字时后端应回报 "."（对标 Qt 把空项名映射为 "."）。
 * @return 枚举 API 调用成功返回 true；回调提前终止不算失败。
 */
bool XSettingsNative_enumChildren(XSettingsNativeKey key, bool groups,
                                  XSettingsNativeEnumFunc func, void* userData);

/**
 * @brief 递归枚举整棵子树的全部键路径（对标 qsettings_win.cpp allKeys）。
 * @details 从 root/subKey 起向下递归，把每个键的「值名」以相对 baseKey 的
 *          路径形式回调出来，即 Qt allKeys() 语义：叶子值形成
 *          "a/b/leaf"，每个祖先组不单独出现。空值名（默认值）按 "." 回报。
 * @param baseKey 起始键（UTF-16）；NULL/空表示从根键起。
 * @param basePath 输出路径的前缀（UTF-16，可为 NULL）；若给出，回调的 relPath
 *        以它开头，即「相对当前 group」的语义。
 * @param func 回调；relPath 为 NUL 结尾 UTF-16。
 * @return 起始键不存在返回 true（空结果不算错误）。
 */
bool XSettingsNative_collectAllKeys(XSettingsNativeRoot root, XSettingsNativeView view,
                                    const uint16_t* subKey, const uint16_t* basePath,
                                    XSettingsNativePathEnumFunc func, void* userData);

/* ==================== 刷盘 ==================== */

/**
 * @brief 将键的修改刷到磁盘（对标 RegFlushKey）。
 */
void XSettingsNative_flushKey(XSettingsNativeKey key);

#ifdef __cplusplus
}
#endif

#endif /* XSETTINGS_ON */

#endif /* XSETTINGS_NATIVE_H */
