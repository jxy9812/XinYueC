#include "XLcdNumberTest.h"
#include "XLcdNumber.h"
#if XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON
#include "XMemory.h"
#include "XEvent.h"
#include <stdio.h>
#include <string.h>

static int ln_failures = 0;
static int ln_overflowCount = 0;

static void ln_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[LCD-FAIL] %s\n", what ? what : "");
        ++ln_failures;
    }
}

static void ln_overflowSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++ln_overflowCount;
}

bool XLcdNumberTest_runAll(void)
{
    XLcdNumber* lcd = XLcdNumber_create(NULL, 0);

    /* 1. 默认状态（对标 QLCDNumber 构造：5 位、Dec、Filled、非小点、
          Box|Raised 边框、值 0）。 */
    ln_expect(lcd != NULL, "XLcdNumber 创建");
    ln_expect(XLcdNumber_digitCount(lcd) == 5, "默认 5 位");
    ln_expect(XLcdNumber_mode(lcd) == XLcdNumberMode_Dec, "默认 Dec");
    ln_expect(XLcdNumber_segmentStyle(lcd) == XLcdNumberSegmentStyle_Filled,
              "默认 Filled");
    ln_expect(!XLcdNumber_smallDecimalPoint(lcd), "默认非小数点模式");
    ln_expect(XLcdNumber_value(lcd) == 0.0, "默认值 0");
    ln_expect(XLcdNumber_intValue(lcd) == 0, "默认整值 0");
    ln_expect((XLcdNumber_frameStyle(lcd) & (int)XFrameShape_Box) != 0 &&
              (XLcdNumber_frameStyle(lcd) & (int)XFrameShadow_Raised) != 0,
              "默认 Box|Raised 边框");
    {
        XSize hint = XLcdNumber_sizeHint(lcd);
        ln_expect(hint.width == 10 + 9 * (5 + 1) && hint.height == 23,
                  "sizeHint 对标 Qt 公式");
    }

    /* 2. 整数显示与进制。 */
    XLcdNumber_display_2(lcd, 42);
    ln_expect(XLcdNumber_value(lcd) == 42.0, "display_2(42) 后值为 42");
    ln_expect(XLcdNumber_intValue(lcd) == 42, "整值 42");
    XLcdNumber_setHexMode(lcd);
    ln_expect(XLcdNumber_mode(lcd) == XLcdNumberMode_Hex, "setHexMode 生效");
    ln_expect(XLcdNumber_value(lcd) == 42.0, "进制切换值不变");
    XLcdNumber_setDecMode(lcd);
    XLcdNumber_setOctMode(lcd);
    ln_expect(XLcdNumber_mode(lcd) == XLcdNumberMode_Oct, "setOctMode 生效");
    XLcdNumber_setBinMode(lcd);
    ln_expect(XLcdNumber_mode(lcd) == XLcdNumberMode_Bin, "setBinMode 生效");
    XLcdNumber_setDecMode(lcd);

    /* 3. checkOverflow 语义（对标 Qt：放不下返回 true）。 */
    ln_expect(!XLcdNumber_checkOverflowInt(lcd, 12345), "12345 五位不溢出");
    ln_expect(XLcdNumber_checkOverflowInt(lcd, 123456), "123456 五位溢出");
    ln_expect(!XLcdNumber_checkOverflowDouble(lcd, 3.14), "3.14 五位不溢出");
    /* 对标 Qt：%g 科学计数（如 1e+07）5 位内可显示，不算溢出。 */
    ln_expect(!XLcdNumber_checkOverflowDouble(lcd, 12345678.0),
              "科学计数可显示时不溢出");
    /* 对标 Qt double2string：非 Dec 模式超出 int32 范围判溢出。 */
    XLcdNumber_setBinMode(lcd);
    ln_expect(XLcdNumber_checkOverflowDouble(lcd, 3.0e9),
              "Bin 模式超 int32 浮点溢出");
    ln_expect(!XLcdNumber_checkOverflowDouble(lcd, 15.0),
              "Bin 模式 15（1111 四位）不溢出");
    XLcdNumber_setDecMode(lcd);

    /* 4. overflow 信号：溢出显示保留旧值并发射一次。 */
    {
        XObject_connect_2((XObject*)lcd, XSignal(XLcdNumber_overflow_signal),
                          ln_overflowSlot);
        ln_overflowCount = 0;
        XLcdNumber_display_2(lcd, 123456);
        ln_expect(ln_overflowCount == 1, "溢出发射 overflow 一次");
        ln_expect(XLcdNumber_value(lcd) == 123456.0,
                  "溢出时值仍更新（对标 Qt val 赋值）");
        XLcdNumber_display_2(lcd, 999);
        ln_expect(ln_overflowCount == 1, "不溢出不发射");
    }

    /* 5. 位数控件：0..99 钳位。 */
    XLcdNumber_setDigitCount(lcd, 200);
    ln_expect(XLcdNumber_digitCount(lcd) == 99, "位数上限 99");
    XLcdNumber_setDigitCount(lcd, -5);
    ln_expect(XLcdNumber_digitCount(lcd) == 0, "位数下限 0");
    XLcdNumber_setDigitCount(lcd, 5);

    /* 6. 小数点模式影响 sizeHint（smallPoint 时宽度少一位系数）。 */
    XLcdNumber_setSmallDecimalPoint(lcd, true);
    ln_expect(XLcdNumber_smallDecimalPoint(lcd), "setSmallDecimalPoint 生效");
    {
        XSize hint = XLcdNumber_sizeHint(lcd);
        ln_expect(hint.width == 10 + 9 * 5, "smallPoint 时 sizeHint 收缩");
    }
    XLcdNumber_setSmallDecimalPoint(lcd, false);

    /* 7. 浮点显示与 intValue 四舍五入。 */
    XLcdNumber_display_3(lcd, 3.7);
    ln_expect(XLcdNumber_value(lcd) == 3.7, "display_3(3.7) 值一致");
    ln_expect(XLcdNumber_intValue(lcd) == 4, "intValue 四舍五入为 4");

    /* 8. 字符串显示：可解析前缀同步 value（对标 display(QString)）。 */
    XLcdNumber_display(lcd, "12.5abc");
    ln_expect(XLcdNumber_value(lcd) == 12.5, "字符串前缀解析 12.5");
    XLcdNumber_display(lcd, "hello");
    ln_expect(XLcdNumber_value(lcd) == 0.0, "不可解析串值为 0");

    /* 9. 指定位数构造重载。 */
    {
        XLcdNumber* lcd2 = XLcdNumber_create_2(8u, NULL, 0);
        ln_expect(lcd2 != NULL && XLcdNumber_digitCount(lcd2) == 8,
                  "create_2 指定 8 位");
        XLcdNumber_delete_base(lcd2);
    }

    XLcdNumber_delete_base(lcd);

    {
        int failures = ln_failures;
        ln_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XLcdNumber test: PASS\n");
            return true;
        }
        fprintf(stderr, "XLcdNumber test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}
#else
bool XLcdNumberTest_runAll(void)
{
    fprintf(stderr, "XLcdNumberTest: skipped (module off)\n");
    return true;
}
#endif /* XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON */
