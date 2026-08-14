/******************************************************************************
 * @file       XGeometry.h
 * @brief      基础空间几何类型定义（对标 Qt 6.8 QPoint/QSize/QRect/QRegion）
 * @author     XinYueC 团队
 * @note       提供通用的点、尺寸、矩形、区域等基础几何类型，供 GUI、TUI
 *             和其他坐标相关模块复用
 ******************************************************************************/
#ifndef XGEOMETRY_H
#define XGEOMETRY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
typedef struct XVariant XVariant;

/* ========== 点坐标类型（对标 Qt 6.8 QPoint） ========== */

/**
 * @brief      二维点坐标（对标 Qt 6.8 QPoint）
 * @note       使用整数表示 x/y 坐标
 */
typedef struct XPoint
{
    int x;      /**< X 坐标 */
    int y;      /**< Y 坐标 */
} XPoint;

/**
 * @brief      比较两个 XPoint
 * @param lhs  左操作数
 * @param rhs  右操作数
 * @return     XCompare_Equality / XCompare_Less / XCompare_Greater
 */
int32_t XPoint_compare(const XPoint* lhs, const XPoint* rhs);

/** @brief 深复制创建存储 XPoint 的 XVariant。 */
XVariant* XPoint_toVariant(XPoint point);
/** @brief 从同类型 XVariant 取得 XPoint 值副本。 */
XPoint XPoint_fromVariant(const XVariant* variant);
/** @brief 从同类型 XVariant 借用取得 XPoint 指针。 */
XPoint* XPoint_fromVariant_ref(const XVariant* variant);
/** @brief 设置 XVariant 的 XPoint 值。 */
void XPoint_setVariant(XVariant* variant, XPoint point);

/**
 * @brief 兼容旧的 XVariant XPoint 扩展 API 名称。
 * @details 以下宏仅保留源代码兼容性，实际实现均归属 XPoint。
 */
#define XVariant_create_Point    XPoint_toVariant
#define XVariant_toPoint         XPoint_fromVariant
#define XVariant_toPoint_ref     XPoint_fromVariant_ref
#define XVariant_setValue_Point  XPoint_setVariant

/**
 * @brief      初始化 XPoint 对象
 * @param self 目标 XPoint 对象指针
 * @param x    X 坐标
 * @param y    Y 坐标
 */
void XPoint_init(XPoint* self, int x, int y);

/**
 * @brief      判断点是否为零点
 * @param self 目标 XPoint 对象指针
 * @return     非空且坐标均为 0 时返回 true；空指针也视为零点
 */
bool XPoint_isNull(const XPoint* self);

/**
 * @brief      返回点到原点的 Manhattan 距离
 * @param self 目标 XPoint 对象指针
 * @return     |x| + |y|，超出 int 范围时饱和到 int 边界
 */
int XPoint_manhattanLength(const XPoint* self);

/**
 * @brief      逐分量相加两个点
 * @param lhs  左操作数；可为空，按零点处理
 * @param rhs  右操作数；可为空，按零点处理
 * @return     逐分量相加后的点
 */
XPoint XPoint_add(const XPoint* lhs, const XPoint* rhs);

/**
 * @brief      逐分量相减两个点
 * @param lhs  被减点；可为空，按零点处理
 * @param rhs  减数点；可为空，按零点处理
 * @return     逐分量相减后的点
 */
XPoint XPoint_subtract(const XPoint* lhs, const XPoint* rhs);


/* ========== 基础几何类型 ========== */

/**
 * @brief      二维整数尺寸（对标 Qt 6.8 QSize）
 * @note       使用整数表示的宽度和高度
 */
typedef struct XSize
{
    int width;   /**< 宽度 */
    int height;  /**< 高度 */
}XSize;

/**
 * @brief      二维浮点尺寸（对标 Qt 6.8 QSizeF）
 * @note       使用浮点数表示的宽度和高度
 */
typedef struct XSizeF
{
    float width;   /**< 宽度 */
    float height;  /**< 高度 */
}XSizeF;

/**
 * @brief      二维整数矩形（对标 Qt 6.8 QRect）
 * @note       使用整数坐标表示的矩形区域，包含左上角坐标和宽高
 */
typedef struct XRect
{
    int x;      /**< 左上角 X 坐标 */
    int y;      /**< 左上角 Y 坐标 */
    int width;  /**< 宽度 */
    int height; /**< 高度 */
}XRect;

/**
 * @brief      二维浮点矩形（对标 Qt 6.8 QRectF）
 * @note       使用浮点数坐标表示的矩形区域
 */
typedef struct XRectF
{
    float x;      /**< 左上角 X 坐标 */
    float y;      /**< 左上角 Y 坐标 */
    float width;  /**< 宽度 */
    float height; /**< 高度 */
}XRectF;

/**
 * @brief      区域类型（对标 Qt 6.8 QRegion）
 * @note       表示一个复杂区域，由矩形列表组成
 */
typedef struct XRegion
{
    XRect* rects;     /**< 矩形数组 */
    int    count;     /**< 矩形数量 */
    int    capacity;  /**< 矩形数组容量 */
}XRegion;

/* ========== 辅助函数 ========== */

/**
 * @brief      初始化 XSize 对象
 * @param self  目标 XSize 对象指针
 * @param w     宽度
 * @param h     高度
 */
void XSize_init(XSize* self, int w, int h);

/**
 * @brief      判断尺寸是否为零尺寸
 * @param self 目标 XSize 对象指针
 * @return     宽度和高度均为 0 时返回 true；空指针也视为零尺寸
 */
bool XSize_isNull(const XSize* self);

/**
 * @brief      判断尺寸是否为空
 * @param self 目标 XSize 对象指针
 * @return     宽度或高度小于等于 0 时返回 true
 */
bool XSize_isEmpty(const XSize* self);

/**
 * @brief      判断尺寸是否有效
 * @param self 目标 XSize 对象指针
 * @return     指针非空且宽度、高度均大于等于 0 时返回 true
 */
bool XSize_isValid(const XSize* self);

/**
 * @brief      逐分量取两个尺寸的较小值
 * @param self  左侧尺寸；可为空
 * @param other 右侧尺寸；可为空
 * @return      逐分量取较小值后的尺寸
 */
XSize XSize_boundedTo(const XSize* self, const XSize* other);

/**
 * @brief      逐分量取两个尺寸的较大值
 * @param self  左侧尺寸；可为空
 * @param other 右侧尺寸；可为空
 * @return      逐分量取较大值后的尺寸
 */
XSize XSize_expandedTo(const XSize* self, const XSize* other);

/**
 * @brief      交换宽度和高度
 * @param self  输入尺寸
 * @return      宽高交换后的尺寸
 */
XSize XSize_transposed(const XSize* self);

/**
 * @brief      将尺寸缩放到目标范围
 * @param self   待缩放尺寸，也接收输出结果
 * @param width  目标宽度
 * @param height 目标高度
 * @param mode   缩放模式：0 为忽略宽高比，1 为保持宽高比并限制在目标内，
 *               2 为保持宽高比并覆盖目标范围
 */
void XSize_scale(XSize* self, int width, int height, uint32_t mode);

/**
 * @brief      初始化 XRect 对象
 * @param self  目标 XRect 对象指针
 * @param x     左上角 X 坐标
 * @param y     左上角 Y 坐标
 * @param w     宽度
 * @param h     高度
 */
void XRect_init(XRect* self, int x, int y, int w, int h);

/**
 * @brief      判断矩形是否为空（宽度或高度 <= 0）
 * @param self 目标 XRect 对象指针
 * @return 为空返回 true
 */
bool XRect_isEmpty(const XRect* self);

/**
 * @brief      判断矩形是否包含指定点
 * @param self 目标 XRect 对象指针
 * @param x    点的 X 坐标
 * @param y    点的 Y 坐标
 * @return 包含返回 true
 */
bool XRect_contains(const XRect* self, int x, int y);

/**
 * @brief      判断矩形是否为零矩形
 * @param self 目标 XRect 对象指针
 * @return     宽度和高度均为 0 时返回 true；空指针也视为零矩形
 */
bool XRect_isNull(const XRect* self);

/**
 * @brief      返回规范化矩形
 * @details    负宽度或负高度会被转换为正尺寸，左上角同步调整。
 * @param self 输入矩形
 * @return     规范化后的矩形
 */
XRect XRect_normalized(const XRect* self);

/**
 * @brief      返回矩形左边界
 * @param self 输入矩形
 * @return     x 坐标
 */
int XRect_left(const XRect* self);

/**
 * @brief      返回矩形上边界
 * @param self 输入矩形
 * @return     y 坐标
 */
int XRect_top(const XRect* self);

/**
 * @brief      返回矩形右边界
 * @details    使用 Qt/QRect 语义，返回包含式边界 x + width - 1。
 * @param self 输入矩形
 * @return     右边界坐标
 */
int XRect_right(const XRect* self);

/**
 * @brief      返回矩形下边界
 * @details    使用 Qt/QRect 语义，返回包含式边界 y + height - 1。
 * @param self 输入矩形
 * @return     下边界坐标
 */
int XRect_bottom(const XRect* self);

/**
 * @brief      获取矩形尺寸
 * @param self 输入矩形
 * @return     矩形尺寸
 */
XSize XRect_size(const XRect* self);

/**
 * @brief      获取矩形左上角
 * @param self 输入矩形
 * @return     矩形左上角
 */
XPoint XRect_topLeft(const XRect* self);

/**
 * @brief      获取矩形中心点
 * @param self 输入矩形
 * @return     矩形中心点
 */
XPoint XRect_center(const XRect* self);

/**
 * @brief      判断矩形是否完全包含另一个矩形
 * @param self  外部矩形
 * @param other 待判断的内部矩形
 * @return     完全包含时返回 true
 */
bool XRect_containsRect(const XRect* self, const XRect* other);

/**
 * @brief      判断两个矩形是否有非空交集
 * @param self  左侧矩形
 * @param other 右侧矩形
 * @return     相交时返回 true
 */
bool XRect_intersects(const XRect* self, const XRect* other);

/**
 * @brief      计算两个矩形的交集
 * @param self  左侧矩形
 * @param other 右侧矩形
 * @return      交集矩形；无交集时为零矩形
 */
XRect XRect_intersected(const XRect* self, const XRect* other);

/**
 * @brief      计算两个矩形的包围联合矩形
 * @param self  左侧矩形
 * @param other 右侧矩形
 * @return      包围联合矩形
 */
XRect XRect_united(const XRect* self, const XRect* other);

/**
 * @brief      按四条边调整矩形
 * @param self 输入矩形
 * @param dx1  左边偏移量
 * @param dy1  上边偏移量
 * @param dx2  右边偏移量
 * @param dy2  下边偏移量
 * @return     调整后的矩形
 */
XRect XRect_adjusted(const XRect* self, int dx1, int dy1, int dx2, int dy2);

/**
 * @brief      返回平移后的矩形
 * @param self 输入矩形
 * @param dx    X 方向平移量
 * @param dy    Y 方向平移量
 * @return      平移后的矩形
 */
XRect XRect_translated(const XRect* self, int dx, int dy);

/**
 * @brief      原地平移矩形
 * @param self 待平移矩形
 * @param dx   X 方向平移量
 * @param dy   Y 方向平移量
 */
void XRect_translate(XRect* self, int dx, int dy);

/**
 * @brief      移动矩形使其中心落在指定点
 * @param self   待移动矩形
 * @param center 目标中心点
 */
void XRect_moveCenter(XRect* self, const XPoint* center);

/**
 * @brief      初始化 XSizeF 对象
 * @param self  目标 XSizeF 对象指针
 * @param w     宽度
 * @param h     高度
 */
void XSizeF_init(XSizeF* self, float w, float h);

/**
 * @brief      初始化 XRegion 对象
 * @param self 目标 XRegion 对象指针
 */
void XRegion_init(XRegion* self);

/**
 * @brief      释放 XRegion 资源
 * @param self 目标 XRegion 对象指针
 */
void XRegion_deinit(XRegion* self);

/**
 * @brief      向区域中添加矩形
 * @param self 目标 XRegion 对象指针
 * @param rect 待添加的矩形指针；空矩形会被忽略
 * @note       相邻且可合并的矩形会被合并，区域不接管 rect 的所有权
 */
void XRegion_addRect(XRegion* self, const XRect* rect);

/**
 * @brief      清空区域中的所有矩形
 * @param self 目标 XRegion 对象指针
 * @note       保留已分配的容量，便于后续复用
 */
void XRegion_clear(XRegion* self);

/**
 * @brief      判断区域是否为空
 * @param self 目标 XRegion 对象指针
 * @return     不包含有效矩形时返回 true；空指针也视为空区域
 */
bool XRegion_isEmpty(const XRegion* self);

/**
 * @brief      复制区域
 * @param self 输入区域
 * @param out  输出区域；与 self 相同时不执行操作
 */
void XRegion_copy(const XRegion* self, XRegion* out);

/**
 * @brief      获取区域的包围矩形
 * @param self 输入区域
 * @param out  输出包围矩形；空区域时为零矩形
 */
void XRegion_boundingRect(const XRegion* self, XRect* out);

/**
 * @brief      判断区域是否包含指定点
 * @param self 区域对象
 * @param x    点的 X 坐标
 * @param y    点的 Y 坐标
 * @return     点位于任一子矩形内时返回 true
 */
bool XRegion_contains(const XRegion* self, int x, int y);

/**
 * @brief      判断区域是否与矩形相交
 * @param self 区域对象
 * @param rect 待判断的矩形
 * @return     存在非空交集时返回 true
 */
bool XRegion_intersects(const XRegion* self, const XRect* rect);

/**
 * @brief      计算两个区域的联合
 * @param self  左侧区域
 * @param other 右侧区域
 * @param out   输出区域；支持与输入区域别名
 */
void XRegion_united(const XRegion* self, const XRegion* other, XRegion* out);

/**
 * @brief      计算两个区域的交集
 * @param self  左侧区域
 * @param other 右侧区域
 * @param out   输出区域；支持与输入区域别名
 */
void XRegion_intersected(const XRegion* self, const XRegion* other, XRegion* out);

/**
 * @brief      从区域中减去另一个区域
 * @param self  被减区域
 * @param other 减数区域
 * @param out   输出差集区域；支持与输入区域别名
 */
void XRegion_subtracted(const XRegion* self, const XRegion* other, XRegion* out);

#ifdef __cplusplus
}
#endif
#endif /* XGEOMETRY_H */
