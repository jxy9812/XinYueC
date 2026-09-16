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
    XString* m_title;     /**< 页面标题（对象拥有）。 */
    XString* m_subTitle;  /**< 页面子标题（对象拥有）。 */
    bool m_complete;       /**< 是否完成（默认 true）。 */
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
#define XWizardPage_delete_base(self) XWidget_deinit_base((XWidget*)(self))
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
/** @brief XWizard页setComplete（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param complete bool 参数。
 * @return 无返回值。
 */
void XWizardPage_setComplete(XWizardPage* self, bool complete);
/** @brief XWizard页isComplete（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
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
    XString* m_buttonTexts[XWizardButton_NStandardButtons]; /**< 自定义按钮文本（对象拥有）。 */
#if XPUSHBUTTON_ON
    XPushButton* m_btnBack;     /**< 上一页按钮。 */
    XPushButton* m_btnNext;     /**< 下一页按钮。 */
    XPushButton* m_btnFinish;   /**< 完成按钮。 */
    XPushButton* m_btnCancel;   /**< 取消按钮。 */
    XPushButton* m_btnHelp;     /**< 帮助按钮（HaveHelpButton 时创建）。 */
#endif
    struct XWizardField { XString* name; XString* value; } m_fields[32]; /**< 字段表（内嵌；name/value 对象拥有）。 */
    int m_fieldCount;           /**< 字段数。 */
    XString* m_pixmap;          /**< 向导横幅图路径（对象拥有）。 */
    XWidget* m_sideWidget;      /**< 侧边控件（借用）。 */
    int m_buttonLayout;         /**< 按钮布局码（对标 setButtonLayout）。 */
    int m_titleFormat;          /**< 标题格式码（对标 setTitleFormat）。 */
    int m_subTitleFormat;       /**< 子标题格式码（对标 setSubTitleFormat）。 */
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
#define XWizard_delete_base(self) XDialog_delete_base((XDialog*)(self))

/** @brief XWizardadd页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param page XWizardPage 参数。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XWizard_addPage(XWizard* self, XWizardPage* page);
/** @brief XWizardset页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param page XWizardPage 参数。
 * @return 无返回值。
 */
void XWizard_setPage(XWizard* self, int index, XWizardPage* page);
/** @brief XWizardremove页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XWizard_removePage(XWizard* self, int index);
/** @brief XWizardpage（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWizardPage* XWizard_page(XWizard* self, int index);
/** @brief XWizardpage数量（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XWizard_pageCount(const XWizard* self);
/** @brief XWizardcurrent页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWizardPage* XWizard_currentPage(const XWizard* self);
/** @brief XWizardcurrent索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XWizard_currentIndex(const XWizard* self);
/** @brief XWizardnext（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XWizard_next(XWizard* self);
/** @brief XWizardback（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XWizard_back(XWizard* self);
/** @brief XWizardrestart（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XWizard_restart(XWizard* self);
/** @brief XWizardsetStart索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XWizard_setStartIndex(XWizard* self, int index);
/** @brief XWizardstart索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XWizard_startIndex(const XWizard* self);
/** @brief XWizardhasVisited页（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XWizard_hasVisitedPage(const XWizard* self, int index);
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
/** @brief XWizardset按钮文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param which 目标枚举项。
 * @param utf8 UTF-8 文本。
 * @return 无返回值。
 */
void XWizard_setButtonText(XWizard* self, XWizardButton which, const char* utf8);
/** @brief XWizardbutton文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param which 目标枚举项。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XWizard_buttonText(const XWizard* self, XWizardButton which);

/* ==================== 信号 ==================== */

void* XWizard_currentIdChanged_signal(XWizard* self, int index);
/** @brief XWizardpageAdded 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XWizard_pageAdded_signal(XWizard* self, int index);
/** @brief XWizardpageRemoved 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
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

/** @brief 设置向导横幅图（XString 主版本；对标 setPixmap）。
 * @param self 目标向导。
 * @param which 图类型码（对标 QWizard::WizardPixmap）。
 * @param path 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XWizard_setPixmap(XWizard* self, int which, const XString* path);
/** @brief 设置向导横幅图（UTF-8 兼容重载）。 */
void XWizard_setPixmap_2(XWizard* self, int which, const char* path);
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
/** @brief 设置按钮布局（对标 setButtonLayout）。
 * @param self 目标向导。
 * @param layout 布局码。
 * @return 无返回值。
 */
void XWizard_setButtonLayout(XWizard* self, int layout);
/** @brief 设置标题格式（对标 setTitleFormat）。
 * @param self 目标向导。
 * @param format 格式码。
 * @return 无返回值。
 */
void XWizard_setTitleFormat(XWizard* self, int format);
/** @brief 设置子标题格式（对标 setSubTitleFormat）。
 * @param self 目标向导。
 * @param format 格式码。
 * @return 无返回值。
 */
void XWizard_setSubTitleFormat(XWizard* self, int format);
/** @brief 清理当前页（对标 cleanupPage；当前为文档回调占位）。
 * @param self 目标向导。
 * @return 无返回值。
 */
void XWizard_cleanupPage(XWizard* self);
/** @brief 初始化当前页（对标 initializePage；当前为文档回调占位）。
 * @param self 目标向导。
 * @return 无返回值。
 */
void XWizard_initializePage(XWizard* self);
/** @brief 校验当前页（对标 QWizard::validateCurrentPage）。
 * @param self 目标向导。
 * @return 通过返回 true（当前页 complete 标记）。
 */
bool XWizard_validateCurrentPage(const XWizard* self);
/** @brief 下一页索引（对标 QWizard::nextId；默认顺序 +1）。
 * @param self 目标向导。
 * @return 下一页索引；末页返回 -1。
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

#endif /* XWIZARD_H */