/******************************************************************************
 * @file       XWizard.h
 * @brief      XWizard 向导对话框 + XWizardPage 向导页控件
 *             （对标 Qt 6.8 QWizard / QWizardPage 核心公共 API）。
 * @details    功能范围：
 *             - XWizardPage：setTitle/subTitle、isComplete/setComplete、
 *               initializePage/cleanupPage/validatePage/nextId/isComplete
 *               五虚槽
 *               （class_init 注册默认实现，分派函数查虚表调用）、
 *               wizard() 所属向导查询；
 *             - XWizard（继承 XDialog）：addPage/setPage/removePage/
 *               page/pageIds/currentPage/currentId/currentIdChanged、
 *               next()/back()/restart()、setStartId/startId、
 *               hasVisitedPage/visitedIds、setWizardStyle/wizardStyle、
 *               setOption/testOption/setOptions/options、
 *               setButtonText/buttonText、button(WizardButton)；
 *             - 内部导航：上一页/下一页/完成/取消 四按钮（XPushButton）；
 *             - 布局：顶部标题区 + 中间内容区(page widget) + 底部按钮区。
 * @note       模块总开关 XWIZARD_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWIZARD_H
#define XWIZARD_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XDialog.h"
#include "XString.h"
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h" /* P0-46/47：标题/副标题由内建 XLabel 承载（AutoText ⇒
                     * 富文本子集），同 XErrorMessage::m_textLabel 的 P0-E8
                     * 先例——文本解析与 AutoText 探测与 XLabel 同源。 */
#endif
#if XABSTRACTBUTTON_ON
/* 账本 247 修复：XPUSHBUTTON_ON=0 而 XABSTRACTBUTTON_ON=1 时，本文件与
 * .c 都要用 XAbstractButton_clicked_signal/setText 等真实声明——此前只有
 * struct 前向声明，该配置下 XWizard.c 报 "XAbstractButton undeclared"。 */
#include "XAbstractButton.h"
#endif
#if XPUSHBUTTON_ON
#include "XPushButton.h"
#endif

#if XWIDGET_ON && XDIALOG_ON && XWIZARD_ON

/** @brief 向导样式（对标 QWizard::WizardStyle，数值一致）。 */
typedef enum XWizardStyle
{
    XWizardStyle_ClassicStyle = 0,
    XWizardStyle_ModernStyle = 1,
    XWizardStyle_MacStyle = 2,
    XWizardStyle_AeroStyle = 3,
    XWizardStyle_NStyles = 4   /**< 样式个数上界（对标 QWizard::NStyles）。 */
} XWizardStyle;

/** @brief 向导选项（对标 QWizard::WizardOption，数值逐项一致）。 */
typedef enum XWizardOption
{
    XWizardOption_IndependentPages             = 0x00000001,
    XWizardOption_IgnoreSubTitles               = 0x00000002,
    XWizardOption_ExtendedWatermarkPixmap       = 0x00000004,
    XWizardOption_NoDefaultButton                = 0x00000008,
    XWizardOption_NoBackButtonOnStartPage        = 0x00000010,
    XWizardOption_NoBackButtonOnLastPage         = 0x00000020,
    XWizardOption_DisabledBackButtonOnLastPage   = 0x00000040,
    XWizardOption_HaveNextButtonOnLastPage      = 0x00000080,
    XWizardOption_HaveFinishButtonOnEarlyPages  = 0x00000100,
    XWizardOption_NoCancelButton                  = 0x00000200,
    XWizardOption_CancelButtonOnLeft              = 0x00000400,
    XWizardOption_HaveHelpButton                   = 0x00000800,
    XWizardOption_HelpButtonOnRight                = 0x00001000,
    XWizardOption_HaveCustomButton1               = 0x00002000,
    XWizardOption_HaveCustomButton2               = 0x00004000,
    XWizardOption_HaveCustomButton3               = 0x00008000,
    XWizardOption_NoCancelButtonOnLastPage       = 0x00010000
} XWizardOption;

/** @brief 向导按钮枚举（对标 QWizard::WizardButton，数值逐项一致）。
 *  @details 0..5 为六个标准按钮槽（NStandardButtons==6，未设自定义文本
 *           时回落随样式变化的缺省文本）；6..8 为自定义按钮槽（构造即
 *           预建，也可经 setButton 登记调用方借用按钮替换；setButtonText/
 *           buttonText 对 0..8 全部受理——Qt 的 buttonText 对自定义槽
 *           回落按钮自身文本）；9 为布局弹簧占位
 *           （Stretch，仅 setButtonLayout 语义用）；NoButton==-1 表示
 *           "无此槽位"。 */
typedef enum XWizardButton
{
    XWizardButton_BackButton = 0,
    XWizardButton_NextButton = 1,
    XWizardButton_CommitButton = 2,
    XWizardButton_FinishButton = 3,
    XWizardButton_CancelButton = 4,
    XWizardButton_HelpButton = 5,
    XWizardButton_CustomButton1 = 6,
    XWizardButton_CustomButton2 = 7,
    XWizardButton_CustomButton3 = 8,
    XWizardButton_Stretch = 9,
    XWizardButton_NoButton = -1,
    XWizardButton_NStandardButtons = 6,
    XWizardButton_NButtons = 9
} XWizardButton;

/** @brief 向导图片槽位（对标 QWizard::WizardPixmap，数值一致）。
 *  @details 本库 setPixmap/pixmap 以 XString 路径作四槽独立承载
 *           （Watermark/Logo/Banner/Background，对标 qwizard.cpp:
 *           3485-3515）；which 取值即本枚举，未设槽位读取时回落向导
 *           同槽、二者皆空返回 NULL。 */
typedef enum XWizardPixmap
{
    XWizardPixmap_WatermarkPixmap = 0,
    XWizardPixmap_LogoPixmap = 1,
    XWizardPixmap_BannerPixmap = 2,
    XWizardPixmap_BackgroundPixmap = 3,
    XWizardPixmap_NPixmaps = 4
} XWizardPixmap;

/** @brief XAbstractButton 前向声明（按钮族 API 的借用承载；完整定义见
 *         XAbstractButton.h；XPUSHBUTTON_ON 关闭时仍可登记/查询借用
 *         指针，不依赖完整类型）。 */
struct XAbstractButton;

/* ==================== XWizardPage ==================== */

/**
 * @brief XWizardPage 虚函数表枚举。
 * @details 5 个新槽位从 XCLASS_VTABLE_GET_SIZE(XWidget) 开始追加，
 *          分别对标 QWizardPage 的公开虚函数 initializePage /
 *          cleanupPage / validatePage / nextId / isComplete；默认实现由
 *          XWizardPage_class_init 注册：initializePage 为空操作；
 *          cleanupPage 把本页登记字段恢复为初值（Qt 6.8 默认，
 *          qwizard.cpp:3550-3569）；validatePage 返回 true（对标 Qt 默认）；
 *          nextId 无所属向导返回 -1，有则返回**下一页 id**（末页 -1）；
 *          isComplete 返回"完备位 && 无必填字段仍为初值"（Qt 6.8 默认
 *          必填分支，qwizard.cpp:3598-3629）。
 *          调用统一经 XWizardPage_initializePage 等 _分派_ 函数查表，
 *          子类用 XVTABLE_OVERLOAD_DEFAULT 覆盖即可生效。
 * @note  IsComplete 追加在末位（不改既有 4 槽编号），与 Qt
 *          `virtual bool isComplete() const` 同义：向导的
 *          Next/Finish 使能与 XWizard_validateCurrentPage 都经虚表取
 *          当前实现（qwizard.cpp:3598-3629、1631-1638）。默认实现 =
 *          `setComplete` 完备位 && 本页无"必填字段仍为初值"
 *          （必填由 XWizardPage_registerField 的字段名末尾 `*` 标记）。
 */
XCLASS_DEFINE_BEGING(XWizardPage)
XCLASS_DEFINE_ENUM(XWizardPage, InitializePage) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_ENUM(XWizardPage, CleanupPage),
XCLASS_DEFINE_ENUM(XWizardPage, ValidatePage),
XCLASS_DEFINE_ENUM(XWizardPage, NextId),
XCLASS_DEFINE_ENUM(XWizardPage, IsComplete),
XCLASS_DEFINE_END(XWizardPage)

typedef struct XWizardPage
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    XString* m_title;     /**< 页面标题（对象拥有）。 */
    XString* m_subTitle;  /**< 页面子标题（对象拥有）。 */
    XString* m_pixmaps[XWizardPixmap_NPixmaps];
                          /**< 页面图片路径，按 Watermark/Logo/Banner/
                               Background 四槽独立存储（对象拥有；NULL=该槽
                               未设，读取时回落向导同槽；对标
                               QWizardPage::setPixmap(which,...) 的
                               d->pixmaps[NPixmaps]，qwizard.cpp:3485-3515；
                               r2 对齐，账本 244/292）。 */
    XString* m_buttonTexts[XWizardButton_NStandardButtons];
                          /**< 自定义按钮文本（对标 QWizardPage::setButtonText；
                               对象拥有；NULL 表示用向导默认文本）。 */
    struct XWizard* m_wizard; /**< 所属向导（借用指针；由 addPage/setPage/
                                   removePage 维护；对标 QWizardPage 私有
                                   d->wizard，供 nextId 默认实现定位）。 */
    /* [内存治理 2026-10-11] 标志位字：4 个 bool 原占 4B（含对齐洞），并入 1 字。 */
    uint32_t m_complete : 1;   /**< 是否完成（默认 true）。 */
    uint32_t m_commitPage : 1; /**< 是否提交页（对标 QWizardPage::isCommitPage）。 */
    uint32_t m_finalPage : 1;  /**< 是否强制末页（对标 QWizardPage::isFinalPage
                                    的显式值；仅在 m_finalPageSet 置位时生效）。 */
    uint32_t m_finalPageSet : 1;/**< setFinalPage(true) 的显式末页位（对标 Qt
                                    d->explicitlyFinal；false 时回落
                                    nextId()==-1 推导，即"非强制末页"）。 */
    uint32_t m_initialized : 1;/**< 首次进入标记（对标 QWizardPage 私有
                                   d->initialized；true 表示已调用过
                                   initializePage，前进导航不再重复触发）。 */
    uint32_t m_lastComplete : 1;/**< 最近一次评估/发射的完备态（对标
                                   QWizardPagePrivate::completeState）。 */
    uint32_t m_lastCompleteValid : 1;/**< m_lastComplete 是否有效（首评必发）。 */
    uint32_t m_wizardPageFlagsUnused : 25; /**< 保留位。 */
} XWizardPage;

/** @brief XWizard页classinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XWizardPage_class_init(void);
/** @brief XWizard页init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XWizardPage_init(XWizardPage* self, XWidget* parent, XWidgetFlags flags);
#define XWizardPage_create(parent, flags) XWizardPage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XWizard页createex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWizardPage* XWizardPage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
/** @brief completeChanged() 信号（对标 QWizardPage::completeChanged；
 *         页完成状态变化时由 setComplete/validatePage 触发，Task 2.6 接线）。 */
void* XWizardPage_completeChanged_signal(XWizardPage* self);

/** @brief XWizard页set标题（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param utf8 UTF-8 文本。
 * @return 无返回值。
 */
void XWizardPage_setTitle(XWizardPage* self, const char* utf8);
/** @brief XWizard页title（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XWizardPage_title(const XWizardPage* self);
/** @brief XWizard页set子标题（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param utf8 UTF-8 文本。
 * @return 无返回值。
 */
void XWizardPage_setSubTitle(XWizardPage* self, const char* utf8);
/** @brief XWizard页sub标题（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XWizardPage_subTitle(const XWizardPage* self);
/** @brief XWizard页setComplete（本库扩展；对标 QWizardPage 无此成员）。
 * @details Qt 6.8 的 QWizardPage **没有 setComplete**：完备性由
 *          `virtual bool isComplete() const`（qwizard.cpp:3598）决定，
 *          状态变化由子类发射 completeChanged()。本库为免去 C 侧必须
 *          派生才能改完备性的负担，提供本快捷 setter 写入完备位并发射
 *          completeChanged()；`isComplete` 虚槽默认实现即读该位
 *          （子类重载仍优先，见 XWizardPage_isComplete）。
 * @param self 目标控件指针。
 * @param complete bool 参数。
 * @return 无返回值。
 */
void XWizardPage_setComplete(XWizardPage* self, bool complete);

/* -------------------- 五虚槽分派（对标 QWizardPage 公开虚函数） -------------------- */

/**
 * @brief      虚槽分派：initializePage（对标 QWizardPage::initializePage）。
 * @details    查对象虚表 EXWizardPage_InitializePage 槽位调用当前实现；
 *             默认实现为空操作。由向导在页面首次进入（前进/重启）时自动
 *             调用，无需手动触发。
 * @param      self 目标页面指针；传入 NULL 或虚表未绑定时忽略。
 * @return     无返回值。
 */
void XWizardPage_initializePage(XWizardPage* self);
/**
 * @brief      虚槽分派：cleanupPage（对标 QWizardPage::cleanupPage）。
 * @details    查对象虚表 EXWizardPage_CleanupPage 槽位调用当前实现；
 *             默认实现为空操作。由向导在经 back() 离开页面时自动调用
 *             （IndependentPages 选项开启时跳过）。
 * @param      self 目标页面指针；传入 NULL 或虚表未绑定时忽略。
 * @return     无返回值。
 */
void XWizardPage_cleanupPage(XWizardPage* self);
/**
 * @brief      虚槽分派：validatePage（对标 QWizardPage::validatePage）。
 * @details    查对象虚表 EXWizardPage_ValidatePage 槽位调用当前实现；
 *             默认实现返回 true（对标 Qt 6.8.3）。next()/Finish 流程中
 *             经 XWizard_validateCurrentPage 间接调用。
 * @param      self 目标页面指针；传入 NULL 或虚表未绑定时返回 true。
 * @return     校验通过返回 true；false 时 next() 停留在当前页。
 */
bool XWizardPage_validatePage(XWizardPage* self);
/**
 * @brief      虚槽分派：nextId（对标 QWizardPage::nextId）。
 * @details    查对象虚表 EXWizardPage_NextId 槽位调用当前实现。默认实现
 *             仿 Qt：无所属向导返回 -1；否则返回本页在向导中的下一页
 *             索引（末页返回 -1）。子类重写可定制动态页序。
 * @param      self 目标页面指针；传入 NULL 或虚表未绑定时返回 -1。
 * @return     下一页索引；无后续页返回 -1。
 */
int XWizardPage_nextId(const XWizardPage* self);
/**
 * @brief      虚槽分派：isComplete（对标 QWizardPage::isComplete）。
 * @details    查对象虚表 EXWizardPage_IsComplete 槽位调用当前实现；默认
 *             实现返回页的完备位（缺省 true）。向导的 Next/Finish 使能
 *             （XWizard 内部 updateButtons）与 validateCurrentPage 均经
 *             本函数取值，子类覆盖后立即生效（Qt 同：qwizard.cpp:
 *             1631-1638 读 currentPage()->isComplete()）。
 * @param      self 目标页面指针；传入 NULL 或虚表未绑定时返回 false。
 * @return     完备返回 true。
 */
bool XWizardPage_isComplete(const XWizardPage* self);
/**
 * @brief      查询所属向导（对标 QWizardPage::wizard）。
 * @details    返回 addPage/setPage 登记的向导借用指针；removePage 时
 *             置回 NULL。
 * @param      self 目标页面指针；传入 NULL 时返回 NULL。
 * @return     所属向导借用指针；未加入任何向导返回 NULL；禁止释放。
 */
struct XWizard* XWizardPage_wizard(const XWizardPage* self);

/** @brief 设置页面自定义按钮文本（对标 QWizardPage::setButtonText）。
 * @details 覆盖向导级按钮文本的页面级显示；当前页设过页级文本时按钮行
 *          以页级文本为准（对标 QWizardPrivate::updateButtonTexts 的
 *          页级分支，qwizard.cpp:1392-1404）。XWizardPage_buttonText 的
 *          读取口径同 Qt：页级未设时回落向导级文本。
 * @param self 目标页面指针；传入 NULL 或 which 越界时忽略。
 * @param which 按钮枚举（XWizardButton）。
 * @param utf8 按钮文本（UTF-8）；NULL 恢复默认文本。
 * @return 无返回值。
 */
void XWizardPage_setButtonText(XWizardPage* self, XWizardButton which,
                               const char* utf8);
/** @brief 查询页面自定义按钮文本（对标 QWizardPage::buttonText）。
 * @details 对齐 Qt（qwizard.cpp:3742-3752）：先取页级自定义文本；未设
 *          则回落所属向导的 XWizard_buttonText（含向导级自定义文本与
 *          缺省文本）；无向导返回空串。
 * @param self 目标页面指针；传入 NULL 或 which 越界时返回空串。
 * @param which 按钮枚举（XWizardButton）。
 * @return 文本（UTF-8）；借用内部缓存/向导缓存，禁止释放。
 */
const char* XWizardPage_buttonText(const XWizardPage* self,
                                   XWizardButton which);
/** @brief 标记为提交页（对标 QWizardPage::setCommitPage）。
 * @details 存储标志；当前页为提交页时按钮行用 Commit 槽替代 Next
 *          （对标 QWizardPrivate::updateButtonStates 的 commitPage 分支）。
 * @param self 目标页面指针；传入 NULL 时忽略。
 * @param commitPage true 设为提交页。
 * @return 无返回值。
 */
void XWizardPage_setCommitPage(XWizardPage* self, bool commitPage);
/** @brief 查询是否提交页（对标 QWizardPage::isCommitPage）。
 * @param self 目标页面指针；传入 NULL 时返回 false。
 * @return 提交页返回 true。
 */
bool XWizardPage_isCommitPage(const XWizardPage* self);
/** @brief 标记为强制末页（对标 QWizardPage::setFinalPage）。
 * @details 对齐 Qt 的 explicitlyFinal 语义（qwizard.cpp:3643-3660）：
 *          setFinalPage(true) 后 isFinalPage 恒 true；setFinalPage(false)
 *          后**回落推导**（nextId()==-1 即末页），不是"强制非末页"。
 * @param self 目标页面指针；传入 NULL 时忽略。
 * @param finalPage true 设为强制末页。
 * @return 无返回值。
 */
void XWizardPage_setFinalPage(XWizardPage* self, bool finalPage);
/** @brief 查询是否末页（对标 QWizardPage::isFinalPage 三态语义）。
 * @details 显式 setFinalPage(true) 过则返回 true；否则按 Qt 规则推导：
 *          nextId() 返回 -1 即为末页（含**非当前页**的末页——Qt
 *          对非当前页走 `nextId() == -1` 分支，qwizard.cpp:3664-3680）。
 *          **未加入向导的独立页**同样走 `nextId() == -1`（默认实现无
 *          向导返回 -1）⇒ 返回 true（本机 Qt 6.8.3 实测 standalone
 *          page isFinalPage()==1；r2 对齐，账本 239）。
 * @param self 目标页面指针；传入 NULL 时返回 false。
 * @return 末页返回 true。
 */
bool XWizardPage_isFinalPage(const XWizardPage* self);
/** @brief 设置页面图片（对标 QWizardPage::setPixmap；XString 主版本）。
 * @details 四槽独立存储（Watermark/Logo/Banner/Background），读取时
 *          按"页图非空 → 向导同槽"回落（qwizard.cpp:3485-3515）。
 * @param self 目标页面指针；传入 NULL 时忽略。
 * @param which 图片类型（XWizardPixmap；越界忽略）。
 * @param path 图片路径借用指针；NULL 清除该槽。
 * @return 无返回值。
 */
void XWizardPage_setPixmap(XWizardPage* self, int which, const XString* path);
/** @brief 设置页面图片（UTF-8 路径兼容重载）。
 * @param self 目标页面指针；传入 NULL 时忽略。
 * @param which 图片类型（XWizardPixmap；越界忽略）。
 * @param utf8 图片路径（UTF-8）；NULL 清除该槽。
 * @return 无返回值。
 */
void XWizardPage_setPixmap_2(XWizardPage* self, int which, const char* utf8);
/** @brief 查询页面图片路径（对标 QWizardPage::pixmap）。
 * @details 对齐 Qt 的回落链（qwizard.cpp:3503-3515）：本页该槽非空则
 *          返回本页图；否则返回所属向导的 XWizard_pixmap(which)；无向导
 *          且未设置返回 NULL。
 * @param self 目标页面指针；传入 NULL 时返回 NULL。
 * @param which 图片类型（XWizardPixmap；越界返回 NULL）。
 * @return 借用内部 XString 指针；未设置返回 NULL；禁止释放或修改。
 */
const XString* XWizardPage_pixmap(const XWizardPage* self, int which);
/** @brief 读取向导字段（对标 QWizardPage::field；Qt 侧为 protected，
 *         本库开放为普通函数以便 C 调用）。
 * @details 等价 field(name)：转调所属向导的 XWizard_field；未加入向导
 *          或字段未登记返回 NULL。
 * @param self 目标页面指针；NULL 时返回 NULL。
 * @param name 字段名（UTF-8）；NULL 时返回 NULL。
 * @return 向导字段表的借用 XString 指针；禁止释放或修改。
 */
const XString* XWizardPage_field(const XWizardPage* self, const char* name);
/** @brief 写入向导字段（对标 QWizardPage::setField）。
 * @details 转调所属向导的 XWizard_setField_2；未加入向导时为空操作。
 * @param self 目标页面指针；NULL 时忽略。
 * @param name 字段名（UTF-8）；NULL 时忽略。
 * @param value 字段值（UTF-8）；NULL 清空该字段。
 * @return 无返回值。
 */
void XWizardPage_setField(XWizardPage* self, const char* name,
                          const char* value);
/** @brief 登记字段（对标 QWizardPage::registerField，qwizard.h:205-208）。
 * @details Qt 语义：字段名**以 `*` 结尾**表示必填（qwizard.cpp:168-177
 *          的 `name.endsWith('*')` 去掉星号并置 mandatory）——
 *          `isComplete()` 对"必填字段仍为初值"返回 false；页面
 *          `cleanupPage()` 默认把本页全部字段恢复为初值
 *          （qwizard.cpp:3550-3569）。登记时捕获初值 = 当前控件属性值。
 *
 *          本库映射（C 无 QMetaObject）：
 *          - widget/property 非空时字段值**实时读写该控件的动态属性**
 *            （XObject_setProperty/XObject_property_const + XString 型
 *            XVariant），等价 Qt 的 QObject::property/setProperty；
 *          - property 为 NULL 时退化为向导字段表内的值（Qt 此时按
 *            className 查 setDefaultProperty 缺省属性表，本库无
 *            QMetaObject 类名反射，需显式给属性名，见 deferred 记录）；
 *          - changedSignal 传信号标识（`XSignal(...)`，0=不连接）。Qt
 *            的 changedSignal 是信号**名**字符串、经元对象连接；C 无
 *            反射，以信号标识等价表达：必填字段的连接用于值变化时
 *            重新评估 completeChanged。
 * @param self 目标页面指针；NULL 时忽略。
 * @param name 字段名（UTF-8）；NULL/空串忽略；末尾 `*` 标记必填。
 * @param widget 值来源控件（借用）；可为 NULL（仅字段表承载）。
 * @param property 属性名（UTF-8 借用）；可为 NULL。
 * @param changedSignal 变化信号标识；0=不连接。
 * @return 无返回值。
 */
void XWizardPage_registerField(XWizardPage* self, const char* name,
                               XWidget* widget, const char* property,
                               size_t changedSignal);

/* ==================== XWizard ==================== */

/**
 * @brief XWizard 虚函数表枚举。
 * @details 两个向导级受保护虚函数槽（对标 QWizard::initializePage(int) /
 *          cleanupPage(int)，qwizard.h:152-154）：子类用
 *          XVTABLE_OVERLOAD_DEFAULT 覆盖后，向导导航会在进入/离开页面时
 *          以**页 id** 调用向导级实现（默认实现转调
 *          XWizardPage_initializePage/cleanupPage，与 Qt 默认一致——
 *          qwizard.cpp 里 QWizard::initializePage(id) 默认就是
 *          currentPage 派发）。r2 对齐，账本 238。
 */
XCLASS_DEFINE_BEGING(XWizard)
XCLASS_DEFINE_ENUM(XWizard, InitializePage) = XCLASS_VTABLE_GET_SIZE(XDialog),
XCLASS_DEFINE_ENUM(XWizard, CleanupPage),
XCLASS_DEFINE_END(XWizard)

#define XWIZARD_MAX_PAGES 16
/** @brief 首选尺寸下限（对标 QWizard::sizeHint 的
 *         `size.expandedTo(QSize(500, 360))`，qwizard.cpp）。 */
#define XWIZARD_SIZEHINT_MIN_WIDTH 500
#define XWIZARD_SIZEHINT_MIN_HEIGHT 360
/** @brief 默认属性登记表容量上限（setDefaultProperty；满后新登记忽略）。 */
#define XWIZARD_MAX_DEFAULT_PROPERTIES 16
/** @brief 字段表容量上限（registerField/setField 登记）。 */
#define XWIZARD_MAX_FIELDS 32
/** @brief 自定义按钮布局序列容量上限（setButtonLayout；Qt 的
 *         QList<WizardButton> 无固定上限，本库定容）。 */
#define XWIZARD_MAX_BUTTON_LAYOUT 16

/** @brief 向导字段项（对标 QWizardPrivate::QWizardField）。
 *  @details name 为去掉必填星号后的字段名；value 是字段表值（UTF-8）；
 *           widget/property 非空时值为控件动态属性的实时镜像（见
 *           XWizardPage_registerField 注）；initialValue 是登记时捕获的
 *           初值（cleanupPage 恢复与必填判空都用它）。 */
typedef struct XWizardFieldEntry
{
    XString* name;         /**< 字段名（对象拥有）。 */
    XString* value;        /**< 字段值（对象拥有，UTF-8 内码）。 */
    XString* property;     /**< 控件属性名（对象拥有；可为 NULL）。 */
    XString* initialValue; /**< 登记时初值（对象拥有）。 */
    XWizardPage* page;     /**< 所属页面（借用；用于 cleanupPage 分页恢复
                                与 isComplete 的必填判定）。 */
    XWidget* widget;       /**< 值来源控件（借用；可为 NULL）。 */
    size_t changedSignal;  /**< 变化信号标识（0=未连接）。 */
    bool mandatory;        /**< 必填（字段名末尾 `*`）。 */
} XWizardFieldEntry;

/** @brief 默认属性登记项（对标 QWizardDefaultProperty）。
 *  @details Qt 用 (className, property, changedSignal) 供 registerField
 *           在不给属性名时按对象类名（含子类）解析；本库 className 用
 *           XObject_className/XObject_inherits 匹配（C 侧等价物），
 *           changedSignal 存信号标识。 */
typedef struct XWizardDefaultProperty
{
    XString* className;    /**< 类名（对象拥有）。 */
    XString* property;     /**< 属性名（对象拥有）。 */
    size_t changedSignal;  /**< 变化信号标识（0=无）。 */
} XWizardDefaultProperty;

typedef struct XWizard
{
    XDialog m_base;             /**< 基类成员；必须是第一个。 */
    /* 指针组（8B 对齐；内嵌指针数组/字段表整体为一个 8B 对齐单元）。 */
    XWizardPage* m_pages[XWIZARD_MAX_PAGES]; /**< 页面数组。 */
    int m_pageIds[XWIZARD_MAX_PAGES]; /**< 与 m_pages 同序的**稳定页 id**
                                （对标 QWizardPrivate::pageMap 的键：Qt 的
                                addPage 分配 max+1 且 removePage 后不回收、
                                不左移——本机 6.8.3 实测 0,1,2 删 1 后再
                                addPage 得 3；r2 对齐，账本 236）。 */
    XWizardFieldEntry m_fields[XWIZARD_MAX_FIELDS]; /**< 字段表（内嵌；name/
                                value/property/initialValue 对象拥有）。 */
    XString* m_buttonTexts[XWizardButton_NButtons]; /**< 自定义按钮文本
                                （对象拥有；P0-43 起九槽全受理 0..8，对标
                                Qt setButtonText 经 ensureButton 对
                                NButtons 内全部槽位受理，qwizard.cpp:2595）。 */
    bool m_buttonTextCustom[XWizardButton_NButtons];
                                /**< 该槽是否**显式设过**文本（对标 Qt 的
                                     buttonCustomTexts 映射：设过即返回存储值，
                                     显式设空也返回空串；未设返回缺省文本）。 */
    struct XAbstractButton* m_buttons[XWizardButton_NButtons];
                                /**< 九个按钮槽（Back/Next/Commit/Finish/
                                    Cancel/Help/Custom1..3）的活动按钮：
                                    构造即预建（对标 QWizardPrivate::init
                                    的 ensureButton 与 updateButtonLayout，
                                    qwizard.cpp:661-664/1460-1490；账本 242），
                                    **由向导拥有并随析构删除**。 */
    struct XAbstractButton* m_customButtons[XWizardButton_NButtons];
                                /**< setButton 登记替换的按钮（借用；NULL=
                                    用预建按钮）。登记按钮展示/接线由向导
                                    负责，但生命周期由调用方管理。 */
    int m_buttonLayoutSeq[XWIZARD_MAX_BUTTON_LAYOUT];
                                /**< setButtonLayout 的自定义布局序列
                                    （XWizardButton 值，含 Stretch）。 */
    int m_history[XWIZARD_MAX_PAGES]; /**< 前进导航历史（栈；存**页 id**，
                                   对标 QWizardPrivate::history）：每次前进
                                   压入旧页 id，back() 弹出目标 id。 */
    XString* m_pixmaps[XWizardPixmap_NPixmaps]; /**< 向导横幅图四槽
                                    （Watermark/Logo/Banner/Background；
                                    对象拥有；对标 QWizard::setPixmap 的
                                    defaultPixmaps[NPixmaps]）。 */
    XWizardDefaultProperty m_defaultProps[XWIZARD_MAX_DEFAULT_PROPERTIES];
                                /**< 默认属性登记表（setDefaultProperty
                                     写入；固定容量，满后忽略新登记）。 */
    XWidget* m_sideWidget;      /**< 侧边控件（借用）。 */
#if XFRAME_ON && XLABEL_ON
    XLabel* m_titleLabel;       /**< 内建标题标签（子控件，向导拥有；
                                     对标 QWizardHeader::titleLabel；
                                     P0-46： setTitleFormat 的 0/1/2 直接映射
                                     XLabelTextFormat，AutoText 探测/富文本
                                     子集与 XLabel 同源不另写解析器；
                                     鼠标穿透，颜色经调色板下发——同
                                     XErrorMessage::m_textLabel 的 P0-E8 先例）。 */
    XLabel* m_subTitleLabel;    /**< 内建副标题标签（子控件，向导拥有；
                                     对标 QWizardHeader::subTitleLabel；
                                     P0-47：格式随 m_subTitleFormat）。 */
#endif
#if XPUSHBUTTON_ON
    XPushButton* m_btnBack;     /**< 上一页按钮（预建）。 */
    XPushButton* m_btnNext;     /**< 下一页按钮（预建）。 */
    XPushButton* m_btnCommit;   /**< 提交按钮（预建；提交页时替代 Next）。 */
    XPushButton* m_btnFinish;   /**< 完成按钮（预建）。 */
    XPushButton* m_btnCancel;   /**< 取消按钮（预建）。 */
    XPushButton* m_btnHelp;     /**< 帮助按钮（预建；HaveHelpButton 控制显隐）。 */
#endif
    int m_options;              /**< 选项位标志（XWizardOption 位集至
                                   0x00010000，setter 往返任意组合，保留 int）。 */
    int m_buttonLayoutCount;    /**< m_buttonLayoutSeq 有效长度（0=用缺省
                                   布局）。 */
    int m_titleFormat;          /**< 标题格式码（对标 setTitleFormat；默认
                                   AutoText==2，本机 Qt 6.8.3 实测）。 */
    int m_subTitleFormat;       /**< 子标题格式码（对标 setSubTitleFormat；
                                   默认 AutoText==2）。 */
    bool m_hasCustomLayout;     /**< setButtonLayout 是否生效（对标
                                   QWizardPrivate::buttonsHaveCustomLayout）。 */
    bool m_visited[XWIZARD_MAX_PAGES]; /**< 已访问标记（与 m_pages 同序）。 */
    /* [内存治理 2026-10-11] 窄整型：页数/索引/计数容量上限
       XWIZARD_MAX_PAGES=16 与字段表 32 条，均 ≤127，uint8_t 足够；
       currentIndex 有 -1 哨兵（XWizard.c:1112 无页面复位），用
       int8_t 保留符号。样式 0..3 收 uint8_t。 */
    uint8_t m_pageCount;        /**< 已添加页面数。 */
    int8_t m_currentIndex;      /**< 当前页索引（-1=无页面，对标
                                     currentId 默认 -1）。 */
    int m_startId;              /**< 起始页 id（对标 QWizardPrivate::start：
                                     维护为**已解析值**——缺省等于首个页 id，
                                     removePage 会随首个页变化调整；显式
                                     setStartId 后保持不变）。 */
    bool m_startSetByUser;      /**< 起始页是否由 setStartId 显式指定
                                     （对标 startSetByUser）。 */
    int m_nextPageId;           /**< 下一个 addPage 分配的页 id（单调递增、
                                    不回收；对标 Qt 的 id 分配）。 */
    uint8_t m_fieldCount;       /**< 字段数。 */
    uint8_t m_defaultPropCount; /**< 默认属性表已用条数（0=空）。 */
    uint8_t m_style;            /**< 向导样式（XWizardStyle 0..3）。 */
    uint8_t m_historyCount;     /**< 历史栈深度（0..XWIZARD_MAX_PAGES）。 */
} XWizard;

/** @brief XWizardclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XWizard_class_init(void);
/** @brief XWizardinit（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XWizard_init(XWizard* self, XWidget* parent, XWidgetFlags flags);
#define XWizard_create(parent, flags) XWizard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XWizardcreateex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWizard* XWizard_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/** @brief XWizardadd页（对标 QWizard::addPage）。
 * @details 对标 Qt 的 id 分配（qwizard.cpp addPage）：新页 id = 当前最大
 *          id + 1（首个 0），**removePage 不回收 id、页表不左移**
 *          （本机 6.8.3 实测 0,1,2 删 1 后再 addPage 得 3；r2 对齐，
 *          账本 236）。
 * @param self 目标控件指针。
 * @param page XWizardPage 参数。
 * @return 新页 id；失败（满/空指针）返回 -1。
 */
int XWizard_addPage(XWizard* self, XWizardPage* page);
/** @brief XWizardset页（对标 QWizard::setPage(id, page)）。
 * @details **id 已存在时整份忽略**（Qt 打印 "Page with duplicate ID
 *          ignored" 后 return，qwizard.cpp setPage）；id 为 -1 拒绝。
 *          新 id 可稀疏（大于现有最大 id 时按该 id 登记）。
 * @param self 目标控件指针。
 * @param id 页 id。
 * @param page XWizardPage 参数。
 * @return 无返回值。
 */
void XWizard_setPage(XWizard* self, int id, XWizardPage* page);
/** @brief XWizardremove页（对标 QWizard::removePage(id)）。
 * @details 按 **id** 移除页面：其余页 id 保持不变（不左移）。
 * @param self 目标控件指针。
 * @param id 页 id。
 * @return 无返回值。
 */
void XWizard_removePage(XWizard* self, int id);
/** @brief XWizardpage（对标 QWizard::page(id)）。
 * @param self 目标控件指针。
 * @param id 页 id。
 * @return 页指针；无此 id 返回 NULL。
 */
XWizardPage* XWizard_page(XWizard* self, int id);
#define XWizard_pageCount(self) XWizard_pageIds((self), NULL, 0)
/** @brief XWizardcurrent页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWizardPage* XWizard_currentPage(const XWizard* self);
/** @brief 查询当前页 id（对标 QWizard::currentId；无当前页返回 -1，
 *         与 Qt 默认 currentId==-1 同口径）。
 * @details P1-29：本体即 Qt 名 currentId；旧名 currentIndex 降为过渡宏
 *          （见 setCurrentId 后的别名块）。
 * @param self 目标控件指针；NULL 时返回 -1。
 * @return 当前页 id；无页面/未进入时 -1。
 */
int XWizard_currentId(const XWizard* self);
/** @brief 跳转到指定页 id（对标 QWizard::setCurrentId；currentId 属性
 *         WRITE 访问器）。
 * @details 按 **id** 解析页（不再当作下标）；切换可见页、登记已访问并
 *          发射 currentIdChanged(id)；未知 id 忽略（Qt 打印警告并忽略）。
 *          前进跳转会把旧页 id 压入导航历史（对标
 *          QWizardPrivate::history），随后的 back() 退到此页。
 *          P1-30：本体即 Qt 名 setCurrentId；旧名 setCurrentIndex 降为
 *          过渡宏。
 * @param self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param id 目标页 id。
 * @return 无返回值。
 */
void XWizard_setCurrentId(XWizard* self, int id);
/** @brief XWizardnext（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XWizard_next(XWizard* self);
/** @brief 后退一页（对标 QWizard::back）。
 * @details 从导航历史栈弹出上一页（不是简单的 index-1）：本机 Qt 6.8.3
 *          实测 0→2 直接跳页后 back() 回到 0。历史为空时无操作；后退
 *          离开页面时按 IndependentPages 选项决定是否调用 cleanupPage。
 * @param self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XWizard_back(XWizard* self);
/** @brief 重启向导（对标 QWizard::restart = reset + 进入起始页）。
 * @details 清空导航历史与访问史、复位各页 initialized，再前进进入
 *          起始页（startIndex==-1 时进首页）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XWizard_restart(XWizard* self);
/** @brief 设置起始页 id（对标 QWizard::setStartId）。
 * @details 语义与 Qt 逐条一致（qwizard.cpp setStartId）：-1 表示回到
 *          "缺省起始页"（= 首个页 id，无页面则 -1）；未知 id 打印警告并
 *          忽略；显式设置后 removePage 会重新调整为首个页
 *          （startSetByUser 复位）。
 * @param self 目标控件指针。
 * @param id 起始页 id（-1=缺省）。
 * @return 无返回值。
 */
void XWizard_setStartId(XWizard* self, int id);
/** @brief 查询起始页 id（对标 QWizard::startId）。
 * @details 返回**已解析**的起始页 id：缺省为首个页 id（无页面 -1），
 *          显式设置后为设置值（本机 Qt 6.8.3 实测三页向导 startId()==0）。
 *          P1-31：本体即 Qt 名 startId；旧名 startIndex 降为过渡宏
 *          （见下）。
 * @param self 目标控件指针；NULL 时返回 -1。
 * @return 起始页 id；无页面返回 -1。
 */
int XWizard_startId(const XWizard* self);
/** @brief XWizardhasVisited页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param id 稳定页 id（非页表下标；对标 QWizard::hasVisitedPage(int id)
 *           按页 id 查询，实现按 id 定位页表项读访问位）。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XWizard_hasVisitedPage(const XWizard* self, int id);
/** @brief 汇总已访问页 id 列表（对标 QWizard::visitedIds）。
 * @details 输出**稳定页 id**（不是页表下标；页删除后 id 不左移，
 *          见 XWizard_addPage 注）；输出按页表顺序（id 单调分配故同
 *          id 升序）。outIds 为 NULL 或 maxCount <= 0 时为"计数查询"
 *          模式，仅返回已访问页总数；否则最多写入 maxCount 个 id 并
 *          返回实际写入个数。
 * @param self 目标向导指针；NULL 时返回 0。
 * @param outIds 调用方提供的 int 缓冲；可为 NULL（仅计数，不写入）。
 * @param maxCount 缓冲容量（个数）；outIds 为 NULL 时不生效。
 * @return 计数查询模式返回已访问页总数；写入模式返回实际写入个数。
 */
int XWizard_visitedIds(const XWizard* self, int* outIds, int maxCount);
/** @brief 列出全部页 id（对标 QWizard::pageIds）。
 * @details 输出**稳定页 id**（非页表下标），按页表顺序（= id 升序），
 *          覆盖所有已登记页面（不限已访问）。outIds 为
 *          NULL 或 maxCount <= 0 时为"计数查询"模式，仅返回页面总数；
 *          否则最多写入 maxCount 个 id 并返回实际写入个数。
 * @param self 目标向导指针；NULL 时返回 0。
 * @param outIds 调用方提供的 int 缓冲；可为 NULL（仅计数，不写入）。
 * @param maxCount 缓冲容量（个数）；outIds 为 NULL 时不生效。
 * @return 计数查询模式返回页面总数；写入模式返回实际写入个数。
 */
int XWizard_pageIds(const XWizard* self, int* outIds, int maxCount);
/** @brief XWizardsetWizard样式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param style XWizardStyle 参数。
 * @return 无返回值。
 */
void XWizard_setWizardStyle(XWizard* self, XWizardStyle style);
/** @brief XWizardwizard样式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
XWizardStyle XWizard_wizardStyle(const XWizard* self);
/** @brief XWizardset选项（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param option XWizardOption 参数。
 * @param on bool：true 开启。
 * @return 无返回值。
 */
void XWizard_setOption(XWizard* self, XWizardOption option, bool on);
/** @brief XWizardtest选项（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param option XWizardOption 参数。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XWizard_testOption(const XWizard* self, XWizardOption option);
/** @brief XWizardsetOptions（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param options int 参数。
 * @return 无返回值。
 */
void XWizard_setOptions(XWizard* self, int options);
/** @brief XWizardoptions（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XWizard_options(const XWizard* self);
/** @brief 设置按钮文本（对标 QWizard::setButtonText）。
 * @details **九个槽位（0..8）全部受理**（对标 Qt 经 ensureButton 对
 *          NButtons 内槽位受理，qwizard.cpp:2595-2606；P0-43 前自定义
 *          槽 6..8 被静默丢弃）。文本按槽位存储并**下发给槽位活动按钮**
 *          （setButton 登记按钮优先，否则预建按钮；Qt 的
 *          `d->btns[which]->setText`，登记按钮即 btns[which]）——当前页
 *          设过页级文本时不覆盖（qwizard.cpp:2604-2605）。未设置的槽位
 *          由 XWizard_buttonText 返回**缺省文本**（见该函数注）。
 * @param self 目标控件指针。
 * @param which 目标枚举项（0..8；NoButton/Stretch/越界忽略）。
 * @param utf8 UTF-8 文本；显式空串清空按钮文本（Qt 同）。
 * @return 无返回值。
 */
void XWizard_setButtonText(XWizard* self, XWizardButton which, const char* utf8);
/** @brief 查询按钮文本（对标 QWizard::buttonText）。
 * @details 对齐 Qt（qwizard.cpp:2620-2635）：
 *          ① 向导级自定义文本优先（显式设空也返回空串）；
 *          ② 标准槽（0..5）未设时返回**缺省按钮文本**（随 wizardStyle：
 *             MacStyle 的 Back/Next/Finish 缺省文案不同，对标
 *             buttonDefaultText 的 macStyle 分支，qwizard.cpp:614-640）；
 *          ③ 自定义槽（6..8）缺省层为空 ⇒ **回落槽位活动按钮自身文本**
 *             （Qt 的 `d->btns[which]->text()`，qwizard.cpp:2634）——
 *             setButton 登记按钮后可读回按钮文本（P0-86）。
 *          Qt 的缺省文本来自 `QWizard::tr("< &Back")` 等源串，本机 C
 *          locale 实测 `< &Back` / `&Next >` / `Commit` / `&Finish` /
 *          `Cancel` / `&Help`；本库无翻译设施，按同一语义给中文缺省
 *          （本项目的标准按钮文案惯例，XDialogButtonBox 同口径），
 *          属**文案本地化**而非行为偏差（r2 对齐，账本 241）。
 * @param self 目标控件指针。
 * @param which 目标枚举项（越界/NoButton/Stretch 返回空串）。
 * @return 返回 UTF-8 文本；无效时返回空串。
 * @warning 借用返回（现契约）：标准槽指向内部缓存，自定义槽（6..8）
 *          未设自定义文本时**直接借用槽位按钮内部 XString 的 UTF-8**
 *          ——按钮文本变化/按钮析构后该指针即刻失效，不得持有。
 */
const char* XWizard_buttonText(const XWizard* self, XWizardButton which);
/** @brief 查询按钮控件（对标 QWizard::button）。
 * @details 九个槽位（六标准 + CustomButton1..3）**构造即预建**，未由
 *          setButton 替换时返回预建按钮（对标 QWizardPrivate::init 的
 *          ensureButton + updateButtonLayout：Help 与 Custom1..3 也在
 *          布局序列里被 ensureButton 预建；账本 242）。预建按钮的
 *          显隐由 updateButtonStates/选项决定（Help 需 HaveHelpButton、
 *          Custom1..3 需 HaveCustomButton1..3）。
 * @param self 目标向导指针；传入 NULL 返回 NULL。
 * @param which 按钮枚举（XWizardButton）。
 * @return 按钮控件借用指针（所有权在向导或登记方，禁止释放）；
 *         NULL 表示 which 越界或 NoButton。
 */
struct XAbstractButton* XWizard_button(const XWizard* self, int which);
/** @brief 设置按钮控件（对标 QWizard::setButton）。
 * @details which 槽位改用调用方按钮：槽内预建按钮隐藏，登记按钮入
 *          按钮行布局；标准槽的点击按槽位语义接线（Back→back、
 *          Next/Commit→next、Finish→accept、Cancel→reject、
 *          Help→helpRequested），自定义槽发射 customButtonClicked(which)。
 *          传入 NULL 清除登记（恢复使用预建按钮）。
 * @note    项目有意偏差：Qt 的 setButton **接管按钮所有权**并在替换时
 *          删除旧按钮；本库沿用**借用承载**（向导不删除登记按钮；
 *          依据：XWizard.h 既有契约、Test/XGuiDemo/xgui_demo_apitest_dialogs.c
 *          的 setButton→NULL→XClassDelete 序列与 Test/QtAlign 用例）。
 * @param self 目标向导指针；传入 NULL 或 which 越界时忽略。
 * @param which 按钮枚举（XWizardButton）。
 * @param btn 按钮控件借用指针；NULL 清除登记。
 * @return 无返回值。
 */
void XWizard_setButton(XWizard* self, int which,
                       struct XAbstractButton* btn);
/** @brief 登记默认属性（对标 QWizard::setDefaultProperty）。
 * @details Qt 原型 `setDefaultProperty(const char *className,
 *          const char *property, const char *changedSignal)`，用于
 *          registerField 未给属性名时按对象类名（含子类）解析属性。
 *          C 无元对象类名/信号名反射，映射为：className 用
 *          XObject_className/XObject_inherits 匹配（C 侧等价物），
 *          changedSignal 传信号标识（0=无）。同名（类名+属性）重复登记
 *          覆盖旧信号。
 * @param self 目标向导指针；传入 NULL 或 className/property 为空时忽略。
 * @param className 类名（UTF-8 借用；内部深拷贝）。
 * @param property 属性名（UTF-8 借用；内部深拷贝）。
 * @param changedSignal 变化信号标识（`XSignal(...)`；0=不连接）。
 * @return 无返回值。
 * @note 容量上限 XWIZARD_MAX_DEFAULT_PROPERTIES（16）条；表满后忽略。
 */
void XWizard_setDefaultProperty(XWizard* self, const char* className,
                                const char* property, size_t changedSignal);

/* ==================== 信号 ==================== */

void* XWizard_currentIdChanged_signal(XWizard* self, int index);
/** @brief XWizardpageAdded 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @param index 载荷为**稳定页 id**（对标 Qt 的 pageAdded(int id)；
 *              非"页表下标"，同函数体内 addPage 以 id 发射）。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XWizard_pageAdded_signal(XWizard* self, int index);
/** @brief XWizardpageRemoved 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @param index 载荷为**稳定页 id**（对标 Qt 的 pageRemoved(int id)）。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XWizard_pageRemoved_signal(XWizard* self, int index);

/**
 * @brief      发射 helpRequested() 信号（对标 QWizard::helpRequested）。
 * @details    用户点击向导的 Help 按钮时真发射；self 非 NULL 且有已
 *             连接槽时经 XObject_emitSignal 同步通知（空参信号
 *             args = NULL），否则只返回信号标识。
 * @param      self 目标向导指针；可为 NULL。
 * @return     不透明的 helpRequested 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
/* ==================== Task 2.6：字段/侧边/布局/格式/导航 ==================== */

/** @brief 设置向导图片（XString 主版本；对标 QWizard::setPixmap）。
 * @details 四槽独立存储（Watermark/Logo/Banner/Background；对标
 *          QWizardPrivate::defaultPixmaps[NPixmaps]）。
 * @param self 目标向导。
 * @param which 图类型码（XWizardPixmap；越界忽略）。
 * @param path 借用 XString*；可为 NULL（清除该槽）。
 * @return 无返回值。
 */
void XWizard_setPixmap(XWizard* self, int which, const XString* path);
/** @brief 设置向导图片（UTF-8 兼容重载）。 */
void XWizard_setPixmap_2(XWizard* self, int which, const char* path);
/** @brief 查询向导图片路径（对标 QWizard::pixmap）。
 * @param self 目标向导；传入 NULL 时返回 NULL。
 * @param which 图类型码（XWizardPixmap；越界返回 NULL）。
 * @return 借用内部 XString 指针；该槽未设置返回 NULL；禁止释放或修改。
 */
const XString* XWizard_pixmap(const XWizard* self, int which);
/** @brief 读取字段值（对标 QWizard::field；XString 借用）。
 * @param self 目标向导。
 * @param name 借用 XString*；不能为 NULL。
 * @return 内部借用 XString*；未设置返回 NULL。
 */
const XString* XWizard_field(const XWizard* self, const XString* name);
/** @brief 读取字段值（UTF-8 兼容重载）。 */
const char* XWizard_field_2(const XWizard* self, const char* name);
/** @brief 设置字段值（XString 主版本；对标 QWizard::setField）。
 * @param self 目标向导。
 * @param name 借用 XString*；不能为 NULL。
 * @param value 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。
 */
void XWizard_setField(XWizard* self, const XString* name,
                      const XString* value);
/** @brief 设置字段值（UTF-8 兼容重载）。 */
void XWizard_setField_2(XWizard* self, const char* name,
                        const char* value);
/** @brief 设置侧边控件（对标 setSideWidget；借用）。
 * @param self 目标向导。
 * @param widget 控件借用指针；可为 NULL（清除）。
 * @return 无返回值。
 */
void XWizard_setSideWidget(XWizard* self, XWidget* widget);
/** @brief 查询侧边控件。 @param self 目标向导。 @return 借用指针。 */
XWidget* XWizard_sideWidget(const XWizard* self);
/** @brief 设置按钮布局（对标 QWizard::setButtonLayout）。
 * @details Qt 签名是 `QList<WizardButton>`（qwizard.cpp:2658-2684）：
 *          序列决定按钮行顺序与**哪些按钮出现**（未列入的按钮不参与
 *          布局），`Stretch` 插入弹簧占位；重复按钮按 Qt 拒绝整份布局
 *          并告警。C 侧映射为 `(const int* layout, int count)`，元素取
 *          XWizardButton（含 XWizardButton_Stretch 与 NoButton）。
 * @param self 目标向导；NULL 时忽略。
 * @param layout 布局序列数组（借用；元素为 XWizardButton 值）。
 * @param count 元素个数（<=0 或 > XWIZARD_MAX_BUTTON_LAYOUT 时忽略）。
 * @return 无返回值。
 */
void XWizard_setButtonLayout(XWizard* self, const int* layout, int count);
/** @brief 设置标题格式（对标 setTitleFormat；Qt::TextFormat 取值）。
 * @param self 目标向导。
 * @param format 格式码（0=PlainText、1=RichText、2=AutoText；默认 2）。
 * @return 无返回值。
 */
void XWizard_setTitleFormat(XWizard* self, int format);
/** @brief 查询标题格式（对标 QWizard::titleFormat）。
 * @param self 目标向导；传入 NULL 时返回 0。
 * @return 格式码（对标 Qt::TextFormat：0=PlainText，1=RichText，
 *         2=AutoText；缺省 2，本机 Qt 6.8.3 实测）。
 */
int XWizard_titleFormat(const XWizard* self);
/** @brief 设置子标题格式（对标 setSubTitleFormat）。
 * @param self 目标向导。
 * @param format 格式码。
 * @return 无返回值。
 */
void XWizard_setSubTitleFormat(XWizard* self, int format);
/** @brief 查询子标题格式（对标 QWizard::subTitleFormat）。
 * @param self 目标向导；传入 NULL 时返回 0。
 * @return 格式码（对标 Qt::TextFormat：0=PlainText，1=RichText，
 *         2=AutoText；缺省 2）。
 */
int XWizard_subTitleFormat(const XWizard* self);
/** @brief 向导级清理页（对标 QWizard::cleanupPage(int id)，虚槽分派）。
 * @details 查虚表 EXWizard_InitializePage… 的 CleanupPage 槽调用向导级
 *          实现；默认实现转调该页的 XWizardPage_cleanupPage（Qt 默认
 *          即转发，qwizard.cpp）。子类 XVTABLE_OVERLOAD_DEFAULT 覆盖后
 *          导航会自动以其为准。P1-34：本体改名对齐 Qt（原
 *          XWizard_cleanupPage_2）；无参便捷版已随 P2-19 删除。
 * @param self 目标向导；NULL 或未知 id 时为空操作。
 * @param id 页 id。
 * @return 无返回值。
 */
void XWizard_cleanupPage(XWizard* self, int id);
/** @brief 向导级初始化页（对标 QWizard::initializePage(int id)，虚槽分派）。
 * @details 默认实现转调该页的 XWizardPage_initializePage。P1-33：本体
 *          改名对齐 Qt（原 XWizard_initializePage_2）；无参便捷版已随
 *          P2-18 删除。
 * @param self 目标向导；NULL 或未知 id 时为空操作。
 * @param id 页 id。
 * @return 无返回值。
 */
void XWizard_initializePage(XWizard* self, int id);
/** @brief 校验当前页（对标 QWizard::validateCurrentPage；分派到当前页的
 *         XWizardPage_validatePage 虚槽，默认返回 true）。
 * @param self 目标向导。
 * @return 通过返回 true；无当前页时直接返回 true。
 */
bool XWizard_validateCurrentPage(const XWizard* self);
/** @brief 下一页索引（对标 QWizard::nextId；分派到当前页的
 *         XWizardPage_nextId 虚槽，默认顺序 +1）。
 * @param self 目标向导。
 * @return 下一页索引；末页或无页面返回 -1。
 */
int XWizard_nextId(const XWizard* self);
/** @brief 完成向导（对标 QWizard::done）。
 * @param self 目标向导。
 * @param result 结果码。
 * @return 无返回值。
 */
void XWizard_done(XWizard* self, int result);

void* XWizard_helpRequested_signal(XWizard* self);
/** @brief customButtonClicked(int) 信号（对标 QWizard::customButtonClicked；
 *         载荷：XWizardButton 枚举；按钮布局接线见 Task 2.6）。 */
void* XWizard_customButtonClicked_signal(XWizard* self, int which);

#endif /* XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */

#endif /* XGUI_ON */

#endif /* XWIZARD_H */
