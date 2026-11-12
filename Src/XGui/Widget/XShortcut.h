/**
 * @file       XShortcut.h
 * @brief      XShortcut 快捷键对象（对标 Qt 6.8 QShortcut 核心公共 API；
 *             QShortcut : QObject，本类 XObject 派生）。
 * @details    功能范围：
 *             - key/setKey、keys()/setKeys() 多序列形态（对标 Qt 6
 *               QShortcut 的 `QList<QKeySequence> keys()`：一个快捷键可
 *               登记多条**备选**序列，任一条命中即触发）；
 *             - **修饰位感知**（台账 §B12 的主修项）：序列承载从「`XKey`
 *               裸键码」升级为 `XKeySequence` 值类型（修饰位 + 键码 +
 *               多组）；带修饰位的完整入口是
 *               `XShortcut_setKeySequence` / `XShortcut_keySequence` /
 *               `XShortcut_setSequences` / `XShortcut_sequenceCount` /
 *               `XShortcut_sequenceAt` / `XShortcut_match_2` /
 *               `XShortcut_activate_2`；旧的 int 键码入口
 *               （setKey/key/setKeys/keyCount/keyAt/match/activate）
 *               **保留为「裸键便捷视图」**（等价修饰位 = NoModifier），
 *               既有调用点行为逐位不变；
 *             - enabled/isEnabled、context/setContext、autoRepeat/
 *               setAutoRepeat、whatsThis/setWhatsThis；
 *             - id()（Qt 6.0 起弃用；本库为每条序列分配进程内唯一 id，
 *               与 QShortcutMap 的 `--currentId` 同号源）、
 *               parentWidget()（Qt 6.0 起弃用的模板，返回创建时父控件）；
 *             - 信号：activated()/activatedAmbiguously()（空参，
 *               args=NULL）；
 *             - 全局注册表：XShortcut_register/unregister（模块静态
 *               XVector<XShortcut*>），XShortcut_match/_2 供调用方按键
 *               匹配；XWidget 按键路径（XWidget_dispatchKeyEvent）在
 *               KEY_PRESS 上先过快捷键表，命中且未被 ShortcutOverride
 *               覆盖即激活——autoRepeat 守卫（P3-33）也在该点生效；
 *             - **平台原生（全局）快捷键**：XShortcut_setGlobal 让序列在
 *               平台层真正抓键（X11 XGrabKey / Win32 RegisterHotKey），
 *               应用失焦时仍触发；平台不支持或无抓键能力时
 *               XShortcut_setGlobal 返回 false 并退回进程内匹配
 *               （台账 §B12 验收方式 4）。
 * @note       XShortcutContext 数值按 Qt 6.8 头文件 qnamespace.h 的
 *             Qt::ShortcutContext 逐项对齐：WidgetShortcut=0、
 *             WindowShortcut=1、ApplicationShortcut=2、
 *             WidgetWithChildrenShortcut=3。
 * @note       匹配语义（对标 QShortcutMap::find/dispatchEvent，
 *             qshortcutmap.cpp:373-470）：enabled 且**任一**备选序列与
 *             按键「修饰位 + 键码」完全匹配即候选（XKeySequence_matches
 *             == ExactMatch）；比较前两侧都剥掉 KeypadModifier（对齐
 *             QShortcutMap::nextState 的 `find(e, Qt::KeypadModifier)`
 *             回退：NumLock 开时小键盘键多带 Keypad 位）。context 过滤——
 *             ApplicationShortcut 恒匹配；WindowShortcut 与
 *             WidgetWithChildrenShortcut 要求 focusWidget 非 NULL 且与
 *             快捷键父级同顶层；WidgetShortcut 要求 focusWidget 非 NULL
 *             且等于创建时传入的 parent（parent 为 NULL 时等价任意焦点
 *             控件）。
 * @note       按键侧的修饰位来自 XKeyEvent::m_modifiers（平台后端已端到端
 *             携带，见 XPlatformNativeWindow_posix.c 的
 *             xpwn_translateModifiers）。
 * @note       **多组序列（如 "Ctrl+O, Ctrl+S"）的已知限制**：Qt 的
 *             QShortcutMap（qshortcutmap.cpp nextState/find）按序按键
 *             **累积前缀匹配**——先 Ctrl+O 进入 PartialMatch、再 Ctrl+S
 *             才 ExactMatch 并派发；本实现的 XShortcut_match/_2 是
 *             **单事件匹配**（一次 KeyPress 与整条序列做
 *             XKeySequence_isEqual），多组序列因组数不等**恒为 NoMatch、
 *             永不触发**。这不会误触（安全方向），但「登记了却不响应」
 *             ——与 Qt 的差异在此**显式披露**：现网全部调用点
 *             （XAbstractButton/XLabel/XShortcutAlignTest）均为单组序列，
 *             行为兼容；多键累积匹配（需要 QShortcutMap 式的
 *             currentSequences 前缀状态机 + 按键消费语义改造
 *             XWidget_dispatchKeyEvent）建议专项批次补齐。同理，
 *             XShortcut_setGlobal（平台抓键）是单键码机制（XGrabKey /
 *             RegisterHotKey），**含多组序列的快捷键返回 false 不抓**，
 *             留在进程内匹配。
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
#include "XKeySequence.h"
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
 *             时生效。本实现的匹配过滤见文件头 @note。
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
 *             XClass 的 Deinit 以释放序列/文本资源并注销注册表。
 */
XCLASS_DEFINE_BEGING(XShortcut)
XCLASS_DEFINE_EXTEND_END(XShortcut, XObject)

/* ==================== 快捷键对象（对标 Qt 6.8 QShortcut） ==================== */

/**
 * @brief      XShortcut 快捷键对象。
 * @details    m_base 是第一个成员；m_keys 为对象拥有的
 *             `XVector<XKeySequence>`（多条备选序列，对标
 *             QShortcut::keys()）；m_whatsThis 为对象拥有的 XString；
 *             m_parentWidget 为创建时传入的父对象借用指针（用于
 *             Widget/Window 上下文匹配）；m_ids 与序列一一对应的进程内
 *             全局 id（对标 QShortcutMap 的 id 双射）。
 * @warning    结构体新增字段一律**尾部追加**（位置初始化器风险，见
 *             AGENTS.md「结构体初值铁律」）。m_keys 字段名与槽位沿用旧
 *             布局（只把元素类型换成 XKeySequence），不改变既有偏移。
 */
typedef struct XShortcut
{
    XObject           m_base;         /**< 基类成员；必须是第一个，由 XClass 管理。 */
    XString*          m_whatsThis;    /**< What's This 帮助文本（对象拥有）。 */
    XObject*          m_parentWidget; /**< 创建时父对象借用指针（可为 NULL）。 */
    XVector*          m_keys;         /**< 备选序列表（元素类型 XKeySequence，对象拥有；
                                           对标 QShortcut::keys()；空表=未绑定）。
                                           key()/setKey() 是取首项/单序列便捷视图。 */
    /* [内存治理 2026-10-11] 标志位字：2 个 bool + 上下文 0..3
       （XShortcutContext）合并 1 字。 */
    uint32_t m_enabled : 1;      /**< 是否启用（默认 true）。 */
    uint32_t m_autoRepeat : 1;   /**< 是否允许自动重复（默认 true）。 */
    uint32_t m_context : 2;      /**< 快捷键上下文（默认 WindowShortcut）。 */
    uint32_t m_global : 1;       /**< 是否已向平台层抓键（XShortcut_setGlobal）。 */
    uint32_t m_shortcutFlagsUnused : 27; /**< 保留位。 */
    /* ---- 尾部追加（2026-11-15，台账 §B12 修饰位/id 改造） ---- */
    XVector*          m_ids;      /**< 每条序列的全局 id（XVector<int>，对象拥有；
                                      长度 = m_keys 元素数；0 = 未分配）。
                                      复用框架 XVector，不手搓动态数组。 */
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
 * @brief      带初始裸键码创建快捷键对象（对标
 *             `QShortcut(QKeySequence(key), QObject*)`；裸键 = 无修饰位）。
 * @param      key 初始键码（XKey 枚举值）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。
 */
#define XShortcut_create_2(key, parent) \
    XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE, (key), (parent))
XShortcut* XShortcut_create_2_ex(XMemoryType memory, int key,
                                 XObject* parent);

/**
 * @brief      带初始序列创建快捷键对象（对标
 *             `QShortcut(const QKeySequence&, QObject*)`，含修饰位）。
 * @param      sequence 初始序列借用指针；可为 NULL（等价空序列）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。
 */
#define XShortcut_create_3(sequence, parent) \
    XShortcut_create_3_ex(XCLASS_DEFAULT_MEMORY_TYPE, (sequence), (parent))
XShortcut* XShortcut_create_3_ex(XMemoryType memory,
                                 const XKeySequence* sequence,
                                 XObject* parent);

/**
 * @brief      带初始「修饰位 + 键码」创建快捷键对象（便捷入口）。
 * @param      modifiers 修饰位掩码（XKeyboardModifiers 域）。
 * @param      key 键码（XKey 枚举值；0 视为空序列）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。
 */
#define XShortcut_create_4(modifiers, key, parent) \
    XShortcut_create_4_ex(XCLASS_DEFAULT_MEMORY_TYPE, (modifiers), (key), (parent))
XShortcut* XShortcut_create_4_ex(XMemoryType memory,
                                 XKeyboardModifiers modifiers, int key,
                                 XObject* parent);

/* ==================== 属性（对标 QShortcut） ==================== */

/**
 * @brief      设置主序列为单条裸键码（对标 QShortcut::setKey 的裸键形态）。
 * @details    等价 setKeys([key])：清空既有序列、只留这一条；修饰位为
 *             NoModifier。key==0 时清空（Qt 的 `setKey(QKeySequence())`
 *             ⇒ `setKeys({})`）。带修饰位的完整入口是
 *             XShortcut_setKeySequence_2 / XShortcut_setKeySequence。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      key 键码（XKey 枚举值；0 清除）。
 * @return     无返回值。
 */
void XShortcut_setKey(XShortcut* self, int key);

/**
 * @brief      查询首条序列的键码（对标 QShortcut::key 的键码视图）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     首条序列首组组合的键码；空序列或 self 为 NULL 时返回 0。
 */
int XShortcut_key(const XShortcut* self);

/**
 * @brief      设置多条备选裸键序列（对标 QShortcut::setKeys 的裸键形态）。
 * @details    替换全部序列：count<=0 或 keys 为 NULL 清空。任一条命中
 *             即触发。元素为裸键码（修饰位 NoModifier）；带修饰位用
 *             XShortcut_setSequences。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      keys 键码数组借用指针；可为 NULL。
 * @param      count 键码数量。
 * @return     无返回值。
 */
void XShortcut_setKeys(XShortcut* self, const int* keys, int count);

/**
 * @brief      查询序列条数（对标 QShortcut::keys().size()）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     条数；self 为 NULL 或未绑定返回 0。
 */
int XShortcut_keyCount(const XShortcut* self);

/**
 * @brief      按索引查询序列键码（对标 QShortcut::keys().value(index)）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      index 索引（0 起）。
 * @return     键码；越界/self 为 NULL 返回 0。
 */
int XShortcut_keyAt(const XShortcut* self, int index);

/**
 * @brief      查询首条序列的修饰位（`XKeySequence` 视图）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     修饰位掩码；空序列/NULL 返回 XKeyboardModifier_NoModifier。
 */
XKeyboardModifiers XShortcut_keyModifiers(const XShortcut* self);

/**
 * @brief      设置主序列（对标 `QShortcut::setKey(const QKeySequence&)`）。
 * @details    等价 setSequences([seq])：清空既有序列、只留这一条；
 *             sequence 为 NULL 或空序列时清空全部。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      sequence 序列借用指针；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_setKeySequence(XShortcut* self, const XKeySequence* sequence);

/**
 * @brief      按「修饰位 + 键码」设置主序列（便捷入口）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      modifiers 修饰位掩码。
 * @param      key 键码；0 清空序列。
 * @return     无返回值。
 */
void XShortcut_setKeySequence_2(XShortcut* self,
                                XKeyboardModifiers modifiers, int key);

/**
 * @brief      按文本设置主序列（对标 `setKey(QKeySequence("Ctrl+S"))`）。
 * @details    文本经常规 Qt 文本口径解析（XKeySequence_fromString）；
 *             解析失败（非法文本）时清空序列——与 Qt 把非法文本解析成
 *             空 QKeySequence 同口径。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      utf8 以 '\0' 结尾的快捷键文本（如 "Ctrl+S"）；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_setKeySequence_3(XShortcut* self, const char* utf8);

/**
 * @brief      查询主序列（对标 `QShortcut::key()` 返回的 QKeySequence）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     首条序列的**借用**指针（指向对象内部存储；随
 *             setKey 族/setSequences/对象析构失效，禁止释放）；空序列或
 *             self 为 NULL 时返回 NULL。
 * @warning    借用语义：调用方不得释放，也不得跨 setter 持有。
 */
const XKeySequence* XShortcut_keySequence(const XShortcut* self);

/**
 * @brief      设置多条备选序列（对标
 *             `QShortcut::setKeys(const QList<QKeySequence>&)`）。
 * @details    替换全部序列：count<=0 或 sequences 为 NULL 清空。任一条
 *             **单组**序列与按键完全匹配即触发（XShortcut_match_2）。
 *             同值早退（对标
 *             qshortcut.cpp:380-390 的 `if (d->sc_sequences == keys) return;`）。
 *             **已置全局抓键（XShortcut_setGlobal(true)）时**，序列变更
 *             会重新向平台层抓键（Qt 的 redoGrab 同义）。
 * @warning    **多组序列（count>=2，如 "Ctrl+O, Ctrl+S"）当前不参与按键
 *             匹配、永不触发**：Qt 按序累积前缀匹配（QShortcutMap
 *             nextState），本实现是单事件匹配（见文件头 @note 的显式
 *             披露）。登记多组序列不会误触（安全方向），但也不会响应；
 *             且含多组序列的快捷键 XShortcut_setGlobal 返回 false（平台
 *             抓键是单键码机制）。需要多键序列请走 XKeySequenceEdit 捕获
 *             后交由后续专项的累积匹配批次。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      sequences 序列数组借用指针（按值读取，不接管所有权）；
 *             可为 NULL。
 * @param      count 序列数量。
 * @return     无返回值。
 */
void XShortcut_setSequences(XShortcut* self, const XKeySequence* sequences,
                            int count);

/**
 * @brief      查询序列条数（对标 QShortcut::keys().size()）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     条数；self 为 NULL 或未绑定返回 0。
 */
int XShortcut_sequenceCount(const XShortcut* self);

/**
 * @brief      按索引查询序列（对标 QShortcut::keys().value(index)）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      index 索引（0 起）。
 * @return     序列的**借用**指针；越界/self 为 NULL 返回 NULL。
 * @warning    借用语义：不得释放，也不得跨 setter 持有。
 */
const XKeySequence* XShortcut_sequenceAt(const XShortcut* self, int index);

/**
 * @brief      按索引查询序列的全局 id（对标 QShortcut::id() 的逐序列形态）。
 * @details    id 由对象首次登记序列时从进程内单调递减计数器分配
 *             （对齐 QShortcutMap 的 `--(d->currentId)`，
 *             qshortcutmap.cpp:133）；0 表示未分配（空序列）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      index 序列索引（0 起）。
 * @return     id；越界/NULL/空序列返回 0。
 */
int XShortcut_sequenceId(const XShortcut* self, int index);

/**
 * @brief      主序列的全局 id（对标 QShortcut::id()，Qt 6.0 起弃用）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     首条序列 id；未分配返回 0。
 */
int XShortcut_id(const XShortcut* self);

/**
 * @brief      创建时父对象（对标 QShortcut::parentWidget()，Qt 6.0 起弃用）。
 * @details    Qt 的实现是 `static_cast<QWidget*>(QObject::parent())`；
 *             本库返回创建时登记的父对象借用指针（若它确实是控件，即
 *             QWidget* 的等价物；非控件父级同样原样返回，由调用方自行
 *             判定）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     父对象借用指针；无父级/NULL 返回 NULL。禁止释放。
 */
XObject* XShortcut_parentWidget(const XShortcut* self);

/**
 * @brief      设置是否启用（对标 QShortcut::setEnabled）。
 * @details    同值早退（对标 qshortcut.cpp:430-434）；禁用后 match 不命中、
 *             activate 不发射；已置全局抓键时同步改平台层的启用位
 *             （对标 `shortcutMap.setShortcutEnabled`）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      enabled true 启用，false 禁用。
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
 * @details    同值早退（对标 qshortcut.cpp:459-466）；已置全局抓键时
 *             重新抓键（Qt 的 redoGrab 同义）。
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
 *             缺省 true。【P3-33 2026-11-11：此前仅存位，无按键路径消费】
 *             同值早退（对标 qshortcut.cpp:484-494）。
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

/* ==================== 平台原生（全局）快捷键（对标 grabShortcut） ==================== */

/**
 * @brief      启用/禁用平台原生抓键（全局快捷键，应用失焦时仍触发）。
 * @details    对标 Qt 侧 `QWidget::grabShortcut`（qwidget.cpp:11894-11903）
 *             所做的事：把序列登记进平台层的快捷键表并按上下文抓键——
 *             X11 走 `XGrabKey`（含 BadAccess 冲突重试），Win32 走
 *             `RegisterHotKey`（WM_HOTKEY 消息）。
 *
 *             失败语义（台账 §B12 验收方式 2/4）：
 *             - 平台后端无抓键能力（未连接窗口系统 / 嵌入式 / X11 头未检出）
 *               ⇒ 返回 **false**，对象留在进程内匹配模式（既有行为），
 *               `m_global` 不置位；
 *             - 组合已被其它客户端（或本进程其它快捷键）抓走 ⇒ 返回
 *               **false**，不崩、不抢已注册组合；
 *             - 成功 ⇒ 返回 true，`m_global` 置位；此后
 *               setSequences/setContext/setEnabled 会同步重抓/改平台位。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      on true 抓键（全局），false 释放并退回进程内匹配。
 * @return     true 已生效；false 平台不支持或抓键冲突（对象保持
 *             进程内匹配）。
 */
bool XShortcut_setGlobal(XShortcut* self, bool on);

/**
 * @brief      查询是否已置平台原生抓键。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     true 已全局抓键。
 */
bool XShortcut_isGlobal(const XShortcut* self);

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
 * @brief      按键匹配（裸键版本；等价修饰位 = NoModifier）。
 * @details    保留向后兼容：`XShortcut_match(key, ctx, focus)` 等价
 *             `XShortcut_match_2(key, NoModifier, ctx, focus)`。
 *             旧调用点（裸键快捷键）行为逐位不变。
 * @param      key 按键键码（XKey 枚举值）。
 * @param      context 当前上下文枚举值；本实现不使用该参数过滤
 *             （保留 Qt 语义扩展点）。
 * @param      focusWidget 当前焦点控件借用指针；可为 NULL。
 * @return     命中的快捷键借用指针；未命中返回 NULL。调用方不应释放。
 */
XShortcut* XShortcut_match(int key, XShortcutContext context,
                           XWidget* focusWidget);

/**
 * @brief      按键匹配（带修饰位；对标 QShortcutMap::find + dispatchEvent）。
 * @details    匹配规则：enabled 且**任一**备选序列与「modifiers + key」
 *             完全匹配（XKeySequence_matches == ExactMatch）；比较前两侧
 *             都剥掉 KeypadModifier（对齐 QShortcutMap::nextState 的
 *             `find(e, Qt::KeypadModifier)` 回退）。context 过滤见文件头
 *             @note。同键多命中时返回注册表序首个（歧义判定由
 *             XShortcut_activate_2 负责）。
 * @param      key 按键键码（XKey 枚举值）。
 * @param      modifiers 事件修饰位（XKeyEvent::m_modifiers）。
 * @param      context 当前上下文枚举值（保留扩展点）。
 * @param      focusWidget 当前焦点控件借用指针；可为 NULL。
 * @return     命中的快捷键借用指针；未命中返回 NULL。调用方不应释放。
 */
XShortcut* XShortcut_match_2(int key, XKeyboardModifiers modifiers,
                             XShortcutContext context, XWidget* focusWidget);

/**
 * @brief      触发快捷键：无歧义发射 activated，有歧义改发
 *             activatedAmbiguously（对标 QShortcutMap 派发语义）。
 * @details    XGui 扩展入口：供按键路径调用方在命中后触发。发射前以本
 *             快捷键父控件为焦点代理重放 context 过滤：同序列命中多个
 *             启用快捷键时对每个命中者发射 activatedAmbiguously()（不发
 *             activated）；无歧义对 self 发射 activated()。**禁用
 *             （isEnabled()==false）的快捷键不发射任何信号**——对标
 *             QShortcutMap 的启用位过滤（qshortcutmap.cpp:373
 *             `(*it).enabled`）。
 * @param      self 目标快捷键对象；可为 NULL。
 * @return     无返回值。
 */
void XShortcut_activate(XShortcut* self);

/**
 * @brief      按「键码 + 修饰位」触发快捷键（带修饰位的激活入口）。
 * @details    与 XShortcut_activate 相同，但歧义判定用**完整序列**匹配
 *             （而不是首组键码）：同一个键码配不同修饰位不算歧义
 *             （Ctrl+S 与 S 是两条互不干扰的快捷键）。这正是 Qt
 *             QShortcutMap 以 keySequence 为键的行为。
 * @param      self 目标快捷键对象；可为 NULL。
 * @param      key 按下的键码。
 * @param      modifiers 按下的修饰位。
 * @return     无返回值。
 */
void XShortcut_activate_2(XShortcut* self, int key,
                          XKeyboardModifiers modifiers);

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
 *             XShortcut_activate/_2 对每个命中者发射（此时不发射
 *             activated，对标 Qt 歧义派发语义）。
 * @param      self 发射信号的对象；可为 NULL。
 * @return     不透明的 activatedAmbiguously 信号标识。
 */
void* XShortcut_activatedAmbiguously_signal(XShortcut* self);

/* ==================== 平台抓键桥（供 XWidget/XAbstractButton 复用） ====== */

/**
 * @brief      把「修饰位 + 键码」抓到平台层（内部桥，供 XWidget 的
 *             grabShortcut 族与 XAbstractButton 的 setShortcut 使用）。
 * @details    转发 XGuiApplication_grabShortcut（其内部落到
 *             XPlatformNativeWindow_grabShortcut）。抓键失败（冲突/
 *             无平台能力）返回 false；调用方据此可回退进程内匹配。
 *             同一组合重复抓键幂等（引用计数在平台层）。
 * @param      modifiers 修饰位掩码。
 * @param      key 键码。
 * @param      nativeWindow 归属原生窗口借用指针（可为 NULL，表示用
 *             平台缺省根窗口/主实例）。
 * @return     true 抓键成功。
 */
bool XShortcut_grabNative(XKeyboardModifiers modifiers, int key,
                          void* nativeWindow);

/**
 * @brief      释放平台层抓键（与 XShortcut_grabNative 成对；引用计数减一）。
 * @param      modifiers 修饰位掩码。
 * @param      key 键码。
 * @param      nativeWindow 归属原生窗口借用指针；可为 NULL。
 * @return     true 已释放（含「本无抓键」的幂等情形）。
 */
bool XShortcut_releaseNative(XKeyboardModifiers modifiers, int key,
                             void* nativeWindow);

#endif /* XWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSHORTCUT_H */
