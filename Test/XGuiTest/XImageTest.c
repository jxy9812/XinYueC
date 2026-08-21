#include "XGuiTest.h"
#if DEMOTEST
#include "XImage.h"
#include "XImageFormat.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XMemory.h"
#include <string.h>

/* ==================== XImage 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XImageCreateTest(void)
{
    XPrintf("===== 创建与初始化测试 =====\n");
    /* create 空图像 */
    {
        XImage* img = XImage_create();
        XPrintf("create(): isNull=%s (期望:是)\n", XImage_isNull(img) ? "是" : "否");
        XImage_delete_base(img);
    }
    /* init 空图像 */
    {
        XImage img;
        XImage_init(&img);
        XPrintf("init(): isNull=%s (期望:是)\n", XImage_isNull(&img) ? "是" : "否");
        XImage_deinit_base(&img);
    }
    /* init_ex 指定大小 */
    {
        XImage img;
        XImage_init_ex(&img, 100, 200, XImageFormat_ARGB32);
        XPrintf("init_ex(100,200,ARGB32): w=%d (期望:100), h=%d (期望:200), null=%s (期望:否)\n",
            XImage_width(&img), XImage_height(&img), XImage_isNull(&img) ? "是" : "否");
        XImage_deinit_base(&img);
    }
    /* init_ex 带外部数据 */
    {
        XImage img;
        uint8_t* data = (uint8_t*)XMalloc_System(400 * 100);
        XImage_init_ex_2(&img, 100, 100, XImageFormat_RGB32, 0, data, XFree_System, data);
        XPrintf("init_ex_2(100,100,RGB32,external): w=%d, h=%d, null=%s (期望:否)\n",
            XImage_width(&img), XImage_height(&img), XImage_isNull(&img) ? "是" : "否");
        XImage_deinit_base(&img);
    }
    /* 无效参数 */
    {
        XImage img;
        XImage_init_ex(&img, 0, 0, XImageFormat_ARGB32);
        XPrintf("init_ex(0,0): isNull=%s (期望:是)\n", XImage_isNull(&img) ? "是" : "否");
        XImage_deinit_base(&img);
    }
    XPrintf("\n");
}

/**
 * @brief      拷贝与移动测试
 */
static void XImageCopyMoveTest(void)
{
    XPrintf("===== 拷贝与移动测试 =====\n");
    {
        XImage a, b;
        XImage_init_ex(&a, 10, 10, XImageFormat_ARGB32);
        XImage_init(&b);
        XImage_copy_base(&b, &a);
        XPrintf("copy: b.w=%d (期望:10), b.h=%d (期望:10), a.null=%s (期望:否)\n",
            XImage_width(&b), XImage_height(&b), XImage_isNull(&a) ? "是" : "否");
        XImage_deinit_base(&a);
        XImage_deinit_base(&b);
    }
    {
        XImage a, b;
        XImage_init_ex(&a, 20, 30, XImageFormat_RGB32);
        XImage_init(&b);
        XImage_move_base(&b, &a);
        XPrintf("move: b.w=%d (期望:20), b.h=%d (期望:30), a.isNull=%s (期望:是)\n",
            XImage_width(&b), XImage_height(&b), XImage_isNull(&a) ? "是" : "否");
        XImage_deinit_base(&a);
        XImage_deinit_base(&b);
    }
    XPrintf("\n");
}

/**
 * @brief      查询方法测试
 */
static void XImageQueryTest(void)
{
    XPrintf("===== 查询方法测试 =====\n");
    XImage img;
    XImage_init_ex(&img, 80, 60, XImageFormat_ARGB32_Premultiplied);
    XPrintf("width=%d (期望:80)\n", XImage_width(&img));
    XPrintf("height=%d (期望:60)\n", XImage_height(&img));
    XPrintf("depth=%d (期望:32)\n", XImage_depth(&img));
    XPrintf("format=%d (期望:6=ARGB32_Premultiplied)\n", XImage_format(&img));
    XPrintf("hasAlphaChannel=%s (期望:是)\n", XImage_hasAlphaChannel(&img) ? "是" : "否");
    XPrintf("allGray=%s\n", XImage_allGray(&img) ? "是" : "否");
    XPrintf("isNull=%s (期望:否)\n", XImage_isNull(&img) ? "是" : "否");
    XSize size;
    XImage_size(&img, &size);
    XPrintf("size: w=%d (期望:80), h=%d (期望:60)\n", size.width, size.height);
    XRect rect;
    XImage_rect(&img, &rect);
    XPrintf("rect: x=%d, y=%d, w=%d (期望:80), h=%d (期望:60)\n", rect.x, rect.y, rect.width, rect.height);
    XPrintf("bytesPerLine=%d\n", XImage_bytesPerLine(&img));
    XPrintf("sizeInBytes=%d\n", XImage_sizeInBytes(&img));
    XImage_deinit_base(&img);
    /* 空图像查询 */
    {
        XImage empty;
        XImage_init(&empty);
        XPrintf("空图像: width=%d (期望:0), height=%d (期望:0), isNull=%s (期望:是)\n",
            XImage_width(&empty), XImage_height(&empty), XImage_isNull(&empty) ? "是" : "否");
        XImage_deinit_base(&empty);
    }
    XPrintf("\n");
}

/**
 * @brief      像素操作测试
 */
static void XImagePixelTest(void)
{
    XPrintf("===== 像素操作测试 =====\n");
    XImage img;
    XImage_init_ex(&img, 5, 5, XImageFormat_ARGB32);
    /* 设置像素 */
    XImage_setPixel(&img, 0, 0, 0xFFFF0000);  /* 红色 */
    XImage_setPixel(&img, 1, 1, 0xFF00FF00);  /* 绿色 */
    XImage_setPixel(&img, 2, 2, 0xFF0000FF);  /* 蓝色 */
    XImage_setPixel(&img, 3, 3, 0xFFFFFFFF);  /* 白色 */
    XImage_setPixel(&img, 4, 4, 0xFF000000);  /* 黑色 */
    /* 读取像素 */
    XPrintf("pixel(0,0)=0x%08X (期望:0xFFFF0000)\n", XImage_pixel(&img, 0, 0));
    XPrintf("pixel(1,1)=0x%08X (期望:0xFF00FF00)\n", XImage_pixel(&img, 1, 1));
    XPrintf("pixel(2,2)=0x%08X (期望:0xFF0000FF)\n", XImage_pixel(&img, 2, 2));
    XPrintf("pixel(3,3)=0x%08X (期望:0xFFFFFFFF)\n", XImage_pixel(&img, 3, 3));
    XPrintf("pixel(4,4)=0x%08X (期望:0xFF000000)\n", XImage_pixel(&img, 4, 4));
    /* 越界读取 */
    XPrintf("pixel(-1,0)=0x%08X (期望:0)\n", XImage_pixel(&img, -1, 0));
    XPrintf("pixel(10,0)=0x%08X (期望:0)\n", XImage_pixel(&img, 10, 0));
    XImage_deinit_base(&img);
    XPrintf("\n");
}

/**
 * @brief      格式转换测试
 */
static void XImageConvertTest(void)
{
    XPrintf("===== 格式转换测试 =====\n");
    XImage src;
    XImage_init_ex(&src, 10, 10, XImageFormat_ARGB32);
    XImage_setPixel(&src, 0, 0, 0x80FF0000);
    /* 转换为 RGB32 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_convertToFormat(&src, XImageFormat_RGB32, 0, &dst);
        XPrintf("convertToFormat RGB32: w=%d (期望:10), h=%d (期望:10), format=%d (期望:4)\n",
            XImage_width(&dst), XImage_height(&dst), XImage_format(&dst));
        XImage_deinit_base(&dst);
    }
    /* 转换为 Grayscale8 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_convertToFormat(&src, XImageFormat_Grayscale8, 0, &dst);
        XPrintf("convertToFormat Grayscale8: w=%d, h=%d, format=%d (期望:24)\n",
            XImage_width(&dst), XImage_height(&dst), XImage_format(&dst));
        XImage_deinit_base(&dst);
    }
    XImage_deinit_base(&src);
    XPrintf("\n");
}

/**
 * @brief      缩放与变换测试
 */
static void XImageTransformTest(void)
{
    XPrintf("===== 缩放与变换测试 =====\n");
    XImage src;
    XImage_init_ex(&src, 20, 30, XImageFormat_ARGB32);
    XImage_setPixel(&src, 10, 15, 0xFFFF0000);
    /* 缩放 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_scaled(&src, 40, 60, 0, 0, &dst);
        XPrintf("scaled(40,60): w=%d (期望:40), h=%d (期望:60)\n", XImage_width(&dst), XImage_height(&dst));
        XImage_deinit_base(&dst);
    }
    /* 缩放至宽度 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_scaledToWidth(&src, 50, 0, &dst);
        XPrintf("scaledToWidth(50): w=%d (期望:50), h=%d (期望:75)\n", XImage_width(&dst), XImage_height(&dst));
        XImage_deinit_base(&dst);
    }
    /* 缩放至高度 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_scaledToHeight(&src, 60, 0, &dst);
        XPrintf("scaledToHeight(60): w=%d (期望:40), h=%d (期望:60)\n", XImage_width(&dst), XImage_height(&dst));
        XImage_deinit_base(&dst);
    }
    /* 镜像 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_mirrored(&src, true, false, &dst);
        XPrintf("mirrored(horizontal): w=%d (期望:20), h=%d (期望:30)\n", XImage_width(&dst), XImage_height(&dst));
        XImage_deinit_base(&dst);
    }
    /* RGB 交换 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_rgbSwapped(&src, &dst);
        XPrintf("rgbSwapped: w=%d, h=%d, null=%s (期望:否)\n", XImage_width(&dst), XImage_height(&dst), XImage_isNull(&dst) ? "是" : "否");
        XImage_deinit_base(&dst);
    }
    XImage_deinit_base(&src);
    XPrintf("\n");
}

/**
 * @brief      拷贝区域测试
 */
static void XImageCopyRectTest(void)
{
    XPrintf("===== 拷贝区域测试 =====\n");
    XImage src;
    XImage_init_ex(&src, 100, 100, XImageFormat_ARGB32);
    XImage_setPixel(&src, 10, 10, 0xFFFF0000);
    /* 拷贝区域 */
    {
        XRect rect = {5, 5, 20, 20};
        XImage dst;
        XImage_init(&dst);
        XImage_copyRect(&src, &rect, &dst);
        XPrintf("copyRect(5,5,20,20): w=%d (期望:20), h=%d (期望:20)\n", XImage_width(&dst), XImage_height(&dst));
        XImage_deinit_base(&dst);
    }
    /* 全图拷贝 */
    {
        XImage dst;
        XImage_init(&dst);
        XImage_copyRect(&src, NULL, &dst);
        XPrintf("copyRect(NULL): w=%d (期望:100), h=%d (期望:100)\n", XImage_width(&dst), XImage_height(&dst));
        XImage_deinit_base(&dst);
    }
    XImage_deinit_base(&src);
    XPrintf("\n");
}

/**
 * @brief      填充与清除测试
 */
static void XImageFillTest(void)
{
    XPrintf("===== 填充与清除测试 =====\n");
    XImage img;
    XImage_init_ex(&img, 10, 10, XImageFormat_ARGB32);
    XImage_fill(&img, 0xFFFF0000);
    XPrintf("fill(red): pixel(0,0)=0x%08X (期望:0xFFFF0000)\n", XImage_pixel(&img, 0, 0));
    XPrintf("fill(red): pixel(5,5)=0x%08X (期望:0xFFFF0000)\n", XImage_pixel(&img, 5, 5));
    /* 填充矩形区域 */
    XRect rect = {2, 2, 4, 4};
    XImage_fillRect(&img, &rect, 0xFF00FF00);
    XPrintf("fillRect(green 2,2,4,4): pixel(2,2)=0x%08X (期望:0xFF00FF00)\n", XImage_pixel(&img, 2, 2));
    XPrintf("fillRect: pixel(0,0)=0x%08X (期望:0xFFFF0000)\n", XImage_pixel(&img, 0, 0));
    /* 清除 */
    XImage_clear(&img, &rect, 0);
    XPrintf("clear: pixel(2,2)=0x%08X (期望:0)\n", XImage_pixel(&img, 2, 2));
    XImage_deinit_base(&img);
    XPrintf("\n");
}

/**
 * @brief      XImage 综合测试入口
 */
void XMenu_XImageTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XImage 图像类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XImageCreateTest);

    action = XMenu_addAction(menu, "拷贝与移动测试");
    XAction_setAction(action, XImageCopyMoveTest);

    action = XMenu_addAction(menu, "查询方法测试");
    XAction_setAction(action, XImageQueryTest);

    action = XMenu_addAction(menu, "像素操作测试");
    XAction_setAction(action, XImagePixelTest);

    action = XMenu_addAction(menu, "格式转换测试");
    XAction_setAction(action, XImageConvertTest);

    action = XMenu_addAction(menu, "缩放与变换测试");
    XAction_setAction(action, XImageTransformTest);

    action = XMenu_addAction(menu, "拷贝区域测试");
    XAction_setAction(action, XImageCopyRectTest);

    action = XMenu_addAction(menu, "填充与清除测试");
    XAction_setAction(action, XImageFillTest);
}
#endif // DEMOTEST

