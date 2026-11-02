/**
 * @file       XSettings.h
 * @brief      XSettings 持久化键值设置类公开 API（对标 Qt 6.8 QSettings）。
 * @details    XSettings 继承 XObject，全量对齐 Qt 6.8 QSettings 的公开 API
 *             名字、行为语义与继承关系（QSettings : QObject → XSettings :
 *             XObject）；INI 格式的键/值转义、@Rect/@Size/@Point/@DateTime/
 *             @ByteArray/@String/@Invalid 类型前缀、[组/子组/键] 节语法、
 *             [General]/[%General] 特殊节、UTF-8 编码（读跳过 BOM、写无
 *             BOM）等行为逐条对齐 qtbase/src/corelib/io/qsettings.cpp。
 *             写盘走 XSaveFile（临时文件 + 原子重命名），读盘走 XFile，
 *             字符串/数值转换走 XString/XChar，内存走 XMemory——实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 *
 *             与 Qt 的裁剪边界（有意偏离项，均与既有对齐类同口径记录）：
 *             - NativeFormat 在 Qt 桌面绑定平台原生存储（Windows 注册表/
 *               macOS plist）；本实现统一退化为 INI 文件后端（Linux 上
 *               Qt 的 NativeFormat 本就是 INI 文件），跨平台行为一致；
 *             - Registry32Format/Registry64Format 枚举值保留对齐，但本层
 *               禁用平台 API、无注册表设备后端，使用时置
 *               XSettings_AccessError 且读写均为空，不产生文件；
 *             - Qt 经 QFileSystemWatcher 监听外部修改后重载（QSettings::
 *               event 处理 QDynamicFileEvent）；本实现文件仅在首次访问与
 *               sync() 时读写，不监听外部变更，也无对应 event 分支；
 *             - @Variant(...) 前缀在 Qt 走 QDataStream 二进制（绑定
 *               Qt_5_6 流版本）；本实现用内部序列化承载 XVariant 全类型
 *               往返，字节布局不与 Qt 二进制兼容（Qt 该前缀自身亦跨流
 *               版本不兼容），@DateTime 同理以内部序列化承载；
 *             - Qt 禁止拷贝 QSettings（Q_DISABLE_COPY）；本类按 XClass
 *               对象模型提供 copy/move 槽位（深拷贝产生独立实例，含文件
 *               路径与全部键值），供容器与测试体系使用；
 *             - setIniCodec 已在 Qt 6 移除，不提供；WASM 专属的
 *               WebLocalStorageFormat/WebIndexedDBFormat 不存在。
 * @note       模块总开关 XSETTINGS_ON 定义于本头文件（兜底默认值，先于
 *             包含依赖生效，对齐 XAction.h 口径）；关闭时裁剪本头文件
 *             全部公共声明。对象字段由 XClass 生命周期管理；Qt 保证
 *             QSettings 对象线程安全（内部互斥），本实现按裁剪决策不加
 *             内部锁，多线程访问由调用方自行串行化。
 * @author     XinYueC 团队
 */
#ifndef XSETTINGS_H
#define XSETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 模块总开关：兜底默认值写在本头文件（对齐 XAction.h 口径），保证开关
 * 先于包含依赖生效；产品可在强制包含的产品配置里预先 #define 覆盖。 */
#ifndef XSETTINGS_ON
#define XSETTINGS_ON 1
#endif

#if XSETTINGS_ON

#include <stdbool.h>
#include <stdint.h>

#include "CXinYueConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XStringList.h"
#include "XVariant.h"
#include "XMap.h"
#include "XIODevice.h"

/* ==================== 枚举（对标 Qt 6.8 QSettings） ==================== */

/**
 * @brief      设置读写状态（对标 Qt 6.8 QSettings::Status，取值一致）。
 * @details    status() 返回的首操作后的错误状态：NoError 正常；AccessError
 *             文件不可读/不可写（含注册表等无后端格式）；FormatError INI
 *             内容解析失败（能读多少读多少，不中断）。
 */
typedef enum XSettings_Status
{
    XSettings_NoError     = 0,   /**< 无错误（对标 QSettings::NoError）。 */
    XSettings_AccessError = 1,   /**< 访问错误：文件不可读/不可写（对标 QSettings::AccessError）。 */
    XSettings_FormatError = 2    /**< 格式错误：INI 内容解析失败（对标 QSettings::FormatError）。 */
} XSettings_Status;

/**
 * @brief      存储格式（对标 Qt 6.8 QSettings::Format，取值一致）。
 * @details    NativeFormat 本实现退化为 INI 文件后端（见文件头 @details
 *             裁剪边界）；Registry32/64Format 无后端，操作置 AccessError；
 *             CustomFormat1..16 为 registerFormat 注册的自定义格式槽位，
 *             编号与 Qt 一致（17..32），按注册顺序占用。
 */
typedef enum XSettings_Format
{
    XSettings_NativeFormat    = 0,   /**< 原生格式（本实现 = INI 文件；对标 QSettings::NativeFormat）。 */
    XSettings_IniFormat       = 1,   /**< INI 文本格式（对标 QSettings::IniFormat）。 */
    XSettings_Registry32Format = 2,  /**< 32 位注册表格式（仅保留枚举值；无后端；对标 QSettings::Registry32Format）。 */
    XSettings_Registry64Format = 3,  /**< 64 位注册表格式（仅保留枚举值；无后端；对标 QSettings::Registry64Format）。 */
    XSettings_InvalidFormat   = 16,  /**< 无效格式（对标 QSettings::InvalidFormat）。 */
    XSettings_CustomFormat1   = 17,  /**< 自定义格式 1（对标 QSettings::CustomFormat1）。 */
    XSettings_CustomFormat2   = 18,  /**< 自定义格式 2（对标 QSettings::CustomFormat2）。 */
    XSettings_CustomFormat3   = 19,  /**< 自定义格式 3（对标 QSettings::CustomFormat3）。 */
    XSettings_CustomFormat4   = 20,  /**< 自定义格式 4（对标 QSettings::CustomFormat4）。 */
    XSettings_CustomFormat5   = 21,  /**< 自定义格式 5（对标 QSettings::CustomFormat5）。 */
    XSettings_CustomFormat6   = 22,  /**< 自定义格式 6（对标 QSettings::CustomFormat6）。 */
    XSettings_CustomFormat7   = 23,  /**< 自定义格式 7（对标 QSettings::CustomFormat7）。 */
    XSettings_CustomFormat8   = 24,  /**< 自定义格式 8（对标 QSettings::CustomFormat8）。 */
    XSettings_CustomFormat9   = 25,  /**< 自定义格式 9（对标 QSettings::CustomFormat9）。 */
    XSettings_CustomFormat10  = 26,  /**< 自定义格式 10（对标 QSettings::CustomFormat10）。 */
    XSettings_CustomFormat11  = 27,  /**< 自定义格式 11（对标 QSettings::CustomFormat11）。 */
    XSettings_CustomFormat12  = 28,  /**< 自定义格式 12（对标 QSettings::CustomFormat12）。 */
    XSettings_CustomFormat13  = 29,  /**< 自定义格式 13（对标 QSettings::CustomFormat13）。 */
    XSettings_CustomFormat14  = 30,  /**< 自定义格式 14（对标 QSettings::CustomFormat14）。 */
    XSettings_CustomFormat15  = 31,  /**< 自定义格式 15（对标 QSettings::CustomFormat15）。 */
    XSettings_CustomFormat16  = 32   /**< 自定义格式 16（对标 QSettings::CustomFormat16）。 */
} XSettings_Format;

/**
 * @brief      设置作用域（对标 Qt 6.8 QSettings::Scope，取值一致）。
 * @details    UserScope 存用户级目录，SystemScope 存系统级目录；具体目录
 *             解析与 Qt 各平台口径一致（见 XSettings.c 的路径派生说明）。
 */
typedef enum XSettings_Scope
{
    XSettings_UserScope   = 0,   /**< 用户作用域（对标 QSettings::UserScope）。 */
    XSettings_SystemScope = 1    /**< 系统作用域（对标 QSettings::SystemScope）。 */
} XSettings_Scope;

/**
 * @brief      自定义格式扩展名匹配的大小写敏感性
 *              （对标 Qt::CaseSensitivity，取值一致）。
 */
typedef enum XSettings_CaseSensitivity
{
    XSettings_CaseInsensitive = 0,   /**< 不区分大小写（对标 Qt::CaseInsensitive）。 */
    XSettings_CaseSensitive   = 1    /**< 区分大小写（对标 Qt::CaseSensitive）。 */
} XSettings_CaseSensitivity;

/* ==================== 自定义格式回调（对标 QSettings::ReadFunc/WriteFunc） ==================== */

/**
 * @brief 自定义格式的键值映射表（对标 QSettings::SettingsMap =
 *        QMap<QString, QVariant>）。
 * @details 即 XMap_create_XVariantMap() 创建的按值映射：键槽为 XString
 *          对象、值槽为 XVariant 对象，insert 时按值深拷贝入表、整表释放
 *          时自动析构。回调读写契约（值语义）：构造局部对象 →
 *          XMapBase_insert_base(map, &key, &value) → XClassDeinit 局部
 *          对象；遍历用 XMap_keys_base/XMapBase_values_base 取槽借用
 *          （(XString*)XVector_at_base，禁止解引用、禁止删除槽对象）。
 */
typedef struct XMap XSettings_SettingsMap;

/**
 * @brief      自定义格式读取回调（对标 QSettings::ReadFunc）。
 * @param      device 已打开的输入设备（借用，不关闭；读 API 走
 *             XIODevice_read_1/readAll_1 等基类接口）。
 * @param      map 输出映射表（已构造；成功时回调向其写入全部键值，键值
 *             所有权移交映射表）。
 * @return     true 读取成功；false 失败（status 置 XSettings_AccessError）。
 */
typedef bool (*XSettings_ReadFunc)(XIODevice* device, XSettings_SettingsMap* map);

/**
 * @brief      自定义格式写出回调（对标 QSettings::WriteFunc）。
 * @param      device 已打开的输出设备（借用，不关闭）。
 * @param      map 待写出映射表（借用；回调不得释放其元素）。
 * @return     true 写出成功；false 失败（status 置 XSettings_AccessError）。
 */
typedef bool (*XSettings_WriteFunc)(XIODevice* device, const XSettings_SettingsMap* map);

/* ==================== 类虚函数表 ==================== */

/**
 * @brief      XSettings 类虚函数表。
 * @details    XSettings 不新增虚函数槽位，直接继承 XObject 的事件槽位，
 *             并重载 XClass 的 Copy/Move/Deinit 以管理私有数据的深拷贝、
 *             转移与落盘释放（deinit 先 sync 再释放，对齐 QSettings 析构
 *             语义）。
 */
XCLASS_DEFINE_BEGING(XSettings)
XCLASS_DEFINE_EXTEND_END(XSettings, XObject)

/* ==================== 对象定义 ==================== */

typedef struct XSettingsData XSettingsData;   /**< 私有数据（不透明，对标 QSettingsPrivate + QConfFile）。 */

/**
 * @brief      XSettings 设置对象（对标 Qt 6.8 QSettings）。
 * @details    m_class 是第一个成员，因此对象可向上转换为 XObject；私有
 *             状态全部收在 m_data（不透明指针），调用方不得直接访问。
 */
typedef struct XSettings
{
    XObject        m_class;    /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XSettingsData* m_data;     /**< 私有数据（对标 QSettingsPrivate）；对象拥有。 */
} XSettings;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化并返回 XSettings 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；虚表创建或注册失败时返回 NULL。
 * @note       返回指针具有静态生命周期，调用方不得释放、修改或保存为
 *             可写的派生虚表。
 */
XVtable* XSettings_class_init(void);

/**
 * @brief      默认初始化嵌入式 XSettings 对象（对标 QSettings() 默认构造）。
 * @details    组织名/应用名取 XCoreApplication 的全局设置（可为空），格式
 *             取 XSettings_defaultFormat()（默认 NativeFormat，本实现退化为
 *             INI），作用域 UserScope，文件路径按组织名/应用名派生；无
 *             parent 参数（Qt 的 parent=nullptr 分支）。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @note       init 不分配 self；栈对象用 XClassDeinit（析构即 sync 落盘），
 *             堆对象用 XClassDelete。
 */
void XSettings_init(XSettings* self);

/**
 * @brief      带父对象的初始化重载（对标 QSettings(QObject *parent)）。
 * @details    除父对象外与 XSettings_init 一致；parent 释放时级联释放本
 *             堆对象（栈对象仅 deinit），不取得其所有权。
 */
void XSettings_init_2(XSettings* self, XObject* parent);

/**
 * @brief      带作用域的初始化重载（对标 QSettings(Scope scope, QObject *parent)）。
 * @param      scope 设置作用域（用户级/系统级）。
 * @param      parent 父对象借用指针；可为 NULL。
 */
void XSettings_init_3(XSettings* self, XSettings_Scope scope, XObject* parent);

/**
 * @brief      带组织名/应用名的初始化重载
 *              （对标 QSettings(organization, application, QObject *parent)）。
 * @param      organization 组织名；可为 NULL/空（此时文件退化为应用名派生）。
 * @param      application 应用名；可为 NULL/空。
 * @param      parent 父对象借用指针；可为 NULL。
 */
void XSettings_init_4(XSettings* self, const XString* organization,
                      const XString* application, XObject* parent);

/**
 * @brief      XSettings_init_4 的 UTF-8 便捷版本。
 * @param      organizationUtf8 组织名；按 UTF-8 解码，可为 NULL/空。
 * @param      applicationUtf8 应用名；按 UTF-8 解码，可为 NULL/空。
 * @param      parent 父对象借用指针；可为 NULL。
 */
void XSettings_init_4_utf8(XSettings* self, const char* organizationUtf8,
                           const char* applicationUtf8, XObject* parent);

/**
 * @brief      带作用域/组织名/应用名的初始化重载
 *              （对标 QSettings(Scope, organization, application, QObject *parent)）。
 */
void XSettings_init_5(XSettings* self, XSettings_Scope scope,
                      const XString* organization, const XString* application,
                      XObject* parent);

/**
 * @brief      带格式/作用域/组织名/应用名的初始化重载（对标
 *              QSettings(Format, Scope, organization, application, QObject *parent)）。
 * @param      format 存储格式；NativeFormat 按 INI 文件处理，Registry32/64
 *             无后端（读写置 AccessError），CustomFormatN 需先 registerFormat。
 */
void XSettings_init_6(XSettings* self, XSettings_Format format, XSettings_Scope scope,
                      const XString* organization, const XString* application,
                      XObject* parent);

/**
 * @brief      按显式文件路径的初始化重载
 *              （对标 QSettings(fileName, format, QObject *parent)）。
 * @details    organizationName/applicationName 置空；路径支持相对路径
 *             （内部经 XFileInfo 规范化为绝对路径，行为对齐 Qt）。
 * @param      fileName INI 文件路径；不可为 NULL（为 NULL 时按组织名/应用名
 *             默认路径派生，行为对齐 Qt 空文件名的容错分支）。
 * @param      format 存储格式。
 * @param      parent 父对象借用指针；可为 NULL。
 */
void XSettings_init_7(XSettings* self, const XString* fileName,
                      XSettings_Format format, XObject* parent);

/**
 * @brief      XSettings_init_7 的 UTF-8 便捷版本。
 * @param      fileNameUtf8 INI 文件路径；按 UTF-8 解码。
 */
void XSettings_init_7_utf8(XSettings* self, const char* fileNameUtf8,
                           XSettings_Format format, XObject* parent);

/**
 * @brief      使用默认内存类型创建堆对象（对标 QSettings() 默认构造）。
 * @return     新建对象指针，调用方用 XClassDelete 释放；失败返回 NULL。
 */
XSettings* XSettings_create(void);

/** @brief 同 XSettings_init_2 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_2(XObject* parent);

/** @brief 同 XSettings_init_3 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_3(XSettings_Scope scope, XObject* parent);

/** @brief 同 XSettings_init_4 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_4(const XString* organization,
                              const XString* application, XObject* parent);

/** @brief 同 XSettings_init_4_utf8 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_4_utf8(const char* organizationUtf8,
                                   const char* applicationUtf8, XObject* parent);

/** @brief 同 XSettings_init_5 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_5(XSettings_Scope scope, const XString* organization,
                              const XString* application, XObject* parent);

/** @brief 同 XSettings_init_6 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_6(XSettings_Format format, XSettings_Scope scope,
                              const XString* organization, const XString* application,
                              XObject* parent);

/** @brief 同 XSettings_init_7 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_7(const XString* fileName, XSettings_Format format,
                              XObject* parent);

/** @brief 同 XSettings_init_7_utf8 的堆创建版本；调用方用 XClassDelete 释放。 */
XSettings* XSettings_create_7_utf8(const char* fileNameUtf8, XSettings_Format format,
                                   XObject* parent);

/** @brief 深拷贝创建（Qt 禁拷贝；本类按 XClass 对象模型提供独立实例拷贝）。 */
XSettings* XSettings_create_copy(const XSettings* other);

/** @brief 转移创建（源对象置空态；调用方用 XClassDelete 释放）。 */
XSettings* XSettings_create_move(XSettings* other);

/* ==================== 键值读写 ==================== */

/**
 * @brief      写入键值（对标 QSettings::setValue）。
 * @details    key 以当前分组为前缀解析（见 beginGroup），值以 XVariant
 *             深拷贝保存；仅改内存态，落盘时机对齐 Qt（析构/sync/事件循环
 *             空闲，本实现为析构/sync）。key 为空串时按 Qt 语义当前分组
 *             顶层键处理（剥分组后为空则忽略）。
 * @param      self 对象指针。
 * @param      key 键名；不可为 NULL（NULL 静默忽略，对齐 Qt 空键容错）。
 * @param      value 值借用指针；可为 NULL（等价于写入无效值，读回为
 *             XVariantType_NULL，序列化为 @Invalid()）。
 * @note       本函数不落盘；显式调 XSettings_sync 或依赖析构落盘。
 */
void XSettings_setValue(XSettings* self, const XString* key, const XVariant* value);

/**
 * @brief      XSettings_setValue 的 UTF-8 键名便捷版本。
 * @param      keyUtf8 键名；按 UTF-8 解码，不可为 NULL。
 */
void XSettings_setValue_utf8(XSettings* self, const char* keyUtf8, const XVariant* value);

/**
 * @brief      读取键值（对标 QSettings::value(key)）。
 * @details    键不存在时返回 XVariantType_NULL 变体（对标 Qt 返回无效
 *             QVariant）；INI 文件在此前未读入时按需读入（含解析错误
 *             置 XSettings_FormatError）。
 * @param      self 对象指针。
 * @param      key 键名；不可为 NULL。
 * @return     新建 XVariant，调用方用 XClassDelete 释放；self/key 非法时
 *             返回 NULL。
 */
XVariant* XSettings_value(const XSettings* self, const XString* key);

/**
 * @brief      读取键值（带默认值；对标 QSettings::value(key, defaultValue)）。
 * @details    键不存在时返回 defaultValue 的深拷贝（而非 Qt 的引用语义，
 *             所有权归调用方；Qt 默认值参数本身也不被存储）。
 */
XVariant* XSettings_value_2(const XSettings* self, const XString* key,
                            const XVariant* defaultValue);

/** @brief XSettings_value 的 UTF-8 键名便捷版本。 */
XVariant* XSettings_value_utf8(const XSettings* self, const char* keyUtf8);

/** @brief XSettings_value_2 的 UTF-8 键名便捷版本。 */
XVariant* XSettings_value_2_utf8(const XSettings* self, const char* keyUtf8,
                                 const XVariant* defaultValue);

/**
 * @brief      判断键是否存在（对标 QSettings::contains）。
 * @param      key 键名（相对当前分组解析）；不可为 NULL。
 * @return     存在返回 true；self/key 非法返回 false。
 */
bool XSettings_contains(const XSettings* self, const XString* key);

/** @brief XSettings_contains 的 UTF-8 键名便捷版本。 */
bool XSettings_contains_utf8(const XSettings* self, const char* keyUtf8);

/**
 * @brief      删除键及其子树（对标 QSettings::remove）。
 * @details    key 指向组时整组（含嵌套子组）删除；指向键时仅删该键；
 *             空串删除全部（Qt 文档定义行为）。IniFormat 大小写不敏感
 *             匹配，与 Qt 一致。
 */
void XSettings_remove(XSettings* self, const XString* key);

/** @brief XSettings_remove 的 UTF-8 键名便捷版本。 */
void XSettings_remove_utf8(XSettings* self, const char* keyUtf8);

/**
 * @brief      全部键列表（对标 QSettings::allKeys）。
 * @return     新建 XStringList，元素为完整键路径（相对文件根、不含当前
 *             分组），调用方用 XClassDelete 释放；self 非法返回 NULL。
 */
XStringList* XSettings_allKeys(const XSettings* self);

/**
 * @brief      当前分组的直接子键列表（对标 QSettings::childKeys）。
 * @return     新建 XStringList（不含嵌套键、不含子组名），调用方释放。
 */
XStringList* XSettings_childKeys(const XSettings* self);

/**
 * @brief      当前分组的直接子组列表（对标 QSettings::childGroups）。
 * @return     新建 XStringList（不含嵌套子组），调用方释放。
 */
XStringList* XSettings_childGroups(const XSettings* self);

/* ==================== 分组与数组 ==================== */

/**
 * @brief      进入分组（对标 QSettings::beginGroup）。
 * @details    分组栈式嵌套；后续键值访问的键名自动加该前缀。prefix 含
 *             '/' 时按嵌套组处理（对齐 Qt）。
 * @param      prefix 组名；不可为 NULL。
 */
void XSettings_beginGroup(XSettings* self, const XString* prefix);

/** @brief XSettings_beginGroup 的 UTF-8 组名便捷版本。 */
void XSettings_beginGroup_utf8(XSettings* self, const char* prefixUtf8);

/**
 * @brief      退出当前分组（对标 QSettings::endGroup）。
 * @details    栈空时按 Qt 语义告警后忽略（不崩溃）。
 */
void XSettings_endGroup(XSettings* self);

/**
 * @brief      当前完整分组路径（对标 QSettings::group）。
 * @return     借用指针（形如 "a/b/c"），下次改分组/析构前有效；栈空时
 *             返回空串指针（非 NULL）。self 非法返回 NULL。
 */
const XString* XSettings_group_const(const XSettings* self);

/**
 * @brief      开始读数组（对标 QSettings::beginReadArray）。
 * @details    数组即键 "prefix/N/xxx" 的组集合；返回元素个数（读
 *             "prefix/size" 键，缺失/非法返回 0），并将当前分组移入
 *             "prefix"。配套 setArrayIndex 选择下标后按子键读写。
 * @return     数组元素个数；self/prefix 非法返回 0。
 */
int XSettings_beginReadArray(const XSettings* self, const XString* prefix);

/** @brief XSettings_beginReadArray 的 UTF-8 组名便捷版本。 */
int XSettings_beginReadArray_utf8(const XSettings* self, const char* prefixUtf8);

/**
 * @brief      开始写数组（对标 QSettings::beginWriteArray(prefix)）。
 * @details    size 未给定时按现有最大下标 + 1 推导（Qt 语义：遍历已有
 *             键算 maxArrayIndex），并写 "prefix/size" 键。
 */
void XSettings_beginWriteArray(XSettings* self, const XString* prefix);

/**
 * @brief      开始写数组（显式元素个数；对标 QSettings::beginWriteArray(prefix, size)）。
 * @param      size 元素个数；<0 时同无 size 版本（现有个数推导）。
 */
void XSettings_beginWriteArray_2(XSettings* self, const XString* prefix, int size);

/** @brief XSettings_beginWriteArray 的 UTF-8 组名便捷版本。 */
void XSettings_beginWriteArray_utf8(XSettings* self, const char* prefixUtf8);

/** @brief XSettings_beginWriteArray_2 的 UTF-8 组名便捷版本。 */
void XSettings_beginWriteArray_2_utf8(XSettings* self, const char* prefixUtf8, int size);

/**
 * @brief      结束数组（对标 QSettings::endArray）。
 * @details    等价于 endGroup（数组即分组实现，Qt 同构）。
 */
void XSettings_endArray(XSettings* self);

/**
 * @brief      设置数组当前下标（对标 QSettings::setArrayIndex）。
 * @param      i 下标；负值按 Qt 语义置无效（后续键访问不带下标前缀）。
 */
void XSettings_setArrayIndex(XSettings* self, int i);

/* ==================== 生命周期与状态 ==================== */

/**
 * @brief      清空全部键值（对标 QSettings::clear）。
 * @details    仅清内存态并标记待写；文件在 sync/析构时被覆盖为空。
 */
void XSettings_clear(XSettings* self);

/**
 * @brief      将未落盘的修改写回文件（对标 QSettings::sync）。
 * @details    经 XSaveFile 临时文件 + 原子重命名写盘（Qt 同构）；无修改
 *             时不写。写失败置 XSettings_AccessError。
 */
void XSettings_sync(XSettings* self);

/**
 * @brief      当前状态（对标 QSettings::status）。
 * @return     首次访问以来的错误状态；无错误返回 XSettings_NoError。
 */
XSettings_Status XSettings_status(const XSettings* self);

/**
 * @brief      文件是否可写（对标 QSettings::isWritable）。
 * @details    按文件所在目录的可写性判断；不可写时 setValue 仍入内存态，
 *             sync 失败置 AccessError（Qt 同语义）。
 */
bool XSettings_isWritable(const XSettings* self);

/** @brief 原子写是否强制（对标 QSettings::isAtomicSyncRequired）。 */
bool XSettings_isAtomicSyncRequired(const XSettings* self);

/**
 * @brief      设置原子写强制（对标 QSettings::setAtomicSyncRequired）。
 * @details    true 时 sync 失败不回退为直接覆盖写（保持原文件不动），
 *             置 AccessError；false（默认）允许临时文件机制不可用时直接
 *             覆盖写。
 */
void XSettings_setAtomicSyncRequired(XSettings* self, bool enable);

/** @brief 是否启用回退文件链（对标 QSettings::fallbacksEnabled）。 */
bool XSettings_fallbacksEnabled(const XSettings* self);

/**
 * @brief      设置回退文件链开关（对标 QSettings::setFallbacksEnabled）。
 * @details    本实现单文件后端（无 organizationName 多级回退文件的
 *             Qt 桌面注册表场景），该开关仅保存/返回，不改变读写行为。
 */
void XSettings_setFallbacksEnabled(XSettings* self, bool b);

/* ==================== 元信息 ==================== */

/**
 * @brief      实际读写的文件路径（对标 QSettings::fileName）。
 * @return     借用指针（含按组织名/应用名/作用域解析后的绝对路径）；
 *             对象存活期间有效。self 非法返回 NULL。
 */
const XString* XSettings_fileName_const(const XSettings* self);

/** @brief 存储格式（对标 QSettings::format）。 */
XSettings_Format XSettings_format(const XSettings* self);

/** @brief 作用域（对标 QSettings::scope）。 */
XSettings_Scope XSettings_scope(const XSettings* self);

/** @brief 组织名借用指针（对标 QSettings::organizationName）；可为空串。 */
const XString* XSettings_organizationName_const(const XSettings* self);

/** @brief 应用名借用指针（对标 QSettings::applicationName）；可为空串。 */
const XString* XSettings_applicationName_const(const XSettings* self);

/* ==================== 静态配置 ==================== */

/**
 * @brief      设置默认格式（对标 QSettings::setDefaultFormat）。
 * @details    影响"未显式给格式"的构造器；进程级单点。
 */
void XSettings_setDefaultFormat(XSettings_Format format);

/** @brief 读取默认格式（对标 QSettings::defaultFormat）；初始为 NativeFormat。 */
XSettings_Format XSettings_defaultFormat(void);

/**
 * @brief      设置格式+作用域的文件系统根路径（对标 QSettings::setPath）。
 * @details    覆盖按组织名/应用名派生路径时所用目录（UserScope/
 *             SystemScope 分别设置）；仅影响显式调用后的新对象。
 * @param      path 根目录；不可为 NULL。
 */
void XSettings_setPath(XSettings_Format format, XSettings_Scope scope, const XString* path);

/** @brief XSettings_setPath 的 UTF-8 路径便捷版本。 */
void XSettings_setPath_utf8(XSettings_Format format, XSettings_Scope scope,
                            const char* pathUtf8);

/**
 * @brief      注册自定义格式（对标 QSettings::registerFormat）。
 * @details    按文件扩展名匹配（caseSensitivity 决定匹配口径）；同扩展名
 *             后注册覆盖先注册。readFunc/writeFunc 至少给一个（均可为
 *             NULL，对应方向操作置 AccessError）。
 * @param      extension 扩展名（形如 ".conf"，含点）；不可为 NULL/空。
 * @return     分配到的 XSettings_Format（CustomFormat1..16）；注册满
 *             16 个返回 XSettings_InvalidFormat。
 */
XSettings_Format XSettings_registerFormat(const XString* extension,
                                          XSettings_ReadFunc readFunc,
                                          XSettings_WriteFunc writeFunc,
                                          XSettings_CaseSensitivity caseSensitivity);

/** @brief XSettings_registerFormat 的 UTF-8 扩展名便捷版本。 */
XSettings_Format XSettings_registerFormat_utf8(const char* extensionUtf8,
                                               XSettings_ReadFunc readFunc,
                                               XSettings_WriteFunc writeFunc,
                                               XSettings_CaseSensitivity caseSensitivity);

#endif /* XSETTINGS_ON */

#ifdef __cplusplus
}
#endif

#endif /* XSETTINGS_H */
