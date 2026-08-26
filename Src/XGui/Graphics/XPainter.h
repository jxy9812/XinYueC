/******************************************************************************
 * @file       XPainter.h
 * @brief      XPainter 绘图器类（对标 Qt 6.8 QPainter）
 * @author     XinYueC 团队
 * @note       纯 C 的绘图器，提供两种内置绘制后端，可整体替换：
 *             - XPainter_begin_image()：把绘制命令输出到 XImage（软件光栅化）；
 *             - XPainter_begin_picture()：把绘制命令录制到 XPicture（矢量，
 *               可保存/加载/回放）。
 *             XPainter 通过五个回调（m_drawLine / m_fillRect / m_drawImage /
 *             m_save / m_restore）把图形操作派发给当前设备后端，因此上层
 *             也可以自定义回调实现自己的绘制引擎（如 XIcon、测试探针），
 *             无需依赖任何平台 API，可在嵌入式环境直接使用。
 ******************************************************************************/
#ifndef XPAINTER_H
#define XPAINTER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XPicture.h"
#include "XImage.h"
#include "XGeometry.h"
#include "XPainter_config.h"
#include "XFont.h"

/* XPicture_play() 通过 XPainter 的五类回调派发指令；完整回调签名见下。 */

/**
 * @brief      绘制直线回调原型。
 * @param painter 绘制器对象指针。
 * @param x1 起点 X 坐标。
 * @param y1 起点 Y 坐标。
 * @param x2 终点 X 坐标。
 * @param y2 终点 Y 坐标。
 * @return 绘制成功返回 true。
 */
typedef bool (*XPainterDrawLineProc)(XPainter* painter, int x1, int y1,
                                     int x2, int y2);
/**
 * @brief      填充矩形回调原型。
 * @param painter 绘制器对象指针。
 * @param rect 待填充的矩形（不保证为空矩形）。
 * @param color ARGB32 填充颜色。
 * @return 绘制成功返回 true。
 */
typedef bool (*XPainterFillRectProc)(XPainter* painter, const XRect* rect,
                                     uint32_t color);
/**
 * @brief      绘制图像回调原型。
 * @param painter 绘制器对象指针。
 * @param image 待绘制的源图像（保证非空）。
 * @param x 目标左上角 X 坐标。
 * @param y 目标左上角 Y 坐标。
 * @return 绘制成功返回 true。
 */
typedef bool (*XPainterDrawImageProc)(XPainter* painter, const XImage* image,
                                      int x, int y);
/**
 * @brief      保存绘制状态回调原型。
 * @param painter 绘制器对象指针。
 * @return 保存成功返回 true。
 */
typedef bool (*XPainterSaveProc)(XPainter* painter);
/**
 * @brief      恢复绘制状态回调原型。
 * @param painter 绘制器对象指针。
 * @return 恢复成功返回 true。
 */
typedef bool (*XPainterRestoreProc)(XPainter* painter);

#if XPAINTER_SHAPE_ON
/**
 * @brief      形状命令枚举（对标 QPainter 形状 API 的子集）。
 */
typedef enum XPainterShapeOp
{
    XPainterShapeOp_Ellipse = 1,      /**< 椭圆（drawEllipse）。 */
    XPainterShapeOp_Arc = 2,          /**< 圆弧（drawArc）。 */
    XPainterShapeOp_Pie = 3,          /**< 扇形（drawPie）。 */
    XPainterShapeOp_Chord = 4,        /**< 弦形（drawChord）。 */
    XPainterShapeOp_RoundedRect = 5   /**< 圆角矩形（drawRoundedRect）。 */
} XPainterShapeOp;

/**
 * @brief      形状绘制回调原型。
 * @param painter 绘制器对象指针。
 * @param op 形状命令枚举。
 * @param rect 外接矩形。
 * @param startAngle 起始角，1/16 度；无角度形状传 0。
 * @param spanAngle 跨越角，1/16 度；无角度形状传 0。
 * @param filled 是否填充内部；无填充概念时传 false。
 * @param xRadius X 方向圆角半径；非圆角形状传 0。
 * @param yRadius Y 方向圆角半径；非圆角形状传 0。
 * @return 绘制成功返回 true。
 */
typedef bool (*XPainterDrawShapeProc)(XPainter* painter, XPainterShapeOp op,
                                     const XRect* rect, int startAngle,
                                     int spanAngle, bool filled,
                                     int xRadius, int yRadius);
#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON
/**
 * @brief      折线绘制回调原型。
 * @param painter 绘制器对象指针。
 * @param points 顶点数组；回调应只读该数组，数组由调用方保持有效至函数返回。
 * @param count 顶点数量。
 * @return 绘制成功返回 true。
 */
typedef bool (*XPainterDrawPolylineProc)(XPainter* painter,
                                         const XPoint* points, int count);

/**
 * @brief      多边形绘制回调原型。
 * @param painter 绘制器对象指针。
 * @param points 顶点数组；回调应只读该数组，数组由调用方保持有效至函数返回。
 * @param count 顶点数量。
 * @param filled 是否要求使用当前画刷填充内部。
 * @return 绘制成功返回 true。
 */
typedef bool (*XPainterDrawPolygonProc)(XPainter* painter,
                                        const XPoint* points, int count,
                                        bool filled);

/**
 * @brief      点集绘制回调原型。
 * @param painter 绘制器对象指针。
 * @param points 点数组；回调应只读该数组，数组由调用方保持有效至函数返回。
 * @param count 点数量。
 * @return 全部绘制成功返回 true。
 */
typedef bool (*XPainterDrawPointsProc)(XPainter* painter,
                                       const XPoint* points, int count);
#endif /* XPAINTER_POLYGON_ON */

#if XPAINTER_PATH_ON
/**
 * @brief      XPainterPath 前向声明（完整定义见文件后部）。
 */
typedef struct XPainterPath XPainterPath;

/**
 * @brief      路径绘制命令枚举（对标 QPainter::drawPath/fillPath/strokePath）。
 */
typedef enum XPainterPathOp
{
    XPainterPathOp_Draw = 1,   /**< 填充并描边路径（drawPath）。 */
    XPainterPathOp_Fill = 2,   /**< 仅填充路径内部（fillPath）。 */
    XPainterPathOp_Stroke = 3  /**< 仅描边路径轮廓（strokePath）。 */
} XPainterPathOp;

/**
 * @brief      路径绘制高层回调原型。
 * @param painter 绘制器对象指针。
 * @param op 路径绘制命令枚举。
 * @param path 路径对象；回调应只读该对象，对象由调用方保持有效至函数返回。
 * @return 绘制成功返回 true。
 * @note       回调非空时优先于内置路径展平实现；NULL/空路径不会派发本回调。
 */
typedef bool (*XPainterDrawPathProc)(XPainter* painter, XPainterPathOp op,
                                     const XPainterPath* path);
#endif /* XPAINTER_PATH_ON */

/**
 * @brief      合成模式枚举（对标 QPainter::CompositionMode）。
 */
typedef enum XPainterCompositionMode
{
    XPainterCompositionMode_SourceOver = 0, /**< 源覆盖模式（默认，带 Alpha 混合） */
    XPainterCompositionMode_Source = 1      /**< 源替换模式（整体覆盖目标像素） */
} XPainterCompositionMode;

#if XPAINTER_CLIP_ON
/**
 * @brief 裁剪操作枚举（对标 Qt::ClipOperation，数值与 Qt 6.8 一致）。
 */
typedef enum XPainterClipOperation
{
    XPainterClipOperation_NoClip = 0,       /**< 关闭当前裁剪。 */
    XPainterClipOperation_ReplaceClip = 1,  /**< 用新矩形替换当前裁剪。 */
    XPainterClipOperation_IntersectClip = 2 /**< 新矩形与当前裁剪取交集。 */
} XPainterClipOperation;
#endif /* XPAINTER_CLIP_ON */

#if XPAINTER_RENDERHINT_ON
/** @brief 渲染提示位（对标 QPainter::RenderHint，数值与 Qt 6.8 一致）。 */
typedef enum XPainterRenderHint
{
    XPainterRenderHint_Antialiasing = 0x01,
    XPainterRenderHint_TextAntialiasing = 0x02,
    XPainterRenderHint_SmoothPixmapTransform = 0x04,
    XPainterRenderHint_VerticalSubpixelPositioning = 0x08,
    XPainterRenderHint_LosslessImageRendering = 0x40,
    XPainterRenderHint_NonCosmeticBrushPatterns = 0x80
} XPainterRenderHint;
typedef uint32_t XPainterRenderHints;
#endif /* XPAINTER_RENDERHINT_ON */

/* ========== 画笔/画刷/渐变色/文本布局类型（对标 QPen/QBrush/QGradient/TextFlag） ========== */

#if XPAINTER_PENSTYLE_ON
/** @brief 画笔线段样式（对标 Qt 6.8 QPen::Style）。 */
typedef enum XPainterPenStyle
{
    XPainterPenStyle_NoPen = 0,      /**< 不绘制线段 */
    XPainterPenStyle_SolidLine = 1,  /**< 实线 */
    XPainterPenStyle_DashLine = 2,   /**< 虚线 */
    XPainterPenStyle_DotLine = 3,    /**< 点线 */
    XPainterPenStyle_DashDotLine = 4,    /**< 一点一划 */
    XPainterPenStyle_DashDotDotLine = 5 /**< 两点一划 */
} XPainterPenStyle;

/** @brief 画笔端点样式（对标 Qt 6.8 QPen::CapStyle）。 */
typedef enum XPainterPenCapStyle
{
    XPainterPenCapStyle_FlatCap = 0,  /**< 平头端点 */
    XPainterPenCapStyle_SquareCap = 1,/**< 方头端点（含半线宽外延） */
    XPainterPenCapStyle_RoundCap = 2  /**< 圆头端点（同样可用作圆点） */
} XPainterPenCapStyle;

/** @brief 画笔拐角样式（对标 Qt 6.8 QPen::JoinStyle）。 */
typedef enum XPainterPenJoinStyle
{
    XPainterPenJoinStyle_MiterJoin = 0, /**< 斜接拐角（默认） */
    XPainterPenJoinStyle_BevelJoin = 1, /**< 平切拐角 */
    XPainterPenJoinStyle_RoundJoin = 2  /**< 圆角拐角 */
} XPainterPenJoinStyle;
#endif /* XPAINTER_PENSTYLE_ON */

#if XPAINTER_BRUSH_ON
/** @brief 画刷样式（对标 Qt 6.8 QBrush::Style）。 */
typedef enum XPainterBrushStyle
{
    XPainterBrushStyle_NoBrush = 0,              /**< 不填充 */
    XPainterBrushStyle_SolidPattern = 1,         /**< 纯色填充 */
    XPainterBrushStyle_LinearGradientPattern = 2,/**< 线性渐变色 */
    XPainterBrushStyle_RadialGradientPattern = 3,/**< 径向渐变色 */
    XPainterBrushStyle_ConicalGradientPattern = 4 /**< 锥形渐变色 */
} XPainterBrushStyle;

/** @brief 渐变色类型（对标 Qt 6.8 QGradient::Type）。 */
typedef enum XPainterGradientType
{
    XPainterGradientType_Linear = 0,
    XPainterGradientType_Radial = 1,
    XPainterGradientType_Conical = 2
} XPainterGradientType;

/** @brief 渐变色停止点（对标 QGradientStop）。 */
typedef struct XPainterGradientStop
{
    float m_position;   /**< 0.0~1.0 位置。 */
    uint32_t m_color;   /**< ARGB32 颜色。 */
} XPainterGradientStop;

/** @brief 单段渐变色中最多停止点数量（固定数组，嵌入式友好，无需堆分配）。 */
#define XPAINTER_GRADIENT_MAX_STOPS 8

/** @brief 渐变色描述（对标 QGradient 的最小子集）。
 * @note 线性渐变：m_start=(startX,startY)、m_end=(endX,endY)；
 *       径向渐变：m_center 为圆心、m_focal 为焦点、m_radius/hr 为半径；
 *       锥形渐变：m_center 为锥心，m_angleDeg 为起始角。 */
typedef struct XPainterGradient
{
    XPainterGradientType m_type;                  /**< 渐变类型。 */
    float m_startX, m_startY;                     /**< 线性渐变起点。 */
    float m_endX, m_endY;                         /**< 线性渐变终点。 */
    float m_centerX, m_centerY;                   /**< 径向/锥形中心。 */
    float m_focalX, m_focalY;                     /**< 径向焦点。 */
    float m_radius;                               /**< 径向半径。 */
    float m_angleDeg;                             /**< 锥形起始角（度，顺时针）。 */
    int m_stopCount;                              /**< 停止点数量。 */
    XPainterGradientStop m_stops[XPAINTER_GRADIENT_MAX_STOPS]; /**< 停止点表。 */
} XPainterGradient;

/** @brief 画刷描述（对标 QBrush 的最小子集）。 */
typedef struct XPainterBrush
{
    XPainterBrushStyle m_style;   /**< 画刷样式。 */
    uint32_t m_color;             /**< 纯色画刷颜色（ARGB32）。 */
    XPainterGradient m_gradient;  /**< 渐变色画刷描述（仅渐变样式有意义）。 */
} XPainterBrush;
#endif /* XPAINTER_BRUSH_ON */

#if XPAINTER_TEXTLAYOUT_ON
/** @brief 文本布局标志（对标 Qt 6.8 Qt::AlignmentFlag / Qt::TextFlag 全集合，
 *  数值与 Qt 保持一致）。 */
typedef uint32_t XPainterTextFlags;
enum
{
    XPAINTER_TEXT_ALIGN_LEFT    = 0x0001u,  /**< 水平左对齐（AlignLeft）。 */
    XPAINTER_TEXT_ALIGN_RIGHT   = 0x0002u,  /**< 水平右对齐（AlignRight）。 */
    XPAINTER_TEXT_ALIGN_HCENTER = 0x0004u,  /**< 水平居中（AlignHCenter）。 */
    XPAINTER_TEXT_ALIGN_JUSTIFY = 0x0008u,  /**< 两端对齐（AlignJustify）。 */
    XPAINTER_TEXT_ALIGN_ABSOLUTE = 0x0010u, /**< 左右对齐不随布局方向翻转（AlignAbsolute）。 */
    XPAINTER_TEXT_ALIGN_TOP     = 0x0020u,  /**< 垂直顶对齐（AlignTop）。 */
    XPAINTER_TEXT_ALIGN_BOTTOM  = 0x0040u,  /**< 垂直底对齐（AlignBottom）。 */
    XPAINTER_TEXT_ALIGN_VCENTER = 0x0080u,  /**< 垂直居中（AlignVCenter）。 */
    XPAINTER_TEXT_CENTER = XPAINTER_TEXT_ALIGN_HCENTER | XPAINTER_TEXT_ALIGN_VCENTER,
    XPAINTER_TEXT_SINGLE_LINE        = 0x0100u, /**< 单行模式（TextSingleLine）。 */
    XPAINTER_TEXT_DONT_CLIP          = 0x0200u, /**< 不裁剪到矩形（TextDontClip）。 */
    XPAINTER_TEXT_EXPAND_TABS        = 0x0400u, /**< 制表符展开（TextExpandTabs）。 */
    XPAINTER_TEXT_SHOW_MNEMONIC      = 0x0800u, /**< 显示助记键下划线（TextShowMnemonic）。 */
    XPAINTER_TEXT_WORD_WRAP          = 0x1000u, /**< 按词换行（TextWordWrap）。 */
    XPAINTER_TEXT_WRAP_ANYWHERE      = 0x2000u, /**< 任意处换行（TextWrapAnywhere）。 */
    XPAINTER_TEXT_DONT_PRINT         = 0x4000u, /**< 不打印文本（TextDontPrint）。 */
    XPAINTER_TEXT_HIDE_MNEMONIC      = 0x8000u, /**< 隐藏助记键（TextHideMnemonic）。 */
    XPAINTER_TEXT_JUSTIFICATION_FORCED = 0x10000u, /**< 强制两端对齐（仅标记，不产生拉伸）。 */
    XPAINTER_TEXT_FORCE_LTR          = 0x20000u, /**< 强制从左到右（当前仅左对齐）。 */
    XPAINTER_TEXT_FORCE_RTL          = 0x40000u, /**< 强制从右到左（未实现 RTL 字库）。 */
    XPAINTER_TEXT_LONGEST_VARIANT    = 0x80000u, /**< 多变体取最长（当前无多变体）。 */
    XPAINTER_TEXT_INCLUDE_TRAILING_SPACES = 0x08000000u /**< 包含行尾空格（TextIncludeTrailingSpaces）。 */
};
#endif /* XPAINTER_TEXTLAYOUT_ON */

/**
 * @brief      当前绑定的绘制设备类型枚举。
 * @note       内部成员 m_deviceKind 使用，上层仅可通过
 *             XPainter_isActive()/XPainter_device() 查询。
 */
typedef enum XPainterDeviceKind
{
    XPainterDevice_None = 0,   /**< 未绑定设备 */
    XPainterDevice_Image = 1,  /**< 已绑定 XImage（软件光栅化） */
    XPainterDevice_Picture = 2 /**< 已绑定 XPicture（指令录制） */
} XPainterDeviceKind;

#if XPAINTER_LAYOUT_DIRECTION_ON
/** @brief 文本布局方向（对标 Qt::LayoutDirection）。 */
typedef enum XPainterLayoutDirection
{
    XPainterLayoutDirection_LeftToRight = 0, /**< 从左到右。 */
    XPainterLayoutDirection_RightToLeft = 1, /**< 从右到左。 */
    XPainterLayoutDirection_Auto = 2        /**< 根据文本内容自动判断。 */
} XPainterLayoutDirection;
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */

/**
 * @brief      XPainter 当前绘制状态快照。
 * @note       save()/restore() 保存与恢复的正是该结构体内容；
 *             上层可直接通过 XPainter 对象的 m_state 成员只读查询。
 */
typedef struct XPainterState
{
#if XPAINTER_PENSTYLE_ON
    XPainterPenStyle m_penStyle;          /**< 画笔线段样式（对标 QPen::style）。 */
    XPainterPenCapStyle m_penCap;         /**< 画笔端点样式（对标 QPen::capStyle）。 */
    XPainterPenJoinStyle m_penJoin;       /**< 画笔拐角样式（对标 QPen::joinStyle）。 */
#endif
    uint32_t m_penColor;        /**< 画笔颜色（ARGB32）。 */
    int m_penWidth;             /**< 画笔宽度（像素，>=1）。 */
    uint32_t m_brushColor;      /**< 画刷颜色（ARGB32）。 */
#if XPAINTER_BRUSH_ON
    XPainterBrush m_brush;      /**< 完整画刷描述（对标 QBrush，含样式与渐变色）。 */
#endif
    uint32_t m_backgroundColor; /**< 背景颜色（ARGB32，默认不透明白；对标 QPainter::setBackground）。 */
#if XPAINTER_CLIP_ON
    bool m_hasClip;             /**< 是否启用裁剪（false 表示不裁剪）。 */
    bool m_hasClipRect;         /**< 是否保存过可重新启用的裁剪记录。 */
    XRect m_clipRect;           /**< 当前组合裁剪的设备坐标包围矩形。 */
    XPainterClipOperation m_clipOperation; /**< 最近一次裁剪操作。 */
#endif /* XPAINTER_CLIP_ON */
    XImageTransform m_transform; /**< 用户坐标到设备坐标的变换矩阵。 */
#if XPAINTER_WORLD_MATRIX_ON
    bool m_worldMatrixEnabled;  /**< 是否把 m_transform 应用于绘制（对标 QPainter::worldMatrixEnabled）。 */
#endif /* XPAINTER_WORLD_MATRIX_ON */
#if XPAINTER_VIEW_TRANSFORM_ON
    XRect m_window;             /**< 逻辑窗口矩形（对标 QPainter::window）。 */
    XRect m_viewport;           /**< 设备视口矩形（对标 QPainter::viewport）。 */
    bool m_viewTransformEnabled;/**< 是否把 window/viewport 映射应用于绘制。 */
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    float m_opacity;            /**< 整体不透明度（0.0~1.0）。 */
    XPainterCompositionMode m_compositionMode; /**< 合成模式。 */
#if XPAINTER_RENDERHINT_ON
    XPainterRenderHints m_renderHints; /**< 渲染提示位集合。 */
#endif /* XPAINTER_RENDERHINT_ON */
#if XPAINTER_LAYOUT_DIRECTION_ON
    XPainterLayoutDirection m_layoutDirection; /**< 文本布局方向。 */
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */
    XFont m_font;               /**< 当前字体（对标 QPainter::font）。 */
} XPainterState;

/**
 * @brief      XPainter 绘图器类结构体（对标 Qt 6.8 QPainter）。
 * @note       值样式上下文结构体，不持有图像/图片所有权，调用方负责
 *             管理被绘制对象生命周期。结构体内存有内部状态栈指针，
 *             请勿用 memcpy/赋值拷贝；需要复用请重新 init。
 *             - m_state：当前绘制状态，可通过公开 setter 修改；
 *             - m_stateStack：save()/restore() 使用的状态栈，勿直接操作；
 *             - 回调字段在 XPainter_begin_image()/begin_picture()、
 *               XPainter_end() 时由 XPainter 自动维护，也可手工替换
 *               （如测试探针、其他绘制引擎）。
 */
typedef struct XPainter
{
    void* m_userData;                 /**< 用户数据指针，不转移所有权。 */

    /* ========== 绘制引擎回调（两种内置后端 + 自定义引擎共用） ========== */
    XPainterDrawLineProc m_drawLine;  /**< 画线回调。 */
    XPainterFillRectProc m_fillRect;  /**< 填充矩形回调。 */
    XPainterDrawImageProc m_drawImage;/**< 绘制图像回调。 */
    XPainterSaveProc m_save;          /**< 保存状态回调。 */
    XPainterRestoreProc m_restore;    /**< 恢复状态回调。 */
#if XPAINTER_SHAPE_ON
    XPainterDrawShapeProc m_drawShape;/**< 形状命令高层回调；为空时按基础指令分解。 */
#endif
#if XPAINTER_POLYGON_ON
    XPainterDrawPolylineProc m_drawPolyline;/**< 折线高层回调；为空时按画线逐段分解。 */
    XPainterDrawPolygonProc m_drawPolygon;  /**< 多边形高层回调；为空时按扫描线本地实现。 */
    XPainterDrawPointsProc m_drawPoints;    /**< 点集高层回调；为空时逐点绘制。 */
#endif
#if XPAINTER_PATH_ON
    XPainterDrawPathProc m_drawPath;        /**< 路径绘制高层回调；为空时按路径展平分解。 */
#endif
    /* ========== 当前绑定设备 ========== */
    XPainterDeviceKind m_deviceKind;  /**< 已绑定的设备类型。 */
    XImage* m_image;                  /**< 软件光栅化目标图像（仅 Image 后端）。 */
    XPicture* m_picture;              /**< 指令录制目标图片（仅 Picture 后端）。 */

    /* ========== 绘制状态 ========== */
    XPainterState m_state;            /**< 当前绘制状态。 */

    /* ========== save/restore 状态栈（内部维护，勿直接访问） ========== */
    XPainterState* m_stateStack;      /**< 状态栈动态数组。 */
    int m_stateCount;                 /**< 栈内状态数量。 */
    int m_stateCapacity;              /**< 栈容量。 */
} XPainter;

/* ========== 生命周期 ========== */

/**
 * @brief      初始化 XPainter（不绑定设备，设置默认绘制状态）。
 * @param self 待初始化的绘制器指针。
 * @param userData 用户数据指针，可通过 m_userData 访问，不转移所有权。
 */
void XPainter_init(XPainter* self, void* userData);

/**
 * @brief      释放 XPainter 内部资源（状态栈并解除设备绑定）。
 * @param self 待释放的绘制器指针；NULL 时直接返回。
 */
void XPainter_deinit(XPainter* self);

/* ========== 设备绑定 ========== */

/**
 * @brief      绑定软件光栅化后端，后续绘制输出到指定图像。
 * @param self 绘制器指针。
 * @param image 目标图像；可为任意像素格式，空图像返回 false。
 * @return 绑定成功返回 true。
 * @note       绑定时自动结束上一次绑定；图像所有权仍归调用方。
 */
bool XPainter_begin_image(XPainter* self, XImage* image);

/**
 * @brief      绑定指令录制后端，后续绘制作为指令录制到指定图片。
 * @param self 绘制器指针。
 * @param picture 目标图片；录制命令会追加入其现有指令流。
 * @return 绑定成功返回 true。
 * @note       绑定时自动结束上一次绑定；图片所有权仍归调用方。
 */
bool XPainter_begin_picture(XPainter* self, XPicture* picture);

/**
 * @brief      结束绘制：解除设备绑定、清空状态栈并恢复默认状态。
 * @param self 绘制器指针。
 * @return 结束成功返回 true。
 */
bool XPainter_end(XPainter* self);

/**
 * @brief      判断绘制器是否已绑定设备。
 * @param self 绘制器指针。
 * @return 已绑定返回 true。
 */
bool XPainter_isActive(const XPainter* self);

/**
 * @brief      返回当前绑定的设备指针。
 * @param self 绘制器指针。
 * @return XImage* / XPicture* 指针；未绑定时返回 NULL。
 */
void* XPainter_device(const XPainter* self);

/* ========== 绘制操作（按当前 transform/clip/opacity 输出） ========== */

/**
 * @brief      绘制一条直线（对标 QPainter::drawLine）。
 * @param self 绘制器指针。
 * @param x1 起点 X 坐标。
 * @param y1 起点 Y 坐标。
 * @param x2 终点 X 坐标。
 * @param y2 终点 Y 坐标。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawLine(XPainter* self, int x1, int y1, int x2, int y2);
/**
 * @brief      以两个点绘制一条直线的重载版本。
 * @param self 绘制器指针。
 * @param p1 起点；NULL 返回 false。
 * @param p2 终点；NULL 返回 false。
 * @return 绘制成功返回 true。
 */
bool XPainter_drawLine_2(XPainter* self, const XPoint* p1, const XPoint* p2);

/**
 * @brief      绘制一个点（用当前画笔）。
 * @param self 绘制器指针。
 * @param x 点 X 坐标。
 * @param y 点 Y 坐标。
 * @return 绘制成功返回 true。
 */
bool XPainter_drawPoint(XPainter* self, int x, int y);
/**
 * @brief      以点结构绘制一个点的重载版本。
 * @param self 绘制器指针。
 * @param point 点指针；NULL 返回 false。
 * @return 绘制成功返回 true。
 */
bool XPainter_drawPoint_2(XPainter* self, const XPoint* point);

/**
 * @brief      绘制矩形边框（用当前画笔，沿四条边内侧各一像素）。
 * @param self 绘制器指针。
 * @param rect 矩形；NULL 或空矩形视为无操作返回 true。
 * @return 绘制成功返回 true。
 */
bool XPainter_drawRect(XPainter* self, const XRect* rect);

/**
 * @brief      批量绘制矩形（对标 QPainter::drawRects）。
 * @param self 绘制器指针。
 * @param rects 矩形数组；NULL 或 count<=0 视为无操作返回 true。
 * @param rectCount 矩形数量。
 * @return 全部绘制成功返回 true；未绑定设备或任一绘制失败返回 false。
 */
bool XPainter_drawRects(XPainter* self, const XRect* rects, int rectCount);

/**
 * @brief      批量绘制直线（对标 QPainter::drawLines(QPoint*, lineCount)）。
 * @param self 绘制器指针。
 * @param pointPairs 线段端点数组，每两个 XPoint 组成一条线；
 *                   NULL 或 pairCount<=0 视为无操作返回 true。
 * @param pairCount 线段数量。
 * @return 全部绘制成功返回 true；未绑定设备或任一绘制失败返回 false。
 */
bool XPainter_drawLines(XPainter* self, const XPoint* pointPairs, int pairCount);

/**
 * @brief      填充矩形（对标 QPainter::fillRect，显式颜色）。
 * @param self 绘制器指针。
 * @param rect 矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @param color ARGB32 填充颜色。
 * @return 填充成功返回 true。
 */
bool XPainter_fillRect(XPainter* self, const XRect* rect, uint32_t color);
/**
 * @brief      用当前画刷颜色填充矩形的重载版本。
 * @param self 绘制器指针。
 * @param rect 矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @return 填充成功返回 true。
 */
bool XPainter_fillRect_2(XPainter* self, const XRect* rect);

/**
 * @brief      用背景颜色擦除矩形区域（对标 QPainter::eraseRect）。
 * @param self 绘制器指针。
 * @param rect 目标矩形；NULL 或空矩形视为无操作返回 true。
 * @return 成功返回 true；未绑定设备返回 false。
 */
bool XPainter_eraseRect(XPainter* self, const XRect* rect);

/**
 * @brief      绘制图像（对标 QPainter::drawImage，最近邻采样）。
 * @param self 绘制器指针。
 * @param image 源图像；NULL 返回 false，空图像视为无操作返回 true。
 * @param x 目标左上角 X 坐标。
 * @param y 目标左上角 Y 坐标。
 * @return 绘制成功返回 true。
 */
bool XPainter_drawImage(XPainter* self, const XImage* image, int x, int y);
/**
 * @brief      以位置点绘制图像的重载版本。
 * @param self 绘制器指针。
 * @param image 源图像；NULL 返回 false，空图像视为无操作返回 true。
 * @param pos 目标左上角位置；NULL 返回 false。
 * @return 绘制成功返回 true。
 */
bool XPainter_drawImage_2(XPainter* self, const XImage* image, const XPoint* pos);

/**
 * @brief      在指定位置回放绘图记录（对标 QPainter::drawPicture）。
 * @details    内部 save + translate(x,y) + XPicture_play + restore，
 *             不影响调用者后续绘制的变换与状态。
 * @param self 绘制器指针。
 * @param picture 绘图记录；NULL 返回 false，空记录视为无操作返回 true。
 * @param x 目标左上角 X 坐标。
 * @param y 目标左上角 Y 坐标。
 * @return 回放成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawPicture(XPainter* self, XPicture* picture, int x, int y);

#if XPAINTER_SHAPE_ON

/**
 * @brief      绘制椭圆（对标 QPainter::drawEllipse）。
 * @param self 绘制器指针。
 * @param rect 外接矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawEllipse(XPainter* self, const XRect* rect);

/**
 * @brief      绘制圆弧（对标 QPainter::drawArc）。
 * @param self 绘制器指针。
 * @param rect 外接矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @param startAngle 起始角，1/16 度。
 * @param spanAngle 跨越角，1/16 度；0 视为无操作返回 true。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawArc(XPainter* self, const XRect* rect,
                      int startAngle, int spanAngle);

/**
 * @brief      绘制扇形（对标 QPainter::drawPie）。
 * @param self 绘制器指针。
 * @param rect 外接矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @param startAngle 起始角，1/16 度。
 * @param spanAngle 跨越角，1/16 度；0 视为无操作返回 true。
 * @param filled 是否用当前画刷填充内部。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawPie(XPainter* self, const XRect* rect,
                      int startAngle, int spanAngle, bool filled);

/**
 * @brief      绘制弦形（对标 QPainter::drawChord）。
 * @param self 绘制器指针。
 * @param rect 外接矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @param startAngle 起始角，1/16 度。
 * @param spanAngle 跨越角，1/16 度；0 视为无操作返回 true。
 * @param filled 是否用当前画刷填充内部。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawChord(XPainter* self, const XRect* rect,
                        int startAngle, int spanAngle, bool filled);

/**
 * @brief      绘制圆角矩形（对标 QPainter::drawRoundedRect）。
 * @param self 绘制器指针。
 * @param rect 目标矩形；NULL 返回 false，空矩形视为无操作返回 true。
 * @param xRadius X 方向圆角半径；小于等于 0 时退化为普通矩形。
 * @param yRadius Y 方向圆角半径；小于等于 0 时退化为普通矩形。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawRoundedRect(XPainter* self, const XRect* rect,
                              int xRadius, int yRadius);

#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON

/**
 * @brief      绘制折线（对标 QPainter::drawPolyline）。
 * @param self 绘制器指针。
 * @param points 顶点数组；NULL 或 count<=0 视为无操作返回 true。
 * @param count 顶点数量。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawPolyline(XPainter* self, const XPoint* points, int count);

/**
 * @brief      绘制多边形（对标 QPainter::drawPolygon）。
 * @param self 绘制器指针。
 * @param points 顶点数组；NULL 或 count<=0 视为无操作返回 true。
 * @param count 顶点数量。
 * @param filled 是否用当前画刷填充内部。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawPolygon(XPainter* self, const XPoint* points, int count,
                          bool filled);

/**
 * @brief      绘制凸多边形（对标 QPainter::drawConvexPolygon）。
 * @details    Qt 的该接口始终用当前画刷填充、当前画笔描边，因此无填充
 *             参数；内部当前复用 drawPolygon 的扫描线实现，不区分凸凹性。
 * @param self 绘制器指针。
 * @param points 顶点数组；NULL 或 count<=0 视为无操作返回 true。
 * @param count 顶点数量。
 * @return 成功返回 true。
 */
bool XPainter_drawConvexPolygon(XPainter* self, const XPoint* points,
                                int count);

/**
 * @brief      绘制点集（对标 QPainter::drawPoints）。
 * @param self 绘制器指针。
 * @param points 点数组；NULL 或 count<=0 视为无操作返回 true。
 * @param count 点数量。
 * @return 全部绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawPoints(XPainter* self, const XPoint* points, int count);

#endif /* XPAINTER_POLYGON_ON */

#if XPAINTER_PATH_ON
/* ========== 路径对象与绘制（对标 Qt 6.8 QPainterPath / QPainter::drawPath） ========== */

/** @brief 路径元素类型（对标 QPainterPath::ElementType 的常用子集）。 */
typedef enum XPainterPathElementType
{
    XPainterPathElement_MoveTo = 0,    /**< 移动当前位置。 */
    XPainterPathElement_LineTo = 1,    /**< 直线段。 */
    XPainterPathElement_QuadTo = 2,    /**< 二次贝塞尔曲线段。 */
    XPainterPathElement_CubicTo = 3,   /**< 三次贝塞尔曲线段。 */
    XPainterPathElement_CloseSubpath = 4 /**< 闭合子路径。 */
} XPainterPathElementType;

/** @brief 路径元素（固定三组坐标，插值语义随类型变化）。
 *  - MoveTo/LineTo：目标点在 m_x1/m_y1；
 *  - QuadTo：控制点在 m_x1/m_y1、终点在 m_x2/m_y2；
 *  - CubicTo：控制点 1=m_x1/m_y1、控制点 2=m_x2/m_y2、终点=m_x3/m_y3。 */
typedef struct XPainterPathElement
{
    XPainterPathElementType m_type; /**< 元素类型。 */
    float m_x1, m_y1;               /**< 第一组坐标。 */
    float m_x2, m_y2;               /**< 第二组坐标。 */
    float m_x3, m_y3;               /**< 第三组坐标。 */
} XPainterPathElement;

/**
 * @brief      动态路径对象（对标 QPainterPath 的最小嵌入子集）。
 * @note       内部用动态数组存储元素，使用后必须 XPainterPath_deinit；
 *             支持 moveTo/lineTo/quadTo/cubicTo/closeSubpath 与
 *             addRect/addEllipse 便捷构造，路径元素与 Qt 口径一致。
 */
typedef struct XPainterPath
{
    int m_elementCount;        /**< 元素数量。 */
    int m_elementCapacity;     /**< 容量（内部维护）。 */
    XPainterPathElement* m_elements; /**< 元素数组。 */
    float m_currentX, m_currentY;    /**< 当前点。 */
    float m_subpathStartX, m_subpathStartY; /**< 当前子路径起点。 */
} XPainterPath;

/**
 * @brief 初始化空路径。
 * @param self 路径指针；NULL 直接返回。
 */
void XPainterPath_init(XPainterPath* self);
/**
 * @brief 释放路径内部动态数组。
 * @param self 路径指针；NULL 直接返回。
 */
void XPainterPath_deinit(XPainterPath* self);
/**
 * @brief 移动当前位置并把当前子路径起点设为 (x,y)（对标 moveTo）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_moveTo(XPainterPath* self, float x, float y);
/**
 * @brief 追加直线段到 (x,y)（对标 lineTo）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_lineTo(XPainterPath* self, float x, float y);
/**
 * @brief 追加二次贝塞尔曲线段（对标 quadTo）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_quadTo(XPainterPath* self, float cx, float cy,
                         float x, float y);
/**
 * @brief 追加三次贝塞尔曲线段（对标 cubicTo）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_cubicTo(XPainterPath* self, float c1x, float c1y,
                          float c2x, float c2y, float x, float y);
/**
 * @brief 闭合当前子路径（后半段可继续追加段；对标 closeSubpath）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_closeSubpath(XPainterPath* self);
/**
 * @brief 追加一个矩形子路径（MoveTo→LineTo×3→Close）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_addRect(XPainterPath* self, const XRect* rect);
/**
 * @brief 追加一个椭圆子路径（按 64 段折线逼近圆弧）。
 * @return 成功返回 true；内存不足返回 false。
 */
bool XPainterPath_addEllipse(XPainterPath* self, const XRect* rect);
/** @brief 返回路径元素数量。 */
int XPainterPath_elementCount(const XPainterPath* self);
/** @brief 查询当前点坐标；outX/outY 可为 NULL。 */
void XPainterPath_currentPosition(const XPainterPath* self,
                                  float* x, float* y);

/**
 * @brief      绘制路径：填充部分走当前画刷，轮廓走当前画笔（对标 drawPath）。
 * @param self 绘制器指针。
 * @param path 路径对象；NULL 视为无操作返回 true。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_drawPath(XPainter* self, const XPainterPath* path);
/**
 * @brief      仅填充路径内部（对标 fillPath）。
 * @param self 绘制器指针。
 * @param path 路径对象；NULL 视为无操作返回 true。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_fillPath(XPainter* self, const XPainterPath* path);
/**
 * @brief      仅描边路径（对标 strokePath）。
 * @param self 绘制器指针。
 * @param path 路径对象；NULL 视为无操作返回 true。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
bool XPainter_strokePath(XPainter* self, const XPainterPath* path);
#endif /* XPAINTER_PATH_ON */

/* ========== 内置点阵文本（对标 QPainter::drawText 最小字集，字体由 XFont 选择） ========== */

/**
 * @brief      绘制一行点阵文本（对标 QPainter::drawText 的单行子集）。
 * @details    字库选择走 XFont：XFont_setFamily 选择内置字库，像素字号经
 *             XFont_bitmap* 算法算出整倍缩放，不依赖平台字体/资源：
 *             - 字库选择 XFont_setFamily，字号缩放由 XFont 像素字号决定；
 *             - x 为第一字形左上角 X，baselineY 为基线 Y（字形顶部在
 *               baselineY - ascent，底部在 baselineY + descent - 1）；
 *             - 遇 '\n' 停止，其余控制字符占一个字宽但不绘制；
 *             - 文本经 XPainter_fillRect 逐运行段输出，自动受当前
 *               裁剪矩形/变换约束；绑定 XPicture 后端时录制为填充指令；
 *             - 常与 XLabel 的纯文本显示配合。
 * @param self 绘制器指针。
 * @param x 起点 X 坐标。
 * @param baselineY 基线 Y 坐标。
 * @param utf8 UTF-8 单行文本；NULL 或空字符串视为无操作返回 true。
 * @param color ARGB32 文本颜色。
 * @return 绘制成功返回 true；未绑定设备返回 false。
 */
/**
 * @brief      在矩形区域内按对齐/换行标志绘制文本（对标 Qt 6.8
 *             QPainter::drawText(const QRect&, int, const QString&)）。
 * @details    使用绘制器当前字体；返回 false 表示设备无效或输入为空。
 * @param self  绘制器指针。
 * @param rect  文本布局矩形；NULL 或宽度/高度非正时直接返回 true。
 * @param flags XPAINTER_TEXT_* 位组合（对齐、换行、裁剪标志）。
 * @param utf8  UTF-8 文本；NULL 或空字符串返回 true。
 * @param color ARGB32 文本颜色。
 * @return true 表示调用可接受；false 表示绘制器未绑定有效设备。
 */
bool XPainter_drawTextRect(XPainter* self, const XRect* rect, uint32_t flags,
                           const char* utf8, uint32_t color);

bool XPainter_drawText(XPainter* self, int x, int baselineY,
                       const char* utf8, uint32_t color);
/**
 * @brief      计算一行点阵文本的宽度（像素，含 XFont 字号缩放）。
 * @param font 点阵字体（XFont_setFamily 选字库）；NULL 用默认字库。
 * @param utf8 UTF-8 文本；NULL 返回 0。
 * @return 首行宽度（遇 '\n' 停止；等宽字体下 = 字形数 x 字宽 x scale）。
 */
int XPainter_textWidth(const XFont* font, const char* utf8);
/** @brief 返回指定点阵字体的行高（含 XFont 字号缩放）。 */
int XPainter_textHeight(const XFont* font);
/** @brief 返回指定点阵字体的基线以上高度（含缩放）。 */
int XPainter_textAscent(const XFont* font);
/** @brief 返回指定点阵字体的基线以下高度（含缩放）。 */
int XPainter_textDescent(const XFont* font);

/**
 * @brief      绘制一个 UTF-8 码点字形并返回消耗字节数。
 * @details    供 XLabel 逐字形着色（选中段/链接段）与链接下划线使用：
 *             - 从 utf8 解码下一个码点，按字体像素字号整倍缩放输出字形，
 *               随后通过 XPainter_fillRect 逐运行段绘制；
 *             - 控制字符占一个字宽但不绘制字形；
 *             - 未绑定设备时不做绘制，但仍返回本次解码消耗的字节数。
 * @param self  绘制器指针。
 * @param x     缩放后字形左上角 X。
 * @param baselineY 缩放后基线 Y。
 * @param utf8  指向待绘制码点的 UTF-8 指针；NULL 或 NUL 返回 0。
 * @param color ARGB32 字形颜色。
 * @return 已消耗的字节数（1~4）；NUL/输入空返回 0。
 */
int XPainter_drawGlyph(XPainter* self, int x, int baselineY,
                       const char* utf8, uint32_t color);

/**
 * @brief      计算 UTF-8 文本指定字节区间 [startByte, endByte) 的宽度。
 * @details    供 XLabel 逐行断行/测宽使用；区间必须落在完整码点边界上，
 *             内部按字节边界解码，不会越界读取。
 * @param font      点阵字体（XFont_setFamily 选字库）；NULL 用默认字库。
 * @param utf8      UTF-8 文本；NULL 返回 0。
 * @param startByte 起始字节偏移（不含负值）。
 * @param endByte   结束字节偏移（排他）；大于文本长度时按文本长度处理。
 * @return 区间内字形数 x 字宽 x scale。
 */
int XPainter_textWidthRange(const XFont* font, const char* utf8,
                            int startByte, int endByte);

/* ========== 当前字体（对标 QPainter::setFont/font） ========== */

/**
 * @brief      设置绘制文本时使用的字体（对标 QPainter::setFont）。
 * @details    drawText/drawGlyph 使用绘制器当前字体；字体家族选择内置
 *             XFont 字库、像素字号决定整倍缩放（XFont_setFamily 等）。
 * @param self 绘制器指针。
 * @param font 要使用的 XFont；NULL 恢复为内置默认字库（8x16、1 倍）。
 */
void XPainter_setFont(XPainter* self, const XFont* font);

/**
 * @brief      获取绘制文本时使用的当前字体（对标 QPainter::font）。
 * @param self 绘制器指针。
 * @return 当前字体指针；self 为 NULL 时返回 NULL。
 */
const XFont* XPainter_font(const XPainter* self);

/**
 * @brief      保存当前绘制状态到状态栈。
 * @param self 绘制器指针。
 * @return 保存成功返回 true；未绑定设备或内部扩容失败返回 false。
 */
bool XPainter_save(XPainter* self);

/**
 * @brief      从状态栈恢复最近一次保存的绘制状态。
 * @param self 绘制器指针。
 * @return 恢复成功返回 true；未绑定设备或栈为空返回 false。
 */
bool XPainter_restore(XPainter* self);

/* ========== 画笔 / 画刷 ========== */

/**
 * @brief      设置画笔颜色（对标 QPainter::setPen，颜色版）。
 * @param self 绘制器指针。
 * @param color ARGB32 颜色。
 */
void XPainter_setPen(XPainter* self, uint32_t color);
/**
 * @brief      获取当前画笔颜色。
 * @param self 绘制器指针。
 * @return ARGB32 颜色。
 */
uint32_t XPainter_penColor(const XPainter* self);
/**
 * @brief      设置画笔宽度（像素，小于 1 按 1 处理）。
 * @param self 绘制器指针。
 * @param width 画笔宽度。
 */
void XPainter_setPenWidth(XPainter* self, int width);
/**
 * @brief      获取当前画笔宽度。
 * @param self 绘制器指针。
 * @return 画笔宽度（像素）。
 */
int XPainter_penWidth(const XPainter* self);
/**
 * @brief      设置画刷颜色（fillRect_2 使用）。
 * @param self 绘制器指针。
 * @param color ARGB32 颜色。
 */
void XPainter_setBrush(XPainter* self, uint32_t color);
/**
 * @brief      获取当前画刷颜色。
 * @param self 绘制器指针。
 * @return ARGB32 颜色。
 */
uint32_t XPainter_brushColor(const XPainter* self);

/**
 * @brief      设置背景颜色（对标 QPainter::setBackground）。
 * @param self 绘制器指针。
 * @param color ARGB32 背景颜色，默认不透明白。
 */
void XPainter_setBackground(XPainter* self, uint32_t color);

/**
 * @brief      获取当前背景颜色（对标 QPainter::background 的颜色部分）。
 * @param self 绘制器指针。
 * @return 背景颜色；self 为 NULL 返回 0。
 */
uint32_t XPainter_background(const XPainter* self);

#if XPAINTER_PENSTYLE_ON
/* ========== 画笔样式（对标 QPen Style/CapStyle/JoinStyle） ========== */

/** @brief 设置画笔线段样式（对标 QPen::setStyle）。 */
void XPainter_setPenStyle(XPainter* self, XPainterPenStyle style);
/** @brief 获取当前画笔线段样式。 */
XPainterPenStyle XPainter_penStyle(const XPainter* self);
/** @brief 设置画笔端点样式（对标 QPen::setCapStyle）。 */
void XPainter_setPenCapStyle(XPainter* self, XPainterPenCapStyle cap);
/** @brief 获取当前画笔端点样式。 */
XPainterPenCapStyle XPainter_penCapStyle(const XPainter* self);
/** @brief 设置画笔拐角样式（对标 QPen::setJoinStyle）。 */
void XPainter_setPenJoinStyle(XPainter* self, XPainterPenJoinStyle join);
/** @brief 获取当前画笔拐角样式。 */
XPainterPenJoinStyle XPainter_penJoinStyle(const XPainter* self);
#endif /* XPAINTER_PENSTYLE_ON */

#if XPAINTER_BRUSH_ON
/* ========== 画刷样式与渐变色（对标 QBrush/QGradient） ========== */

/**
 * @brief 设置画刷样式（对标 QBrush::setStyle）。
 * @param self 绘制器指针。
 * @param style 画刷样式；超出范围按 SolidPattern 处理。
 */
void XPainter_setBrushStyle(XPainter* self, XPainterBrushStyle style);
/** @brief 获取当前画刷样式。 */
XPainterBrushStyle XPainter_brushStyle(const XPainter* self);
/**
 * @brief 设置渐变色画刷（对标 QBrush(QGradient)）。
 * @param self 绘制器指针。
 * @param gradient 渐变色描述；NULL 恢复纯色画刷。
 */
void XPainter_setBrushGradient(XPainter* self,
                               const XPainterGradient* gradient);
/** @brief 获取当前画刷完整描述（含样式/颜色/渐变色）。 */
void XPainter_brush(const XPainter* self, XPainterBrush* out);
/** @brief 初始化一个线性渐变色描述。 */
void XPainterGradient_initLinear(XPainterGradient* gradient,
                                 float x1, float y1, float x2, float y2);
/** @brief 初始化一个径向渐变色描述。 */
void XPainterGradient_initRadial(XPainterGradient* gradient,
                                 float cx, float cy, float radius,
                                 float focalX, float focalY);
/** @brief 初始化一个锥形渐变色描述。 */
void XPainterGradient_initConical(XPainterGradient* gradient,
                                  float cx, float cy, float angleDeg);
/** @brief 追加一个渐变色停止点（最多 XPAINTER_GRADIENT_MAX_STOPS 个）。 */
void XPainterGradient_addStop(XPainterGradient* gradient,
                              float position, uint32_t color);
#endif /* XPAINTER_BRUSH_ON */

/* ========== 裁剪 ========== */

#if XPAINTER_CLIP_ON
/**
 * @brief      以指定操作设置逻辑坐标裁剪矩形（对标 QPainter::setClipRect）。
 * @param self 绘制器指针。
 * @param rect 逻辑坐标裁剪矩形；NULL 时不修改状态，空矩形表示裁剪为空。
 * @param operation 裁剪操作；ReplaceClip 替换，IntersectClip 与当前有效裁剪
 *                  取交集，NoClip 关闭裁剪。当前未启用裁剪时，IntersectClip
 *                  按 Qt 光栅引擎行为退化为 ReplaceClip。
 */
void XPainter_setClipRect(XPainter* self, const XRect* rect,
                          XPainterClipOperation operation);
/**
 * @brief      判断是否启用了裁剪。
 * @param self 绘制器指针。
 * @return 启用裁剪返回 true。
 */
bool XPainter_hasClipping(const XPainter* self);

/**
 * @brief      启用/禁用裁剪而不清空裁剪矩形（对标 QPainter::setClipping）。
 * @param self 绘制器指针。
 * @param enable true 时启用（需已有有效裁剪矩形），false 时关闭。
 */
void XPainter_setClipping(XPainter* self, bool enable);

/**
 * @brief      获取当前裁剪边界矩形（对标 QPainter::clipBoundingRect）。
 * @details    有已设置的有效裁剪矩形时返回该矩形（含 setClipping(false)
 *             关闭后仍保留的场景）；未设置过或传入空矩形清除后输出零矩形。
 * @param self 绘制器指针。
 * @param out 输出矩形。
 */
void XPainter_clipBoundingRect(const XPainter* self, XRect* out);
#endif /* XPAINTER_CLIP_ON */

/* ========== 变换 ========== */

/**
 * @brief      设置用户坐标到设备坐标的变换矩阵。
 * @param self 绘制器指针。
 * @param matrix 变换矩阵；NULL 时不修改状态。
 * @param combine true 时与当前矩阵组合，false 时直接替换（对标
 *                QPainter::setTransform 的 combine 参数）。
 */
void XPainter_setTransform(XPainter* self, const XImageTransform* matrix,
                           bool combine);
/**
 * @brief      获取当前变换矩阵。
 * @param self 绘制器指针。
 * @param out 输出矩阵；可为 NULL。
 */
void XPainter_transform(const XPainter* self, XImageTransform* out);
/**
 * @brief      重置变换为单位矩阵。
 * @param self 绘制器指针。
 */
void XPainter_resetTransform(XPainter* self);
/**
 * @brief      叠加平移变换（对标 QPainter::translate）。
 * @param self 绘制器指针。
 * @param dx X 方向平移量。
 * @param dy Y 方向平移量。
 */
void XPainter_translate(XPainter* self, float dx, float dy);
/**
 * @brief      叠加缩放变换（对标 QPainter::scale）。
 * @param self 绘制器指针。
 * @param sx X 方向缩放系数。
 * @param sy Y 方向缩放系数。
 */
void XPainter_scale(XPainter* self, float sx, float sy);
/**
 * @brief      叠加旋转变换（对标 QPainter::rotate，角度制，顺时针为正）。
 * @param self 绘制器指针。
 * @param degrees 旋转角度。
 */
void XPainter_rotate(XPainter* self, float degrees);

/**
 * @brief      叠加切变变换（对标 QPainter::shear）。
 * @param self 绘制器指针。
 * @param sh 水平剪切系数。
 * @param sv 垂直剪切系数。
 */
void XPainter_shear(XPainter* self, float sh, float sv);
/**
 * @brief      设置世界变换（对标 QPainter::setWorldTransform）。
 * @param self 绘制器指针。
 * @param matrix 变换矩阵；NULL 时不修改状态。
 * @param combine true 时与当前世界矩阵组合，false 时直接替换。
 */
void XPainter_setWorldTransform(XPainter* self, const XImageTransform* matrix,
                                bool combine);
/**
 * @brief      获取当前世界变换（对标 QPainter::worldTransform）。
 * @param self 绘制器指针。
 * @param out 输出矩阵；可为 NULL。
 */
void XPainter_worldTransform(const XPainter* self, XImageTransform* out);
#if XPAINTER_WORLD_MATRIX_ON
/**
 * @brief 启用或停用世界变换（对标 QPainter::setWorldMatrixEnabled）。
 * @details 停用时保留 m_transform 的数值，但后续绘制按单位变换执行；绘制器
 *          未绑定设备时不修改状态。
 * @param self 绘制器指针。
 * @param enabled true 应用世界变换，false 暂时忽略世界变换。
 */
void XPainter_setWorldMatrixEnabled(XPainter* self, bool enabled);
/**
 * @brief 查询世界变换是否应用于绘制（对标 QPainter::worldMatrixEnabled）。
 * @param self 绘制器指针。
 * @return 已绑定设备且世界变换启用时返回 true，否则返回 false。
 */
bool XPainter_worldMatrixEnabled(const XPainter* self);
#endif /* XPAINTER_WORLD_MATRIX_ON */
/**
 * @brief      获取世界变换与 window/viewport 视图变换的组合矩阵。
 * @details    对标 QPainter::combinedTransform。该查询始终使用已保存的世界
 *             矩阵数值；世界矩阵开关只决定实际绘制是否应用该矩阵。未激活
 *             绘制器、退化窗口或空指针时输出单位矩阵。
 * @param self 绘制器指针。
 * @param out 输出组合矩阵；可为 NULL。
 */
void XPainter_combinedTransform(const XPainter* self, XImageTransform* out);
#if XPAINTER_VIEW_TRANSFORM_ON
/**
 * @brief      设置逻辑窗口矩形并启用视图变换（对标 QPainter::setWindow）。
 * @details    window 表示逻辑坐标系，viewport 表示设备坐标系。调用后保留
 *             viewport 原值并立即启用映射；矩形宽或高为 0 时状态仍被保存，
 *             但软件后端无法构造可用缩放矩阵，后续绘制返回失败。
 * @param self 绘制器指针。
 * @param window 逻辑窗口矩形；NULL 时不修改状态。
 */
void XPainter_setWindow(XPainter* self, const XRect* window);
/**
 * @brief      获取逻辑窗口矩形（对标 QPainter::window）。
 * @param self 绘制器指针。
 * @param out 输出矩形；可为 NULL。未激活绘制器输出零矩形。
 */
void XPainter_window(const XPainter* self, XRect* out);
/**
 * @brief      设置设备视口矩形并启用视图变换（对标 QPainter::setViewport）。
 * @details    viewport 表示设备坐标系，window 表示逻辑坐标系。调用后保留
 *             window 原值并立即启用映射。
 * @param self 绘制器指针。
 * @param viewport 设备视口矩形；NULL 时不修改状态。
 */
void XPainter_setViewport(XPainter* self, const XRect* viewport);
/**
 * @brief      获取设备视口矩形（对标 QPainter::viewport）。
 * @param self 绘制器指针。
 * @param out 输出矩形；可为 NULL。未激活绘制器输出零矩形。
 */
void XPainter_viewport(const XPainter* self, XRect* out);
/**
 * @brief      启用或停用 window/viewport 视图变换。
 * @details    对标 QPainter::setViewTransformEnabled。停用时保留 window 和
 *             viewport 数值，实际绘制只使用世界变换。
 * @param self 绘制器指针。
 * @param enabled true 应用视图变换，false 暂时忽略视图变换。
 */
void XPainter_setViewTransformEnabled(XPainter* self, bool enabled);
/**
 * @brief      查询视图变换是否应用于绘制（对标 QPainter::viewTransformEnabled）。
 * @param self 绘制器指针。
 * @return 已绑定设备且视图变换启用时返回 true，否则返回 false。
 */
bool XPainter_viewTransformEnabled(const XPainter* self);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */

/* ========== 透明度与合成 ========== */

/**
 * @brief      设置整体不透明度（0.0~1.0，越界自动钳位）。
 * @param self 绘制器指针。
 * @param opacity 不透明度。
 */
void XPainter_setOpacity(XPainter* self, float opacity);
/**
 * @brief      获取当前不透明度。
 * @param self 绘制器指针。
 * @return 不透明度（0.0~1.0）。
 */
float XPainter_opacity(const XPainter* self);
/**
 * @brief      设置合成模式。
 * @param self 绘制器指针。
 * @param mode 合成模式。
 */
void XPainter_setCompositionMode(XPainter* self, XPainterCompositionMode mode);
/**
 * @brief      获取当前合成模式。
 * @param self 绘制器指针。
 * @return 合成模式。
 */
XPainterCompositionMode XPainter_compositionMode(const XPainter* self);

#if XPAINTER_RENDERHINT_ON
/**
 * @brief 设置或清除单个渲染提示（对标 QPainter::setRenderHint）。
 * @param self 绘制器指针。
 * @param hint 渲染提示位。
 * @param enabled true 设置该位，false 清除该位。
 */
void XPainter_setRenderHint(XPainter* self, XPainterRenderHint hint,
                            bool enabled);

/**
 * @brief 按位设置或清除渲染提示集合（对标 QPainter::setRenderHints）。
 * @param self 绘制器指针。
 * @param hints 渲染提示位集合。
 * @param enabled true 设置所有位，false 清除所有位。
 */
void XPainter_setRenderHints(XPainter* self, XPainterRenderHints hints,
                             bool enabled);

/**
 * @brief 获取当前渲染提示集合（对标 QPainter::renderHints）。
 * @param self 绘制器指针。
 * @return 当前渲染提示位集合；self 为空时返回 0。
 */
XPainterRenderHints XPainter_renderHints(const XPainter* self);

/**
 * @brief 判断指定渲染提示是否启用（对标 QPainter::testRenderHint）。
 * @param self 绘制器指针。
 * @param hint 待判断的渲染提示位。
 * @return 指定位全部启用返回 true。
 */
bool XPainter_testRenderHint(const XPainter* self, XPainterRenderHint hint);
#endif /* XPAINTER_RENDERHINT_ON */

#if XPAINTER_LAYOUT_DIRECTION_ON
/**
 * @brief 设置文本布局方向（对标 QPainter::setLayoutDirection）。
 * @param self 绘制器指针。
 * @param direction 布局方向；非法值按 Auto 处理。
 */
void XPainter_setLayoutDirection(XPainter* self,
                                 XPainterLayoutDirection direction);

/**
 * @brief 获取文本布局方向（对标 QPainter::layoutDirection）。
 * @param self 绘制器指针。
 * @return 当前方向；self 为空时返回 Auto。
 */
XPainterLayoutDirection XPainter_layoutDirection(const XPainter* self);
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPAINTER_H */
