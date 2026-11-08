/******************************************************************************
 * @file       XBrush.h
 * @brief      画刷值类型（对标 Qt 6.8 QBrush）。
 * @details    XBrush 是填充描述值类型：样式（XBrushStyle，数值与 Qt 6.8
 *             Qt::BrushStyle 完全一致）+ 颜色 + 渐变 + 纹理 + 变换。
 *             本类型只含 8 字节句柄（指向常驻数据块），按值拷贝与赋值
 *             安全，不需要调用方释放——与 XGui 调色板/样式选项按值传递
 *             的既有契约一致（对齐 QBrush 的隐式共享语义；本库以进程级
 *             驻留池实现，省去引用计数与析构钩子）。
 *             渐变/纹理指针按借用语义保存：对象归调用方所有，调用方须
 *             保证其生命周期覆盖画刷使用期（主题/样式对象即典型宿主）。
 * @note       模块开关：随 XGUI 公共头提供，无独立开关。本类型不依赖
 *             绘制器，可在无 XPainter 的裁剪配置下使用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBRUSH_H
#define XBRUSH_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XColor.h"
#include "XGeometry.h"

/** @brief 渐变/纹理句柄的前向声明（借用语义，定义见 XGradient.h/XPixmap.h）。 */
typedef struct XGradient XGradient;
typedef struct XPixmap XPixmap;
typedef struct XImage XImage;
typedef struct XPainter XPainter;

/** @brief 仿射变换（与 XImage.h 的 XImageTransform 同布局；此处前置声明避免
 *         画刷头反向依赖图像模块——使用处包含 XImage.h 即可互操作）。 */
typedef struct XBrushTransform
{
    float m11, m12, m21, m22; /**< 线性部分。 */
    float dx, dy;             /**< 平移部分。 */
    float m13, m23, m33;      /**< 透视/齐次系数；仿射时 0/0/1。 */
} XBrushTransform;

/**
 * @brief 画刷样式（对标 Qt 6.8 Qt::BrushStyle，数值一致）。
 */
typedef enum XBrushStyle
{
    XBrushStyle_NoBrush = 0,                  /**< 不填充（对标 NoBrush）。 */
    XBrushStyle_SolidPattern = 1,             /**< 纯色（对标 SolidPattern）。 */
    XBrushStyle_Dense1Pattern = 2,            /**< 12.5% 密度（对标 Dense1Pattern）。 */
    XBrushStyle_Dense2Pattern = 3,            /**< 25% 密度。 */
    XBrushStyle_Dense3Pattern = 4,            /**< 37.5% 密度。 */
    XBrushStyle_Dense4Pattern = 5,            /**< 50% 密度。 */
    XBrushStyle_Dense5Pattern = 6,            /**< 62.5% 密度。 */
    XBrushStyle_Dense6Pattern = 7,            /**< 75% 密度。 */
    XBrushStyle_Dense7Pattern = 8,            /**< 87.5% 密度。 */
    XBrushStyle_HorPattern = 9,               /**< 水平线（对标 HorPattern）。 */
    XBrushStyle_VerPattern = 10,              /**< 垂直线（对标 VerPattern）。 */
    XBrushStyle_CrossPattern = 11,            /**< 十字线（对标 CrossPattern）。 */
    XBrushStyle_BDiagPattern = 12,            /**< 反斜线（对标 BDiagPattern）。 */
    XBrushStyle_FDiagPattern = 13,            /**< 正斜线（对标 FDiagPattern）。 */
    XBrushStyle_DiagCrossPattern = 14,        /**< 交叉斜线（对标 DiagCrossPattern）。 */
    XBrushStyle_LinearGradientPattern = 15,   /**< 线性渐变（对标 LinearGradientPattern）。 */
    XBrushStyle_RadialGradientPattern = 16,   /**< 径向渐变（对标 RadialGradientPattern）。 */
    XBrushStyle_ConicalGradientPattern = 17,  /**< 锥形渐变（对标 ConicalGradientPattern）。 */
    XBrushStyle_TexturePattern = 24           /**< 纹理（对标 TexturePattern）。 */
} XBrushStyle;

/** @brief 画刷值类型：仅 8 字节句柄，按值拷贝安全。 */
typedef struct XBrush
{
    const struct XBrushData* m_data; /**< 常驻数据块（只读共享；NULL=NoBrush）。 */
} XBrush;

/**
 * @brief      创建 NoBrush 画刷（对标 QBrush()）。
 * @details    返回空句柄（m_data=NULL），所有读取接口按 NoBrush 语义回退：
 *             样式=NoBrush、颜色=不透明黑、渐变/纹理=NULL、变换=单位阵。
 * @return     画刷值；不分配堆内存。
 */
XBrush XBrush_create(void);
/**
 * @brief      按样式创建画刷（对标 QBrush(Qt::BrushStyle)）。
 * @param      style 画刷样式（XBrushStyle）。
 * @return     画刷值；颜色为默认不透明黑。
 */
XBrush XBrush_create_style(XBrushStyle style);
/**
 * @brief      按颜色与样式创建画刷（对标 QBrush(const QColor&, Qt::BrushStyle)）。
 * @param      color 画刷颜色（SolidPattern 下即填充色）。
 * @param      style 画刷样式；常用 XBrushStyle_SolidPattern。
 * @return     画刷值；句柄来自驻留池。
 */
XBrush XBrush_create_color(XColor color, XBrushStyle style);
/**
 * @brief      初始化嵌入式画刷为 NoBrush（对标默认构造）。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XBrush_init(XBrush* self);
/**
 * @brief      反初始化画刷（对标析构占位）。
 * @details    常驻池实现下数据不归调用方所有，本函数只清空句柄，
 *             不释放任何资源；保留入口以对称值类型生命周期写法。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XBrush_deinit(XBrush* self);
/**
 * @brief      拷贝画刷（对标拷贝构造）。
 * @details    8B 句柄赋值，共享同一只读数据块；源对象生命周期不受影响。
 * @param      dest 目标画刷；可为 NULL（无操作）。
 * @param      src 源画刷；NULL 时目标置 NoBrush。
 * @return     无返回值。
 */
void XBrush_copy(XBrush* dest, const XBrush* src);
/**
 * @brief      交换两个画刷（对标 QBrush::swap）。
 * @param      a 画刷 A；可为 NULL（无操作）。
 * @param      b 画刷 B；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XBrush_swap(XBrush* a, XBrush* b);

/**
 * @brief      查询画刷样式（对标 style）。
 * @param      self 目标画刷；可为 NULL。
 * @return     样式枚举；NULL 或 NoBrush 返回 XBrushStyle_NoBrush。
 */
XBrushStyle XBrush_style(const XBrush* self);
/**
 * @brief      设置画刷样式（对标 setStyle）。
 * @details    值语义：生成并挂接新句柄，不影响共享同一数据的其他画刷。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @param      style 新样式（XBrushStyle）。
 * @return     无返回值。
 */
void XBrush_setStyle(XBrush* self, XBrushStyle style);
/**
 * @brief      查询画刷颜色（对标 color）。
 * @param      self 目标画刷；可为 NULL。
 * @return     颜色值；NULL/NoBrush 返回不透明黑。
 */
XColor XBrush_color(const XBrush* self);
/**
 * @brief      设置画刷颜色（对标 setColor）。
 * @details    SolidPattern 下即填充色；渐变样式下作为渐变外的回退色。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @param      color 新颜色值。
 * @return     无返回值。
 */
void XBrush_setColor(XBrush* self, XColor color);
/**
 * @brief      查询渐变（对标 gradient）。
 * @param      self 目标画刷；可为 NULL。
 * @return     渐变借用指针；未设置返回 NULL。
 * @warning    返回值不归画刷所有，调用方不得释放。
 */
const XGradient* XBrush_gradient(const XBrush* self);
/**
 * @brief      设置渐变（对标 setGradient）。
 * @details    拥有语义：渐变按值拷入常驻池并去重，调用方可在设置后释放
 *             源对象；设置后样式自动切到 LinearGradientPattern。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @param      gradient 渐变源指针；NULL 表示清除渐变。
 * @return     无返回值。
 */
void XBrush_setGradient(XBrush* self, const XGradient* gradient);
/**
 * @brief      查询纹理（对标 texture）。
 * @param      self 目标画刷；可为 NULL。
 * @return     纹理借用指针；未设置返回 NULL。
 * @warning    返回值不归画刷所有，调用方不得释放。
 */
const XPixmap* XBrush_texture(const XBrush* self);
/**
 * @brief      设置纹理（对标 setTexture）。
 * @details    拥有语义：纹理经类拷贝归入常驻池（XPixmap 内部像素数据
 *             为引用计数，拷贝即 COW），调用方可在设置后释放源对象；
 *             设置后样式自动切到 TexturePattern，并清除纹理图像槽。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @param      texture 纹理源指针；NULL 表示清除纹理。
 * @return     无返回值。
 */
void XBrush_setTexture(XBrush* self, const XPixmap* texture);
/**
 * @brief      查询纹理图像（对标 textureImage）。
 * @param      self 目标画刷；可为 NULL。
 * @return     纹理图像借用指针；未显式设置图像时返回 NULL。
 * @warning    返回值不归画刷所有，调用方不得释放。
 */
const XImage* XBrush_textureImage(const XBrush* self);
/**
 * @brief      设置纹理图像（对标 setTextureImage）。
 * @details    借用语义；设置后样式自动切到 TexturePattern，并清除
 *             XPixmap 纹理槽（两者互斥）。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @param      image 纹理图像借用指针；NULL 表示清除。
 * @return     无返回值。
 */
void XBrush_setTextureImage(XBrush* self, const XImage* image);
/**
 * @brief      分离共享数据（对标 detach）。
 * @details    常驻池数据本身只读共享、无写时复制需求，本函数为空操作；
 *             提供入口只为对齐 Qt 调用习惯。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XBrush_detach(XBrush* self);
/**
 * @brief      查询变换（对标 transform）。
 * @param      self 目标画刷；可为 NULL。
 * @return     变换矩阵；NULL/未设置返回单位阵。
 */
XBrushTransform XBrush_transform(const XBrush* self);
/**
 * @brief      设置变换（对标 setTransform）。
 * @param      self 目标画刷；可为 NULL（无操作）。
 * @param      transform 新变换；NULL 复位为单位阵。
 * @return     无返回值。
 */
void XBrush_setTransform(XBrush* self, const XBrushTransform* transform);

/**
 * @brief      查询是否不透明（对标 isOpaque）。
 * @details    仅 SolidPattern 且 alpha=255 判定为不透明；带纹理（位图或
 *             图像）一律按可能含透明像素返回 false（保守策略）。
 * @param      self 目标画刷；可为 NULL。
 * @return     不透明返回 true；NULL/NoBrush/带纹理返回 false。
 */
bool XBrush_isOpaque(const XBrush* self);
/**
 * @brief      比较两个画刷是否相等（对标 operator==）。
 * @details    逐字段比较样式、颜色、渐变指针、纹理指针与变换矩阵。
 * @param      a 画刷 A；可为 NULL。
 * @param      b 画刷 B；可为 NULL。
 * @return     相等返回 true；同一对象或均为空句柄也返回 true。
 */
bool XBrush_isEqual(const XBrush* a, const XBrush* b);
/**
 * @brief      查询是否独占数据（对标 isDetached）。
 * @details    常驻池数据只读共享，永无独占态，恒返回 false。
 * @param      self 目标画刷；可为 NULL。
 * @return     恒为 false。
 */
bool XBrush_isDetached(const XBrush* self);

/**
 * @brief      将画刷应用到绘制器（对标 QPainter::setBrush）。
 * @details    纯色走 setBrush；渐变把 XGradient 停止点转为 XPainterGradient
 *             并切到对应渐变样式；NoBrush 关闭填充；纹理暂回退纯色。
 *             XGui 的 XGradient 基类不带几何参数，渐变几何按目标矩形
 *             生成（线性：上→下；径向：矩形中心+短边半径；锥形：矩形中心）。
 * @param      self 画刷；NULL 视为 NoBrush。
 * @param      painter 目标绘制器；NULL 无操作。
 * @param      rect 目标矩形（渐变几何基准）；NULL 按 (0,0,1,1) 处理。
 * @return     无返回值。
 */
void XBrush_applyToPainter(const XBrush* self, XPainter* painter, const XRect* rect);

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XBRUSH_H */
