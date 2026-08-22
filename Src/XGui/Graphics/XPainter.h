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

/**
 * @brief      合成模式枚举（对标 QPainter::CompositionMode）。
 */
typedef enum XPainterCompositionMode
{
    XPainterCompositionMode_SourceOver = 0, /**< 源覆盖模式（默认，带 Alpha 混合） */
    XPainterCompositionMode_Source = 1      /**< 源替换模式（整体覆盖目标像素） */
} XPainterCompositionMode;

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

/**
 * @brief      XPainter 当前绘制状态快照。
 * @note       save()/restore() 保存与恢复的正是该结构体内容；
 *             上层可直接通过 XPainter 对象的 m_state 成员只读查询。
 */
typedef struct XPainterState
{
    uint32_t m_penColor;        /**< 画笔颜色（ARGB32）。 */
    int m_penWidth;             /**< 画笔宽度（像素，>=1）。 */
    uint32_t m_brushColor;      /**< 画刷颜色（ARGB32）。 */
    bool m_hasClip;             /**< 是否启用裁剪（false 表示不裁剪）。 */
    XRect m_clipRect;           /**< 裁剪矩形（设备坐标）。 */
    XImageTransform m_transform; /**< 用户坐标到设备坐标的变换矩阵。 */
    float m_opacity;            /**< 整体不透明度（0.0~1.0）。 */
    XPainterCompositionMode m_compositionMode; /**< 合成模式。 */
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

/* ========== 裁剪 ========== */

/**
 * @brief      设置裁剪矩形（设备坐标，在变换之后生效）。
 * @param self 绘制器指针。
 * @param rect 裁剪矩形；NULL 或空矩形表示清除裁剪。
 */
void XPainter_setClipRect(XPainter* self, const XRect* rect);
/**
 * @brief      获取当前裁剪矩形。
 * @param self 绘制器指针。
 * @param out 输出矩形；无裁剪时输出零矩形。可为 NULL。
 */
void XPainter_clipRect(const XPainter* self, XRect* out);
/**
 * @brief      判断是否启用了裁剪。
 * @param self 绘制器指针。
 * @return 启用裁剪返回 true。
 */
bool XPainter_hasClipping(const XPainter* self);

/* ========== 变换 ========== */

/**
 * @brief      设置用户坐标到设备坐标的变换矩阵。
 * @param self 绘制器指针。
 * @param matrix 变换矩阵；NULL 表示重置为单位变换。
 */
void XPainter_setTransform(XPainter* self, const XImageTransform* matrix);
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

#ifdef __cplusplus
}
#endif
#endif /* XPAINTER_H */
