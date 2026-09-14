#ifndef XPIESLICE_H
#define XPIESLICE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XPieSlice)
XCLASS_DEFINE_EXTEND_END(XPieSlice, XObject)

/**
 * @brief      切片标签位置（对标 Qt Charts 6.8 QPieSlice::LabelPosition，
 *             数值一致）。
 */
typedef enum XPieSlice_LabelPosition
{
    XPieSlice_LabelPosition_Outside = 0,            /**< 扇区外侧（对标 LabelOutside）。 */
    XPieSlice_LabelPosition_InsideHorizontal = 1,   /**< 内侧水平（对标 LabelInsideHorizontal）。 */
    XPieSlice_LabelPosition_InsideTangential = 2,   /**< 内侧切向（对标 LabelInsideTangential）。 */
    XPieSlice_LabelPosition_InsideNormal = 3        /**< 内侧法向（对标 LabelInsideNormal）。 */
} XPieSlice_LabelPosition;

/**
 * @brief 饼图切片对象（对标 QPieSlice）。
 *
 *        继承 XObject 以支持点击/悬停/属性变化信号；percentage/
 *        startAngle/angleSpan 为只读派生值，由所属 XPieSeries 布局
 *        计算后回写。
 */
typedef struct XPieSlice
{
    XObject m_base;                  /**< 基类成员；必须是第一个。 */
    XString* m_label;                /**< 切片标签（UTF-8；对象拥有）。 */
    double m_value;                  /**< 切片值。 */
    uint32_t m_color;                /**< 切片填充色（ARGB；0=主题色）。 */
    uint32_t m_borderColor;          /**< 边框色（ARGB；0=填充色）。 */
    int m_borderWidth;               /**< 边框宽（像素；0=无边框）。 */
    uint32_t m_labelColor;           /**< 标签色（ARGB；0=白色）。 */
    bool m_labelVisible;             /**< 标签可见（默认 false）。 */
    XPieSlice_LabelPosition m_labelPosition; /**< 标签位置。 */
    bool m_exploded;                 /**< 是否分离突出。 */
    double m_explodeDistanceFactor;  /**< 分离距离系数（0-1；默认 0.15）。 */
    double m_labelArmLengthFactor;   /**< 标签引线长度系数（默认 0.15）。 */
    double m_percentage;             /**< 占比（0-1；series 布局回写）。 */
    double m_startAngle;             /**< 起始角（度；series 布局回写）。 */
    double m_angleSpan;              /**< 角跨度（度；series 布局回写）。 */
    uint32_t m_penColor;             /**< 画笔颜色（0=默认）。 */
    double m_penWidth;               /**< 画笔线宽（默认 1）。 */
    uint32_t m_brushColor;           /**< 画刷颜色（0=透明）。 */
    uint32_t m_labelBrushColor;      /**< 标签画刷颜色（0=默认）。 */
    XString* m_labelFontFamily;      /**< 标签字体族（对象拥有）。 */
    int m_labelFontSize;             /**< 标签字号（磅；0=默认）。 */
} XPieSlice;

XVtable* XPieSlice_class_init(void);

/**
 * @brief 初始化嵌入式切片对象（对标 QPieSlice() 默认构造）。
 *
 * @param self 目标切片指针，不能为空。
 * @return 无返回值。
 */
void XPieSlice_init(XPieSlice* self);

/**
 * @brief 初始化并设置标签与值（对标 QPieSlice(label, value)）。
 *
 * @param self  目标切片指针。
 * @param label UTF-8 标签。
 * @param value 切片值。
 * @return 无返回值。
 */
void XPieSlice_init_2(XPieSlice* self, const char* label, double value);

/**
 * @brief 堆上创建切片对象。
 *
 * @param memory 内存类型。
 * @param label  UTF-8 标签；可为 NULL（空标签）。
 * @param value  切片值。
 * @return 切片指针；分配失败返回 NULL。
 */
XPieSlice* XPieSlice_create_ex(XMemoryType memory, const char* label,
                               double value);
#define XPieSlice_create(label, value) \
    XPieSlice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, label, value)

/** @brief 析构入口（查表分派父类析构）。 */
#define XPieSlice_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上切片对象（查表分派析构并释放内存）。 */
#define XPieSlice_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QPieSlice 公共 API） ==================== */

/** @brief 设置标签。 @param self 目标切片指针。 @param label UTF-8 标签。 @return 无返回值。 */
void XPieSlice_setLabel(XPieSlice* self, const char* label);
/** @brief 读取标签。 @param self 目标切片指针。 @return 标签（UTF-8）。 */
const char* XPieSlice_label(const XPieSlice* self);
/** @brief 设置切片值。 @param self 目标切片指针。 @param value 切片值。 @return 无返回值。 */
void XPieSlice_setValue(XPieSlice* self, double value);
/** @brief 读取切片值。 @param self 目标切片指针。 @return 切片值。 */
double XPieSlice_value(const XPieSlice* self);
/** @brief 设置标签可见。 @param self 目标切片指针。 @param visible true 显示。 @return 无返回值。 */
void XPieSlice_setLabelVisible(XPieSlice* self, bool visible);
/** @brief 查询标签可见。 @param self 目标切片指针。 @return 可见返回 true。 */
bool XPieSlice_isLabelVisible(const XPieSlice* self);
/** @brief 设置标签位置。 @param self 目标切片指针。 @param position 位置枚举。 @return 无返回值。 */
void XPieSlice_setLabelPosition(XPieSlice* self, XPieSlice_LabelPosition position);
/** @brief 查询标签位置。 @param self 目标切片指针。 @return 位置枚举。 */
XPieSlice_LabelPosition XPieSlice_labelPosition(const XPieSlice* self);
/** @brief 设置分离突出。 @param self 目标切片指针。 @param exploded true 分离。 @return 无返回值。 */
void XPieSlice_setExploded(XPieSlice* self, bool exploded);
/** @brief 查询分离突出。 @param self 目标切片指针。 @return 分离返回 true。 */
bool XPieSlice_isExploded(const XPieSlice* self);
/** @brief 设置边框色。 @param self 目标切片指针。 @param color ARGB 颜色。 @return 无返回值。 */
void XPieSlice_setBorderColor(XPieSlice* self, uint32_t color);
/** @brief 读取边框色。 @param self 目标切片指针。 @return ARGB 颜色。 */
uint32_t XPieSlice_borderColor(const XPieSlice* self);
/** @brief 设置边框宽。 @param self 目标切片指针。 @param width 宽（像素）。 @return 无返回值。 */
void XPieSlice_setBorderWidth(XPieSlice* self, int width);
/** @brief 读取边框宽。 @param self 目标切片指针。 @return 宽（像素）。 */
int XPieSlice_borderWidth(const XPieSlice* self);
/** @brief 设置切片填充色。 @param self 目标切片指针。 @param color ARGB 颜色；0=主题色。 @return 无返回值。 */
void XPieSlice_setColor(XPieSlice* self, uint32_t color);
/** @brief 读取切片填充色。 @param self 目标切片指针。 @return ARGB 颜色。 */
uint32_t XPieSlice_color(const XPieSlice* self);
/** @brief 设置标签色。 @param self 目标切片指针。 @param color ARGB 颜色。 @return 无返回值。 */
void XPieSlice_setLabelColor(XPieSlice* self, uint32_t color);
/** @brief 读取标签色。 @param self 目标切片指针。 @return ARGB 颜色。 */
uint32_t XPieSlice_labelColor(const XPieSlice* self);
/** @brief 设置标签引线长度系数。 @param self 目标切片指针。 @param factor 系数（0-1）。 @return 无返回值。 */
void XPieSlice_setLabelArmLengthFactor(XPieSlice* self, double factor);
/** @brief 读取标签引线长度系数。 @param self 目标切片指针。 @return 系数。 */
double XPieSlice_labelArmLengthFactor(const XPieSlice* self);
/** @brief 设置分离距离系数。 @param self 目标切片指针。 @param factor 系数（0-1）。 @return 无返回值。 */
void XPieSlice_setExplodeDistanceFactor(XPieSlice* self, double factor);
/** @brief 读取分离距离系数。 @param self 目标切片指针。 @return 系数。 */
double XPieSlice_explodeDistanceFactor(const XPieSlice* self);

/* ==================== 派生只读值（series 布局回写） ==================== */

/** @brief 读取占比（0-1）。 @param self 目标切片指针。 @return 占比。 */
double XPieSlice_percentage(const XPieSlice* self);
/** @brief 读取起始角（度）。 @param self 目标切片指针。 @return 起始角。 */
double XPieSlice_startAngle(const XPieSlice* self);
/** @brief 读取角跨度（度）。 @param self 目标切片指针。 @return 角跨度。 */
double XPieSlice_angleSpan(const XPieSlice* self);

/* ==================== 信号（对标 QPieSlice Q_SIGNALS） ==================== */

/**
 * @brief 发射点击信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_clicked_signal(XPieSlice* self);

/**
 * @brief 发射悬停信号。
 *
 * @param self  切片指针；NULL 返回信号标识。
 * @param state true 进入悬停。
 * @return 信号标识指针。
 */
void* XPieSlice_hovered_signal(XPieSlice* self, bool state);

/**
 * @brief 发射按压信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_pressed_signal(XPieSlice* self);

/**
 * @brief 发射释放信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_released_signal(XPieSlice* self);

/**
 * @brief 发射双击信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_doubleClicked_signal(XPieSlice* self);

/**
 * @brief 发射标签变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_labelChanged_signal(XPieSlice* self);

/**
 * @brief 发射值变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_valueChanged_signal(XPieSlice* self);

/**
 * @brief 发射标签可见变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_labelVisibleChanged_signal(XPieSlice* self);

/**
 * @brief 发射颜色变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_colorChanged_signal(XPieSlice* self);

/**
 * @brief 发射边框色变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_borderColorChanged_signal(XPieSlice* self);

/**
 * @brief 发射占比变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_percentageChanged_signal(XPieSlice* self);

/**
 * @brief 发射起始角变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_startAngleChanged_signal(XPieSlice* self);

/**
 * @brief 发射角跨度变化信号。
 *
 * @param self 切片指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSlice_angleSpanChanged_signal(XPieSlice* self);

/* ==================== 画笔/画刷/标签字体（C 参数化，对标 QPen/QBrush/QFont） ==================== */

/** @brief 设置画笔（颜色+线宽，对标 setPen(QPen)）。 @param self 目标切片指针。 @param color ARGB。 @param width 线宽（像素）。 @return 无返回值。 */
void XPieSlice_setPen(XPieSlice* self, uint32_t color, double width);
/** @brief 读取画笔（参数化，对标 pen()）。 @param self 目标切片指针。 @param color 输出颜色。 @param width 输出线宽。 @return 无返回值。 */
void XPieSlice_pen(const XPieSlice* self, uint32_t* color, double* width);
/** @brief 设置画刷（颜色，对标 setBrush(QBrush)）。 @param self 目标切片指针。 @param color ARGB（0=透明）。 @return 无返回值。 */
void XPieSlice_setBrush(XPieSlice* self, uint32_t color);
/** @brief 读取画刷颜色（对标 brush()）。 @param self 目标切片指针。 @return ARGB。 */
uint32_t XPieSlice_brush(const XPieSlice* self);
/** @brief 设置标签画刷（颜色）。 @param self 目标切片指针。 @param color ARGB。 @return 无返回值。 */
void XPieSlice_setLabelBrush(XPieSlice* self, uint32_t color);
/** @brief 读取标签画刷颜色。 @param self 目标切片指针。 @return ARGB。 */
uint32_t XPieSlice_labelBrush(const XPieSlice* self);
/** @brief 设置标签字体（字体族+字号，对标 setLabelFont(QFont)）。 @param self 目标切片指针。 @param family 字体族（NULL 保持）。 @param pointSize 字号（0 保持）。 @return 无返回值。 */
void XPieSlice_setLabelFont(XPieSlice* self, const char* family,
                            int pointSize);
/** @brief 读取标签字体族（对标 labelFont()）。 @param self 目标切片指针。 @return 字体族。 */
const char* XPieSlice_labelFont(const XPieSlice* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XPIESLICE_H */
