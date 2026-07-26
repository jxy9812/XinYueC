#include "XGuiTest.h"
#if DEMOTEST
#include "XPicture.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ==================== XPicture 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XPictureCreateTest(void)
{
    XPrintf("===== 创建与初始化测试 =====\n");
    /* create */
    {
        XPicture* pic = XPicture_create();
        XPrintf("create(): isNull=%s (期望:是)\n", XPicture_isNull(pic) ? "是" : "否");
        XPicture_delete_base(pic);
    }
    /* init */
    {
        XPicture pic;
        XPicture_init(&pic, -1);
        XPrintf("init(-1): isNull=%s (期望:是)\n", XPicture_isNull(&pic) ? "是" : "否");
        XPrintf("size=%u (期望:0)\n", XPicture_size(&pic));
        XPicture_deinit_base(&pic);
    }
    /* init 指定版本 */
    {
        XPicture pic;
        XPicture_init(&pic, 6);
        XPrintf("init(6): isNull=%s, size=%u\n", XPicture_isNull(&pic) ? "是" : "否", XPicture_size(&pic));
        XPicture_deinit_base(&pic);
    }
    XPrintf("\n");
}

/**
 * @brief      拷贝测试
 */
static void XPictureCopyTest(void)
{
    XPrintf("===== 拷贝测试 =====\n");
    {
        XPicture a, b;
        XPicture_init(&a, -1);
        XPicture_init(&b, -1);
        XPicture_copy(&b, &a);
        XPrintf("copy: b.isNull=%s (期望:是)\n", XPicture_isNull(&b) ? "是" : "否");
        XPicture_deinit_base(&a);
        XPicture_deinit_base(&b);
    }
    XPrintf("\n");
}

/**
 * @brief      边界矩形与数据测试
 */
static void XPictureRectDataTest(void)
{
    XPrintf("===== 边界矩形与数据测试 =====\n");
    XPicture pic;
    XPicture_init(&pic, -1);
    /* 设置边界矩形 */
    {
        XRect rect = {10, 20, 100, 200};
        XPicture_setBoundingRect(&pic, &rect);
        XRect out;
        XPicture_boundingRect(&pic, &out);
        XPrintf("boundingRect: x=%d (期望:10), y=%d (期望:20), w=%d (期望:100), h=%d (期望:200)\n",
            out.x, out.y, out.width, out.height);
    }
    /* 设置数据 */
    {
        const char* data = "test data";
        XPicture_setData(&pic, data, 9);
        XPrintf("setData: size=%u (期望:9), isNull=%s (期望:否)\n",
            XPicture_size(&pic), XPicture_isNull(&pic) ? "是" : "否");
        const char* outData = XPicture_data(&pic);
        XPrintf("data=%s (期望:test data)\n", outData ? outData : "NULL");
    }
    /* 分离检测 */
    XPrintf("isDetached=%s (期望:是)\n", XPicture_isDetached(&pic) ? "是" : "否");
    XPicture_detach(&pic);
    XPrintf("detach后: isDetached=%s\n", XPicture_isDetached(&pic) ? "是" : "否");
    XPicture_deinit_base(&pic);
    XPrintf("\n");
}

static void XPictureCowFileRegressionTest(void)
{
    const char* fileName = "xpicture_regression_test.bin";
    XPicture a, b, loaded;
    XRect original = {1, 2, 30, 40};
    XRect changed = {5, 6, 70, 80};
    XRect actual;
    XPicture_init(&a, -1);
    XPicture_init(&b, -1);
    XPicture_init(&loaded, -1);
    XPicture_setData(&a, "original", 8);
    XPicture_setBoundingRect(&a, &original);
    XPicture_copy(&b, &a);
    assert(!XPicture_isDetached(&a));
    XPicture_setData(&b, "changed", 7);
    XPicture_setBoundingRect(&b, &changed);
    assert(XPicture_size(&a) == 8 && memcmp(XPicture_data(&a), "original", 8) == 0);
    assert(XPicture_size(&b) == 7 && memcmp(XPicture_data(&b), "changed", 7) == 0);
    XPicture_boundingRect(&a, &actual);
    assert(memcmp(&actual, &original, sizeof(XRect)) == 0);
    assert(XPicture_save(&b, fileName));
    assert(XPicture_load(&loaded, fileName));
    assert(XPicture_size(&loaded) == 7 && memcmp(XPicture_data(&loaded), "changed", 7) == 0);
    remove(fileName);
    XPicture_deinit_base(&loaded);
    XPicture_deinit_base(&b);
    XPicture_deinit_base(&a);
    XPrintf("XPicture COW/file regression: PASS\n");
}

/**
 * @brief      XPicture 综合测试入口
 */
void XMenu_XPictureTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XPicture 绘图记录类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XPictureCreateTest);

    action = XMenu_addAction(menu, "拷贝测试");
    XAction_setAction(action, XPictureCopyTest);

    action = XMenu_addAction(menu, "边界矩形与数据测试");
    XAction_setAction(action, XPictureRectDataTest);

    action = XMenu_addAction(menu, "COW与文件回归测试");
    XAction_setAction(action, XPictureCowFileRegressionTest);
}
#endif // DEMOTEST


