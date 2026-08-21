#include "XGuiTest.h"
#if DEMOTEST
#include "XImageWriter.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"

/* ==================== XImageWriter 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XImageWriterCreateTest(void)
{
    XPrintf("===== XImageWriter 创建与初始化测试 =====\n");
    /* create */
    {
        XImageWriter* writer = XImageWriter_create();
        XPrintf("create(): writer=%s\n", writer ? "非NULL" : "NULL");
        XImageWriter_deinit_base(writer);
        XFree_System(writer);
    }
    /* init */
    {
        XImageWriter writer;
        XImageWriter_init(&writer);
        XPrintf("init(): format=%s (期望:NULL), quality=%d (期望:-1)\n",
            XImageWriter_format(&writer) ? XImageWriter_format(&writer) : "NULL",
            XImageWriter_quality(&writer));
        XImageWriter_deinit_base(&writer);
    }
    /* init_file */
    {
        XImageWriter writer;
        XImageWriter_init_file_2(&writer, "output.png", "PNG");
        XPrintf("init_file('output.png','PNG'): fileName=%s, format=%s\n",
            XImageWriter_fileName(&writer) ? XImageWriter_fileName(&writer) : "NULL",
            XImageWriter_format(&writer) ? XImageWriter_format(&writer) : "NULL");
        XImageWriter_deinit_base(&writer);
    }
    XPrintf("\n");
}

/**
 * @brief      格式与参数设置测试
 */
static void XImageWriterParamTest(void)
{
    XPrintf("===== 格式与参数设置测试 =====\n");
    XImageWriter writer;
    XImageWriter_init(&writer);
    /* 格式 */
    XImageWriter_setFormat_2(&writer, "JPEG");
    XPrintf("setFormat('JPEG'): format=%s (期望:JPEG)\n", XImageWriter_format(&writer));
    /* 质量 */
    XImageWriter_setQuality(&writer, 90);
    XPrintf("setQuality(90): quality=%d (期望:90)\n", XImageWriter_quality(&writer));
    /* 压缩 */
    XImageWriter_setCompression(&writer, 5);
    XPrintf("setCompression(5): compression=%d (期望:5)\n", XImageWriter_compression(&writer));
    /* 子类型 */
    XImageWriter_setSubType_2(&writer, "baseline");
    XPrintf("setSubType('baseline'): subType=%s\n", XImageWriter_subType(&writer) ? XImageWriter_subType(&writer) : "NULL");
    /* 优化写入 */
    XImageWriter_setOptimizedWrite(&writer, true);
    XPrintf("setOptimizedWrite(true): optimized=%s (期望:是)\n", XImageWriter_optimizedWrite(&writer) ? "是" : "否");
    /* 渐进式扫描 */
    XImageWriter_setProgressiveScanWrite(&writer, true);
    XPrintf("setProgressiveScanWrite(true): progressive=%s (期望:是)\n", XImageWriter_progressiveScanWrite(&writer) ? "是" : "否");
    /* 变换 */
    XImageWriter_setTransformation(&writer, XImageIOHandlerTransformation_Rotate90);
    XPrintf("setTransformation(Rotate90): transform=%d (期望:4)\n", XImageWriter_transformation(&writer));
    /* 文件名 */
    XImageWriter_setFileName_2(&writer, "output.jpg");
    XPrintf("setFileName('output.jpg'): fileName=%s\n", XImageWriter_fileName(&writer) ? XImageWriter_fileName(&writer) : "NULL");
    XImageWriter_deinit_base(&writer);
    XPrintf("\n");
}

/**
 * @brief      写入与错误处理测试
 */
static void XImageWriterWriteTest(void)
{
    XPrintf("===== 写入与错误处理测试 =====\n");
    XImageWriter writer;
    XImageWriter_init(&writer);
    /* canWrite 应该返回 false (无设备无文件) */
    XPrintf("canWrite (无设备) = %s (期望:否)\n", XImageWriter_canWrite(&writer) ? "是" : "否");
    /* write 应该失败 */
    {
        XImage img;
        XImage_init_ex(&img, 10, 10, XImageFormat_ARGB32);
        bool ok = XImageWriter_write(&writer, &img);
        XPrintf("write (无设备) = %s (期望:否)\n", ok ? "是" : "否");
        XImage_deinit_base(&img);
    }
    /* 错误信息 */
    XPrintf("error=%d, errorString=%s\n", XImageWriter_error(&writer),
        XImageWriter_errorString(&writer) ? XImageWriter_errorString(&writer) : "NULL");
    XImageWriter_deinit_base(&writer);
    XPrintf("\n");
}

/**
 * @brief      XImageWriter 综合测试入口
 */
void XMenu_XImageWriterTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XImageWriter 图像写入器");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XImageWriterCreateTest);

    action = XMenu_addAction(menu, "格式与参数设置测试");
    XAction_setAction(action, XImageWriterParamTest);

    action = XMenu_addAction(menu, "写入与错误处理测试");
    XAction_setAction(action, XImageWriterWriteTest);
}
#endif // DEMOTEST

