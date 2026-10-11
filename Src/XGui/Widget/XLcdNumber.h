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

/**
 * @brief      获取 XLcdNumber 虚函数表（首次调用时构建单例虚表）。
 * @details    继承 XFrame 虚表并重载 PaintEvent（七段数码管绘制）。
 * @return     虚函数表单例指针（库内部管理，借用；不得释放）。
 */
XVtable* XLcdNumber_class_init(void);
/**
 * @brief      初始化 XLcdNumber 控件（对标 QLCDNumber(parent) 构造）。
 * @details    内部对 self 清零后完成基类与成员初始化：默认 5 位、Dec
 *             进制、Filled 段风格、Box|Raised 边框、小数点独立占位。
 * @param      self 目标控件指针；指向一段未初始化的 XLcdNumber 存储；
 *             传入 NULL 时不执行任何操作。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XLcdNumber_init(XLcdNumber* self, XWidget* parent, XWidgetFlags flags);
/**
 * @brief      以指定位数初始化 XLcdNumber 控件（对标 QLCDNumber(uint,
 *             QWidget*) 构造）。
 * @details    默认状态同 XLcdNumber_init，仅位数由 numDigits 指定；
 *             目标未初始化时由本函数完成完整初始化。
 * @param      self 目标控件指针；指向一段未初始化的 XLcdNumber 存储；
 *             传入 NULL 时不执行任何操作。
 * @param      numDigits 位数；超出 0..99 按边界钳位。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XLcdNumber_init_2(XLcdNumber* self, unsigned numDigits,
                       XWidget* parent, XWidgetFlags flags);
/** @brief 创建堆对象便捷宏：以默认内存类型调用 XLcdNumber_create_ex。 */
#define XLcdNumber_create(parent, flags) XLcdNumber_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      创建 XLcdNumber 堆对象（对标 QLCDNumber(parent) 构造）。
 * @param      memory 内存分配器类型（XMemoryType）。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新建对象指针（默认状态同 XLcdNumber_init；调用方以
 *             XClassDelete 释放）；分配失败返回 NULL。
 */
XLcdNumber* XLcdNumber_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
/** @brief 创建堆对象便捷宏：以默认内存类型调用 XLcdNumber_create_ex_2。 */
#define XLcdNumber_create_2(numDigits, parent, flags) \
    XLcdNumber_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (numDigits), (parent), (flags))
/**
 * @brief      以指定位数创建 XLcdNumber 堆对象（对标 QLCDNumber(uint,
 *             QWidget*) 构造）。
 * @param      memory 内存分配器类型（XMemoryType）。
 * @param      numDigits 位数；超出 0..99 按边界钳位。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新建对象指针（默认状态同 XLcdNumber_init_2；调用方以
 *             XClassDelete 释放）；分配失败返回 NULL。
 */
XLcdNumber* XLcdNumber_create_ex_2(XMemoryType memory, unsigned numDigits,
                                   XWidget* parent, XWidgetFlags flags);

/* ==================== 专属属性（对标 QLCDNumber public API） ==================== */

/**
 * @brief      查询小数点并入数字位模式（对标 QLCDNumber::
 *             smallDecimalPoint）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @return     true 表示小数点并入数字位右下角；false 表示小数点独占
 *             一个数字位（默认）。self 为 NULL 时返回 false。
 */
bool XLcdNumber_smallDecimalPoint(const XLcdNumber* self);
/**
 * @brief      设置小数点并入数字位模式并重绘（对标
 *             QLCDNumber::setSmallDecimalPoint）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      on true 表示并入数字位右下角；false 表示独占一个数字位。
 * @return     无返回值；self 为 NULL 或模式未变化时保持原状态。
 */
void XLcdNumber_setSmallDecimalPoint(XLcdNumber* self, bool on);
/**
 * @brief      查询当前位数（对标 QLCDNumber::digitCount）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @return     当前位数（0..99，默认 5）；self 为 NULL 时返回 0。
 */
int XLcdNumber_digitCount(const XLcdNumber* self);
/**
 * @brief      设置位数并重绘（对标 QLCDNumber::setDigitCount）。
 * @details    位数变化后按当前数值与进制重新裁剪显示串，并更新尺寸
 *             建议与重绘；超出 0..99 按边界钳位（对标 Qt 侧警告+钳位）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      numDigits 新位数；负数取 0，大于 99 取 99。
 * @return     无返回值；self 为 NULL 或位数未变化时保持原状态。
 */
void XLcdNumber_setDigitCount(XLcdNumber* self, int numDigits);
/**
 * @brief      查询进制模式（对标 QLCDNumber::mode）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @return     当前进制模式（默认 XLcdNumberMode_Dec）；self 为 NULL 时
 *             返回 XLcdNumberMode_Dec。
 */
XLcdNumberMode XLcdNumber_mode(const XLcdNumber* self);
/**
 * @brief      设置进制模式并按当前数值重显（对标 QLCDNumber::setMode）。
 * @details    进制变化后按当前数值重算显示串（非 Dec 模式按整数等价
 *             显示）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      mode 目标进制模式（XLcdNumberMode）。
 * @return     无返回值；self 为 NULL 或模式未变化时保持原状态。
 */
void XLcdNumber_setMode(XLcdNumber* self, XLcdNumberMode mode);
/**
 * @brief      查询段绘制风格（对标 QLCDNumber::segmentStyle）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @return     当前段风格（默认 XLcdNumberSegmentStyle_Filled）；self 为
 *             NULL 时返回 XLcdNumberSegmentStyle_Filled。
 */
XLcdNumberSegmentStyle XLcdNumber_segmentStyle(const XLcdNumber* self);
/**
 * @brief      设置段绘制风格并重绘（对标 QLCDNumber::setSegmentStyle）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      style 目标段风格（XLcdNumberSegmentStyle）。
 * @return     无返回值；self 为 NULL 或风格未变化时保持原状态。
 */
void XLcdNumber_setSegmentStyle(XLcdNumber* self,
                                XLcdNumberSegmentStyle style);
/**
 * @brief      检查整数能否按当前位数/进制完整显示（对标
 *             QLCDNumber::checkOverflow(int)）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @param      num 待检查整数。
 * @return     溢出（无法完整显示）返回 true；self 为 NULL 时返回 false。
 */
bool XLcdNumber_checkOverflowInt(const XLcdNumber* self, int num);
/**
 * @brief      检查浮点数能否按当前位数/进制完整显示（对标
 *             QLCDNumber::checkOverflow(double)）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @param      num 待检查浮点数。
 * @return     溢出（无法完整显示）返回 true；self 为 NULL 时返回 false。
 */
bool XLcdNumber_checkOverflowDouble(const XLcdNumber* self, double num);
/**
 * @brief      查询当前显示数值（对标 QLCDNumber::value）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @return     当前数值；self 为 NULL 时返回 0.0。
 */
double XLcdNumber_value(const XLcdNumber* self);
/**
 * @brief      查询当前数值四舍五入后的整数（对标 QLCDNumber::intValue）。
 * @param      self 目标控件借用指针；可为 NULL。
 * @return     四舍五入到最近整数的数值；self 为 NULL 时返回 0。
 */
int XLcdNumber_intValue(const XLcdNumber* self);

/* ==================== 显示槽（对标 public slots display 重载） ==================== */

/**
 * @brief      显示 UTF-8 字符串（对标 QLCDNumber::display(const
 *             QString&)）。
 * @details    显示串忽略 mode/smallDecimalPoint（超长右对齐裁剪到当前
 *             位数）；整串（允许首尾空白）可解析为数值时同步 value，
 *             否则置 0（对标 QString::toDouble 失败置 0）。本路径不判
 *             溢出、不发射 overflow。
 * @param      self 目标控件指针；可为 NULL。
 * @param      utf8 待显示字符串（UTF-8，NUL 结尾；借用，不取得所有权；
 *             NULL 按空串处理，超出缓冲容量时截断）。
 * @return     无返回值。
 */
void XLcdNumber_display(XLcdNumber* self, const char* utf8);
/**
 * @brief      显示整数（对标 QLCDNumber::display(int)）。
 * @details    按当前进制与位数格式化并同步 value；溢出时发射 overflow
 *             信号并保留旧显示。
 * @param      self 目标控件指针；可为 NULL。
 * @param      num 待显示整数。
 * @return     无返回值。
 */
void XLcdNumber_display_2(XLcdNumber* self, int num);
/**
 * @brief      显示浮点数（对标 QLCDNumber::display(double)）。
 * @details    按当前进制与位数格式化并同步 value（仅 Dec 模式显示小数
 *             部分，其余进制按整数等价）；溢出时发射 overflow 信号并
 *             保留旧显示。
 * @param      self 目标控件指针；可为 NULL。
 * @param      num 待显示浮点数。
 * @return     无返回值。
 */
void XLcdNumber_display_3(XLcdNumber* self, double num);
/**
 * @brief      切换到十六进制模式并按当前数值重显（对标
 *             QLCDNumber::setHexMode 便捷槽）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     无返回值；模式未变化时保持原状态。
 */
void XLcdNumber_setHexMode(XLcdNumber* self);
/**
 * @brief      切换到十进制模式并按当前数值重显（对标
 *             QLCDNumber::setDecMode 便捷槽）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     无返回值；模式未变化时保持原状态。
 */
void XLcdNumber_setDecMode(XLcdNumber* self);
/**
 * @brief      切换到八进制模式并按当前数值重显（对标
 *             QLCDNumber::setOctMode 便捷槽）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     无返回值；模式未变化时保持原状态。
 */
void XLcdNumber_setOctMode(XLcdNumber* self);
/**
 * @brief      切换到二进制模式并按当前数值重显（对标
 *             QLCDNumber::setBinMode 便捷槽）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     无返回值；模式未变化时保持原状态。
 */
void XLcdNumber_setBinMode(XLcdNumber* self);

/* ==================== 信号（对标 QLCDNumber signals） ==================== */

/**
 * @brief      overflow() 信号标识（对标 QLCDNumber::overflow）。
 * @details    显示内容超出位数容量时由 display_2/display_3 发射。
 * @param      self 发射者控件借用指针；可为 NULL（本实现不校验）。
 * @return     不透明的信号标识（仅用作 XObject_connect 的信号参数；
 *             不得当作函数指针调用，不得释放）。
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

#endif /* XLCDNUMBER_H */
