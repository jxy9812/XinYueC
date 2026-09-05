#include "XPrintf.h"
#include"XCodeTest.h"
#include"XMemory.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XDateTime.h"
#include"XAtomic.h"
// 辅助函数：打印 XString 内容
void print_xstring(const char* label, const XString* str) {
    if (str == NULL) {
        XPrintf("%s: (NULL)\n", label);
        return;
    }
    //XPrintf_(str);
    XPrintf("%s\n",XString_toUtf8(str));
}

// 辅助函数：验证并打印日期
void test_and_print_date(const char* label, XDate date) {
    XPrintf("\n--- %s ---\n", label);
    XPrintf("Valid: %s\n", XDate_isValid(&date) ? "Yes" : "No");
    if (XDate_isValid(&date)) {
        XPrintf("Year: %d, Month: %d, Day: %d\n",
            XDate_year(&date), XDate_month(&date), XDate_day(&date));
        XPrintf("Day of Week: %d, Day of Year: %d\n",
            XDate_dayOfWeek(&date), XDate_dayOfYear(&date));
        XPrintf("Days in Month/Year: %d / %d\n",
            XDate_daysInMonth(&date), XDate_daysInYear(&date));
    }
}

// 辅助函数：验证并打印时间
void test_and_print_time(const char* label, XTime time) {
    XPrintf("\n--- %s ---\n", label);
    XPrintf("Valid: %s\n", XTime_isValid(&time) ? "Yes" : "No");
    if (XTime_isValid(&time)) {
        XPrintf("Hour: %d, Minute: %d, Second: %d, Millisecond: %d\n",
            XTime_hour(&time), XTime_minute(&time), XTime_second(&time), XTime_msec(&time));
    }
}

// 辅助函数：验证并打印日期时间
void test_and_print_datetime(const char* label, XDateTime datetime) {
    XPrintf("\n--- %s ---\n", label);
    XPrintf("Valid: %s\n", XDateTime_isValid(&datetime) ? "Yes" : "No");
    if (XDateTime_isValid(&datetime)) {
        int64_t msecs_since_epoch = XDateTime_toMSecsSinceEpoch(&datetime);
        XPrintf("Timestamp (ms): %lld\n", (long long)msecs_since_epoch);

        XDate date = XDateTime_date(&datetime);
        XTime time = XDateTime_time(&datetime);
        XPrintf("Date: %d-%02d-%02d\n", XDate_year(&date), XDate_month(&date), XDate_day(&date));
        XPrintf("Time: %02d:%02d:%02d.%03d\n",
            XTime_hour(&time), XTime_minute(&time), XTime_second(&time), XTime_msec(&time));
    }
}
void XDateTimeTest()
{
    XPrintf("=== XDate, XTime, XDateTime Comprehensive Test ===\n");

    // ====== 1. XDate 测试 ======
    XPrintf("\n========== Testing XDate ==========\n");

    // 1.1 有效性检查
    XDate invalid_date = XDate_create();
    test_and_print_date("Invalid Date (Created Empty)", invalid_date);

    // 1.2 创建有效日期
    XDate valid_date = XDate_create_date(2023, 12, 25);
    test_and_print_date("Valid Date (2023-12-25)", valid_date);

    // 1.3 静态方法测试
    XPrintf("\nStatic Validation: XDate_isValid_static(2023, 2, 29) = %s\n",
        XDate_isValid_static(2023, 2, 29) ? "Valid" : "Invalid");
    XPrintf("Static Leap Year: XDate_isLeapYear_static(2024) = %s\n",
        XDate_isLeapYear_static(2024) ? "Leap" : "Not Leap");

    // 1.4 日期运算
    XDate date_plus_10 = XDate_addDays(&valid_date, 10);
    test_and_print_date("Date + 10 Days", date_plus_10);

    XDate date_next_month = XDate_addMonths(&valid_date, 1);
    test_and_print_date("Date + 1 Month", date_next_month);

    // 1.5 格式化与解析
    XString* iso_str = XDate_toString_iso(&valid_date);
    print_xstring("ISO Format String", iso_str);

    XDate parsed_date = XDate_fromString_iso("2023-12-25");
    test_and_print_date("Parsed from '2023-12-25'", parsed_date);

    // 清理
    XString_delete_base(iso_str);

    // ====== 2. XTime 测试 ======
    XPrintf("\n========== Testing XTime ==========\n");

    // 2.1 有效性检查
    XTime invalid_time = XTime_create();
    test_and_print_time("Invalid Time (Created Empty)", invalid_time);

    // 2.2 创建有效时间
    XTime valid_time = XTime_create_time(14, 30, 45, 123);
    test_and_print_time("Valid Time (14:30:45.123)", valid_time);

    // 2.3 时间运算
    XTime time_plus_30s = XTime_addSecs(&valid_time, 30);
    test_and_print_time("Time + 30 Seconds", time_plus_30s);

    XTime time_minus_1h = XTime_addSecs(&valid_time, -3600);
    test_and_print_time("Time - 1 Hour", time_minus_1h);

    // 2.4 时间差计算
    int secs_diff = XTime_secsTo(&valid_time, &time_plus_30s);
    int msecs_diff = XTime_msecsTo(&valid_time, &time_plus_30s);
    XPrintf("\nTime Difference: %d seconds, %d milliseconds\n", secs_diff, msecs_diff);

    // 2.5 格式化与解析 (简化版)
    XString* time_str = XTime_toString_format(&valid_time, "HH:mm:ss");
    print_xstring("Formatted Time String", time_str);

    XTime parsed_time = XTime_fromString_format("14:30:45", "HH:mm:ss");
    test_and_print_time("Parsed from '14:30:45'", parsed_time);

    // 清理
    XString_delete_base(time_str);

    // ====== 3. XDateTime 测试 ======
    XPrintf("\n========== Testing XDateTime ==========\n");

    // 3.1 有效性检查
    XDateTime invalid_datetime = XDateTime_create();
    test_and_print_datetime("Invalid DateTime (Created Empty)", invalid_datetime);

    // 3.2 创建有效日期时间
    XDateTime valid_datetime = XDateTime_create_datetime(valid_date, valid_time);
    test_and_print_datetime("Valid DateTime (2023-12-25 14:30:45.123)", valid_datetime);

    // 3.3 日期时间运算
    XDateTime dt_plus_1d = XDateTime_addDays(&valid_datetime, 1);
    test_and_print_datetime("DateTime + 1 Day", dt_plus_1d);

    XDateTime dt_plus_1h = XDateTime_addSecs(&valid_datetime, 3600);
    test_and_print_datetime("DateTime + 1 Hour", dt_plus_1h);

    // 3.4 时间戳转换
    int64_t timestamp_ms = XDateTime_toMSecsSinceEpoch(&valid_datetime);
    XDateTime dt_from_ts;
    XDateTime_setMSecsSinceEpoch(&dt_from_ts, timestamp_ms);
    test_and_print_datetime("DateTime from Timestamp", dt_from_ts);

    // 3.5 格式化与解析
    XString* dt_iso_str = XDateTime_toString_iso(&valid_datetime);
    print_xstring("DateTime ISO Format", dt_iso_str);

    XDateTime parsed_dt = XDateTime_fromString_iso("2023-12-25 14:30:45");
    test_and_print_datetime("Parsed DateTime from ISO", parsed_dt);

    // 自定义格式测试
    XString* custom_format_str = XDateTime_toString_format(&valid_datetime, "yyyy/MM/dd HH-mm-ss");
    print_xstring("Custom Format 'yyyy/MM/dd HH-mm-ss'", custom_format_str);

    // 清理
    XString_delete_base(dt_iso_str);
    XString_delete_base(custom_format_str);

    XPrintf("\n=== All tests completed ===\n");
    return 0;
		XCoreApplication_exec();
		//XThread_deleteLater(th);

		XCoreApplication_processEvents(XEventLoop_AllEvents);
}

void XTestMenu_XDateTimeTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XDateTime(日期时间)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XDateTimeTest);
	}
}