/**
 * @file       XSplitterHandle.h
 * @brief      XSplitterHandle 分割器把手部件（对标 Qt 6.8 QSplitterHandle
 *             全类，qsplitter.h:96-125 + qsplitter.cpp:86-324）。
 * @details    把手为分割器的真实子部件，占据分隔点 m_index 两侧页之间
 *             的把手条带（几何由 XSplitter 布局直配：宽度=有效把手宽，
 *             轴长=分割器轴长；可见性随 xsp_bandAfter——相邻两页不全隐
 *             藏才显，保留「收起态可点可展开」的防死态设计，不照 Qt
 *             :376-380 整条隐藏）。派生定式对标 XRubberBand.h:33-58
 *             （XWidget 直派生，非 Frame——把手无框机语义）。
 *             行为映射（qsplitter.cpp 实读行号）：
 *             - paintEvent（:306-324）：CE_Splitter，state 含
 *               Enabled/MouseOver(hover)/Sunken(pressed)，横竖由
 *               m_horizontal 表达；
 *             - mousePressEvent（:273-281）：mouseOffset=pick(local)、
 *               pressed=1、grabMouse；
 *             - mouseMoveEvent（:255-268）：pressed 且 opaque→
 *               splitter.moveSplitter(pick(父映射)−offset)；非 opaque→
 *               setRubberBand(closestLegalPosition) 记待落位；非 pressed
 *               无 move 逻辑（旧分割器 hover 光标已由构造期静态方向
 *               光标承载，MouseOver 绘制态经 Enter/Leave 刷新）；
 *             - mouseReleaseEvent（:286-301）：非 opaque 记位落置；
 *               空闲点击 <=2px→经分割器发 handleClicked(m_index,0)
 *               （XGui 扩展迁移自旧分割器 ：800-809）；
 *             - mouseDoubleClickEvent：XGui 扩展 handleClicked(m_index,1)
 *               （Qt 把手无双击，迁移自旧 ：821-841）；
 *             - resizeEvent（Qt tiny-mode setMask :204-226）：XGui 无
 *               窗口遮罩对应物→把手全条带命中，**登记偏差**；
 *             - moveSplitter/closestLegalPosition：用 m_index 转调分割
 *               器公开对标 API（Qt 用 indexOf(this)（qsplitter.cpp:166/
 *               :183），XGui O(1) 等价；indexOf 把手匹配已扩，双真源
 *               靠槽位单点同步）。
 * @note       生命周期：父链级联析构为唯一所有权；外部拆卸走
 *             XObject_deleteLater（定式 XTableWidget.c:206）。不入
 *             XSplitter::m_pages 权威序表，防双释放。
 * @author     XinYueC 团队
 */
#ifndef XSPLITTERHANDLE_H
#define XSPLITTERHANDLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XSplitter.h"

#if XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

XCLASS_DEFINE_BEGING(XSplitterHandle)
XCLASS_DEFINE_EXTEND_END(XSplitterHandle, XWidget)

/**
 * @brief      XSplitterHandle 把手对象；m_base 必须是第一个成员。
 * @note       完整类型名 typedef 已由 XSplitter.h 前置声明（本头文件
 *             必经其包含），此处只补结构体定义。对标
 *             QSplitterHandlePrivate（qsplitter_p.h:114-119）+ XGui
 *             增补 m_index。
 */
struct XSplitterHandle
{
    XWidget m_base;             /**< 基类成员；必须是第一个。 */
    XSplitter* m_splitter;      /**< 所属分割器（对标 Qt sprivate，借用不持有）。 */
    int16_t m_mouseOffset;      /**< 按下时指针在条带内的轴向偏移（对标 Qt
                                     mouseOffset；拖动目标=条带原点+拾取−偏移）。 */
    int16_t m_pressPick;        /**< 按下时的轴向拾取坐标（空闲点击判定：
                                     释放点偏离 <=2px 视为点击而非拖动）。 */
    int16_t m_index;            /**< 分隔点号（介于页 m_index 与 m_index+1
                                     之间；由 XSplitter 槽位操作单点回写，
                                     对标 Qt indexOf(this) 的 O(1) 化）。 */
    int16_t m_dragTarget;       /**< 非透明拖动待落位把手位（条带原点坐标；
                                     迁移自旧分割器 dragTarget 动态属性）。 */
    uint8_t m_orient;           /**< 方向（XOrientation 取值 1/2，与分割器同步）。 */
    /* [内存治理] 标志位字。 */
    uint32_t m_hover : 1;       /**< 悬停（MouseOver 绘制态；Enter/Leave 刷新）。 */
    uint32_t m_pressed : 1;     /**< 按压中（Sunken 绘制态 + 拖动判定）。 */
    uint32_t m_dragTargetValid : 1; /**< m_dragTarget 已记录（区分「未记录」
                                     与「记录为 0」）。 */
    uint32_t m_handleFlagsUnused : 29; /**< 保留位。 */
};

/* ==================== 生命周期 ==================== */

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XSplitterHandle_class_init(void);
/**
 * @brief      初始化把手（对标 QSplitterHandle(Qt::Orientation, QSplitter*)
 *             构造函数）：挂入分割器子链并设置方向光标。
 * @note       m_index 由 XSplitter 的把手工厂（EXSplitter_CreateHandle）
 *             回写，本函数不置。
 */
void XSplitterHandle_init(XSplitterHandle* self, XOrientation orientation,
                          XSplitter* parent, XWidgetFlags flags);
/**
 * @brief      按指定内存类型创建把手实例（XGui 堆构造定式，对标 new）。
 */
XSplitterHandle* XSplitterHandle_create_ex(XMemoryType memory,
                                           XOrientation orientation,
                                           XSplitter* parent,
                                           XWidgetFlags flags);

/* ==================== 属性（对标 qsplitter.h:101-104 一带） ==================== */

/** @brief 设置方向并同步方向光标（分割器 setOrientation 遍历调用，
 *         对标 qsplitter.cpp:987-990 的把手侧同步）。 */
void XSplitterHandle_setOrientation(XSplitterHandle* self,
                                    XOrientation orientation);
/** @brief 查询方向（XOrientation_Horizontal/Vertical）。 */
XOrientation XSplitterHandle_orientation(const XSplitterHandle* self);
/** @brief 查询所属分割器（对标 Qt sprivate 访问；无主返回 NULL）。 */
XSplitter* XSplitterHandle_splitter(const XSplitterHandle* self);
/** @brief 查询所属分割器的不透明拖动（对标 opaqueResize，借分割器
 *         getter——含 SH_Splitter_OpaqueResize 样式回落）。 */
bool XSplitterHandle_opaqueResize(const XSplitterHandle* self);
/** @brief 查询本把手分隔点号（介于页 i 与页 i+1 之间；对标
 *         splitterIndex，O(1) 读 m_index）。未挂入分割器返回 -1。 */
int XSplitterHandle_splitterIndex(const XSplitterHandle* self);

/* ==================== 拖动落位（对标 public Q_SLOTS moveSplitter） ==================== */

/** @brief 把本把手分隔点移到 position（对标 QSplitterHandle::moveSplitter
 *         槽：position 经分割器 adjustPos 迟滞夹取后落位并发射
 *         splitterMoved）。index 形参保 Qt 槽签名，实现恒用 m_index。 */
void XSplitterHandle_moveSplitter(XSplitterHandle* self, int position,
                                  int index);
/** @brief 查询 position 就近合法位置（对标 Qt private closestLegalPosition
 *         ：按 getRange 近区间夹取）。 */
int XSplitterHandle_closestLegalPosition(const XSplitterHandle* self,
                                         int position);

/* ==================== 尺寸提示（CT_Splitter 样式接线） ==================== */

/** @brief 把手尺寸提示（对标 QSplitterHandle::sizeHint，
 *         qsplitter.cpp:191-199：经 XStyle_sizeFromContents(CT_Splitter,
 *         opt, {把手宽, 把手宽}) 计算返回）。 */
XSize XSplitterHandle_sizeHint(const XSplitterHandle* self);

/* ==================== 内部共享（XSplitter 实现，把手调用） ==================== */

/* 非透明拖动（opaqueResize=false）的橡皮筋：Qt 在把手 mouseMove 非
 * opaque 分支 setRubberBand(show) 且 release setRubberBand(-1)
 * （deleteLater，qsplitter.cpp:255-301/:1301-1327）。橡皮筋为
 * XRubberBand(Line) 子件由分割器持有，本函数为分割器实现、把手调用的
 * 内部共享入口（对标 QSplitterPrivate::setRubberBand；rBord=3 的
 * 3px 内缩在 XRubberBand 自绘路径无对应物，登记）。 */
void xsp_setRubberBand(XSplitter* self, int pos);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */

#endif /* XSPLITTERHANDLE_H */
