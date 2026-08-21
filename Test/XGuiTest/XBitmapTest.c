#include "XGuiTest.h"
#if DEMOTEST
#include "XBitmap.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"

/* ==================== XBitmap 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XBitmapCreateTest(void)
{
    XPrintf("===== 创建与初始化测试 =====\n");
    /* create 空 */
    {
        XBitmap* b = XBitmap_create();
        XPrintf("create(): isNull=%s (期望:是)\n", XPixmap_isNull((XPixmap*)b) ? "是" : "否");
        XBitmap_delete_base(b);
    }
    /* init 空 */
    {
        XBitmap b;
        XBitmap_init(&b);
        XPrintf("init(): isNull=%s (期望:是)\n", XPixmap_isNull((XPixmap*)&b) ? "是" : "否");
        XBitmap_deinit_base(&b);
    }
    /* init_ex 指定大小 */
    {
        XBitmap b;
        XBitmap_init_ex(&b, 100, 200);
        XPrintf("init_ex(100,200): w=%d (期望:100), h=%d (期望:200), null=%s (期望:否)\n",
            XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b), XPixmap_isNull((XPixmap*)&b) ? "是" : "否");
        XPrintf("isQBitmap=%s (期望:是)\n", XPixmap_isQBitmap((XPixmap*)&b) ? "是" : "否");
        XBitmap_deinit_base(&b);
    }
    /* init_size */
    {
        XBitmap b;
        XSize size = {40, 50};
        XBitmap_init_size(&b, &size);
        XPrintf("init_size(40,50): w=%d (期望:40), h=%d (期望:50)\n", XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b));
        XBitmap_deinit_base(&b);
    }
    /* init_pixmap 从像素图 */
    {
        XPixmap p;
        XPixmap_init_ex(&p, 30, 40);
        XBitmap b;
        XBitmap_init_pixmap(&b, &p);
        XPrintf("init_pixmap: w=%d (期望:30), h=%d (期望:40)\n", XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b));
        XPixmap_deinit_base(&p);
        XBitmap_deinit_base(&b);
    }
    XPrintf("\n");
}

/**
 * @brief      拷贝测试
 */
static void XBitmapCopyTest(void)
{
    XPrintf("===== 拷贝测试 =====\n");
    {
        XBitmap a, b;
        XBitmap_init_ex(&a, 15, 25);
        XBitmap_init(&b);
        XBitmap_copy_base(&b, &a);
        XPrintf("copy: b.w=%d (期望:15), b.h=%d (期望:25)\n", XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b));
        XBitmap_deinit_base(&a);
        XBitmap_deinit_base(&b);
    }
    XPrintf("\n");
}

/**
 * @brief      清除与变换测试
 */
static void XBitmapOperationTest(void)
{
    XPrintf("===== 清除与变换测试 =====\n");
    XBitmap b;
    XBitmap_init_ex(&b, 20, 20);
    XBitmap_clear(&b);
    XPrintf("clear: w=%d (期望:20), null=%s (期望:否)\n", XPixmap_width((XPixmap*)&b), XPixmap_isNull((XPixmap*)&b) ? "是" : "否");
    /* 变换 */
    {
        XBitmap out;
        XBitmap_init(&out);
        XBitmap_transformed_2(&b, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, &out);
        XPrintf("transformed(identity): w=%d, h=%d\n", XPixmap_width((XPixmap*)&out), XPixmap_height((XPixmap*)&out));
        XBitmap_deinit_base(&out);
    }
    XBitmap_deinit_base(&b);
    XPrintf("\n");
}

/**
 * @brief      静态方法测试
 */
static void XBitmapStaticTest(void)
{
    XPrintf("===== 静态方法测试 =====\n");
    /* fromImage */
    {
        XImage img;
        XImage_init_ex(&img, 10, 10, XImageFormat_ARGB32);
        XBitmap b;
        XBitmap_init(&b);
        XBitmap_fromImage(&img, 0, &b);
        XPrintf("fromImage: w=%d (期望:10), h=%d (期望:10), null=%s\n",
            XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b), XPixmap_isNull((XPixmap*)&b) ? "是" : "否");
        XImage_deinit_base(&img);
        XBitmap_deinit_base(&b);
    }
    /* fromPixmap */
    {
        XPixmap src;
        XPixmap_init_ex(&src, 25, 35);
        XBitmap b;
        XBitmap_init(&b);
        XBitmap_fromPixmap(&src, &b);
        XPrintf("fromPixmap: w=%d (期望:25), h=%d (期望:35)\n", XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b));
        XPixmap_deinit_base(&src);
        XBitmap_deinit_base(&b);
    }
    /* fromData */
    {
        XBitmap b;
        XBitmap_init(&b);
        XSize size = {8, 8};
        uint8_t bits[] = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};
        XBitmap_fromData(&size, bits, XImageFormat_Mono, &b);
        XPrintf("fromData: w=%d (期望:8), h=%d (期望:8)\n", XPixmap_width((XPixmap*)&b), XPixmap_height((XPixmap*)&b));
        XBitmap_deinit_base(&b);
    }
    XPrintf("\n");
}

/**
 * @brief      XBitmap 综合测试入口
 */
void XMenu_XBitmapTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XBitmap 位图类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XBitmapCreateTest);

    action = XMenu_addAction(menu, "拷贝测试");
    XAction_setAction(action, XBitmapCopyTest);

    action = XMenu_addAction(menu, "清除与变换测试");
    XAction_setAction(action, XBitmapOperationTest);

    action = XMenu_addAction(menu, "静态方法测试");
    XAction_setAction(action, XBitmapStaticTest);
}
#endif // DEMOTEST
