#include "XGuiTest.h"
#if DEMOTEST
#include "XImageIOHandler.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XMemory.h"
#include <string.h>

/* ==================== XImageIOHandler 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XImageIOHandlerCreateTest(void)
{
    XPrintf("===== XImageIOHandler 创建与初始化测试 =====\n");
    {
        XImageIOHandler handler;
        XImageIOHandler_init(&handler);
        XPrintf("init: device=%s (期望:NULL)\n", XImageIOHandler_device(&handler) ? "非NULL" : "NULL");
        XPrintf("format=%s (期望:NULL)\n", XImageIOHandler_format(&handler) ? XImageIOHandler_format(&handler) : "NULL");
        XImageIOHandler_deinit_base(&handler);
    }
    XPrintf("\n");
}

/**
 * @brief      设备管理测试
 */
static void XImageIOHandlerDeviceTest(void)
{
    XPrintf("===== 设备管理测试 =====\n");
    XImageIOHandler handler;
    XImageIOHandler_init(&handler);
    /* 设置格式 */
    XImageIOHandler_setFormat(&handler, "PNG");
    XPrintf("setFormat('PNG'): format=%s (期望:PNG)\n", XImageIOHandler_format(&handler));
    XImageIOHandler_deinit_base(&handler);
    XPrintf("\n");
}

/**
 * @brief      虚函数默认行为测试
 */
static void XImageIOHandlerVirtualTest(void)
{
    XPrintf("===== 虚函数默认行为测试 =====\n");
    XImageIOHandler handler;
    XImageIOHandler_init(&handler);
    XPrintf("canRead (默认虚函数) = %s (期望:否)\n", XImageIOHandler_canRead_base(&handler) ? "是" : "否");
    XImage img;
    XImage_init(&img);
    XPrintf("read (默认虚函数) = %s (期望:否)\n", XImageIOHandler_read_base(&handler, &img) ? "是" : "否");
    XImage_deinit_base(&img);
    XPrintf("write (默认虚函数) = %s (期望:否)\n", XImageIOHandler_write_base(&handler, NULL) ? "是" : "否");
    int val = 0;
    XPrintf("option (默认虚函数) = %s (期望:否)\n", XImageIOHandler_option_base(&handler, XImageIOHandlerOption_Size, &val) ? "是" : "否");
    XPrintf("supportsOption (默认虚函数) = %s (期望:否)\n", XImageIOHandler_supportsOption_base(&handler, XImageIOHandlerOption_Quality) ? "是" : "否");
    XPrintf("jumpToNextImage (默认虚函数) = %s (期望:否)\n", XImageIOHandler_jumpToNextImage_base(&handler) ? "是" : "否");
    XPrintf("jumpToImage (默认虚函数) = %s (期望:否)\n", XImageIOHandler_jumpToImage_base(&handler, 0) ? "是" : "否");
    XPrintf("loopCount (默认虚函数) = %d (期望:0)\n", XImageIOHandler_loopCount_base(&handler));
    XPrintf("imageCount (默认虚函数) = %d (期望:0)\n", XImageIOHandler_imageCount_base(&handler));
    XPrintf("nextImageDelay (默认虚函数) = %d (期望:0)\n", XImageIOHandler_nextImageDelay_base(&handler));
    XPrintf("currentImageNumber (默认虚函数) = %d (期望:0)\n", XImageIOHandler_currentImageNumber_base(&handler));
    XImageIOHandler_deinit_base(&handler);
    XPrintf("\n");
}

/**
 * @brief      静态工具测试
 */
static void XImageIOHandlerStaticTest(void)
{
    XPrintf("===== 静态工具测试 =====\n");
    {
        XImage img;
        XImage_init(&img);
        XSize size = {100, 200};
        bool ok = XImageIOHandler_allocateImage(&size, XImageFormat_ARGB32, &img);
        XPrintf("allocateImage(100,200,ARGB32): ok=%s (期望:是), w=%d, h=%d\n",
            ok ? "是" : "否", XImage_width(&img), XImage_height(&img));
        XImage_deinit_base(&img);
    }
    {
        XImage img;
        XImage_init(&img);
        XSize size = {0, 0};
        bool ok = XImageIOHandler_allocateImage(&size, XImageFormat_ARGB32, &img);
        XPrintf("allocateImage(0,0): ok=%s (期望:否)\n", ok ? "是" : "否");
        XImage_deinit_base(&img);
    }
    XPrintf("\n");
}

/**
 * @brief      XImageIOHandler 综合测试入口
 */
void XMenu_XImageIOHandlerTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XImageIOHandler IO处理器");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XImageIOHandlerCreateTest);

    action = XMenu_addAction(menu, "设备管理测试");
    XAction_setAction(action, XImageIOHandlerDeviceTest);

    action = XMenu_addAction(menu, "虚函数默认行为测试");
    XAction_setAction(action, XImageIOHandlerVirtualTest);

    action = XMenu_addAction(menu, "静态工具测试");
    XAction_setAction(action, XImageIOHandlerStaticTest);
}
#endif // DEMOTEST


