#include"XTimerTest.h"
#include"XHrTimerGroup.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XThread.h"
#include <assert.h>
#include <windows.h> // for Sleep
// 全局变量用于测试回调
static int g_callback_count = 0;
static int g_single_shot_fired = 0;
static int g_periodic_fired = 0;
static XHandle g_periodic_handle = NULL;

// 测试回调函数
void test_timer_callback(void* userData,XTimerData* data) {
    g_callback_count++;
    if (data->m_isSingleShot) {
        g_single_shot_fired = 1;
    }
    else {
        g_periodic_fired++;
        // 在第3次触发后移除周期性定时器
        if (g_periodic_fired >= 3) {
            XHrTimerGroup* group = (XHrTimerGroup*)data->m_userData;
            XHrTimerGroup_removeTimer_base(group, g_periodic_handle);
        }
    }
}

void XHrTimerTest()
{
    while(true)
    {
        g_callback_count = 0;
        g_single_shot_fired = 0;
        g_periodic_fired = 0;
        g_periodic_handle = 0;
        printf("Starting XHrTimerGroup tests on Windows...\n");

        // 1. 创建定时器组 (精度设为1ms)
        XHrTimerGroup* timer_group = XHrTimerGroup_create(1000000ULL);
        assert(timer_group != NULL);
        printf("1. Timer group created.\n");

        // 2. 准备单次定时器数据 (50ms后触发)
        XTimerData single_shot_data = { 0 };
        single_shot_data.m_timeout = 50; // ms
        single_shot_data.m_interval = 0; // 单次
        single_shot_data.m_isSingleShot = true;
        single_shot_data.m_timerCallback = test_timer_callback;
        single_shot_data.m_userData = NULL;

        XHandle single_shot_handle = XHrTimerGroup_addTimerMs_base(timer_group, single_shot_data);
        assert(single_shot_handle != NULL);
        printf("2. Single-shot timer added.\n");

        // 3. 准备周期性定时器数据 (100ms间隔)
        XTimerData periodic_data = { 0 };
        periodic_data.m_timeout = 100; // 首次100ms后触发
        periodic_data.m_interval = 100; // 之后每100ms触发
        periodic_data.m_isSingleShot = false;
        periodic_data.m_timerCallback = test_timer_callback;
        periodic_data.m_userData = timer_group; // 传入group以便在回调中移除自己

        g_periodic_handle = XHrTimerGroup_addTimerMs_base(timer_group, periodic_data);
        assert(g_periodic_handle != NULL);
        printf("3. Periodic timer added.\n");

        // 4. 验证初始状态
        assert(g_callback_count == 0);
        assert(g_single_shot_fired == 0);
        assert(g_periodic_fired == 0);
        printf("4. Initial state verified.\n");

        // 5. 等待并触发滴答 (60ms)
        // 此时应该只触发单次定时器
        Sleep(60);
        XHrTimerGroup_tick_base(timer_group);

        assert(g_callback_count == 1);
        assert(g_single_shot_fired == 1);
        assert(g_periodic_fired == 0);
        printf("5. Single-shot timer fired correctly.\n");

        // 6. 再次滴答 (再等110ms)
        Sleep(110);
        XHrTimerGroup_tick_base(timer_group);

        assert(g_callback_count == 2);
        assert(g_periodic_fired == 1);
        printf("6. First periodic timer fired.\n");

        // 7. 再次滴答 (再等110ms)
        Sleep(110);
        XHrTimerGroup_tick_base(timer_group);

        assert(g_callback_count == 3);
        assert(g_periodic_fired == 2);
        printf("7. Second periodic timer fired.\n");

        // 8. 再次滴答 (再等110ms)
        // 周期性定时器第三次触发，并在回调中被移除
        Sleep(110);
        XHrTimerGroup_tick_base(timer_group);

        assert(g_callback_count == 4);
        assert(g_periodic_fired == 3);
        printf("8. Third periodic timer fired and removed.\n");

        // 9. 再等待一段时间，确保没有多余回调
        Sleep(200);
        XHrTimerGroup_tick_base(timer_group);

        assert(g_callback_count == 4); // 计数不应再增加
        printf("9. No extra callbacks fired after removal.\n");

        // 10. 清理资源
        XClass_delete_base(timer_group);
        //XDelete(timer_group);
        printf("10. All tests passed! Cleanup done.\n");
    }

    return 0;
	XCoreApplication_exec();
	//while(true) XTimeWheelGroup_handler_base(wheel);
}

void XMenu_XHrTimerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XHrTimer(高精度定时器组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XHrTimerTest);
	}
}