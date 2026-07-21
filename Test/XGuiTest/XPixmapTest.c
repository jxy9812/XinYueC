#include "XGuiTest.h"
#if DEMOTEST
#include "XPixmap.h"
#include "XImage.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"

/* ==================== XPixmap 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XPixmapCreateTest(void)
{
    XPrintf("===== 创建与初始化测试 =====\n");
    /* create 空 */
    {
        XPixmap* p = XPixmap_create();
        XPrintf("create(): isNull=%s (期望:是)\n", XPixmap_isNull(p) ? "是" : "否");
        XPixmap_delete_base(p);
    }
    /* init 空 */
    {
        XPixmap p;
        XPixmap_init(&p);
        XPrintf("init(): isNull=%s (期望:是)\n", XPixmap_isNull(&p) ? "是" : "否");
        XPixmap_deinit_base(&p);
    }
    /* init_ex 指定大小 */
    {
        XPixmap p;
        XPixmap_init_ex(&p, 100, 200);
        XPrintf("init_ex(100,200): w=%d (期望:100), h=%d (期望:200), null=%s (期望:否)\n",
            XPixmap_width(&p), XPixmap_height(&p), XPixmap_isNull(&p) ? "是" : "否");
        XPixmap_deinit_base(&p);
    }
    /* init_size 使用 XSize */
    {
        XPixmap p;
        XSize size = {50, 75};
        XPixmap_init_size(&p, &size);
        XPrintf("init_size(50,75): w=%d (期望:50), h=%d (期望:75)\n", XPixmap_width(&p), XPixmap_height(&p));
        XPixmap_deinit_base(&p);
    }
    /* init_image 从 XImage */
    {
        XImage img;
        XImage_init_ex(&img, 30, 40, XImageFormat_ARGB32);
        XPixmap p;
        XPixmap_init_image(&p, &img, 0);
        XPrintf("init_image: w=%d (期望:30), h=%d (期望:40), null=%s (期望:否)\n",
            XPixmap_width(&p), XPixmap_height(&p), XPixmap_isNull(&p) ? "是" : "否");
        XImage_deinit_base(&img);
        XPixmap_deinit_base(&p);
    }
    XPrintf("\n");
}

/**
 * @brief      拷贝与移动测试
 */
static void XPixmapCopyMoveTest(void)
{
    XPrintf("===== 拷贝与移动测试 =====\n");
    {
        XPixmap a, b;
        XPixmap_init_ex(&a, 10, 10);
        XPixmap_init(&b);
        XPixmap_copy(&b, &a);
        XPrintf("copy: b.w=%d (期望:10), a.null=%s (期望:否)\n", XPixmap_width(&b), XPixmap_isNull(&a) ? "是" : "否");
        XPixmap_deinit_base(&a);
        XPixmap_deinit_base(&b);
    }
    {
        XPixmap a, b;
        XPixmap_init_ex(&a, 20, 30);
        XPixmap_init(&b);
        XPixmap_move(&b, &a);
        XPrintf("move: b.w=%d (期望:20), a.isNull=%s (期望:是)\n", XPixmap_width(&b), XPixmap_isNull(&a) ? "是" : "否");
        XPixmap_deinit_base(&a);
        XPixmap_deinit_base(&b);
    }
    XPrintf("\n");
}

/**
 * @brief      查询方法测试
 */
static void XPixmapQueryTest(void)
{
    XPrintf("===== 查询方法测试 =====\n");
    XPixmap p;
    XPixmap_init_ex(&p, 80, 60);
    XPrintf("width=%d (期望:80)\n", XPixmap_width(&p));
    XPrintf("height=%d (期望:60)\n", XPixmap_height(&p));
    XPrintf("depth=%d (期望:32)\n", XPixmap_depth(&p));
    XPrintf("defaultDepth=%d\n", XPixmap_defaultDepth());
    XPrintf("isNull=%s (期望:否)\n", XPixmap_isNull(&p) ? "是" : "否");
    XPrintf("isQBitmap=%s (期望:否)\n", XPixmap_isQBitmap(&p) ? "是" : "否");
    XSize size;
    XPixmap_size(&p, &size);
    XPrintf("size: w=%d (期望:80), h=%d (期望:60)\n", size.width, size.height);
    XRect rect;
    XPixmap_rect(&p, &rect);
    XPrintf("rect: x=%d, y=%d, w=%d (期望:80), h=%d (期望:60)\n", rect.x, rect.y, rect.width, rect.height);
    XPrintf("devicePixelRatio=%f (期望:1.0)\n", XPixmap_devicePixelRatio(&p));
    XPixmap_deinit_base(&p);
    /* 空图查询 */
    {
        XPixmap empty;
        XPixmap_init(&empty);
        XPrintf("空图: width=%d (期望:0), isNull=%s (期望:是)\n", XPixmap_width(&empty), XPixmap_isNull(&empty) ? "是" : "否");
        XPixmap_deinit_base(&empty);
    }
    XPrintf("\n");
}

/**
 * @brief      填充与掩码测试
 */
static void XPixmapFillTest(void)
{
    XPrintf("===== 填充与掩码测试 =====\n");
    XPixmap p;
    XPixmap_init_ex(&p, 20, 20);
    XPixmap_fill(&p, 0xFFFF0000);  /* 红色填充 */
    XPrintf("fill(red): w=%d, h=%d, null=%s (期望:否)\n", XPixmap_width(&p), XPixmap_height(&p), XPixmap_isNull(&p) ? "是" : "否");
    /* 创建掩码 */
    {
        XPixmap mask;
        XPixmap_init(&mask);
        XPixmap_createHeuristicMask(&p, true, &mask);
        XPrintf("createHeuristicMask: null=%s\n", XPixmap_isNull(&mask) ? "是" : "否");
        XPixmap_deinit_base(&mask);
    }
    /* 从颜色创建掩码 */
    {
        XPixmap mask;
        XPixmap_init(&mask);
        XPixmap_createMaskFromColor(&p, 0xFFFF0000, 0, &mask);
        XPrintf("createMaskFromColor: null=%s\n", XPixmap_isNull(&mask) ? "是" : "否");
        XPixmap_deinit_base(&mask);
    }
    XPixmap_deinit_base(&p);
    XPrintf("\n");
}

/**
 * @brief      缩放测试
 */
static void XPixmapScaleTest(void)
{
    XPrintf("===== 缩放测试 =====\n");
    XPixmap p;
    XPixmap_init_ex(&p, 20, 30);
    /* 缩放 */
    {
        XPixmap dst;
        XPixmap_init(&dst);
        XPixmap_scaled(&p, 40, 60, 0, 0, &dst);
        XPrintf("scaled(40,60): w=%d (期望:40), h=%d (期望:60)\n", XPixmap_width(&dst), XPixmap_height(&dst));
        XPixmap_deinit_base(&dst);
    }
    /* 缩放至宽度 */
    {
        XPixmap dst;
        XPixmap_init(&dst);
        XPixmap_scaledToWidth(&p, 50, 0, &dst);
        XPrintf("scaledToWidth(50): w=%d (期望:50), h=%d (期望:75)\n", XPixmap_width(&dst), XPixmap_height(&dst));
        XPixmap_deinit_base(&dst);
    }
    /* 缩放至高度 */
    {
        XPixmap dst;
        XPixmap_init(&dst);
        XPixmap_scaledToHeight(&p, 60, 0, &dst);
        XPrintf("scaledToHeight(60): w=%d (期望:40), h=%d (期望:60)\n", XPixmap_width(&dst), XPixmap_height(&dst));
        XPixmap_deinit_base(&dst);
    }
    XPixmap_deinit_base(&p);
    XPrintf("\n");
}

/**
 * @brief      转换测试
 */
static void XPixmapConvertTest(void)
{
    XPrintf("===== 转换测试 =====\n");
    XPixmap p;
    XPixmap_init_ex(&p, 30, 40);
    /* toImage */
    {
        XImage img;
        XImage_init(&img);
        XPixmap_toImage(&p, &img);
        XPrintf("toImage: w=%d (期望:30), h=%d (期望:40)\n", XImage_width(&img), XImage_height(&img));
        XImage_deinit_base(&img);
    }
    /* fromImage */
    {
        XImage img;
        XImage_init_ex(&img, 50, 60, XImageFormat_ARGB32);
        XPixmap dst;
        XPixmap_init(&dst);
        XPixmap_fromImage(&img, 0, &dst);
        XPrintf("fromImage: w=%d (期望:50), h=%d (期望:60)\n", XPixmap_width(&dst), XPixmap_height(&dst));
        XImage_deinit_base(&img);
        XPixmap_deinit_base(&dst);
    }
    XPixmap_deinit_base(&p);
    XPrintf("\n");
}

/**
 * @brief      XPixmap 综合测试入口
 */
void XMenu_XPixmapTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XPixmap 像素图类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XPixmapCreateTest);

    action = XMenu_addAction(menu, "拷贝与移动测试");
    XAction_setAction(action, XPixmapCopyMoveTest);

    action = XMenu_addAction(menu, "查询方法测试");
    XAction_setAction(action, XPixmapQueryTest);

    action = XMenu_addAction(menu, "填充与掩码测试");
    XAction_setAction(action, XPixmapFillTest);

    action = XMenu_addAction(menu, "缩放测试");
    XAction_setAction(action, XPixmapScaleTest);

    action = XMenu_addAction(menu, "转换测试");
    XAction_setAction(action, XPixmapConvertTest);
}
#endif // DEMOTEST


