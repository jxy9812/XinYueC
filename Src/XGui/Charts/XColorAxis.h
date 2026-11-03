/**
 * @file       XColorAxis.h
 * @brief      XColorAxis 颜色轴（对标 Qt Charts 6.8 QColorAxis）。
 * @details    颜色轴把数值域映射成颜色条，用于给散点/热力等序列按数值着色。
 *             以 m_base 组合继承 XAbstractAxis（可见性/网格/标签/标题等
 *             公共属性与信号在基类）；本类持有数值域、刻度数、色条厚度与
 *             渐变定义。
 *
 *             Qt 用 QLinearGradient 表达渐变；本库无渐变类型，改用
 *             「若干色标（stop, ARGB）」序列等价承载：
 *             - setGradient  → XColorAxis_setGradientStops
 *             - gradient()   → XColorAxis_gradientStopAt / _stopCount
 *             色标按 stop 升序保存（0..1），渲染时线性插值。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCOLORAXIS_H
#define XCOLORAXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractAxis.h"
#include "XLinearGradient.h"

#if XCHARTS_ON

/** @brief 色标最大数量（超出按最近插入替换尾部）。 */
#define XCOLORAXIS_MAX_STOPS 8

/** @brief 颜色轴（m_base 必须是第一个成员；对标 QColorAxis : QAbstractAxis）。 */
typedef struct XColorAxis
{
    XAbstractAxis m_base;      /**< 基类成员；必须是第一个（可见/网格/标题等）。 */

    /* ---- 8 字节对齐组 ---- */
    XLinearGradient* m_gradient; /**< 渐变（对象拥有；对标 QColorAxis::gradient）。 */
    double m_size;             /**< 色条厚度（像素；默认 50，对标 Qt 默认）。 */

    /* ---- 4 字节对齐组 ---- */
    int m_tickCount;           /**< 刻度数（默认 5）。 */

    /* ---- 1 字节组 ---- */
    bool m_autoRange;          /**< 自动量程（默认 true，对标 Qt）。 */
} XColorAxis;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XColorAxis_class_init(void);
/** @brief 初始化嵌入式颜色轴。 @param self 目标轴指针。 @return 无返回值。 */
void XColorAxis_init(XColorAxis* self);
/** @brief 反初始化（基类资源）。 @param self 目标轴指针。 @return 无返回值。 */
void XColorAxis_deinit_impl(XColorAxis* self);
/** @brief 堆上创建颜色轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XColorAxis* XColorAxis_create_ex(XMemoryType memory);
#define XColorAxis_create() XColorAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QColorAxis） ==================== */

/** @brief 查询轴类型（对标 QColorAxis::type）。 @param self 目标轴指针。 @return 固定返回 AxisTypeColor。 */
int XColorAxis_type(const XColorAxis* self);

/* ==================== 数值域（对标 QColorAxis） ==================== */

/** @brief 设置范围（对标 setRange）。 @param self 目标轴指针。 @param min 下限。 @param max 上限（须 >= min）。 @return 无返回值。 */
void XColorAxis_setRange(XColorAxis* self, double min, double max);
/** @brief 设置下限（对标 setMin）。 @param self 目标轴指针。 @param min 下限。 @return 无返回值。 */
void XColorAxis_setMin(XColorAxis* self, double min);
/** @brief 查询下限（对标 min）。 @param self 目标轴指针。 @return 下限。 */
double XColorAxis_min(const XColorAxis* self);
/** @brief 设置上限（对标 setMax）。 @param self 目标轴指针。 @param max 上限。 @return 无返回值。 */
void XColorAxis_setMax(XColorAxis* self, double max);
/** @brief 查询上限（对标 max）。 @param self 目标轴指针。 @return 上限。 */
double XColorAxis_max(const XColorAxis* self);

/* ==================== 刻度 / 尺寸（对标 QColorAxis） ==================== */

/** @brief 设置刻度数（对标 setTickCount）。 @param self 目标轴指针。 @param count 刻度数（<2 按 2 钳位）。 @return 无返回值。 */
void XColorAxis_setTickCount(XColorAxis* self, int count);
/** @brief 查询刻度数（对标 tickCount）。 @param self 目标轴指针。 @return 刻度数。 */
int XColorAxis_tickCount(const XColorAxis* self);
/** @brief 设置色条厚度（对标 setSize）。 @param self 目标轴指针。 @param size 厚度（像素；<0 钳位 0）。 @return 无返回值。 */
void XColorAxis_setSize(XColorAxis* self, double size);
/** @brief 查询色条厚度（对标 size）。 @param self 目标轴指针。 @return 厚度。 */
double XColorAxis_size(const XColorAxis* self);
/** @brief 设置自动量程（对标 setAutoRange）。 @param self 目标轴指针。 @param autoRange true 自动。 @return 无返回值。 */
void XColorAxis_setAutoRange(XColorAxis* self, bool autoRange);
/** @brief 查询自动量程（对标 autoRange）。 @param self 目标轴指针。 @return 自动返回 true。 */
bool XColorAxis_autoRange(const XColorAxis* self);

/* ==================== 渐变（对标 QColorAxis::setGradient / gradient） ==================== */

/**
 * @brief 设置渐变（XLinearGradient 主版本；对标 setGradient(QLinearGradient)）。
 * @details 内部深拷贝一份，调用方保留入参所有权（_const 契约）。
 * @param self 目标轴指针。
 * @param gradient 借用 XLinearGradient*；NULL 表示清除渐变。
 * @return 无返回值（变化时发射 gradientChanged）。
 */
void XColorAxis_setGradient(XColorAxis* self, const XLinearGradient* gradient);

/** @brief 查询渐变（借用；对标 gradient）。
 * @param self 目标轴指针。
 * @return 借用 XLinearGradient*；未设置返回 NULL。归轴所有，调用者不得释放。
 */
const XLinearGradient* XColorAxis_gradient_const(const XColorAxis* self);

/** @brief 查询渐变（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XLinearGradient*；调用者不得释放。
 */
const XLinearGradient* XColorAxis_gradient(const XColorAxis* self);

/**
 * @brief 设置色标序列（便捷入口；等价先建 XLinearGradient 再 setGradient）。
 * @details 渐变几何沿用当前渐变的起止点（默认 (0,0)→(1,0)）。
 * @param self 目标轴指针。
 * @param positions stop 位置数组（0..1，借用）。
 * @param colors 对应 ARGB 颜色数组（借用）。
 * @param count 色标数量。
 * @return 无返回值；positions/colors 为 NULL 或 count<=0 时清空色标。
 */
void XColorAxis_setGradientStops(XColorAxis* self, const double* positions,
                                 const uint32_t* colors, int count);

/** @brief 查询色标数量（对标 gradient().stops().size()）。
 * @param self 目标轴指针。
 * @return 有效色标数。
 */
int XColorAxis_gradientStopCount(const XColorAxis* self);

/**
 * @brief 按下标读取色标（对标 gradient().stops().at()）。
 * @param self 目标轴指针。
 * @param index 色标下标。
 * @param position 输出位置；可空。
 * @param color 输出颜色；可空。
 * @return 越界返回 false；成功返回 true。
 */
bool XColorAxis_gradientStopAt(const XColorAxis* self, int index,
                               double* position, uint32_t* color);

/**
 * @brief 按归一化位置取渐变色（对标 QLinearGradient 线性插值）。
 * @param self 目标轴指针。
 * @param pos 位置 0..1（越界钳位）。
 * @return ARGB 颜色；无渐变或无色标返回 0。
 */
uint32_t XColorAxis_colorAt(const XColorAxis* self, double pos);

/**
 * @brief 转换为绘制层渐变（供 XPainter_setBrushGradient 直接消费）。
 * @param self 目标轴指针。
 * @param out 输出绘制层渐变；调用方提供存储。
 * @return 有渐变返回 true；未设置返回 false。
 */
bool XColorAxis_toPainterGradient(const XColorAxis* self,
                                  XPainterGradient* out);

/* ==================== 信号（对标 QColorAxis Q_SIGNALS） ==================== */

/** @brief minChanged 信号（载荷：下限）。 */
void* XColorAxis_minChanged_signal(XColorAxis* self, double min);
/** @brief maxChanged 信号（载荷：上限）。 */
void* XColorAxis_maxChanged_signal(XColorAxis* self, double max);
/** @brief rangeChanged 信号（载荷：下限, 上限）。 */
void* XColorAxis_rangeChanged_signal(XColorAxis* self, double min, double max);
/** @brief tickCountChanged 信号（载荷：刻度数）。 */
void* XColorAxis_tickCountChanged_signal(XColorAxis* self, int tickCount);
/** @brief gradientChanged 信号（载荷：色标数；对标 gradientChanged(QLinearGradient)）。 */
void* XColorAxis_gradientChanged_signal(XColorAxis* self, int stopCount);
/** @brief sizeChanged 信号（载荷：厚度）。 */
void* XColorAxis_sizeChanged_signal(XColorAxis* self, double size);
/** @brief autoRangeChanged 信号（载荷：自动量程）。 */
void* XColorAxis_autoRangeChanged_signal(XColorAxis* self, bool autoRange);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCOLORAXIS_H */
