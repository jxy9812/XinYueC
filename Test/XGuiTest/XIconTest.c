#include "XGuiTest.h"
#if DEMOTEST
#include "XIcon.h"
#include "XPixmap.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"

/* ==================== XIcon 测试 ==================== */

/**
 * @brief      创建与初始化测试
 */
static void XIconCreateTest(void)
{
    XPrintf("===== 创建与初始化测试 =====\n");
    /* create 空 */
    {
        XIcon* icon = XIcon_create();
        XPrintf("create(): isNull=%s (期望:是)\n", XIcon_isNull(icon) ? "是" : "否");
        XIcon_delete_base(icon);
    }
    /* init 空 */
    {
        XIcon icon;
        XIcon_init(&icon);
        XPrintf("init(): isNull=%s (期望:是)\n", XIcon_isNull(&icon) ? "是" : "否");
        XIcon_deinit_base(&icon);
    }
    /* init_pixmap */
    {
        XPixmap p;
        XPixmap_init_ex(&p, 32, 32);
        XIcon icon;
        XIcon_init_pixmap(&icon, &p);
        XPrintf("init_pixmap(32x32): isNull=%s (期望:否)\n", XIcon_isNull(&icon) ? "是" : "否");
        XIcon_deinit_base(&icon);
        XPixmap_deinit_base(&p);
    }
    XPrintf("\n");
}

/**
 * @brief      拷贝与移动测试
 */
static void XIconCopyMoveTest(void)
{
    XPrintf("===== 拷贝与移动测试 =====\n");
    {
        XPixmap p;
        XPixmap_init_ex(&p, 16, 16);
        XIcon a, b;
        XIcon_init_pixmap(&a, &p);
        XIcon_init(&b);
        XIcon_copy(&b, &a);
        XPrintf("copy: b.isNull=%s (期望:否)\n", XIcon_isNull(&b) ? "是" : "否");
        XIcon_deinit_base(&a);
        XIcon_deinit_base(&b);
        XPixmap_deinit_base(&p);
    }
    {
        XPixmap p;
        XPixmap_init_ex(&p, 24, 24);
        XIcon a, b;
        XIcon_init_pixmap(&a, &p);
        XIcon_init(&b);
        XIcon_move(&b, &a);
        XPrintf("move: b.isNull=%s (期望:否), a.isNull=%s (期望:是)\n",
            XIcon_isNull(&b) ? "是" : "否", XIcon_isNull(&a) ? "是" : "否");
        XIcon_deinit_base(&a);
        XIcon_deinit_base(&b);
        XPixmap_deinit_base(&p);
    }
    XPrintf("\n");
}

/**
 * @brief      添加资源与像素图获取测试
 */
static void XIconPixmapTest(void)
{
    XPrintf("===== 添加资源与像素图获取测试 =====\n");
    XPixmap p;
    XPixmap_init_ex(&p, 48, 48);
    XIcon icon;
    XIcon_init_pixmap(&icon, &p);
    /* 添加更多像素图 */
    {
        XPixmap p2;
        XPixmap_init_ex(&p2, 24, 24);
        XIcon_addPixmap(&icon, &p2, XIconMode_Disabled, XIconState_Off);
        XPixmap_deinit_base(&p2);
    }
    /* 获取像素图 */
    {
        XPixmap out;
        XPixmap_init(&out);
        XIcon_pixmap(&icon, 48, 48, XIconMode_Normal, XIconState_Off, &out);
        XPrintf("pixmap(48,48,Normal,Off): w=%d (期望:48), null=%s\n",
            XPixmap_width(&out), XPixmap_isNull(&out) ? "是" : "否");
        XPixmap_deinit_base(&out);
    }
    /* 获取正方形像素图 */
    {
        XPixmap out;
        XPixmap_init(&out);
        XIcon_pixmapExtent(&icon, 32, XIconMode_Normal, XIconState_Off, &out);
        XPrintf("pixmapExtent(32): w=%d (期望:32)\n", XPixmap_width(&out));
        XPixmap_deinit_base(&out);
    }
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&p);
    XPrintf("\n");
}

/**
 * @brief      掩码与缓存测试
 */
static void XIconExtraTest(void)
{
    XPrintf("===== 掩码与缓存测试 =====\n");
    XPixmap p;
    XPixmap_init_ex(&p, 32, 32);
    XIcon icon;
    XIcon_init_pixmap(&icon, &p);
    XPrintf("isMask=%s (期望:否)\n", XIcon_isMask(&icon) ? "是" : "否");
    XIcon_setIsMask(&icon, true);
    XPrintf("setIsMask(true): isMask=%s (期望:是)\n", XIcon_isMask(&icon) ? "是" : "否");
    XPrintf("cacheKey=%lld (期望:非0)\n", XIcon_cacheKey(&icon));
    XPrintf("isDetached=%s (期望:是)\n", XIcon_isDetached(&icon) ? "是" : "否");
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&p);
    XPrintf("\n");
}

/**
 * @brief      XIcon 综合测试入口
 */
void XMenu_XIconTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XIcon 图标类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "创建与初始化测试");
    XAction_setAction(action, XIconCreateTest);

    action = XMenu_addAction(menu, "拷贝与移动测试");
    XAction_setAction(action, XIconCopyMoveTest);

    action = XMenu_addAction(menu, "添加资源与像素图获取测试");
    XAction_setAction(action, XIconPixmapTest);

    action = XMenu_addAction(menu, "掩码与缓存测试");
    XAction_setAction(action, XIconExtraTest);
}
#endif // DEMOTEST


