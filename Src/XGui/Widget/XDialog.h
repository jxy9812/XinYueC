/**
 * @file       XDialog.h
 * @brief      XDialog 对话框控件（对标 Qt 6.8 QDialog 核心公共 API）。
 * @details    继承 XWidget（对标 QDialog : QWidget）；构造时 flags 不含
 *             窗口类型位则自动叠加 Dialog 类型（qdialog.cpp:374-378），
 *             即与 Qt 一致恒为独立顶层窗口；exec() 事件循环模态；
 *             accepted()/rejected() 信号（done() 内统一按 accepted 前、
 *             finished 后的顺序发射）；done(int) 完成；
 *             setModal/setResult/result。
 * @note       模块总开关 XDIALOG_ON。
 * @author     XinYueC 团队
 */
#ifndef XDIALOG_H
#define XDIALOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XDIALOG_ON

XCLASS_DEFINE_BEGING(XDialog)
XCLASS_DEFINE_EXTEND_END(XDialog, XWidget)

/** @brief 对话框结果码（对标 QDialog::DialogCode，数值一致）。 */
typedef enum XDialogCode
{
    XDialogCode_Rejected = 0,        /**< 拒绝（对标 QDialog::Rejected）。 */
    XDialogCode_Accepted = 1         /**< 接受（对标 QDialog::Accepted）。 */
} XDialogCode;

typedef struct XDialog
{
    XWidget m_base;    /**< 基类成员；必须是第一个。 */
    int m_result;      /**< 对标 result()；DialogCode 或标准按钮位值。 */
    bool m_modal;      /**< 对标 modal 属性（默认 false，对标 QDialog::modal；exec 路径显式模态化）。 */
    bool m_inExec;     /**< exec() 循环标志。 */
    bool m_sizeGripEnabled; /**< 对标 QDialog::sizeGripEnabled；仅存储位
                                （XSizeGrip 控件未自动嵌入）。 */
    int m_resetModalityTo; /**< open() 临时切换窗口模态前的原模态值
                                （-1=未记录；对标 QDialogPrivate::
                                resetModalityTo，done/exec 时恢复）。 */
    int m_csdAppliedTop;   /**< 当前已套用的 CSD 内容避让顶偏移（像素；
                                0=未套用/系统标题栏模式）。增量记账：
                                再次套用只补「目标-已套用」差值，重复
                                调用幂等不叠加（见
                                XDialog_decorationTopOffset）。 */
} XDialog;

/** @brief XDialogclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XDialog_class_init(void);
/** @brief XDialoginit（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XDialog_init(XDialog* self, XWidget* parent, XWidgetFlags flags);
#define XDialog_create(parent, flags) XDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XDialogcreateex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XDialog* XDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/**
 * @brief      启动模态对话框事件循环（对标 QDialog::exec）。
 * @details    exec 期间无条件应用模态；递归 exec 打印警告并返回 -1；
 *             返回值为 result()（QMessageBox 下为被点标准按钮位值）。
 *             设有 DeleteOnClose 属性时 exec 返回前删除对话框。
 * @param      self 目标控件指针；NULL 返回 0。
 * @return     对话框结果码；递归调用返回 -1。
 */
int XDialog_exec(XDialog* self);
/**
 * @brief      关闭对话框并设置结果码（对标 QDialog::done）。
 */
void XDialog_done(XDialog* self, int result);
/**
 * @brief      以接受方式关闭对话框（对标 QDialog::accept；result=1）。
 */
void XDialog_accept(XDialog* self);
/**
 * @brief      以拒绝方式关闭对话框（对标 QDialog::reject；result=0）。
 */
void XDialog_reject(XDialog* self);
/**
 * @brief      获取最近一次结果码（对标 QDialog::result）。
 */
int XDialog_result(const XDialog* self);
/**
 * @brief      设置结果码（对标 QDialog::setResult）。
 */
void XDialog_setResult(XDialog* self, int result);
/**
 * @brief      设置模态标志（对标 QDialog::setModal）。
 */
void XDialog_setModal(XDialog* self, bool modal);
/**
 * @brief      获取模态标志（对标 QDialog::isModal）。
 */
bool XDialog_isModal(const XDialog* self);
/**
 * @brief      以窗口模态方式显示对话框并立即返回（对标 QDialog::open）。
 * @details    与 Qt 语义一致（qdialog.cpp:509-526）：open 前若
 *             windowModality 不是 WindowModal 则临时改为 WindowModal
 *             （原值记录在 m_resetModalityTo，done/exec 关闭时恢复），
 *             setResult(0) 后 show()，不进入本地事件循环；结果只经
 *             finished/accepted/rejected 回传。项目模态拦截以应用模态
 *             门为既定等价物照常登记（demo 约定"模态门照常生效"）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @return     无返回值。
 */
void XDialog_open(XDialog* self);
/**
 * @brief      设置尺寸手柄开关（对标 QDialog::setSizeGripEnabled）。
 * @details    仅存储状态；XSizeGrip 控件未自动嵌入窗口边角。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      enable true 显示尺寸手柄。
 * @return     无返回值。
 */
void XDialog_setSizeGripEnabled(XDialog* self, bool enable);
/**
 * @brief      查询尺寸手柄开关（对标 QDialog::isSizeGripEnabled）。
 * @param      self 目标控件指针；传入 NULL 时返回 false。
 * @return     开启返回 true。
 */
bool XDialog_isSizeGripEnabled(const XDialog* self);
/**
 * @brief      返回对话框内容的 CSD 避让顶偏移（像素）。
 * @details    框架自绘窗口装饰（XWindowDecoration，CSD）把标题条画在
 *             客户区顶部，对话框子控件仍从 y≈0 布局时首行内容被条带
 *             遮挡（用户实测：独立顶层消息盒图标半截）。本查询返回装
 *             饰条当前高度作为布局起始避让量（XWindowDecoration_
 *             marginsFor 取值，与 XWindow_frameMargins 同源落盘、条未
 *             承载时按 XTitleBar_defaultHeight 预测；主窗口布局让位
 *             同源先例）。仅对话框自身为独立顶层窗口且被框架装饰时非
 *             零：系统标题栏模式（WM 在客户区外画条）、CSD 抑制
 *             （XWindow_setCsdFrameSuppressed 置位等价——未装饰时保
 *             留边距恒零）及子控件形态对话框一律返回 0，布局零变化。
 *             对标无公开 Qt API：Qt 原生标题栏在窗口框架内由 WM 预
 *             留，QDialog 无需避让；XGui CSD 条带画在客户区内故需此
 *             等价物。派生类（XMessageBox 等）自排布内容时以本值为
 *             计算起点偏移；exec/open/show 时基类对布局挂载的对话框
 *             自动增量改写根布局顶边距、对其余对话框整体下移直接子
 *             控件（消息盒等自管派生类除外）。
 * @param      self 目标对话框；可为 NULL。
 * @return     CSD 激活返回装饰条高；否则返回 0。
 */
int XDialog_decorationTopOffset(const XDialog* self);

/* ==================== 信号 ==================== */

/**
 * @brief      接受信号（真发射）。
 */
void* XDialog_accepted_signal(XDialog* self);
/**
 * @brief      拒绝信号（真发射）。
 */
void* XDialog_rejected_signal(XDialog* self);
/**
 * @brief      完成信号（真发射）。
 */
void* XDialog_finished_signal(XDialog* self, int result);

#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XDIALOG_H */