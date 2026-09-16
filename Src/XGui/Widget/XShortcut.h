/**
 * @file       XShortcut.h
 * @brief      XShortcut 快捷键对象（对标 Qt 6.8 QShortcut 核心公共 API；
 *             QShortcut : QObject，本类 XObject 派生）。
 * @details    功能范围：
 *             - key/setKey（XKeySequence 以 int 键码承载，键码取
 *               XEvent.h 的 XKey 枚举；@note 映射说明）；
 *             - enabled/isEnabled、context/setContext、autoRepeat/
 *               setAutoRepeat、whatsThis/setWhatsThis；
 *             - 信号：activated()/activatedAmbiguously()（空参，
 *               args=NULL）；
 *             - 全局注册表：XShortcut_register/unregister（模块静态
 *               XVector<XShortcut*>），XShortcut_match 供调用方按键
 *               匹配；XWidget 按键路径不强制接入（@note）。
 * @note       XShortcutContext 数值按 Qt 6.8 头文件 qnamespace.h 的
 *             Qt::ShortcutContext 逐项对齐：WidgetShortcut=0、
 *             WindowShortcut=1、ApplicationShortcut=2、
 *             WidgetWithChildrenShortcut=3。
 * @note       匹配语义简化：enabled 且 key 相等即候选；context 过滤
 *             ——ApplicationShortcut 恒匹配；WindowShortcut 与
 *             WidgetWithChildrenShortcut 要求 focusWidget 非 NULL；
 *             WidgetShortcut 要求 focusWidget 非 NULL 且等于创建时
 *             传入的 parent（parent 为 NULL 时等价任意焦点控件）。
 * @author     XinYueC 团队
 */
#ifndef XSHORTCUT_H
#define XSHORTCUT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XWidget.h"

#if XWIDGET_ON

/* ==================== 枚举（对标 Qt::ShortcutContext，数值对齐 Qt 6.8） ==================== */

/**
 * @brief      快捷键上下文（对标 Qt 6.8 Qt::ShortcutContext，数值逐项
 *             一致）。
 * @details    WidgetShortcut 仅在创建时传入的 parent 控件获得焦点时
 *             生效；WindowShortcut 在 parent 所在窗口活动时生效；
 *             ApplicationShortcut 在整个应用活动时生效；
 *             WidgetWithChildrenShortcut 在 parent 或其子控件获得焦点
 *             时生效。本实现的匹配过滤见文件头 @note（简化语义）。
 */
typedef enum XShortcutContext
{
    XShortcutContext_WidgetShortcut = 0,          /**< 控件级（对标 Qt::WidgetShortcut）。 */
    XShortcutContext_WindowShortcut = 1,          /**< 窗口级（对标 Qt::WindowShortcut）。 */
    XShortcutContext_ApplicationShortcut = 2,     /**< 应用级（对标 Qt::ApplicationShortcut）。 */
    XShortcutContext_WidgetWithChildrenShortcut = 3 /**< 控件及子控件级（对标 Qt::WidgetWithChildrenShortcut）。 */
} XShortcutContext;

/* ==================== 类虚函数表 ==================== */

/**
 * @brief      XShortcut 类虚函数表。
 * @details    不新增虚函数槽位，直接继承 XObject 的事件槽位，并重载
 *             XClass 的 Deinit 以释放文本资源并注销注册表。
 */
XCLASS_DEFINE_BEGING(XShortcut)
XCLASS_DEFINE_EXTEND_END(XShortcut, XObject)

/* ==================== 快捷键对象（对标 Qt 6.8 QShortcut） ==================== */

/**
 * @brief      XShortcut 快捷键对象。
 * @details    m_base 是第一个成员；m_key 以 int 承载单键键码（XKey
 *             枚举值，组合键序列不在本子批范围，见文件头 @note）；
 *             m_whatsThis 为对象拥有的 XString；m_parentWidget 为创建
 *             时传入的父对象借用指针（用于 WidgetShortcut 匹配）。
 */
typedef struct XShortcut
{
    XObject           m_base;         /**< 基类成员；必须是第一个，由 XClass 管理。 */
    int               m_key;          /**< 键码（XKey 枚举值；0 表示未设置）。 */
    bool              m_enabled;      /**< 是否启用（默认 true）。 */
    bool              m_autoRepeat;   /**< 是否允许自动重复（默认 true）。 */
    XShortcutContext  m_context;      /**< 快捷键上下文（默认 WindowShortcut）。 */
    XString*          m_whatsThis;    /**< What's This 帮助文本（对象拥有）。 */
    XObject*          m_parentWidget; /**< 创建时父对象借用指针（可为 NULL）。 */
} XShortcut;

/* ==================== 生命周期（对标 QShortcut 构造） ==================== */

/**
 * @brief      初始化并返回 XShortcut 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；失败返回 NULL。
 */
XVtable* XShortcut_class_init(void);

/**
 * @brief      默认初始化嵌入式 XShortcut 对象。
 * @param      self 待初始化的可写对象存储；不可为 NULL。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_init(XShortcut* self, XObject* parent);

/**
 * @brief      使用默认内存类型创建快捷键对象。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。成功后必须
 *             XShortcut_delete_base 释放。
 */
#define XShortcut_create(parent) \
    XShortcut_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
XShortcut* XShortcut_create_ex(XMemoryType memory, XObject* parent);

/**
 * @brief      带初始键码创建快捷键对象（对标 QShortcut(key,parent)）。
 * @param      key 初始键码（XKey 枚举值）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。
 */
#define XShortcut_create_2(key, parent) \
    XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE, (key), (parent))
XShortcut* XShortcut_create_2_ex(XMemoryType memory, int key,
                                 XObject* parent);

#define XShortcut_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XShortcut_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QShortcut） ==================== */

/**
 * @brief      设置快捷键键码（对标 QShortcut::setKey）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      key 键码（XKey 枚举值；0 清除）。
 * @return     无返回值。
 */
void XShortcut_setKey(XShortcut* self, int key);

/**
 * @brief      查询快捷键键码（对标 QShortcut::key）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     键码；self 为 NULL 时返回 0。
 */
int XShortcut_key(const XShortcut* self);

/**
 * @brief      设置是否启用（对标 QShortcut::setEnabled）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      enabled true 启用，false 禁用（禁用后 match 不命中）。
 * @return     无返回值。
 */
void XShortcut_setEnabled(XShortcut* self, bool enabled);

/**
 * @brief      查询是否启用（对标 QShortcut::isEnabled）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     启用返回 true；self 为 NULL 返回 false。
 */
bool XShortcut_isEnabled(const XShortcut* self);

/**
 * @brief      设置快捷键上下文（对标 QShortcut::setContext）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      context XShortcutContext 枚举值。
 * @return     无返回值。
 */
void XShortcut_setContext(XShortcut* self, XShortcutContext context);

/**
 * @brief      查询快捷键上下文（对标 QShortcut::context）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     上下文枚举值；self 为 NULL 返回 WindowShortcut。
 */
XShortcutContext XShortcut_context(const XShortcut* self);

/**
 * @brief      设置是否允许自动重复（对标 QShortcut::setAutoRepeat）。
 * @details    仅存储标志；XGui 当前无按键重复事件路径接入。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      on true 允许，false 禁止。
 * @return     无返回值。
 */
void XShortcut_setAutoRepeat(XShortcut* self, bool on);

/**
 * @brief      查询是否允许自动重复（对标 QShortcut::autoRepeat）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     允许返回 true；self 为 NULL 返回 false。
 */
bool XShortcut_autoRepeat(const XShortcut* self);

/**
 * @brief      设置 What's This 帮助文本（对标 QShortcut::setWhatsThis）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      text 源文本借用指针；可为 NULL 表示空文本。
 * @return     无返回值。
 */
void XShortcut_setWhatsThis(XShortcut* self, const XString* text);

/**
 * @brief      设置 What's This 帮助文本（UTF-8 兼容重载）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      utf8 以 '\0' 结尾的 UTF-8 文本；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_setWhatsThis_2(XShortcut* self, const char* utf8);

/**
 * @brief      查询 What's This 帮助文本的拷贝（对标 QShortcut::whatsThis）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；未设置或 self 为 NULL 返回 NULL。
 */
XString* XShortcut_whatsThis(const XShortcut* self);

/* ==================== 全局注册表与按键匹配（XGui 扩展入口） ==================== */

/**
 * @brief      把快捷键对象注册进全局注册表。
 * @details    注册表为模块静态 XVector<XShortcut*>；重复注册忽略。
 *             create 时自动注册，delete 时自动注销；此函数供手动
 *             管理注册状态的调用方使用。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_register(XShortcut* self);

/**
 * @brief      从全局注册表注销快捷键对象。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_unregister(XShortcut* self);

/**
 * @brief      按键匹配：遍历注册表返回第一个命中的快捷键。
 * @details    匹配规则（简化）：enabled 且 key 相等；context 过滤按
 *             Qt 语义简化——ApplicationShortcut 恒匹配；WindowShortcut
 *             与 WidgetWithChildrenShortcut 要求 focusWidget 非 NULL；
 *             WidgetShortcut 要求 focusWidget 非 NULL 且等于创建时
 *             parent（parent 为 NULL 时等价任意焦点控件）。
 * @param      key 按键键码（XKey 枚举值）。
 * @param      context 当前上下文枚举值；本实现暂不使用该参数过滤
 *             （保留 Qt 语义扩展点，@note）。
 * @param      focusWidget 当前焦点控件借用指针；可为 NULL。
 * @return     命中的快捷键借用指针；未命中返回 NULL。调用方不应释放
 *             返回对象。
 */
XShortcut* XShortcut_match(int key, XShortcutContext context,
                           XWidget* focusWidget);

/**
 * @brief      触发快捷键：发射 activated 信号。
 * @details    XGui 扩展入口（对标 QShortcut 内部激活语义）：供按键
 *             路径调用方在 XShortcut_match 命中后触发；本实现不区分
 *             歧义，恒发射 activated()。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_activate(XShortcut* self);

/* ==================== 信号（对标 QShortcut signals） ==================== */

/**
 * @brief      快捷键激活信号（对标 QShortcut::activated；空参，
 *             args=NULL 发射）。
 * @param      self 发射信号的对象；可为 NULL。
 * @return     不透明的 activated 信号标识。
 */
void* XShortcut_activated_signal(XShortcut* self);

/**
 * @brief      快捷键歧义激活信号（对标 QShortcut::activatedAmbiguously；
 *             空参，args=NULL 发射）。
 * @details    本实现未建模多快捷键歧义，信号保留供调用方连接。
 * @param      self 发射信号的对象；可为 NULL。
 * @return     不透明的 activatedAmbiguously 信号标识。
 */
void* XShortcut_activatedAmbiguously_signal(XShortcut* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON */
#endif /* XSHORTCUT_H */
