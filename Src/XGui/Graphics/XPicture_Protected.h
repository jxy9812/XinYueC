/*****************************************************************************
 * @file       XPicture_Protected.h
 * @brief      XPicture 记录协议内部接口（Qt 私有面对位下沉）。
 * @details    Qt 6.8 的 QPicture 公开面只有 play/load/save/data/setData
 *             （qpicture.h），命令记录协议（QAbstractPicture 的
 *             begin/drawLine/drawPath/end 等）位于私有层
 *             qpicture_p.h / qabstractpicture_p.h；本库此前把 31 个
 *             record* 命令 + clearCommands/isValidStream 提升为公开 API。
 *             按「可见性下沉」纪律，这些内部协议入口统一迁到本保护面
 *             头文件：公开头 XPicture.h 不再声明，调用方须显式 include
 *             本头（当前调用方为 XPainter.c 与 XPicture.c）。
 *             对外可用的公开面保持 play/load/save/size/data 等。
 ******************************************************************************/
#ifndef XPICTURE_PROTECTED_H
#define XPICTURE_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XPicture.h"

/**
 * @brief 记录绘制直线命令。
 * @param self 目标图片对象指针。
 * @param x1 起点 X 坐标。
 * @param y1 起点 Y 坐标。
 * @param x2 终点 X 坐标。
 * @param y2 终点 Y 坐标。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawLine(XPicture* self, int x1, int y1, int x2, int y2);
/**
 * @brief 记录单点绘制命令（对标 Qt QPicture 的 PdcDrawPoint）。
 * @param self 目标图片对象指针。
 * @param x 点的 X 坐标。
 * @param y 点的 Y 坐标。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawPoint(XPicture* self, int x, int y);
/**
 * @brief 记录 UTF-8 单行文本绘制命令。
 * @param self 目标图片对象指针。
 * @param x 文本左侧 X 坐标。
 * @param baselineY 文本基线 Y 坐标。
 * @param utf8 以 NUL 结尾的 UTF-8 文本；空字符串按无操作处理。
 * @param color 文本 ARGB32 颜色。
 * @param font 记录时使用的点阵字体；用于更新 Picture 边界，可为 NULL。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 文本字节数限制为 4096，流中保存原始 UTF-8，不保存主机指针。
 */
bool XPicture_recordDrawText(XPicture* self, int x, int baselineY,
                             const char* utf8, uint32_t color,
                             const XFont* font);
/**
 * @brief 记录完整画笔状态命令（对标 Qt QPicturePaintEngine::updatePen）。
 * @param self 目标图片对象指针。
 * @param color 画笔 ARGB32 颜色。
 * @param style 画笔线段样式数值；与 XPainterPenStyle 枚举一致。
 * @param width 画笔宽度；0 表示 cosmetic 画笔。
 * @param cap 画笔端点样式数值；与 XPainterPenCapStyle 枚举一致。
 * @param join 画笔拐角样式数值；与 XPainterPenJoinStyle 枚举一致。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetPen(XPicture* self, uint32_t color, int style,
                           int width, int cap, int join);
/**
 * @brief 记录当前字体状态（对标 Qt QPicture 的 PdcSetFont）。
 * @param self 目标图片对象指针。
 * @param font 要记录的字体；NULL 表示恢复默认字体。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 便携快照复用 XFont_toString()，保存家族、字号、字重和样式等
 *       当前点阵文字后端实际使用的属性；复杂 Qt 字体引擎属性不在此流中。
 */
bool XPicture_recordSetFont(XPicture* self, const XFont* font);
/**
 * @brief 记录绘制器整体不透明度。
 * @param self 目标图片对象指针。
 * @param opacity 不透明度，范围为 0.0 到 1.0；调用方应传入有限值。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetOpacity(XPicture* self, float opacity);
/**
 * @brief 记录绘制器合成模式。
 * @param self 目标图片对象指针。
 * @param mode 合成模式数值，与 XPainterCompositionMode 枚举一致。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetCompositionMode(XPicture* self, int mode);
/**
 * @brief 记录绘制器渲染提示位集合。
 * @param self 目标图片对象指针。
 * @param hints 渲染提示位集合，与 XPainterRenderHints 数值一致。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetRenderHints(XPicture* self, uint32_t hints);
/**
 * @brief 记录画刷原点。
 * @param self 目标图片对象指针。
 * @param x 画刷原点 X 坐标，使用单精度便携表示。
 * @param y 画刷原点 Y 坐标，使用单精度便携表示。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetBrushOrigin(XPicture* self, float x, float y);
/** @brief 记录背景颜色；回放时恢复为实心背景画刷。 */
bool XPicture_recordSetBackgroundColor(XPicture* self, uint32_t color);
/** @brief 记录背景填充模式；0 为透明，1 为不透明。 */
bool XPicture_recordSetBackgroundMode(XPicture* self, int mode);
/**
 * @brief 记录基础画刷状态。
 * @param self 目标图片对象指针。
 * @param style 画刷样式枚举数值；便携流固定记录四字节整数。
 * @param color 画刷基色，使用 ARGB32 表示。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 该接口只覆盖普通样式和基色；渐变、纹理等 QBrush 资源不在此
 *       固定负载中编码，调用方应为这些样式保留原有边界语义。
 */
bool XPicture_recordSetBrush(XPicture* self, int style, uint32_t color);
#if XPAINTER_BRUSH_ON
/**
 * @brief 记录渐变画刷的便携固定负载。
 * @param self 目标图片对象指针。
 * @param gradient 渐变描述；停止点数量不能超过 XPAINTER_GRADIENT_MAX_STOPS。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 仅保存线性、径向和锥形渐变的几何参数与停止点，不保存 Qt
 *       的 spread、坐标模式、插值模式和画刷变换；纹理画刷仍由基础
 *       画刷边界处理。
 */
bool XPicture_recordSetBrushGradient(XPicture* self,
                                     const XPainterGradient* gradient);
#endif /* XPAINTER_BRUSH_ON */
/**
 * @brief 记录世界变换矩阵及其启用状态。
 * @param self 目标图片对象指针。
 * @param matrix 世界变换矩阵；九个元素按 XImageTransform 字段顺序记录。
 * @param enabled 是否在回放绘制中应用该世界矩阵。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 便携流使用九个 IEEE-754 单精度值和一个无符号启用标志；这与
 *       XPainter 的矩阵状态宽度一致，不暴露主机结构体填充或字节序。
 */
bool XPicture_recordSetTransform(XPicture* self,
                                 const XImageTransform* matrix,
                                 bool enabled);
/**
 * @brief 记录逻辑窗口矩形及其视图变换启用状态。
 * @param self 目标图片对象指针。
 * @param window 逻辑坐标窗口；矩形四个整数按固定宽度写入。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note Qt 的 setWindow 会保留任意整数矩形，并立即刷新组合矩阵；本记录
 *       不对宽高作有效性裁剪，以便退化窗口的错误行为由回放绘制阶段处理。
 */
bool XPicture_recordSetWindow(XPicture* self, const XRect* window);
/**
 * @brief 记录设备视口矩形及其视图变换启用状态。
 * @param self 目标图片对象指针。
 * @param viewport 设备坐标视口；矩形四个整数按固定宽度写入。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetViewport(XPicture* self, const XRect* viewport);
/**
 * @brief 记录 window/viewport 视图变换是否启用。
 * @param self 目标图片对象指针。
 * @param enabled true 表示应用视图映射，false 表示暂时忽略映射。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetViewTransformEnabled(XPicture* self, bool enabled);
/**
 * @brief 记录裁剪启用状态。
 * @param self 目标图片对象指针。
 * @param enabled true 表示启用当前裁剪，false 表示暂时禁用。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetClipEnabled(XPicture* self, bool enabled);
/**
 * @brief 记录矩形裁剪及其组合操作。
 * @param self 目标图片对象指针。
 * @param rect 逻辑坐标裁剪矩形。
 * @param operation XPainterClipOperation 数值：0=NoClip、1=ReplaceClip、
 *                2=IntersectClip。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSetClipRect(XPicture* self, const XRect* rect,
                                int operation);
/**
 * @brief 记录区域裁剪及其组合操作。
 * @param self 目标图片对象指针。
 * @param region 逻辑坐标矩形区域；允许为空区域。
 * @param operation XPainterClipOperation 数值：0=NoClip、1=ReplaceClip、
 *                2=IntersectClip。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 区域以固定宽度矩形数组编码，保持 Qt PdcSetClipRegion 的可回放
 *       语义；复杂路径裁剪不在此接口中编码。
 */
bool XPicture_recordSetClipRegion(XPicture* self, const XRegion* region,
                                  int operation);
/**
 * @brief 记录填充矩形命令。
 * @param self 目标图片对象指针。
 * @param rect 要填充的矩形。
 * @param color 填充颜色，使用 ARGB32 表示。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordFillRect(XPicture* self, const XRect* rect, uint32_t color);
/**
 * @brief 记录高层形状命令（椭圆/圆弧/扇形/弦/圆角矩形）。
 * @param self 目标图片对象指针。
 * @param shapeOp XPainterShapeOp 枚举数值；1=椭圆、2=圆弧、3=扇形、
 *                4=弦、5=圆角矩形。
 * @param rect 形状外接矩形，回放时使用记录当时的归一化矩形。
 * @param startAngle 起始角，单位 1/16 度；无角度形状传 0。
 * @param spanAngle 跨越角，单位 1/16 度；无角度形状传 0。
 * @param filled 是否同时按记录时的画刷状态填充内部。
 * @param xRadius X 方向圆角半径；非圆角形状传 0。
 * @param yRadius Y 方向圆角半径；非圆角形状传 0。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawShape(XPicture* self, int shapeOp, const XRect* rect,
                              int startAngle, int spanAngle, bool filled,
                              int xRadius, int yRadius);
/**
 * @brief 记录折线命令（对标 QPicture 中 drawPolyline 的独立路径命令）。
 * @param self 目标图片对象指针。
 * @param points 顶点数组。
 * @param count 顶点数量；少于 2 时按无操作返回 true。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawPolyline(XPicture* self, const XPoint* points,
                                 int count);
/**
 * @brief 记录多边形命令（对标 QPicture 中 drawPolygon 的独立路径命令）。
 * @param self 目标图片对象指针。
 * @param points 顶点数组。
 * @param count 顶点数量；少于 2 时按无操作返回 true。
 * @param filled 是否按记录时的画刷状态填充内部。
 * @param fillRule XPainterFillRule 枚举数值；0=奇偶、1=非零绕组。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawPolygon(XPicture* self, const XPoint* points, int count,
                                bool filled, int fillRule);
/**
 * @brief 记录点集命令（对标 QPicture 中 drawPoints 的独立路径命令）。
 * @param self 目标图片对象指针。
 * @param points 点集数组。
 * @param count 点数量；少于 1 时按无操作返回 true。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawPoints(XPicture* self, const XPoint* points, int count);
/**
 * @brief 记录路径绘制命令（对标 QPicture 中 drawPath/fillPath/strokePath
 *        对应的 PdcDrawPath 指令）。
 * @param self 目标图片对象指针。
 * @param pathOp XPainterPathOp 枚举数值；1=drawPath、2=fillPath、3=strokePath。
 * @param path 要记录的路径对象；NULL 或空路径按无操作返回 true。
 * @return 命令写入成功返回 true，否则返回 false。
 * @note 路径 fillRule 以尾随 4 字节随本指令持久化（与 DrawTiledPixmap
 *       尾随 extra 同惯例），回放恢复；旧录制流无该字段时保持默认
 *       OddEven 零回归。
 */
bool XPicture_recordDrawPath(XPicture* self, int pathOp,
                             const struct XPainterPath* path);
/**
 * @brief 记录绘制图像命令。
 * @param self 目标图片对象指针。
 * @param image 待绘制的源图像。
 * @param x 目标左上角 X 坐标。
 * @param y 目标左上角 Y 坐标。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawImage(XPicture* self, const XImage* image, int x, int y);
/**
 * @brief 记录平铺像素图命令。
 * @param self 目标图片对象指针。
 * @param image 平铺用源图像，需包含完整像素及设备像素比元数据。
 * @param rect 目标逻辑矩形；宽度或高度不大于零时按无操作处理。
 * @param offset 平铺起始偏移，按逻辑像素解释；NULL 等价于零偏移。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawTiledPixmap(XPicture* self, const XImage* image,
                                    const XRect* rect, const XPoint* offset);
#if XPAINTER_PIXMAP_ON && XPAINTER_IMAGE_RECT_ON
/**
 * @brief 记录带目标矩形和源矩形的像素图绘制命令。
 * @param self 目标图片对象指针。
 * @param image 像素图对应的源图像，记录时复制全部像素和元数据。
 * @param targetRect 目标逻辑矩形；不取得所有权。
 * @param sourceRect 源物理像素矩形；不取得所有权。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordDrawPixmap(XPicture* self, const XImage* image,
                               const XRect* targetRect,
                               const XRect* sourceRect);
#endif /* XPAINTER_PIXMAP_ON && XPAINTER_IMAGE_RECT_ON */
/**
 * @brief 记录保存绘制状态命令。
 * @param self 目标图片对象指针。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordSave(XPicture* self);
/**
 * @brief 记录恢复绘制状态命令。
 * @param self 目标图片对象指针。
 * @return 命令写入成功返回 true，否则返回 false。
 */
bool XPicture_recordRestore(XPicture* self);
/**
 * @brief 清除图片中的全部绘制命令。
 * @param self 目标图片对象指针。
 */
void XPicture_clearCommands(XPicture* self);
/**
 * @brief 检查图片数据是否为当前命令流格式。
 * @param self 图片对象指针。
 * @return 数据有效返回 true，否则返回 false。
 */
bool XPicture_isValidStream(const XPicture* self);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPICTURE_PROTECTED_H */
