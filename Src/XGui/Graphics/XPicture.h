/******************************************************************************
 * @file       XPicture.h
 * @brief      XPicture 绘图指令记录与回放类（对标 Qt 6.8 QPicture）
 * @author     XinYueC 团队
 * @note       提供绘图指令的录制和回放功能，支持矢量图形序列化
 ******************************************************************************/
#ifndef XPICTURE_H
#define XPICTURE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XGeometry.h"
#include "XTypes.h"
#include "XString.h"
#include "XPainter_config.h"

typedef struct XIODevice XIODevice;

/* XPicture 持有紧凑的便携命令流；XPainter 仅保存回调，不依赖窗口工具包。 */
typedef struct XImage XImage;
typedef struct XPainter XPainter;
typedef struct XImageTransform XImageTransform;
typedef struct XFont XFont;
struct XPainterPath;

/* 流常量属于 XGui C ABI；无论主机架构如何，字节流始终采用小端序。 */
#define XPICTURE_STREAM_VERSION 1u
#define XPICTURE_HEADER_SIZE 40u
#define XPICTURE_RECORD_HEADER_SIZE 8u
#define XPICTURE_MAGIC_SIZE 8u

typedef enum XPictureOpcode
{
    XPictureOpcode_DrawLine = 1,
    XPictureOpcode_FillRect = 2,
    XPictureOpcode_DrawImage = 3,
    XPictureOpcode_Save = 4,
    XPictureOpcode_Restore = 5,
    XPictureOpcode_DrawShape = 6,
    XPictureOpcode_DrawPolyline = 7,
    XPictureOpcode_DrawPolygon = 8,
    XPictureOpcode_DrawPoints = 9,
    XPictureOpcode_DrawPath = 10,
    XPictureOpcode_SetPen = 11, /**< 完整画笔状态（对标 QPicture PdcSetPen）。 */
    XPictureOpcode_SetOpacity = 12, /**< 绘制器不透明度（对标 PdcSetOpacity）。 */
    XPictureOpcode_SetCompositionMode = 13, /**< 合成模式（对标 PdcSetCompositionMode）。 */
    XPictureOpcode_SetRenderHints = 14, /**< 渲染提示位（对标 PdcSetRenderHint）。 */
    XPictureOpcode_SetBrushOrigin = 15, /**< 画刷原点（对标 PdcSetBrushOrigin）。 */
    XPictureOpcode_SetTransform = 16, /**< 世界变换矩阵（对标 PdcSetWMatrix）。 */
    XPictureOpcode_SetBackgroundColor = 17, /**< 背景颜色（对标 PdcSetBkColor）。 */
    XPictureOpcode_SetBackgroundMode = 18, /**< 背景模式（对标 PdcSetBkMode）。 */
    XPictureOpcode_SetBrush = 19, /**< 基础画刷样式与颜色（对标 PdcSetBrush）。 */
    XPictureOpcode_SetWindow = 20, /**< 逻辑窗口矩形（对标 QPainter::setWindow）。 */
    XPictureOpcode_SetViewport = 21, /**< 设备视口矩形（对标 QPainter::setViewport）。 */
    XPictureOpcode_SetViewTransformEnabled = 22, /**< 视图变换启用状态。 */
    XPictureOpcode_SetClipEnabled = 23, /**< 裁剪启用状态（对标 PdcSetClipEnabled）。 */
    XPictureOpcode_SetClipRect = 24, /**< 矩形裁剪（对标 PdcSetClipRegion 的矩形子集）。 */
    XPictureOpcode_SetClipRegion = 25, /**< 区域裁剪（对标 PdcSetClipRegion）。 */
    XPictureOpcode_DrawTiledPixmap = 26, /**< 平铺像素图（对标 PdcDrawTiledPixmap）。 */
    XPictureOpcode_DrawPixmap = 27, /**< 矩形像素图（对标 PdcDrawPixmap）。 */
    XPictureOpcode_SetFont = 28, /**< 字体状态（对标 PdcSetFont）。 */
    XPictureOpcode_DrawPoint = 29 /**< 单点绘制（对标 PdcDrawPoint）。 */
} XPictureOpcode;

/* ========== XPicture 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XPicture)
XCLASS_DEFINE_EXTEND_END(XPicture, XClass)

/* 前向声明 */
typedef struct XPicturePrivate XPicturePrivate;

/**
 * @brief      XPicture 绘图记录类结构体（对标 Qt 6.8 QPicture）
 * @note       继承自 XClass，记录 QPainter 绘图指令并支持回放和序列化
 */
typedef struct XPicture
{
    XClass              m_class;   /**< 继承的基类成员 */
    XPicturePrivate*     m_data;    /**< 私有数据指针 */
}XPicture;

/**
 * @brief      初始化 XPicture 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XPicture_class_init();

/**
 * @brief      在堆上创建 XPicture 实例
 * @return     指向新创建的 XPicture 对象的指针，失败返回 NULL
 */
XPicture* XPicture_create_ex(XMemoryType memory);

/** @brief 由已有记录创建深拷贝对象。 */
XPicture* XPicture_create_copy(const XPicture* other, XMemoryType memory);

/** @brief 创建对象并转移已有记录的所有权。 */
XPicture* XPicture_create_move(XPicture* other, XMemoryType memory);

/**
 * @brief      初始化 XPicture 实例
 * @param self          待初始化的 XPicture 对象指针
 * @param formatVersion 格式版本号（-1 表示使用默认版本）
 */
void XPicture_init(XPicture* self, int formatVersion);

/**
 * @brief      复制构造函数
 * @param self 目标 XPicture 对象指针
 * @param other 源 XPicture 对象指针
 */

/**
 * @brief      移动构造函数
 * @param self 目标 XPicture 对象指针
 * @param other 源 XPicture 对象指针（移动后源对象变为空）
 */

/**
 * @brief      释放 XPicture 资源
 * @param self 待释放的 XPicture 对象指针
 */
/** @brief 交换两个记录对象的数据所有权。 */
void XPicture_swap(XPicture* self, XPicture* other);

/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
/**
 * @brief 通过 XClass 虚表复制绘图记录。
 * @param self 目标绘图记录对象指针。
 * @param other 源绘图记录对象指针。
 */
#define XPicture_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/**
 * @brief 通过 XClass 虚表移动绘图记录。
 * @param self 目标绘图记录对象指针。
 * @param other 源绘图记录对象指针；移动后源对象为空。
 */
#define XPicture_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放绘图记录。 @param self 待释放的绘图记录指针。 */
#define XPicture_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
/** @brief 删除堆上的绘图记录对象。 @param self 待删除的绘图记录指针。 */
#define XPicture_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 查询方法 ========== */

/**
 * @brief      判断绘图记录是否为 null
 * @param self 目标 XPicture 对象指针
 * @return 为 null 返回 true
 */
bool XPicture_isNull(const XPicture* self);

/** @brief 返回绘图设备类型（对标 QPicture::devType）。 */
int XPicture_devType(const XPicture* self);

/** @brief 返回底层绘图引擎；纯 C 记录无独立引擎时返回 NULL。 */
void* XPicture_paintEngine(const XPicture* self);

/**
 * @brief      获取绘图记录数据大小
 * @param self 目标 XPicture 对象指针
 * @return 数据大小（字节）
 */
uint32_t XPicture_size(const XPicture* self);

/**
 * @brief      获取绘图记录数据指针
 * @param self 目标 XPicture 对象指针
 * @return 数据指针
 */
const char* XPicture_data(const XPicture* self);

/**
 * @brief      设置绘图记录数据
 * @param self 目标 XPicture 对象指针
 * @param data 数据指针
 * @param size 数据大小
 */
void XPicture_setData(XPicture* self, const char* data, uint32_t size);

/**
 * @brief      获取边界矩形
 * @param self 目标 XPicture 对象指针
 * @param out  输出矩形结构体指针
 */
void XPicture_boundingRect(const XPicture* self, XRect* out);

/**
 * @brief      设置边界矩形
 * @param self 目标 XPicture 对象指针
 * @param rect 矩形结构体指针
 */
void XPicture_setBoundingRect(XPicture* self, const XRect* rect);

/* ========== 操作方法 ========== */

/**
 * @brief      回放绘图指令到指定的纯 C XPainter
 * @param self    目标 XPicture 对象指针；回放期间只读
 * @param painter XPainter 指针；NULL 仅对空记录有效
 * @return 回放成功返回 true
 */
bool XPicture_play(const XPicture* self, XPainter* painter);

/* 将命令写入便携流；溢出、分配失败、已有数据畸形或输入无效时返回 false。 */
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

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载绘图记录
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 加载成功返回 true；文件无法打开时返回 false 并将图片置为空；文件已打开
 *         但流格式无效时返回 false，同时保留已读的非空数据供 size()/data() 查询。
 */
bool XPicture_load(XPicture* self, const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名加载图片的兼容重载。
 * @param self 目标图片对象指针。
 * @param fileName UTF-8 编码的文件名。
 * @return 加载成功返回 true，失败返回 false。
 */
bool XPicture_load_2(XPicture* self, const char* fileName);

/**
 * @brief 从 XIODevice 读取绘图记录。
 * @param self 目标图片对象指针。
 * @param device 输入设备指针，由调用方持有。
 * @return 读取成功返回 true，否则返回 false。与 Qt 6.8 一致，设备读取到的非空
 *         畸形流会保留在对象中，仅通过返回值和 XPicture_isValidStream() 报告失败；
 *         空流或读取失败会得到空图片。
 */
bool XPicture_load_device(XPicture* self, XIODevice* device);

/**
 * @brief      保存绘图记录到文件
 * @param self     目标 XPicture 对象指针
 * @param fileName 文件名
 * @return 文件名有效且设备成功打开时返回 true；Qt 6.8 不检查后续设备写入结果，
 *         因此短写或写入错误不会改变返回值。
 */
bool XPicture_save(const XPicture* self, const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名保存图片的兼容重载。
 * @param self 源图片对象指针。
 * @param fileName UTF-8 编码的目标文件名。
 * @return 文件名有效且设备成功打开时返回 true；设备写入短写或错误不会改变返回值，
 *         参数无效或打开失败时返回 false。
 */
bool XPicture_save_2(const XPicture* self, const char* fileName);

/**
 * @brief 将绘图记录写入设备。
 * @param self 目标绘图记录对象指针。
 * @param device 接收字节流的设备指针。
 * @return 记录和设备指针有效时返回 true；Qt 6.8 不检查设备 write 的短写或错误返回，
 *         本接口同样不检查，也不会额外调用 flush；参数无效时返回 false。
 */
bool XPicture_save_device(const XPicture* self, XIODevice* device);

/* ========== 数据分离 ========== */

/**
 * @brief      分离数据（写时复制）
 * @param self 目标 XPicture 对象指针
 */
void XPicture_detach(XPicture* self);

/**
 * @brief      判断数据是否已分离
 * @param self 目标 XPicture 对象指针
 * @return 已分离返回 true
 */
bool XPicture_isDetached(const XPicture* self);

#ifdef __cplusplus
}
#endif

/* XClass 创建 API 的默认内存类型封装。 */
#undef XPicture_create
#define XPicture_create() XPicture_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#define XPicture_create_copy_default(other) XPicture_create_copy((other), XCLASS_DEFAULT_MEMORY_TYPE)
#define XPicture_create_move_default(other) XPicture_create_move((other), XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XPICTURE_H */
