/**
 * @file       XLcdNumber.h
 * @brief      XLcdNumber LCD 数码管数字显示控件（对标 Qt 6.8 QLCDNumber
 *             全部公共 API）。
 * @details    功能范围：
 *             - 进制模式：Hex/Dec/Oct/Bin（数值与 QLCDNumber::Mode 一致；
 *               非 Dec 模式显示整数等价）；
 *             - 段风格：Outline/Filled/Flat（数值与 QLCDNumber::
 *               SegmentStyle 一致，默认 Filled）；
 *             - 位数：digitCount 0..99（越界钳位，对标 setDigitCount 的
 *               警告与钳位行为），默认构造 5 位、指定位数构造重载；
 *             - 显示槽：display（字符串，非法字符替换为空格、忽略
 *               mode/smallDecimalPoint）、display_2（整数）、
 *               display_3（浮点）；溢出时发射 overflow 信号并保留旧显示；
 *             - checkOverflow 对标同签名判定（display 前预估溢出）；
 *             - 小数点模式 smallDecimalPoint：true 时小数点并入数字位
 *               右下角，false 时独占一个数字位；
 *             - 绘制：七段数码管（段集覆盖 0-9/A-F/减号/小数点等
 *               QLCDNumber 合法字符，非法字符按 Qt 语义替换空格）；
 *             - 默认边框 Box|Raised（对标构造中的 setFrameStyle）。
 *             信号：overflow()。
 * @note       模块总开关 XLCDNUMBER_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XFRAME_ON。
 *             绘制为工程简化实现：Qt 以内部字体度量计算段长，本实现按
 *             控件几何均分数字位；数值/进制/溢出/字符串语义与 Qt 一致。
 * @author     XinYueC 团队
 */
#ifndef XLCDNUMBER_H
#define XLCDNUMBER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XFrame.h"

#if XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON

/** @brief 显示字符串缓冲上限（99 位数字 + smallPoint 插点余量）。 */
#define XLCDNUMBER_STR_MAX 256

/**
 * @brief      显示进制模式（对标 QLCDNumber::Mode，数值一致）。
 */
typedef enum XLcdNumberMode
{
    XLcdNumberMode_Hex = 0,   /**< 十六进制。 */
    XLcdNumberMode_Dec = 1,   /**< 十进制（默认；唯一支持小数显示）。 */
    XLcdNumberMode_Oct = 2,   /**< 八进制。 */
    XLcdNumberMode_Bin = 3    /**< 二进制。 */
} XLcdNumberMode;

/**
 * @brief      段绘制风格（对标 QLCDNumber::SegmentStyle，数值一致）。
 */
typedef enum XLcdNumberSegmentStyle
{
    XLcdNumberSegmentStyle_Outline = 0, /**< 背景色填充的凸起段轮廓。 */
    XLcdNumberSegmentStyle_Filled = 1,  /**< 前景色填充的凸起段（默认）。 */
    XLcdNumberSegmentStyle_Flat = 2     /**< 前景色填充的平坦段。 */
} XLcdNumberSegmentStyle;

XCLASS_DEFINE_BEGING(XLcdNumber)
XCLASS_DEFINE_EXTEND_END(XLcdNumber, XFrame)

/**
 * @brief      XLcdNumber 控件对象；m_base 必须是第一个成员（嵌 XFrame）。
 */
typedef struct XLcdNumber
{
    XFrame m_base;                   /**< 基类成员；必须是第一个。 */
    double m_value;                  /**< 当前数值（对标 d->val）。 */
    int    m_digitCount;             /**< 位数（0..99）。 */
    int    m_mode;                   /**< 进制模式（XLcdNumberMode）。 */
    int    m_segmentStyle;           /**< 段风格（XLcdNumberSegmentStyle）。 */
    bool   m_smallDecimalPoint;      /**< 小数点并入数字位模式。 */
    char   m_digitStr[XLCDNUMBER_STR_MAX]; /**< 当前显示串（已裁剪）。 */
    unsigned char m_points[XLCDNUMBER_STR_MAX];
                                     /**< smallPoint 模式下各数字位的
                                          小数点标志（对标 points 位图）。 */
} XLcdNumber;

/* ==================== 生命周期 ==================== */

XVtable* XLcdNumber_class_init(void);
void XLcdNumber_init(XLcdNumber* self, XWidget* parent, XWidgetFlags flags);
/** @brief 以指定位数初始化（对标 QLCDNumber(uint, QWidget*) 构造）。 */
void XLcdNumber_init_2(XLcdNumber* self, unsigned numDigits,
                       XWidget* parent, XWidgetFlags flags);
#define XLcdNumber_create(parent, flags) XLcdNumber_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XLcdNumber* XLcdNumber_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
#define XLcdNumber_create_2(numDigits, parent, flags) \
    XLcdNumber_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (numDigits), (parent), (flags))
XLcdNumber* XLcdNumber_create_ex_2(XMemoryType memory, unsigned numDigits,
                                   XWidget* parent, XWidgetFlags flags);
#define XLcdNumber_deinit_base(self) XFrame_deinit_base((XFrame*)(self))
#define XLcdNumber_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 专属属性（对标 QLCDNumber public API） ==================== */

/** @brief 查询小数点并入数字位模式（默认 false）。 */
/**
 * @brief      获取小数点模式。
 */
bool XLcdNumber_smallDecimalPoint(const XLcdNumber* self);
/** @brief 设置小数点并入数字位模式并重绘（对标 setSmallDecimalPoint）。 */
/**
 * @brief      设置小数点模式。
 */
void XLcdNumber_setSmallDecimalPoint(XLcdNumber* self, bool on);
/** @brief 查询当前位数（默认 5）。 */
/**
 * @brief      获取位数。
 */
int XLcdNumber_digitCount(const XLcdNumber* self);
/** @brief 设置位数（0..99，越界钳位）并重绘（对标 setDigitCount）。 */
/**
 * @brief      设置位数。
 */
void XLcdNumber_setDigitCount(XLcdNumber* self, int numDigits);
/** @brief 查询进制模式（默认 Dec）。 */
/**
 * @brief      获取显示模式。
 */
XLcdNumberMode XLcdNumber_mode(const XLcdNumber* self);
/** @brief 设置进制模式并重绘。 */
/**
 * @brief      设置显示模式。
 */
void XLcdNumber_setMode(XLcdNumber* self, XLcdNumberMode mode);
/** @brief 查询段风格（默认 Filled）。 */
/**
 * @brief      获取分段风格。
 */
XLcdNumberSegmentStyle XLcdNumber_segmentStyle(const XLcdNumber* self);
/** @brief 设置段风格并重绘。 */
/**
 * @brief      设置分段风格。
 */
void XLcdNumber_setSegmentStyle(XLcdNumber* self,
                                XLcdNumberSegmentStyle style);
/** @brief 检查整数是否能按当前位数/进制显示（对标 checkOverflow(int)）。 */
bool XLcdNumber_checkOverflowInt(const XLcdNumber* self, int num);
/** @brief 检查浮点数是否能按当前位数/进制显示
 *         （对标 checkOverflow(double)；返回 true 表示溢出）。 */
bool XLcdNumber_checkOverflowDouble(const XLcdNumber* self, double num);
/** @brief 查询当前显示数值。 */
double XLcdNumber_value(const XLcdNumber* self);
/** @brief 查询当前数值四舍五入后的整数（对标 intValue）。 */
int XLcdNumber_intValue(const XLcdNumber* self);

/* ==================== 显示槽（对标 public slots display 重载） ==================== */

/** @brief 显示字符串：忽略 mode/smallDecimalPoint，非法字符替换空格；
 *         字符串可解析为数值时同步 value（对标 display(const QString&)）。 */
/**
 * @brief      显示数值。
 */
void XLcdNumber_display(XLcdNumber* self, const char* utf8);
/** @brief 显示整数（对标 display(int)；溢出发射 overflow 并保留旧显示）。 */
/**
 * @brief      显示整数值。
 */
void XLcdNumber_display_2(XLcdNumber* self, int num);
/** @brief 显示浮点数（对标 display(double)；溢出发射 overflow）。 */
/**
 * @brief      显示三值。
 */
void XLcdNumber_display_3(XLcdNumber* self, double num);
/** @brief 切换到十六进制模式（对标 setHexMode 便捷槽）。 */
/**
 * @brief      十六进制模式。
 */
void XLcdNumber_setHexMode(XLcdNumber* self);
/** @brief 切换到十进制模式（对标 setDecMode 便捷槽）。 */
/**
 * @brief      十进制模式。
 */
void XLcdNumber_setDecMode(XLcdNumber* self);
/** @brief 切换到八进制模式（对标 setOctMode 便捷槽）。 */
/**
 * @brief      八进制模式。
 */
void XLcdNumber_setOctMode(XLcdNumber* self);
/** @brief 切换到二进制模式（对标 setBinMode 便捷槽）。 */
/**
 * @brief      二进制模式。
 */
void XLcdNumber_setBinMode(XLcdNumber* self);

/* ==================== 信号（对标 QLCDNumber signals） ==================== */

/** @brief 显示内容超出位数容量时发射（对标 overflow）。 */
/**
 * @brief      溢出信号（真发射）。
 */
void* XLcdNumber_overflow_signal(XLcdNumber* self);

/* ==================== 父类 API 宏转发（对齐库内 XDial 惯例） ==================== */

#define XLcdNumber_sizeHint(self) XWidget_sizeHint((const XWidget*)(self))
#define XLcdNumber_frameStyle(self) XFrame_frameStyle((const XFrame*)(self))
#define XLcdNumber_setFrameStyle(self, style) XFrame_setFrameStyle((XFrame*)(self), (style))
#define XLcdNumber_frameShape(self) XFrame_frameShape((const XFrame*)(self))
#define XLcdNumber_setFrameShape(self, shape) XFrame_setFrameShape((XFrame*)(self), (shape))
#define XLcdNumber_frameShadow(self) XFrame_frameShadow((const XFrame*)(self))
#define XLcdNumber_setFrameShadow(self, shadow) XFrame_setFrameShadow((XFrame*)(self), (shadow))
#define XLcdNumber_lineWidth(self) XFrame_lineWidth((const XFrame*)(self))
#define XLcdNumber_setLineWidth(self, width) XFrame_setLineWidth((XFrame*)(self), (width))
#define XLcdNumber_frameWidth(self) XFrame_frameWidth((const XFrame*)(self))

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLCDNUMBER_H */
