/**
 * @file       XShortcut.h
 * @brief      XShortcut 快捷键对象（对标 Qt 6.8 QShortcut 核心公共 API；
 *             QShortcut : QObject，本类 XObject 派生）。
 * @details    功能范围：
 *             - key/setKey（XKeySequence 以 int 键码承载，键码取
 *               XEvent.h 的 XKey 枚举；@note 映射说明）；
 *             - keys()/setKeys() 多序列形态（对标 Qt 6 QShortcut 的
 *               `QList<QKeySequence> keys()` / `setKeys(...)`：一个快捷键
 *               可登记多条**备选**序列，任一条命中即触发；X 以 int 键码
 *               数组承载，键码语义与 key/setKey 相同）；
 *             - enabled/isEnabled、context/setContext、autoRepeat/
 *               setAutoRepeat、whatsThis/setWhatsThis；
 *             - 信号：activated()/activatedAmbiguously()（空参，
 *               args=NULL）；
 *             - 全局注册表：XShortcut_register/unregister（模块静态
 *               XVector<XShortcut*>），XShortcut_match 供调用方按键
 *               匹配；XWidget 按键路径（XWidget_dispatchKeyEvent）在
 *               KEY_PRESS 上先过快捷键表，命中且未被 ShortcutOverride
 *               覆盖即激活——autoRepeat 守卫（P3-33）也在该点生效。
 * @note       XShortcutContext 数值按 Qt 6.8 头文件 qnamespace.h 的
 *             Qt::ShortcutContext 逐项对齐：WidgetShortcut=0、
 *             WindowShortcut=1、ApplicationShortcut=2、
 *             WidgetWithChildrenShortcut=3。
 * @note       匹配语义简化：enabled 且**任一**备选序列键码等于按键即
 *             候选；context 过滤——ApplicationShortcut 恒匹配；
 *             WindowShortcut 与 WidgetWithChildrenShortcut 要求
 *             focusWidget 非 NULL；WidgetShortcut 要求 focusWidget 非
 *             NULL 且等于创建时传入的 parent（parent 为 NULL 时等价任意
 *             焦点控件）。
 * @note       **已知边界**：序列元素是 XKey 裸键码，不携带修饰位
 *             （QKeySequence 的 Ctrl/Alt/Shift 组合在本层无法表达）；
 *             修饰位感知的快捷键需要改 XShortcut 序列承载与 XWidget 的
 *             按键派发（见缺口台账 52/75），不在本类单独可修。
 * @author     XinYueC 团队
 */
#ifndef XSHORTCUT_H
#define XSHORTCUT_H

#if XGUI_ON
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
 * @details    m_base 是第一个成员；m_keys 为对象拥有的 int 键码数组
 *             （XKey 枚举值；多条备选序列，对标 QShortcut::keys()）；
 *             m_whatsThis 为对象拥有的 XString；m_parentWidget 为创建
 *             时传入的父对象借用指针（用于 WidgetShortcut 匹配）。
 */
typedef struct XShortcut
{
    XObject           m_base;         /**< 基类成员；必须是第一个，由 XClass 管理。 */
    XString*          m_whatsThis;    /**< What's This 帮助文本（对象拥有）。 */
    XObject*          m_parentWidget; /**< 创建时父对象借用指针（可为 NULL）。 */
    XVector*          m_keys;         /**< 备选序列键码表（int，对象拥有；
                                           对标 QShortcut::keys()；空表=未绑定）。
                                           key()/setKey() 是取首项/单序列便捷视图。 */
    /* [内存治理 2026-10-11] 标志位字：2 个 bool + 上下文 0..3
       （XShortcutContext）合并 1 字。 */
    uint32_t m_enabled : 1;      /**< 是否启用（默认 true）。 */
    uint32_t m_autoRepeat : 1;   /**< 是否允许自动重复（默认 true）。 */
    uint32_t m_context : 2;      /**< 快捷键上下文（默认 WindowShortcut）。 */
    uint32_t m_shortcutFlagsUnused : 28; /**< 保留位。 */
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
 *             XClassDelete 释放。
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


/* ==================== 属性（对标 QShortcut） ==================== */

/**
 * @brief      设置快捷键键码（对标 QShortcut::setKey）。
 * @details    等价 setKeys([key])：清空既有序列、只留这一条。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      key 键码（XKey 枚举值；0 清除）。
 * @return     无返回值。
 */
void XShortcut_setKey(XShortcut* self, int key);

/**
 * @brief      查询首条键码（对标 QShortcut::key：`keys().value(0)`）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     首条键码；空序列或 self 为 NULL 时返回 0。
 */
int XShortcut_key(const XShortcut* self);

/**
 * @brief      设置多条备选序列（对标 QShortcut::setKeys 的
 *             `QList<QKeySequence>`，qshortcut.h）。
 * @details    替换全部序列：count<=0 或 keys 为 NULL 清空。任一条命中即
 *             触发（XShortcut_match/activate 按「任一键码相等」判定）。
 *             序列元素是本框架的 int 键码（XKey），不是 QKeySequence 的
 *             修饰位组合——修饰位仍不在本层建模（见文件头 @note）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      keys 键码数组借用指针；可为 NULL。
 * @param      count 键码数量。
 * @return     无返回值。
 */
void XShortcut_setKeys(XShortcut* self, const int* keys, int count);

/**
 * @brief      查询序列条数（对标 QShortcut::keys().size()）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return 条数；self 为 NULL 或未绑定返回 0。
 */
int XShortcut_keyCount(const XShortcut* self);

/**
 * @brief      按索引查询序列键码（对标 QShortcut::keys().value(index)）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      index 索引（0 起）。
 * @return 键码；越界/self 为 NULL 返回 0。
 */
int XShortcut_keyAt(const XShortcut* self, int index);

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
 * @details    标志在按键派发点生效（`XWidget_dispatchKeyEvent`，对标
 *             QShortcutMap::tryShortcut 的守卫 qshortcutmap.cpp:588-591）：
 *             `keyEvent.isAutoRepeat() && !autoRepeat()` ⇒ 命中该快捷键时
 *             **不激活**（按键仍按常规路径送焦点控件）。
 *             缺省 true；头文件 qshortcut.h 注释口径「whether the shortcut
 *             can be triggered on auto-repeating key events」。【P3-33
 *             2026-11-11：此前仅存位，无按键路径消费】
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
 *             XClassDelete；未设置或 self 为 NULL 返回 NULL。
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
 * @details    匹配规则（简化）：enabled 且**任一**备选序列键码等于 key
 *             （多序列见 setKeys）；context 过滤按
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
 * @brief      触发快捷键：无歧义发射 activated，有歧义改发
 *             activatedAmbiguously（对标 QShortcutMap 派发语义）。
 * @details    XGui 扩展入口（对标 QShortcut 内部激活语义）：供按键
 *             路径调用方在 XShortcut_match 命中后触发。发射前以本快
 *             捷键父控件为焦点代理重放 context 过滤：同键命中多个启
 *             用快捷键时对每个命中者发射 activatedAmbiguously()（不
 *             发 activated，对标 Qt 歧义派发）；无歧义对 self 发射
 *             activated()。**禁用（isEnabled()==false）的快捷键不发射
 *             任何信号**——对标 QShortcutMap 的启用位过滤
 *             （qshortcutmap.cpp:373 `(*it).enabled`），禁用的快捷键
 *             根本不在匹配集合里。
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
 * @details    同一键序列在同上下文命中多个启用快捷键时，由
 *             XShortcut_activate 对每个命中者发射（此时不发射
 *             activated，对标 Qt 歧义派发语义）。
 * @param      self 发射信号的对象；可为 NULL。
 * @return     不透明的 activatedAmbiguously 信号标识。
 */
void* XShortcut_activatedAmbiguously_signal(XShortcut* self);

#endif /* XWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSHORTCUT_H */
