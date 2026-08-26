/** @file XPainter_config.h
 * @brief XGui XPainter 绘图器模块配置文件（对标 Qt 6.8 QPainter）。
 * @note  通过本文件可以逐类裁剪绘图功能，仅保留嵌入式实际需要的绘制
 *        能力以减小固件体积：
 *          1. XPAINTER_ON            - 总开关（XGuiConfig.h 统一定义）；
 *          2. XPAINTER_SHAPE_ON      - 形状绘制：椭圆/圆弧/扇形/弦/圆角矩形
 *                                      （对标 QPainter::drawEllipse /
 *                                      drawArc / drawPie / drawChord /
 *                                      drawRoundedRect）；
 *          3. XPAINTER_POLYGON_ON    - 多边形/折线/点集绘制
 *                                      （对标 drawPolygon / drawPolyline /
 *                                      drawPoints）；
 *          4. XPAINTER_PENSTYLE_ON   - 画笔样式与端点/拐角样式
 *                                      （对标 QPen Style/CapStyle/JoinStyle）；
 *          5. XPAINTER_BRUSH_ON      - 画刷体系：实心/无画刷及线性、径向、
 *                                      锥形渐变色画刷（对标 QBrush/Gradient）；
 *          6. XPAINTER_TEXTLAYOUT_ON - 文本布局：边界矩形内对齐、自动换行、
 *                                      多行绘制（对标 drawText(QRectF,flags,...)）。
 *          7. XPAINTER_LAYOUT_DIRECTION_ON - 绘制器文本布局方向状态
 *                                      （对标 QPainter::setLayoutDirection）。
 *          8. XPAINTER_RENDERHINT_ON    - 绘制器渲染提示状态
 *                                      （对标 QPainter::setRenderHint）。
 *          9. XPAINTER_WORLD_MATRIX_ON  - 世界变换启用状态
 *                                      （对标 QPainter::setWorldMatrixEnabled）。
 *         10. XPAINTER_VIEW_TRANSFORM_ON  - window/viewport 视图变换状态
 *                                      （对标 QPainter::setWindow /
 *                                      setViewport / setViewTransformEnabled）。
 *         11. XPAINTER_CLIP_ON       - 矩形裁剪状态与裁剪操作
 *                                      （对标 QPainter::setClipRect /
 *                                      setClipping / clipBoundingRect）。
 *        关闭某项开关后：
 *          - 对应公共 API 被条件编译裁剪，引用会触发编译错误，提醒调用方
 *            同步裁剪引用（硬裁剪语义，与 XLayout_config.h 保持一致）；
 *          - 对应 .c 实现整段不参与编译，静态符号不会进入固件。
 *        本文件只引入通用坐标类型（XGeometry 等），编译通过后不产生任何
 *        平台 API 调用，嵌入式可直接使用。
 */

#ifndef XPAINTER_CONFIG_H
#define XPAINTER_CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XGUI_ON 主开关已定义 */
#include "XGuiConfig.h"

/* ========================================================================== */
/*                        模块总开关                                          */
/* ========================================================================== */
/** @brief XPainter 模块总开关；置 0 时裁剪整个绘图器公共 API 与全部
 *  扩展能力。总开关在 XGuiConfig.h 中统一定义，此处仅兜底默认值。
 *  注意：布局、控件（XWidget/XLabel 等）依赖绘制能力，裁剪 XPainter
 *  时应同步裁剪这些依赖项。 */
#ifndef XPAINTER_ON
#define XPAINTER_ON 1
#endif

#if XPAINTER_ON

/* ========================================================================== */
/*                        各绘制能力开关                                      */
/* ========================================================================== */

/** @brief 形状绘制开关；置 0 时裁剪椭圆、圆弧、扇形、弦与圆角矩形绘制
 *  API（XPainter_drawEllipse / drawArc / drawPie / drawChord /
 *  drawRoundedRect）。保留基础 drawRect/drawLine/fillRect。 */
#ifndef XPAINTER_SHAPE_ON
#define XPAINTER_SHAPE_ON 1
#endif

/** @brief 多边形/折线/点集绘制开关；置 0 时裁剪 XPainter_drawPolygon /
 *  drawPolyline / drawPoints。 */
#ifndef XPAINTER_POLYGON_ON
#define XPAINTER_POLYGON_ON 1
#endif

/** @brief 画笔样式开关；置 0 时裁剪画笔样式（实线/虚线/点线等）与
 *  端点/拐角样式相关 API；画笔颜色与宽度仍保留。 */
#ifndef XPAINTER_PENSTYLE_ON
#define XPAINTER_PENSTYLE_ON 1
#endif

/** @brief 画刷体系开关；置 0 时裁剪画刷样式（无/实心）与线性、径向、
 *  锥形渐变色画刷 API；XPainter_setBrush（颜色版）仍保留。 */
#ifndef XPAINTER_BRUSH_ON
#define XPAINTER_BRUSH_ON 1
#endif

/** @brief 路径绘制开关；置 0 时裁剪 QPainterPath 风格的路径对象
 *  （XPainterPath / drawPath / fillPath / strokePath）。 */
#ifndef XPAINTER_PATH_ON
#define XPAINTER_PATH_ON 1
#endif

/** @brief 文本布局开关；置 0 时裁剪边界矩形内对齐/自动换行/多行绘制
 *  API（XPainter_drawTextRect）；单行基线绘制 drawText 仍保留。 */
#ifndef XPAINTER_TEXTLAYOUT_ON
#define XPAINTER_TEXTLAYOUT_ON 1
#endif

/** @brief 文本布局方向开关；置 0 时不保存绘制器方向状态，文本按从左到右
 * 处理；置 1 时提供 QPainter::setLayoutDirection/layoutDirection 对应 API。 */
#ifndef XPAINTER_LAYOUT_DIRECTION_ON
#define XPAINTER_LAYOUT_DIRECTION_ON 1
#endif

/** @brief 渲染提示开关；置 0 时裁剪 RenderHint 状态及其查询/修改 API。 */
#ifndef XPAINTER_RENDERHINT_ON
#define XPAINTER_RENDERHINT_ON 1
#endif

/** @brief 世界变换状态开关；置 0 时始终应用当前变换矩阵，并裁剪矩阵启用查询/修改 API。 */
#ifndef XPAINTER_WORLD_MATRIX_ON
#define XPAINTER_WORLD_MATRIX_ON 1
#endif

/** @brief window/viewport 视图变换开关；置 0 时裁剪逻辑窗口、设备视口及
 *  视图变换状态 API，绘制仅使用世界变换，适合坐标固定的嵌入式界面。 */
#ifndef XPAINTER_VIEW_TRANSFORM_ON
#define XPAINTER_VIEW_TRANSFORM_ON 1
#endif

/** @brief 矩形裁剪开关；置 0 时裁剪 ClipOperation、裁剪状态及对应公共 API，
 *  软件光栅后端不执行逐像素裁剪，适合无需局部重绘保护的嵌入式界面。 */
#ifndef XPAINTER_CLIP_ON
#define XPAINTER_CLIP_ON 1
#endif

#endif /* XPAINTER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPAINTER_CONFIG_H */
