/**
 * @file       XVirtualKeyboardInputContext.h
 * @brief      XVirtualKeyboardInputContext 输入上下文公开 API（对标 Qt
 *             6.8 QVirtualKeyboardInputContext，qvirtualkeyboardinputcontext.h
 *             :59-111 全公共面）。
 * @details    对齐要点：
 *             - 进程单例 XVirtualKeyboardInputContext_instance()（Qt
 *               QML singleton InputContext；惰性单例先例
 *               XGuiApplication_inputMethod）；
 *             - 编辑控件零绑定：上下文不持有编辑控件引用，控件状态经
 *               XWidget_inputMethodQuery 虚槽（经
 *               XInputMethod_defaultQueryHandler 桥）向焦点控件查询；
 *               hints=控件级 OR 叠加 Settings.inputMethodHints
 *               （qvirtualkeyboardinputcontext_p.cpp:397-470 口径）；
 *             - 【commit 落地契约定型（公共信号）】commit(text)/虚键
 *               落地经公共信号 commitRequested(text)/
 *               keyEventRequested(key,text,modifiers)——默认面板在
 *               setTextArea/popup 时连接、closePopup 断开，经既有写入
 *               链落 m_target；无面板连接时提交/虚键丢弃（文档化，Qt
 *               无平台集成层同样无处落地）。上下文零绑定，写入执行权
 *               在已持 m_target 的面板；
 *             - ShiftHandler 状态机（Qt 私有 shifthandler.cpp 口径）内
 *               聚本类：toggleShift/reset/autoCapitalize 落保护头；双
 *               判定间隔=XStyleHints_mouseDoubleClickInterval（默认
 *               400）；公共读数=isShiftActive/isCapsLockActive/
 *               isUppercase；
 *             - 17 个无参信号照抄 :94-111 + 两扩展公共信号；
 *             - preeditTextAttributes 本轮无属性面（Qt QList
 *               <QInputMethodEvent::Attribute> 无基建），恒返回 0；
 *             - setSelectionOnFocusObject 无选区基建，登记入参后空操
 *               作（scopeOut：选区手柄 N-A）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象
 *             层，时间源用 XDateTime 单调毫秒（Src/ 禁直呼时间源裁定）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDINPUTCONTEXT_H
#define XVIRTUALKEYBOARDINPUTCONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"
#include "XGeometry.h"

#if XVIRTUALKEYBOARD_ON

/* 前向声明。 */
typedef struct XVirtualKeyboardInputEngine XVirtualKeyboardInputEngine;
typedef struct XVirtualKeyboardObserver XVirtualKeyboardObserver;
typedef struct XString XString;

/** @brief 声明 XVirtualKeyboardInputContext 虚函数枚举：继承 XObject
 *         （无新增槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardInputContext)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardInputContext, XObject)

/**
 * @brief      输入上下文对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存引擎（拥有）/焦点对象（借用）/组串/缓
 *             存查询值/ShiftHandler 状态机。调用者不得手工修改字段。
 */
typedef struct XVirtualKeyboardInputContext
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardInputContext;

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类共享虚函数表指针（进程期常驻，借用）；不失败。
 */
XVtable* XVirtualKeyboardInputContext_class_init(void);

/* ==================== 单例 ==================== */

/**
 * @brief      进程单例（惰性创建；对标 QML singleton InputContext）。
 * @return     单例借用指针；创建失败返回 NULL。不得释放。
 */
XVirtualKeyboardInputContext* XVirtualKeyboardInputContext_instance(void);

/* ==================== 状态查询（对标 :62-84） ==================== */

/**
 * @brief      返回 shift 激活态（对标 isShiftActive）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     shift 激活返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputContext_isShiftActive(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      返回大写锁激活态（对标 isCapsLockActive）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     大写锁激活返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputContext_isCapsLockActive(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      返回有效大写态（=shift||capsLock；对标 isUppercase）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     有效大写态返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputContext_isUppercase(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回锚点位置（向焦点控件查询 ImAnchorPosition）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     锚点位置（UTF-16 代码单元偏移）；无焦点或 self 为 NULL
 *             返回 0。
 */
int XVirtualKeyboardInputContext_anchorPosition(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      返回光标位置（向焦点控件查询 ImCursorPosition）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     光标位置（UTF-16 代码单元偏移）；无焦点或 self 为 NULL
 *             返回 0。
 */
int XVirtualKeyboardInputContext_cursorPosition(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回生效 hints（控件级 OR 叠加 Settings.inputMethodHints；
 *             对标 inputMethodHints）。
 * @details    无焦点时仅返回 Settings 叠加分量。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     生效提示位（可按位组合）；self 为 NULL 返回 0。
 */
uint32_t XVirtualKeyboardInputContext_inputMethodHints(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回组串文本（对标 preeditText）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     新建 XString*（空组串返回空串对象）；self 为 NULL 返回
 *             NULL。调用方用 XClassDelete 释放。
 */
XString* XVirtualKeyboardInputContext_preeditText(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回组串文本属性数（对标 preeditTextAttributes）。
 * @details    本轮无属性面（Qt QList<QInputMethodEvent::Attribute> 无
 *             基建），恒返回 0。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     属性数；本轮恒为 0。
 */
int XVirtualKeyboardInputContext_preeditTextAttributes(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      设置组串文本（对标 setPreeditText(text,attributes,
 *             replaceFrom,replaceLength)）。
 * @details    组串状态镜像入口：插件每次组串变化经此同步（Qt pinyin
 *             经 inputContext->setPreeditText 写串同型）；attributes/
 *             replaceFrom/replaceLength 本轮无行内渲染面，登记后忽略
 *             （scopeOut）。变化时发 preeditTextChanged。
 * @param      self 上下文对象；可为 NULL。
 * @param      text 组串文本（UTF-8 借用；NULL 等价清空，函数不取得所
 *             有权）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardInputContext_setPreeditText_2(
        XVirtualKeyboardInputContext* self, const char* text);

/**
 * @brief      返回环绕文本（向焦点控件查询 ImSurroundingText；无焦点
 *             返回空串对象）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方用 XClassDelete 释放；self
 *             为 NULL 或分配失败返回 NULL。
 */
XString* XVirtualKeyboardInputContext_surroundingText(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回选中文本（向焦点控件查询 ImCurrentSelection；无焦点
 *             返回空串对象）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方用 XClassDelete 释放；self
 *             为 NULL 或分配失败返回 NULL。
 */
XString* XVirtualKeyboardInputContext_selectedText(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回锚点矩形（向焦点控件查询）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     锚点矩形（控件局部坐标）；无焦点或 self 为 NULL 返回零
 *             矩形。
 */
XRectF XVirtualKeyboardInputContext_anchorRectangle(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      返回光标矩形（向焦点控件查询）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     光标矩形（控件局部坐标）；无焦点或 self 为 NULL 返回零
 *             矩形。
 */
XRectF XVirtualKeyboardInputContext_cursorRectangle(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回动画态（对标 isAnimating）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     动画进行中返回 true（默认 false）；self 为 NULL 返回
 *             false。
 */
bool XVirtualKeyboardInputContext_isAnimating(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      设置动画态（对标 setAnimating；变化发 animatingChanged）。
 * @param      self 上下文对象；可为 NULL，NULL 时不执行操作。
 * @param      isAnimating 新动画态。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardInputContext_setAnimating(
        XVirtualKeyboardInputContext* self, bool isAnimating);

/**
 * @brief      返回区域语言（对标 locale；默认 "zh_CN"）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方用 XClassDelete 释放；self
 *             为 NULL 或分配失败返回 NULL。
 */
XString* XVirtualKeyboardInputContext_locale(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回输入项对象（对标 inputItem；=当前焦点对象借用）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     焦点对象借用指针（不转移所有权，不得释放）；无焦点或
 *             self 为 NULL 返回 NULL。
 */
XObject* XVirtualKeyboardInputContext_inputItem(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回输入引擎（对标 inputEngine；上下文拥有）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     引擎借用指针（所有权在上下文，不得释放）；上下文未初
 *             始化或 self 为 NULL 返回 NULL。
 */
XVirtualKeyboardInputEngine* XVirtualKeyboardInputContext_inputEngine(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回选区控制可见态（对标 isSelectionControlVisible；无
 *             选区基建，默认 false）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     选区控制可见返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputContext_isSelectionControlVisible(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      锚点矩形与裁剪矩形相交（对标 anchorRectIntersectsClipRect）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     相交返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputContext_anchorRectIntersectsClipRect(
        const XVirtualKeyboardInputContext* self);
/**
 * @brief      光标矩形与裁剪矩形相交（对标 cursorRectIntersectsClipRect）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     相交返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputContext_cursorRectIntersectsClipRect(
        const XVirtualKeyboardInputContext* self);

/**
 * @brief      返回键盘观察器单例（对标 keyboardObserver，Qt
 *             REVISION(6,1)——无版本门槛，访问器恒可用）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     观察器单例借用指针（不得释放）；self 为 NULL 返回 NULL。
 */
XVirtualKeyboardObserver* XVirtualKeyboardInputContext_keyboardObserver(
        const XVirtualKeyboardInputContext* self);

/* ==================== 操作（Q_INVOKABLE 对标 :86-92） ==================== */

/**
 * @brief      向焦点对象发送合成按键（对标 sendKeyClick）。
 * @details    经公共信号 keyEventRequested(key,text,modifiers) 落地面
 *             板写入链（上下文零绑定契约）；无面板连接时丢弃。
 * @param      self 上下文对象；可为 NULL。
 * @param      key 键值。
 * @param      text 键文本（UTF-8 借用；可为 NULL）。
 * @param      modifiers 修饰位（可按位组合）。
 * @return     无返回值；self 为 NULL 或无面板连接时按键丢弃。
 */
void XVirtualKeyboardInputContext_sendKeyClick(
        XVirtualKeyboardInputContext* self, int key, const char* text,
        uint32_t modifiers);

/**
 * @brief      提交当前组串（对标 commit()；组串非空时经
 *             commitRequested 落地并清组串）。
 * @param      self 上下文对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；组串为空或无面板连接时仅清组串不落地。
 */
void XVirtualKeyboardInputContext_commit(
        XVirtualKeyboardInputContext* self);

/**
 * @brief      提交指定文本（对标 commit(text, replaceFrom,
 *             replaceLength)）。
 * @details    【commit 落地契约】经公共信号 commitRequested(text) 由
 *             已持 m_target 的默认面板执行既有写入链；replaceFrom/
 *             replaceLength 无选区替换基建，登记后忽略（scopeOut）。
 * @param      self 上下文对象；可为 NULL。
 * @param      text 提交文本（UTF-8 借用，函数不取得所有权；NULL 按
 *             空串处理）。
 * @return     无返回值；self 为 NULL 或无面板连接时文本丢弃。
 */
void XVirtualKeyboardInputContext_commit_2(
        XVirtualKeyboardInputContext* self, const char* text);

/**
 * @brief      清空组串（对标 clear；组串非空时发 preeditTextChanged）。
 * @param      self 上下文对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XVirtualKeyboardInputContext_clear(XVirtualKeyboardInputContext* self);

/**
 * @brief      在焦点对象上设置选区（对标 setSelectionOnFocusObject）。
 * @details    无选区基建（scopeOut）：登记入参后空操作。
 * @param      self 上下文对象；可为 NULL。
 * @param      anchorPos 锚点位置（控件局部坐标借用；可为 NULL）。
 * @param      cursorPos 光标位置（控件局部坐标借用；可为 NULL）。
 * @return     无返回值。
 */
void XVirtualKeyboardInputContext_setSelectionOnFocusObject(
        XVirtualKeyboardInputContext* self, const XPointF* anchorPos,
        const XPointF* cursorPos);

/* ==================== 信号（17 个无参对标 :94-111 + 2 扩展） ==================== */

/**
 * @brief      preeditTextChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_preeditTextChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      inputMethodHintsChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_inputMethodHintsChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      surroundingTextChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_surroundingTextChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      selectedTextChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_selectedTextChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      anchorPositionChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_anchorPositionChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      cursorPositionChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_cursorPositionChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      anchorRectangleChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_anchorRectangleChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      cursorRectangleChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_cursorRectangleChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      shiftActiveChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_shiftActiveChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      capsLockActiveChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_capsLockActiveChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      uppercaseChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_uppercaseChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      animatingChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_animatingChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      localeChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_localeChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      inputItemChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_inputItemChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      selectionControlVisibleChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_selectionControlVisibleChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      anchorRectIntersectsClipRectChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_anchorRectIntersectsClipRectChanged_signal(
        XVirtualKeyboardInputContext* self);
/**
 * @brief      cursorRectIntersectsClipRectChanged() 信号标识。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_cursorRectIntersectsClipRectChanged_signal(
        XVirtualKeyboardInputContext* self);

/**
 * @brief      【XGui 扩展】commitRequested(const char* text) 信号标识
 *             ——commit 落地契约公共面：默认面板连接后经既有写入链落
 *             m_target；无连接即丢弃（文档化）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @param      text 信号负载占位（与信号签名对齐；getter 不解引用）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_commitRequested_signal(
        XVirtualKeyboardInputContext* self, const char* text);
/**
 * @brief      【XGui 扩展】keyEventRequested(int key, const char* text,
 *             uint32_t modifiers) 信号标识——虚键/合成键落地契约公
 *             共面（同上）。
 * @param      self 上下文对象借用指针；可为 NULL。
 * @param      key 信号负载占位（与信号签名对齐；getter 不解引用）。
 * @param      text 信号负载占位（与信号签名对齐；getter 不解引用）。
 * @param      modifiers 信号负载占位（与信号签名对齐；getter 不解引
 *             用）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputContext_keyEventRequested_signal(
        XVirtualKeyboardInputContext* self, int key, const char* text,
        uint32_t modifiers);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDINPUTCONTEXT_H */
