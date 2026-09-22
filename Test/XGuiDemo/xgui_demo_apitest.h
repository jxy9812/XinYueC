/* xgui_demo_apitest.h —— XGuiWindowDemo 内置控件 API 全量测试契约。
 *
 * 运行口径：demo 以 --apitest 启动时先跑本套件（逐族 PASS/FAIL，
 * 任一失败退出码 1），再按需继续窗口流程；与 --autotest（页面交互）、
 * 回归测试（行为断言）互补，构成按控件类型分族的三层测试面。
 *
 * 套件纪律：
 *  - 每控件族一个翻译单元（xgui_demo_apitest_<family>.c），实现本头
 *    声明的入口，返回失败断言数（0=全过）；
 *  - 断言用 XAPI_EXPECT（输出 XGuiApiTest: [PASS]/[FAIL] 中文描述，
 *    实现文件内须有局部变量 int failures 计数）；
 *  - 无头语义：控件不 show 也可调绝大多数 API；信号断言经
 *    XObject_event_base 直发合成事件（与真实输入同路径）；
 *  - 对标 Qt 6.8.3：确定的文档默认值直接断言；不确定的写注释说明
 *    不硬断言（防误报）；每条断言中文注释标对标 Qt 的哪个行为；
 *  - 视觉边界：本套件只断言 API 状态与几何/map 事实，渲染效果由
 *    主线截图亲验，不在断言职责内。
 */

#ifndef XGUI_DEMO_APITEST_H
#define XGUI_DEMO_APITEST_H

#include "XPrintf.h"
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 空安全取 UTF-8 C 串：XString_toUtf8 对空串/空指针返回 NULL，
 *         断言里直接下标/strcmp 会踩空；统一经此取值，NULL 视作 ""。
 *  @warning 仅本套件测试代码使用（static inline，各 TU 独立实例）。 */
static inline const char* xapi_u8(const XString* s)
{
    const char* u = s ? XString_toUtf8(s) : NULL;
    return u ? u : "";
}

/** @brief 空安全 C 串下标垫片：控件 getter 对空值可能返回 NULL，
 *         断言里直接 [0] 会踩空；统一经此取值，NULL 视作 ""。 */
static inline const char* xapi_cstr(const char* s)
{
    return s ? s : "";
}

/** @brief 断言宏：cond 成立计 PASS，否则计 FAIL。
 *  @warning 使用处须有局部变量 int failures。 */
#define XAPI_EXPECT(cond, what) \
    do { \
        if (cond) XPrintf("XGuiApiTest: [PASS] %s\n", what); \
        else { XPrintf("XGuiApiTest: [FAIL] %s\n", what); ++failures; } \
    } while (0)

int xapi_buttons_run(void);    /**< 按钮：XAbstractButton/XPushButton/XCheckBox/XRadioButton/XToolButton/XCommandLinkButton/XButtonGroup */
int xapi_labels_run(void);     /**< 显示：XLabel/XFrame/XGroupBox/XLcdNumber/XProgressBar */
int xapi_input_run(void);      /**< 输入：XLineEdit/XAbstractSpinBox/XSpinBox/XDateTimeEdit/XComboBox/XFontComboBox */
int xapi_text_run(void);       /**< 文本：XTextEdit/XPlainTextEdit/XTextBrowser/XTextDocument/XCompleter/XKeySequenceEdit */
int xapi_views_run(void);      /**< 条目视图：XAbstractItemModel/XAbstractItemView/XListView/XListWidget/XTreeView/XTreeWidget/XTableView/XTableWidget/XHeaderView */
int xapi_containers_run(void); /**< 容器：XTabBar/XTabWidget/XStackedWidget/XSplitter/XScrollArea/XScrollBar/XAbstractScrollArea/XToolBox/XMdiArea/XDockWidget/XMainWindow */
int xapi_dialogs_run(void);    /**< 对话框：XDialog/XDialogButtonBox/XMessageBox/XInputDialog/XFileDialog/XColorDialog/XProgressDialog/XWizard */
int xapi_menus_run(void);      /**< 框架杂项：XMenu/XMenuBar/XToolBar/XStatusBar/XAction/XActionGroup/XShortcut/XToolTip/XErrorMessage/XSplashScreen/XFocusFrame/XSizeGrip/XRubberBand */
int xapi_core_run(void);       /**< 基类与样式：XWidget 几何/显隐/启用/焦点/palette/字体/XStyle/XPalette/XGraphicsEffect 四效果 */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_APITEST_H */
