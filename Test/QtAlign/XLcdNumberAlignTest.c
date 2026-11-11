/**
 * @file       XLcdNumberAlignTest.c
 * @brief      XLcdNumber × Qt 6.8 QLCDNumber 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qlcdnumber.h
 *               （Mode/SegmentStyle 枚举、digitCount/smallDecimalPoint/
 *               segmentStyle/checkOverflow(int,double)/value/intValue、
 *               display 三个重载与 setHexMode 等便捷槽、sizeHint）；
 *             - Qt 6.8 源码 qlcdnumber.cpp（jsdelivr
 *               qtbase@v6.8.0/src/widgets/widgets/qlcdnumber.cpp）：
 *               `QLCDNumberPrivate::init` 置 Box|Raised、Dec、
 *               smallPoint=false、Filled、Minimum/Minimum 尺寸策略
 *               （:319-331）、sizeHint = 10+9*(digitCount+
 *               (smallDecimalPoint?0:1)) 高 23（:1161-1167）、
 *               setDigitCount 0..99 钳位并保留现有显示串（:371-408）、
 *               display(QString) 先 val=0 再整串 toDouble（:283-291）、
 *               display(int/double) 先写 val 再格式化、溢出只发 overflow
 *               （:263-281）、intValue = qRound（:302-309）、
 *               segmentStyle 映射 fill=(Flat|Filled)、
 *               shadow=(Outline|Filled)（:1143-1147）；
 *             - 本机 Qt 6.8.0 + offscreen 实测：缺省 digits=5/mode=1/
 *               seg=1/small=0/frameStyle=33/sizeHint 64x23
 *               （smallPoint 时 55x23）、minimumSizeHint (-1,-1)；
 *               display("12.5")→value 12.5、display("abc")→0；
 *               display(123456) 五位 → value 123456 且 checkOverflow=1；
 *               3 位时十进制 999/-99 不溢出、1000/-100 溢出；
 *               3 位 Hex 4095/4096、Bin 7/8、Oct 511/512 为边界。
 *
 *             覆盖：缺省属性与 sizeHint、位数钳位与进制便捷槽、
 *             溢出矩阵与 overflow 信号、display 字符串/数值语义、
 *             父子所有权。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON

#include "XLcdNumber.h"
#include "XFrame.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"

/* ==================== 信号探针 ==================== */

static int lcd_overflowCount = 0;

static void lcd_overflowSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++lcd_overflowCount;
}

/* ==================== 用例 ==================== */

/** @brief 缺省状态与 sizeHint（qlcdnumber.cpp:320-332, 1164-1167）。 */
XQ_TEST(XLcdNumber, defaultsAndSizeHint)
{
    XLcdNumber* lcd = XLcdNumber_create(NULL, 0);
    XSize hint;
    XSize minHint;
    XQ_REQUIRE(lcd != NULL);

    XQ_EQ_I64(XLcdNumber_digitCount(lcd), 5);
    XQ_EQ_I64(XLcdNumber_mode(lcd), (int)XLcdNumberMode_Dec);
    XQ_EQ_I64(XLcdNumber_segmentStyle(lcd),
              (int)XLcdNumberSegmentStyle_Filled);
    XQ_CHECK(!XLcdNumber_smallDecimalPoint(lcd));
    /* QLCDNumberPrivate::init：setFrameStyle(Box | Raised)（:323）→ 33。 */
    XQ_EQ_I64(XFrame_frameStyle((XFrame*)lcd),
              (int)(XFrameShape_Box | XFrameShadow_Raised));
    XQ_CHECK(XLcdNumber_value(lcd) == 0.0);
    XQ_EQ_I64(XLcdNumber_intValue(lcd), 0);

    /* sizeHint = 10 + 9*(5+1) x 23（Qt 实测 64x23）。 */
    hint = XLcdNumber_sizeHint(lcd);
    XQ_EQ_I64(hint.width, 10 + 9 * (5 + 1));
    XQ_EQ_I64(hint.height, 23);

    /* smallDecimalPoint=true 时小数点并入数字位，少一个位宽系数
     * （Qt 实测 55x23）。 */
    XLcdNumber_setSmallDecimalPoint(lcd, true);
    hint = XLcdNumber_sizeHint(lcd);
    XQ_EQ_I64(hint.width, 10 + 9 * 5);
    XQ_EQ_I64(hint.height, 23);
    XLcdNumber_setSmallDecimalPoint(lcd, false);

    /* Qt 6.8 实测 minimumSizeHint=(-1,-1)：QLCDNumber 未覆写
     * minimumSizeHint（qlcdnumber.cpp 全文无该函数），基类
     * QWidget::minimumSizeHint 返回无效尺寸；布局按 Minimum 策略
     * 回落 sizeHint。 */
    minHint = XWidget_minimumSizeHint((XWidget*)lcd);
    XQ_EQ_I64(minHint.width, -1);
    XQ_EQ_I64(minHint.height, -1);

    /* 尺寸策略 Minimum/Minimum（QLCDNumberPrivate::init qlcdnumber.cpp:330）。 */
    {
        XWidgetSizePolicy policy = XWidget_sizePolicy((XWidget*)lcd);
        XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
                  (int)XWidgetSizePolicy_Minimum);
        XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
                  (int)XWidgetSizePolicy_Minimum);
    }

    XClassDelete((XClass*)lcd);
}

/** @brief 位数钳位与进制便捷槽（qlcdnumber.cpp:360-405, :470-475）。 */
XQ_TEST(XLcdNumber, digitCountAndModes)
{
    XLcdNumber* lcd = XLcdNumber_create(NULL, 0);
    XSize hint;
    XQ_REQUIRE(lcd != NULL);

    XLcdNumber_setDigitCount(lcd, 3);
    XQ_EQ_I64(XLcdNumber_digitCount(lcd), 3);
    hint = XLcdNumber_sizeHint(lcd);
    XQ_EQ_I64(hint.width, 10 + 9 * (3 + 1));
    /* 0..99 之外钳位（Qt 告警后钳位，:373-382）。 */
    XLcdNumber_setDigitCount(lcd, 0);
    XQ_EQ_I64(XLcdNumber_digitCount(lcd), 0);
    XQ_EQ_I64(XLcdNumber_sizeHint(lcd).width, 10 + 9 * (0 + 1));
    XLcdNumber_setDigitCount(lcd, -5);
    XQ_EQ_I64(XLcdNumber_digitCount(lcd), 0);
    XLcdNumber_setDigitCount(lcd, 200);
    XQ_EQ_I64(XLcdNumber_digitCount(lcd), 99);

    /* 便捷槽（q lcdnumber.h:61-64）。 */
    XLcdNumber_setDigitCount(lcd, 5);
    XLcdNumber_setHexMode(lcd);
    XQ_EQ_I64(XLcdNumber_mode(lcd), (int)XLcdNumberMode_Hex);
    XLcdNumber_setOctMode(lcd);
    XQ_EQ_I64(XLcdNumber_mode(lcd), (int)XLcdNumberMode_Oct);
    XLcdNumber_setBinMode(lcd);
    XQ_EQ_I64(XLcdNumber_mode(lcd), (int)XLcdNumberMode_Bin);
    XLcdNumber_setDecMode(lcd);
    XQ_EQ_I64(XLcdNumber_mode(lcd), (int)XLcdNumberMode_Dec);

    /* 段风格三态往返（setSegmentStyle 只存风格，绘制按 fill/shadow 映射）。 */
    XLcdNumber_setSegmentStyle(lcd, XLcdNumberSegmentStyle_Outline);
    XQ_EQ_I64(XLcdNumber_segmentStyle(lcd),
              (int)XLcdNumberSegmentStyle_Outline);
    XLcdNumber_setSegmentStyle(lcd, XLcdNumberSegmentStyle_Flat);
    XQ_EQ_I64(XLcdNumber_segmentStyle(lcd),
              (int)XLcdNumberSegmentStyle_Flat);
    XLcdNumber_setSegmentStyle(lcd, XLcdNumberSegmentStyle_Filled);
    XQ_EQ_I64(XLcdNumber_segmentStyle(lcd),
              (int)XLcdNumberSegmentStyle_Filled);

    XClassDelete((XClass*)lcd);
}

/** @brief checkOverflow 矩阵 + overflow 信号（qlcdnumber.cpp:427-449, :539-547）。 */
XQ_TEST(XLcdNumber, overflowMatrix)
{
    XLcdNumber* lcd = XLcdNumber_create_2(3u, NULL, 0);
    XQ_REQUIRE(lcd != NULL);

    /* Qt 6.8 实测（3 位十进制）：999/-99 可显示，1000/-100 溢出。 */
    XQ_CHECK(!XLcdNumber_checkOverflowInt(lcd, 999));
    XQ_CHECK(XLcdNumber_checkOverflowInt(lcd, 1000));
    XQ_CHECK(!XLcdNumber_checkOverflowInt(lcd, -99));
    XQ_CHECK(XLcdNumber_checkOverflowInt(lcd, -100));

    /* 3 位 Hex：4095(FFF) 可显示、4096 溢出。 */
    XLcdNumber_setHexMode(lcd);
    XQ_CHECK(!XLcdNumber_checkOverflowInt(lcd, 4095));
    XQ_CHECK(XLcdNumber_checkOverflowInt(lcd, 4096));
    /* 3 位 Bin：7(111) 可显示、8 溢出。 */
    XLcdNumber_setBinMode(lcd);
    XQ_CHECK(!XLcdNumber_checkOverflowInt(lcd, 7));
    XQ_CHECK(XLcdNumber_checkOverflowInt(lcd, 8));
    /* 3 位 Oct：511(777) 可显示、512 溢出。 */
    XLcdNumber_setOctMode(lcd);
    XQ_CHECK(!XLcdNumber_checkOverflowInt(lcd, 511));
    XQ_CHECK(XLcdNumber_checkOverflowInt(lcd, 512));
    XLcdNumber_setDecMode(lcd);

    /* display(int) 溢出发 overflow 且值仍更新、旧显示保留
     * （qlcdnumber.cpp:539-547；Qt 实测 value 仍为入参）。 */
    lcd_overflowCount = 0;
    XObject_connect_2((XObject*)lcd, XSignal(XLcdNumber_overflow_signal),
                      lcd_overflowSlot);
    XLcdNumber_display_2(lcd, 1000);
    XQ_EQ_I64(lcd_overflowCount, 1);
    XQ_CHECK(XLcdNumber_value(lcd) == 1000.0);
    XLcdNumber_display_2(lcd, 999);
    XQ_EQ_I64(lcd_overflowCount, 1);

    XClassDelete((XClass*)lcd);
}

/** @brief display 字符串/数值语义（qlcdnumber.cpp:565-573, :502-514, :527-531）。 */
XQ_TEST(XLcdNumber, displayValues)
{
    XLcdNumber* lcd = XLcdNumber_create(NULL, 0);
    XQ_REQUIRE(lcd != NULL);

    /* display(const QString&)：整串 toDouble 成功才写 value（Qt 实测
     * "12.5" → 12.5），失败置 0。 */
    XLcdNumber_display(lcd, "12.5");
    XQ_CHECK(XLcdNumber_value(lcd) == 12.5);
    /* intValue = qRound（12.5 → 13；-12.5 → -13，half away from zero）。 */
    XQ_EQ_I64(XLcdNumber_intValue(lcd), 13);
    XLcdNumber_display(lcd, "-12.5");
    XQ_EQ_I64(XLcdNumber_intValue(lcd), -13);
    XQ_CHECK(XLcdNumber_value(lcd) == -12.5);
    /* 尾巴带非数值字符 ⇒ 整串解析失败 ⇒ 0（与 Qt QString::toDouble 同口径）。 */
    XLcdNumber_display(lcd, "12.5px");
    XQ_CHECK(XLcdNumber_value(lcd) == 0.0);
    XLcdNumber_display(lcd, "abc");
    XQ_CHECK(XLcdNumber_value(lcd) == 0.0);

    /* display(int/double) 同步 value；非 Dec 模式按整数等价显示。 */
    XLcdNumber_display_2(lcd, 123);
    XQ_CHECK(XLcdNumber_value(lcd) == 123.0);
    XLcdNumber_display_3(lcd, 321.0);
    XQ_CHECK(XLcdNumber_value(lcd) == 321.0);

    XClassDelete((XClass*)lcd);
}

/** @brief 父子所有权：LCD 作为子控件随父析构释放。 */
XQ_TEST(XLcdNumber, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XLcdNumber* lcd = NULL;
    XQ_REQUIRE(host != NULL);
    lcd = XLcdNumber_create(host, 0);
    XQ_REQUIRE(lcd != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)lcd) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)lcd));
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON */
