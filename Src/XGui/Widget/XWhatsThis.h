/**
 * @file       XWhatsThis.h
 * @brief      「What's This?」帮助模式（对标 Qt 6.8 QWhatsThis 全部静态 API）。
 * @details    功能范围（逐项对标 qwhatsthis.h:21-34 的公开面）：
 *             - `enterWhatsThisMode` / `inWhatsThisMode` /
 *               `leaveWhatsThisMode`——进入/查询/退出帮助模式；进入与退出
 *               都会向**每一个顶层控件**投递
 *               `XEVENT_TYPE_ENTER_WHATS_THIS_MODE` /
 *               `XEVENT_TYPE_LEAVE_WHATS_THIS_MODE`
 *               （qwhatsthis.cpp:491-522 的 `notifyToplevels`）；
 *             - `showText` / `hideText`——在指定全局坐标显示/销毁帮助浮层
 *               （qwhatsthis.cpp:432-503 / :648-673）；
 *             - `createAction`——返回勾选式动作，触发即进入帮助模式
 *               （qwhatsthis.cpp:444-478 的 `QWhatsThisAction`）；
 *             - 模式内的输入消费：左键按下把 `XEVENT_TYPE_WHATS_THIS`
 *               投给命中控件并**吞掉该次点击**（qwhatsthis.cpp:355-372），
 *               Esc 退出（:420-445），右键与 `Shift+F10` 放行（上下文
 *               菜单优先）。
 *             - `Shift+F1` 直显当前焦点控件的帮助文本
 *               （qwidget.cpp:9029-9036 的 QWidget::event 尾部）。
 * @note       为什么需要一整套：此前框架只有 `XWidget_setWhatsThis`
 *             文本存取（`XWidget.h`）与 `XDialog` 右键菜单直投一个**无载
 *             荷**的 `XEVENT_TYPE_WHATS_THIS`（`XDialog.c:1140-1218`，
 *             其注释自述「帮助文本的浮层展示属 QWhatsThis 子系统，见
 *             deferred 台账」），**没有任何事件处理者**——即「选了不兑现」。
 *             本模块把「进入模式 → 命中控件 → 浮层展示 → 退出」补全；
 *             事件载荷见 `Src/XCore/XCode/XEvent/XWhatsThisEvent.h`。
 * @note       模块沿用 XGuiConfig.h 既有控件开关模式：**不新增开关**，直接
 *             在 XWIDGET_ON 下编译（与 XToolTip 同款）。浮层与文本渲染在
 *             `XLABEL_ON && XFRAME_ON` 时用 XLabel，否则退化为普通顶层
 *             XWidget（仅记录状态、不渲染文本——与 XToolTip 的退化口径一致）。
 * @author     XinYueC 团队
 */
#ifndef XWHATSTHIS_H
#define XWHATSTHIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XString.h"
#include "XWidget.h"
#include "XGeometry.h"

#if XWIDGET_ON

/* ==================== 模式（对标 QWhatsThis 的模式三件套） ==================== */

/**
 * @brief      进入「What's This?」帮助模式（对标
 *             QWhatsThis::enterWhatsThisMode，qwhatsthis.cpp:491-500）。
 * @details    **幂等**：已在模式内时直接返回（Qt 的
 *             `if (QWhatsThisPrivate::instance) return;`）。首次进入时：
 *             ① 安装进程级事件过滤器（等价 Qt 的
 *             `qApp->installEventFilter(this)`，:381）；
 *             ② 压入 WhatsThis 覆盖光标（:389 的
 *             `setOverrideCursor(Qt::WhatsThisCursor)`）；
 *             ③ 向每个顶层控件投递 `XEVENT_TYPE_ENTER_WHATS_THIS_MODE`
 *             （:496-498 的 notifyToplevels）。
 *             之后左键点击控件即显示其帮助文本并退出模式；Esc 或任意
 *             非修饰键同样退出。
 * @return     无返回值。
 */
void XWhatsThis_enterWhatsThisMode(void);

/**
 * @brief      查询是否处于帮助模式（对标 QWhatsThis::inWhatsThisMode）。
 * @return     在模式内返回 true。
 */
bool XWhatsThis_inWhatsThisMode(void);

/**
 * @brief      退出帮助模式（对标 QWhatsThis::leaveWhatsThisMode，
 *             qwhatsthis.cpp:512-522）。
 * @details    不在模式内时为空操作（Qt：`delete instance` 对空指针安全，
 *             但随后仍会发 Leave 事件——**只在确曾进入时**本实现才发，
 *             避免开机期无意义的顶层广播）。退出时：摘事件过滤器、弹出
 *             覆盖光标、向每个顶层控件投递
 *             `XEVENT_TYPE_LEAVE_WHATS_THIS_MODE`。
 *             **不动**已显示的浮层（Qt 同：showText 先 leave 再 say）。
 * @return     无返回值。
 */
void XWhatsThis_leaveWhatsThisMode(void);

/* ==================== 浮层（对标 showText / hideText） ==================== */

/**
 * @brief      在全局坐标显示帮助浮层（对标 QWhatsThis::showText，
 *             qwhatsthis.cpp:648-665）。
 * @details    Qt 语义：先 `leaveWhatsThisMode()`，再创建专用弹层
 *             （`QWhatsThat`，Qt::Popup + WA_DeleteOnClose）并把文本画进去。
 *             本实现：退出模式 → 复用/新建一个顶层浮层（窗口类型取
 *             `XWindowType_ToolTip`，与 XToolTip 同款走平台层的瞬态弹层
 *             visual/depth 修复路径）→ 按 `x/y` 与 `w` 的几何落位（优先
 *             控件下方 2px，放不下改其上方，最后钳制进屏幕工作区）→
 *             show + raise + 首帧上屏。
 *             **浮层不抢焦点**（不调用 setFocus），也不改当前焦点对象。
 *             **空文本不显示**（Qt `say()` 的 `text.size() == 0` 早退）。
 * @param      x 期望的全局 X（浮层按控件居中对齐时会覆盖它）。
 * @param      y 期望的全局 Y。
 * @param      text 帮助文本借用指针；NULL 或零长度按空文本处理。
 * @param      w 关联控件借用指针（用于落位与换行宽度参考）；可为 NULL。
 * @return     无返回值。
 */
void XWhatsThis_showText(int x, int y, const XString* text, XWidget* w);

/**
 * @brief      显示帮助浮层（UTF-8 兼容重载，转发主版本）。
 * @param      x 期望的全局 X。
 * @param      y 期望的全局 Y。
 * @param      utf8 以 '\0' 结尾的 UTF-8 文本；NULL/空串按空文本处理。
 * @param      w 关联控件借用指针；可为 NULL。
 * @return     无返回值。
 */
void XWhatsThis_showText_2(int x, int y, const char* utf8, XWidget* w);

/**
 * @brief      销毁当前帮助浮层（对标 QWhatsThis::hideText，
 *             qwhatsthis.cpp:670-673）。
 * @details    与 XToolTip 的 300ms 淡出不同：Qt 的 `delete
 *             QWhatsThat::instance` 是**立即**销毁（无 hideTimer），
 *             故本实现同步隐藏并清空文本。无浮层时为空操作。
 * @return     无返回值。
 */
void XWhatsThis_hideText(void);

/**
 * @brief      查询帮助浮层当前是否可见。
 * @return     可见返回 true。
 * @note        Qt 无同名公开接口（QWhatsThis 不暴露浮层状态）；本接口是
 *             对齐取证/测试所需的只读查询，语义与
 *             `XToolTip_isVisible` 同款。
 */
bool XWhatsThis_isTextVisible(void);

/**
 * @brief      取当前帮助文本的**拷贝**（对标浮层内容查询）。
 * @return     新建 XString 拷贝，调用方拥有，用完 XClassDelete；无文本
 *             返回 NULL。
 */
XString* XWhatsThis_text(void);

/**
 * @brief      解析并显示**指定控件**的帮助文本（直显入口）。
 * @details    解析顺序与模式内点击一致（xwt_resolveHelp）：命中控件为
 *             页签条（XTABBAR_ON）时按页签命中取 `tabWhatsThis`，否则
 *             沿父链上溯取控件级 `whatsThis`；解析到即
 *             `showText(控件全局原点, text, target)`。
 *             用途：①框架既有「What's This?」右键菜单动作的直接出口
 *             （XDialog.c:1166 此前直投无载荷 WHATS_THIS 事件且全仓无
 *             处理者＝死路）；②应用层 Shift+F1 直显（对标
 *             qwidget.cpp:9029-9036 的 per-控件帮助，Qt 在
 *             QWidget::event 里实现，框架改为显式入口）。
 * @param      widget 目标控件；NULL 返回 false。
 * @return     解析到文本并显示返回 true；无文本返回 false（不显示、
 *             不改变焦点——B13 验收 ②）。
 */
bool XWhatsThis_showTextForWidget(XWidget* widget);

/**
 * @brief      控件层帮助事件处理入口（供 XWidget::event 的两条分支调用）。
 * @details    对标 `QWidget::event` 的两个分支（qwidget.cpp:9330-9341）：
 *             - `case QEvent::WhatsThis:`——控件自身有文本则
 *               `QWhatsThis::showText(event->globalPos(), d->whatsThis, this)`，
 *               否则 `event->ignore()`（交给 QApplication::notify 上抛父
 *               控件，qapplication.cpp:2962-2986）；
 *             - `case QEvent::QueryWhatsThis:`——无文本则 `ignore()`，
 *               有文本则保持接受（Qt 用接受状态决定光标形态）。
 *             本函数把这两条**同源**实现成一处，XWidget 只需按事件类型
 *             转发，避免在两个分支里各写一份文本/坐标处理。
 *             `event` 必须是 `XHelpEvent`（携带 pos/globalPos）；
 *             若是**裸 XEvent**（框架既有 XDialog.c 右键菜单路径就是这样
 *             投的），坐标回落到控件自身原点，文本展示行为不变。
 * @param      self 接收事件的控件；NULL 时返回 false。
 * @param      event 事件（借用，不取得所有权）；NULL 返回 false。
 * @return     true 表示已处理（含 ignore 情形）；false 表示不是这两种
 *             帮助事件，调用方应继续自己的分派。
 */
bool XWhatsThis_handleWidgetEvent(XWidget* self, XEvent* event);

/* ==================== 动作（对标 createAction） ==================== */

/**
 * @brief      创建触发帮助模式的勾选式动作（对标
 *             QWhatsThis::createAction，qwhatsthis.cpp:686-700 →
 *             `QWhatsThisAction`）。
 * @details    Qt 的该动作：勾选式（checkable）、触发即
 *             `enterWhatsThisMode()`、快捷键 `Shift+F1`。Qt 还给它设了
 *             问号图标（XPM）；本框架无内建 XPM 图标资源，**不设图标**
 *             （XAction 的 icon 是文件路径，无对应内建资源可指）。
 *             `XAction` 当前没有 `setShortcut` 公开接口，故快捷键**不
 *             在此设置**——`Shift+F1` 的等价入口由本模块的事件过滤器
 *             直接兑现（见 enterWhatsThisMode 注），行为一致。
 * @param      parent 父对象借用指针（动作挂到其父子链）；可为 NULL。
 * @return     新建动作，调用方拥有（挂在 parent 下时随 parent 释放）；
 *             失败返回 NULL。
 */
XAction* XWhatsThis_createAction(XObject* parent);

#endif /* XWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XWHATSTHIS_H */
