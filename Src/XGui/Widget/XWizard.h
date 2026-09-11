/******************************************************************************
 * @file       XWizard.h
 * @brief      XWizard 向导对话框 + XWizardPage 向导页控件
 *             （对标 Qt 6.8 QWizard / QWizardPage 核心公共 API）。
 * @details    功能范围：
 *             - XWizardPage：setTitle/subTitle、isComplete/setComplete、
 *               initializePage/cleanupPage（虚槽声明）；
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
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XDialog.h"
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
    XWizardStyle_AeroStyle = 3
} XWizardStyle;

/** @brief 向导选项（对标 QWizard::WizardOption，数值一致）。 */
typedef enum XWizardOption
{
    XWizardOption_IndependentPages             = 0x00000001,
    XWizardOption_IgnoreSubTitles               = 0x00000002,
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

/** @brief 向导按钮枚举（对标 QWizard::WizardButton，数值一致）。 */
typedef enum XWizardButton
{
    XWizardButton_BackButton = 0,
    XWizardButton_NextButton = 1,
    XWizardButton_CommitButton = 2,
    XWizardButton_FinishButton = 3,
    XWizardButton_CancelButton = 4,
    XWizardButton_HelpButton = 5,
    XWizardButton_NStandardButtons = 6
} XWizardButton;

/* ==================== XWizardPage ==================== */

XCLASS_DEFINE_BEGING(XWizardPage)
XCLASS_DEFINE_EXTEND_END(XWizardPage, XWidget)

typedef struct XWizardPage
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    char m_title[128];     /**< 页面标题。 */
    char m_subTitle[128];  /**< 页面子标题。 */
    bool m_complete;       /**< 是否完成（默认 true）。 */
} XWizardPage;

XVtable* XWizardPage_class_init(void);
void XWizardPage_init(XWizardPage* self, XWidget* parent, XWidgetFlags flags);
#define XWizardPage_create(parent, flags) XWizardPage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XWizardPage* XWizardPage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XWizardPage_delete_base(self) XWidget_deinit_base((XWidget*)(self))

void XWizardPage_setTitle(XWizardPage* self, const char* utf8);
const char* XWizardPage_title(const XWizardPage* self);
void XWizardPage_setSubTitle(XWizardPage* self, const char* utf8);
const char* XWizardPage_subTitle(const XWizardPage* self);
void XWizardPage_setComplete(XWizardPage* self, bool complete);
bool XWizardPage_isComplete(const XWizardPage* self);

/* ==================== XWizard ==================== */

XCLASS_DEFINE_BEGING(XWizard)
XCLASS_DEFINE_EXTEND_END(XWizard, XDialog)

#define XWIZARD_MAX_PAGES 16

typedef struct XWizard
{
    XDialog m_base;             /**< 基类成员；必须是第一个。 */
    XWizardPage* m_pages[XWIZARD_MAX_PAGES]; /**< 页面数组。 */
    int m_pageCount;            /**< 已添加页面数。 */
    int m_currentIndex;         /**< 当前页索引。 */
    int m_startIndex;           /**< 起始页索引。 */
    bool m_visited[XWIZARD_MAX_PAGES]; /**< 已访问标记。 */
    int m_style;                /**< 向导样式。 */
    int m_options;              /**< 选项位标志。 */
    char m_buttonTexts[6][32];  /**< 自定义按钮文本。 */
#if XPUSHBUTTON_ON
    XPushButton* m_btnBack;     /**< 上一页按钮。 */
    XPushButton* m_btnNext;     /**< 下一页按钮。 */
    XPushButton* m_btnFinish;   /**< 完成按钮。 */
    XPushButton* m_btnCancel;   /**< 取消按钮。 */
#endif
} XWizard;

XVtable* XWizard_class_init(void);
void XWizard_init(XWizard* self, XWidget* parent, XWidgetFlags flags);
#define XWizard_create(parent, flags) XWizard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XWizard* XWizard_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XWizard_delete_base(self) XDialog_delete_base((XDialog*)(self))

int XWizard_addPage(XWizard* self, XWizardPage* page);
void XWizard_setPage(XWizard* self, int index, XWizardPage* page);
void XWizard_removePage(XWizard* self, int index);
XWizardPage* XWizard_page(XWizard* self, int index);
int XWizard_pageCount(const XWizard* self);
XWizardPage* XWizard_currentPage(const XWizard* self);
int XWizard_currentIndex(const XWizard* self);
void XWizard_next(XWizard* self);
void XWizard_back(XWizard* self);
void XWizard_restart(XWizard* self);
void XWizard_setStartIndex(XWizard* self, int index);
int XWizard_startIndex(const XWizard* self);
bool XWizard_hasVisitedPage(const XWizard* self, int index);
void XWizard_setWizardStyle(XWizard* self, XWizardStyle style);
XWizardStyle XWizard_wizardStyle(const XWizard* self);
void XWizard_setOption(XWizard* self, XWizardOption option, bool on);
bool XWizard_testOption(const XWizard* self, XWizardOption option);
void XWizard_setOptions(XWizard* self, int options);
int XWizard_options(const XWizard* self);
void XWizard_setButtonText(XWizard* self, XWizardButton which, const char* utf8);
const char* XWizard_buttonText(const XWizard* self, XWizardButton which);

/* ==================== 信号 ==================== */

void* XWizard_currentIdChanged_signal(XWizard* self, int index);
void* XWizard_pageAdded_signal(XWizard* self, int index);
void* XWizard_pageRemoved_signal(XWizard* self, int index);

#endif /* XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */

#ifdef __cplusplus
}
#endif

void* XWizard_completeChanged_signal(XWizard* self);
void* XWizard_customButtonClicked_signal(XWizard* self);
#endif /* XWIZARD_H */