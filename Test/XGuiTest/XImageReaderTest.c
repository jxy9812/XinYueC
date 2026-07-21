#include "XGuiTest.h"
#if DEMOTEST
#include "XImageReader.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"

/* ==================== XImageReader 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XImageReaderCreateTest(void)
{
    XPrintf("===== XImageReader 创建与初始化测试 =====\n");
    /* create */
    {
        XImageReader* reader = XImageReader_create();
        XPrintf("create(): reader=%s\n", reader ? "非NULL" : "NULL");
        XImageReader_deinit_base(reader);
        XFree_System(reader);
    }
    /* init */
    {
        XImageReader reader;
        XImageReader_init(&reader);
        XPrintf("init(): format=%s (期望:NULL), autoDetect=%s (期望:是)\n",
            XImageReader_format(&reader) ? XImageReader_format(&reader) : "NULL",
            XImageReader_autoDetectImageFormat(&reader) ? "是" : "否");
        XImageReader_deinit_base(&reader);
    }
    XPrintf("\n");
}

/**
 * @brief      格式设置测试
 */
static void XImageReaderFormatTest(void)
{
    XPrintf("===== 格式设置测试 =====\n");
    XImageReader reader;
    XImageReader_init(&reader);
    XImageReader_setFormat(&reader, "PNG");
    XPrintf("setFormat('PNG'): format=%s (期望:PNG)\n", XImageReader_format(&reader));
    XImageReader_setAutoDetectImageFormat(&reader, false);
    XPrintf("setAutoDetect(false): autoDetect=%s (期望:否)\n", XImageReader_autoDetectImageFormat(&reader) ? "是" : "否");
    XImageReader_setAutoDetectImageFormat(&reader, true);
    XPrintf("setAutoDetect(true): autoDetect=%s (期望:是)\n", XImageReader_autoDetectImageFormat(&reader) ? "是" : "否");
    XImageReader_setDecideFormatFromContent(&reader, true);
    XPrintf("setDecideFormatFromContent(true): decideFromContent=%s\n", XImageReader_decideFormatFromContent(&reader) ? "是" : "否");
    XImageReader_deinit_base(&reader);
    XPrintf("\n");
}

/**
 * @brief      文件名与裁剪尺寸测试
 */
static void XImageReaderFileTest(void)
{
    XPrintf("===== 文件名与裁剪测试 =====\n");
    XImageReader reader;
    XImageReader_init(&reader);
    /* 设置文件名 */
    XImageReader_setFileName(&reader, "test.png");
    XPrintf("setFileName('test.png'): fileName=%s\n", XImageReader_fileName(&reader) ? XImageReader_fileName(&reader) : "NULL");
    /* 设置裁剪矩形 */
    XRect clipRect = {10, 10, 50, 50};
    XImageReader_setClipRect(&reader, &clipRect);
    XRect out;
    XImageReader_clipRect(&reader, &out);
    XPrintf("setClipRect(10,10,50,50): x=%d, y=%d, w=%d (期望:50), h=%d (期望:50)\n", out.x, out.y, out.width, out.height);
    /* 设置缩放尺寸 */
    XSize sz = {100, 200}; XImageReader_setScaledSize(&reader, &sz);
    XSize scaledOut;
    XImageReader_scaledSize(&reader, &scaledOut);
    XPrintf("setScaledSize(100,200): w=%d (期望:100), h=%d (期望:200)\n", scaledOut.width, scaledOut.height);
    /* 设置质量 */
    XImageReader_setQuality(&reader, 85);
    XPrintf("setQuality(85): quality=%d (期望:85)\n", XImageReader_quality(&reader));
    XImageReader_deinit_base(&reader);
    XPrintf("\n");
}

/**
 * @brief      读取操作与错误处理测试
 */
static void XImageReaderReadTest(void)
{
    XPrintf("===== 读取操作与错误处理测试 =====\n");
    XImageReader reader;
    XImageReader_init(&reader);
    /* canRead 应该返回 false (无设备) */
    XPrintf("canRead (无设备) = %s (期望:否)\n", XImageReader_canRead(&reader) ? "是" : "否");
    /* read 应该失败 */
    {
        XImage img;
        XImage_init(&img);
        bool ok = XImageReader_read(&reader, &img);
        XPrintf("read (无设备) = %s (期望:否)\n", ok ? "是" : "否");
        XImage_deinit_base(&img);
    }
    /* 错误信息 */
    XPrintf("error=%d, errorString=%s\n", XImageReader_error(&reader),
        XImageReader_errorString(&reader) ? XImageReader_errorString(&reader) : "NULL");
    /* 帧操作 */
    XPrintf("supportsAnimation=%s (期望:否)\n", XImageReader_supportsAnimation(&reader) ? "是" : "否");
    XPrintf("imageCount=%d (期望:0)\n", XImageReader_imageCount(&reader));
    XPrintf("loopCount=%d (期望:0)\n", XImageReader_loopCount(&reader));
    XPrintf("currentImageNumber=%d (期望:0)\n", XImageReader_currentImageNumber(&reader));
    XImageReader_deinit_base(&reader);
    XPrintf("\n");
}

/**
 * @brief      静态方法测试
 */
static void XImageReaderStaticTest(void)
{
    XPrintf("===== 静态方法测试 =====\n");
    /* 支持的格式 */
    void* formats = XImageReader_supportedImageFormats();
    XPrintf("supportedImageFormats=%s\n", formats ? "非NULL" : "NULL");
    /* 支持的 MIME 类型 */
    void* mimeTypes = XImageReader_supportedMimeTypes();
    XPrintf("supportedMimeTypes=%s\n", mimeTypes ? "非NULL" : "NULL");
    /* 内存分配限制 */
    int limit = XImageReader_allocationLimit();
    XPrintf("allocationLimit=%d\n", limit);
    XImageReader_setAllocationLimit(128);
    XPrintf("setAllocationLimit(128): limit=%d (期望:128)\n", XImageReader_allocationLimit());
    XImageReader_setAllocationLimit(limit);
    XPrintf("\n");
}

/**
 * @brief      XImageReader 综合测试入口
 */
void XMenu_XImageReaderTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XImageReader 图像读取器");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XImageReaderCreateTest);

    action = XMenu_addAction(menu, "格式设置测试");
    XAction_setAction(action, XImageReaderFormatTest);

    action = XMenu_addAction(menu, "文件名与裁剪测试");
    XAction_setAction(action, XImageReaderFileTest);

    action = XMenu_addAction(menu, "读取操作与错误处理测试");
    XAction_setAction(action, XImageReaderReadTest);

    action = XMenu_addAction(menu, "静态方法测试");
    XAction_setAction(action, XImageReaderStaticTest);
}
#endif // DEMOTEST


